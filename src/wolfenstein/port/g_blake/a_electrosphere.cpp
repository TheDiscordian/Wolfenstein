/*
** a_electrosphere.cpp
**
**---------------------------------------------------------------------------
** Copyright 2026 TheDiscordian
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
** Blake Stone Electro-Sphere movement (T_OfsBounce and friends).
**
*/

#include "actor.h"
#include "gamemap.h"
#include "id_ca.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_iwad.h"
#include "wl_net.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_sphere("ElectroSphere");

// Spawn variants: which axes the sphere prefers to bounce along.
enum { SPHERE_VERT, SPHERE_HORZ, SPHERE_DIAG };

static const dirtype sphere_opposite[9] =
	{west, southwest, south, southeast, east, northeast, north, northwest, nodir};

static bool SphereCheckSpot(AActor *ob, int x, int y)
{
	MapSpot spot = map->GetSpot(x, y, 0);
	// Walls and door cells block, even fully open doors.
	if(spot->tile)
		return false;
	return TrySpot(ob, spot);
}

// Sphere version of TryWalk: never opens doors, never passes door cells.
// Probes leave the actor untouched; a commit sets the travel distance.
static bool SphereTryWalk(AActor *ob, bool commit)
{
	static const signed char xd[8] = {1, 1, 0, -1, -1, -1, 0, 1};
	static const signed char yd[8] = {0, -1, -1, -1, 0, 1, 1, 1};

	if(ob->dir == nodir)
		return false;

	const int tx = ob->tilex + xd[ob->dir];
	const int ty = ob->tiley + yd[ob->dir];

	if(!SphereCheckSpot(ob, tx, ty))
		return false;
	if(ob->dir & 1) // diagonals also need both adjacent cardinals clear
	{
		if(!SphereCheckSpot(ob, tx, ob->tiley))
			return false;
		if(!SphereCheckSpot(ob, ob->tilex, ty))
			return false;
	}

	if(!commit)
		return true;

	ob->EnterZone(map->GetSpot(tx, ty, 0)->zone);
	ob->distance = TILEGLOBAL;
	return true;
}

// MoveObj without the player back-up: the sphere floats through the player's
// tile and damages with its arc instead.
static void SphereMoveObj(AActor *ob, int32_t move)
{
	static const signed char xd[8] = {1, 1, 0, -1, -1, -1, 0, 1};
	static const signed char yd[8] = {0, -1, -1, -1, 0, 1, 1, 1};

	if(ob->dir == nodir)
		return;

	ob->x += xd[ob->dir]*move;
	ob->y += yd[ob->dir]*move;
	ob->distance -= move;
}

static int RandomSphereDir(int variant)
{
	switch(variant)
	{
		case SPHERE_VERT: return ((pr_sphere() % 2) * 4) + 2; // north/south
		case SPHERE_HORZ: return (pr_sphere() % 2) * 4;       // east/west
		case SPHERE_DIAG: return ((pr_sphere() % 4) * 2) + 1;
	}
	return nodir;
}

// Pick a fresh direction, preferring the sphere's own variant axes.
static void SphereStartDir(AActor *ob, int variant)
{
	for(int loop = 0; loop < 3; ++loop)
	{
		ob->dir = static_cast<dirtype>(RandomSphereDir((variant + loop) % 3));
		if(SphereTryWalk(ob, true))
			break;
		ob->dir = sphere_opposite[ob->dir];
		if(SphereTryWalk(ob, true))
			break;
		ob->dir = nodir;
	}
}

static void CheckForcedMove(AActor *ob, int axis)
{
	const dirtype olddir = ob->dir;
	ob->dir = static_cast<dirtype>(RandomSphereDir(axis));
	if(!SphereTryWalk(ob, false))
	{
		ob->dir = sphere_opposite[ob->dir];
		if(!SphereTryWalk(ob, false))
			ob->dir = olddir;
	}
}

// True while every diagonal is blocked; commits the first open one otherwise.
static bool CheckTrappedDiag(AActor *ob)
{
	const dirtype orgdir = ob->dir;
	int d;
	for(d = northeast; d <= southeast; d += 2)
	{
		ob->dir = static_cast<dirtype>(d);
		if(SphereTryWalk(ob, false))
			break;
	}
	if(d > southeast)
	{
		ob->dir = orgdir;
		return true;
	}
	SphereTryWalk(ob, true);
	return false;
}

// A diagonal sphere forced onto a cardinal axis: resume diagonal movement
// when open space appears, otherwise 50/50 chance of swapping cardinal axis.
static bool MoveTrappedDiag(AActor *ob, int variant)
{
	if(variant != SPHERE_DIAG || (ob->dir & 1))
		return false;
	if(!CheckTrappedDiag(ob))
		return true;
	if(pr_sphere() & 1)
		return false;
	CheckForcedMove(ob, (ob->dir == north || ob->dir == south) ? SPHERE_HORZ : SPHERE_VERT);
	return SphereTryWalk(ob, true);
}

