/*
** blake_sbar.cpp
**
**---------------------------------------------------------------------------
** Copyright 2013 Braden Obrzut
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
**
*/

#include "wl_def.h"
#include "a_inventory.h"
#include "a_keys.h"
#include "colormatcher.h"
#include "id_ca.h"
#include "id_sd.h"
#include "id_us.h"
#include "id_vh.h"
#include "g_mapinfo.h"
#include "v_font.h"
#include "v_video.h"
#include "wl_agent.h"
#include "wl_def.h"
#include "wl_game.h"
#include "wl_iwad.h"
#include "wl_play.h"
#include "r_sprites.h"
#include "xs_Float.h"
#include "thingdef/thingdef.h"
#include "of_ecwolf_gpu.h"

enum
{
	STATUSLINES = 48,
	STATUSTOPLINES = 16
};

class BlakeStatusBar : public DBaseStatusBar
{
public:
	BlakeStatusBar() : CurrentScore(0), ScoreRollWait(0), InfoMessagePriority(0), InfoMessageTics(0),
		StartupMsgPending(false), EcgScrollTics(0), HeartTics(0), HeartBright(false)
	{
		memset(EcgLegend, 0, sizeof(EcgLegend));
		memset(EcgSegments, 0, sizeof(EcgSegments));
		iconNF = iconFrame = iconAnimTics = 0;
		iconKey = 0;
	}

	void DrawStatusBar();
	unsigned int GetHeight(bool top)
	{
		if(viewsize == 21)
			return 0;
		return top ? STATUSTOPLINES : STATUSLINES;
	}

	void NewGame()
	{
		CurrentScore = players[ConsolePlayer].score;
		InfoMessage = "";
		InfoMessagePriority = 0;
		InfoMessageTics = 0;
		memset(EcgLegend, 0, sizeof(EcgLegend));
		memset(EcgSegments, 0, sizeof(EcgSegments));
		EcgScrollTics = 0;
		HeartTics = 0;
		HeartBright = false;

		// Blake's start-of-game info-area greeting (bstone DrawPlayScreen's
		// InitInfoMsg path): armed by NewGameMessage() when a game is (re)started
		// or the player respawns after dying, and shown here, after the clear
		// above, so it survives into the level.  Priority 0 leaves it overridable
		// by any gameplay message, matching the original's "system" message; the
		// persist count keeps it up until then instead of decaying to idle
		// (bstone shows it with DisplayTime 0).
		if(StartupMsgPending)
		{
			StartupMsgPending = false;
			DisplayInfoMessage("R.E.B.A.\rAGENT: BLAKE STONE\rALL SYSTEMS READY.", 0, INFOMSG_PERSIST);
		}
	}

	void NewGameMessage() { StartupMsgPending = true; }

	void Tick();

	// Drains one queued pinball bonus into the info area when it's free
	// (bstone DisplayPinballBonus).  Public so GivePoints' check can poke it.
	void DrainPinballBonus();

	void DisplayInfoMessage(const char *msg, int priority, int tics)
	{
		if(priority < InfoMessagePriority)
			return;

		InfoMessage = msg;
		InfoMessagePriority = priority;
		InfoMessageTics = tics;
		iconNF = iconFrame = iconAnimTics = 0;	// pickups/attacks re-bake via SetInfoMessageIcon
	}

	// Sets the icon drawn in the info area's left box for the current message
	// (bstone ^SH/^AN): bakes the class's walk-cycle frames (enemies) or spawn
	// frame (items).  Pass NULL to clear.
	void SetInfoMessageIcon(const class ClassDef *cls);

protected:
	void DrawInfoArea();
	void DrawLed(double percent, double x, double y) const;
	void DrawString(FFont *font, const char* string, double x, double y, bool shadow, EColorRange color=CR_UNTRANSLATED, bool center=false) const;

	// A negative tic count means the message persists until a higher- or
	// equal-priority message replaces it, never reverting to idle on the clock
	// (bstone DISPLAY_MSG / DisplayTime 0, e.g. the start-game greeting).
	static const int INFOMSG_PERSIST = -1;

	// Info-area enemy walk-cycle frame delay, in game-tics (bstone piAnimTable
	// maxdelay 20 @ TICRATE 70).  Accumulated against `tics` (not render frames)
	// so the speed is frame-rate independent and matches the original.
	static const int ICON_ANIM_DELAY = 20;

private:
	int CurrentScore;
	int ScoreRollWait;        // tics to show the "-ROLL-" placeholder after a score roll
	FString InfoMessage;
	int InfoMessagePriority;
	int InfoMessageTics;
	// Info-area icon (bstone ^SH/^AN): the item/enemy sprite drawn in a black box
	// at the info area's left, baked from the sprite into plain paletted buffers at
	// set-time (during the tick) so the per-frame draw never touches a sprite
	// texture during the GPU 3D frame -- doing so corrupts the Pocket GPU's column
	// state (white lines in the 3D view).  Enemies animate their walk cycle (bstone
	// ^AN); items are a single frame (bstone ^SH).  iconNF 0 = no icon.
	static const int MAX_ICON_FRAMES = 6;
	int iconNF;                   // number of baked frames (0 = none)
	int iconFrame;                // current animation frame
	int iconAnimTics;             // tic accumulator for the walk cycle
	int iconW[MAX_ICON_FRAMES], iconH[MAX_ICON_FRAMES];
	// Opaque-content bounds within the (often padded) sprite frame, so the figure
	// is centred in the box by its actual pixels, not the canvas.
	int iconCX0[MAX_ICON_FRAMES], iconCY0[MAX_ICON_FRAMES];
	int iconCW[MAX_ICON_FRAMES], iconCH[MAX_ICON_FRAMES];
	int32_t iconKey;              // source class id, for the info-area cache key
	uint8_t iconPix[MAX_ICON_FRAMES][64*64];   // native-size paletted pixels (column-major)
	uint8_t iconMask[MAX_ICON_FRAMES][64*64];  // 1 = opaque
	bool StartupMsgPending;   // show the new-game greeting on the next NewGame()

	// AoG health monitor state (bstone DrawHealthMonitor).
	int EcgLegend[6];
	int EcgSegments[6];
	int EcgScrollTics;
	int HeartTics;
	bool HeartBright;
};

DBaseStatusBar *CreateStatusBar_Blake() { return new BlakeStatusBar(); }

