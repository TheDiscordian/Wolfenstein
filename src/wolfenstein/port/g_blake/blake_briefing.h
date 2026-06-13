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

#endif // __BLAKE_BRIEFING_H__
