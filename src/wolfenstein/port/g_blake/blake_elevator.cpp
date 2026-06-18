/*
** blake_elevator.cpp
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
** Blake Stone elevator floor-select panel (bstone aog_input_floor) plus the
** per-floor lock table behind it.  The Elevator_SelectFloor special raises
** a flag; PlayLoop runs the modal panel between frames, and a selection
** travels via ex_newmap to the chosen floor's LevelNumber.
**
*/

#include "wl_def.h"
#include "blake_elevator.h"
#include "blake_informant.h"
#include "a_inventory.h"
#include "am_map.h"
#include "farchive.h"
#include "g_mapinfo.h"
#include "gamemap.h"
#include "id_ca.h"
#include "id_in.h"
#include "id_sd.h"
#include "id_us.h"
#include "id_vh.h"
#include "id_vl.h"
#include "language.h"
#include "lnspec.h"
#include "m_random.h"
#include "thinker.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"
#include "w_wad.h"
#include "wl_agent.h"
#include "wl_def.h"
#include "wl_draw.h"
#include "wl_game.h"
#include "wl_inter.h"
#include "wl_iwad.h"
#include "wl_main.h"
#include "wl_menu.h"
#include "wl_play.h"
#include "thingdef/thingdef.h"
#include "textures/textures.h"

bool Blake_ElevatorRequested = false;

// BLAKEPAL indices shared with the loading screen (wl_inter.cpp).
enum
{
	BLAKE_BORDER_LO = 0x80,
	BLAKE_BORDER_MED = 0x82,
	BLAKE_BORDER_CORNER = 0x83,
	BLAKE_BORDER_HI = 0x85
};

// =============================================================================
// Floor lock table
//
// bstone gamestuff.level[].locked + overall_floor, keyed by LevelNumber.
// Floors start locked, unlock on entry; the red access card unlocks exactly
// the floor after the highest unlocked one.  overallFloor caches the panel's
// per-floor stats sum for the OVERALL MISSION row.
// =============================================================================

struct FloorMeta
{
	BYTE unlocked;
	WORD overallFloor;
};
static FloorMeta floorMeta[128];

// AOG episodes span 15 LevelNumbers: slot 0 is the secret floor, 1-10 ride
// the elevator, 11-14 are extra secret slots.
static const int FLOORS_PER_EPISODE = 15;
static const int STATS_FLOORS = 11;

// PS has a single episode: LevelNumbers 1-20 on the teleport panel.
static const int PS_FLOORS = 20;

static bool IsBlake()
{
	return IWad::CheckGameFilter("Blake");
}

static bool IsAOG()
{
	return EpisodeInfo::GetNumEpisodes() > 1;
}

static int FloorIndex(int levelNum)
{
	return (levelNum - 1) % FLOORS_PER_EPISODE;
}

// LevelNumber of the episode's slot 0.
static int EpisodeBase(int levelNum)
{
	return ((levelNum - 1) / FLOORS_PER_EPISODE) * FLOORS_PER_EPISODE + 1;
}

void Blake_FloorLocksNewGame()
{
	memset(floorMeta, 0, sizeof(floorMeta));
	Blake_PsClear();
}

void Blake_FloorEntered()
{
	if(!IsBlake() || !levelInfo)
		return;
	// Per-level pinball bonus queue resets on each floor entry.
	extern void Blake_PinballReset();
	Blake_PinballReset();
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl >= (int)countof(floorMeta))
		return;
	floorMeta[lvl].unlocked = true;
}

void Blake_FloorLockSerialize(FArchive &arc)
{
	DWORD count = countof(floorMeta);
	arc << count;
	if(!arc.IsStoring())
		memset(floorMeta, 0, sizeof(floorMeta));
	for(DWORD i = 0;i < count;++i)
	{
		BYTE unlocked = i < countof(floorMeta) ? floorMeta[i].unlocked : 0;
		WORD overall = i < countof(floorMeta) ? floorMeta[i].overallFloor : 0;
		arc << unlocked << overall;
		if(i < countof(floorMeta))
		{
			floorMeta[i].unlocked = unlocked;
			floorMeta[i].overallFloor = overall;
		}
	}
}

void Blake_FloorLocksLoadLegacy()
{
	// Saves that predate the lock table progressed floor by floor, so open
	// everything up to the floor the save is on.
	memset(floorMeta, 0, sizeof(floorMeta));
	Blake_PsClear();
	if(!IsBlake() || !levelInfo)
		return;
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl >= (int)countof(floorMeta))
		return;
	if(!IsAOG())
	{
		for(int f = 1;f <= MIN(lvl, PS_FLOORS);++f)
			floorMeta[f].unlocked = true;
		return;
	}
	const int base = EpisodeBase(lvl);
	for(int f = FloorIndex(lvl);f >= 0;--f)
		floorMeta[base + f].unlocked = true;
}

// =============================================================================
// Panel text chunks
// =============================================================================

