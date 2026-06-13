//
//  of_ecwolf_opl_music.cpp -- authentic OPL2 (AdLib) music for openfpgaOS ECWolf.
//
//  Renders a game's native IMF (the "sqHack" OPL register script stored in
//  AUDIOT) through the DBOPL emulator and feeds the resulting 48 kHz stereo
//  PCM into the openfpgaOS mixer (of_audio_write).  This is the genuine DOS
//  AdLib sound.  SD_StartMusic comes here first when the pre-rendered cache
//  (muscache.ofx) holds the song, and falls through to here when the music
//  pack has no MIDI for the requested song.
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
//  Async pump (device): the heavy DBOPL render / SD cache read runs on the
//  main loop (the producer), writing PCM ahead into a multi-second RAM ring;
//  a ~100 Hz timer ISR (the consumer) copies that PCM into the OS audio ring.
//  Because the ISR keeps feeding the audio ring even while the main loop is
//  blocked (level loads, screen fades, menu transitions), the music no longer
//  stutters across those stalls.  The ISR only COPIES -- it never synthesises
//  and never touches the SD card -- so it stays inside a tiny bounded budget,
//  like the SDK MIDI pump.  The timer's single callback slot is free here:
//  ECWolf only reaches this OPL player when no SDK-MIDI song is playing.
//  On the desktop test (OF_PC) there is no ISR: the SDL_mixer PostMix hook
//  drives the same producer straight into of_audio_write.
//

#include "of_ecwolf_opl_music.h"

#ifdef OF_ECWOLF_OPENFPGA

#include "wl_def.h"
#include "m_swap.h"   // LittleShort
#include "id_sd.h"
#include "dosbox/dbopl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "of_audio.h"   // OF_AUDIO_RATE, of_audio_write/free/init
#include "of_timer.h"   // of_time_us, of_timer_set_callback
}

#ifndef OF_PC
#include "of_fastram.h"  // OF_FASTDATA -- pin ISR-touched cursors to BRAM
#endif

#ifdef OF_PC
#include <SDL_mixer.h>  // Mix_SetPostMix on the desktop test build
#endif

// The mixer is a fixed 48 kHz stereo s16 path (of_audio.h).  The IMF service
// ticks at 700 Hz, so each tick spans OF_AUDIO_RATE/700 output frames; the
// 48000/700 remainder is carried in a fractional accumulator so tick timing
// stays exact (matching SD_PrepareAdLibSound's 140 Hz accumulator approach).
#define OPL_MUSIC_RATE   OF_AUDIO_RATE
#define OPL_IMF_HZ       700

// Monotonic time; the direct service-table read is cheaper than the ECALL
// path and this runs once per render block.
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

	// Pre-rendered music cache stream (muscache.ofx hit): PCM is read from
	// the pack file instead of synthesized, leaving the DBOPL state unused.
	bool          streamMode;
	uint32_t      streamBase;     // first PCM byte of this song's entry
	uint32_t      streamSamples;  // total mono samples in the entry
	uint32_t      streamPos;      // mono samples consumed this pass
};

OPLMusic M;

// Optional pre-rendered cache: scripts/muscache.sh renders every IMF song
// at the OPL2's native 49716 Hz on the host, sinc-band-limits it to 48 kHz,
// and writes the standalone "muscache.ofx" pack (its own data slot, id 27),
// keyed by FNV-1a hash of the IMF event bytes.  A miss (no pack, modded or
// changed song) falls back to the on-device DBOPL render below, so the
// cache can never be stale or required.  Only the small hash index is
// resident; each song streams its PCM straight from the file.
FILE     *musCacheFile;
bool      musCacheProbed;
uint8_t  *musCacheIndex;     // count * 16-byte entries
uint32_t  musCacheCount;
uint32_t  musCacheMakeupQ16; // residual RMS gain from the pack header (0 = 1.0)

uint64_t MusCache_Hash(const uint8_t *data, uint32_t n)
{
	uint64_t h = 14695981039346656037ull;
	for(uint32_t i = 0; i < n; ++i)
	{
		h ^= data[i];
		h *= 1099511628211ull;
	}
	return h;
}