// The DOS "ATTACKING:" info-area message per enemy class (bstone ActorInfoMsg,
// 3d_msgs.cpp, AOG variant).  ^FC17 makes "ATTACKING:" red and ^FCA6 the name
// grey (both DOS-faithful); the icon is set separately, not via the ^AN code.
namespace {
struct BlakeAttackMsg { const char* className; const char* msg; };
const BlakeAttackMsg blakeAttackMsgs[] = {
	{ "RentACop",                "^FC17\r\r  ATTACKING:\r^FCA6SECTOR PATROL" },
	{ "SectorGuard",             "^FC17\r\r  ATTACKING:\r^FCA6 SECTOR GUARD" },
	{ "ProGuard",                "^FC17\r\r  ATTACKING:\r^FCA6STAR SENTINEL" },
	{ "TechWarrior",             "^FC17\r\r  ATTACKING:\r^FCA6 TECH WARRIOR" },
	{ "STARTrooper",             "^FC17\r\r  ATTACKING:\r^FCA6 STAR TROOPER" },
	{ "AlienProtector",          "^FC17  ATTACKING:\r^FCA6    ALIEN\r  PROTECTOR" },
	{ "GeneralScientist",        "^FC17\r\r  ATTACKING:\r^FCA6   BIO-TECH" },
	{ "FloatingBomb",            "^FC17  ATTACKING:\r^FCA6PERSCAN DRONE\r  EXPLOSION" },
	{ "VolatileTransport",       "^FC17  ATTACKING:\r^FCA6 VOLATILE MAT.\r  TRANSPORT\r  EXPLOSION" },
	{ "GeneticGuard",            "^FC17  ATTACKING:\r^FCA6 HIGH-SECURITY\r GENETIC GUARD" },
	{ "CyborgWarrior",           "^FC17  ATTACKING:\r^FCA6   CYBORG\r   WARRIOR" },
	{ "SpiderMutant",            "^FC17  ATTACKING:\r^FCA6   SPIDER\r   MUTANT" },
	{ "SpiderMutantMorphed",     "^FC17  ATTACKING:\r^FCA6   SPIDER\r   MUTANT" },
	{ "AcidDragon",              "^FC17\r  ATTACKING:\r^FCA6 ACID DRAGON" },
	{ "BreatherBeast",           "^FC17  ATTACKING:\r^FCA6   BREATHER\r    BEAST" },
	{ "BioMechGuardian",         "^FC17  ATTACKING:\r^FCA6   BIO-MECH\r   GUARDIAN" },
	{ "ReptilianWarrior",        "^FC17  ATTACKING:\r^FCA6  REPTILIAN\r   WARRIOR" },
	{ "ReptilianWarriorMorphed", "^FC17  ATTACKING:\r^FCA6  REPTILIAN\r   WARRIOR" },
	{ "MechSentinel",            "^FC17  ATTACKING:\r^FCA6 EXPERIMENTAL\r MECH-SENTINEL" },
	{ "MutantHuman",             "^FC17  ATTACKING:\r^FCA6 EXPERIMENTAL\r MUTANT HUMAN" },
	{ "MutantHumanMorphed",      "^FC17  ATTACKING:\r^FCA6 EXPERIMENTAL\r MUTANT HUMAN" },
	{ "SmallCanisterAlien",      "^FC17  ATTACKING:\r^FCA6 EXPERIMENTAL\r GENETIC ALIEN" },
	{ "LargeCanisterAlien",      "^FC17  ATTACKING:\r^FCA6 EXPERIMENTAL\r GENETIC ALIEN" },
	{ "GurneyMutant",            "^FC17  ATTACKING:\r^FCA6   MUTATED\r    GUARD" },
	{ "PODAlien",                "^FC17\r  ATTACKING:\r^FCA6  POD ALIEN" },
	{ "CeilingTurretRotate",     "^FC17  ATTACKING:\r^FCA6  AUTOMATED\rHEAVY ARMORED\r ROBOT TURRET" },
	{ "CeilingTurretStatic",     "^FC17  ATTACKING:\r^FCA6  AUTOMATED\rHEAVY ARMORED\r ROBOT TURRET" },
	{ "GiantStalker",            "^FC17  ATTACKING:\r^FCA6  THE GIANT\r   STALKER" },
};

// The DOS pickup info-area message per Blake pickup class (bstone BonusMsg,
// 3d_msgs.cpp, AOG variant).  ^FC57 makes the label green and ^FCA6 the name
// grey (both DOS-faithful); the icon is set separately, not via the ^SH code.
// The two ConcessionCoin tokens carry "%d" for the live food-token total.
struct BlakePickupMsg { const char* className; const char* msg; };
const BlakePickupMsg blakePickupMsgs[] = {
	{ "RedAccessCard",        "^FC57\r\r ACCESS CARD:\r^FCA6  RED LEVEL" },
	{ "YellowAccessCard",     "^FC57\r\r ACCESS CARD:\r^FCA6 YELLOW LEVEL" },
	{ "GreenAccessCard",      "^FC57\r\r ACCESS CARD:\r^FCA6 GREEN LEVEL" },
	{ "BlueAccessCard",       "^FC57\r\r ACCESS CARD:\r^FCA6  BLUE LEVEL" },
	{ "GoldAccessCard",       "^FC57\r\r ACCESS CARD:\r^FCA6  GOLD LEVEL" },
	{ "RedAccessKey",         "^FC57\r\r ACCESS CARD:\r^FCA6  RED LEVEL" },
	{ "YellowAccessKey",      "^FC57\r\r ACCESS CARD:\r^FCA6 YELLOW LEVEL" },
	{ "BlueAccessKey",        "^FC57\r\r ACCESS CARD:\r^FCA6  BLUE LEVEL" },
	{ "SlowFireProtector",    "^FC57\r\r   WEAPON:\r^FCA6  SLOW FIRE\r  PROTECTOR\r" },
	{ "RapidAssaultWeapon",   "^FC57\r\r   WEAPON:\r^FCA6 RAPID ASSAULT\r   WEAPON" },
	{ "DualNeutronDisruptor", "^FC57\r\r   WEAPON:\r^FCA6 DUAL NEUTRON\r   DISRUPTER" },
	{ "PlasmaDischargeUnit",  "^FC57\r   WEAPON:\r^FCA6   PLASMA\r DISCHARGE\r    UNIT" },
	{ "AntiPlasmaCannon",     "^FC57\r\r   WEAPON:\r^FCA6 ANTI-PLASMA\r   CANNON" },
	{ "ChargeUnit",           "^FC57\r   WEAPON:\r^FCA6 ENERGY PACK\r   (8 UNITS)" },	// both give 8 (bstone bo_clip patches msg[45])
	{ "ChargePack",           "^FC57\r   WEAPON:\r^FCA6 ENERGY PACK\r   (8 UNITS)" },
	{ "FirstAidKit",          "^FC57\r\r   HEALTH:\r^FCA6  FIRST AID\r     KIT" },
	{ "HamMeat",              "^FC57\r\r    FOOD:\r^FCA6  RAW MEAT" },
	{ "ChickenLeg",           "^FC57\r\r    FOOD:\r^FCA6  RAW MEAT" },
	{ "Sandwich",             "^FC57\r\r    FOOD:\r^FCA6  SANDWICH" },
	{ "CandyBar",             "^FC57\r\r    FOOD:\r^FCA6  CANDY BAR" },
	{ "FullWaterBowl",        "^FC57\r\r    FOOD:\r^FCA6 FRESH WATER" },
	{ "BlakeWaterPuddle",     "^FC57\r\r    FOOD:\r^FCA6 WATER PUDDLE" },
	{ "MoneyBag",             "^FC57\r\r    BONUS:\r^FCA6  MONEY BAG" },
	{ "Loot",                 "^FC57\r\r    BONUS:\r^FCA6    LOOT" },
	{ "Gold1Bar",             "^FC57\r\r    BONUS:\r^FCA6  GOLD BARS" },
	{ "Gold2Bars",            "^FC57\r\r    BONUS:\r^FCA6  GOLD BARS" },
	{ "Gold3Bars",            "^FC57\r\r    BONUS:\r^FCA6  GOLD BARS" },
	{ "Gold5Bars",            "^FC57\r\r    BONUS:\r^FCA6  GOLD BARS" },
	{ "XylanOrb",             "^FC57\r\r    BONUS:\r^FCA6  XYLAN ORB" },
	{ "ConcessionCoin",       "^FC57\r  FOOD TOKEN:\r^FCA6   1 CREDIT\r\r  TOKENS: %d" },
	{ "ConcessionCoin5",      "^FC57\r  FOOD TOKEN:\r^FCA6   5 CREDITS\r  TOKENS: %d" },
	{ "RadarPack",            "^FC57\r   RADAR:  \r^FCA6MAGNIFICATION\r   ENERGY" },
};
}

// Returns the attacker's "ATTACKING:" info message, or NULL if it's not a mapped
// Blake enemy (so Wolfenstein and unmapped actors show nothing).
const char *Blake_AttackerInfoMsg(AActor *attacker)
{
	if (!attacker)
		return NULL;
	const FName cls = attacker->GetClass()->GetName();
	for (unsigned i = 0; i < countof(blakeAttackMsgs); ++i)
		if (cls == FName(blakeAttackMsgs[i].className))
			return blakeAttackMsgs[i].msg;
	return NULL;
}

// Sets the current info-message icon to a class's spawn sprite (bstone ^SH/^AN).
// Free function so non-Blake TUs (wl_agent's attack path) can set it without the
// BlakeStatusBar type.  Must be called AFTER DisplayInfoMessage (which clears it).
void Blake_SetInfoIcon(const ClassDef *cls)
{
	extern DBaseStatusBar *StatusBar;
	if (StatusBar && IWad::CheckGameFilter("Blake"))
		static_cast<BlakeStatusBar *>(StatusBar)->SetInfoMessageIcon(cls);
}

// LINC "ACCESS DENIED" message when a locked door is tried without the key
// (bstone OperateDoor, 3d_act1.cpp:1215).  lock 1..5 = red/yellow/blue/green/
// gold (lockdefs.txt "Lock N Blake"); anything else = permanently locked.
// Blake-only -- the strings are wrong for Wolfenstein's gold/silver keys.
void Blake_DoorDeniedMsg(AActor *activator, int lock)
{
	extern DBaseStatusBar *StatusBar;
	if (!activator || !activator->player || !IWad::CheckGameFilter("Blake"))
		return;
	const char *msg;
	switch (lock)
	{
	case 1:  msg = "\r\r      RED LEVEL\r    ACCESS DENIED!"; break;
	case 2:  msg = "\r\r     YELLOW LEVEL\r    ACCESS DENIED!"; break;
	case 3:  msg = "\r\r      BLUE LEVEL\r    ACCESS DENIED!"; break;
	case 4:  msg = "\r\r     GREEN LEVEL\r    ACCESS DENIED!"; break;
	case 5:  msg = "\r\r      GOLD LEVEL\r    ACCESS DENIED!"; break;
	default: msg = "\r\r   DOOR PERMANENTLY\r        LOCKED."; break;
	}
	StatusBar->DisplayInfoMessage(msg, 0x200, 300);
}