// Strip the TP control codes from a VGAGRAPH text chunk: ^XX terminates,
// ^FCxx takes a colour, ^STx a style; everything else is two chars.
static FString LoadPanelText(const char *lumpname, const char *fallback)
{
	int lumpNum = Wads.CheckNumForName(lumpname);
	if(lumpNum < 0)
		return fallback;

	FMemLump lump = Wads.ReadLump(lumpNum);
	FString data = lump.GetString();

	FString out;
	for(unsigned int i = 0;i < data.Len();++i)
	{
		char ch = data[i];
		if(ch == '^')
		{
			char c1 = i + 1 < data.Len() ? toupper(data[i + 1]) : 0;
			char c2 = i + 2 < data.Len() ? toupper(data[i + 2]) : 0;
			if(c1 == 'X' && c2 == 'X')
				break;
			i += 2;
			if(c1 == 'F' && c2 == 'C')
				i += 2;
			else if(c1 == 'S' && c2 == 'T')
				i += 1;
			continue;
		}
		if(ch == '\r')
			continue;
		out += ch;
	}

	// Trim trailing blank lines.
	while(out.Len() && (out[out.Len() - 1] == '\n' || out[out.Len() - 1] == ' '))
		out.Truncate(out.Len() - 1);

	return out.Len() ? out : FString(fallback);
}

// =============================================================================
// Panel drawing
// =============================================================================

static void ElevBar(int color, double x, double y, double w, double h)
{
	screen->VirtualToRealCoords(x, y, w, h, 320, 200, true, true);
	VWB_Clear(color, x, y, x+w, y+h);
}

static void ElevBevelBox(int x, int y, int w, int h, int tl, int br)
{
	ElevBar(BLAKE_BORDER_MED, x, y, w, h);
	ElevBar(tl, x, y, w, 1);
	ElevBar(br, x, y+h-1, w, 1);
	ElevBar(tl, x, y, 1, h);
	ElevBar(br, x+w-1, y, 1, h);
	ElevBar(BLAKE_BORDER_CORNER, x, y+h-1, 1, 1);
	ElevBar(BLAKE_BORDER_CORNER, x+w-1, y, 1, 1);
}

static void ElevRect(int color, int x, int y, int w, int h)
{
	ElevBar(color, x, y, w, 1);
	ElevBar(color, x, y+h-1, w, 1);
	ElevBar(color, x, y, 1, h);
	ElevBar(color, x+w-1, y, 1, h);
}

static void ElevShadowText(FFont *font, const char *string, int x, int y, EColorRange color)
{
	PrintX = x+1;
	PrintY = y+1;
	US_Print(font, string, CR_BLACK);
	PrintX = x;
	PrintY = y;
	US_Print(font, string, color);
}

// Top status strip with the live area/lives text (bstone DrawTopInfo).
static void ElevDrawTopInfo()
{
	word w, h;

	static FTextureID STBarTop = TexMan.GetTexture("STTOP", FTexture::TEX_Any);
	double tx = 0, ty = 0, tw = 320, th = 16;
	screen->VirtualToRealCoords(tx, ty, tw, th, 320, 200, true, true);
	screen->DrawTexture(TexMan(STBarTop), tx, 0.0,
		DTA_DestWidthF, tw,
		DTA_DestHeightF, th,
		TAG_DONE);

	FString area, lives;
	if(levelInfo->LevelNumber > 20)
		area = "SECRET";
	else
		area.Format("AREA: %d", levelInfo->LevelNumber);
	lives.Format("LIVES: %d", players[ConsolePlayer].lives);

	ElevShadowText(SmallFont, area, 18, 5, CR_WHITE);
	FString name = levelInfo->GetName(map);
	VW_MeasurePropString(SmallFont, name, w, h);
	ElevShadowText(SmallFont, name, 160 - w/2, 5, CR_WHITE);
	ElevShadowText(SmallFont, lives, 267, 5, CR_WHITE);
}

// Bottom message box (bstone BMAmsg): centred shadowed lines.
static void ElevDrawBottomBox(const FString &text)
{
	word w, h;

	ElevBevelBox(0, 152, 320, 48, BLAKE_BORDER_HI, BLAKE_BORDER_LO);
	ElevBevelBox(7, 156, 306, 40, BLAKE_BORDER_LO, BLAKE_BORDER_HI);

	FFont *font = V_GetFont("BIGFONT");
	if(!font) font = SmallFont;

	TArray<FString> lines;
	long start = 0;
	while(start <= (long)text.Len())
	{
		long nl = text.IndexOf('\n', start);
		if(nl == -1) nl = (long)text.Len();
		lines.Push(text.Mid(start, nl - start));
		start = nl + 1;
	}

	VW_MeasurePropString(font, "0", w, h);
	const int pitch = h + 1;
	int y = 156 + (40 - ((int)lines.Size()*pitch + 1))/2;
	for(unsigned int i = 0;i < lines.Size();++i)
	{
		VW_MeasurePropString(font, lines[i], w, h);
		ElevShadowText(font, lines[i], 8 + (303 - w)/2, y, CR_WHITE);
		y += pitch;
	}
}

