/*
** a_morph.cpp
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
** Blake Stone (Planet Strike) morph-post wake-up.
**
*/

#include "actor.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"

// A_BlakeMorphWake: bstone converts a finished morph post in place straight into
// an actively-chasing enemy (NewState(s_ofs_chase1), 3d_act2.cpp:2311).  The port
// spawns the real enemy fresh, which lands dormant in its Look state and has to
// re-sight the player.  The morphed-awake actor calls this from its Spawn state
// before falling into See, mirroring FirstSighting (wl_state.cpp:1195): target
// the player, enter attack mode at run speed.  So the alien pursues immediately
// instead of standing idle until it independently re-acquires the player.
ACTION_FUNCTION(A_BlakeMorphWake)
{
	AActor *p = players[ConsolePlayer].mo;
	if(!p)
		return false;

	self->target = p;
	self->flags &= ~FL_PATHING;
	self->flags |= FL_ATTACKMODE | FL_FIRSTATTACK;
	self->speed = self->runspeed;
	return false;
}

// bstone locks the player's weapon (noShots = true) while Goldfire morphs on the
// final level (3d_state.cpp:1524), so the morph cutscene can't be shot through.
// A_WeaponReady honours this flag; cleared when the morphed boss appears and at
// every level setup (Blake_NoShotsReset) so a death mid-morph never leaves the
// weapon stuck.
bool blakeNoShots = false;

void Blake_NoShotsReset()
{
	blakeNoShots = false;
}

ACTION_FUNCTION(A_BlakeWeaponLock)
{
	blakeNoShots = true;
	return false;
}

ACTION_FUNCTION(A_BlakeWeaponUnlock)
{
	blakeNoShots = false;
	return false;
}