// Per-tic thinker: arc damage plus the bounce movement loop.
ACTION_FUNCTION(A_SphereBounce)
{
	ACTION_PARAM_INT(variant, 0);

	// Arc the player when within one tile (Chebyshev distance).
	for(unsigned int i = 0; i < Net::InitVars.numPlayers; ++i)
	{
		AActor *p = players[i].mo;
		if(!p || !(p->flags & FL_SHOOTABLE))
			continue;
		const fixed dx = abs(p->x - self->x);
		const fixed dy = abs(p->y - self->y);
		if((dx > dy ? dx : dy) < TILEGLOBAL)
		{
			// DOS plays the arc-zap (ELECARCDAMAGESND), not the shot sample
			// (3d_act2.cpp:1042).
			PlaySoundLocActor("barrier/zap", self);
			// bstone arcs 4/tic at 70 Hz; this thinker runs once per sim step,
			// so scale by simStepMult to keep the health-drain rate.
			DamageActor(p, self, 4 * simStepMult);
		}
	}

	// Safety net: also picks the initial direction on the first tic.
	if(self->dir == nodir)
	{
		SphereStartDir(self, variant);
		if(self->dir == nodir)
			return true;
	}

	// bstone T_OfsBounce travels speed*tics per game-tic; under the device fixed
	// step this thinker runs once per sim step (simStepMult tics), so scale to
	// keep the bounce speed (the native chase code does the same, wl_act2.cpp).
	int32_t move = self->speed * simStepMult;
	while(move)
	{
		if(move < self->distance)
		{
			SphereMoveObj(self, move);
			break;
		}

		// Align on the tile centre, then pick the next tile.
		self->x = ((fixed)self->tilex<<TILESHIFT)+TILEGLOBAL/2;
		self->y = ((fixed)self->tiley<<TILESHIFT)+TILEGLOBAL/2;
		move -= self->distance;
		self->distance = TILEGLOBAL;

		if(!SphereTryWalk(self, true))
		{
			bool check_opposite = false;

			switch(self->dir)
			{
				case northeast:
				case northwest:
				case southeast:
				case southwest:
					if(variant != SPHERE_DIAG)
					{
						if(!MoveTrappedDiag(self, variant))
							SphereStartDir(self, variant);
						continue;
					}

					self->dir = static_cast<dirtype>((self->dir + 2) % 8); // 90 degrees left
					if(SphereTryWalk(self, false))
						break;
					self->dir = sphere_opposite[self->dir]; // 90 degrees right
					if(SphereTryWalk(self, false))
						break;
					self->dir = static_cast<dirtype>((self->dir + 2) % 8); // cornered
					check_opposite = true;
					break;

				case north:
				case south:
					if(variant != SPHERE_VERT)
					{
						if(!MoveTrappedDiag(self, variant))
							SphereStartDir(self, variant);
						continue;
					}
					check_opposite = true;
					break;

				case east:
				case west:
					if(variant != SPHERE_HORZ)
					{
						if(!MoveTrappedDiag(self, variant))
							SphereStartDir(self, variant);
						continue;
					}
					check_opposite = true;
					break;

				default:
					break;
			}

			if(check_opposite)
			{
				self->dir = sphere_opposite[self->dir];
				if(!SphereTryWalk(self, false))
					self->dir = nodir;
			}
			if(!SphereTryWalk(self, true))
				self->dir = nodir;
		}
		else
		{
			// A clear path still lets a trapped diagonal swerve back.
			MoveTrappedDiag(self, variant);
		}
	}

	return true;
}

// Every 10 tics the sphere shows a random different roam frame. AOG roams
// three frames; PS also cycles the ouch sprite as a fourth.
ACTION_FUNCTION(A_SphereRoamFrame)
{
	static const char* const roam[4] = {"RoamA", "RoamB", "RoamC", "RoamE"};

	const int frames = IWad::GetGame().Name.CompareNoCase("Planet Strike") == 0 ? 4 : 3;

	// caller's frame letter tells us which roam frame just finished (E = 4)
	int cur = caller->frame >= 4 ? 3 : caller->frame;
	if(cur >= frames)
		cur = 0;

	int pick = pr_sphere() % (frames - 1);
	if(pick >= cur)
		pick++;

	const Frame *frame = self->FindState(roam[pick]);
	if(!frame)
		return false;

	if(result)
		result->JumpFrame = frame;
	else
		self->SetState(frame);
	return false;
}
