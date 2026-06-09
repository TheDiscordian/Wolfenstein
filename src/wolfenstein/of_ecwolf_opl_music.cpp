//
//  of_ecwolf_opl_music.cpp -- authentic OPL2 (AdLib) music for openfpgaOS ECWolf.
//
//  Renders a game's native IMF (the "sqHack" OPL register script stored in
//  AUDIOT) through the DBOPL emulator and feeds the resulting 48 kHz stereo
//  PCM into the openfpgaOS mixer (of_audio_write).  This is the genuine DOS
//  AdLib sound, used for games that ship no Standard-MIDI replacement pack
//  (Blake Stone) -- SD_StartMusic falls through to here when the music pack
//  has no MIDI for the requested song.
//
//  The IMF format (matching id_sd.cpp's sqHack handling):
//    optional leading u16 = data length in bytes (0 => length is the lump size)
//    then pairs of: u16 (low byte = OPL reg, high byte = value),
//                   u16 delay in 700 Hz ticks until the next event.
//  Driven exactly like the engine's SDL_IMFMusicPlayer AL block at 700 Hz.
//
//  Chip ownership: this player keeps its OWN DBOPL::Chip, separate from the
//  one SD_PrepareAdLibSound uses for SFX -- the two never share register
//  state, so music and effects can run concurrently without stomping each
//  other.  The chip is built once (Setup() brute-force calibrates the
//  envelope tables and costs seconds on this CPU) and reused across songs.
//
//  Pump: on device a periodic timer callback (of_timer) tops up the audio
//  ring; on the desktop test of_timer is a no-op, so the SDL_mixer PostMix
//  hook (audio-thread rate, real SDL_mixer) drives the same render.  Both
//  paths throttle on of_audio_free() and a wall-clock budget so the
//  continuous dbopl render can never monopolise the CPU.
//

#include "of_ecwolf_opl_music.h"

#ifdef OF_ECWOLF_OPENFPGA

#include "wl_def.h"
#include "m_swap.h"   // LittleShort
#include "id_sd.h"
#include "dosbox/dbopl.h"

#include <stdlib.h>
#include <string.h>

extern "C" {
#include "of_audio.h"   // OF_AUDIO_RATE, of_audio_write/free/init
#include "of_timer.h"   // of_timer_set_callback, of_time_us
}

#ifdef OF_PC
#include <SDL_mixer.h>  // Mix_SetPostMix on the desktop test build
#endif

// The mixer is a fixed 48 kHz stereo s16 path (of_audio.h).  The IMF service
// ticks at 700 Hz, so each tick spans OF_AUDIO_RATE/700 output frames; the
// 48000/700 remainder is carried in a fractional accumulator so tick timing
// stays exact (matching SD_PrepareAdLibSound's 140 Hz accumulator approach).
#define OPL_MUSIC_RATE   OF_AUDIO_RATE
#define OPL_IMF_HZ       700

// On device the pump runs in the timer ISR, where of_time_us()'s ECALL would
// trigger a nested trap (see of_midi.c).  Read the monotonic timer through the
// direct service-table pointer instead; on PC fall back to the plain extern.
static inline uint32_t OPL_NowUs(void)
{
#ifdef OF_PC
	return of_time_us();
#else
	return OF_SVC->timer_get_us();
#endif
}

// OPL register helpers below mirror id_sd.cpp's alOut/SDL_AlSetChanInst but
// target THIS player's chip rather than the (OpenFPGA-gated) global oplChip.
namespace {

struct OPLMusic
{
	// Pointer, not a value: DBOPL::Chip's ctor runs InitTables() (pow/sin/log
	// FP math).  A global value member would run that at C++ static-init, before
	// the RISC-V FPU/runtime is up -> trap before main().  Construct lazily at
	// first play (FPU ready), mirroring the SFX chip.
	DBOPL::Chip  *chip;
	bool          chipReady;

	// The chip stores a pointer to the volume int (Chip::SetVolume keeps a
	// reference), so it must outlive every render -- hence a member, kept in
	// sync with the engine's MusicVolume each pump.
	int           volume;

	// IMF sequence (owned copy of the lump bytes).
	uint8_t      *data;
	int           dataLen;       // bytes of the owned buffer
	bool          loop;

	const word   *seqStart;      // first event word
	const word   *seqPtr;        // next event word
	int           seqLen;        // bytes remaining in this pass
	int           seqTotalLen;   // bytes in one full pass
	longword      timeCount;     // 700 Hz ticks elapsed
	longword      nextEventTime;  // tick at which seqPtr fires

	// 700 Hz tick <-> 48 kHz sample cadence.
	int           samplesLeftInTick;  // output frames still owed for cur tick
	uint32_t      sampleAcc;          // fractional-sample accumulator

