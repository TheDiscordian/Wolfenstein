/*
** a_turret.cpp
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
** Blake Stone ceiling-turret seek/fire gate.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_turret("CeilingTurret");

static const int SEEK_TURN_DELAY = 30;	// tics between sweep steps (DOS 3d_act2.c:32)

// A_TurretSeek: DOS T_Seek (3d_act2.c:5063).  The hanging turret fires only when
// the player is within its ~45 deg facing cone with line of sight (CheckView) and
// inside fifteen tiles, on a distance-weighted chance -- point-blank always fires.
// While it has not found the player it sweeps, advancing one of eight facings
// every SEEK_TURN_DELAY (30) tics; the directional TURRA1..8 sprite shows the turn.
ACTION_FUNCTION(A_TurretSeek)
{
	AActor *p = players[ConsolePlayer].mo;
	if(!p)
		return false;

	bool targetFound = false;

	// CheckView: a different tile, within the facing cone, with line of sight.
	if((self->tilex != p->tilex || self->tiley != p->tiley) &&
		self->CheckVisibility(p, ANGLE_45/2))
	{
		const int dx = abs((int)self->tilex - (int)p->tilex);
		const int dy = abs((int)self->tiley - (int)p->tiley);
		if(dx < 15 && dy < 15)
		{
			const int dist = dx > dy ? dx : dy;
			const int chance = (dist <= 1) ? 300 : (pr_turret() / dist);
			if(pr_turret() < chance)
			{
				const Frame *missile = self->FindState("Missile");
				if(missile)
				{
					if(result)
						result->JumpFrame = missile;
					else
						self->SetState(missile);
				}
				return false;
			}
			targetFound = true;
		}
	}

	// Sweep one of eight facings every 30 tics while searching.  Only the rotating
	// turret runs this seek (the static turret has no seek state), so no stationary
	// guard is needed; temp1 is the sweep countdown.
	if(!targetFound)
	{
		self->temp1 -= (short)tics;
		if(self->temp1 <= 0)
		{
			self->temp1 = SEEK_TURN_DELAY;
			self->angle += ANGLE_45;
		}
	}
	return false;
}
