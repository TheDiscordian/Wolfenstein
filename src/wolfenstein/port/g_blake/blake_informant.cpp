/*
** blake_informant.cpp
**
**---------------------------------------------------------------------------
** Copyright 2025 openfpgaOS
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
** Blake Stone scientist interrogation. Friendly bio-techs answer the use
** key when nothing else handled it: informants give hints and one-time
** gifts, regular scientists answer nicely or turn hostile.
**
*/

#include "wl_def.h"
#include "a_inventory.h"
#include "blake_informant.h"
#include "farchive.h"
#include "g_mapinfo.h"
#include "gamemap.h"
#include "id_ca.h"
#include "m_random.h"
#include "w_wad.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "wl_state.h"
#include "thingdef/thingdef.h"

static FRandom pr_interrogate("BlakeInterrogate");

// Earliest TimeCount at which the next interrogation may land.  Original Blake
// (3d_agent.cpp) paces interrogation with a tic cooldown so mashing use can't
// instantly drain an informant's gifts; transient, no need to serialize.
static int32_t interrogateReadyTime = 0;
static const int32_t INTERROGATE_DELAY_INFORMANT = 20;  // ~1/3 s
static const int32_t INTERROGATE_DELAY_SCIENTIST = 120; // ~2 s

// Scientist hint words from map plane 1, one list per type. The zone under
// a word's tile decides which room an informant hint belongs to; words on
// tiles without a zone form the general pool.
struct BlakeHintWord
{
	unsigned short tilex, tiley;
	unsigned char msgnum; // 1-based message number in the hint chunk
};

static TArray<BlakeHintWord> hintWords[3];
static TArray<FString> hintTexts[3];
static bool hintTextsLoaded;
static bool informantWarned;

static const char* const hintLumps[3] = { "HINTINFR", "HINTNSCI", "HINTMSCI" };

void Blake_ClearHints()
{
	for(unsigned int type = 0;type < 3;++type)
	{
		hintWords[type].Clear();
		hintTexts[type].Clear();
	}
	hintTextsLoaded = false;
	informantWarned = false;
}

void Blake_AddHint(unsigned int type, unsigned int x, unsigned int y, unsigned int msgnum)
{
	if(type >= 3 || msgnum < 1)
		return;

	BlakeHintWord word;
	word.tilex = (unsigned short)x;
	word.tiley = (unsigned short)y;
	word.msgnum = (unsigned char)msgnum;
	hintWords[type].Push(word);
}

// Splits a hint chunk into its messages. Each message ends with a "^XX"
// terminator; leading newlines are skipped and '\n' is stripped ('\r' is
// the line break the info area understands).
static void LoadHintTexts()
{
	if(hintTextsLoaded)
		return;
	hintTextsLoaded = true;

	for(unsigned int type = 0;type < 3;++type)
	{
		int lumpNum = Wads.CheckNumForName(hintLumps[type]);
		if(lumpNum < 0)
			continue;

		FMemLump lump = Wads.ReadLump(lumpNum);
		FString data = lump.GetString();

		const char *p = data.GetChars();
		for(;;)
		{
			const char *end = strstr(p, "^XX");
			if(!end)
				break;

			while(p < end && (*p == '\n' || *p == '\r'))
				++p;

			FString msg;
			for(;p < end;++p)
			{
				if(*p != '\n')
					msg += *p;
			}
			hintTexts[type].Push(msg);
			p = end + 3;
		}
	}
}

static const char *HintText(unsigned int type, unsigned int msgnum)
{
	if(msgnum < 1 || msgnum > hintTexts[type].Size())
		return "";
	return hintTexts[type][msgnum - 1];
}

static const MapZone *HintWordZone(const BlakeHintWord &word)
{
	if(!map)
		return NULL;
	return map->GetSpot(word.tilex, word.tiley, 0)->zone;
}