void MusCache_Probe()
{
	if (musCacheProbed)
		return;
	musCacheProbed = true;

	FILE *f = fopen("muscache.ofx", "rb");
	if (f == NULL)
		return;

	uint8_t header[16];
	if (fread(header, 1, 16, f) != 16 || memcmp(header, "OFM1", 4) != 0)
	{
		fclose(f);
		return;
	}
	const uint32_t count = ReadLittleLong(header + 4);
	const uint32_t rate = ReadLittleLong(header + 8);
	uint32_t makeupQ16 = ReadLittleLong(header + 12);
	if (makeupQ16 == 0)
		makeupQ16 = 65536;  // older packs: no makeup field
	// The PCM feeds of_audio_write directly, so a pack at any other rate
	// would play at the wrong pitch -- refuse it.  The tool clamps makeup
	// to 0.25..4.0, so a value outside that range is header corruption.
	if (count == 0 || count > 100000 || rate != (uint32_t)OF_AUDIO_RATE ||
	    makeupQ16 < 16384 || makeupQ16 > 262144)
	{
		fclose(f);
		return;
	}

	uint8_t *index = (uint8_t *)malloc(count * 16);
	if (index == NULL)
	{
		fclose(f);
		return;
	}
	if (fread(index, 1, count * 16, f) != count * 16)
	{
		free(index);
		fclose(f);
		return;
	}

	// Reject a truncated pack (interrupted SD copy: header+index intact,
	// PCM cut short) so every song falls back to DBOPL instead of dying
	// mid-stream on a short read.
	long fileSize = -1;
	if (fseek(f, 0, SEEK_END) == 0)
		fileSize = ftell(f);
	for (uint32_t i = 0; i < count; i++)
	{
		const uint8_t *e = index + i * 16;
		const uint64_t end = (uint64_t)ReadLittleLong(e + 8) +
			(uint64_t)ReadLittleLong(e + 12) * 2;
		if (fileSize < 0 || end > (uint64_t)fileSize)
		{
			free(index);
			fclose(f);
			return;
		}
	}

	musCacheFile = f;
	musCacheIndex = index;
	musCacheCount = count;
	musCacheMakeupQ16 = makeupQ16;
	printf("OpenFPGA: music cache indexed (%u songs).\n", (unsigned)count);
}

// Look the song hash up in the resident index; false = miss.
bool MusCache_Lookup(uint64_t hash, uint32_t *base, uint32_t *samples)
{
	for (uint32_t i = 0; i < musCacheCount; i++)
	{
		const uint8_t *e = musCacheIndex + i * 16;
		const uint64_t ehash = (uint64_t)ReadLittleLong(e) |
			((uint64_t)ReadLittleLong(e + 4) << 32);
		if (ehash != hash)
			continue;
		*base = ReadLittleLong(e + 8);
		*samples = ReadLittleLong(e + 12);
		return *samples != 0;
	}
	return false;
}

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

// === Async RAM ring + timer-ISR drain ===================================
//
// Single-producer (main loop) / single-consumer (timer ISR) PCM ring, so no
// locks.  The producer renders/reads music PCM ahead into this ring; the ISR
// copies it into the OS audio ring even while the main loop is stalled.  PCM
// is stored interleaved-stereo so the ISR hands of_audio_write a pointer
// straight into the ring (splitting only at the wrap) -- it performs NO data
// stores to SDRAM, sidestepping the ISR-store-vs-GPU race entirely.  Only the
// cursors live in BRAM: the ISR writes pcmTail, and an ISR store to SDRAM
// would race with GPU/bridge bus traffic (see of_smp_voice.c).
#ifndef OF_PC
// 1<<18 frames = ~5.46 s at 48 kHz (1 MB SDRAM, interleaved stereo).  Deep
// enough to outlast any level load / transition that stalls the producer;
// power of two so the ring index is a cheap mask.
#define PCM_RING_FRAMES  (1u << 18)
#define PCM_RING_MASK    (PCM_RING_FRAMES - 1u)

int16_t                      *pcmRing;  // SDRAM, interleaved stereo, malloc'd once
OF_FASTDATA volatile uint32_t pcmHead;  // producer frame cursor (main loop)
OF_FASTDATA volatile uint32_t pcmTail;  // consumer frame cursor (timer ISR)
OF_FASTDATA volatile int      drainOn;  // gates the ISR off during teardown
#endif

// Mono frames the producer may emit right now: ring room on device, OS audio
// ring room on the desktop test.
inline int OPL_SinkFree()
{
#ifndef OF_PC
	return (int)(PCM_RING_FRAMES - (pcmHead - pcmTail));
#else
	return of_audio_free();
#endif
}

