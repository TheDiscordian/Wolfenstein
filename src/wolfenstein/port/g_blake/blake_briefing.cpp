// blake_briefing.cpp
//
// Blake Stone mission briefing entry points.  Render the BRIEFI<cluster>
// (pre-mission intro) and BRIEFW<cluster> (mission win debrief) VGAGRAPH text
// chunks through the JAM Text Presenter (jm_tp.cpp), mirroring the PresenterInfo
// setup of DOS HelpPresenter (3d_menu.c:297) and the Breifing()
// dispatch (3d_inter.c:57).

#include <string.h>
#include <stdio.h>

#include "wl_def.h"
#include "id_in.h"
#include "id_ca.h"
#include "id_us.h"
#include "id_vh.h"
#include "w_wad.h"
#include "wl_iwad.h"
#include "wl_menu.h"
#include "wl_game.h"
#include "wl_play.h"
#include "g_mapinfo.h"
#include "textures/textures.h"

#include "jm_tp.h"
#include "blake_briefing.h"

// Render a single briefing/text VGAGRAPH chunk (BRIEFI/BRIEFW/SAGAART/ORDERART)
// through the Text Presenter.  Shared by the briefings and the Story/Ordering
// menu screens.  fadeOutAfter leaves a black screen on return (menu callers fade
// the menu back in); briefings omit it because GameLoop fades before the level.
static void Blake_PresentBriefingLump(const char *lumpname, bool fadeOutAfter = false, bool continueKeys = true)
{
	int lumpnum = Wads.CheckNumForName(lumpname);
	if (lumpnum == -1)
	{
		return;
	}

	PresenterInfo pi;
	memset(&pi, 0, sizeof(pi));

	// Briefings let ENTER advance pages; the Instructions / Story / Ordering
	// screens do not (DOS continue_keys=false, 3d_menu.c:287/561/576).  ESC
	// always aborts.
	pi.flags = TPF_SHOW_PAGES | (continueKeys ? TPF_CONTINUE : 0) | TPF_ABORTABLE;

	VW_FadeOut();

	// TP_Presenter only fills its text region (xl..xh / yl..yh), not the whole
	// screen, and DOS HelpPresenter draws a full-screen help-window border
	// the port lacks -- so without this the previous menu's LINC frame bleeds
	// through around the briefing.  Clear the framebuffer to the briefing bg
	// (0x7d, == pi.bgcolor below).  Still faded to black, so no flicker.
	VWB_Clear(0x7d, 0, 0, screenWidth, screenHeight);

	// DOS HelpPresenter frames the text with a full-screen help-window border
	// (3d_menu.c:321); the four edge pics ship in the Blake data (vsimap/bs6map).
	static const FTextureID winPics[4] = {
		TexMan.GetTexture("TOPWINDW", FTexture::TEX_Any),
		TexMan.GetTexture("LFTWINDW", FTexture::TEX_Any),
		TexMan.GetTexture("RGTWINDW", FTexture::TEX_Any),
		TexMan.GetTexture("BOTWINDW", FTexture::TEX_Any),
	};
	static const int winPos[4][2] = { {0,0}, {0,8}, {312,8}, {8,176} };
	for(int i = 0;i < 4;++i)
		if(FTexture *t = TexMan(winPics[i]))
			VWB_DrawGraphic(t, winPos[i][0], winPos[i][1], MENU_NONE);

	// Text region + colours, copied from DOS HelpPresenter (3d_menu.c:329).
	// The script's own ^BC/^LC/^DC/^SC codes override these per page; these are
	// the defaults used before the first such code fires.
	pi.xl = 8;
	pi.yl = 8;
	pi.xh = 311;
	pi.yh = 175;
	pi.ltcolor = 0x7b;
	pi.bgcolor = 0x7d;
	pi.dkcolor = 0x7f;
	pi.shcolor = 0x00;
	pi.fontnumber = 4;

#if OF_DEVICE_BEHAVIOR
	// Pocket has no ESC/ENTER: pages = D-pad, A confirms, B backs out.
	pi.infoline = (char*)"   UP / DN - PAGES     A - CONTINUE     B - EXIT";
#else
	pi.infoline = (char*)"           UP / DN - PAGES            ESC - EXITS";
#endif

	// Load, present, and free the briefing text.
	TP_LoadScript(lumpnum, &pi);
	TP_Presenter(&pi);
	TP_FreeScript(&pi);

	// Briefings omit a trailing VW_FadeOut(): GameLoop (wl_game.cpp) fades to
	// black before SetupGameLevel, so a fade here would be a redundant, visible
	// fade-to-black (VW_FadeOut is unguarded).  Menu-driven screens pass
	// fadeOutAfter to return on a black screen for the caller to fade the menu in.
	if (fadeOutAfter)
		VW_FadeOut();

	IN_ClearKeysDown();
}

// BRIEFI<cluster>: the pre-mission ("intro") briefing, shown by EnterText.
void Blake_ShowBriefing(int cluster)
{
	// Inert unless this is a Blake game.
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}

	if (cluster < 1)
	{
		cluster = 1;
	}

	char lumpname[16];
	snprintf(lumpname, sizeof(lumpname), "BRIEFI%d", cluster);
	Blake_PresentBriefingLump(lumpname);
}

// BRIEFW<cluster>: the mission win debriefing, shown by Victory() when a mission
// completes -- DOS Breifing(BT_WIN) at ex_victorious (3d_inter.c:57).
void Blake_ShowWinBriefing(int cluster)
{
	// Inert unless this is a Blake game.
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}

	if (cluster < 1)
	{
		cluster = 1;
	}

	char lumpname[16];
	snprintf(lumpname, sizeof(lumpname), "BRIEFW%d", cluster);
	Blake_PresentBriefingLump(lumpname);
}

