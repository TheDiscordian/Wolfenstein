#!/usr/bin/env python3
"""IMF (id Music Format, OPL2) -> Standard MIDI converter for Blake Stone.

Blake Stone's music lives as IMF chunks at the tail of the game's AUDIOT
(after the PC-speaker and AdLib *sound* chunks). The N music chunks are the
last N entries, where N = number of names in the engine's `music { }` remap.
This tool reads AUDIOHED+AUDIOT, slices out each music chunk, and renders the
OPL register stream to a type-0 SMF so the openFPGA core's of_midi synth can
play it against the GM soundfont.

Conversion model:
  - OPL2 has 9 melodic channels (0-8). Registers:
      0xA0+ch  F-Number low 8 bits
      0xB0+ch  bits0-1 F-Number high, bits2-4 block (octave), bit5 key-on
  - Real frequency = FNum * OPL_clock / 2^(20-block), OPL_clock = 49716 Hz.
  - MIDI note = round(12*log2(f/440)+69).
  - IMF word pairs are (reg, val) followed by a uint16 LE delay in ticks at
    the IMF rate (Blake/Wolf = 700 Hz). Delay accumulates onto the next event.
  - One MIDI track per OPL channel (channels 9-15 of MIDI; we skip MIDI ch 9
    = percussion). A coarse GM program is assigned per channel.

Timbre is approximate (GM soundfont, not AdLib FM) by design; notes and timing
extract faithfully.
"""
import struct
import sys
import math
import argparse

IMF_RATE_DEFAULT = 700        # Hz, Blake Stone / Wolf3D
OPL_CLOCK = 49716.0           # OPL sample clock
MIDI_DIVISION = 560           # ticks per quarter note (see tempo note below)

# We emit a fixed tempo and map IMF ticks straight to MIDI ticks 1:1 by
# choosing tempo so that MIDI_DIVISION ticks = one quarter note and the
# microseconds-per-tick equals 1e6/IMF_RATE. That keeps playback at the
# original wall-clock speed regardless of the synth.

# GM programs per OPL channel index (0-8). Spread voices so the GM render
# isn't a wall of pianos; purely cosmetic, all channels are melodic.
GM_PROGRAM_BY_CH = [
    0,    # ch0  Acoustic Grand Piano
    33,   # ch1  Electric Bass (finger)
    48,   # ch2  String Ensemble 1
    25,   # ch3  Acoustic Guitar (steel)
    73,   # ch4  Flute
    11,   # ch5  Vibraphone
    61,   # ch6  Brass Section
    81,   # ch7  Lead 2 (sawtooth)
    16,   # ch8  Drawbar Organ
]

# MIDI channels to use for OPL channels 0-8, skipping channel 9 (percussion).
MIDI_CH_FOR_OPL = [0, 1, 2, 3, 4, 5, 6, 7, 8]


def read_audiohed(path):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data) // 4
    return list(struct.unpack("<%dI" % n, data))


def music_chunks(audiohed_path, audiot_path, num_music):
    """Return list of raw chunk bytes for the last `num_music` chunks."""
    offs = read_audiohed(audiohed_path)
    with open(audiot_path, "rb") as f:
        audiot = f.read()
    num_chunks = len(offs) - 1
    out = []
    for i in range(num_chunks - num_music, num_chunks):
        start, end = offs[i], offs[i + 1]
        out.append(audiot[start:end])
    return out


def parse_imf(chunk):
    """Yield (reg, val, delay) tuples from an IMF type-1 chunk.

    Type-1 IMF: first uint16 LE is the data length in bytes; the body is
    that many bytes of (reg u8, val u8, delay u16 LE) records. Anything past
    the declared length is a text/footer tag and is ignored.
    """
    if len(chunk) < 2:
        return
    data_len = struct.unpack("<H", chunk[:2])[0]
    if data_len == 0 or data_len + 2 > len(chunk):
        # Type-0 (no length word) fallback: treat whole chunk as data.
        body = chunk
    else:
        body = chunk[2:2 + data_len]
    pos = 0
    end = len(body) - (len(body) % 4)
    while pos < end:
        reg, val, delay = struct.unpack_from("<BBH", body, pos)
        pos += 4
        yield reg, val, delay


def fnum_block_to_note(fnum, block):
    if fnum == 0:
        return None
    freq = fnum * OPL_CLOCK / (1 << (20 - block))
    if freq <= 0:
        return None
    note = int(round(12 * math.log2(freq / 440.0) + 69))
    if note < 0 or note > 127:
        return None
    return note


def write_varlen(value):
    out = bytearray()
    out.append(value & 0x7F)
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(bytearray(reversed(out)))


