/*
** a_steam.cpp
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
** Blake Stone steam grate / steam pipe release timer.
**
*/

#include "actor.h"
#include "m_random.h"
#include "wl_def.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"

static FRandom pr_steam("SteamGrate");

// A_SteamWait: bstone T_SteamObj (3d_act2.cpp:5932).  The grate only releases
// steam while it is drawn on screen (FL_VISIBLE); off screen the timer is frozen,
// so distant grates do not hiss.  temp1 is the countdown; when it expires the
// grate jumps to its Release animation and re-arms with a fresh random delay
// (US_RndT() << 3, up to ~34 s).
ACTION_FUNCTION(A_SteamWait)
{
	if(!(self->flags & FL_VISIBLE))
		return false;

	if(self->temp1 > (short)tics)
	{
		self->temp1 -= (short)tics;
		return false;
	}

	self->temp1 = (short)(pr_steam() << 3);
	const Frame *release = self->FindState("Release");
	if(release)
	{
		if(result)
			result->JumpFrame = release;
		else
			self->SetState(release);
	}
	return false;
}
