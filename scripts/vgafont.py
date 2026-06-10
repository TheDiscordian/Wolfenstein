#!/usr/bin/env python3
# Decode a VGAGRAPH font chunk and render glyphs as ASCII art.
# Usage: vgafont.py <datadir> <ext> [chunk] [chars]
# Example: vgafont.py src/blakestone BS6 3 "JAM1d"
import struct, sys

if len(sys.argv) < 3:
    sys.exit(__doc__ or "usage: vgafont.py <datadir> <ext> [chunk] [chars]")

base, ext = sys.argv[1].rstrip('/') + '/', sys.argv[2].upper()
dict_data = open(base + 'VGADICT.' + ext, 'rb').read()
head_data = open(base + 'VGAHEAD.' + ext, 'rb').read()
graph = open(base + 'VGAGRAPH.' + ext, 'rb').read()

# huffman nodes: 255 pairs of uint16
nodes = [struct.unpack_from('<HH', dict_data, i*4) for i in range(255)]

def chunk_offset(i):
    return head_data[i*3] | (head_data[i*3+1]<<8) | (head_data[i*3+2]<<16)

def huff_expand(src, explen):
    out = bytearray()
    head = 254
    node = head
    pos = 0
    val = src[pos]; pos += 1
    mask = 1
    while True:
        nv = nodes[node][1] if (val & mask) else nodes[node][0]
        if mask == 0x80:
            if pos >= len(src): break
            val = src[pos]; pos += 1
            mask = 1
        else:
            mask <<= 1
        if nv < 256:
            out.append(nv)
            node = head
            if len(out) >= explen: break
        else:
            node = nv - 256
    return bytes(out)

chunk = int(sys.argv[3]) if len(sys.argv) > 3 else 1
pos, nxt = chunk_offset(chunk), chunk_offset(chunk+1)
raw = graph[pos:nxt]
explen = struct.unpack_from('<I', raw, 0)[0]
data = huff_expand(raw[4:], explen)
print(f"chunk {chunk}: complen={len(raw)} explen={explen} got={len(data)}")

height = struct.unpack_from('<h', data, 0)[0]
loc = [struct.unpack_from('<H', data, 2+i*2)[0] for i in range(256)]
wid = list(data[514:514+256])
print(f"height={height}")
defined = [i for i in range(256) if wid[i]>0]
print(f"defined chars: {len(defined)}, first={defined[0]} ({chr(defined[0]) if 32<=defined[0]<127 else '?'}), last={defined[-1]}")
print(f"loc[0]={loc[0]} (0x{loc[0]:04x})")

def render(ch):
    c = ord(ch)
    w = wid[c]
    print(f"--- glyph for {ch!r} (code {c}) width={w} loc={loc[c]} ---")
    if w == 0:
        print("  (no glyph)")
        return
    for y in range(height):
        row = data[loc[c]+y*w : loc[c]+y*w+w]
        print('  ' + ''.join('#' if b else '.' for b in row))

for ch in sys.argv[4] if len(sys.argv) > 4 else '1iP':
    render(ch)
