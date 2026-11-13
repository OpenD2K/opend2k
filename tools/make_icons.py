#!/usr/bin/env python3
"""OpenD2K app icons: one design, every platform's files.

    python -I tools/make_icons.py            (Windows: needs Microsoft Edge for the SVG rendering)

The icon is our own drawing (no game art, logos or house emblems): a bevelled bronze "D2K" (outlines
from the Play Bold font in third_party/fonts, OFL) over a desert at dusk with a harvester, spice and a
distant base, in a bolted bronze frame. Writes:
  assets/icon/opend2k.svg, opend2k-small.svg (16-32 px), android-foreground.svg, android-background.svg
  assets/icon/png/opend2k-{16..1024}.png       (rounded; 1024 = the master for macOS / iOS)
  assets/icon/png/opend2k-square-1024.png      (full bleed, for iOS / app stores that mask themselves)
  cmake/windows/opend2k.ico                    (embedded in the .exe by cmake/windows/opend2k.rc)
  android/app/src/main/res/mipmap-*/ic_launcher*.png + mipmap-anydpi-v26/ic_launcher.xml
  src/platform/app_icon_png.inc                (128 px PNG: the window icon on Windows / Linux)
Needs Pillow and fontTools (pip install pillow fonttools).
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "assets" / "icon"


# ---- the letters ------------------------------------------------------------------------------

def letters_path(cx, cy, width, tracking=-30):
    """'D2K' from Play Bold as one SVG path, centred on (cx, cy), `width` wide."""
    f = TTFont(REPO / "third_party/fonts/play/Play-Bold.ttf")
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    x, glyphs = 0, []
    for ch in "D2K":
        g = cmap[ord(ch)]
        glyphs.append((g, x))
        x += gs[g].width + tracking
    bp = BoundsPen(gs)
    for g, gx in glyphs:
        gs[g].draw(TransformPen(bp, (1, 0, 0, 1, gx, 0)))
    x0, y0, x1, y1 = bp.bounds
    k = width / (x1 - x0)
    tx, ty = cx - (x0 + x1) / 2 * k, cy + (y0 + y1) / 2 * k  # y flips
    cmds = []
    for g, gx in glyphs:
        pen = SVGPathPen(gs, ntos=lambda v: f"{v:.1f}")
        gs[g].draw(TransformPen(pen, (k, 0, 0, -k, tx + gx * k, ty)))
        cmds.append(pen.getCommands())
    return " ".join(cmds), (y1 - y0) * k


# ---- the picture --------------------------------------------------------------------------------

DEFS = """
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#1a0e1c"/><stop offset="0.5" stop-color="#4e1f18"/>
    <stop offset="0.78" stop-color="#b4562a"/><stop offset="1" stop-color="#e8903e"/>
  </linearGradient>
  <radialGradient id="glow" cx="0.5" cy="0.62" r="0.5">
    <stop offset="0" stop-color="#ffb860" stop-opacity="0.55"/><stop offset="1" stop-color="#ffb860" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="metal" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff3cc"/><stop offset="0.3" stop-color="#f4bd56"/><stop offset="0.55" stop-color="#b0621c"/>
    <stop offset="0.62" stop-color="#8a4612"/><stop offset="1" stop-color="#ffcf6e"/>
  </linearGradient>
  <linearGradient id="frame" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#e8b062"/><stop offset="0.5" stop-color="#7c4418"/><stop offset="1" stop-color="#c88a40"/>
  </linearGradient>
  <linearGradient id="dune1" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#c8692a"/><stop offset="1" stop-color="#7a3214"/></linearGradient>
  <linearGradient id="dune2" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#9a4418"/><stop offset="1" stop-color="#3a160a"/></linearGradient>
  <radialGradient id="rivet" cx="0.35" cy="0.3" r="0.7">
    <stop offset="0" stop-color="#fff0c8"/><stop offset="0.5" stop-color="#c08038"/><stop offset="1" stop-color="#4a2408"/>
  </radialGradient>
  <filter id="sh" x="-20%" y="-20%" width="140%" height="140%"><feDropShadow dx="0" dy="12" stdDeviation="12" flood-color="#000" flood-opacity="0.65"/></filter>
  <filter id="blur"><feGaussianBlur stdDeviation="6"/></filter>