// 64x64 radar replica of bstone ShowOverhead(14, 71, 32, 0,
// OV_KEYS|OV_WHOLE_MAP): floors 0x55, doors by state, player 0xF0, keys
// 0xF3; walls and unrevealed tiles keep the unmapped colour.  Hidden-area
// shading is not tracked here.  The grid render is split out so the PS
// panel can snapshot it per floor (bstone SaveOverheadChunk).
static void ElevOverheadGrid(BYTE *grid, bool playerDot, BYTE unmappedColor)
{
	const BYTE MAPPED_COLOR = 0x55;

	memset(grid, unmappedColor, 64*64);

	const unsigned int mapwidth = MIN<unsigned int>(map->GetHeader().width, 64);
	const unsigned int mapheight = MIN<unsigned int>(map->GetHeader().height, 64);

	AActor *playermo = players[ConsolePlayer].mo;
	const int ptilex = playermo->x >> FRACBITS;
	const int ptiley = playermo->y >> FRACBITS;

	// Key positions, gathered up front.
	const ClassDef * const keyCls = ClassDef::FindClass("Key");
	TArray<WORD> keySpots;
	for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
	{
		if(keyCls && iter->IsA(keyCls))
			keySpots.Push(((iter->y >> FRACBITS) << 8) | (iter->x >> FRACBITS));
	}

	for(unsigned int my = 0;my < mapheight;++my)
	{
		MapSpot spot = map->GetSpot(0, my, 0);
		for(unsigned int mx = 0;mx < mapwidth;++mx, ++spot)
		{
			BYTE color = unmappedColor;

			if(playerDot && (int)mx == ptilex && (int)my == ptiley)
				color = 0xF0;
			else if((spot->amFlags & AM_Visible) || gamestate.fullmap)
			{
				if(spot->tile)
				{
					if(spot->tile->offsetVertical || spot->tile->offsetHorizontal)
					{
						// Door: elevator doors light up, locked stay red,
						// closed green, open reads as floor.
						bool elev = false, locked = false, open = false;
						for(unsigned int s = 0;s < 2 && !elev;++s)
						{
							FTextureID texid = spot->GetTexture(s == 0 ? MapTile::North : MapTile::East);
							if(texid.isValid() && strncmp(TexMan(texid)->Name, "DELEV", 5) == 0)
								elev = true;
						}
						for(unsigned int t = 0;t < spot->triggers.Size();++t)
						{
							if(spot->triggers[t].action == Specials::Door_Open && spot->triggers[t].arg[3] != 0)
								locked = true;
						}
						for(unsigned int s = 0;s < 4;++s)
						{
							if(spot->slideAmount[s] > 0)
								open = true;
						}

						if(elev)
							color = 0xFD;
						else if(locked)
							color = 0x18;
						else if(!open)
							color = 0x58;
						else
							color = MAPPED_COLOR;
					}
					// Solid walls keep the unmapped colour, like bstone.
				}
				else
					color = MAPPED_COLOR;

				if(color == MAPPED_COLOR || color == unmappedColor)
				{
					for(unsigned int k = 0;k < keySpots.Size();++k)
					{
						if(keySpots[k] == ((my << 8) | mx))
						{
							color = 0xF3;
							break;
						}
					}
				}
			}

			grid[my*64 + mx] = color;
		}
	}
}

static void ElevBlitGrid(const BYTE *grid, int bx, int by)
{
	for(int my = 0;my < 64;++my)
	{
		for(int mx = 0;mx < 64;++mx)
			ElevBar(grid[my*64 + mx], bx + mx, by + my, 1, 1);
	}
}

static void ElevDrawOverhead(int bx, int by)
{
	BYTE grid[64*64];
	ElevOverheadGrid(grid, true, 0x06);
	ElevBlitGrid(grid, bx, by);
}

// =============================================================================
// Stats bars (bstone ShowStats/ShowRatio)
// =============================================================================

static bool statsQuick;

static int ElevShowRatio(int bx, int by, int total, int accum)
{
	const int BAR_W = 48, BAR_H = 5;
	const int nx = bx + 52;

	static const EColorRange naColor = V_FindFontColor("BlakeElevGreen");
	static const EColorRange pctColor = CR_WHITE;

	// The bars are placed via VirtualToRealCoords(320,200,...); route the percent
	// readouts through the SAME transform (pa=MENU_NONE) rather than the menu's
	// MenuToRealCoords (pa=MENU_CENTER default), so the digits sit on their bars
	// instead of drifting off them.
	const int oldpa = pa;
	pa = MENU_NONE;

	if(!total)
	{
		ElevBar(0, bx, by, BAR_W, BAR_H);
		ElevBar(0, nx, by, 19, BAR_H);
		PrintX = nx;
		PrintY = by;
		US_Print(SmallFont, "N/A", naColor);
		pa = oldpa;
		return 100;
	}

	const int maxperc = accum * 100 / total;
	const int numbars = maxperc * 48 / 100;

	ElevBar(0x07, bx, by, BAR_W, BAR_H);

	// bstone PrintStatPercent: seed the readout with 0% so a stale N/A from
	// the previous selection cannot linger when no bars are drawn.
	ElevBar(0, nx, by, 19, BAR_H);
	PrintX = nx + 9;
	PrintY = by;
	US_Print(SmallFont, "0%", pctColor);

	int percentage = 1;
	for(int loop = 0;loop < numbars;++loop)
	{
		if(LastScan != 0)
			statsQuick = true;

		ElevBar(0xc8, bx + loop, by, 1, BAR_H);

		if(loop == numbars - 1)
			percentage = maxperc;
		else
			percentage += 2;

		// Right-aligned-ish percent readout.
		FString pct;
		pct.Format("%d%%", percentage);
		ElevBar(0, nx, by, 18, BAR_H);
		PrintX = percentage < 10 ? nx + 9 : (percentage < 100 ? nx + 4 : nx - 1);
		PrintY = by;
		US_Print(SmallFont, pct, pctColor);

		if(!statsQuick)
		{
			if(!(loop % 2))
				SD_PlaySound("blake/statstick");
			VW_WaitVBL(1);
			VW_UpdateScreen();
		}
	}

	if(numbars)
	{
		FString pct;
		pct.Format("%d%%", maxperc);
		ElevBar(0, nx, by, 18, BAR_H);
		PrintX = maxperc < 10 ? nx + 9 : (maxperc < 100 ? nx + 4 : nx - 1);
		PrintY = by;
		US_Print(SmallFont, pct, pctColor);

		if(!statsQuick)
		{
			SD_PlaySound("blake/statsdone");
			IN_UserInput(15, ACK_Local);
		}
	}

	pa = oldpa;
	return maxperc;
}

