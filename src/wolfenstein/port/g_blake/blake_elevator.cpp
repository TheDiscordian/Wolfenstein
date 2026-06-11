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
#include "thinker.h"
#include "v_font.h"
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
}

void Blake_FloorEntered()
{
	if(!IsBlake() || !levelInfo)
		return;
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
	if(!IsBlake() || !levelInfo)
		return;
	const int lvl = levelInfo->LevelNumber;
	if(lvl < 1 || lvl >= (int)countof(floorMeta))
		return;
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
// shading is not tracked here.
static void ElevDrawOverhead(int bx, int by)
{
	const BYTE UNMAPPED_COLOR = 0x06;
	const BYTE MAPPED_COLOR = 0x55;

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
			BYTE color = UNMAPPED_COLOR;

			if((int)mx == ptilex && (int)my == ptiley)
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

				if(color == MAPPED_COLOR || color == UNMAPPED_COLOR)
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

			ElevBar(color, bx + (int)mx, by + (int)my, 1, 1);
		}
	}
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

	if(!total)
	{
		ElevBar(0, bx, by, BAR_W, BAR_H);
		ElevBar(0, nx, by, 19, BAR_H);
		PrintX = nx;
		PrintY = by;
		US_Print(SmallFont, "N/A", naColor);
		return 100;
	}

	const int maxperc = accum * 100 / total;
	const int numbars = maxperc * 48 / 100;

	ElevBar(0x07, bx, by, BAR_W, BAR_H);

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

	return maxperc;
}

static void ElevShowStats(int bx, int by)
{
	const int lvl = levelInfo->LevelNumber;

	statsQuick = false;

	// TOTAL POINTS, INFORMANTS ALIVE (not tracked yet), ENEMY DESTROYED.
	const int p1 = ElevShowRatio(bx, by, gamestate.treasuretotal, gamestate.treasurecount);
	const int p2 = ElevShowRatio(bx, by + 7, 0, 0);
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

	ElevDrawBottomBox(LoadPanelText("FLOORMSG",
		"Press a number to select a floor.\n(0 selects floor 10)"));
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

		if(scan == sc_Escape || (ci.button1 && !prev.button1))
			result = -1;
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
		else if(scan == sc_Space || scan == sc_Enter || (ci.button0 && !prev.button0))
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

void Blake_ElevatorCheck()
{
	if(!Blake_ElevatorRequested)
		return;
	Blake_ElevatorRequested = false;

	if(!IsBlake() || !IsAOG() || playstate != ex_stillplaying)
		return;

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