"""

SKY = '<rect x="-600" y="-600" width="2224" height="2224" fill="url(#sky)"/><rect width="1024" height="1024" fill="url(#glow)"/>'

HORIZON = """<g fill="#2a120c" opacity="0.9">
  <path d="M90 700 L90 676 A34 34 0 0 1 158 676 L158 700 Z"/><path d="M150 700 L150 666 A46 46 0 0 1 242 666 L242 700 Z"/>
  <rect x="250" y="636" width="22" height="64"/><rect x="244" y="628" width="34" height="10"/>
  <path d="M800 700 L800 680 A30 30 0 0 1 860 680 L860 700 Z"/><rect x="872" y="648" width="16" height="52"/></g>"""

FAR_DUNE = """<path d="M-600 740 L0 720 C 200 660, 420 670, 600 720 S 880 770, 1024 700 L1624 690 L1624 1624 L-600 1624 Z" fill="url(#dune1)"/>
<path d="M0 720 C 200 660, 420 670, 600 720" fill="none" stroke="#f6b06a" stroke-width="8" stroke-linecap="round" opacity="0.75"/>"""

# A harvester of our own design (facing right): tracks, hull, ribbed spice hopper, cab, scoop.
HARVESTER = """<ellipse cx="500" cy="800" rx="220" ry="34" fill="#e6a868" opacity="0.55" filter="url(#blur)"/>
<ellipse cx="380" cy="796" rx="150" ry="22" fill="#f0bc80" opacity="0.45" filter="url(#blur)"/>
<g transform="translate(610 690) rotate(-6) scale(1.55)">
  <rect x="0" y="56" width="150" height="24" rx="12" fill="#1e0e08"/>
  <g fill="#5a3418"><circle cx="14" cy="68" r="8"/><circle cx="42" cy="68" r="8"/><circle cx="70" cy="68" r="8"/><circle cx="98" cy="68" r="8"/><circle cx="126" cy="68" r="8"/></g>
  <path d="M4 56 L10 30 L104 30 L118 40 L146 40 L150 56 Z" fill="#3a1e10"/>
  <path d="M10 30 L104 30 L108 34 L12 34 Z" fill="#b07038"/>
  <rect x="16" y="8" width="70" height="22" rx="3" fill="#4a2814"/>
  <g stroke="#7a4620" stroke-width="3"><line x1="30" y1="10" x2="30" y2="28"/><line x1="44" y1="10" x2="44" y2="28"/><line x1="58" y1="10" x2="58" y2="28"/><line x1="72" y1="10" x2="72" y2="28"/></g>
  <path d="M16 8 L86 8 L86 12 L16 12 Z" fill="#c88a48"/>
  <path d="M100 30 L100 12 L124 12 L136 40 L118 40 Z" fill="#4a2814"/>
  <path d="M106 16 L122 16 L130 34 L106 34 Z" fill="#ffcf80"/>
  <path d="M146 42 L170 48 L172 70 L150 74 Z" fill="#2a140a"/>
  <g stroke="#c88a48" stroke-width="2.5"><line x1="156" y1="50" x2="156" y2="70"/><line x1="164" y1="52" x2="164" y2="70"/></g>
</g>"""

NEAR_DUNE = """<path d="M-600 880 L0 860 C 240 780, 500 790, 720 850 S 950 900, 1024 860 L1624 850 L1624 1624 L-600 1624 Z" fill="url(#dune2)"/>
<path d="M0 860 C 240 780, 500 790, 720 850" fill="none" stroke="#d8823e" stroke-width="7" stroke-linecap="round" opacity="0.7"/>
<g fill="#ff8a2a">""" + "".join(
    f'<circle cx="{x}" cy="{y}" r="{r}"/>' for x, y, r in [
        (120, 900, 7), (160, 930, 5), (210, 895, 6), (250, 950, 8), (300, 915, 5), (340, 960, 6), (390, 905, 7), (440, 945, 5),
        (180, 975, 6), (560, 930, 6), (610, 965, 5), (660, 915, 7), (840, 950, 6), (890, 920, 5)]) + """</g>