	volatile bool active;
	bool          pumpInstalled;
};

OPLMusic M;

// Mirror of alOutMusic(): write one OPL register/value to the music chip.
inline void OPL_Write(byte reg, byte val)
{
	M.chip->SetVolume(M.volume);
	M.chip->WriteReg(reg, val);
}

// Mirror of id_sd.cpp's SDL_AlSetChanInst for the music chip.
void OPL_SetChanInst(const Instrument *inst, unsigned int chan)
{
	static const byte chanOps[OPL_CHANNELS] = {
		0, 1, 2, 8, 9, 0xA, 0x10, 0x11, 0x12
	};
	byte m = chanOps[chan];
	byte c = m + 3;
	OPL_Write(m + alChar,   inst->mChar);
	OPL_Write(m + alScale,  inst->mScale);
	OPL_Write(m + alAttack, inst->mAttack);
	OPL_Write(m + alSus,    inst->mSus);
	OPL_Write(m + alWave,   inst->mWave);
	OPL_Write(c + alChar,   inst->cChar);
	OPL_Write(c + alScale,  inst->cScale);
	OPL_Write(c + alAttack, inst->cAttack);
	OPL_Write(c + alSus,    inst->cSus);
	OPL_Write(c + alWave,   inst->cWave);
	OPL_Write(chan + alFreqL,   0);
	OPL_Write(chan + alFreqH,   0);
	OPL_Write(chan + alFeedCon, 0);
}

// Key-off release instrument used to silence every channel between songs,
// matching the ChannelRelease constant in SD_StartMusic.
const Instrument kChannelRelease = {
	0, 0,
	0x3F, 0x3F,
	0xFF, 0xFF,
	0xF, 0xF,
	0, 0,
	0,
	0, 0, {0, 0, 0}
};

void OPL_ResetChip()
{
	M.chip->SetVolume(M.volume);
	for (Bit32u r = 0x20; r <= 0xF5; r++)
		M.chip->WriteReg(r, 0);
	M.chip->WriteReg(1, 0x20);  // WSE = 1 (waveform select enable)
	for (int i = 0; i < OPL_CHANNELS; ++i)
		OPL_SetChanInst(&kChannelRelease, i);
}

// Advance the 700 Hz IMF service by one tick: dispatch every event due at the
// current time, then bump the clock.  Loops or stops at end of sequence.
// Returns false when a non-looping sequence has finished.
bool OPL_ServiceTick()
{
	do
	{
		if (M.nextEventTime > M.timeCount)
			break;
		// Each event = (reg/val word, delay word).
		const byte reg = *(const byte *)M.seqPtr;
		const byte val = *(((const byte *)M.seqPtr) + 1);
		M.nextEventTime = M.timeCount + LittleShort(*(M.seqPtr + 1));
		OPL_Write(reg, val);
		M.seqPtr += 2;
		M.seqLen -= 4;
	}
	while (M.seqLen > 0);

	M.timeCount++;

	if (M.seqLen <= 0)
	{
		if (!M.loop)
			return false;
		M.seqPtr = M.seqStart;
		M.seqLen = M.seqTotalLen;
		M.nextEventTime = 0;
		M.timeCount = 0;
	}
	return true;
}

// Produce and push up to `maxFrames` stereo frames into the mixer, honoring a
// wall-clock budget.  Shared by the timer (device) and PostMix (PC) drivers.
void OPL_Feed(int maxFrames, uint32_t budgetUs)
{
	if (!M.active)
		return;

	M.volume = MusicVolume;

	const uint32_t startUs = OPL_NowUs();
	int produced = 0;
	int16_t block[256 * 2];

	while (produced < maxFrames)
	{
		// Top up the current tick's owed frames; service a new tick when the
		// previous one is fully consumed.
		if (M.samplesLeftInTick <= 0)
		{
			if (!OPL_ServiceTick())
			{
				// Non-looping sequence ended.  Just go inactive here -- never
				// detach the pump from inside it (the device pump runs in an
				// ISR; tearing down the timer from within it is unsafe).  Full
				// teardown happens later via SD_MusicOff -> OPLMusic_Stop.
				M.active = false;
				return;
			}
			// Frames this tick spans (consumed incrementally below).
			M.sampleAcc += OPL_MUSIC_RATE;
			M.samplesLeftInTick = (int)(M.sampleAcc / OPL_IMF_HZ);
			M.sampleAcc -= (uint32_t)M.samplesLeftInTick * OPL_IMF_HZ;
		}

		int want = maxFrames - produced;
		if (want > 256) want = 256;
		if (want > M.samplesLeftInTick) want = M.samplesLeftInTick;
		if (want <= 0)
			break;

		// Render `want` frames of the current tick directly.
		Bit32s buf[256];
		int rendered = 0;
		while (rendered < want)
		{
			int chunk = want - rendered;
			if (chunk > 256) chunk = 256;
			M.chip->SetVolume(M.volume);
			M.chip->GenerateBlock2((Bitu)chunk, buf);
			for (int i = 0; i < chunk; ++i)
			{
				Bit32s s = buf[i] << 2;
				if (s > 32767) s = 32767;
				else if (s < -32768) s = -32768;
				block[(rendered + i) * 2]     = (int16_t)s;
				block[(rendered + i) * 2 + 1] = (int16_t)s;
			}
			rendered += chunk;
		}

		of_audio_write(block, want);
		produced += want;
		M.samplesLeftInTick -= want;

		if ((uint32_t)(OPL_NowUs() - startUs) > budgetUs)
			break;
	}
}

// --- Pump drivers ---------------------------------------------------------

void OPL_TimerPump(void)
{
	if (!M.active)
		return;
	int freeFrames = of_audio_free();
	if (freeFrames <= 0)
		return;
	// Cap per call so one pump can't render an unbounded backlog.
	if (freeFrames > OPL_MUSIC_RATE / 20)   // ~50 ms of audio
		freeFrames = OPL_MUSIC_RATE / 20;
	OPL_Feed(freeFrames, 4000 /* us */);
}

#ifdef OF_PC
void OPL_PostMix(void *udata, Uint8 *stream, int len)
{
	(void)udata; (void)stream; (void)len;
	// PostMix runs in SDL's audio thread on the desktop test; use it purely
	// as a tick to keep of_audio_write's ring fed (it drains to a separate
	// SDL device).  No wall-clock cap needed off-device.
	OPL_TimerPump();
}
#endif

void OPL_InstallPump()
{
	if (M.pumpInstalled)
		return;
#ifdef OF_PC
	Mix_SetPostMix(OPL_PostMix, NULL);
#else
	of_timer_set_callback(OPL_TimerPump, 50);
#endif
	M.pumpInstalled = true;
}

void OPL_RemovePump()
{
	if (!M.pumpInstalled)
		return;
#ifdef OF_PC
	Mix_SetPostMix(NULL, NULL);
#else
	of_timer_set_callback(NULL, 0);
#endif
	M.pumpInstalled = false;
}

void OPL_FreeData()
{
	if (M.data != NULL)
	{
		free(M.data);
		M.data = NULL;
	}
	M.dataLen = 0;
	M.seqStart = M.seqPtr = NULL;
	M.seqLen = M.seqTotalLen = 0;
}

} // namespace

