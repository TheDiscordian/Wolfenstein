/*
** blake_drops.cpp
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
** PS reserved-item drops (bstone ScanInfoPlane FL2_DROP_* +
** PlaceReservedItemNearTile).  Plane-0 cells 72-77 mark the enemy spawned
** above them; the mark is carried in temp1's high byte and pays out where
** the enemy dies.
**
*/

#include "actor.h"
#include "wl_def.h"
#include "g_mapinfo.h"
#include "gamemap.h"
#include "id_ca.h"
#include "wl_iwad.h"
#include "thingdef/thingdef.h"
#include "blake_drops.h"

// bstone SpecialSpawnFlags order (RKEY_TILE = 72).
static const char* const DropClasses[6] =
{
	"RedAccessCard",
	"YellowAccessCard",
	"BlueAccessCard",
	"AntiPlasmaCannon",
	"DualNeutronDisruptor",
	"PlasmaDetonatorWeapon"
};

struct DropCell
{
	WORD x, y;
	BYTE code;
};
static TArray<DropCell> dropCells;

void Blake_ReservedDropClear()
{
	dropCells.Clear();
}

void Blake_ReservedDropCell(unsigned int x, unsigned int y, unsigned int tile)
{
	if(tile < 72 || tile > 77)
		return;
	// PS only: AOG and Wolf use these as ordinary tiles.
	if(!IWad::CheckGameFilter("Blake") || EpisodeInfo::GetNumEpisodes() > 1)
		return;

	DropCell cell = { (WORD)x, (WORD)y, (BYTE)(tile - 71) };
	dropCells.Push(cell);
}

void Blake_ReservedDropAttach()
{
	if(!dropCells.Size())
		return;

	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(!(ob->flags & FL_SHOOTABLE))
			continue;

		const int tx = ob->x >> FRACBITS;
		const int ty = ob->y >> FRACBITS;
		for(unsigned int c = 0;c < dropCells.Size();++c)
		{
			if(dropCells[c].x == tx && dropCells[c].y == ty)
			{
				ob->temp1 = (short)((ob->temp1 & 0xFF) | (dropCells[c].code << 8));
				break;
			}
		}
	}
	dropCells.Clear();
}

// bstone PlaceItemNearTile: the death tile if it is clear, else the nearest
// clear neighbour.
static void PlaceReservedItem(const ClassDef *cls, int tx, int ty)
{
	for(int radius = 0;radius < 3;++radius)
	{
		for(int dy = -radius;dy <= radius;++dy)
		{
			for(int dx = -radius;dx <= radius;++dx)
			{
				if(MAX(abs(dx), abs(dy)) != radius)
					continue;

				const int cx = tx + dx, cy = ty + dy;
				if(cx < 0 || cy < 0 ||
					cx >= (int)map->GetHeader().width || cy >= (int)map->GetHeader().height)
					continue;
				if(map->GetSpot(cx, cy, 0)->tile)
					continue;

				AActor *drop = AActor::Spawn(cls,
					((fixed)cx << FRACBITS) + FRACUNIT/2,
					((fixed)cy << FRACBITS) + FRACUNIT/2,
					0, SPAWN_AllowReplacement);
				drop->angle = 0;
				return;
			}
		}
	}
}

void Blake_CheckReservedDrop(AActor *ob)
{
	const int code = (ob->temp1 >> 8) & 0xFF;
	if(code < 1 || code > 6)
		return;
	ob->temp1 &= 0xFF;

	const ClassDef *cls = ClassDef::FindClass(DropClasses[code - 1]);
	if(cls)
		PlaceReservedItem(cls, ob->x >> FRACBITS, ob->y >> FRACBITS);
}