static void ElevShowStats(int bx, int by)
{
	const int lvl = levelInfo->LevelNumber;

	statsQuick = false;

	// TOTAL POINTS, INFORMANTS ALIVE, ENEMY DESTROYED.
	const int p1 = ElevShowRatio(bx, by, gamestate.treasuretotal, gamestate.treasurecount);
	const int p2 = ElevShowRatio(bx, by + 7, Blake_InformantsTotal(lvl), Blake_InformantsAlive(lvl));
	const int p3 = ElevShowRatio(bx, by + 14, gamestate.killtotal, gamestate.killcount);

	// OVERALL FLOOR, cached for the mission row.
	const int floorSum = p1 + p2 + p3;
	if(lvl >= 1 && lvl < (int)countof(floorMeta))
		floorMeta[lvl].overallFloor = floorSum;
	ElevShowRatio(bx, by + 26, 300, floorSum);

	// OVERALL MISSION across the episode's stats floors.
	const int base = EpisodeBase(lvl);
	int total = 0, mission = 0;
	for(int f = 0;f < STATS_FLOORS;++f)
	{
		total += 300;
		if(base + f < (int)countof(floorMeta))
			mission += floorMeta[base + f].overallFloor;
	}
	ElevShowRatio(bx, by + 33, total, mission);
}

// Calc-only ElevShowRatio (bstone ss_justcalc); no totals = a free 100.
static int ElevCalcRatio(int total, int accum)
{
	if(!total)
		return 100;
	return accum * 100 / total;
}

// Overall mission ratio for the high-score table (bstone CheckHighScore's
// ss_justcalc ShowStats): refresh the current floor's cached sum, then
// average the episode's stats floors.
int Blake_MissionRatio()
{
	if(!levelInfo)
		return 0;
	const int lvl = levelInfo->LevelNumber;

	const int p1 = ElevCalcRatio(gamestate.treasuretotal, gamestate.treasurecount);
	const int p2 = ElevCalcRatio(Blake_InformantsTotal(lvl), Blake_InformantsAlive(lvl));
	const int p3 = ElevCalcRatio(gamestate.killtotal, gamestate.killcount);
	if(lvl >= 1 && lvl < (int)countof(floorMeta))
		floorMeta[lvl].overallFloor = p1 + p2 + p3;

	int mission = 0;
	if(IsAOG())
	{
		const int base = EpisodeBase(lvl);
		for(int f = 0;f < STATS_FLOORS;++f)
		{
			if(base + f < (int)countof(floorMeta))
				mission += floorMeta[base + f].overallFloor;
		}
		return mission / (STATS_FLOORS * 3);
	}
	for(int f = 1;f <= PS_FLOORS;++f)
		mission += floorMeta[f].overallFloor;
	return mission / (PS_FLOORS * 3);
}

// =============================================================================
// The AOG floor-select panel
// =============================================================================

static void ElevDrawButton(int index, bool pressed)
{
	const int x = 264 + 24 * (index % 2);
	const int y = 114 - 20 * (index / 2);

	FString name;
	name.Format(pressed ? "TELEON%02d" : "TELEOF%02d", index + 1);
	FTextureID texid = TexMan.CheckForTexture(name, FTexture::TEX_Any);
	if(texid.isValid())
		VWB_DrawGraphic(TexMan(texid), x, y);
}

static void ElevDrawCursor(int currentFloor, int targetFloor)
{
	for(int i = 0;i < 10;++i)
	{
		int color;
		if(currentFloor == i + 1)
			color = (currentFloor == targetFloor) ? 0x1F : 0x18;
		else if(targetFloor == i + 1)
			color = 0xAF;
		else
			color = 0x82;

		const int x = 264 + 24 * (i % 2);
		const int y = 114 - 20 * (i / 2);
		ElevRect(color, x, y, 17, 17);
	}
}

// Message area on TELETXBG: two FONTHUGE lines plus the floor digits.
static void ElevDrawMessage(const FString &msg, int currentFloor, int lockedFloor)
{
	static FTextureID texBG = TexMan.CheckForTexture("TELETXBG", FTexture::TEX_Any);
	if(texBG.isValid())
		VWB_DrawGraphic(TexMan(texBG), 24, 26);

	FFont *huge = V_GetFont("FONTHUGE");
	if(!huge) huge = SmallFont;

	static const EColorRange msgColor = V_FindFontColor("BlakeElevBlue");
	static const EColorRange digitColor = V_FindFontColor("BlakeElevYellow");

	int y = 26;
	long start = 0;
	while(start < (long)msg.Len())
	{
		long nl = msg.IndexOf('\n', start);
		if(nl == -1) nl = (long)msg.Len();
		PrintX = 24;
		PrintY = y;
		US_Print(huge, msg.Mid(start, nl - start), msgColor);
		start = nl + 1;
		y += 17;
	}

	FString digit;
	if(currentFloor >= 1)
	{
		digit.Format("%d", currentFloor);
		PrintX = 167;
		PrintY = 26;
		US_Print(huge, digit, digitColor);
	}
	if(lockedFloor >= 1)
	{
		digit.Format("%d", lockedFloor);
		PrintX = 82;
		PrintY = 26;
		US_Print(huge, digit, digitColor);
	}
}

