/*
** a_liquid.cpp
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
** Blake Stone fluid (liquid) alien stand/ambush logic.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"

static FRandom pr_liquid("LiquidAlien");

// A_LiquidStand: bstone T_LiquidStand (3d_act2.cpp:3990).  While risen the fluid
// alien is solid + shootable and fires up to five times (80/255 each shot); once
// the player is more than a tile away it submerges (40/255 per check, or forced
// after the fifth shot), resetting the counter.  Standing adjacent resets the
// counter so it keeps firing.  temp1 is the shot counter (bstone's temp2).
//
// The port has no FL_VISIBLE, so bstone's extra "submerge when the player can't
// see me" condition is omitted; the roll and the five-shot cap still drive it
// under.
ACTION_FUNCTION(A_LiquidStand)
{
	self->flags |= FL_SHOOTABLE | FL_SOLID;

	if(pr_liquid() < 80 && self->temp1 < 5)
	{
		++self->temp1;
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

	AActor *p = players[ConsolePlayer].mo;
	if(!p)
		return false;

	const int dx = abs((int)self->tilex - (int)p->tilex);
	const int dy = abs((int)self->tiley - (int)p->tiley);
	if(dx > 1 || dy > 1)
	{
		if(pr_liquid() < 40 || self->temp1 == 5)
		{
			self->temp1 = 0;
			self->flags &= ~(FL_SOLID | FL_SHOOTABLE);
			const Frame *fall = self->FindState("Fall");
			if(fall)
			{
				if(result)
					result->JumpFrame = fall;
				else
					self->SetState(fall);
			}
		}
	}
	else
	{
		self->temp1 = 0;
	}

	return false;
}
