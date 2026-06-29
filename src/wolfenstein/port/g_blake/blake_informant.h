#ifndef __BLAKE_INFORMANT_H__
#define __BLAKE_INFORMANT_H__

class AActor;
class FArchive;

// Hint word types from map plane 1 (high byte 0xF1-0xF3).
enum
{
	BLAKE_HINT_INFORMANT = 0,
	BLAKE_HINT_NICE      = 1,
	BLAKE_HINT_MEAN      = 2
};

// Called at map load before objects are parsed.
void Blake_ClearHints();
// Clears the once-per-game "don't shoot the informants" warning (new-game only).
void Blake_InformantNewGame();
// Records one scientist hint word; msgnum is the 1-based message number.
void Blake_AddHint(unsigned int type, unsigned int x, unsigned int y, unsigned int msgnum);
// Interrogates the nearest friendly scientist in front of the player.
// Returns false if there was nobody to question.
bool Blake_TryInterrogate(AActor *playerMo);

// Per-floor informant census for the panel stats (DOS total_inf/accum_inf,
// 3d_game.c:2076), keyed by LevelNumber.  Spawns count via AActor::Spawn; deaths decrement.
void Blake_InformantsClear();
void Blake_InformantsReset();
void Blake_InformantSpawned(AActor *actor);
int Blake_InformantsTotal(int lvl);
int Blake_InformantsAlive(int lvl);
void Blake_InformantSerialize(FArchive &arc);

#endif