// Returns the chosen floor (1-10) or -1 for cancel.
static int ElevInputFloor()
{
	const FString messages[4] =
	{
		LoadPanelText("ELEVMSG0", "Current floor:\nSelect a floor."),
		LoadPanelText("ELEVMSG1", "RED access card used\nto unlock floor!"),
		LoadPanelText("ELEVMSG4", "Floor     is LOCKED.\nTry another floor."),
		LoadPanelText("ELEVMSG5", "You must first get\nthe RED access card!")
	};

	const int lvl = levelInfo->LevelNumber;
	const int base = EpisodeBase(lvl);
	const int currentFloor = FloorIndex(lvl);

	int lastUnlocked = 0;
	for(int f = 1;f <= 10;++f)
	{
		if(base + f < (int)countof(floorMeta) && floorMeta[base + f].unlocked)
			lastUnlocked = f;
	}

	VW_FadeOut();

	ElevDrawTopInfo();
	ElevBevelBox(0, 16, 320, 136, BLAKE_BORDER_HI, BLAKE_BORDER_LO);
	ElevBevelBox(7, 21, 306, 126, BLAKE_BORDER_LO, BLAKE_BORDER_HI);

	static FTextureID texBack = TexMan.CheckForTexture("TELEBACK", FTexture::TEX_Any);
	if(texBack.isValid())
		VWB_DrawGraphic(TexMan(texBack), 8, 22);

#if OF_DEVICE_BEHAVIOR
	// Pocket has no number keys: d-pad cycles the floor, A confirms.
	ElevDrawBottomBox("Use UP/DOWN to select a floor.\nThen press A.");
#else
	ElevDrawBottomBox(LoadPanelText("FLOORMSG",
		"Press a number to select a floor.\n(0 selects floor 10)"));
#endif
	ElevDrawOverhead(14, 71);

	IN_ClearKeysDown();

	int result = -2;
	int targetFloor = currentFloor;
	bool drawMessage = true, drawCursor = true, drawStats = true;
	bool useDelay = false;
	int pressedButton = -1;
	const FString *message = &messages[0];
	int messageCurrent = currentFloor, messageLocked = -1;

	// Seed edge detection with whatever is still held from Cmd_Use.
	ControlInfo ci, prev;
	ReadAnyControl(&prev);

	while(result == -2)
	{
		ReadAnyControl(&ci);
		const ScanCode scan = LastScan;
		IN_ClearKeysDown();

		int targetLevel = 0;

#if OF_DEVICE_BEHAVIOR
		// Pocket: B (sc_Space) is the advertised back; START (sc_Escape) also backs.
		if(scan == sc_Space || scan == sc_Escape)
			result = -1;
#else
		if(scan == sc_Escape || (ci.button1 && !prev.button1))
			result = -1;
#endif
		else if((ci.dir == dir_North || ci.dir == dir_East) && ci.dir != prev.dir)
		{
			if(++targetFloor > 10) targetFloor = 1;
			drawCursor = true;
		}
		else if((ci.dir == dir_South || ci.dir == dir_West) && ci.dir != prev.dir)
		{
			if(--targetFloor < 1) targetFloor = 10;
			drawCursor = true;
		}
		else if(scan == sc_Enter || (ci.button0 && !prev.button0))
		{
			targetLevel = targetFloor;
			drawCursor = true;
		}

		if(scan >= SDLx_SCANCODE(1) && scan <= SDLx_SCANCODE(0))
		{
			targetLevel = scan - SDLx_SCANCODE(1) + 1;
			targetFloor = targetLevel;
			drawCursor = true;
		}

		prev = ci;

		if(targetLevel >= 1 && targetLevel != currentFloor)
		{
			SD_PlaySound("blake/elevbutton");
			pressedButton = targetLevel - 1;

			const bool unlocked = floorMeta[base + targetLevel].unlocked;
			AActor *playermo = players[ConsolePlayer].mo;
			static const ClassDef * const redCard = ClassDef::FindClass("RedAccessCard");
			AInventory *card = redCard ? playermo->FindInventory(redCard) : NULL;

			if(unlocked)
				result = targetLevel;
			else if(card && targetLevel == lastUnlocked + 1)
			{
				result = targetLevel;
				useDelay = true;
				drawMessage = true;
				message = &messages[1];
				messageCurrent = -1;
				messageLocked = -1;

				playermo->RemoveInventory(card);
				card->Destroy();
			}
			else
			{
				useDelay = true;
				drawMessage = true;
				messageCurrent = -1;
				if(targetLevel == lastUnlocked + 1)
				{
					message = &messages[3];
					messageLocked = -1;
				}
				else
				{
					message = &messages[2];
					messageLocked = targetLevel;
				}
			}
		}

		if(drawMessage)
		{
			drawMessage = false;
			ElevDrawMessage(*message, messageCurrent, messageLocked);
			if(pressedButton >= 0)
				ElevDrawButton(pressedButton, true);
		}

		if(drawCursor)
		{
			drawCursor = false;
			ElevDrawCursor(currentFloor, targetFloor);
		}

		VW_UpdateScreen();

		if(screenfaded)
			VW_FadeIn();

		if(drawStats)
		{
			drawStats = false;
			ElevShowStats(167, 92);
		}

		if(useDelay)
		{
			useDelay = false;
			IN_UserInput(210, ACK_Local);

			message = &messages[0];
			messageCurrent = currentFloor;
			messageLocked = -1;
			drawMessage = true;
			drawCursor = true;
			if(pressedButton >= 0)
			{
				ElevDrawButton(pressedButton, false);
				pressedButton = -1;
			}
		}
	}

	IN_ClearKeysDown();

	return result >= 1 ? result : -1;
}

