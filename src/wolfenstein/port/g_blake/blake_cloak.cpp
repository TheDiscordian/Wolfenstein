/*
** blake_cloak.cpp
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
** Planet Strike enemy cloaking.  Map cloak tiles (78 = cloak, 80 = cloak +
** ambush) flag the enemy standing on them as FL2_CLOAKED; the renderer draws a
** cloaked enemy as a dark fuzz silhouette (DrawScaleds/ScaleSprite), and a hit
** sets FL2_DAMAGECLOAK to reveal it for one frame (DamageActor).  Map-data
** driven, mirroring the PS reserved-item drops (blake_drops.cpp).
**
*/

#include "actor.h"
#include "wl_def.h"
#include "g_mapinfo.h"
#include "gamemap.h"
#include "id_ca.h"
#include "wl_iwad.h"
#include "thingdef/thingdef.h"
#include "blake_cloak.h"

struct CloakCell
{
	WORD x, y;
	bool ambush;
};
static TArray<CloakCell> cloakCells;

bool Blake_IsPSCloak()
{
	return IWad::CheckGameFilter("Blake") && EpisodeInfo::GetNumEpisodes() == 1;
}

void Blake_CloakCellClear()
{
	cloakCells.Clear();
}

void Blake_CloakCell(unsigned int x, unsigned int y, unsigned int tile)
{
	if(tile != 78 && tile != 80)	// CLOAK_TILE / CLOAK_AMBUSH_TILE
		return;
	if(!Blake_IsPSCloak())
		return;

	CloakCell cell = { (WORD)x, (WORD)y, tile == 80 };
	cloakCells.Push(cell);
}

void Blake_CloakAttach()
{
	if(!cloakCells.Size())
		return;

	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(!(ob->flags & FL_SHOOTABLE))
			continue;

		const int tx = ob->x >> FRACBITS;
		const int ty = ob->y >> FRACBITS;
		for(unsigned int c = 0;c < cloakCells.Size();++c)
		{
			if(cloakCells[c].x == tx && cloakCells[c].y == ty)
			{
				ob->flags2 |= FL2_CLOAKED;
				if(cloakCells[c].ambush)
					ob->flags |= FL_AMBUSH;
				break;
			}
		}
	}
	cloakCells.Clear();
}