// LINC info-area bonus message when the player grabs a pickup (bstone GetBonus
// -> DisplayInfoMsg, MP_BONUS).  Looked up by actor class; Blake-only.  Called
// from AInventory::Touch after a successful pickup, so the live token total is
// already updated for the ConcessionCoin "%d".  Takes the class (captured
// before the pickup) since the world actor may be destroyed by then.
void Blake_PickupInfoMsg(AActor *toucher, const ClassDef *itemClass)
{
	extern DBaseStatusBar *StatusBar;
	if (!itemClass || !toucher || toucher != players[ConsolePlayer].mo
		|| !IWad::CheckGameFilter("Blake"))
		return;
	const FName cls = itemClass->GetName();
	for (unsigned i = 0; i < countof(blakePickupMsgs); ++i)
	{
		if (cls != FName(blakePickupMsgs[i].className))
			continue;
		const char *tmpl = blakePickupMsgs[i].msg;
		if (strchr(tmpl, '%'))
		{
			unsigned int tokens = 0;
			static const ClassDef * const coinCls = ClassDef::FindClass("ConcessionCoin");
			if (AInventory *coins = toucher->FindInventory(coinCls))
				tokens = coins->amount;
			FString out;
			out.Format(tmpl, tokens);
			StatusBar->DisplayInfoMessage(out, 0x200, 300);
		}
		else
			StatusBar->DisplayInfoMessage(tmpl, 0x200, 300);
		// The item's own spawn sprite is the info-area icon (bstone ^SH).
		static_cast<BlakeStatusBar *>(StatusBar)->SetInfoMessageIcon(itemClass);
		return;
	}
}

// LINC weapon-select feedback (bstone CheckWeaponChange, 3d_agent.cpp:466).
// available -> "ACTIVATED AND READY", else "NOT CURRENTLY AVAILABLE".  The DOS
// game fires this on number-key selection; the Pocket has no number keys, so
// CheckWeaponChange also calls it when a next/prev cycle lands on a weapon.
void Blake_WeaponSelectMsg(bool available)
{
	extern DBaseStatusBar *StatusBar;
	if (!IWad::CheckGameFilter("Blake"))
		return;
	StatusBar->DisplayInfoMessage(available
		? "\r\r   SELECTED WEAPON\r ACTIVATED AND READY."
		: "\r\r  SELECTED WEAPON NOT\r  CURRENTLY AVAILABLE.", 0x200, 300);
}

// --- Pinball score bonuses (bstone CheckPinballBonus / DisplayPinballBonus) --
// The DOS game awards "pinball" bonuses at score milestones, shown in the info
// area above all gameplay messages (MP_PINBALL_BONUS) and drained one at a time
// as the area frees up.  State is per-level and transient (not serialised): a
// bonus queued but not yet shown is lost on save/reload -- a minor cosmetic
// difference from bstone's saved per-level queue.
//
// All seven bstone bonuses are wired: Guardian-Alien (0x01), score rolled
// (0x02), half-million "great score" (0x04), extra life (0x08), all enemies
// destroyed (0x10), all points collected (0x20) and all informants alive (0x40).
// The score-driven ones queue from Blake_CheckPinballBonus; the level-tally ones
// edge-detect in Tick (the port bumps stat counters after GivePoints, opposite
// to bstone, so a score-time check would miss the final kill/pickup).
#define MP_PINBALL_BONUS 0x3000

namespace {
struct PinballBonusInfo { int bit; const char *text; int points; bool recurring; };
const PinballBonusInfo pinballBonuses[] = {
	// bit order == display priority (bstone B_* bit order / PinballBonus table).
	{ 0x01, "^FC57    GUARDIAN ALIEN\r      DESTROYED!\r\r^FCA6   FIND THE EXIT TO\rCOMPLETE THIS MISSION",         0,       false }, // B_GALIEN_DESTROYED
	{ 0x02, "^FC57\rROLLED SCORE DISPLAY!\r^FCA6   FULL AMMO BONUS!\r  FULL HEALTH BONUS!\r1,000,000 POINT BONUS!", 1000000, true  }, // B_SCORE_ROLLED
	{ 0x04, "^FC57\r     GREAT SCORE!\r^FCA6   FULL AMMO BONUS!\r  FULL HEALTH BONUS!\r1,000,000 POINT BONUS!",     1000000, false }, // B_ONE_MILLION
	{ 0x08, "^FC57\r\r     GREAT SCORE!\r^FCA6  EXTRA LIFE BONUS!\r",                                               0,       true  }, // B_EXTRA_MAN
	{ 0x10, "^FC57\r\r ALL ENEMY DESTROYED!\r^FCA6  50,000 POINT BONUS!\r",                                        50000,   false }, // B_ENEMY_DESTROYED
	{ 0x20, "^FC57\r\r ALL POINTS COLLECTED!\r^FCA6  50,000 POINT BONUS!\r",                                       50000,   false }, // B_TOTAL_POINTS
	{ 0x40, "^FC57\r\r ALL INFORMANTS ALIVE!\r^FCA6  50,000 POINT BONUS!\r",                                       50000,   false }, // B_INFORMANTS_ALIVE
};
uint16_t pinballQueue = 0;	// bonuses earned, waiting to be shown
uint16_t pinballShown = 0;	// non-recurring bonuses already shown this level
int32_t levelPointsTotal = 0;	// bstone total_points: every award available this floor
int32_t levelPointsAccum = 0;	// bstone accum_points: points actually earned this floor

// Dormant spawner -> live monster point fixup (bstone counts the EVENTUAL
// monster's value in total_points, since these carry `points 0` and morph into
// a point-bearing monster via A_SpawnItemEx).  Values == bstone actor_points.
struct WaitFormPoints { const char *className; int points; };
const WaitFormPoints blakeWaitForms[] = {
	{ "SpiderMutantMorphed",     5000 },	// -> SpiderMutant
	{ "ReptilianWarriorMorphed", 8000 },	// -> ReptilianWarrior
	{ "MutantHumanMorphed",      6055 },	// -> MutantHuman
	{ "SmallAlienCanister",      3750 },	// -> SmallCanisterAlien
	{ "LargeAlienCanister",      6050 },	// -> LargeCanisterAlien
	{ "GurneyMutantSleep",       3750 },	// -> GurneyMutant
	{ "PODAlienEgg",             5075 },	// -> PODAlien
};

// Full weapon charge + health (bstone B_MillFunc / B_RollFunc: GiveAmmo 99,
// HealSelf 99).  Only the weapon charge (ChargeUnit) is topped up -- the food-
// token currency (ConcessionCoin) is a separate Ammo subclass and is left be.
void Blake_FullAmmoHealth()
{
	player_t &p = players[ConsolePlayer];
	if(!p.mo)
		return;
	p.health = 100;
	static const ClassDef * const chargeCls = ClassDef::FindClass("ChargeUnit");
	for(AInventory *item = p.mo->inventory; item; item = item->inventory)
		if(item->GetClass()->IsDescendantOf(chargeCls))
			item->amount = item->maxamount;
}
}

// bstone keeps these out of BOTH total_points and accum_points: electro-spheres
// and the electro alien are dynamically/specially spawned (3d_game.cpp:1099 sets
// new_actor=nullptr so they never hit the load-time total), and Goldstern/Goldfire
// is add_to_stats=false (3d_state.cpp:1321).  Excluding them from our total AND
// our accum keeps the ALL-POINTS bonus consistent.  Called from AActor::Die too.
bool Blake_PointsExcluded(AActor *a)
{
	if(!a)
		return false;
	static const ClassDef * const ex[] = {
		ClassDef::FindClass("ElectroSphere"), ClassDef::FindClass("ElectroAlien"),
		ClassDef::FindClass("DrGoldfire"),    ClassDef::FindClass("MorphedGoldfire"),
	};
	for(unsigned i = 0; i < countof(ex); ++i)
		if(ex[i] && a->IsKindOf(ex[i]))
			return true;
	return false;
}

