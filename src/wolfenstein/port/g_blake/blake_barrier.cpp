/*
** blake_barrier.cpp
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
** The global barrier switch table (DOS gamestate.barrier_table). Wall
** switches key barrier groups by their link coordinate; the table holds the
** group state across level loads so cross-floor switches and floor revisits
** keep their effect, and barrier actors are converged to it after spawning
** (DOS ConnectBarriers plus the T_BarrierTransition table checks).
**
*/

#include "wl_def.h"
#include "blake_barrier.h"
#include "actor.h"
#include "farchive.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "thingdef/thingdef.h"
#include "textures/textures.h"
#include "wl_agent.h"

// Table key: (level << 16) | (link x << 8) | link y.
static TMap<unsigned int, bool> barrierTable;

struct SwitchCellRec
{
	unsigned short x, y;
	unsigned int key;
};
static TArray<SwitchCellRec> switchWalls;
static bool applyPending;

static unsigned int Key(unsigned int levelNum, unsigned int x, unsigned int y)
{
	return (levelNum<<16) | (x<<8) | y;
}

static bool IsBarrier(AActor *actor)
{
	static const char* const barrierClassNames[4] = {
		"ElectricArcBarrier", "ElectricPostBarrier",
		"VerticalSpikeActive", "VerticalPostActive"
	};

	for(unsigned int c = 0;c < 4;++c)
	{
		const ClassDef *cls = ClassDef::FindClass(barrierClassNames[c]);
		if(cls && actor->GetClass()->IsDescendantOf(cls))
			return true;
	}
	return false;
}

// Sends a barrier actor to the given state if it isn't there already.
static void ConvergeActor(AActor *actor, bool on)
{
	// An arc barrier the player shot down with the anti-plasma cannon stays dead
	// (A_BarrierShutdown sets hidden). The wall-switch table must never re-enable
	// it -- it has FL_SOLID cleared, so without this it would read as merely "off".
	if(actor->hidden)
		return;
	if(!!(actor->flags & FL_SOLID) == on)
		return;
	if(const Frame *state = actor->FindState(on ? "Active" : "Inactive"))
		actor->SetState(state);
}

static void RepaintSwitchSpot(MapSpot spot, bool on)
{
	static const char* const switchTex[2][2] = {
		{"SWITCHI1", "SWITCHI2"}, {"SWITCHA1", "SWITCHA2"}
	};

	if(!spot->tile)
		return;
	for(unsigned int side = 0;side < 4;++side)
	{
		// North/south faces use the *1 texture, east/west the *2.
		const bool ew = side == MapTile::East || side == MapTile::West;
		spot->SetTexture(static_cast<MapTile::Side>(side),
			TexMan.GetTexture(switchTex[on][ew], FTexture::TEX_Wall));
	}
}

void Blake_BarrierClear()
{
	barrierTable.Clear();
	switchWalls.Clear();
	applyPending = false;
}

void Blake_BarrierStartLevel()
{
	switchWalls.Clear();
	applyPending = true;
}

bool Blake_BarrierEnsure(unsigned int levelNum, unsigned int x, unsigned int y, bool on)
{
	if(const bool *state = barrierTable.CheckKey(Key(levelNum, x, y)))
		return *state;
	barrierTable[Key(levelNum, x, y)] = on;
	return on;
}

bool Blake_BarrierGet(unsigned int levelNum, unsigned int x, unsigned int y)
{
	if(const bool *state = barrierTable.CheckKey(Key(levelNum, x, y)))
		return *state;
	return false;
}

void Blake_BarrierSet(unsigned int levelNum, unsigned int x, unsigned int y, bool on)
{
	barrierTable[Key(levelNum, x, y)] = on;
}

void Blake_BarrierAddSwitchCell(unsigned int x, unsigned int y, unsigned int levelNum, unsigned int lx, unsigned int ly)
{
	SwitchCellRec rec = {(unsigned short)x, (unsigned short)y, Key(levelNum, lx, ly)};
	switchWalls.Push(rec);
}

void Blake_BarrierSetActors(unsigned int tag, bool on)
{
	MapSpot member = NULL;
	while((member = map->GetSpotByTag(tag, member)))
	{
		if(member->tile)
			continue;

		const unsigned int x = member->GetX(), y = member->GetY();
		for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
		{
			AActor * const actor = iter;
			if(actor->tilex != x || actor->tiley != y || !IsBarrier(actor))
				continue;
			ConvergeActor(actor, on);
		}
	}
}

