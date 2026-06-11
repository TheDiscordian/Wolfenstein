#ifndef __BLAKE_BARRIER_H__
#define __BLAKE_BARRIER_H__

class FArchive;

// Global barrier switch table (bstone gamestate.barrier_table): maps
// (level, link x, link y) to a barrier group's on/off state. It outlives
// level loads so cross-floor switches work, and is carried in save games.

// Wipes everything at the start of a new game.
void Blake_BarrierClear();
// Called at map load before objects are parsed.
void Blake_BarrierStartLevel();
// Inserts an entry with the given state if none exists; returns the state.
bool Blake_BarrierEnsure(unsigned int levelNum, unsigned int x, unsigned int y, bool on);
bool Blake_BarrierGet(unsigned int levelNum, unsigned int x, unsigned int y);
void Blake_BarrierSet(unsigned int levelNum, unsigned int x, unsigned int y, bool on);
// Records a switch wall on the current map wired to a table entry so it
// can be repainted when the entry changes.
void Blake_BarrierAddSwitchCell(unsigned int x, unsigned int y, unsigned int levelNum, unsigned int lx, unsigned int ly);
// Sets every barrier actor in the tagged group to the given state.
void Blake_BarrierSetActors(unsigned int tag, bool on);
// Repaints every recorded switch wall wired to the entry.
void Blake_BarrierRepaintSwitches(unsigned int levelNum, unsigned int lx, unsigned int ly, bool on);
// InfoArea message for operating a wall switch (DOS DisplaySwitchOperateMsg).
void Blake_BarrierOperateMsg(unsigned int levelNum, bool on);
// One-shot after the level's actors spawn: converges barrier groups and
// switch walls to the table.
void Blake_BarrierApply();
void Blake_BarrierSerialize(FArchive &arc);
// An old save has no table; drop parse-time state so it plays out unchanged.
void Blake_BarrierLoadLegacy();

#endif