<g fill="#ffd28a" opacity="0.9"><circle cx="140" cy="912" r="3"/><circle cx="270" cy="930" r="3"/><circle cx="410" cy="925" r="3"/><circle cx="640" cy="940" r="3"/></g>"""

SIMPLE_DUNE = """<path d="M-600 790 L0 770 C 260 690, 520 700, 740 770 S 950 830, 1024 780 L1624 770 L1624 1624 L-600 1624 Z" fill="url(#dune2)"/>
<path d="M0 770 C 260 690, 520 700, 740 770" fill="none" stroke="#f0a050" stroke-width="16" stroke-linecap="round" opacity="0.8"/>"""


def monogram(d, outline=40, rim=10, highlight=True):
    out = f'<g filter="url(#sh)"><path d="{d}" fill="#2a1206" stroke="#2a1206" stroke-width="{outline}" stroke-linejoin="round"/>'
    out += f'<path d="{d}" fill="url(#metal)" stroke="#6a3410" stroke-width="{rim}" stroke-linejoin="round"/>'
    if highlight:
        out += f'<path d="{d}" fill="none" stroke="#fff4d0" stroke-width="3" opacity="0.5"/>'
    return out + "</g>"


def frame(rivets=True, inner=56):
    s = f'<rect width="1024" height="1024" fill="url(#frame)"/>'
    s += '<rect x="20" y="20" width="984" height="984" rx="92" fill="none" stroke="#3a1c08" stroke-width="6" opacity="0.6"/>'
    if rivets:
        pts = [(150, 38), (874, 38), (150, 986), (874, 986), (38, 150), (986, 150), (38, 874), (986, 874), (512, 38), (512, 986),
               (38, 512), (986, 512), (331, 38), (693, 38), (331, 986), (693, 986), (38, 331), (986, 331), (38, 693), (986, 693)]
        s += "".join(f'<circle cx="{x}" cy="{y}" r="10" fill="url(#rivet)"/>' for x, y in pts)
    return s


def svg(body, clip_inner=None):
    clip = ""
    if clip_inner is not None:
        i = clip_inner
        clip = f'<clipPath id="inner"><rect x="{i}" y="{i}" width="{1024 - 2 * i}" height="{1024 - 2 * i}" rx="{70 if i > 40 else 40}"/></clipPath>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">\n'
            f'<!-- OpenD2K icon: generated by tools/make_icons.py (our own drawing; letters: Play Bold, OFL). -->\n'
            f'<defs>{DEFS}{clip}</defs>\n{body}\n</svg>\n')


def build_svgs():
    d_full, _ = letters_path(512, 470, 690)
    d_small, _ = letters_path(512, 440, 820, tracking=-10)
    d_fg, _ = letters_path(512, 500, 500)
    full = frame(rivets=False) + '<g clip-path="url(#inner)">' + SKY + HORIZON + FAR_DUNE + HARVESTER + NEAR_DUNE + monogram(d_full) + "</g>"
    full += frame_rivets_only()
    small = frame(rivets=False) + '<g clip-path="url(#inner)">' + SKY + SIMPLE_DUNE + monogram(d_small, outline=56, rim=14, highlight=False) + "</g>"
    # Android adaptive layers (108 dp; the launcher shows the middle 72 dp in its own shape, letters
    # within the 66 dp safe zone): the scene full bleed behind, the letters in front.
    background = SKY + HORIZON + FAR_DUNE + HARVESTER + NEAR_DUNE
    foreground = monogram(d_fg, outline=34, rim=9)
    return {
        "opend2k.svg": svg(full, clip_inner=56),
        "opend2k-small.svg": svg(small, clip_inner=40),
        "android-background.svg": svg(background),
        "android-foreground.svg": svg(foreground),
    }


def frame_rivets_only():
    pts = [(150, 38), (874, 38), (150, 986), (874, 986), (38, 150), (986, 150), (38, 874), (986, 874), (512, 38), (512, 986),
           (38, 512), (986, 512), (331, 38), (693, 38), (331, 986), (693, 986), (38, 331), (986, 331), (38, 693), (986, 693)]
    return "".join(f'<circle cx="{x}" cy="{y}" r="10" fill="url(#rivet)"/>' for x, y in pts)


# ---- rendering ----------------------------------------------------------------------------------

def edge():
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe", r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"):
        if os.path.exists(p):
            return p
    sys.exit("Microsoft Edge not found (it renders the SVGs)")


def render(svg_path: Path, size=1024) -> Image.Image:
    with tempfile.TemporaryDirectory() as td:
        html = Path(td) / "page.html"
        html.write_text(f'<html><body style="margin:0;background:transparent"><img src="{svg_path.as_uri()}" '
                        f'width="{size}" height="{size}" style="display:block"></body></html>', encoding="utf-8")
        png = Path(td) / "out.png"
        subprocess.run([edge(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                        "--default-background-color=00000000", f"--window-size={size},{size}", f"--screenshot={png}",
                        html.as_uri()], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        im = Image.open(png).convert("RGBA")
        im.load()
        return im.crop((0, 0, size, size))


def rounded(im: Image.Image, radius_frac=0.2) -> Image.Image:
    s = im.size[0]
    big = s * 4
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=int(big * radius_frac), fill=255)
    out = im.copy()
    out.putalpha(Image.composite(im.getchannel("A"), Image.new("L", im.size, 0), mask.resize(im.size, Image.LANCZOS)))
    return out


def scaled(im: Image.Image, size: int) -> Image.Image:
    out = im.resize((size, size), Image.LANCZOS)
    if size <= 64:
        out = out.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=1))
    return out


def png_bytes(im: Image.Image) -> bytes:
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return b.getvalue()


def write_ico(path: Path, images):
    """An .ico of PNG-compressed entries (Windows Vista+)."""
    data = [png_bytes(im) for im in images]
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for im, d in zip(images, data):
        w = im.size[0]
        entries += struct.pack("<BBBBHHII", w if w < 256 else 0, w if w < 256 else 0, 0, 0, 1, 32, len(d), offset)
        offset += len(d)
    path.write_bytes(head + entries + b"".join(data))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "png").mkdir(exist_ok=True)
    for name, text in build_svgs().items():
        (OUT / name).write_text(text, encoding="utf-8")
    full = render(OUT / "opend2k.svg")
    small = render(OUT / "opend2k-small.svg")
    bg = render(OUT / "android-background.svg")
    fg = render(OUT / "android-foreground.svg")
    full_r, small_r = rounded(full), rounded(small, 0.18)

    full.save(OUT / "png" / "opend2k-square-1024.png", optimize=True)
    sizes = {}
    for s in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        sizes[s] = scaled(small_r if s <= 32 else full_r, s)
        sizes[s].save(OUT / "png" / f"opend2k-{s}.png", optimize=True)

    # Windows .ico
    write_ico(REPO / "cmake" / "windows" / "opend2k.ico", [sizes[s] for s in (16, 24, 32, 48, 64, 128, 256)])

    # Android: legacy launcher icons + adaptive layers (108 dp)
    res = REPO / "android" / "app" / "src" / "main" / "res"
    for dpi, k in (("mdpi", 1), ("hdpi", 1.5), ("xhdpi", 2), ("xxhdpi", 3), ("xxxhdpi", 4)):
        d = res / f"mipmap-{dpi}"
        d.mkdir(parents=True, exist_ok=True)
        scaled(full_r if 48 * k > 32 else small_r, int(48 * k)).save(d / "ic_launcher.png", optimize=True)
        scaled(bg, int(108 * k)).save(d / "ic_launcher_background.png", optimize=True)
        scaled(fg, int(108 * k)).save(d / "ic_launcher_foreground.png", optimize=True)
    (res / "mipmap-anydpi-v26").mkdir(exist_ok=True)
    (res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<!-- OpenD2K launcher icon (tools/make_icons.py) -->\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
        '</adaptive-icon>\n', encoding="utf-8")

    # The window icon (Windows / Linux title bars and task bars): a 128 px PNG compiled in.
    data = png_bytes(sizes[128])
    lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    (REPO / "src" / "platform" / "app_icon_png.inc").write_text(
        "// OpenD2K window icon: assets/icon/png/opend2k-128.png (generated by tools/make_icons.py).\n"
        + ",\n".join(lines) + "\n", encoding="utf-8")
    print("icons written:", OUT, "cmake/windows/opend2k.ico, android mipmaps, src/platform/app_icon_png.inc")


if __name__ == "__main__":
    main()