// Picks an informant's hint: a message placed in the informant's zone if
// the level has one, otherwise a general hint. The pick is sticky -- the
// word index is kept in temp1 while the informant stays in the zone it
// was picked for.
static FString PickInformantHint(AActor *ob)
{
	const MapZone *obZone = ob->GetZone();
	TArray<unsigned int> areaList, generalList;
	for(unsigned int i = 0;i < hintWords[BLAKE_HINT_INFORMANT].Size();++i)
	{
		const MapZone *zone = HintWordZone(hintWords[BLAKE_HINT_INFORMANT][i]);
		if(zone == NULL)
			generalList.Push(i);
		else if(zone == obZone)
			areaList.Push(i);
	}

	const TArray<unsigned int> &list = areaList.Size() ? areaList : generalList;
	const MapZone *wantZone = areaList.Size() ? obZone : NULL;
	if(!list.Size())
		return "";

	// Low byte only: the high byte carries the PS reserved-drop code.
	const int sticky = ob->temp1 & 0xFF;
	unsigned int pick;
	if(sticky > 0 && (unsigned int)(sticky - 1) < hintWords[BLAKE_HINT_INFORMANT].Size()
		&& HintWordZone(hintWords[BLAKE_HINT_INFORMANT][sticky - 1]) == wantZone)
	{
		pick = sticky - 1;
	}
	else
	{
		pick = list[pr_interrogate(list.Size())];
		ob->temp1 = (short)((ob->temp1 & ~0xFF) | (pick + 1));
	}

	return HintText(BLAKE_HINT_INFORMANT, hintWords[BLAKE_HINT_INFORMANT][pick].msgnum);
}

// Uniform pick over the level's nice/mean replies (never sticky).
static FString PickScientistReply(unsigned int type)
{
	if(!hintWords[type].Size())
		return "";
	const BlakeHintWord &word = hintWords[type][pr_interrogate(hintWords[type].Size())];
	return HintText(type, word.msgnum);
}

// bstone level point tally (blake_sbar.cpp): drives the near-100% location hint.
extern int Blake_LevelPointsPercent();

// Enemy display names for the near-100% location report (bstone
// get_enemy_actor_name, 3d_debug.cpp).  Pre-uppercased like bstone's report.
// Checked most-derived first so the PS guard variants win over the AoG base they
// inherit.  Bosses and hazards bstone keeps out of the countable set (Dr.
// Goldfire, the electro alien, the projection cube) are simply absent here, so
// they match no name and are skipped.  "\r" is the presenter's line break.
static const char *EnemyReportName(const AActor *ob)
{
	struct Entry { const char *cls; const char *name; };
	static const Entry table[] = {
		{ "SectorGuard",        "SECTOR GUARD" },
		{ "RentACop",           "SECTOR PATROL" },
		{ "TechWarrior",        "TECH WARRIOR" },
		{ "ProGuard",           "STAR SENTINEL" },
		{ "AlienProtector",     "ALIEN PROTECTOR" },
		{ "STARTrooper",        "STAR TROOPER" },
		{ "GeneticGuard",       "HIGH SECURITY\r GENETIC GUARD" },
		{ "MechSentinel",       "EXPERIMENTAL\r MECH SENTINEL" },
		{ "MutantHuman",        "EXPERIMENTAL\r MUTANT HUMAN" },
		{ "CyborgWarrior",      "CYBORG WARRIOR" },
		{ "SpiderMutant",       "SPIDER MUTANT" },
		{ "ReptilianWarrior",   "REPTILIAN WARRIOR" },
		{ "AcidDragon",         "ACID DRAGON" },
		{ "BreatherBeast",      "BREATHER BEAST" },
		{ "BioMechGuardian",    "BIO-MECH GUARDIAN" },
		{ "GiantStalker",       "THE GIANT STALKER" },
		{ "SpectorDemon",       "THE SPECTOR DEMON" },
		{ "ArmoredStalker",     "THE ARMORED STALKER" },
		{ "CrawlerBeast",       "THE CRAWLER BEAST" },
		{ "MorphedGoldfire",    "MORPHED DR. GOLDFIRE" },
		{ "LiquidAlien",        "FLUID ALIEN" },
		{ "PODAlien",           "POD ALIEN" },
		{ "FloatingBomb",       "PERSCAN DRONE" },
		{ "VolatileTransport",  "VOLATILE\r MATERIAL TRANSPORT" },
		{ "GurneyMutant",       "MUTATED GUARD" },
		{ "SmallCanisterAlien", "SMALL EXPERIMENTAL\r GENETIC ALIEN" },
		{ "LargeCanisterAlien", "LARGE EXPERIMENTAL\r GENETIC ALIEN" },
		{ "ElectroSphere",      "PLASMA SPHERE" },
	};
	static const unsigned int N = sizeof(table) / sizeof(table[0]);
	static const ClassDef *cls[N];
	static bool inited = false;
	if(!inited)
	{
		for(unsigned int i = 0;i < N;++i)
			cls[i] = ClassDef::FindClass(table[i].cls);
		inited = true;
	}
	for(unsigned int i = 0;i < N;++i)
		if(cls[i] && ob->IsKindOf(cls[i]))
			return table[i].name;
	return NULL;
}

