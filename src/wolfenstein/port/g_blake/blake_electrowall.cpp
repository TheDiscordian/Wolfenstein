/*
** blake_electrowall.cpp
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
** Planet Strike electro-alien spawning walls (bstone CheckSpawnEA / the tile-24
** PLASMSP wall + 0xFA quantity byte).  Certain PS walls continuously emit
** ElectroAliens, up to a per-skill cap, while the player is in a connected area;
** killing one frees a slot.  Modelled on blake_goldstern.cpp, the canonical Blake
** per-sim-step spawner.
**
*/

#include "wl_def.h"
#include "blake_electrowall.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "gamemap.h"
#include "m_random.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_ea("ElectroWall");

// Wait times are 70Hz tics, matching the once-per-sim-step ElectroWall_Tick.
enum { MAXEAWALLS = 12 };

static struct
{
	unsigned short tilex, tiley;
	int aliensOut;	// live aliens from this wall (capped per skill)
	int delay;		// tics until the next spawn attempt
	int byteVal;	// 0xFA quantity byte, when fixed
	bool fixedByte;	// true once the 0xFA byte pinned the interval
} eaList[MAXEAWALLS];
static int numEAWalls;

// bstone no-byte interval: 8..30 seconds.
static int DefaultDelay() { return 60*8 + pr_ea(60*22); }

void ElectroWall_Clear()
{
	numEAWalls = 0;
}

void ElectroWall_AddWall(unsigned int x, unsigned int y)
{
	if(numEAWalls >= MAXEAWALLS)
		return;
	eaList[numEAWalls].tilex = (unsigned short)x;
	eaList[numEAWalls].tiley = (unsigned short)y;
	eaList[numEAWalls].aliensOut = 0;
	eaList[numEAWalls].delay = DefaultDelay();
	eaList[numEAWalls].byteVal = 0;
	eaList[numEAWalls].fixedByte = false;
	numEAWalls++;
}

void ElectroWall_SetDelay(unsigned int x, unsigned int y, int qtyByte)
{
	for(int w = 0;w < numEAWalls;++w)
		if(eaList[w].tilex == x && eaList[w].tiley == y)
		{
			eaList[w].byteVal = qtyByte & 0xFF;
			eaList[w].delay = 60 * eaList[w].byteVal;	// bstone 3d_game.cpp
			eaList[w].fixedByte = true;
			return;
		}
}

// actorat[] equivalent: a solid actor on the tile blocks the spawn.
static bool TileOccupied(int tx, int ty)
{
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if((ob->flags & FL_SOLID) && (int)ob->tilex == tx && (int)ob->tiley == ty)
			return true;
	}
	return false;
}

// bstone CheckSight(player, site): close range is automatic, otherwise the site
// must be in the player's facing half-plane (cardinals only) with a clear line.
// (Copied from blake_goldstern.cpp's PlayerSeesSite.)
static bool PlayerSeesSite(AActor *playerMo, int tx, int ty)
{
	static const fixed MINSIGHT = 0x18000l*64;

	const fixed x = ((fixed)tx<<TILESHIFT)+TILEGLOBAL/2;
	const fixed y = ((fixed)ty<<TILESHIFT)+TILEGLOBAL/2;
	const fixed deltax = x - playerMo->x;
	const fixed deltay = y - playerMo->y;

	if(abs(deltax) <= MINSIGHT && abs(deltay) <= MINSIGHT)
		return true;

	switch((playerMo->angle + ANGLE_45/2)/ANGLE_45 % 8)
	{
		case north: if(deltay > 0) return false; break;
		case east:  if(deltax < 0) return false; break;
		case south: if(deltay < 0) return false; break;
		case west:  if(deltax > 0) return false; break;
		default: break;
	}

	return CheckLine(playerMo, x, y, tx, ty);
}

void ElectroWall_Tick()
{
	if(!numEAWalls || !players[0].mo)
		return;

	AActor *pl = players[0].mo;
	unsigned int skill = gamestate.difficulty->SpawnFilter;
	if(skill > 3)
		skill = 3;

	static const int xy_offset[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
	static const ClassDef * const cls = ClassDef::FindClass("ElectroAlien");
	if(!cls)
		return;

	for(int w = 0;w < numEAWalls;++w)
	{
		if((unsigned int)eaList[w].aliensOut > skill)	// cap = skill + 1
			continue;
		if(eaList[w].delay > 0)
		{
			eaList[w].delay -= simStepMult;
			if(eaList[w].delay < 0)
				eaList[w].delay = 0;
			continue;
		}

		// Pick the first cardinal neighbour in an area the player can reach
		// (areabyplayer analog: same door-connected zone set).
		int nx = -1, ny = -1;
		for(int o = 0;o < 4;++o)
		{
			const int cx = eaList[w].tilex + xy_offset[o][0];
			const int cy = eaList[w].tiley + xy_offset[o][1];
			if(cx < 0 || cx > 63 || cy < 0 || cy > 63)
				continue;
			const MapZone *nz = map->GetSpot(cx, cy, 0)->zone;
			if(nz && map->CheckLink(nz, pl->GetZone(), true))
			{
				nx = cx;
				ny = cy;
				break;
			}
		}
		if(nx < 0)
			continue;
		if(TileOccupied(nx, ny))
			continue;
		if(abs((int)pl->tilex - nx) < 2 && abs((int)pl->tiley - ny) < 2)
			continue;
		if(!PlayerSeesSite(pl, nx, ny) && pr_ea() < 200)
			continue;

		AActor *a = AActor::Spawn(cls, ((fixed)nx<<TILESHIFT)+TILEGLOBAL/2,
			((fixed)ny<<TILESHIFT)+TILEGLOBAL/2, 0, SPAWN_AllowReplacement);
		if(!a)
			continue;

		// Dynamically-spawned electro aliens don't count toward the floor's kill
		// total (bstone never adds them to stats); undo Spawn's increment.
		if(a->flags & FL_COUNTKILL)
		{
			a->flags &= ~FL_COUNTKILL;
			--gamestate.killtotal;
		}
		a->temp1 = (short)(w + 1);	// +1 sentinel: which wall to credit on death

		eaList[w].aliensOut++;
		eaList[w].delay = eaList[w].fixedByte ? 60 * eaList[w].byteVal : DefaultDelay();
		break;	// one spawn per tick (bstone breaks)
	}
}

void Blake_ElectroWallSerialize(FArchive &arc)
{
	arc << numEAWalls;
	for(int w = 0;w < MAXEAWALLS;++w)
		arc << eaList[w].tilex << eaList[w].tiley << eaList[w].aliensOut
		    << eaList[w].delay << eaList[w].byteVal << eaList[w].fixedByte;
}

// On death, free the slot on the wall that spawned this alien (temp1 = wall + 1;
// map-placed electro aliens carry temp1 0 or an out-of-range value and are
// ignored).
ACTION_FUNCTION(A_ElectroWallDie)
{
	const int s = self->temp1;
	if(s > 0 && s <= numEAWalls && eaList[s-1].aliensOut > 0)
		eaList[s-1].aliensOut--;
	return true;
}
