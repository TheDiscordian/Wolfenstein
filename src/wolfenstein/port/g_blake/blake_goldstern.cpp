/*
** blake_goldstern.cpp
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
** Dr. Goldfire's hunt (bstone CheckSpawnGoldstern and friends). Map code
** 124 registers a teleport site; while he is off the map a timer counts
** down and he warps in at a site the player can see. Killing him is only
** an escape -- except on the PS final level, where he morphs instead.
**
*/

#include "wl_def.h"
#include "blake_goldstern.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "m_random.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_goldstern("Goldstern");

// Wait times are 70Hz tics, matching the once-per-tic Goldstern_Tick.
enum
{
	GOLDIE_MAX_SPAWNS = 10,
	MIN_GOLDIE_FIRST_WAIT = 5*60,
	MAX_GOLDIE_FIRST_WAIT = 15*60,
	MIN_GOLDIE_WAIT = 30*60,
	MAX_GOLDIE_WAIT = 4*60*60,
	GOLDIE_POINTS = 5000,
	GOLD_MORPH_LEVEL = 19
};

enum { GS_NEEDCOORD, GS_FIRSTTIME, GS_COORDFOUND, GS_NO_MORE };

static struct
{
	int lastIndex;
	int spawnCnt;
	int flags;
	int waitTime;
	bool goldSpawned;
	bool pendingImmediate;
} gold;

static struct { unsigned short tilex, tiley; } goldieList[GOLDIE_MAX_SPAWNS];
static bool bossKeyDropped;

// gamestate.mapon equivalent: floor index within the episode.
static int Mapon()
{
	if(EpisodeInfo::GetNumEpisodes() > 1)
		return ((int)levelInfo->LevelNumber - 1)%15;
	return (int)levelInfo->LevelNumber - 1;
}

void Goldstern_Clear()
{
	gold.lastIndex = GOLDIE_MAX_SPAWNS;
	gold.spawnCnt = 0;
	gold.flags = GS_NEEDCOORD;
	gold.waitTime = 0;
	gold.goldSpawned = false;
	gold.pendingImmediate = false;
	bossKeyDropped = false;
}

void Goldstern_AddSite(unsigned int x, unsigned int y, bool immediate)
{
	if(gold.spawnCnt >= GOLDIE_MAX_SPAWNS)
		return;

	if(immediate)
	{
		// 141: PS only. The spawn itself waits for the first tic so the
		// world is live.
		if(EpisodeInfo::GetNumEpisodes() > 1)
			return;
		goldieList[gold.spawnCnt].tilex = (unsigned short)x;
		goldieList[gold.spawnCnt].tiley = (unsigned short)y;
		gold.lastIndex = gold.spawnCnt++;
		gold.flags = GS_COORDFOUND;
		gold.goldSpawned = true;
		gold.pendingImmediate = true;
		return;
	}

	gold.flags = GS_FIRSTTIME;
	if(Mapon() == 9)
		gold.waitTime = 60;
	else
		gold.waitTime = MIN_GOLDIE_FIRST_WAIT + pr_goldstern(MAX_GOLDIE_FIRST_WAIT - MIN_GOLDIE_FIRST_WAIT);
	goldieList[gold.spawnCnt].tilex = (unsigned short)x;
	goldieList[gold.spawnCnt].tiley = (unsigned short)y;
	gold.spawnCnt++;
}

// Guards against a double spawn when site state was rebuilt by a map
// parse but the actor came back with a loaded game. Only a live one
// counts -- an escaping corpse is still playing its warp-out.
static bool GoldfireExists()
{
	static const ClassDef * const goldCls = ClassDef::FindClass("DrGoldfire");
	static const ClassDef * const morphCls = ClassDef::FindClass("MorphedGoldfire");

	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(ob->health > 0 &&
			((goldCls && ob->IsKindOf(goldCls)) || (morphCls && ob->IsKindOf(morphCls))))
			return true;
	}
	return false;
}

// actorat[] equivalent: a solid actor on the tile blocks the spawn.
static bool TileOccupied(int tx, int ty)
{
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if((ob->flags & FL_SOLID) && (int)ob->tilex == tx && (int)ob->tiley == ty)
			return true;
	}
	return false;
}

static void SpawnGoldfire()
{
	static const ClassDef * const goldCls = ClassDef::FindClass("DrGoldfire");
	if(!goldCls)
		return;

	const fixed x = ((fixed)goldieList[gold.lastIndex].tilex<<TILESHIFT)+TILEGLOBAL/2;
	const fixed y = ((fixed)goldieList[gold.lastIndex].tiley<<TILESHIFT)+TILEGLOBAL/2;
	AActor *gf = AActor::Spawn(goldCls, x, y, 0, SPAWN_AllowReplacement);
	if(!gf)
		return;

	// get_start_hit_point per game, x15 on a boss floor.
	static const int aogHP[4] = {50, 63, 125, 188};
	static const int psHP[4] = {90, 100, 150, 250};
	unsigned int skill = gamestate.difficulty->SpawnFilter;
	if(skill > 3)
		skill = 3;
	int hp = (EpisodeInfo::GetNumEpisodes() > 1 ? aogHP : psHP)[skill];
	if(Mapon() == 9)
		hp *= 15;
	gf->health = hp;
}

