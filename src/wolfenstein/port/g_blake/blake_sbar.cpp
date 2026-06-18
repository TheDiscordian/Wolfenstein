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
#include "id_us.h"
#include "id_vh.h"
#include "g_mapinfo.h"
#include "v_font.h"
#include "v_video.h"
#include "wl_agent.h"
#include "wl_def.h"
#include "wl_iwad.h"
#include "wl_play.h"
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
	BlakeStatusBar() : CurrentScore(0), InfoMessagePriority(0), InfoMessageTics(0),
		StartupMsgPending(false), EcgScrollTics(0), HeartTics(0), HeartBright(false)
	{
		memset(EcgLegend, 0, sizeof(EcgLegend));
		memset(EcgSegments, 0, sizeof(EcgSegments));
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
		// by any gameplay message, matching the original's "system" message.
		if(StartupMsgPending)
		{
			StartupMsgPending = false;
			DisplayInfoMessage("R.E.B.A.\rAGENT: BLAKE STONE\rALL SYSTEMS READY.", 0, 300);
		}
	}

	void NewGameMessage() { StartupMsgPending = true; }

	void Tick();

	void DisplayInfoMessage(const char *msg, int priority, int tics)
	{
		if(priority < InfoMessagePriority)
			return;

		InfoMessage = msg;
		InfoMessagePriority = priority;
		InfoMessageTics = tics;
	}

protected:
	void DrawInfoArea();
	void DrawLed(double percent, double x, double y) const;
	void DrawString(FFont *font, const char* string, double x, double y, bool shadow, EColorRange color=CR_UNTRANSLATED, bool center=false) const;

private:
	int CurrentScore;
	FString InfoMessage;
	int InfoMessagePriority;
	int InfoMessageTics;
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
// 3d_msgs.cpp, AOG variant) -- the ^AN icon / ^FC colour codes stripped (the
// info area draws plain text + \r breaks), the name layout kept verbatim.
namespace {
struct BlakeAttackMsg { const char* className; const char* msg; };
const BlakeAttackMsg blakeAttackMsgs[] = {
	{ "RentACop",                "\r\r  ATTACKING:\rSECTOR PATROL" },
	{ "SectorGuard",             "\r\r  ATTACKING:\r SECTOR GUARD" },
	{ "ProGuard",                "\r\r  ATTACKING:\rSTAR SENTINEL" },
	{ "TechWarrior",             "\r\r  ATTACKING:\r TECH WARRIOR" },
	{ "STARTrooper",             "\r\r  ATTACKING:\r STAR TROOPER" },
	{ "AlienProtector",          "  ATTACKING:\r    ALIEN\r  PROTECTOR" },
	{ "GeneralScientist",        "\r\r  ATTACKING:\r   BIO-TECH" },
	{ "FloatingBomb",            "  ATTACKING:\rPERSCAN DRONE\r  EXPLOSION" },
	{ "VolatileTransport",       "  ATTACKING:\r VOLATILE MAT.\r  TRANSPORT\r  EXPLOSION" },
	{ "GeneticGuard",            "  ATTACKING:\r HIGH-SECURITY\r GENETIC GUARD" },
	{ "CyborgWarrior",           "  ATTACKING:\r   CYBORG\r   WARRIOR" },
	{ "SpiderMutant",            "  ATTACKING:\r   SPIDER\r   MUTANT" },
	{ "SpiderMutantMorphed",     "  ATTACKING:\r   SPIDER\r   MUTANT" },
	{ "AcidDragon",              "\r  ATTACKING:\r ACID DRAGON" },
	{ "BreatherBeast",           "  ATTACKING:\r   BREATHER\r    BEAST" },
	{ "BioMechGuardian",         "  ATTACKING:\r   BIO-MECH\r   GUARDIAN" },
	{ "ReptilianWarrior",        "  ATTACKING:\r  REPTILIAN\r   WARRIOR" },
	{ "ReptilianWarriorMorphed", "  ATTACKING:\r  REPTILIAN\r   WARRIOR" },
	{ "MechSentinel",            "  ATTACKING:\r EXPERIMENTAL\r MECH-SENTINEL" },
	{ "MutantHuman",             "  ATTACKING:\r EXPERIMENTAL\r MUTANT HUMAN" },
	{ "MutantHumanMorphed",      "  ATTACKING:\r EXPERIMENTAL\r MUTANT HUMAN" },
	{ "SmallCanisterAlien",      "  ATTACKING:\r EXPERIMENTAL\r GENETIC ALIEN" },
	{ "LargeCanisterAlien",      "  ATTACKING:\r EXPERIMENTAL\r GENETIC ALIEN" },
	{ "GurneyMutant",            "  ATTACKING:\r   MUTATED\r    GUARD" },
	{ "PODAlien",                "\r  ATTACKING:\r  POD ALIEN" },
	{ "CeilingTurretRotate",     "  ATTACKING:\r  AUTOMATED\rHEAVY ARMORED\r ROBOT TURRET" },
	{ "CeilingTurretStatic",     "  ATTACKING:\r  AUTOMATED\rHEAVY ARMORED\r ROBOT TURRET" },
	{ "GiantStalker",            "  ATTACKING:\r  THE GIANT\r   STALKER" },
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
	sbarKey[3] = 0;
	// Key on the info-area message CONTENT, not InfoMessageTics: the timer ticks
	// down every frame while a message shows, but the drawn text is unchanged,
	// so keying on the timer churned the cache the whole time a message was up.
	// Idle (no message) keys to 0; the token count is in sbarKey[5].
	uint32_t infoKey = 0;
	if(InfoMessageTics > 0)
	{
		infoKey = 0x811c9dc5u;
		for(const char *c = InfoMessage.GetChars();c != NULL && *c;++c)
			infoKey = (infoKey ^ (unsigned char)*c) * 16777619u;
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

	FString msg;
	if(InfoMessageTics > 0)
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
	}

	// Messages are \r-separated lines drawn 6px apart in the small font.
	// ^-prefixed control codes (bstone HandleControlCodes): ^FCxx picks the
	// font colour by palette row, ^XX ends the message, the others are
	// skipped along with their operands.
	double x = 3;
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
				x = 3;
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

	if(InfoMessageTics > 0 && --InfoMessageTics == 0)
	{
		InfoMessage = "";
		InfoMessagePriority = 0;
	}

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
