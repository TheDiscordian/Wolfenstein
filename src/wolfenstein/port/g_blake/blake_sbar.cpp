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
#include "wl_play.h"
#include "xs_Float.h"
#include "thingdef/thingdef.h"

enum
{
	STATUSLINES = 48,
	STATUSTOPLINES = 16
};

class BlakeStatusBar : public DBaseStatusBar
{
public:
	BlakeStatusBar() : CurrentScore(0), InfoMessagePriority(0), InfoMessageTics(0) {}

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
	}

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
};

DBaseStatusBar *CreateStatusBar_Blake() { return new BlakeStatusBar(); }

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

	screen->DrawTexture(TexMan(STBar), stx, sty,
		DTA_DestWidthF, stw,
		DTA_DestHeightF, sth,
		TAG_DONE);

	stx = 0;
	sty = 0;
	stw = 320;
	sth = STATUSTOPLINES;
	screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
	int topy = xs_ToInt(static_cast<real64>(sth));

	screen->DrawTexture(TexMan(STBarTop), stx, 0.0,
		DTA_DestWidthF, stw,
		DTA_DestHeightF, sth,
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

	FString health;
	health.Format("%3d", players[ConsolePlayer].health);
	DrawString(HealthFont, health, 128, 162, false);

	FString score;
	score.Format("%7d", CurrentScore);
	DrawString(ScoreFont, score, 256, 155, false);

	if(players[ConsolePlayer].ReadyWeapon)
	{
		FTexture *weapon = TexMan(players[ConsolePlayer].ReadyWeapon->icon);
		if(weapon)
		{
			stx = 248;
			sty = 176;
			stw = weapon->GetScaledWidthDouble();
			sth = weapon->GetScaledHeightDouble();
			screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
			screen->DrawTexture(weapon, stx, sty,
				DTA_DestWidthF, stw,
				DTA_DestHeightF, sth,
				TAG_DONE);
		}

		// TODO: Fix color
		unsigned int amount = players[ConsolePlayer].ReadyWeapon->ammo[AWeapon::PrimaryFire]->amount;
		DrawLed(static_cast<double>(amount)/static_cast<double>(players[ConsolePlayer].ReadyWeapon->ammo[AWeapon::PrimaryFire]->maxamount), 243, 155);

		FString ammo;
		ammo.Format("%3d%%", amount);
		DrawString(IndexFont, ammo, 252, 190, false, CR_LIGHTBLUE);
	}

	if(players[ConsolePlayer].mo)
	{
		static const ClassDef * const radarPackCls = ClassDef::FindClass("RadarPack");
		AInventory *radarPack = players[ConsolePlayer].mo->FindInventory(radarPackCls);
		if(radarPack)
			DrawLed(static_cast<double>(radarPack->amount)/static_cast<double>(radarPack->maxamount), 235, 155);
		else
			DrawLed(0, 235, 155);
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
	double y = 200-STATUSLINES+3;
	FString line;
	for(const char* ch = msg.GetChars();;++ch)
	{
		if(*ch == '\r' || *ch == '\0')
		{
			if(line.Len())
				DrawString(SmallFont, line, 3, y, true, CR_GRAY);
			if(*ch == '\0')
				break;
			line = "";
			y += 6;
		}
		else
			line += *ch;
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
}