class TrackBuilder:
    """Accumulates MIDI events with absolute tick times, emits a sorted track."""

    def __init__(self):
        self.events = []  # (tick, order, bytes)
        self._order = 0

    def add(self, tick, data):
        self.events.append((tick, self._order, data))
        self._order += 1

    def serialize(self):
        self.events.sort(key=lambda e: (e[0], e[1]))
        body = bytearray()
        last = 0
        for tick, _, data in self.events:
            delta = tick - last
            last = tick
            body += write_varlen(delta)
            body += data
        body += write_varlen(0) + b"\xFF\x2F\x00"  # end of track
        return bytes(body)


def imf_to_midi(chunk, imf_rate):
    builder = TrackBuilder()

    # Tempo: us per quarter = MIDI_DIVISION * (1e6 / imf_rate).
    us_per_quarter = int(round(MIDI_DIVISION * (1_000_000.0 / imf_rate)))
    builder.add(0, b"\xFF\x51\x03" + struct.pack(">I", us_per_quarter)[1:])

    # Per-channel program changes.
    for opl_ch, prog in enumerate(GM_PROGRAM_BY_CH):
        mch = MIDI_CH_FOR_OPL[opl_ch]
        builder.add(0, bytes([0xC0 | mch, prog]))

    fnum_low = [0] * 9
    fnum_high = [0] * 9
    block = [0] * 9
    key_on = [False] * 9
    cur_note = [None] * 9

    tick = 0

    def note_off(opl_ch, t):
        if cur_note[opl_ch] is not None:
            mch = MIDI_CH_FOR_OPL[opl_ch]
            builder.add(t, bytes([0x80 | mch, cur_note[opl_ch], 0]))
            cur_note[opl_ch] = None

    def note_on(opl_ch, t):
        note = fnum_block_to_note(
            (fnum_high[opl_ch] << 8) | fnum_low[opl_ch], block[opl_ch]
        )
        if note is None:
            return
        if cur_note[opl_ch] is not None:
            if cur_note[opl_ch] == note:
                return  # retrigger same pitch: leave sounding
            note_off(opl_ch, t)
        mch = MIDI_CH_FOR_OPL[opl_ch]
        builder.add(t, bytes([0x90 | mch, note, 100]))
        cur_note[opl_ch] = note

    for reg, val, delay in parse_imf(chunk):
        if 0xA0 <= reg <= 0xA8:
            ch = reg - 0xA0
            fnum_low[ch] = val
            if key_on[ch]:
                # frequency slide while sounding: re-key to new pitch
                note_on(ch, tick)
        elif 0xB0 <= reg <= 0xB8:
            ch = reg - 0xB0
            fnum_high[ch] = val & 0x03
            block[ch] = (val >> 2) & 0x07
            new_key = bool(val & 0x20)
            if new_key and not key_on[ch]:
                key_on[ch] = True
                note_on(ch, tick)
            elif not new_key and key_on[ch]:
                key_on[ch] = False
                note_off(ch, tick)
            elif new_key and key_on[ch]:
                note_on(ch, tick)  # pitch changed while held

        tick += delay

    # Release anything still held at end.
    for ch in range(9):
        note_off(ch, tick)

    track = builder.serialize()
    # MThd: 4-byte length (always 6), then format/ntracks/division.
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, MIDI_DIVISION)
    track_chunk = b"MTrk" + struct.pack(">I", len(track)) + track
    return header + track_chunk


def parse_music_names(mapfile):
    """Extract the ordered name list from a remap's `music { ... }` block."""
    names = []
    with open(mapfile, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    idx = text.find("music")
    if idx < 0:
        return names
    brace = text.find("{", idx)
    close = text.find("}", brace)
    body = text[brace + 1:close]
    for tok in body.split(","):
        tok = tok.strip().strip('"').strip()
        if tok:
            names.append(tok)
    return names


def main():
    ap = argparse.ArgumentParser(description="Blake Stone IMF -> MIDI")
    ap.add_argument("--audiohed", required=True)
    ap.add_argument("--audiot", required=True)
    ap.add_argument("--mapfile", required=True,
                    help="remap .txt with the music { } block (defines names+order)")
    ap.add_argument("--outdir", required=True,
                    help="dir to write MUSIC/<NAME> SMF files into")
    ap.add_argument("--rate", type=int, default=IMF_RATE_DEFAULT)
    args = ap.parse_args()

    names = parse_music_names(args.mapfile)
    if not names:
        sys.exit("no music names found in %s" % args.mapfile)
    chunks = music_chunks(args.audiohed, args.audiot, len(names))

    import os
    musicdir = os.path.join(args.outdir, "MUSIC")
    os.makedirs(musicdir, exist_ok=True)
    for name, chunk in zip(names, chunks):
        smf = imf_to_midi(chunk, args.rate)
        with open(os.path.join(musicdir, name), "wb") as f:
            f.write(smf)
        print("%-12s  imf_chunk=%6d  smf=%6d" % (name, len(chunk), len(smf)))


if __name__ == "__main__":
    main()
