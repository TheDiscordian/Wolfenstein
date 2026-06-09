/*
** muscache -- pre-render IMF (AdLib) music at native OPL quality
**
** Renders every IMF music chunk found in AUDIOT files through the Nuked
** OPL3 emulator (cycle-accurate YMF262 die emulation, run in OPL2 mode) at
** the chip's native rate (49716 Hz), band-limits it down to the Pocket
** mixer's 48000 Hz with a windowed-sinc filter, and writes a single
** hash-keyed pack file.  DBOPL renders each song in parallel purely for
** LOUDNESS CALIBRATION: the on-device cache-miss fallback synthesizes with
** DBOPL (of_ecwolf_opl_music.cpp), so the pack is gain-matched to it and
** hits/misses sound identical in level.  The pack ships as the standalone
** "muscache.ofx" file (data slot 27); the port looks songs up by FNV-1a
** hash of the IMF event bytes and falls back to on-device DBOPL synthesis
** on a miss, so the cache is always optional and never stale.
**
** Each song is rendered for exactly one sequence pass, ending at the tick
** in which the final event fires -- the same point at which the runtime
** service loop rewinds -- so the PCM loops where the IMF loops.
**
** Usage: muscache <out.ofx> <startmusic> <AUDIOHED> <AUDIOT> [<AUDIOHED> <AUDIOT> ...]
**   <startmusic> = first music chunk index in AUDIOT (300 for Blake Stone)
**
** Pack format (all little-endian):
**   u32 magic "OFM1"   u32 count   u32 rate   u32 makeupQ16
**   count * { u64 hash; u32 byteOffset; u32 sampleCount; }
**   16-bit signed mono PCM blobs
**
** The PCM is stored clean (peak-gain-matched, no clipping); makeupQ16 is the
** residual RMS gain (Q16, 0 = 1.0) the runtime applies in its volume multiply
** so cache hits match the DBOPL fallback's loudness, clipping only where the
** fallback itself saturates.
*/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

#include "dbopl.h"
extern "C" {
#include "opl3.h"
}

static const unsigned OPL_NATIVE_RATE = 49716;  // YM3812 sample rate
static const unsigned OUT_RATE        = 48000;  // Pocket mixer native rate
static const unsigned IMF_HZ          = 700;    // IMF music service rate
static const uint64_t MAX_TICKS       = 700ull * 60 * 20;  // 20 min degenerate-data cap

/* OPL register bases (match port/id_sd.h) */
#define alChar    0x20
#define alScale   0x40
#define alAttack  0x60
#define alSus     0x80
#define alWave    0xE0
#define alFreqL   0xA0
#define alFreqH   0xB0
#define alFeedCon 0xC0

/* Music channel operator offsets (match of_ecwolf_opl_music.cpp). */
static const uint8_t chanOps[9] = { 0, 1, 2, 8, 9, 0xA, 0x10, 0x11, 0x12 };

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t fnv1a64(const uint8_t *data, size_t n)
{
	uint64_t h = 14695981039346656037ull;
	for(size_t i = 0; i < n; ++i)
	{
		h ^= data[i];
		h *= 1099511628211ull;
	}
	return h;
}