// Sums every point award available on the floor (bstone total_points), computed
// at floor entry after SpawnThings(): treasure value + live-monster points, with
// dormant spawners counting their morphed monster's value and the excluded
// classes left out.
static int32_t Blake_ComputeLevelPointsTotal()
{
	int32_t total = 0;
	static const ClassDef * const scoreItemCls = ClassDef::FindClass("ScoreItem");
	for(AActor::Iterator it = AActor::GetIterator(); it.Next();)
	{
		AActor *a = it;
		if(!a)
			continue;
		if(scoreItemCls && a->IsKindOf(scoreItemCls))	// treasure: value is in amount
		{
			total += static_cast<AInventory *>(a)->amount;
			continue;
		}
		const FName cls = a->GetClass()->GetName();
		bool waited = false;
		for(unsigned i = 0; i < countof(blakeWaitForms); ++i)
			if(cls == FName(blakeWaitForms[i].className))
			{
				total += blakeWaitForms[i].points;
				waited = true;
				break;
			}
		if(waited)
			continue;
		if(a->points && !Blake_PointsExcluded(a))
			total += a->points;
	}
	return total;
}

// Clears the per-level pinball state (called on floor entry, after SpawnThings).
void Blake_PinballReset()
{
	pinballQueue = 0;
	pinballShown = 0;
	levelPointsAccum = 0;
	levelPointsTotal = IWad::CheckGameFilter("Blake") ? Blake_ComputeLevelPointsTotal() : 0;
}

// Guardian-Alien bonus (bstone 3d_state.cpp:1216: ActivatePinballBonus in the
// death handler for the six AoG guardian bosses, !is_ps).  Fires once per level
// on any of those boss deaths; AoG only.  Called from AActor::Die.
void Blake_GuardianAlienBonus(AActor *ob)
{
	if(!ob || !IWad::CheckGameFilter("Blake")
		|| IWad::GetGame().Name.CompareNoCase("Planet Strike") == 0)
		return;
	static const ClassDef * const bosses[] = {
		ClassDef::FindClass("CyborgWarrior"),    ClassDef::FindClass("BioMechGuardian"),
		ClassDef::FindClass("ReptilianWarrior"), ClassDef::FindClass("SpiderMutant"),
		ClassDef::FindClass("BreatherBeast"),    ClassDef::FindClass("AcidDragon"),
	};
	for(unsigned i = 0; i < countof(bosses); ++i)
		if(bosses[i] && ob->IsKindOf(bosses[i]))
		{
			if(!(pinballShown & 0x01) && !(pinballQueue & 0x01))
				pinballQueue |= 0x01;
			return;
		}
}

void BlakeStatusBar::DrainPinballBonus()
{
	if(!pinballQueue || InfoMessagePriority >= MP_PINBALL_BONUS)
		return;
	for(unsigned i = 0; i < countof(pinballBonuses); ++i)
	{
		const PinballBonusInfo &b = pinballBonuses[i];
		if(!(pinballQueue & b.bit))
			continue;
		const char *text = b.text;
		// On the final AoG episode bstone's B_GAliFunc replaces the guardian
		// message with the "projection generators" objective (3d_state.cpp:1233);
		// show that variant directly (Cluster 6 == bstone episode 5).
		if(b.bit == 0x01 && levelInfo && levelInfo->Cluster == 6)
			text = "^FC57    GUARDIAN ALIEN\r      DESTROYED!\r\r^FCA6 FIND AND DESTROY ALL\rPROJECTION GENERATORS!";
		DisplayInfoMessage(text, MP_PINBALL_BONUS, 7*60);
		SD_PlaySound("blake/rollscore");
		if(!b.recurring)
			pinballShown |= b.bit;
		pinballQueue &= ~b.bit;
		// Award the bonus points (re-enters GivePoints -> CheckPinballBonus, but
		// the message we just set blocks a nested drain, so further bonuses just
		// queue and wait for the next Tick).  add_to_stats=false: bonus points
		// never count toward the per-level points total (bstone GivePoints(,false)).
		players[ConsolePlayer].GivePoints(b.points, false);
		if(b.bit == 0x02 || b.bit == 0x04)	// score rolled / one million
			Blake_FullAmmoHealth();
		if(b.bit == 0x02)			// bstone B_RollFunc: start the "-ROLL-" display
			ScoreRollWait = 60*10;
		break;	// one bonus per drain; the rest wait until this one expires
	}
}

// Queues score-milestone bonuses (bstone CheckPinballBonus).  Blake-only.
// Called from player_t::GivePoints with the pre/post score, whether the extra-
// man threshold granted a life, and whether this award counts toward the per-
// level points total (add_to_stats: false for bonus/electro/goldstern awards).
void Blake_CheckPinballBonus(int32_t scoreBefore, int32_t scoreAfter, bool gainedLife, bool addToStats)
{
	if(!IWad::CheckGameFilter("Blake"))
		return;
	if(addToStats)
		levelPointsAccum += scoreAfter - scoreBefore;	// ScoreMultiplier==1 for Blake
	const int32_t MAX_DISPLAY_SCORE = 9999999;
	if(scoreBefore <= MAX_DISPLAY_SCORE && scoreAfter > MAX_DISPLAY_SCORE && !(pinballShown & 0x02))
		pinballQueue |= 0x02;	// B_SCORE_ROLLED (recurring -> never in shown)
	if(scoreBefore < 500000 && scoreAfter >= 500000 && !(pinballShown & 0x04))
		pinballQueue |= 0x04;	// B_ONE_MILLION
	if(gainedLife)
		pinballQueue |= 0x08;	// B_EXTRA_MAN
	if(pinballQueue)
		static_cast<BlakeStatusBar *>(StatusBar)->DrainPinballBonus();
}

void BlakeStatusBar::DrawLed(double percent, double x, double y) const
{
	static FTextureID LED[2][3] = {
		{TexMan.GetTexture("STLEDDR", FTexture::TEX_Any), TexMan.GetTexture("STLEDDY", FTexture::TEX_Any), TexMan.GetTexture("STLEDDG", FTexture::TEX_Any)},
		{TexMan.GetTexture("STLEDLR", FTexture::TEX_Any), TexMan.GetTexture("STLEDLY", FTexture::TEX_Any), TexMan.GetTexture("STLEDLG", FTexture::TEX_Any)}
	};

	unsigned int which = 0;
	if(percent > 0.71)
		which = 2;
	else if(percent > 0.38)
		which = 1;
	FTexture *dim = TexMan(LED[0][which]);
	FTexture *light = TexMan(LED[1][which]);

	double w = dim->GetScaledWidthDouble();
	double h = dim->GetScaledHeightDouble();

	screen->VirtualToRealCoords(x, y, w, h, 320, 200, true, true);

	int lightclip = xs_ToInt(static_cast<real64>(y + h*(1-percent)));
	screen->DrawTexture(dim, x, y,
		DTA_DestWidthF, w,
		DTA_DestHeightF, h,
		DTA_ClipBottom, lightclip,
		TAG_DONE);
	screen->DrawTexture(light, x, y,
		DTA_DestWidthF, w,
		DTA_DestHeightF, h,
		DTA_ClipTop, lightclip,
		TAG_DONE);
}

