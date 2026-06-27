// blake_briefing.h
//
// Blake Stone mission briefing entry point.  Renders the BRIEFI<cluster>
// VGAGRAPH text chunk through the JAM Text Presenter (jm_tp), mirroring
// bstone's HelpPresenter / Breifing setup.

#ifndef __BLAKE_BRIEFING_H__
#define __BLAKE_BRIEFING_H__

// Show the pre-mission ("intro") briefing for the given mission cluster
// (1..6).  Inert unless the current IWAD is a Blake game.  Loads BRIEFI<cluster>
// from the VGAGRAPH, fades out, presents, then fades.
void Blake_ShowBriefing(int cluster);

// Show the mission win debriefing for the given mission cluster (1..6), loading
// BRIEFW<cluster>.  Inert unless the current IWAD is a Blake game.  Called from
// Victory() at ex_victorious, mirroring bstone's Breifing(BT_WIN).
void Blake_ShowWinBriefing(int cluster);

// Show the defeat screen (LOSEPIC backdrop + the LOSEART lose message) when the
// player runs out of lives.  Inert unless the current IWAD is a Blake game.
// Mirrors bstone's LoseScreen() at ex_died.
void Blake_ShowLoseScreen();

// Present the Story (SAGAART) and Ordering (ORDERART) text screens for the Blake
// "READ THIS!" menu.  Inert unless the current IWAD is a Blake game.  Mirror
// bstone's CP_BlakeStoneSaga / CP_OrderingInfo.
void Blake_ShowStory();
void Blake_ShowOrdering();

// Present the Instructions (HELPART) text screen for the Blake "READ THIS!" menu.
// HELPART carries the ^AN-animated character-profile script, so it must run
// through TP_Presenter rather than ECWolf's ShowArticle.  Inert unless the current
// IWAD is a Blake game.  Mirrors bstone's CP_ReadThis -> HelpScreens.
void Blake_ShowInstructions();

// First-time QUICK_INFO instructions box (QUIKINF1/QUIKINF2) on a brand-new
// game's first floor.  Inert outside Blake; PS only on its first floor.
void Blake_ShowQuickInfo();

#endif // __BLAKE_BRIEFING_H__
