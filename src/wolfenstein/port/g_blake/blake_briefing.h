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

#endif // __BLAKE_BRIEFING_H__