void BlakeStatusBar::DrawStatusBar()
{
	if(viewsize == 21 && ingame)
		return;

	// Blake's index font is VGAGRAPH font chunk 3, mapped as SMALLFNT.
	static FFont *IndexFont = SmallFont;
	static FFont *HealthFont = V_GetFont("BlakeHealthFont");
	static FFont *ScoreFont = V_GetFont("BlakeScoreFont");
	if(!IndexFont) IndexFont = SmallFont;
	if(!HealthFont) HealthFont = SmallFont;
	if(!ScoreFont) ScoreFont = SmallFont;

	static FTextureID STBar = TexMan.GetTexture("STBAR", FTexture::TEX_Any);
	static FTextureID STBarTop = TexMan.GetTexture("STTOP", FTexture::TEX_Any);

	double stx = 0;
	double sty = 200-STATUSLINES;
	double stw = 320;
	double sth = STATUSLINES;
	screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
	int boty = xs_ToInt(static_cast<real64>(sty));
	const double botStx = stx, botSty = sty, botStw = stw, botSth = sth;

	stx = 0;
	sty = 0;
	stw = 320;
	sth = STATUSTOPLINES;
	screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
	int topy = xs_ToInt(static_cast<real64>(sth));
	const double topStw = stw, topSth = sth;

	// Re-rendering the whole bar (~30 text glyphs + several blits) every frame
	// dominated the frame (~28ms / 36%), though the readouts rarely change.
	// Cache the whole composited bar keyed on the readout values: when nothing
	// changed, restore it with a memcpy and redraw only the ECG heartbeat (which
	// animates every frame).  Full-width only -- a narrow view has side borders
	// the band cache wouldn't capture, so it redraws fully.
	const bool sbarFullWidth = (unsigned)viewwidth == (unsigned)SCREENWIDTH;
	const int sbarPitch = SCREENPITCH;
	const int sbarH = SCREENHEIGHT;
	const int sbarTopBytes = topy > 0 ? topy * sbarPitch : 0;
	const int sbarBotBytes = boty < sbarH ? (sbarH - boty) * sbarPitch : 0;

	static const bool isPS = IWad::GetGame().Name.CompareNoCase("Planet Strike") == 0;
	const int curHealth = players[ConsolePlayer].health;

	// The ECG trace + heart animate independently of the readout values, so they
	// are redrawn every frame -- even on a cache hit.  (AoG only.)
	auto drawEcg = [&]() {
		if(isPS)
			return;
		for(int i = 0;i < 6;++i)
		{
			FString seg;
			seg.Format("ECGBET%02d", EcgSegments[i]);
			VWB_DrawGraphic(TexMan(seg), 120+8*i, 160);
		}
		const char* heart = "ECGGRID";
		if(curHealth > 0)
		{
			if(curHealth < 40)
				heart = "ECGBAD";
			else if(HeartBright)
				heart = "ECGGOOD";
		}
		VWB_DrawGraphic(TexMan(heart), 120, 184);
	};

	// The score rolls toward the real value a little every frame (Tick), so like
	// the ECG it is redrawn every frame -- even on a cache hit -- and kept out of
	// the key.  Drawn after the band snapshot so the cached band stays score-free
	// (transparent glyphs over a baked-in old score would ghost).
	auto drawScore = [&]() {
		FString score;
		if(CurrentScore > 9999999)	// bstone DrawScoreNum: roll the 7-digit display
		{
			if(ScoreRollWait > 0)
				score = " -ROLL-";	// N_BLANK/DASH/R/O/L/L/DASH placeholder
			else
				score.Format("%07d", CurrentScore % 10000000);
		}
		else
			score.Format("%7d", CurrentScore);
		DrawString(ScoreFont, score, 256, 155, false);
	};

	// Key = everything the bar draws except the ECG animation.  A change forces a
	// full redraw + re-cache; otherwise the cached bar is restored.
	uint32_t sbarKey[8] = {0};
	sbarKey[0] = (uint32_t)curHealth;
	sbarKey[1] = (uint32_t)players[ConsolePlayer].lives;
	sbarKey[2] = (uint32_t)levelInfo->LevelNumber;
	// NOT CurrentScore: it rolls up a little every frame (Tick) after any score
	// change, so keying on it churned the cache every frame in combat (a full
	// ~25ms redraw each frame).  The score is redrawn per-frame via drawScore().
	// Reused to key the PS idle objective hint (next-floor lock + detonator
	// possession; the level is already in sbarKey[2]) so it can't go stale.
	sbarKey[3] = 0;
	if(isPS && levelInfo)
	{
		extern bool Blake_PsFloorUnlocked(int lvl);
		unsigned int hint = Blake_PsFloorUnlocked(levelInfo->LevelNumber + 1) ? 1u : 0u;
		if(players[ConsolePlayer].mo)
		{
			static const ClassDef * const detCls = ClassDef::FindClass("PlasmaDetonator");
			if(AInventory *d = detCls ? players[ConsolePlayer].mo->FindInventory(detCls) : NULL)
				hint |= (d->amount > 0) ? 2u : 0u;
		}
		sbarKey[3] = hint;
	}
	// Key on the info-area message CONTENT, not InfoMessageTics: the timer ticks
	// down every frame while a message shows, but the drawn text is unchanged,
	// so keying on the timer churned the cache the whole time a message was up.
	// Idle (no message) keys to 0; the token count is in sbarKey[5].
	uint32_t infoKey = 0;
	if(InfoMessageTics != 0)
	{
		infoKey = 0x811c9dc5u;
		for(const char *c = InfoMessage.GetChars();c != NULL && *c;++c)
			infoKey = (infoKey ^ (unsigned char)*c) * 16777619u;
		// Fold in the icon: gold-bar variants share the text but differ in sprite,
		// and the current walk-cycle frame so animation busts the cache each step.
		infoKey = (infoKey ^ (uint32_t)iconKey) * 16777619u;
		infoKey = (infoKey ^ (uint32_t)iconFrame) * 16777619u;
		if(infoKey == 0)
			infoKey = 1;
	}
	sbarKey[4] = infoKey;
	if(AActor *pmo = players[ConsolePlayer].mo)
	{
		static const ClassDef * const coinCls = ClassDef::FindClass("ConcessionCoin");
		if(AInventory *c = coinCls ? pmo->FindInventory(coinCls) : NULL)
			sbarKey[5] = (uint32_t)c->amount;
		if(AWeapon *rw = players[ConsolePlayer].ReadyWeapon)
		{
			sbarKey[6] = (uint32_t)(uintptr_t)rw;
			if(rw->ammo[AWeapon::PrimaryFire])
			{
				const uint32_t amt = (uint32_t)rw->ammo[AWeapon::PrimaryFire]->amount;
				// The AoG auto charge pistol recharges every frame but only shows
				// READY/WAIT, so its raw ammo must not key the cache (it would
				// churn it constantly and never hit).  Other weapons / PS show
				// the actual amount, so it does.
				static const ClassDef * const autochargeCls = ClassDef::FindClass("AutoChargePistol");
				const bool autocharge = autochargeCls && rw->IsKindOf(autochargeCls);
				sbarKey[6] ^= (autocharge && !isPS) ? (amt > 0 ? 1u : 0u) : (amt << 1);
			}
		}
		unsigned int keymask = 0;
		for(AInventory *item = pmo->inventory;item != NULL;item = item->inventory)
			if(item->IsKindOf(NATIVE_CLASS(Key)))
				keymask ^= (unsigned int)(uintptr_t)item->GetClass();
		sbarKey[7] = keymask;
	}

	static byte *sbarCache = NULL;
	static int sbarCTopy = -1, sbarCBoty = -1, sbarCPitch = -1, sbarCH = -1;
	static uint32_t sbarCKey[8] = {0};
	const bool sbarHit = sbarFullWidth && sbarCache != NULL &&
		sbarCTopy == topy && sbarCBoty == boty && sbarCPitch == sbarPitch &&
		sbarCH == sbarH && memcmp(sbarKey, sbarCKey, sizeof(sbarKey)) == 0;

	extern uint32_t of_sb_dbg_hits, of_sb_dbg_misses;
	if(sbarHit)
	{
		OF_PERF_DBG(++of_sb_dbg_hits);
		byte *fb = screen->GetBuffer();
		if(sbarTopBytes)
			memcpy(fb, sbarCache, sbarTopBytes);
		if(sbarBotBytes)
			memcpy(fb + (size_t)boty * sbarPitch, sbarCache + sbarTopBytes,
				sbarBotBytes);
		drawEcg();
		drawScore();
		return;
	}
	OF_PERF_DBG(++of_sb_dbg_misses);

	// --- Full redraw: a readout changed (or no cache yet). ---
	screen->DrawTexture(TexMan(STBar), botStx, botSty,
		DTA_DestWidthF, botStw,
		DTA_DestHeightF, botSth,
		TAG_DONE);

	screen->DrawTexture(TexMan(STBarTop), 0.0, 0.0,
		DTA_DestWidthF, topStw,
		DTA_DestHeightF, topSth,
		TAG_DONE);

	if(viewsize < 20)
	{
		// Draw outset border
		static byte colors[3] =
		{
			ColorMatcher.Pick(RPART(gameinfo.Border.topcolor), GPART(gameinfo.Border.topcolor), BPART(gameinfo.Border.topcolor)),
			ColorMatcher.Pick(RPART(gameinfo.Border.bottomcolor), GPART(gameinfo.Border.bottomcolor), BPART(gameinfo.Border.bottomcolor)),
			ColorMatcher.Pick(RPART(gameinfo.Border.highlightcolor), GPART(gameinfo.Border.highlightcolor), BPART(gameinfo.Border.highlightcolor))
		};

		VWB_Clear(colors[1], 0, topy, screenWidth-scaleFactorX, topy+scaleFactorY);
		VWB_Clear(colors[1], 0, topy+scaleFactorY, scaleFactorX, boty);
		VWB_Clear(colors[0], scaleFactorX, boty-scaleFactorY, screenWidth, boty);
		VWB_Clear(colors[0], screenWidth-scaleFactorX, topy, screenWidth, static_cast<int>(boty-scaleFactorY));
	}

	// Draw the top information
	FString lives, area;
	// TODO: Don't depend on LevelNumber for this switch
	if(levelInfo->LevelNumber > 20)
		area = "SECRET";
	else
		area.Format("AREA: %d", levelInfo->LevelNumber);
	lives.Format("LIVES: %d", players[ConsolePlayer].lives);
	DrawString(IndexFont, area, 18, 5, true, CR_WHITE);
	DrawString(IndexFont, levelInfo->GetName(map), 160, 5, true, CR_WHITE, true);
	DrawString(IndexFont, lives, 267, 5, true, CR_WHITE);

	// Draw bottom information
	DrawInfoArea();

	// AoG and PS lay out the right half of the bar differently (bstone
	// DrawHealthNum/DrawWeaponPic/DrawAmmoNum/DrawKeyPics coordinates).
	static const EColorRange statusBlue = V_FindFontColor("BlakeStatusBlue");

	if(isPS)
	{
		FString health;
		health.Format("%3d", curHealth);
		DrawString(HealthFont, health, 128, 162, false);
	}
	else
	{
		// ECG trace + heart (drawn via drawEcg so the animation also runs on a
		// cache hit) and the percentage on the health monitor grid.
		drawEcg();

		FString health;
		health.Format("%3d%%", curHealth);
		DrawString(IndexFont, health, 149, 186, false, statusBlue);
	}

	// Score is drawn last (after the cache snapshot below) via drawScore(), so the
	// cached band stays score-free.

	if(players[ConsolePlayer].ReadyWeapon)
	{
		AWeapon *readyWeapon = players[ConsolePlayer].ReadyWeapon;

		FTexture *weapon = TexMan(readyWeapon->icon);
		if(weapon)
		{
			stx = isPS ? 248 : 176;
			sty = isPS ? 176 : 152;
			stw = weapon->GetScaledWidthDouble();
			sth = weapon->GetScaledHeightDouble();
			screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
			screen->DrawTexture(weapon, stx, sty,
				DTA_DestWidthF, stw,
				DTA_DestHeightF, sth,
				TAG_DONE);
		}

		// The auto charge pistol recharges, so it gets a READY/WAIT message
		// instead of the ammo gauge (bstone DrawAmmoMsg/DrawAmmoGuage).
		static const ClassDef * const autochargeCls = ClassDef::FindClass("AutoChargePistol");
		const bool autocharge = autochargeCls && readyWeapon->IsKindOf(autochargeCls);

		unsigned int amount = readyWeapon->ammo[AWeapon::PrimaryFire]->amount;
		if(autocharge)
			VWB_DrawGraphic(TexMan(amount > 0 ? "STREADY" : "STWAIT"), isPS ? 240 : 232, 152);
		else
			DrawLed(static_cast<double>(amount)/static_cast<double>(readyWeapon->ammo[AWeapon::PrimaryFire]->maxamount), isPS ? 243 : 234, 155);

		// AoG hides the percentage for the auto charge pistol; PS always
		// shows it along with the weapon number corner pic.
		if(isPS || !autocharge)
		{
			if(isPS && weapon && weapon->Name.Len() == 7)
			{
				FString corner;
				corner.Format("STCWEAP%c", weapon->Name[6]);
				FTextureID cornerId = TexMan.CheckForTexture(corner, FTexture::TEX_Any);
				if(cornerId.isValid())
					VWB_DrawGraphic(TexMan(cornerId), 248, 184);
			}

			// Right aligned at a fixed 5px digit stride.
			int ammoX = isPS ? 252 : 211;
			if(amount < 100) ammoX += 5;
			if(amount < 10) ammoX += 5;

			FString ammo;
			ammo.Format("%u%%", amount);
			DrawString(IndexFont, ammo, ammoX, 190, false, statusBlue);
		}
	}

	// Radar gauge and magnification pic are PS only.
	if(isPS && players[ConsolePlayer].mo)
	{
		static const ClassDef * const radarPackCls = ClassDef::FindClass("RadarPack");
		AInventory *radarPack = players[ConsolePlayer].mo->FindInventory(radarPackCls);
		if(radarPack)
			DrawLed(static_cast<double>(radarPack->amount)/static_cast<double>(radarPack->maxamount), 235, 155);
		else
			DrawLed(0, 235, 155);
		VWB_DrawGraphic(TexMan("STMAG1X"), 176, 152);
	}

	// Find keys in inventory. AoG's VGAGRAPH has no key pics; the original
	// game draws the security keys as 7x7 colour bars.
	int presentKeys = 0;
	if(players[ConsolePlayer].mo)
	{
		static const ClassDef * const keyClasses[5][2] = {
			{ClassDef::FindClass("RedAccessKey"), ClassDef::FindClass("RedAccessCard")},
			{ClassDef::FindClass("YellowAccessKey"), ClassDef::FindClass("YellowAccessCard")},
			{ClassDef::FindClass("BlueAccessKey"), ClassDef::FindClass("BlueAccessCard")},
			{NULL, ClassDef::FindClass("GreenAccessCard")},
			{NULL, ClassDef::FindClass("GoldAccessCard")}
		};
		for(AInventory *item = players[ConsolePlayer].mo->inventory;item != NULL;item = item->inventory)
		{
			if(!item->IsKindOf(NATIVE_CLASS(Key)))
				continue;
			for(unsigned int key = 0;key < 5;++key)
			{
				if((keyClasses[key][0] && item->IsKindOf(keyClasses[key][0])) ||
					(keyClasses[key][1] && item->IsKindOf(keyClasses[key][1])))
					presentKeys |= 1<<key;
			}
		}
	}

	if(isPS)
	{
		// PS has dedicated key slot pics under the health readout.
		for(unsigned int i = 0;i < 3;++i)
		{
			FString pic;
			pic.Format("STKEYS%d", (presentKeys & (1<<i)) ? i+1 : 0);
			VWB_DrawGraphic(TexMan(pic), 120+16*i, 179);
		}
	}
	else
	{
		// Display order red, yellow, green, blue, gold; colours from BLAKEPAL.
		static const unsigned int keyOrder[5] = {0, 1, 3, 2, 4};
		static const int keyOffColors[5] = {0x11, 0x31, 0x91, 0x51, 0x21};
		static const int keyOnColors[5] = {0xC9, 0xB9, 0x9C, 0x5B, 0x2B};
		for(unsigned int i = 0;i < 5;++i)
		{
			const unsigned int key = keyOrder[i];
			const int color = (presentKeys & (1<<key)) ? keyOnColors[key] : keyOffColors[key];

			stx = 257+8*i;
			sty = 177;
			stw = 7;
			sth = 7;
			screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
			VWB_Clear(color, stx, sty, stx+stw, sty+sth);
		}
	}

	// Cache the freshly composited bar so unchanged frames restore it with a
	// memcpy instead of re-rendering (full-width only).
	if(sbarFullWidth)
	{
		byte *nc = (byte *)realloc(sbarCache,
			(size_t)(sbarTopBytes + sbarBotBytes));
		if(nc)
		{
			sbarCache = nc;
			byte *fb = screen->GetBuffer();
			if(sbarTopBytes)
				memcpy(sbarCache, fb, sbarTopBytes);
			if(sbarBotBytes)
				memcpy(sbarCache + sbarTopBytes,
					fb + (size_t)boty * sbarPitch, sbarBotBytes);
			sbarCTopy = topy;
			sbarCBoty = boty;
			sbarCPitch = sbarPitch;
			sbarCH = sbarH;
			memcpy(sbarCKey, sbarKey, sizeof(sbarKey));
		}
	}

	// Draw the score last -- after the snapshot above captured a score-free band
	// -- so a cache hit composites the live score onto clean background (no
	// transparent-glyph ghosting), matching the snapshot the next hit restores.
	drawScore();
}

