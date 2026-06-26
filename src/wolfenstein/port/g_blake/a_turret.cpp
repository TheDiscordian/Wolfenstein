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

// A_TurretSeek: bstone T_Seek (3d_act2.cpp:2226).  The hanging turret tracks the
// player while it has line of sight and, when the player is within fifteen tiles,
// rolls a distance-weighted chance to open fire -- point-blank always fires, the
// odds thinning the further out the player is.  On a successful roll it jumps to
// Missile (the port's See state otherwise sat on A_FaceTarget and never fired).
ACTION_FUNCTION(A_TurretSeek)
{
	AActor *p = players[ConsolePlayer].mo;
	if(!p)
		return false;

	// bstone seeks only when the player is on a different tile and visible.
	if(self->tilex == p->tilex && self->tiley == p->tiley)
		return false;
	if(!CheckLine(self, p))
		return false;

	const int dx = abs((int)self->tilex - (int)p->tilex);
	const int dy = abs((int)self->tiley - (int)p->tiley);
	if(dx >= 15 || dy >= 15)
		return false;

	const int dist = dx > dy ? dx : dy;
	const int chance = dist ? (pr_turret() / dist) : 300;
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
	}
	return false;
}