void Blake_BarrierRepaintSwitches(unsigned int levelNum, unsigned int lx, unsigned int ly, bool on)
{
	const unsigned int key = Key(levelNum, lx, ly);
	for(unsigned int s = 0;s < switchWalls.Size();++s)
	{
		if(switchWalls[s].key != key)
			continue;
		RepaintSwitchSpot(map->GetSpot(switchWalls[s].x, switchWalls[s].y, 0), on);
	}
}

void Blake_BarrierOperateMsg(unsigned int levelNum, bool on)
{
	FString msg(on ? "\r\r  ACTIVATING BARRIER" : "\r\r DEACTIVATING BARRIER");
	// AOG names the floor; the slot within the episode is the floor number.
	if(EpisodeInfo::GetNumEpisodes() > 1)
	{
		const int slot = ((int)levelNum-1)%15;
		if(slot <= 0 || slot >= 10)
			msg.AppendFormat("\r  ON SECRET FLOOR %d", (slot <= 0 ? 0 : slot-10+1) + 1);
		else
			msg.AppendFormat("\r      ON FLOOR %d", slot);
	}
	StatusBar->DisplayInfoMessage(msg);
}

void Blake_BarrierApply()
{
	if(!applyPending || !map)
		return;
	applyPending = false;

	const unsigned int levelNum = levelInfo->LevelNumber;

	// Index this level's barrier actors by tile for the group flood fills.
	TMap<unsigned int, AActor*> cells;
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor * const actor = iter;
		if(IsBarrier(actor))
			cells[(actor->tilex<<8)|actor->tiley] = actor;
	}

	// Converge every group keyed to this level. The link coordinate itself
	// need not be a barrier; seed its neighbours too. Flooded cells are
	// consumed so overlapping entries follow the first one, as in bstone.
	TMap<unsigned int, bool>::ConstIterator iter(barrierTable);
	TMap<unsigned int, bool>::ConstPair *pair;
	while(iter.NextPair(pair))
	{
		if((pair->Key>>16) != levelNum)
			continue;
		const bool on = pair->Value;

		const unsigned int seed = pair->Key&0xFFFF;
		TArray<unsigned int> stack;
		stack.Push(seed);
		if((seed>>8) > 0)
			stack.Push(seed-0x100);
		if((seed>>8) < 0xFF)
			stack.Push(seed+0x100);
		if((seed&0xFF) > 0)
			stack.Push(seed-1);
		if((seed&0xFF) < 0xFF)
			stack.Push(seed+1);
		while(stack.Size())
		{
			const unsigned int c = stack[stack.Size()-1];
			stack.Delete(stack.Size()-1);

			AActor **actor = cells.CheckKey(c);
			if(!actor)
				continue;
			ConvergeActor(*actor, on);
			cells.Remove(c);

			if((c>>8) > 0)
				stack.Push(c-0x100);
			if((c>>8) < 0xFF)
				stack.Push(c+0x100);
			if((c&0xFF) > 0)
				stack.Push(c-1);
			if((c&0xFF) < 0xFF)
				stack.Push(c+1);
		}
	}

	// Repaint wired switch walls to the table state.
	for(unsigned int s = 0;s < switchWalls.Size();++s)
	{
		if(const bool *on = barrierTable.CheckKey(switchWalls[s].key))
			RepaintSwitchSpot(map->GetSpot(switchWalls[s].x, switchWalls[s].y, 0), *on);
	}
}

void Blake_BarrierSerialize(FArchive &arc)
{
	if(arc.IsStoring())
	{
		DWORD count = barrierTable.CountUsed();
		arc << count;

		TMap<unsigned int, bool>::ConstIterator iter(barrierTable);
		TMap<unsigned int, bool>::ConstPair *pair;
		while(iter.NextPair(pair))
		{
			DWORD key = pair->Key;
			BYTE on = pair->Value;
			arc << key << on;
		}
	}
	else
	{
		barrierTable.Clear();

		DWORD count;
		arc << count;
		while(count--)
		{
			DWORD key;
			BYTE on;
			arc << key << on;
			barrierTable[key] = !!on;
		}
	}
}

void Blake_BarrierLoadLegacy()
{
	barrierTable.Clear();
	switchWalls.Clear();
	applyPending = false;
}