// Bakes a class's info-area icon frames (enemy walk cycle, or item spawn frame)
// into the icon buffers.  Called at icon set-time (Blake_PickupInfoMsg / the
// attack path), which runs during the game tick -- BEFORE the GPU 3D frame -- so
// touching the sprite textures here is safe; the per-frame DrawInfoArea blit then
// never touches a sprite texture.
void BlakeStatusBar::SetInfoMessageIcon(const ClassDef *cls)
{
	iconNF = iconFrame = iconAnimTics = 0;
	iconKey = cls ? (int32_t)(intptr_t)cls : 0;
	if(!cls)
		return;

	FTextureID frames[MAX_ICON_FRAMES];
	const int nf = R_GetClassIconFrames(cls, frames, MAX_ICON_FRAMES);
	for(int i = 0; i < nf; ++i)
	{
		FTexture *tex = frames[i].isValid() ? TexMan(frames[i]) : NULL;
		if(!tex)
			continue;
		const int w = tex->GetWidth(), h = tex->GetHeight();
		if(w <= 0 || h <= 0 || w*h > (int)sizeof(iconPix[0]))
			continue;
		uint8_t *pix = iconPix[iconNF];
		uint8_t *mask = iconMask[iconNF];
		memset(mask, 0, (size_t)w*h);
		int minx = w, miny = h, maxx = -1, maxy = -1;
		for(int c = 0; c < w; ++c)
		{
			const FTexture::Span *spans;
			const BYTE *col = tex->GetColumn(c, &spans);
			for(; spans->Length; ++spans)
			{
				const int end = spans->TopOffset + spans->Length;
				for(int r = spans->TopOffset; r < end && r < h; ++r)
				{
					pix[c*h + r] = col[r];
					mask[c*h + r] = 1;
					if(c < minx) minx = c;
					if(c > maxx) maxx = c;
					if(r < miny) miny = r;
					if(r > maxy) maxy = r;
				}
			}
		}
		if(maxx < minx)	// fully transparent -- skip
			continue;
		iconW[iconNF] = w;
		iconH[iconNF] = h;
		iconCX0[iconNF] = minx;
		iconCY0[iconNF] = miny;
		iconCW[iconNF] = maxx - minx + 1;
		iconCH[iconNF] = maxy - miny + 1;
		++iconNF;
	}
}

