/*
** blake_blastdoor.cpp
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
** Blake Stone blastable doors (DOS BlastNearDoors / TryBlastDoor, 3d_act1.c).
** A plasma-detonator blast or an anti-plasma cannon shot forces closed,
** unlocked doors in the 3x3 neighborhood open and shows a door explosion.
**
*/

#include "actor.h"
#include "wl_def.h"
#include "id_ca.h"
#include "gamemap.h"
#include "lnspec.h"
#include "thingdef/thingdef.h"
#include "blake_blastdoor.h"

// Defined in lnspec.cpp, where the EVDoor thinker is visible.
extern void Blake_ForceDoorOpen(MapSpot spot, const MapTrigger *trig);

// The cosmetic door blast (DOS doorexplodeobj): explosion sprite + sound, no
// radius damage of its own -- the detonator/cannon already dealt that.
static void SpawnDoorExplosion(unsigned int tilex, unsigned int tiley)
{
	static const ClassDef * const cls = ClassDef::FindClass("BlakeDoorExplosion");
	if(!cls)
		return;
	AActor::Spawn(cls, ((fixed)tilex<<TILESHIFT)+(TILEGLOBAL/2),
		((fixed)tiley<<TILESHIFT)+(TILEGLOBAL/2), 0, SPAWN_AllowReplacement);
}

// The unlocked Door_Open trigger on a spot, or NULL.  DOS blasts non-one-way
// doors with lock == kt_none; the port has no one-way doors, and an unlocked
// Blake door is arg[3] == 0 (gamemap_planes door-lock wiring).
static const MapTrigger *UnlockedDoorTrigger(MapSpot spot)
{
	if(!spot)
		return NULL;
	for(unsigned int t = spot->triggers.Size();t-- > 0;)
	{
		const MapTrigger &trig = spot->triggers[t];
		if(trig.action == Specials::Door_Open && trig.arg[3] == 0)
			return &trig;
	}
	return NULL;
}

void Blake_BlastDoorsNear(AActor *origin)
{
	if(!origin || !map)
		return;

	const int tx = origin->tilex, ty = origin->tiley;
	const int w = map->GetHeader().width, h = map->GetHeader().height;

	for(int dy = -1;dy <= 1;++dy)
	{
		const int y = ty+dy;
		if(y < 0 || y >= h)
			continue;
		for(int dx = -1;dx <= 1;++dx)
		{
			const int x = tx+dx;
			if(x < 0 || x >= w)
				continue;

			MapSpot spot = map->GetSpot(x, y, 0);
			const MapTrigger *trig = UnlockedDoorTrigger(spot);
			if(!trig || spot->thinker) // closed (no thinker), unlocked door only
				continue;

			Blake_ForceDoorOpen(spot, trig);
			SpawnDoorExplosion(x, y);
		}
	}
}

ACTION_FUNCTION(A_BlakeBlastDoors)
{
	Blake_BlastDoorsNear(self);
	return true;
}