// =============================================================================
// The PS teleport panel (bstone ps_input_floor)
//
// Per-floor snapshots (bstone OverheadChunk): radar grid plus the stats the
// panel replays for floors other than the current one, and the departure
// pad so travelling back to a visited floor lands on its transporter.
// =============================================================================

struct PsChunk
{
	BYTE valid;
	BYTE padValid;
	fixed padX, padY;
	angle_t padAngle;
	DWORD treasureTotal, treasureCount;
	DWORD killTotal, killCount;
	BYTE radar[64*64];
};
static PsChunk psChunks[PS_FLOORS + 1]; // keyed by LevelNumber, slot 0 unused

void Blake_PsClear()
{
	memset(psChunks, 0, sizeof(psChunks));
}

bool Blake_PsFloorUnlocked(int lvl)
{
	if(lvl < 1 || lvl >= (int)countof(floorMeta))
		return false;
	return floorMeta[lvl].unlocked != 0;
}

void Blake_PsUnlockFloor(int lvl)
{
	if(lvl < 1 || lvl >= (int)countof(floorMeta))
		return;
	floorMeta[lvl].unlocked = true;
}

void Blake_PsSerialize(FArchive &arc)
{
	DWORD count = countof(psChunks);
	arc << count;
	if(!arc.IsStoring())
		Blake_PsClear();
	for(DWORD i = 0;i < count;++i)
	{
		PsChunk local;
		PsChunk &chunk = i < countof(psChunks) ? psChunks[i] : local;
		arc << chunk.valid << chunk.padValid
			<< chunk.padX << chunk.padY << chunk.padAngle
			<< chunk.treasureTotal << chunk.treasureCount
			<< chunk.killTotal << chunk.killCount;
		if(chunk.valid)
		{
			if(arc.IsStoring())
				arc.Write(chunk.radar, sizeof(chunk.radar));
			else
				arc.Read(chunk.radar, sizeof(chunk.radar));
		}
	}
}

// Teleport unit positions on the TELETOP installation map, 0-based.
static const int PS_TELE_X[PS_FLOORS] =
	{16,40,86,23,44,62,83,27,118,161,161,161,213,213,184,205,226,256,276,276};
static const int PS_TELE_Y[PS_FLOORS] =
	{13,26,9,50,50,50,50,62,42,17,26,35,41,50,62,62,62,10,10,30};

// Radar viewport (bstone TOV_X/TOV_Y).
static const int PS_TOV_X = 16;
static const int PS_TOV_Y = 132;

static FRandom pr_telenoise("TeleNoise");

static void PsDrawPic(const char *name, int x, int y)
{
	FTextureID texid = TexMan.CheckForTexture(name, FTexture::TEX_Any);
	if(texid.isValid())
		VWB_DrawGraphic(TexMan(texid), x, y);
}

// bstone VWB_DrawMPic: the unit and arrow pics are masked, palette index 255
// is transparent.  The textures are opaque, so blit column runs around the
// mask colour instead of using VWB_DrawGraphic.
static void PsDrawPicMasked(const char *name, int x, int y)
{
	FTextureID texid = TexMan.CheckForTexture(name, FTexture::TEX_Any);
	if(!texid.isValid())
		return;

	FTexture *tex = TexMan(texid);
	const BYTE *pixels = tex->GetPixels();
	const int w = tex->GetWidth(), h = tex->GetHeight();
	const BYTE mask = GPalette.Remap[255];
	for(int px = 0;px < w;++px)
	{
		const BYTE *col = pixels + px*h;
		for(int py = 0;py < h;)
		{
			if(col[py] == mask)
			{
				++py;
				continue;
			}

			const BYTE c = col[py];
			const int start = py;
			while(py < h && col[py] == c)
				++py;
			ElevBar(c, x + px, y + start, 1, py - start);
		}
	}
}

static void PsDrawUnit(int tp, bool lit)
{
	FString name;
	name.Format(lit ? "TELEON%02d" : "TELEOF%02d", tp + 1);
	PsDrawPicMasked(name, PS_TELE_X[tp], PS_TELE_Y[tp]);
}

// Selector arrows light while a direction is held (dir <0 up, >0 down).
static void PsDrawArrows(int dir)
{
	PsDrawPicMasked(dir < 0 ? "TELEUPON" : "TELEUPOF", 34, 91);
	PsDrawPicMasked(dir < 0 ? "TELEUPON" : "TELEUPOF", 270, 91);
	PsDrawPicMasked(dir > 0 ? "TELEDNON" : "TELEDNOF", 34, 104);
	PsDrawPicMasked(dir > 0 ? "TELEDNON" : "TELEDNOF", 270, 104);
}

