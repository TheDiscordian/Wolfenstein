#ifndef __BLAKE_FLOOR_H__
#define __BLAKE_FLOOR_H__

class FArchive;

// Per-floor world snapshots (bstone's saved level states): leaving a floor
// by elevator stores its world, and returning restores it so enemies,
// doors, and pickups stay as left. A snapshot is consumed when restored,
// so a death restart plays the floor fresh. Carried in save games.

// Wipes everything at the start of a new game.
void Blake_FloorClear();
// Snapshots the departing floor; call right after StartTravel().
void Blake_FloorCapture();
// True when the map about to load has a snapshot and we got there by
// elevator (not a warp or death restart).
bool Blake_FloorPending();
// Restores and consumes the pending snapshot over the freshly parsed map,
// in place of SpawnThings. Returns false if nothing is pending.
bool Blake_FloorRestore();
void Blake_FloorSerialize(FArchive &arc);

#endif