// Emit `n` mono frames (gain already applied), duplicated to stereo.  Device:
// store into the RAM ring for the ISR to drain.  Desktop: push straight to
// of_audio_write (the PostMix hook is the consumer there).
void OPL_Emit(const int16_t *mono, int n)
{
#ifndef OF_PC
	uint32_t head = pcmHead;
	for (int i = 0; i < n; ++i)
	{
		const uint32_t f = (head + (uint32_t)i) & PCM_RING_MASK;
		const int16_t  s = mono[i];
		pcmRing[f * 2]     = s;
		pcmRing[f * 2 + 1] = s;
	}
	__asm__ volatile("" ::: "memory");   // publish the data before the cursor
	pcmHead = head + (uint32_t)n;
#else
	int16_t block[256 * 2];
	int done = 0;
	while (done < n)
	{
		int c = n - done;
		if (c > 256) c = 256;
		for (int i = 0; i < c; ++i)
		{
			block[i * 2]     = mono[done + i];
			block[i * 2 + 1] = mono[done + i];
		}
		of_audio_write(block, c);
		done += c;
	}
#endif
}

#ifndef OF_PC
// Timer ISR (~100 Hz): copy buffered stereo PCM into the OS audio ring until
// it is full or the RAM ring runs dry.  No synthesis, no file I/O, no ECALLs
// -- bounded work safe for interrupt context (cf. of_midi_pump).
void OPL_DrainISR(void)
{
	if (!drainOn || pcmRing == NULL)
		return;

	int freeFrames = of_audio_free();
	if (freeFrames <= 0)
		return;
	// Bound per-tick work; a few ticks still refill a fully drained OS ring
	// (~341 ms) after a stall.
	if (freeFrames > OPL_MUSIC_RATE / 8)
		freeFrames = OPL_MUSIC_RATE / 8;

	uint32_t tail = pcmTail;
	while (freeFrames > 0)
	{
		uint32_t avail = pcmHead - tail;
		if (avail == 0)
			break;                       // producer fell behind
		uint32_t idx = tail & PCM_RING_MASK;
		int run = (int)(PCM_RING_FRAMES - idx);   // contiguous frames to the wrap
		if (run > freeFrames)      run = freeFrames;
		if ((uint32_t)run > avail) run = (int)avail;
		int wrote = of_audio_write(&pcmRing[idx * 2], run);
		if (wrote <= 0)
			break;
		tail += (uint32_t)wrote;
		freeFrames -= wrote;
		if (wrote < run)
			break;                       // OS ring filled mid-write
	}
	__asm__ volatile("" ::: "memory");
	pcmTail = tail;
}
#endif

// Stream pre-rendered PCM from the music cache: read mono samples, apply the
// volume/makeup gain, and emit them.  The cache-hit twin of the DBOPL render
// below.  The pack is rendered at MAX_VOLUME, so MULTIPLY_VOLUME(MusicVolume)
// -- the same per-sample curve the emulators apply -- is baked into a Q15 gain
// here, scaled by the pack's RMS makeup (the DBOPL fallback saturates, so the
// clean pack PCM sits below its loudness); the per-sample clamp clips only
// where the fallback itself would.
void OPL_FeedStream(int maxFrames, uint32_t budgetUs)
{
	M.volume = MusicVolume;
	// makeup can exceed 1.0, so gain can exceed 32768 -- the sample multiply
	// below needs an int64 intermediate and an explicit s16 clamp.
	const int32_t gain = (int32_t)(MULTIPLY_VOLUME(M.volume) *
		((float)musCacheMakeupQ16 / 65536.0f) * 32768.0f + 0.5f);

	const uint32_t startUs = OPL_NowUs();
	int produced = 0;
	int16_t mono[256];

	while (produced < maxFrames)
	{
		uint32_t remaining = M.streamSamples - M.streamPos;
		if (remaining == 0)
		{
			if (!M.loop)
			{
				M.active = false;
				return;
			}
			if (fseek(musCacheFile, (long)M.streamBase, SEEK_SET) != 0)
			{
				printf("OpenFPGA: music cache seek failed; music stopped.\n");
				M.active = false;
				return;
			}
			M.streamPos = 0;
			remaining = M.streamSamples;
		}

		int want = maxFrames - produced;
		if (want > 256) want = 256;
		if ((uint32_t)want > remaining) want = (int)remaining;

		if (fread(mono, 2, want, musCacheFile) != (size_t)want)
		{
			printf("OpenFPGA: music cache read failed; music stopped.\n");
			M.active = false;
			return;
		}
		for (int i = 0; i < want; ++i)
		{
			int32_t v = (int32_t)(((int64_t)(int16_t)LittleShort(mono[i]) * gain) >> 15);
			if (v > 32767) v = 32767;
			else if (v < -32768) v = -32768;
			mono[i] = (int16_t)v;
		}
		OPL_Emit(mono, want);
		produced += want;
		M.streamPos += (uint32_t)want;

		if ((uint32_t)(OPL_NowUs() - startUs) > budgetUs)
			break;
	}
}