// bstone if_noImage: placeholder text for floors without a radar snapshot.
static void PsDrawNoImage()
{
#if OF_DEVICE_BEHAVIOR
	static const char* const lines[6] =
		{"   AREA", "  UNMAPPED", "", "", "  PRESS A", " TO TELEPORT"};
#else
	static const char* const lines[6] =
		{"   AREA", "  UNMAPPED", "", "", " PRESS ENTER", " TO TELEPORT"};
#endif

	static const EColorRange color = V_FindFontColor("BlakeElevGreen");

	ElevBar(0x52, PS_TOV_X, PS_TOV_Y, 64, 64);
	int y = PS_TOV_Y + 13;
	for(unsigned int i = 0;i < countof(lines);++i)
	{
		PrintX = PS_TOV_X + 5;
		PrintY = y;
		US_Print(SmallFont, lines[i], color);
		y += 6;
	}
}

// Locked floors show static on the radar (bstone ShowOverhead zoom<0 snow).
static void PsDrawNoise()
{
	for(int my = 0;my < 64;++my)
	{
		for(int mx = 0;mx < 64;++mx)
			ElevBar(0x42 + (pr_telenoise() & 3), PS_TOV_X + mx, PS_TOV_Y + my, 1, 1);
	}
}

// bstone DisplayTeleportName: location bar between the two backgrounds.
static void PsDrawTeleportName(int tp, bool locked)
{
	static const EColorRange lockedColor = V_FindFontColor("BlakeTeleBright");
	static const EColorRange nameColor = V_FindFontColor("BlakeElevGreen");

	word w, h;
	ElevBar(0x52, 54, 101, 212, 9);

	FString text;
	EColorRange color;
	if(locked)
	{
		text = "-- TELEPORT DISABLED --";
		color = lockedColor;
	}
	else
	{
		FString key;
		key.Format("BLAKE_AREA_%d", tp + 1);
		text = language[key];
		text.ReplaceChars('\r', ' ');
		text.ReplaceChars('\n', ' ');
		text.StripRight();
		color = nameColor;
	}

	VW_MeasurePropString(SmallFont, text, w, h);
	ElevShadowText(SmallFont, text, 160 - w/2, 103, color);
}

// PS stats column (bstone ShowStats at 235,138): three ratio bars, the
// floor total, and the 20-floor mission total.  statsLvl >= 1 caches the
// floor sum like the AOG panel does.  Informant counts come straight from
// the census table (it is per-floor already, so no snapshot copy needed).
static void PsShowStats(int bx, int by, int statsLvl, int infLvl,
	DWORD tt, DWORD tc, DWORD kt, DWORD kc, bool quick)
{
	statsQuick = quick;

	const DWORD it = Blake_InformantsTotal(infLvl);
	const DWORD ia = Blake_InformantsAlive(infLvl);

	const int p1 = ElevShowRatio(bx, by, tt, tc);
	const int p2 = ElevShowRatio(bx, by + 7, it, ia);
	const int p3 = ElevShowRatio(bx, by + 14, kt, kc);

	const int floorSum = p1 + p2 + p3;
	const int maxPerFloor = (tt || kt || it) ? 300 : 0;
	if(statsLvl >= 1 && statsLvl < (int)countof(floorMeta))
		floorMeta[statsLvl].overallFloor = floorSum;
	ElevShowRatio(bx, by + 27, maxPerFloor, floorSum);

	int mission = 0;
	for(int f = 1;f <= PS_FLOORS;++f)
		mission += floorMeta[f].overallFloor;
	ElevShowRatio(bx, by + 34, PS_FLOORS * 300, mission);
}

