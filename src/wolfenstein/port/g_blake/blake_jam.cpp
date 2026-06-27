/*
** blake_jam.cpp
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
** Blake Stone 'JAM' secret cheat: full arsenal (bstone 3d_play.cpp).
**
*/

#include "wl_def.h"
#include "actor.h"
#include "a_inventory.h"
#include "thingdef/thingdef.h"
#include "wl_agent.h"
#include "wl_iwad.h"
#include "wl_play.h"

static void GiveByName(AActor *mo, const char *name)
{
	const ClassDef *cls = ClassDef::FindClass(name);
	if(cls)
		mo->GiveInventory(cls, 1);
}

// DOS Blake Stone 'JAM' cheat payload (type J, A, M, Enter). Full health, full
// charge, every key and standard weapon for the current game. The DOS extras with
// no port hook -- radar power, score reset, the survival-time bonus -- are
// omitted; the plasma detonator is excluded exactly as the original GiveWeapon
// loop excludes it.
void Blake_GiveJamArsenal()
{
	player_t &p = players[ConsolePlayer];
	if(!p.mo)
		return;

	// Full health + every weapon charge topped up (same as Blake_FullAmmoHealth).
	p.health = 100;
	static const ClassDef * const chargeCls = ClassDef::FindClass("ChargeUnit");
	for(AInventory *item = p.mo->inventory; item; item = item->inventory)
		if(chargeCls && item->GetClass()->IsDescendantOf(chargeCls))
			item->amount = item->maxamount;

	const bool isPS = IWad::GetGame().Name.CompareNoCase("Planet Strike") == 0;

	if(isPS)
	{
		GiveByName(p.mo, "RedAccessKey");
		GiveByName(p.mo, "YellowAccessKey");
		GiveByName(p.mo, "BlueAccessKey");
	}
	else
	{
		GiveByName(p.mo, "RedAccessCard");
		GiveByName(p.mo, "YellowAccessCard");
		GiveByName(p.mo, "GreenAccessCard");
		GiveByName(p.mo, "BlueAccessCard");
		GiveByName(p.mo, "GoldAccessCard");
	}

	GiveByName(p.mo, "AutoChargePistol");
	GiveByName(p.mo, "SlowFireProtector");
	GiveByName(p.mo, "RapidAssaultWeapon");
	GiveByName(p.mo, "DualNeutronDisruptor");
	GiveByName(p.mo, "PlasmaDischargeUnit");
	if(isPS)
		GiveByName(p.mo, "AntiPlasmaCannon");

	if(StatusBar)
		StatusBar->DisplayInfoMessage("\r\r     YOU CHEATER!", 0x200, 300);
}
