/*
** blake_floor.cpp
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
** Per-floor world snapshots (DOS SaveLevel/LoadLevel, 3d_main.c:617/428). Leaving a floor
** by elevator compresses the whole world -- thinkers, map state, per-floor
** stats -- into a memory file keyed by map name; coming back restores it
** over the freshly parsed map instead of spawning things. The traveling
** player rides the TRAVEL thinker list and is excluded; pointers to him
** inside the snapshot are written as player tags and resolve to the live
** pawn on restore (FArchive hub travel).
**
*/

#include "wl_def.h"
#include "blake_floor.h"
#include "doomerrors.h"
#include "farchive.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "thinker.h"
#include "version.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_iwad.h"
#include "wl_loadsave.h"
#include "wl_main.h"
#include "wl_play.h"

struct FloorRec
{
	char mapname[9];
	FCompressedMemFile *snapshot;
};
static TArray<FloorRec> floors;

static int FindFloor(const char* mapname)
{
	for(unsigned int i = 0;i < floors.Size();++i)
	{
		if(stricmp(floors[i].mapname, mapname) == 0)
			return (int)i;
	}
	return -1;
}

// The floor-local slice of the game: per-floor stats, the world's thinkers
// (TRAVEL list excluded -- that's the player in transit), and the map's
// dynamic state. Player state travels with the pawn instead.
static void SerializeFloor(FArchive &arc)
{
	arc << gamestate.secretcount
		<< gamestate.treasurecount
		<< gamestate.killcount
		<< gamestate.secrettotal
		<< gamestate.treasuretotal
		<< gamestate.killtotal
		<< gamestate.TimeCount
		<< gamestate.victoryflag
		<< gamestate.fullmap;

	thinkerList.Serialize(arc, ThinkerList::FIRST_TICKABLE);

	arc << map;
}

void Blake_FloorClear()
{
	for(unsigned int i = 0;i < floors.Size();++i)
		delete floors[i].snapshot;
	floors.Clear();
}

void Blake_FloorCapture()
{
	if(!IWad::CheckGameFilter("Blake") || !map)
		return;

	const int existing = FindFloor(levelInfo->MapName);
	if(existing >= 0)
	{
		delete floors[existing].snapshot;
		floors.Delete(existing);
	}

	FloorRec rec;
	strncpy(rec.mapname, levelInfo->MapName, 8);
	rec.mapname[8] = 0;
	rec.snapshot = new FCompressedMemFile();
	rec.snapshot->Open();
	{
		// The archive's destructor closes the file, which implodes it.
		FArchive arc(*rec.snapshot);
		SerializeFloor(arc);
	}
	floors.Push(rec);
}

bool Blake_FloorPending()
{
	// Only an elevator arrival restores; a warp or death restart spawns
	// the floor fresh.
	if(playstate != ex_completed && playstate != ex_secretlevel &&
		playstate != ex_newmap && playstate != ex_victorious)
		return false;
	return FindFloor(gamestate.mapname) >= 0;
}

bool Blake_FloorRestore()
{
	if(!Blake_FloorPending())
		return false;

	const int idx = FindFloor(gamestate.mapname);
	FCompressedMemFile *snapshot = floors[idx].snapshot;
	floors.Delete(idx);

	// The map and object serializers gate on the save version globals.
	GameSave::SaveVersion = GetSaveVersion();
	GameSave::SaveProdVersion = SAVEPRODVER;

	// Actors created while reading must not self-register (AActor::Init
	// gates Activate on loadedgame); the thinker list loop registers them
	// at their stored priority instead.
	const bool wasloaded = loadedgame;
	loadedgame = true;

	try
	{
		snapshot->Reopen();
		FArchive arc(*snapshot);
		arc.SetHubTravel();
		SerializeFloor(arc);
	}
	catch(CRecoverableError &error)
	{
		// Corrupt snapshot (bad save file): drop it and rebuild the floor
		// fresh. The traveling player sits in the TRAVEL list and survives
		// the unload.
		printf("Floor restore failed: %s\n", error.GetMessage());
		loadedgame = wasloaded;
		delete snapshot;
		CA_CacheMap(gamestate.mapname, false);
		return false;
	}

	loadedgame = wasloaded;
	delete snapshot;

	// SpawnThings was skipped, so register the player starts ourselves;
	// CheckSpawnPlayer needs one to spawn the body FinishTravel transfers
	// the traveling pawn onto.
	map->CollectPlayerStarts();
	return true;
}

void Blake_FloorSerialize(FArchive &arc)
{
	if(arc.IsStoring())
	{
		DWORD count = floors.Size();
		arc << count;
		for(unsigned int i = 0;i < floors.Size();++i)
		{
			arc.Write(floors[i].mapname, 8);
			floors[i].snapshot->Serialize(arc);
		}
	}
	else
	{
		Blake_FloorClear();

		DWORD count;
		arc << count;
		while(count--)
		{
			FloorRec rec;
			arc.Read(rec.mapname, 8);
			rec.mapname[8] = 0;
			rec.snapshot = new FCompressedMemFile();
			rec.snapshot->Serialize(arc);
			floors.Push(rec);
		}
	}
}