// Treasure display names (bstone get_bonus_item_name).  Plural for the multi-bar
// gold piles so the report reads "There are ...".
static const char *TreasureReportName(const AActor *ob, bool &plural)
{
	struct Entry { const char *cls; const char *name; bool plural; };
	static const Entry table[] = {
		{ "MoneyBag",  "MONEY BAG",       false },
		{ "Loot",      "LOOT",            false },
		{ "Gold1Bar",  "GOLD BAR",        false },
		{ "Gold2Bars", "TWO GOLD BARS",   true },
		{ "Gold3Bars", "THREE GOLD BARS", true },
		{ "Gold5Bars", "FIVE GOLD BARS",  true },
		{ "XylanOrb",  "XYLAN ORB",       false },
	};
	static const unsigned int N = sizeof(table) / sizeof(table[0]);
	static const ClassDef *cls[N];
	static bool inited = false;
	if(!inited)
	{
		for(unsigned int i = 0;i < N;++i)
			cls[i] = ClassDef::FindClass(table[i].cls);
		inited = true;
	}
	for(unsigned int i = 0;i < N;++i)
		if(cls[i] && ob->IsKindOf(cls[i]))
		{
			plural = table[i].plural;
			return table[i].name;
		}
	return NULL;
}

// bstone find_countable_enemy: the first live enemy still worth points.  The name
// table omits the skipped classes, so an empty name stands in for bstone's skip
// list.
static AActor *FindCountableEnemy()
{
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(ob->health <= 0 || !(ob->flags & FL_COUNTKILL))
			continue;
		if(EnemyReportName(ob))
			return ob;
	}
	return NULL;
}

// bstone find_bonus_item: the first uncollected treasure (picked-up items are
// already gone from the actor list).
static AActor *FindBonusItem(bool &plural)
{
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(TreasureReportName(ob, plural))
			return ob;
	}
	return NULL;
}

// bstone Interrogate near-100% branch (3d_agent.cpp:3470): once the floor is
// 97-99% cleared by points, the informant stops giving general hints and instead
// calls out where a remaining enemy -- or, failing that, treasure -- is, so the
// player can chase down the last percent.  Empty result means "not in the window,
// fall back to the normal hint".
static FString Blake_InformantLocationReport(AActor *playerMo)
{
	const int pct = Blake_LevelPointsPercent();
	if(pct <= 96 || pct >= 100)
		return FString();

	FString msg;
	if(AActor *enemy = FindCountableEnemy())
	{
		msg.Format(" THERE IS\r%s\r AT %d,%d  (%d,%d)", EnemyReportName(enemy),
			enemy->tilex, enemy->tiley, playerMo->tilex, playerMo->tiley);
		return msg;
	}

	bool plural = false;
	if(AActor *item = FindBonusItem(plural))
	{
		const char *name = TreasureReportName(item, plural);
		if(plural)
			msg.Format(" THERE ARE %s\r AT %d,%d  (%d,%d)", name,
				item->tilex, item->tiley, playerMo->tilex, playerMo->tiley);
		else
			msg.Format(" THERE IS A\r%s\r AT %d,%d  (%d,%d)", name,
				item->tilex, item->tiley, playerMo->tilex, playerMo->tiley);
		return msg;
	}

	return FString(" YOU HAVE COLLECTED\r ALL TREASURES");
}