// Draws the message strip in the bottom status bar: the current timed
// message if one is up, otherwise the no-messages/token-count idle text.
void BlakeStatusBar::DrawInfoArea()
{
	static FTextureID STInfo = TexMan.GetTexture("STINFO", FTexture::TEX_Any);

	FTexture *info = TexMan(STInfo);
	if(info)
	{
		double stx = 0;
		double sty = 200-STATUSLINES;
		double stw = info->GetScaledWidthDouble();
		double sth = info->GetScaledHeightDouble();
		screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
		screen->DrawTexture(info, stx, sty,
			DTA_DestWidthF, stw,
			DTA_DestHeightF, sth,
			TAG_DONE);
	}

	// Pickup/enemy icon in a black box at the info area's left (bstone ^SH/^AN, a
	// 37x37 VW_Bar with the sprite scaled into it): fill the box black, then blit
	// the current frame scaled+masked so transparent areas stay black.  Enemies
	// animate their walk cycle (iconFrame, advanced in Tick); items are one frame.
	// Raw-blitted from buffers baked at set-time -- the per-frame draw never touches
	// a sprite texture (that corrupts the Pocket GPU's 3D column state, white lines).
	// Pickup/enemy icon in a black box at the info area's left (bstone ^SH/^AN, a
	// 37x37 VW_Bar with the sprite scaled into it).  Drawn in a SINGLE write pass
	// over the box -- each box pixel written exactly once (scaled content where it
	// covers, else black) -- matching the write pattern of the build with no white
	// lines.  Raw-blitted from set-time-baked buffers; never touches a sprite
	// texture during the GPU frame (that corrupts the Pocket GPU's column state).
	const bool showIcon = (InfoMessageTics != 0 && iconNF > 0);
	if(showIcon)
	{
		byte *fb = screen->GetBuffer();
		const int pitch = screen->GetPitch();
		const int sw = screen->GetWidth(), sh = screen->GetHeight();
		const byte black = GPalette.BlackIndex;

		// Box (bstone VW_Bar) in real coords.
		double bx = 3, by = 200-STATUSLINES+3, bw = 37, bh = 37;
		screen->VirtualToRealCoords(bx, by, bw, bh, 320, 200, true, true);
		const int rx = (int)bx, ry = (int)by, rw = (int)bw, rh = (int)bh;

		// Content scaled uniformly by 37/64 (bstone vid_draw_ui_sprite) and centred
		// in the box on its opaque-content bounds (not stretched, not the padded
		// canvas) -- the real sub-rect of the box the figure occupies.
		const int fr = clamp(iconFrame, 0, iconNF-1);
		const int ih = iconH[fr];
		const int cx0 = iconCX0[fr], cy0 = iconCY0[fr], cw = iconCW[fr], ch = iconCH[fr];
		const uint8_t *pix = iconPix[fr], *mask = iconMask[fr];
		const double scale = 37.0/64.0;
		double svw = cw*scale, svh = ch*scale;
		double sx = (3 + 37.0/2.0) - svw/2.0;
		double sy = (200-STATUSLINES+3 + 37.0/2.0) - svh/2.0;
		screen->VirtualToRealCoords(sx, sy, svw, svh, 320, 200, true, true);
		const int spx = (int)sx, spy = (int)sy, spw = (int)svw, sph = (int)svh;

		for(int oy = 0; oy < rh; ++oy)
		{
			const int py = ry + oy;
			if(py < 0 || py >= sh)
				continue;
			byte *row = fb + (size_t)py*pitch;
			for(int ox = 0; ox < rw; ++ox)
			{
				const int px = rx + ox;
				if(px < 0 || px >= sw)
					continue;
				byte val = black;
				const int rely = py - spy, relx = px - spx;
				if(spw > 0 && sph > 0 && relx >= 0 && relx < spw && rely >= 0 && rely < sph)
				{
					const int srx = cx0 + relx * cw / spw;
					const int sry = cy0 + rely * ch / sph;
					if(mask[srx*ih + sry])
						val = pix[srx*ih + sry];
				}
				row[px] = val;
			}
		}
	}

	// With the icon box present, text indents past it (bstone left_margin advances
	// to the box's right edge: INFOAREA_X 3 + box 37 = 40); otherwise margin 3.
	const double leftMargin = showIcon ? 40.0 : 3.0;

	FString msg;
	if(InfoMessageTics != 0)
		msg = InfoMessage;
	else
	{
		unsigned int tokens = 0;
		if(players[ConsolePlayer].mo)
		{
			static const ClassDef * const coinCls = ClassDef::FindClass("ConcessionCoin");
			if(AInventory *coins = players[ConsolePlayer].mo->FindInventory(coinCls))
				tokens = coins->amount;
		}
		msg.Format("\r    NO MESSAGES.\r    FOOD TOKENS: %u", tokens);

		// PS objective hint while the next floor is still locked (bstone
		// DisplayNoMoMsgs).  AoG shows none.  bstone keys on mapon (0-based);
		// our LevelNumber is 1-based, so mapon 19 (goldfire) = LevelNumber 20.
		static const bool isPS = IWad::GetGame().Name.CompareNoCase("Planet Strike") == 0;
		if(isPS && levelInfo)
		{
			const int lvl = levelInfo->LevelNumber;
			extern bool Blake_PsFloorUnlocked(int lvl);
			if(!Blake_PsFloorUnlocked(lvl + 1))	// next floor not yet unlocked
			{
				if(lvl == 20)
					msg += "\r\r^FC39  DESTROY GOLDFIRE!";
				else if(lvl < 20 || lvl > 24)
				{
					unsigned int detonators = 0;
					static const ClassDef * const detCls = ClassDef::FindClass("PlasmaDetonator");
					if(players[ConsolePlayer].mo)
						if(AInventory *d = players[ConsolePlayer].mo->FindInventory(detCls))
							detonators = d->amount;
					msg += detonators ? "\r\r^FC39DESTROY SECURITY CUBE!"
									  : "\r\r^FC39 FIND THE DETONATOR!";
				}
				// lvl 21..24: final approach floors, no hint (bstone case 20..23).
			}
		}
	}

	// Messages are \r-separated lines drawn 6px apart in the small font.
	// ^-prefixed control codes (bstone HandleControlCodes): ^FCxx picks the
	// font colour by palette row, ^XX ends the message, the others are
	// skipped along with their operands.
	double x = leftMargin;
	double y = 200-STATUSLINES+3;
	EColorRange color = CR_GRAY;
	FString segment;
	for(const char* ch = msg.GetChars();;)
	{
		if(*ch == '\r' || *ch == '\0' || *ch == '^')
		{
			if(segment.Len())
			{
				DrawString(SmallFont, segment, x, y, true, color);
				word segWidth, segHeight;
				VW_MeasurePropString(SmallFont, segment, segWidth, segHeight);
				x += segWidth;
				segment = "";
			}
			if(*ch == '\0')
				break;
			if(*ch == '\r')
			{
				x = leftMargin;
				y += 6;
				++ch;
				continue;
			}

			++ch;
			char code[2] = {0, 0};
			for(unsigned int i = 0;i < 2 && *ch;++i)
				code[i] = toupper(*ch++);

			if(code[0] == 'X' && code[1] == 'X')
				break;
			if(code[0] == 'F' && code[1] == 'C')
			{
				unsigned int palColor = 0;
				for(unsigned int i = 0;i < 2 && *ch;++i)
				{
					char digit = toupper(*ch++);
					palColor <<= 4;
					if(digit >= '0' && digit <= '9')
						palColor |= digit - '0';
					else if(digit >= 'A' && digit <= 'F')
						palColor |= digit - 'A' + 10;
				}
				switch(palColor>>4)
				{
					case 0x1: color = CR_RED; break;
					case 0x3: color = CR_YELLOW; break;
					case 0x5: color = CR_GREEN; break;
					case 0x7: color = CR_CYAN; break;
					default: color = CR_GRAY; break;
				}
			}
			else if((code[0] == 'B' && code[1] == 'G') || (code[0] == 'A' && code[1] == 'N'))
			{
				for(unsigned int i = 0;i < 2 && *ch;++i)
					++ch;
			}
			else if((code[0] == 'S' && code[1] == 'H') || (code[0] == 'L' && code[1] == 'M'))
			{
				for(unsigned int i = 0;i < 3 && *ch;++i)
					++ch;
			}
			continue;
		}
		segment += *ch++;
	}
}