static std::vector<uint8_t> readFile(const char *path)
{
	std::vector<uint8_t> out;
	FILE *f = fopen(path, "rb");
	if(!f)
	{
		fprintf(stderr, "muscache: cannot open %s\n", path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize((size_t)len);
	if(len > 0 && fread(out.data(), 1, (size_t)len, f) != (size_t)len)
	{
		fprintf(stderr, "muscache: short read on %s\n", path);
		exit(1);
	}
	fclose(f);
	return out;
}

/* Locate the IMF event stream inside a music chunk, exactly like
 * OPLMusic_Start(): optional leading u16 length word (0 => the whole chunk
 * is data), clamped to whole 4-byte events.  Returns false if empty. */
static bool parseSeq(const uint8_t *raw, uint32_t size,
                     const uint8_t **seq, uint32_t *seqBytes)
{
	if(size <= 4)
		return false;
	const uint8_t *p = raw;
	int bytes;
	if(rd16(p) == 0)
		bytes = (int)size;
	else
	{
		bytes = rd16(p);
		p += 2;
		if(bytes > (int)size - 2)
			bytes = (int)size - 2;
	}
	bytes &= ~3;
	if(bytes <= 0)
		return false;
	*seq = p;
	*seqBytes = (uint32_t)bytes;
	return true;
}

/* Render one IMF sequence pass through DBOPL at the chip's native rate.
 * Mirrors the port's OPL_ResetChip()/OPL_ServiceTick()/OPL_Feed() exactly
 * (the on-device cache-miss fallback); used only to calibrate loudness. */
static std::vector<int16_t> renderDbopl(DBOPL::Chip &chip,
                                        const uint8_t *seq, uint32_t seqBytes)
{
	chip.SetVolume(MAX_VOLUME);
	for(Bit32u r = 0x20; r <= 0xF5; r++)
		chip.WriteReg(r, 0);
	chip.WriteReg(1, 0x20);  // Set WSE=1

	/* Channel release on all 9 music channels (mirror OPL_ResetChip). */
	for(int ch = 0; ch < 9; ++ch)
	{
		const uint8_t m = chanOps[ch], c = m + 3;
		chip.WriteReg(m + alChar,   0);
		chip.WriteReg(m + alScale,  0x3F);
		chip.WriteReg(m + alAttack, 0xFF);
		chip.WriteReg(m + alSus,    0x0F);
		chip.WriteReg(m + alWave,   0);
		chip.WriteReg(c + alChar,   0);
		chip.WriteReg(c + alScale,  0x3F);
		chip.WriteReg(c + alAttack, 0xFF);
		chip.WriteReg(c + alSus,    0x0F);
		chip.WriteReg(c + alWave,   0);
		chip.WriteReg(ch + alFreqL,   0);
		chip.WriteReg(ch + alFreqH,   0);
		chip.WriteReg(ch + alFeedCon, 0);
	}

	const uint8_t *p = seq;
	int left = (int)seqBytes;
	uint64_t timeCount = 0, nextEventTime = 0;
	uint32_t sampleAcc = 0;
	std::vector<int16_t> pcm;
	pcm.reserve((size_t)(seqBytes / 4) * (OPL_NATIVE_RATE / IMF_HZ));

	Bit32s block32[512];
	for(;;)
	{
		do
		{
			if(nextEventTime > timeCount)
				break;
			nextEventTime = timeCount + rd16(p + 2);
			chip.WriteReg(p[0], p[1]);
			p += 4;
			left -= 4;
		}
		while(left > 0);

		sampleAcc += OPL_NATIVE_RATE;
		uint32_t tickSamples = sampleAcc / IMF_HZ;
		sampleAcc -= tickSamples * IMF_HZ;
		while(tickSamples > 0)
		{
			const uint32_t n = tickSamples > 512 ? 512 : tickSamples;
			chip.GenerateBlock2(n, block32);
			for(uint32_t i = 0; i < n; ++i)
			{
				/* x4 to match OPL_Feed's loudness. */
				Bit32s s = block32[i] << 2;
				if(s > 32767) s = 32767;
				else if(s < -32768) s = -32768;
				pcm.push_back((int16_t)s);
			}
			tickSamples -= n;
		}

		timeCount++;
		if(left <= 0)
			break;
		if(timeCount > MAX_TICKS)
			return std::vector<int16_t>();
	}
	return pcm;
}

/* Render the same sequence through Nuked OPL3 (OPL2 compatibility mode --
 * register 0x105 is never written, so the chip stays "old").  Raw
 * chip-level output; gain calibration against DBOPL happens in main(). */
static std::vector<int16_t> renderNuked(const uint8_t *seq, uint32_t seqBytes)
{
	static opl3_chip chip;
	OPL3_Reset(&chip, OPL_NATIVE_RATE);

	for(uint16_t r = 0x20; r <= 0xF5; r++)
		OPL3_WriteReg(&chip, r, 0);
	OPL3_WriteReg(&chip, 0x01, 0x20);  // WSE (OPL2 etiquette; harmless here)

	for(int ch = 0; ch < 9; ++ch)
	{
		const uint16_t m = chanOps[ch], c = m + 3;
		OPL3_WriteReg(&chip, m + alChar,   0);
		OPL3_WriteReg(&chip, m + alScale,  0x3F);
		OPL3_WriteReg(&chip, m + alAttack, 0xFF);
		OPL3_WriteReg(&chip, m + alSus,    0x0F);
		OPL3_WriteReg(&chip, m + alWave,   0);
		OPL3_WriteReg(&chip, c + alChar,   0);
		OPL3_WriteReg(&chip, c + alScale,  0x3F);
		OPL3_WriteReg(&chip, c + alAttack, 0xFF);
		OPL3_WriteReg(&chip, c + alSus,    0x0F);
		OPL3_WriteReg(&chip, c + alWave,   0);
		OPL3_WriteReg(&chip, ch + alFreqL,   0);
		OPL3_WriteReg(&chip, ch + alFreqH,   0);
		OPL3_WriteReg(&chip, ch + alFeedCon, 0);
	}

	const uint8_t *p = seq;
	int left = (int)seqBytes;
	uint64_t timeCount = 0, nextEventTime = 0;
	uint32_t sampleAcc = 0;
	std::vector<int16_t> pcm;
	pcm.reserve((size_t)(seqBytes / 4) * (OPL_NATIVE_RATE / IMF_HZ));

	for(;;)
	{
		do
		{
			if(nextEventTime > timeCount)
				break;
			nextEventTime = timeCount + rd16(p + 2);
			OPL3_WriteReg(&chip, p[0], p[1]);
			p += 4;
			left -= 4;
		}
		while(left > 0);

		sampleAcc += OPL_NATIVE_RATE;
		uint32_t tickSamples = sampleAcc / IMF_HZ;
		sampleAcc -= tickSamples * IMF_HZ;
		while(tickSamples-- > 0)
		{
			int16_t frame[2];
			OPL3_GenerateResampled(&chip, frame);
			pcm.push_back(frame[0]);   // mono: OPL2 mode mirrors L/R
		}

		timeCount++;
		if(left <= 0)
			break;
		if(timeCount > MAX_TICKS)
			return std::vector<int16_t>();
	}
	return pcm;
}

/* Band-limited 49716 -> 48000 conversion: 33-tap windowed-sinc evaluated
 * per output sample (offline, so no polyphase table needed). */
static std::vector<int16_t> resample(const std::vector<int16_t> &in)
{
	const double ratio = (double)OPL_NATIVE_RATE / (double)OUT_RATE;
	/* Downsampling: cut off just below the OUTPUT Nyquist, expressed as a
	 * fraction of the input rate. */
	const double cutoff = 0.5 / ratio * 0.95;
	const int taps = 16;  /* one-sided; 33-tap kernel */

	const size_t outCount = (size_t)((double)in.size() / ratio);
	std::vector<int16_t> out;
	out.reserve(outCount);

	for(size_t n = 0; n < outCount; ++n)
	{
		const double center = (double)n * ratio;
		const long ci = (long)floor(center + 0.5);
		double acc = 0.0, norm = 0.0;
		for(long k = ci - taps; k <= ci + taps; ++k)
		{
			const double x = (double)k - center;
			/* sinc low-pass */
			double s = (x == 0.0) ? 2.0 * cutoff
				: sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
			/* Blackman window */
			const double w = 0.42
				+ 0.5 * cos(M_PI * x / (double)taps)
				+ 0.08 * cos(2.0 * M_PI * x / (double)taps);
			s *= (fabs(x) <= (double)taps) ? w : 0.0;
			const double v = (k >= 0 && k < (long)in.size()) ? (double)in[k] : 0.0;
			acc += v * s;
			norm += s;
		}
		/* Normalize by the kernel sum for exact unity passband gain. */
		double y = acc / (norm > 1e-9 ? norm : 1.0);
		if(y > 32767.0) y = 32767.0;
		else if(y < -32768.0) y = -32768.0;
		out.push_back((int16_t)lrint(y));
	}
	return out;
}

/* Model the AdLib card's analog output stage (same default as sfxcache, so
 * music and SFX share one tonal character).  2nd-order Butterworth; default
 * 12 kHz, MUS_LPF_HZ overrides (0 disables for the chip-raw purist render). */
static void lowpass(std::vector<int16_t> &pcm, double hz)
{
	if(hz <= 0.0 || hz >= OUT_RATE / 2.0)
		return;
	const double w = tan(M_PI * hz / OUT_RATE);
	const double k = 1.0 / (1.0 + sqrt(2.0) * w + w * w);
	const double b0 = w * w * k, b1 = 2.0 * b0, b2 = b0;
	const double a1 = 2.0 * (w * w - 1.0) * k;
	const double a2 = (1.0 - sqrt(2.0) * w + w * w) * k;

	double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	for(size_t i = 0; i < pcm.size(); ++i)
	{
		const double x = pcm[i];
		double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1; x1 = x;
		y2 = y1; y1 = y;
		if(y > 32767.0) y = 32767.0;
		else if(y < -32768.0) y = -32768.0;
		pcm[i] = (int16_t)lrint(y);
	}
}

struct Entry
{
	std::vector<int16_t> pcm;
};

int main(int argc, char **argv)
{
	if(argc < 5 || ((argc - 3) % 2) != 0)
	{
		fprintf(stderr,
			"usage: muscache <out.ofx> <startmusic> <AUDIOHED> <AUDIOT> [<AUDIOHED> <AUDIOT> ...]\n");
		return 1;
	}

	const long startMusic = atol(argv[2]);
	if(startMusic <= 0)
	{
		fprintf(stderr, "muscache: bad startmusic index '%s'\n", argv[2]);
		return 1;
	}

	double lpfHz = 12000.0;
	if(getenv("MUS_LPF_HZ"))
		lpfHz = atof(getenv("MUS_LPF_HZ"));

	DBOPL::Chip *chip = new DBOPL::Chip();
	chip->Setup(OPL_NATIVE_RATE);

	std::map<uint64_t, Entry> entries;
	unsigned rendered = 0, skipped = 0;
	int peakDbopl = 0, peakNuked = 0;
	double sumSqDbopl = 0.0, sumSqNuked = 0.0;
	uint64_t nDbopl = 0, nNuked = 0;

	for(int a = 3; a + 1 < argc; a += 2)
	{
		std::vector<uint8_t> hed = readFile(argv[a]);
		std::vector<uint8_t> aud = readFile(argv[a + 1]);
		const size_t numOff = hed.size() / 4;
		if(numOff < 2)
			continue;

		unsigned fileRendered = 0;
		for(size_t i = (size_t)startMusic; i + 1 < numOff; ++i)
		{
			const uint32_t off = rd32(&hed[i * 4]);
			const uint32_t end = rd32(&hed[(i + 1) * 4]);
			if(end <= off || end > aud.size())
				continue;

			const uint8_t *seq;
			uint32_t seqBytes;
			if(!parseSeq(&aud[off], end - off, &seq, &seqBytes))
				continue;

			const uint64_t hash = fnv1a64(seq, seqBytes);
			if(entries.count(hash))
			{
				skipped++;
				continue;
			}

			/* Reference render (the on-device fallback emulator) -- used
			 * only to calibrate the pack's loudness. */
			std::vector<int16_t> reference = renderDbopl(*chip, seq, seqBytes);
			if(reference.empty())
			{
				fprintf(stderr, "muscache: chunk %zu exceeds the duration cap; skipped\n", i);
				continue;
			}
			for(size_t k = 0; k < reference.size(); ++k)
			{
				const int v = abs((int)reference[k]);
				if(v > peakDbopl) peakDbopl = v;
				sumSqDbopl += (double)v * v;
			}
			nDbopl += reference.size();

			std::vector<int16_t> native = renderNuked(seq, seqBytes);
			for(size_t k = 0; k < native.size(); ++k)
			{
				const int v = abs((int)native[k]);
				if(v > peakNuked) peakNuked = v;
				sumSqNuked += (double)v * v;
			}
			nNuked += native.size();

			/* MUS_EMIT_REFERENCE=1: emit the DBOPL reference render
			 * instead of Nuked (for emulator A/B comparisons). */
			Entry e;
			e.pcm = resample(getenv("MUS_EMIT_REFERENCE") ? reference : native);
			lowpass(e.pcm, lpfHz);

			entries[hash] = e;
			rendered++;
			fileRendered++;
			printf("muscache: chunk %zu -> %.1f s\n",
				i, (double)e.pcm.size() / OUT_RATE);
		}
		printf("muscache: %s -> %u songs\n", argv[a + 1], fileRendered);
	}

	if(entries.empty())
	{
		fprintf(stderr, "muscache: no music chunks found\n");
		return 1;
	}

	/* Gain-match the Nuked output to the DBOPL fallback so cache hits and
	 * misses play at the same level on device.  The baked-in gain is peak
	 * limited so the pack PCM never clips; the residual RMS difference (the
	 * DBOPL reference saturates on loud songs, inflating its loudness) goes
	 * into the header as a Q16 makeup gain the runtime applies in its volume
	 * multiply. */
	double gain = 1.0, makeup = 1.0;
	if(!getenv("MUS_EMIT_REFERENCE") && peakNuked > 0 && peakDbopl > 0)
	{
		gain = (double)peakDbopl / (double)peakNuked;
		if(gain < 0.25) gain = 0.25;
		else if(gain > 16.0) gain = 16.0;
		if(sumSqNuked > 0.0 && nDbopl > 0 && nNuked > 0)
		{
			const double rmsDbopl = sqrt(sumSqDbopl / (double)nDbopl);
			const double rmsNuked = sqrt(sumSqNuked / (double)nNuked);
			makeup = rmsDbopl / (rmsNuked * gain);
			if(makeup < 0.25) makeup = 0.25;
			else if(makeup > 4.0) makeup = 4.0;
		}
	}
	printf("muscache: loudness calibration: dbopl peak %d, nuked peak %d, "
		"gain %.3f, rms makeup %.3f\n", peakDbopl, peakNuked, gain, makeup);
	for(std::map<uint64_t, Entry>::iterator it = entries.begin();
	    it != entries.end(); ++it)
	{
		std::vector<int16_t> &pcm = it->second.pcm;
		for(size_t k = 0; k < pcm.size(); ++k)
		{
			double v = pcm[k] * gain;
			if(v > 32767.0) v = 32767.0;
			else if(v < -32768.0) v = -32768.0;
			pcm[k] = (int16_t)lrint(v);
		}
	}

	/* Write the pack. */
	FILE *f = fopen(argv[1], "wb");
	if(!f)
	{
		fprintf(stderr, "muscache: cannot write %s\n", argv[1]);
		return 1;
	}
	const uint32_t count = (uint32_t)entries.size();
	uint32_t header[4] = { 0, count, OUT_RATE, (uint32_t)lrint(makeup * 65536.0) };
	memcpy(&header[0], "OFM1", 4);
	fwrite(header, 1, 16, f);

	uint32_t offset = 16 + count * 16;
	for(std::map<uint64_t, Entry>::const_iterator it = entries.begin();
	    it != entries.end(); ++it)
	{
		const uint64_t hash = it->first;
		const uint32_t samples = (uint32_t)it->second.pcm.size();
		fwrite(&hash, 1, 8, f);
		fwrite(&offset, 1, 4, f);
		fwrite(&samples, 1, 4, f);
		offset += samples * 2;
	}
	for(std::map<uint64_t, Entry>::const_iterator it = entries.begin();
	    it != entries.end(); ++it)
		fwrite(it->second.pcm.data(), 2, it->second.pcm.size(), f);
	fclose(f);

	printf("muscache: wrote %s (%u songs, %u duplicates skipped, %.1f MB)\n",
		argv[1], count, skipped, offset / (1024.0 * 1024.0));
	return 0;
}