// FirstSighting equivalent (wl_state.cpp keeps it static). bstone only
// clears FL_FRIENDLY and lets the next SightPlayer call aggro the mean
// scientist; the port provokes him directly.
static void ProvokeScientist(AActor *ob, AActor *playerMo)
{
	PlaySoundLocActor(ob->seesound, ob);
	ob->speed = ob->runspeed;
	if(ob->distance < 0)
		ob->distance = 0;
	ob->flags &= ~(FL_PATHING|FL_FRIENDLY);
	ob->flags |= FL_ATTACKMODE|FL_FIRSTATTACK;
	ob->target = playerMo;
	if(ob->SeeState)
		ob->SetState(ob->SeeState);
}

bool Blake_TryInterrogate(AActor *playerMo)
{
	if(!playerMo || !playerMo->player)
		return false;

	// One attempt per use press.
	if(control[playerMo->player->GetPlayerNum()].buttonheld[bt_use])
		return false;

	// Pace interrogation like the original (3d_agent.cpp) so mashing use can't
	// instantly milk an informant.  TimeCount runs backward on a new level or
	// load, so a ready time more than the longest delay ahead means the clock
	// reset -- don't block in that case.
	const int32_t now = gamestate.TimeCount;
	if(now < interrogateReadyTime &&
		interrogateReadyTime - now <= INTERROGATE_DELAY_SCIENTIST)
		return false;

	static const ClassDef * const genCls = ClassDef::FindClass("GeneralScientist");
	static const ClassDef * const infCls = ClassDef::FindClass("InformantScientist");
	if(!genCls)
		return false;

	// Nearest friendly scientist within a 2 tile box and a 45 degree facing
	// cone; distance is the smaller axis delta, under one tile.
	AActor *best = NULL;
	fixed bestDist = FRACUNIT;
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		AActor *ob = iter;
		if(ob == playerMo || !ob->IsKindOf(genCls))
			continue;
		if(ob->health <= 0 || (ob->flags & FL_ATTACKMODE))
			continue;
		if(abs((int)ob->tilex - (int)playerMo->tilex) > 2 ||
			abs((int)ob->tiley - (int)playerMo->tiley) > 2)
			continue;

		const fixed dx = abs(playerMo->x - ob->x);
		const fixed dy = abs(playerMo->y - ob->y);
		const fixed dist = dx < dy ? dx : dy;
		if(dist >= bestDist)
			continue;

		if(!playerMo->CheckVisibility(ob, ANGLE_45/2))
			continue;

		best = ob;
		bestDist = dist;
	}

	if(!best)
		return false;

	LoadHintTexts();

	const bool informant = infCls && best->IsKindOf(infCls);
	FString msg = "INTERROGATE:";
	FString reply;

	if(informant)
	{
		msg += " ^FC3aINFORMANT^FCa6";

		// Gifts come on later interrogations, once each ever.
		if(best->flags & FL_INTERROGATED)
		{
			static const ClassDef * const chargeCls = ClassDef::FindClass("ChargeUnit");
			static const ClassDef * const coinCls = ClassDef::FindClass("ConcessionCoin");

			AInventory *charge = chargeCls ? playerMo->FindInventory(chargeCls) : NULL;
			AInventory *coins = coinCls ? playerMo->FindInventory(coinCls) : NULL;

			if(!(best->flags & FL_GAVEAMMO) && chargeCls &&
				(!charge || charge->amount < charge->maxamount))
			{
				playerMo->GiveInventory(chargeCls, pr_interrogate(8) + 1);
				best->flags |= FL_GAVEAMMO;
				reply = " HEY BLAKE,\r TAKE MY CHARGE PACK!";
			}
			else if(!(best->flags & FL_GAVETOKENS) && coinCls &&
				(!coins || coins->amount < coins->maxamount))
			{
				playerMo->GiveInventory(coinCls, 5);
				best->flags |= FL_GAVETOKENS;
				reply = " HEY BLAKE,\r TAKE MY FOOD TOKENS!";
			}
		}

		if(reply.IsEmpty())
		{
			// Near 100%: point Blake at the last enemy/treasure instead of a hint.
			reply = Blake_InformantLocationReport(playerMo);
			if(reply.IsEmpty())
				reply = PickInformantHint(best);
			best->flags |= FL_INTERROGATED;
		}
	}
	else
	{
		if((best->flags & FL_MUSTATTACK) || (pr_interrogate() & 1))
		{
			best->flags |= FL_INTERROGATED;
			ProvokeScientist(best, playerMo);
			reply = PickScientistReply(BLAKE_HINT_MEAN);
		}
		else
		{
			// Nice once, but the next interrogation turns him.
			best->flags |= FL_MUSTATTACK;
			reply = PickScientistReply(BLAKE_HINT_NICE);
		}
	}

	msg += "\r\r";
	msg += reply;
	msg += "^XX";

	StatusBar->DisplayInfoMessage(msg, 0x200, 600);
	PlaySoundLocActor("misc/interrogate", playerMo);

	// Informants recharge quickly; a provoked/normal scientist takes longer,
	// matching the original's per-type delay.
	interrogateReadyTime = now +
		(informant ? INTERROGATE_DELAY_INFORMANT : INTERROGATE_DELAY_SCIENTIST);
	return true;
}

