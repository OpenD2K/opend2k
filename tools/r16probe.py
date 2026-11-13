#!/usr/bin/env python3
"""Probe/decode Dune 2000 .R8/.R16 image archives.

Frame layout (hypothesis, from community docs / OpenRA R8Loader; being verified):
  u8  type          0 = padding/empty (skip), 1 = frame with palette, 2 = frame using last palette
  i32 width, height
  i32 x, y          origin offsets
  u32 image_ptr     (in-game pointer, ignored)
  u32 palette_ptr   (non-zero -> a palette block follows the pixels when type == 1)
  u8  bpp           8 or 16
  u8  frame_h, frame_w
  u8  align
  pixels            width*height*(bpp/8)
  [palette]         if type==1 and palette_ptr: 520 bytes (8-byte hdr + 256 x u16 colours)

Usage: r16probe.py FILE [--png OUTDIR] [--limit N]
"""
import argparse, os, struct, sys
from collections import Counter


def rgb555(c):
    r = (c >> 10) & 0x1F; g = (c >> 5) & 0x1F; b = c & 0x1F
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def parse(buf):
    pos, n, frames, pal = 0, len(buf), [], None
    while pos < n:
        t = buf[pos]
        if t == 0:
            pos += 1
            continue
        if pos + 29 > n:
            raise ValueError(f"truncated header at {pos:#x}")
        w, h, x, y, iptr, pptr = struct.unpack_from("<iiiiII", buf, pos + 1)
        bpp, fh, fw, _ = struct.unpack_from("<BBBB", buf, pos + 25)
        start = pos + 29
        if bpp not in (8, 16) or not (0 < w <= 4096 and 0 < h <= 4096):
            raise ValueError(f"bad frame at {pos:#x}: type={t} w={w} h={h} bpp={bpp}")
        size = w * h * (bpp // 8)
        pixels = buf[start:start + size]
        pos = start + size
        if t == 1 and pptr:
            raw = buf[pos + 8:pos + 520]
            pal = [rgb555(c) for c in struct.unpack("<256H", raw)]
            pos += 520
        frames.append(dict(off=start - 29, type=t, w=w, h=h, x=x, y=y, bpp=bpp,
                           fw=fw, fh=fh, pixels=pixels, pal=pal))
    return frames


def to_image(f):
    from PIL import Image
    if f["bpp"] == 16:
        px = struct.unpack(f"<{f['w']*f['h']}H", f["pixels"])
        img = Image.new("RGBA", (f["w"], f["h"]))
        img.putdata([rgb555(c) + ((0,) if c == 0 else (255,)) for c in px])
        return img
    img = Image.frombytes("P", (f["w"], f["h"]), f["pixels"])
    pal = f["pal"] or [(i, i, i) for i in range(256)]
    img.putpalette([v for c in pal for v in c])
    img.info["transparency"] = 0
    return img.convert("RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--png")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    buf = open(a.file, "rb").read()
    frames = parse(buf)
    print(f"{a.file}: {len(frames)} frames, {len(buf)} bytes")
    print(" types:", Counter(f["type"] for f in frames), " bpp:", Counter(f["bpp"] for f in frames))
    print(" sizes:", Counter((f["w"], f["h"]) for f in frames).most_common(8))
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for i, f in enumerate(frames[: a.limit or None]):
            to_image(f).save(os.path.join(a.png, f"{i:05d}.png"))


if __name__ == "__main__":
    sys.exit(main())
