/*
** a_podegg.cpp
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
** Blake Stone (PS) pod-egg hatch timer.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"
#include "blake_scanvalue.h"

static FRandom pr_podegg("PodEgg");

// Pod eggs hatch on a timer that runs only while the egg is drawn on screen, not
// on line-of-sight (DOS T_OfsThink podegg case, 3d_act2.cpp:874).  temp1 is that
// countdown.  DOS seeds it from the egg's scan_value byte (the 0xFA object-plane
// word, 3d_act2.cpp:585): temp1 = scan_value*60 tics, or a random 300..1440 when
// the map placed no byte (60*5 + 60*(US_RndT()%20)).  A scan_value of 0xff also
// makes the egg unshootable.
ACTION_FUNCTION(A_PodEggInit)
{
	const int sv = Blake_ScanValueGet(self->tilex, self->tiley);
	if(sv < 0)
		self->temp1 = (short)(300 + 60 * (pr_podegg() % 20));	// DOS 60*5+60*(rnd%20)
	else
	{
		self->temp1 = (short)(sv * 60);
		if(sv == 0xff)
			self->flags &= ~FL_SHOOTABLE;	// DOS: temp2==0xff*60 -> not shootable
	}
	return false;
}

ACTION_FUNCTION(A_PodEggTick)
{
	if(!(self->flags & FL_VISIBLE))
		return false; // frozen while off screen

	if(self->temp1 > (short)tics)
	{
		self->temp1 -= (short)tics;
		return false;
	}

	// Timer up: hatch (the Death state spawns the pod alien).
	const Frame *death = self->FindState("Death");
	if(death)
	{
		if(result)
			result->JumpFrame = death;
		else
			self->SetState(death);
	}
	return false;
}
