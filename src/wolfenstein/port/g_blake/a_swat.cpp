/*
** a_swat.cpp
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
** STAR Trooper / Alien Protector wound-knockdown (bstone swat actor).
**
** The trooper can be knocked off its feet a fixed number of times before it
** dies.  DamageActor drives the knockdown when a damage hit crosses a "wound
** boundary"; these natives seed the wound count on spawn and run the
** stay-down/get-up timer while the trooper is on the floor.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"

static FRandom pr_swat("SwatWound");

// A_SwatWound: bstone T_SwatWound (3d_act2.cpp:4024).  Runs on the looping
// knocked-down frame.  movecount is the stay-down timer DamageActor armed; count
// it down by this frame's tics, and once it expires and the player has stepped
// back more than a tile (so the trooper does not rise into Blake's face), become
// solid/shootable again and jump to the get-up animation.
ACTION_FUNCTION(A_SwatWound)
{
	if(self->movecount > (short)tics)
	{
		self->movecount -= (short)tics;
		return false;
	}
	self->movecount = 0;

	AActor *plr = players[ConsolePlayer].mo;
	if(plr && (abs(self->x - plr->x) > FRACUNIT || abs(self->y - plr->y) > FRACUNIT))
	{
		self->flags |= FL_SHOOTABLE | FL_SOLID;
		const Frame *getup = self->FindState("GetUp");
		if(getup)
		{
			if(result)
				result->JumpFrame = getup;
			else
				self->SetState(getup);
		}
	}
	return false;
}

// Blake_CheckSwatWound: bstone DamageActor wound check (3d_state.cpp:1546).
// Called from DamageActor when a still-alive trooper takes a hit.  If the hit
// dropped it across a wound boundary, knock it down -- clear solid/shootable, arm
// the stay-down timer in movecount, jump to KnockedDown -- and report that we
// handled it so the normal pain reaction is skipped.  woundMod narrows with the
// wound count in temp1: temp1 0 never trips, temp1 1 trips once near half health.
bool Blake_CheckSwatWound(AActor *ob, int oldHealth)
{
	static const ClassDef * const swatCls = ClassDef::FindClass("STARTrooper");
	if(!swatCls || !ob->IsKindOf(swatCls))
		return false;

	const int startHp = ob->SpawnHealth();

	// Seed the wound count on the trooper's first hit (health still at full).
	// bstone rolls it at spawn (SpawnStand/SpawnPatrol); rolling it here covers
	// both stand and patrol troopers without a spawn-frame action -- the engine
	// enters the initial state with actions suppressed, so a 0-tic Spawn frame
	// would never fire for a patrol trooper.  Half the troopers (temp1 0) can
	// never be knocked down.
	if(oldHealth >= startHp)
		ob->temp1 = (short)(pr_swat() & 1);

	if(ob->temp1 <= 0)
		return false;

	const int woundMod = startHp / (ob->temp1 + 1) + 1;
	if(oldHealth / woundMod == ob->health / woundMod)
		return false; // no wound boundary crossed

	const Frame *down = ob->FindState("KnockedDown");
	if(!down)
		return false;

	ob->flags &= ~(FL_SHOOTABLE | FL_SOLID);
	ob->movecount = (short)(5 * 60 + (pr_swat() % 20) * 60);
	ob->SetState(down);
	return true;
}