void BlakeStatusBar::DrawString(FFont *font, const char* string, double x, double y, bool shadow, EColorRange color, bool center) const
{
	if(!font)
		return;

	word strWidth, strHeight;
	VW_MeasurePropString(font, string, strWidth, strHeight);

	if(center)
		x -= strWidth/2.0;

	const double startX = x;
	FRemapTable *remap = font->GetColorTranslation(color);

	while(*string != '\0')
	{
		char ch = *string++;
		if(ch == '\n')
		{
			y += font->GetHeight();
			x = startX;
			continue;
		}

		int chWidth;
		FTexture *tex = font->GetChar(ch, &chWidth);
		if(tex)
		{
			double tx, ty, tw, th;

			if(shadow)
			{
				tx = x + 1, ty = y + 1, tw = tex->GetScaledWidthDouble(), th = tex->GetScaledHeightDouble();
				screen->VirtualToRealCoords(tx, ty, tw, th, 320, 200, true, true);
				screen->DrawTexture(tex, tx, ty,
					DTA_DestWidthF, tw,
					DTA_DestHeightF, th,
					DTA_FillColor, GPalette.BlackIndex,
					TAG_DONE);
			}

			tx = x, ty = y, tw = tex->GetScaledWidthDouble(), th = tex->GetScaledHeightDouble();
			screen->VirtualToRealCoords(tx, ty, tw, th, 320, 200, true, true);
			screen->DrawTexture(tex, tx, ty,
				DTA_DestWidthF, tw,
				DTA_DestHeightF, th,
				DTA_Translation, remap,
				TAG_DONE);
		}
		x += chWidth;
	}
}

void BlakeStatusBar::Tick()
{
	int scoreDelta = players[ConsolePlayer].score - CurrentScore;
	if(scoreDelta > 1500)
		CurrentScore += scoreDelta/4;
	else
		CurrentScore += clamp<int>(scoreDelta, 0, 8);

	if(ScoreRollWait > 0)
		--ScoreRollWait;

	if(InfoMessageTics > 0 && --InfoMessageTics == 0)
	{
		InfoMessage = "";
		InfoMessagePriority = 0;
	}

	// Advance the info-area enemy walk cycle by elapsed game-tics (bstone ^AN).
	iconAnimTics += tics;
	if(iconNF > 1 && iconAnimTics >= ICON_ANIM_DELAY)
	{
		iconAnimTics = 0;
		iconFrame = (iconFrame + 1) % iconNF;
	}

	// All-enemies-destroyed pinball bonus.  Edge-detected here rather than in
	// GivePoints (bstone's hook) because AActor::Die scores BEFORE bumping
	// killcount, so a score-time check would miss the final kill.
	if(gamestate.killtotal > 0 && gamestate.killcount >= gamestate.killtotal
		&& IWad::CheckGameFilter("Blake")
		&& !(pinballShown & 0x10) && !(pinballQueue & 0x10))
		pinballQueue |= 0x10;	// B_ENEMY_DESTROYED

	// All-points-collected bonus (bstone B_TOTAL_POINTS).  Edge-detected here for
	// the same reason as 0x10: treasurecount/points are tallied after GivePoints.
	if(levelPointsTotal > 0 && levelPointsAccum >= levelPointsTotal
		&& !(pinballShown & 0x20) && !(pinballQueue & 0x20))
		pinballQueue |= 0x20;	// B_TOTAL_POINTS

	// All-informants-alive bonus (bstone B_INFORMANTS_ALIVE): gated on the enemy
	// AND points bonuses already shown (bstone BONUS_SHOWN & (0x10|0x20)).
	if(levelInfo && (pinballShown & 0x30) == 0x30
		&& !(pinballShown & 0x40) && !(pinballQueue & 0x40))
	{
		extern int Blake_InformantsTotal(int lvl);
		extern int Blake_InformantsAlive(int lvl);
		const int lvl = levelInfo->LevelNumber;
		if(Blake_InformantsTotal(lvl) > 0 && Blake_InformantsAlive(lvl) >= Blake_InformantsTotal(lvl))
			pinballQueue |= 0x40;	// B_INFORMANTS_ALIVE
	}

	// Drain a queued pinball bonus once the info area is free.
	DrainPinballBonus();

	// AoG health monitor (bstone DrawHealthMonitor). ECG segment indices:
	// 0 silence, 1-8 shape #1 (66%+), 9-17 shape #2 (33-65%), 18-27 shape #3.
	const int curHealth = players[ConsolePlayer].health;
	if(++EcgScrollTics >= 7)
	{
		EcgScrollTics = 0;

		bool carry = false;
		for(int i = 5;i >= 0;--i)
		{
			if(carry)
			{
				carry = false;
				EcgLegend[i] = EcgLegend[i + 1];
				EcgSegments[i] = EcgSegments[i + 1] - 4;
			}
			else if(EcgSegments[i] != 0)
			{
				++EcgSegments[i];

				bool useCarry = false;
				if(EcgLegend[i] == 1 && EcgSegments[i] == 5)
					useCarry = true;
				else if(EcgLegend[i] == 2 && EcgSegments[i] == 13)
					useCarry = true;
				if(EcgLegend[i] == 3 &&
					(EcgSegments[i] == 22 || EcgSegments[i] == 27))
					useCarry = true;

				if(useCarry)
					carry = true;
				else
				{
					bool skip = false;
					if(EcgLegend[i] == 1 && EcgSegments[i] > 8)
						skip = true;
					else if(EcgLegend[i] == 2 && EcgSegments[i] > 17)
						skip = true;
					if(EcgLegend[i] == 3 && EcgSegments[i] > 27)
						skip = true;

					if(skip)
					{
						EcgLegend[i] = 0;
						EcgSegments[i] = 0;
					}
				}
			}
		}

		if(curHealth > 0 && EcgLegend[5] == 0)
		{
			if(curHealth < 33)
			{
				EcgLegend[5] = 3;
				EcgSegments[5] = 18;
			}
			else if(curHealth >= 66)
			{
				if(EcgLegend[4] != 1)
				{
					EcgLegend[5] = 1;
					EcgSegments[5] = 1;
				}
			}
			else
			{
				EcgLegend[5] = 2;
				EcgSegments[5] = 9;
			}
		}
	}

	// Heart sign pulses at HEALTH_PULSE/2; steady when dead or below 40%.
	if(curHealth < 40)
	{
		HeartTics = 0;
		HeartBright = false;
	}
	else if(++HeartTics >= 35)
	{
		HeartTics = 0;
		HeartBright = !HeartBright;
	}
}
