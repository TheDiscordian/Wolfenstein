/*
** a_detonator.cpp
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
** PS fission detonator chain (bstone TryDropPlasmaDetonator): dropping an
** armed detonator next to the Security Cube destroys it and unlocks the
** next floor's teleport unit.
**
*/

#include "actor.h"
#include "wl_def.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "id_sd.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "wl_state.h"
#include "a_inventory.h"
#include "thingdef/thingdef.h"
#include "blake_elevator.h"

// bstone 3d_msgs.cpp
static const char* const pd_dropped =
	"^FC19\r       WARNING:\r"
	"^FCA6   FISSION DETONATOR\r"
	"       DROPPED!";
static const char* const pd_notnear =
	"^SH035^FCA6\r  YOU MUST\r"
	"  FIND THE\r"
	"  SECURITY\r"
	"    CUBE.";
static const char* const pd_getcloser =
	"^SH035^FCA6\r TRANSPORTER\r"
	" SECURITY OUT\r"
	" OF RANGE";
static const char* const pd_floornotlocked =
	"^SH035^FCA6\r TRANSPORTER\r"
	" SECURITY\r"
	" ALREADY\r"
	" DISABLED.";
static const char* const pd_donthaveany =
	"^SH0E6^FCA6\r NO FISSION\r"
	" DETONATOR\r"
	" AVAILABLE.";
static const char* const pd_no_computer =
	"^SH035^FCA6\r A SECURITY \r"
	" CUBE IS NOT\r"
	" LOCATED IN\r"
	" THIS SECTOR.";
static const char* const pd_floorunlocked =
	"^SH035^FCA6\r TRANSPORTER\r"
	"  SECURITY\r"
	"  DISABLED.";

static AActor *FindSecurityCube()
{
	static const ClassDef * const cubeCls = ClassDef::FindClass("SecurityCube");
	if(!cubeCls)
		return NULL;

	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(ob->health > 0 && ob->IsKindOf(cubeCls))
			return ob;
	}
	return NULL;
}

static int CubeTileDistance(AActor *a, AActor *b)
{
	const int dx = abs((a->x >> FRACBITS) - (b->x >> FRACBITS));
	const int dy = abs((a->y >> FRACBITS) - (b->y >> FRACBITS));
	return MAX(dx, dy);
}

// bstone TryDropPlasmaDetonator: gates run in order, then the armed bomb
// spawns at the player's feet.
ACTION_FUNCTION(A_BlakeDropDetonator)
{
	player_t *player = self->player;
	if(!player || !levelInfo)
		return false;

	const int lvl = levelInfo->LevelNumber;

	if(Blake_PsFloorUnlocked(lvl + 1))
	{
		StatusBar->DisplayInfoMessage(pd_floornotlocked, 0x200, 300);
		return true;
	}

	if(lvl > 20)
	{
		StatusBar->DisplayInfoMessage(pd_no_computer, 0x200, 300);
		return true;
	}

	static const ClassDef * const ammoCls = ClassDef::FindClass("PlasmaDetonator");
	AInventory *ammo = ammoCls ? self->FindInventory(ammoCls) : NULL;
	if(!ammo || ammo->amount == 0)
	{
		StatusBar->DisplayInfoMessage(pd_donthaveany, 0x200, 300);
		return true;
	}

	AActor *cube = FindSecurityCube();
	if(!cube)
	{
		StatusBar->DisplayInfoMessage(pd_no_computer, 0x200, 300);
		return true;
	}

	if(cube->GetZone() != self->GetZone())
	{
		StatusBar->DisplayInfoMessage(pd_notnear, 0x200, 300);
		return true;
	}

	if(CubeTileDistance(self, cube) > 2)
	{
		StatusBar->DisplayInfoMessage(pd_getcloser, 0x200, 300);
		return true;
	}

	// DropPlasmaDetonator
	static const ClassDef * const dropCls = ClassDef::FindClass("PlasmaDetonatorDrop");
	if(!dropCls)
		return false;

	AActor *bomb = AActor::Spawn(dropCls, self->x, self->y, 0, SPAWN_AllowReplacement);
	bomb->target = self;
	PlaySoundLocActor("misc/plasmadetonator/drop", bomb);
	--ammo->amount;
	StatusBar->DisplayInfoMessage(pd_dropped, 0x200, 300);
	return true;
}

// The explosion takes the Security Cube with it and unlocks the next floor
// (bstone ExplodeRadius rotating_cubeobj case).
ACTION_FUNCTION(A_BlakeDetonatorExplode)
{
	AActor *cube = FindSecurityCube();
	if(!cube || !levelInfo)
		return true;

	if(CubeTileDistance(self, cube) > 3)
		return true;

	cube->flags &= ~FL_SOLID;
	cube->health = 0;
	cube->Die();

	Blake_PsUnlockFloor(levelInfo->LevelNumber + 1);
	return true;
}

// End of the cube's death animation (bstone anim-complete handler).
ACTION_FUNCTION(A_BlakeCubeUnlocked)
{
	StatusBar->DisplayInfoMessage(pd_floorunlocked, 0x3000, 300);
	SD_PlaySound("blake/rollscore");
	return true;
}
