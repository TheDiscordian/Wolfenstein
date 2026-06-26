// blake_briefing.cpp
//
// Blake Stone mission briefing entry points.  Render the BRIEFI<cluster>
// (pre-mission intro) and BRIEFW<cluster> (mission win debrief) VGAGRAPH text
// chunks through the JAM Text Presenter (jm_tp.cpp), mirroring the PresenterInfo
// setup of bstone's HelpPresenter (3d_menu.cpp:1664) and the Breifing()
// dispatch (3d_inter.cpp:42).

#include <string.h>
#include <stdio.h>

#include "wl_def.h"
#include "id_in.h"
#include "id_ca.h"
#include "id_vh.h"
#include "w_wad.h"
#include "wl_iwad.h"
#include "textures/textures.h"

#include "jm_tp.h"
#include "blake_briefing.h"

// Render a single briefing/text VGAGRAPH chunk (BRIEFI/BRIEFW/SAGAART/ORDERART)
// through the Text Presenter.  Shared by the briefings and the Story/Ordering
// menu screens.  fadeOutAfter leaves a black screen on return (menu callers fade
// the menu back in); briefings omit it because GameLoop fades before the level.
static void Blake_PresentBriefingLump(const char *lumpname, bool fadeOutAfter = false)
{
	int lumpnum = Wads.CheckNumForName(lumpname);
	if (lumpnum == -1)
	{
		return;
	}

	PresenterInfo pi;
	memset(&pi, 0, sizeof(pi));

	// Mirror HelpPresenter's flags: show page counter, ENTER continues, ESC
	// aborts.
	pi.flags = TPF_SHOW_PAGES | TPF_CONTINUE | TPF_ABORTABLE;

	VW_FadeOut();

	// TP_Presenter only fills its text region (xl..xh / yl..yh), not the whole
	// screen, and bstone's HelpPresenter draws a full-screen help-window border
	// the port lacks -- so without this the previous menu's LINC frame bleeds
	// through around the briefing.  Clear the framebuffer to the briefing bg
	// (0x7d, == pi.bgcolor below).  Still faded to black, so no flicker.
	VWB_Clear(0x7d, 0, 0, screenWidth, screenHeight);

	// Text region + colours, copied from HelpPresenter (3d_menu.cpp:1704).
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
	// black before SetupGameLevel, so a fade here was a duplicate, visible
	// 30-step fade-to-black (VW_FadeOut is unguarded).  Menu-driven screens pass
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
// completes -- bstone Breifing(BT_WIN) at ex_victorious (3d_inter.cpp:42).
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
// the bottom band -- bstone LoseScreen() (3d_game.cpp:3247).  In the AOG VGAGRAPH
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

		// bstone LoseScreen flags + region (3d_game.cpp:3249): keep the cached
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
// the saga presenter script) -- bstone CP_BlakeStoneSaga -> HelpPresenter(SAGATEXT).
void Blake_ShowStory()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}
	Blake_PresentBriefingLump("SAGAART", true);
}

// The ORDERING INFO screen: presents the ordering text ("ORDERART" lump) --
// bstone CP_OrderingInfo -> HelpPresenter(ORDERTEXT).
void Blake_ShowOrdering()
{
	if (!IWad::CheckGameFilter("Blake"))
	{
		return;
	}
	Blake_PresentBriefingLump("ORDERART", true);
}
