//
//  of_ecwolf_opl_music.h -- authentic OPL2 (AdLib) music playback for the
//  openfpgaOS ECWolf core.
//
//  Plays a game's native IMF (sqHack OPL register script) through the DBOPL
//  emulator, the same DOS AdLib sound the engine produced on real hardware.
//  Used when no Standard-MIDI replacement exists in the music pack (e.g.
//  Blake Stone, which ships no MIDI pack).
//

#ifndef OF_ECWOLF_OPL_MUSIC_H
#define OF_ECWOLF_OPL_MUSIC_H

#include <stdint.h>

#ifdef OF_ECWOLF_OPENFPGA

// Start playing the raw IMF chunk (the lump bytes straight from AUDIOT).
// The data is copied internally, so the caller may free its buffer after.
// loop = restart the sequence when it ends.  Returns true on success.
bool OPLMusic_Start(const uint8_t *imf, int len, bool loop);

// Stop playback, detach the pump, and silence the chip.
void OPLMusic_Stop(void);

// True while an IMF sequence is active.
bool OPLMusic_Playing(void);

#endif // OF_ECWOLF_OPENFPGA

#endif // OF_ECWOLF_OPL_MUSIC_H