// bstone CheckSight(player, site): close range is automatic, otherwise the
// site must be in the player's facing half-plane (cardinals only) with a
// clear line to it.
static bool PlayerSeesSite(AActor *playerMo, int tx, int ty)
{
	static const fixed MINSIGHT = 0x18000l*64;

	const fixed x = ((fixed)tx<<TILESHIFT)+TILEGLOBAL/2;
	const fixed y = ((fixed)ty<<TILESHIFT)+TILEGLOBAL/2;
	const fixed deltax = x - playerMo->x;
	const fixed deltay = y - playerMo->y;

	if(abs(deltax) <= MINSIGHT && abs(deltay) <= MINSIGHT)
		return true;

	switch((playerMo->angle + ANGLE_45/2)/ANGLE_45 % 8)
	{
		case north: if(deltay > 0) return false; break;
		case east:  if(deltax < 0) return false; break;
		case south: if(deltay < 0) return false; break;
		case west:  if(deltax > 0) return false; break;
		default: break;
	}

	return CheckLine(playerMo, x, y, tx, ty);
}

static void FindNewGoldieSpawnSite(AActor *playerMo)
{
	gold.waitTime = 0;

	for(int i = 0;i < gold.spawnCnt;++i)
	{
		// Avoid repeating the last site.
		if(gold.spawnCnt > 1 && i == gold.lastIndex)
			continue;

		if(!PlayerSeesSite(playerMo, goldieList[i].tilex, goldieList[i].tiley))
			continue;

		gold.lastIndex = i;
		if(Mapon() == 9)
			gold.waitTime = 60; // boss floor: get him out quick
		else if(gold.flags == GS_FIRSTTIME)
			gold.waitTime = MIN_GOLDIE_FIRST_WAIT + pr_goldstern(MAX_GOLDIE_FIRST_WAIT - MIN_GOLDIE_FIRST_WAIT);
		else
			gold.waitTime = MIN_GOLDIE_WAIT + pr_goldstern(MAX_GOLDIE_WAIT - MIN_GOLDIE_WAIT);
		gold.flags = GS_COORDFOUND;
		break;
	}
}

void Goldstern_Tick()
{
	if(!gold.spawnCnt || !players[0].mo)
		return;

	if(gold.pendingImmediate)
	{
		gold.pendingImmediate = false;
		if(!GoldfireExists())
			SpawnGoldfire();
		return;
	}

	if(gold.goldSpawned || gold.flags == GS_NO_MORE)
		return;

	// CheckSpawnGoldstern
	// simStepMult tics of wait elapse per call under the fixed step.  waitTime
	// only gates the spawn check (no per-value action), so subtracting
	// simStepMult is exact; clamp at 0 so it can't run negative.  simStepMult==1
	// is the stock single decrement.
	if(gold.waitTime > 0)
	{
		gold.waitTime -= simStepMult;
		if(gold.waitTime < 0)
			gold.waitTime = 0;
		return;
	}

	if(gold.flags == GS_COORDFOUND)
	{
		const int tx = goldieList[gold.lastIndex].tilex;
		const int ty = goldieList[gold.lastIndex].tiley;
		AActor *playerMo = players[0].mo;

		if(!TileOccupied(tx, ty) &&
			abs((int)playerMo->tilex - tx) > 1 && abs((int)playerMo->tiley - ty) > 1)
		{
			if(!GoldfireExists())
				SpawnGoldfire();
			gold.goldSpawned = true;
		}
	}
	else
		FindNewGoldieSpawnSite(players[0].mo);
}

// KillActor goldsternobj: on the PS morph level death is the morph
// hand-off; anywhere else he laughs, warps out and the hunt restarts.
ACTION_FUNCTION(A_GoldfireDie)
{
	if(EpisodeInfo::GetNumEpisodes() == 1 && Mapon() == GOLD_MORPH_LEVEL)
	{
		const Frame *frame = self->FindState("Morph");
		if(frame)
		{
			if(result)
				result->JumpFrame = frame;
			else
				self->SetState(frame);
			return false;
		}
	}

	gold.flags = GS_NEEDCOORD;
	gold.goldSpawned = false;
	gold.waitTime = MIN_GOLDIE_WAIT + pr_goldstern(MAX_GOLDIE_WAIT - MIN_GOLDIE_WAIT);

	if(self->target && self->target->player)
		self->target->player->GivePoints(GOLDIE_POINTS);
	else
		players[0].GivePoints(GOLDIE_POINTS);

	// AOG boss floor: the first kill drops the gold access card.
	if(EpisodeInfo::GetNumEpisodes() > 1 && Mapon() == 9 && !bossKeyDropped)
	{
		bossKeyDropped = true;
		static const ClassDef * const keyCls = ClassDef::FindClass("GoldAccessCard");
		if(keyCls)
		{
			static const fixed TILEMASK = ~(TILEGLOBAL-1);
			AActor::Spawn(keyCls, (self->x&TILEMASK)+TILEGLOBAL/2, (self->y&TILEMASK)+TILEGLOBAL/2, 0, SPAWN_AllowReplacement);
		}
	}
	return true;
}

// KillActor gold_morphobj: no more Goldfire this level.
ACTION_FUNCTION(A_GoldMorphDie)
{
	gold.flags = GS_NO_MORE;
	return true;
}