// Returns the chosen unit (0-based) or -1 for cancel.
static int PsInputFloor()
{
	static const EColorRange helpColor = V_FindFontColor("BlakeTeleWhite");

	const int lvl = levelInfo->LevelNumber;
	int tpNum = lvl - 1;
	int lastTpNum = tpNum;

	// Snapshot the current floor for later visits (bstone SaveOverheadChunk).
	PsChunk &cur = psChunks[lvl];
	cur.valid = 1;
	cur.treasureTotal = gamestate.treasuretotal;
	cur.treasureCount = gamestate.treasurecount;
	cur.killTotal = gamestate.killtotal;
	cur.killCount = gamestate.killcount;
	ElevOverheadGrid(cur.radar, true, 0x52);

	VW_FadeOut();

	PsDrawPic("TELETOP", 0, 0);
	PsDrawPic("TELEBOT", 0, 96);

	bool locked = false;
	PsDrawTeleportName(tpNum, locked);
	PsDrawUnit(tpNum, true);
	PsDrawArrows(0);
	ElevBlitGrid(cur.radar, PS_TOV_X, PS_TOV_Y);
#if OF_DEVICE_BEHAVIOR
	ElevShadowText(SmallFont, "UP/DN MOVES SELECTOR - A ACTIVATES", 115, 188, helpColor);
#else
	ElevShadowText(SmallFont, "UP/DN MOVES SELECTOR - ENTER ACTIVATES", 115, 188, helpColor);
#endif

	IN_ClearKeysDown();

	int result = -2;
	bool buttonsDrawn = false;
	DWORD nextMove = 0;

	// Seed edge detection with whatever is still held from Cmd_Use.
	ControlInfo ci, prev;
	ReadAnyControl(&prev);

	while(result == -2)
	{
		ReadAnyControl(&ci);
		const ScanCode scan = LastScan;
		IN_ClearKeysDown();

		const int dir =
			(ci.dir == dir_North || ci.dir == dir_West) ? -1 :
			(ci.dir == dir_South || ci.dir == dir_East) ? 1 : 0;

#if OF_DEVICE_BEHAVIOR
		// Pocket: B (sc_Space) is the advertised back; START (sc_Escape) also backs.
		if(scan == sc_Space || scan == sc_Escape)
			result = -1;
#else
		if(scan == sc_Escape || (ci.button1 && !prev.button1))
			result = -1;
#endif
		else if(scan == sc_Enter || (ci.button0 && !prev.button0))
		{
			if(locked)
				SD_PlaySound("player/usefail");
			else
			{
				result = tpNum;

				// Acknowledge flash on the chosen unit.
				for(int loop = 0;loop < 10;++loop)
				{
					PsDrawUnit(tpNum, false);
					VW_UpdateScreen();
					VW_WaitVBL(4);
					PsDrawUnit(tpNum, true);
					VW_UpdateScreen();
					VW_WaitVBL(4);
				}
			}
		}

		// Held directions step once per 10 tics.
		if(result == -2 && dir && (DWORD)GetTimeCount() >= nextMove)
		{
			if(dir < 0 && tpNum > 0)
				--tpNum;
			else if(dir > 0 && tpNum < PS_FLOORS - 1)
				++tpNum;
			nextMove = (DWORD)GetTimeCount() + 10;
		}

		if(dir)
		{
			PsDrawArrows(dir);
			buttonsDrawn = true;
		}
		else if(buttonsDrawn)
		{
			PsDrawArrows(0);
			buttonsDrawn = false;
		}

		prev = ci;

		if(tpNum != lastTpNum)
		{
			locked = !Blake_PsFloorUnlocked(tpNum + 1);

			PsDrawTeleportName(tpNum, locked);
			PsDrawUnit(lastTpNum, false);
			PsDrawUnit(tpNum, true);

			const PsChunk &chunk = psChunks[tpNum + 1];
			if(!locked)
			{
				if(chunk.valid)
					ElevBlitGrid(chunk.radar, PS_TOV_X, PS_TOV_Y);
				else
					PsDrawNoImage();
			}
			PsShowStats(235, 138, tpNum + 1 == lvl ? lvl : -1, tpNum + 1,
				chunk.treasureTotal, chunk.treasureCount,
				chunk.killTotal, chunk.killCount, true);

			lastTpNum = tpNum;
		}

		if(locked)
			PsDrawNoise();

		VW_UpdateScreen();

		if(screenfaded)
		{
			VW_FadeIn();
			PsShowStats(235, 138, lvl, lvl,
				cur.treasureTotal, cur.treasureCount,
				cur.killTotal, cur.killCount, false);
			IN_ClearKeysDown();
		}
	}

	IN_ClearKeysDown();

	return result;
}

// =============================================================================
// Panel entry
// =============================================================================

static void ElevatorCheckAOG()
{
	const int lvl = levelInfo->LevelNumber;
	const int currentFloor = FloorIndex(lvl);
	if(currentFloor < 1 || currentFloor > 10)
		return;

	const int floor = ElevInputFloor();

	if(floor >= 1 && floor != currentFloor)
	{
		AActor *playermo = players[ConsolePlayer].mo;
		playstate = ex_newmap;
		NewMap.newmap = EpisodeBase(lvl) + floor;
		NewMap.flags = 0;
		NewMap.x = playermo->x;
		NewMap.y = playermo->y;
		NewMap.angle = playermo->angle;
	}
	else
	{
		DrawPlayScreen();
	}
}

static void ElevatorCheckPS()
{
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl > PS_FLOORS)
		return;

	const int tp = PsInputFloor();

	if(tp >= 0 && tp + 1 != lvl)
	{
		AActor *playermo = players[ConsolePlayer].mo;

		// Departure pad: travelling back to this floor lands here, facing
		// away from the pad (bstone select_floor).
		PsChunk &cur = psChunks[lvl];
		cur.padValid = 1;
		cur.padX = playermo->x;
		cur.padY = playermo->y;
		cur.padAngle = playermo->angle + ANGLE_180;

		playstate = ex_newmap;
		NewMap.newmap = tp + 1;
		const PsChunk &dst = psChunks[tp + 1];
		if(dst.padValid)
		{
			NewMap.flags = NEWMAP_KEEPPOSITION|NEWMAP_KEEPFACING;
			NewMap.x = dst.padX;
			NewMap.y = dst.padY;
			NewMap.angle = dst.padAngle;
		}
		else
		{
			NewMap.flags = 0;
			NewMap.x = playermo->x;
			NewMap.y = playermo->y;
			NewMap.angle = playermo->angle;
		}
	}
	else
	{
		DrawPlayScreen();
	}
}

void Blake_ElevatorCheck()
{
	if(!Blake_ElevatorRequested)
		return;
	Blake_ElevatorRequested = false;

	if(!IsBlake() || playstate != ex_stillplaying)
		return;

	if(IsAOG())
		ElevatorCheckAOG();
	else
		ElevatorCheckPS();

	// The buttons the panel cancelled/confirmed on are still latched/held when
	// it returns: Start synthesises Escape (which CheckKeys would turn into a
	// main-menu open), and the B / Use button that opened the panel is still
	// down (so the next Cmd_Use would immediately re-request it).  Consume both
	// as held so neither leaks into the frame after the panel closes.
	control[ConsolePlayer].buttonstate[bt_esc] = false;
	control[ConsolePlayer].buttonheld[bt_esc] = true;
	control[ConsolePlayer].buttonstate[bt_use] = true;
	control[ConsolePlayer].buttonheld[bt_use] = true;
}
