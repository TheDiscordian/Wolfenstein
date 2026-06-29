/*
** blake_blastdoor.h
**
** Blake Stone blastable doors: explosions force nearby closed, unlocked doors
** open (DOS BlastNearDoors / TryBlastDoor, 3d_act1.c:1143).
**
*/

#ifndef __BLAKE_BLASTDOOR_H__
#define __BLAKE_BLASTDOOR_H__

class AActor;

// Force every closed, unlocked, non-one-way door in the explosion's 3x3 tile
// neighborhood open, with a cosmetic door explosion on each.
void Blake_BlastDoorsNear(AActor *origin);

#endif