// The defeat screen: the LOSEPIC background art with the lose message scrolled in
// the bottom band -- DOS LoseScreen() (3d_game.c:3104).  In the AOG VGAGRAPH
// the lump bs6map calls "LOSEART" holds the lose presenter script (the "REBA:
// INCOMING TRANSMISSION" message); "LOSEPIC" is the full-screen backdrop.
void Blake_ShowLoseScreen()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}

	FTextureID losePic = TexMan.CheckForTexture("LOSEPIC", FTexture::TEX_Any);
	int textLump = Wads.CheckNumForName("LOSEART");

	VW_FadeOut();

	if (losePic.isValid())
	{
		CA_CacheScreen(TexMan(losePic));
		VW_UpdateScreen();
	}

	if (textLump != -1)
	{
		PresenterInfo pi;
		memset(&pi, 0, sizeof(pi));

		// DOS LoseScreen flags + region (3d_game.c:3111): keep the cached
		// LOSEPIC as the backdrop (TPF_USE_CURRENT) and scroll the message in the
		// bottom band.
		pi.flags = TPF_USE_CURRENT | TPF_SHOW_CURSOR | TPF_SCROLL_REGION |
			TPF_CONTINUE | TPF_TERM_SOUND | TPF_ABORTABLE;
		pi.xl = 14;
		pi.yl = 141;
		pi.xh = 14 + 293;
		pi.yh = 141 + 32;
		pi.ltcolor = 15;
		pi.bgcolor = 0;
		pi.dkcolor = 1;
		pi.shcolor = 1;
		pi.fontnumber = 2;
		pi.cur_x = (uint16_t)-1;
		pi.print_delay = 2;

		TP_LoadScript(textLump, &pi);
		VW_FadeIn();
		TP_Presenter(&pi);
		TP_FreeScript(&pi);
	}
	else
	{
		// No text chunk -- hold the art until acknowledged.
		VW_FadeIn();
		IN_Ack(ACK_Any);
	}

	VW_FadeOut();
	IN_ClearKeysDown();
}

// The STORY screen: presents the Saga text (the "SAGAART" lump in this data holds
// the saga presenter script) -- DOS CP_BlakeStoneSaga -> HelpPresenter(SAGATEXT) (3d_menu.c:572).
void Blake_ShowStory()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}
	Blake_PresentBriefingLump("SAGAART", true, false);
}

// The ORDERING INFO screen: presents the ordering text ("ORDERART" lump) --
// DOS CP_OrderingInfo -> HelpPresenter(ORDERTEXT) (3d_menu.c:557).
void Blake_ShowOrdering()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}
	Blake_PresentBriefingLump("ORDERART", true, false);
}

// The INSTRUCTIONS screen: presents the help text ("HELPART" lump, which holds
// the character-profile presenter script with the ^AN enemy/device animations) --
// DOS CP_ReadThis -> HelpScreens -> HelpPresenter(HELPTEXT) (3d_menu.c:546).  The port's
// generic ECWolf HelpScreens() runs ShowArticle, which doesn't understand the
// Blake presenter codes, so Blake routes here instead.
void Blake_ShowInstructions()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}
	Blake_PresentBriefingLump("HELPART", true, false);
}

// DOS TerminateStr (3d_menu.c:3545): trim at the "^XX" end marker.
static bool Blake_LoadQuickInfoLump(const char *name, FString &out)
{
	int ln = Wads.CheckNumForName(name, ns_global);
	if(ln == -1)
		return false;
	FMemLump lump = Wads.ReadLump(ln);
	out = FString((const char*)lump.GetMem(), Wads.LumpLength(ln));
	long term = out.IndexOf("^XX");
	if(term >= 0)
		out.Truncate(term);
	return true;
}

// First-time QUICK_INFO instructions (DOS ShowQuickInstructions, 3d_play.c:1849).
// On a brand-new game's first floor, two text pages (QUIKINF1/QUIKINF2) pop over
// the live view; page 1 auto-advances after ~120 tics if the player is idle, and
// any key dismisses.  One-shot per new game (g_showQuickInfo); inert outside
// Blake, never in a demo, and on PS only the first floor.
void Blake_ShowQuickInfo()
{
	if(!IWad::CheckGameFilter("Blake"))
		return;
	if(demoplayback)
		return;
	// bstone (is_ps() && mapon>0) gate. AOG (>1 episode) always shows because the
	// one-shot flag is only ever set on the first floor anyway.
	if(EpisodeInfo::GetNumEpisodes() == 1 && ((int)levelInfo->LevelNumber - 1) > 0)
		return;

	FString s1;
	if(!Blake_LoadQuickInfoLump("QUIKINF1", s1))
		return;

	WindowH = 168;	// Message() centres on WindowH
	Message(s1);
	// DOS holds page 1 for ~120 tics, then ALWAYS shows page 2 (3d_play.c:1860);
	// a keypress during page 1 must not skip page 2.
	IN_UserInput(120, ACK_Local);

	FString s2;
	if(Blake_LoadQuickInfoLump("QUIKINF2", s2))
	{
		Message(s2);
		IN_Ack(ACK_Local);
	}

	IN_ClearKeysDown();
	DrawPlayScreen();	// wipe the box (DOS CleanDrawPlayBorder)
}