// Produce up to `maxFrames` mono frames from the DBOPL render and emit them,
// honoring a wall-clock budget.  Stream-mode songs defer to OPL_FeedStream.
void OPL_Feed(int maxFrames, uint32_t budgetUs)
{
	if (!M.active)
		return;

	if (M.streamMode)
	{
		OPL_FeedStream(maxFrames, budgetUs);
		return;
	}

	M.volume = MusicVolume;

	const uint32_t startUs = OPL_NowUs();
	int produced = 0;
	int16_t mono[256];

	while (produced < maxFrames)
	{
		// Top up the current tick's owed frames; service a new tick when the
		// previous one is fully consumed.
		if (M.samplesLeftInTick <= 0)
		{
			if (!OPL_ServiceTick())
			{
				// Non-looping sequence ended.  Just go inactive; full teardown
				// happens later via SD_MusicOff -> OPLMusic_Stop.
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

		// Render `want` (<=256) frames of the current tick in one call.
		Bit32s buf[256];
		M.chip->SetVolume(M.volume);
		M.chip->GenerateBlock2((Bitu)want, buf);
		for (int i = 0; i < want; ++i)
		{
			Bit32s s = buf[i] << 2;
			if (s > 32767) s = 32767;
			else if (s < -32768) s = -32768;
			mono[i] = (int16_t)s;
		}
		OPL_Emit(mono, want);
		produced += want;
		M.samplesLeftInTick -= want;

		if ((uint32_t)(OPL_NowUs() - startUs) > budgetUs)
			break;
	}
}

// --- Pump drivers ---------------------------------------------------------

#ifndef OF_PC
// Main-loop producer: render/read ahead into the RAM ring so the ISR always
// has PCM to drain.  Fills whatever room the ring has, bounded by a wall-clock
// budget so a single call never stalls the frame.
void OPL_Produce(void)
{
	if (!M.active || pcmRing == NULL)
		return;
	int room = OPL_SinkFree();
	if (room <= 0)
		return;
	OPL_Feed(room, 8000 /* us */);
}

// Fill the ring before the ISR starts draining so it can't underrun at song
// start; cap the wall-clock so a heavy DBOPL prefill can't hang the very
// transition that started the song.
void OPL_Prefill(void)
{
	const uint32_t startUs = OPL_NowUs();
	while (M.active && OPL_SinkFree() > 256)
	{
		OPL_Feed(OPL_SinkFree(), 4000 /* us */);
		if ((uint32_t)(OPL_NowUs() - startUs) > 250000)   // 250 ms cap
			break;
	}
}
#else
void OPL_Pump(void)
{
	if (!M.active)
		return;
	int freeFrames = of_audio_free();
	if (freeFrames <= 0)
		return;
	if (freeFrames > OPL_MUSIC_RATE / 3)
		freeFrames = OPL_MUSIC_RATE / 3;
	OPL_Feed(freeFrames, 12000 /* us */);
}

void OPL_PostMix(void *udata, Uint8 *stream, int len)
{
	(void)udata; (void)stream; (void)len;
	// PostMix runs in SDL's audio thread on the desktop test; use it purely
	// as a tick to keep of_audio_write's ring fed (it drains to a separate
	// SDL device).  No wall-clock cap needed off-device.
	OPL_Pump();
}
#endif

// Device: drive the drain ISR via the periodic timer.  Desktop: hook
// SDL_mixer's PostMix.
void OPL_InstallPump()
{
	if (M.pumpInstalled)
		return;
#ifdef OF_PC
	Mix_SetPostMix(OPL_PostMix, NULL);
#else
	drainOn = 1;
	of_timer_set_callback(OPL_DrainISR, 100);
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
	// Detach the ISR BEFORE clearing state so it can't run mid-teardown.
	of_timer_set_callback(NULL, 0);
	drainOn = 0;
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

// Common tail of both start paths: ready the RAM ring (device), reset the OS
// audio ring, mark the song active, prefill, and start the pump.
bool OPL_BeginPlayback(void)
{
#ifndef OF_PC
	if (pcmRing == NULL)
	{
		// Allocated once and reused across songs (freed only at process exit).
		pcmRing = (int16_t *)malloc((size_t)PCM_RING_FRAMES * 2 * sizeof(int16_t));
		if (pcmRing == NULL)
			return false;
	}
	pcmHead = pcmTail = 0;
#endif
	of_audio_init();
	M.active = true;
#ifndef OF_PC
	OPL_Prefill();
#endif
	OPL_InstallPump();
	return true;
}

} // namespace

bool OPLMusic_CacheHit(const uint8_t *imf, int len)
{
	if (imf == NULL || len <= 4)
		return false;

	MusCache_Probe();
	if (musCacheIndex == NULL)
		return false;

	// Same event-stream parse as OPLMusic_Start's hash block below.
	const uint8_t *hseq = imf;
	int hashBytes;
	if (ReadLittleShort(imf) == 0)
		hashBytes = len;
	else
	{
		hashBytes = ReadLittleShort(imf);
		hseq += 2;
		if (hashBytes > len - 2)
			hashBytes = len - 2;
	}
	hashBytes &= ~3;

	uint32_t base, samples;
	return hashBytes > 0 &&
		MusCache_Lookup(MusCache_Hash(hseq, (uint32_t)hashBytes), &base, &samples);
}

bool OPLMusic_Start(const uint8_t *imf, int len, bool loop)
{
	if (imf == NULL || len <= 4)
		return false;

	OPLMusic_Stop();

	// Pre-rendered cache hit?  Locate the event stream in the caller's
	// buffer (same parse as the miss path below) and look its hash up; on a
	// hit the song streams from the pack and the emulator never runs.
	MusCache_Probe();
	if (musCacheIndex != NULL)
	{
		const uint8_t *hseq = imf;
		int hashBytes;
		if (ReadLittleShort(imf) == 0)
			hashBytes = len;
		else
		{
			hashBytes = ReadLittleShort(imf);
			hseq += 2;
			if (hashBytes > len - 2)
				hashBytes = len - 2;
		}
		hashBytes &= ~3;

		uint32_t base, samples;
		if (hashBytes > 0 &&
			MusCache_Lookup(MusCache_Hash(hseq, (uint32_t)hashBytes), &base, &samples) &&
			fseek(musCacheFile, (long)base, SEEK_SET) == 0)
		{
			M.loop    = loop;
			M.volume  = MusicVolume;
			M.streamMode    = true;
			M.streamBase    = base;
			M.streamSamples = samples;
			M.streamPos     = 0;

			return OPL_BeginPlayback();
		}
	}

	// Own a copy of the IMF bytes; the caller's lump buffer is transient.
	uint8_t *copy = (uint8_t *)malloc(len);
	if (copy == NULL)
		return false;
	memcpy(copy, imf, len);

	M.data    = copy;
	M.dataLen = len;
	M.loop    = loop;
	M.volume  = MusicVolume;

	// Parse the optional leading length word, exactly like SD_StartMusic,
	// then clamp to whole events inside the copy: a corrupt length word
	// would walk the service loop past the buffer.
	const word *seq = reinterpret_cast<const word *>(copy);
	int seqBytes;
	if (*seq == 0)
		seqBytes = len;
	else
	{
		seqBytes = LittleShort(*seq++);
		if (seqBytes > len - 2)
			seqBytes = len - 2;
	}
	seqBytes &= ~3;
	if (seqBytes <= 0)
	{
		OPL_FreeData();
		return false;
	}
	M.seqLen = M.seqTotalLen = seqBytes;
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

	return OPL_BeginPlayback();
}

void OPLMusic_Stop(void)
{
	// Detach the ISR first (inside OPL_RemovePump) so it can't drain a ring
	// we're about to reset, then tear the rest down.
	OPL_RemovePump();
	M.active = false;
#ifndef OF_PC
	pcmHead = pcmTail = 0;   // discard the previous song's buffered PCM
#endif
	if (M.chipReady)
	{
		// Key everything off so no operator tail bleeds into the next song.
		OPL_ResetChip();
	}
	OPL_FreeData();
	M.timeCount = M.nextEventTime = 0;
	M.samplesLeftInTick = 0;
	M.sampleAcc = 0;
	// The cache file and index stay open across songs; only the stream
	// position is per-song state.
	M.streamMode = false;
	M.streamBase = M.streamSamples = M.streamPos = 0;
}

void OPLMusic_Pump(void)
{
#ifndef OF_PC
	OPL_Produce();
#endif
}

bool OPLMusic_Playing(void)
{
	return M.active;
}

#endif // OF_ECWOLF_OPENFPGA
