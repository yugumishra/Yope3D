"""
logo_pack.py — pack the baked logo JSON clips into one compact binary
(assets/logo/logo.bin) the engine loads zero-copy at startup.

The JSONs are ~5x inflated by text (94MB part2 -> ~9MB of actual data). This:
  (A) trims part2's lead-in frames (the ball's approach before it strikes),
  (C) quantizes each [0,1] coord to uint16 (1/65535 ~= 0.03px on 4K — invisible),
  and writes a flat binary that src/rendering/LogoClip.h maps with std::span.

Run after (re)baking the JSONs:
  python3 tools/logo_pack.py

Binary layout (little-endian):
  header:  u32 magic='YLG1', u32 version=1, u32 clipCount=2, u32 reserved=0
  dir[2]:  u32 frameCount, f32 fps, f32 refAspect,
           u32 coordCount, u32 frameStartOffset, u32 coordOffset   (24 bytes each)
  data:    per clip: u32 frameStart[frameCount+1] then u16 coords[coordCount]
           (coords are groups of 4 = [x,y,x,y]; frameStart indexes into coords)
"""

import json
import os
import struct
from array import array

HERE       = os.path.dirname(__file__)
LOGO_DIR   = os.path.abspath(os.path.join(HERE, "..", "assets", "logo"))
PART1_JSON = os.path.join(LOGO_DIR, "logo_part1.json")
PART2_JSON = os.path.join(LOGO_DIR, "logo_part2.json")
OUT_BIN    = os.path.join(LOGO_DIR, "logo.bin")

PART2_TRIM_LEAD = 212    # (A) drop the ball's approach; tumble starts on the strike.
                         #   85 Blender frames @24fps sim -> 60fps output = 85*60/24 = 212.5
MAGIC           = 0x31474C59   # 'YLG1'

# Quantization range. Coords are cam-view [0,1] on-screen, but the sphere rolls
# in from FAR off-screen (actual range ~[-27, 15]); quantizing that whole extent
# would waste precision on the visible [0,1]. Anything past ~1 screen off is
# invisible, so clamp to a margin — off-screen geometry still rolls in/out
# smoothly (its far, invisible part just clamps) and on-screen stays sub-pixel.
QUANT_LO = -2.0
QUANT_HI =  3.0


def q(v):
    """float -> uint16 over [QUANT_LO, QUANT_HI]."""
    t = (min(QUANT_HI, max(QUANT_LO, v)) - QUANT_LO) / (QUANT_HI - QUANT_LO)
    x = int(round(t * 65535.0))
    return 0 if x < 0 else (65535 if x > 65535 else x)


def build_clip(path, trim_lead=0):
    d = json.load(open(path))
    frames = d["frames"][trim_lead:]
    fps = float(d.get("fps", 60.0))
    ra  = float(d.get("ref_aspect", 16.0 / 9.0))

    coords = array("H")      # uint16
    fstart = array("I")      # uint32, index into coords
    idx = 0
    for fr in frames:
        fstart.append(idx)
        for seg in fr:
            coords.append(q(seg[0])); coords.append(q(seg[1]))
            coords.append(q(seg[2])); coords.append(q(seg[3]))
            idx += 4
    fstart.append(idx)       # sentinel end
    return dict(frameCount=len(frames), fps=fps, refAspect=ra,
                coords=coords, fstart=fstart)


clips = [build_clip(PART1_JSON), build_clip(PART2_JSON, PART2_TRIM_LEAD)]

# Assemble payload, keeping every block 4-byte aligned (u32 frameStart first,
# then u16 coords) so the reader's reinterpret_casts are aligned.
DIR_SIZE = 32 * len(clips)   # 32-byte directory entries (see struct below)
DATA_BASE = 16 + DIR_SIZE
payload = bytearray()
dir_entries = []
for c in clips:
    fs_off = DATA_BASE + len(payload)
    payload += c["fstart"].tobytes()
    co_off = DATA_BASE + len(payload)
    payload += c["coords"].tobytes()
    while len(payload) % 4:
        payload += b"\x00"
    dir_entries.append((c["frameCount"], c["fps"], c["refAspect"],
                        len(c["coords"]), fs_off, co_off))

out = bytearray()
out += struct.pack("<IIII", MAGIC, 1, len(clips), 0)
for (fc, fps, ra, cc, fso, coo) in dir_entries:
    # 32 bytes: frameCount, fps, refAspect, coordCount, fsOffset, coOffset, lo, hi
    out += struct.pack("<IffIIIff", fc, fps, ra, cc, fso, coo, QUANT_LO, QUANT_HI)
out += payload

with open(OUT_BIN, "wb") as f:
    f.write(out)

print("logo_pack: wrote %s  (%.1f MB)" % (OUT_BIN, len(out) / 1e6))
for i, (fc, fps, ra, cc, _, _) in enumerate(dir_entries):
    print("  clip%d: frames=%d fps=%g ref_aspect=%.4f coords=%d (%.1f MB)"
          % (i, fc, fps, ra, cc, cc * 2 / 1e6))
