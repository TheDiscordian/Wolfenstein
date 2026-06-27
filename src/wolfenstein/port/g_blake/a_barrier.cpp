/*
** a_barrier.cpp
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
** Blake Stone barrier contact damage.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_net.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_barrier("BlakeBarrier");

// Per-tic thinker for active electric barriers: arcs zap players standing
// alongside; anything shootable caught on the tile itself gets fried.
ACTION_FUNCTION(A_BarrierDamage)
{
	ACTION_PARAM_BOOL(zapPlayer, 0); // arcs zap the player, posts don't

	if(zapPlayer && pr_barrier() < 0x10)
	{
		for(unsigned int i = 0; i < Net::InitVars.numPlayers; ++i)
		{
			AActor *p = players[i].mo;
			if(!p)
				continue;
			const fixed dx = abs(p->x - self->x);
			const fixed dy = abs(p->y - self->y);
			if(dx <= 0x16000 && dy <= 0x16000)
			{
				PlaySoundLocActor("barrier/zap", self);
				DamageActor(p, self, 4);
			}
		}
	}

	if(pr_barrier() < 0x7f)
		DamageActorsOnTile(self, self->tilex, self->tiley, 500);

	return true;
}

// Anti-plasma cannon shutdown for an electric arc barrier (bstone
// T_BarrierShutdown). The hit seeds health = the number of flickers remaining
// (15) and temp1 = the inter-flicker countdown. Each pass toggles the barrier
// lit<->dark on a random interval, fading it out over ~15 flashes, then kills it
// for good. hidden marks it permanently disabled so the wall-switch table
// (ConvergeActor) never re-enables a barrier the player shot down.
ACTION_FUNCTION(A_BarrierShutdown)
{
	if(self->health > 0)
	{
		if(self->temp1 > (short)tics)
		{
			self->temp1 -= (short)tics;
			return true; // hold the current flicker frame
		}

		const Frame *f;
		if(!(self->flags & FL_SOLID)) // currently dark -> flash lit
		{
			self->flags |= FL_SOLID;
			PlaySoundLocActor("barrier/zap", self);
			self->temp1 = (short)(pr_barrier() & 0x7);
			f = self->FindState("ShutdownLit");
		}
		else // currently lit -> go dark
		{
			self->flags &= ~FL_SOLID;
			self->temp1 = (short)(5 + (pr_barrier() & 0xf));
			f = self->FindState("ShutdownDark");
		}
		self->health--;
		if(f)
		{
			if(result)
				result->JumpFrame = f;
			else
				self->SetState(f);
		}
		return false;
	}

	// Flickers exhausted: the barrier is dead. Mark it so it stays down.
	self->hidden = 1;
	self->flags &= ~FL_SOLID;
	if(const Frame *f = self->FindState("Disabled"))
	{
		if(result)
			result->JumpFrame = f;
		else
			self->SetState(f);
	}
	return false;
}

// Per-tic thinker for closing spike/post frames: the animation holds while a
// player is alongside, grinding anyone standing on the tile.
ACTION_FUNCTION(A_VPostGuard)
{
	for(unsigned int i = 0; i < Net::InitVars.numPlayers; ++i)
	{
		AActor *p = players[i].mo;
		if(!p)
			continue;
		const fixed dx = abs(p->x - self->x);
		const fixed dy = abs(p->y - self->y);
		if(dx <= 0x18000 && dy <= 0x18000)
		{
			if(dx <= 0x8000 && dy <= 0x8000)
				DamageActor(p, self, 2);
			++self->ticcount; // hold the current frame
			break;
		}
	}

	return true;
}
