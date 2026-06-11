/*
** a_projgen.cpp
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
** AOG projection generator destruction (bstone rotating_cubeobj). Destroying
** all of them on a floor wins the game.
**
*/

#include "actor.h"
#include "wl_def.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

// get_remaining_generators: a dying one already has health <= 0 (bstone
// checks FL_SHOOTABLE, cleared by its KillActor).
static int RemainingGenerators()
{
	static const ClassDef * const genCls = ClassDef::FindClass("ProjectionGenerator");
	if(!genCls)
		return 0;

	int remaining = 0;
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(ob->health > 0 && ob->IsKindOf(genCls))
			++remaining;
	}
	return remaining;
}

// The ouch frame shows only on the first wound.
ACTION_FUNCTION(A_GeneratorOuch)
{
	if(self->flags & FL_VITALOUCHED)
	{
		const Frame *frame = self->FindState("Spawn");
		if(frame)
		{
			if(result)
				result->JumpFrame = frame;
			else
				self->SetState(frame);
		}
		return false;
	}

	self->flags |= FL_VITALOUCHED;
	return true;
}

// VITAL_GONESND plays at the player so it is heard at any distance.
ACTION_FUNCTION(A_GeneratorGone)
{
	if(players[0].mo)
		PlaySoundLocActor("projgen/gone", players[0].mo);
	return true;
}

// display_remaining_generators
ACTION_FUNCTION(A_GeneratorMessage)
{
	FString msg;
	msg.Format(
		"^FC57 PROJECTION GENERATOR\r"
		"      DESTROYED!\r"
		"\r"
		"^FCA6   - %d REMAINING -\r"
		" DESTROY THEM TO WIN!", RemainingGenerators());

	StatusBar->DisplayInfoMessage(msg, 0x3000, 300);
	return true;
}

// Runs once per generator, a full dead cycle after its die animation; the
// last one to die ends the whole game.
ACTION_FUNCTION(A_GeneratorVictory)
{
	if(RemainingGenerators() == 0)
		playstate = ex_victorious;
	return true;
}