// =============================================================================
// Informant census (bstone stats.total_inf/accum_inf)
//
// Per-floor totals for the panel's INFORMANTS ALIVE ratio, keyed by
// LevelNumber.  AActor::Spawn counts informants as they spawn (covering the
// ScientistSpawner roll, which resolves on the first thinker tick); deaths
// decrement.  Saved in its own inFs chunk; chunk absent = legacy save.
// =============================================================================

struct InformantStats
{
	BYTE total;
	BYTE alive;
};
static InformantStats infStats[128];

void Blake_InformantsClear()
{
	memset(infStats, 0, sizeof(infStats));
}

// Fresh spawns only: a floor snapshot restore keeps its census.
void Blake_InformantsReset()
{
	if(!levelInfo)
		return;
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl >= (int)countof(infStats))
		return;
	infStats[lvl].total = infStats[lvl].alive = 0;
}

void Blake_InformantSpawned(AActor *actor)
{
	static const ClassDef * const infCls = ClassDef::FindClass("InformantScientist");
	if(!infCls || !actor->IsKindOf(infCls) || !levelInfo)
		return;
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl >= (int)countof(infStats))
		return;
	if(infStats[lvl].total < 255)
	{
		++infStats[lvl].total;
		++infStats[lvl].alive;
	}
}

int Blake_InformantsTotal(int lvl)
{
	if(lvl < 1 || lvl >= (int)countof(infStats))
		return 0;
	return infStats[lvl].total;
}

int Blake_InformantsAlive(int lvl)
{
	if(lvl < 1 || lvl >= (int)countof(infStats))
		return 0;
	return infStats[lvl].alive;
}

void Blake_InformantSerialize(FArchive &arc)
{
	DWORD count = countof(infStats);
	arc << count;
	if(!arc.IsStoring())
		Blake_InformantsClear();
	for(DWORD i = 0;i < count;++i)
	{
		BYTE total = i < countof(infStats) ? infStats[i].total : 0;
		BYTE alive = i < countof(infStats) ? infStats[i].alive : 0;
		arc << total << alive;
		if(i < countof(infStats))
		{
			infStats[i].total = total;
			infStats[i].alive = alive;
		}
	}
}

// Shown when an informant is killed; once per level plus a 25/256 chance
// on repeats (bstone tracks the once-flag per game instead).
ACTION_FUNCTION(A_InformantDeath)
{
	static const char* const dkiMsg =
		"^FC39  YOU JUST SHOT AN\r"
		"        INFORMANT!\r"
		"^FC79 ONLY SHOOT BIO-TECHS\r"
		"  THAT SHOOT AT YOU!\r"
		"^FC19        DO NOT SHOOT\r"
		"        INFORMANTS!!\r";

	if(levelInfo)
	{
		const int lvl = levelInfo->LevelNumber;
		if(lvl >= 1 && lvl < (int)countof(infStats) && infStats[lvl].alive > 0)
			--infStats[lvl].alive;
	}

	if(!informantWarned || pr_interrogate() < 25)
	{
		StatusBar->DisplayInfoMessage(dkiMsg, 0x1FF, 900);
		informantWarned = true;
	}
	return true;
}