bool OPLMusic_Start(const uint8_t *imf, int len, bool loop)
{
	if (imf == NULL || len <= 4)
		return false;

	OPLMusic_Stop();

	// Own a copy of the IMF bytes; the caller's lump buffer is transient.
	uint8_t *copy = (uint8_t *)malloc(len);
	if (copy == NULL)
		return false;
	memcpy(copy, imf, len);

	M.data    = copy;
	M.dataLen = len;
	M.loop    = loop;
	M.volume  = MusicVolume;

	// Parse the optional leading length word, exactly like SD_StartMusic.
	const word *seq = reinterpret_cast<const word *>(copy);
	if (*seq == 0)
		M.seqLen = M.seqTotalLen = len;
	else
		M.seqLen = M.seqTotalLen = LittleShort(*seq++);
	M.seqStart = seq;
	M.seqPtr   = seq;
	M.timeCount     = 0;
	M.nextEventTime = 0;
	M.samplesLeftInTick = 0;
	M.sampleAcc         = 0;

	// Build/calibrate the chip once; reset register + channel state per song.
	// Construct the chip HERE (runtime, FPU up), never at static-init -- its
	// ctor runs InitTables()' FP math, which traps pre-main on the RISC-V.
	if (M.chip == NULL)
		M.chip = new DBOPL::Chip();
	if (!M.chipReady)
	{
		M.chip->Setup(OPL_MUSIC_RATE);
		M.chipReady = true;
	}
	OPL_ResetChip();

	of_audio_init();
	M.active = true;
	OPL_InstallPump();
	return true;
}

void OPLMusic_Stop(void)
{
	M.active = false;
	OPL_RemovePump();
	if (M.chipReady)
	{
		// Key everything off so no operator tail bleeds into the next song.
		OPL_ResetChip();
	}
	OPL_FreeData();
	M.timeCount = M.nextEventTime = 0;
	M.samplesLeftInTick = 0;
	M.sampleAcc = 0;
}

bool OPLMusic_Playing(void)
{
	return M.active;
}

#endif // OF_ECWOLF_OPENFPGA
