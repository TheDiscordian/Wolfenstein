// blake_briefing.cpp
//
// Blake Stone mission briefing entry point.  Renders the BRIEFI<cluster>
// VGAGRAPH text chunk through the JAM Text Presenter (jm_tp.cpp), mirroring
// the PresenterInfo setup of bstone's HelpPresenter (3d_menu.cpp:1664) and the
// Breifing() dispatch (3d_inter.cpp:42).

#include <string.h>
#include <stdio.h>

#include "wl_def.h"
#include "id_in.h"
#include "id_vh.h"
#include "w_wad.h"
#include "wl_iwad.h"

#include "jm_tp.h"
#include "blake_briefing.h"

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

	// BRIEFI<cluster> is the pre-mission ("intro") briefing chunk.  (BRIEFW<n>
	// is the win briefing.)
	char lumpname[16];
	snprintf(lumpname, sizeof(lumpname), "BRIEFI%d", cluster);

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

	pi.infoline = (char*)"           UP / DN - PAGES            ESC - EXITS";

	// Load, present, and free the briefing text.
	TP_LoadScript(lumpnum, &pi);
	TP_Presenter(&pi);
	TP_FreeScript(&pi);

	VW_FadeOut();

	IN_ClearKeysDown();
}
