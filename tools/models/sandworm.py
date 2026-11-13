"""Sandworm - the worm as the game shows it when it EATS: the explosion art EX_WORM (Templates.bin explosion 18 =
animation art 18 = DATA.R16 frames 4452-4467, 16 frames), rebuilt as an animated model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/sandworm.py -- --out DIR [--no-bake]
    D2K_UNIT=sandworm .venv/bin/python -I tools/models/vehicle_check/pack_frames.py --models DIR --out PACK

The SANDWORM unit itself has no art (unit art -1, never drawn): only this animation shows the worm, so there are no
facings. The generator writes one pose variant per frame (DIR/variants/SANDWORM_frameNN.glb); pack_frames.py renders
them into the 16 frames. The companion explosion EX_WORMDUST (19, frames 4468-4488: the sand spray) is not replaced.

How the original strip reads (hotspot = the red dot of the sheets; the worm's axis stands 3 px right of it and 20 px
below it, its shadow falls to the right):
  0-1   a burst of pale sand, 60 px wide; in 1 the tip of a jaw shows
  2     three long pointed jaws burst out, splayed wide (dark maw inside)
  3-5   the jaws stand tall (two behind like ears, one in front) and close: 30, 20, 10 degrees
  6-11  closed: a pointed golden cone, 21 px across and 44 px tall, seams between the jaws, sinking slowly
  12-14 sinking fast, 14 is only the tip
  15    a last puff of sand
A ring of pale sand lies round the base in every frame.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

RENDER = dict(house_grey=0.5, sun_elevation=50.0, shadow_azimuth=42.0, contour=0.4, contour_azimuth=-45.0, sharpen=0.6,
              tone=((0.0, 0.0), (0.12, 0.11), (0.25, 0.26), (0.5, 0.55), (0.75, 0.82), (1.0, 1.0)))
KEY = "SANDWORM"
FIRST_FRAME, FRAMES = 4452, 16

P = dict(
    v0=3.0, u0=-24.5,               # the worm's axis relative to the frames' hotspot (px; u0 = -17 screen px / sin 45)
    R=10.4, H=44.0, hinge=5.0, wall=1.7, gap=2.4,
    dust_r=13.0,
)
# frame: (jaw opening in degrees, rise of the head in px (0 = fully out), sand ring scale, burst scale (0 = none))
POSES = [(0, -90, 1.5, 1.9), (24, -30, 1.45, 1.55), (56, -5, 1.25, 0.0), (30, 0, 1.1, 0.0), (20, 0, 1.05, 0.0), (9, -2, 1.0, 0.0),
         (0, -4, 1.0, 0.0), (0, -6, 1.0, 0.0), (0, -7, 1.0, 0.0), (0, -8, 1.0, 0.0), (0, -9, 1.05, 0.0), (0, -10, 1.05, 0.0),
         (0, -16, 1.0, 0.0), (0, -22, 0.95, 0.0), (0, -35, 0.8, 0.0), (0, -90, 0.55, 0.5)]
COL = dict(worm=(0.740, 0.570, 0.370), worm_lt=(0.960, 0.800, 0.500), worm_dk=(0.660, 0.500, 0.320), maw=(0.330, 0.220, 0.160),
           tooth=(0.930, 0.880, 0.760), maw_in=(0.420, 0.290, 0.200), dust=(0.940, 0.890, 0.790), dust_dk=(0.850, 0.770, 0.640))
SPEC = dict(worm=(0.0, 0.5, "paint"), worm_lt=(0.0, 0.4, "paint"), worm_dk=(0.0, 0.6, "paint"), maw=(0.0, 0.8, "dark"),
            tooth=(0.0, 0.35, "paint"), maw_in=(0.0, 0.7, "paint"), dust=(0.0, 0.9, "paint"), dust_dk=(0.0, 0.9, "paint"))
FILM = dict(worm=(0.700, 0.560, 0.360), worm_dk=(0.450, 0.330, 0.220))
K.palette(COL, SPEC, FILM, uv_scale=dict(dust=0.3, dust_dk=0.3, maw=0.3))
SPANS = ((-30.0, 90.0), (90.0, 210.0), (210.0, 330.0))     # the jaws (degrees; 0 = +v, 90 = +u / away from the camera)
BURST_REST = 400.0     # the burst rests high above the model (unoccluded for the texture bake); poses bring it down
MIDS = tuple((a + b) / 2 for a, b in SPANS)


def radius(z):
    """The closed head's outline: a shaft below the hinge, an ogive above it."""
    R, H, hz = P["R"], P["H"], P["hinge"]
    if z <= hz:
        return R
    t = (z - hz) / (H - hz)
    return R * (1.0 - t ** 1.6) ** 0.85


def sector(a0, a1, z0, z1, dr0, dr1, material, name, nz=14, na=8):
    """A shell piece of the head between the angles a0..a1 (degrees) and z0..z1, from radius + dr0 to radius + dr1."""
    rings = []
    for k in range(nz + 1):
        z = z0 + (z1 - z0) * k / nz
        r = radius(z)
        ro, ri = max(0.06, r + dr1), max(0.03, r + dr0)
        ring = [(ro * math.cos(math.radians(a0 + (a1 - a0) * j / na)), ro * math.sin(math.radians(a0 + (a1 - a0) * j / na)), z) for j in range(na + 1)]
        ring += [(ri * math.cos(math.radians(a1 - (a1 - a0) * j / na)), ri * math.sin(math.radians(a1 - (a1 - a0) * j / na)), z) for j in range(na + 1)]
        rings.append(ring)
    return K.loft(name, rings, material)


def build():
    root = K.root(KEY)
    parts = []
    R, H, hz, wall, gap = (P[k] for k in ("R", "H", "hinge", "wall", "gap"))
    rnd = random.Random(7)
    # the shaft (goes far below the ground: the head rises and sinks through it), collar, throat and teeth
    shaft = []
    shaft.append(K.tube_z("shaft", R - 0.4, 0, 0, -100.0, hz + 0.3, "worm", segs=28))
    for k in range(-24, 2):
        shaft.append(K.tube_z("shaft_ring", R + 0.15, 0, 0, hz - 1.2 + 4.0 * k, hz + 0.2 + 4.0 * k, "worm_dk" if k % 2 else "worm_lt", segs=28))
    shaft.append(K.tube_z("throat", R - wall - 0.2, 0, 0, hz - 0.5, hz + 0.9, "maw", segs=24))
    for i in range(12):
        a = 2 * math.pi * (i + 0.5) / 12
        c, s = math.cos(a), math.sin(a)
        rr = R - wall - 1.0
        shaft.append(K.tube("tooth", (rr * c, rr * s, hz + 0.5), ((rr - 2.6) * c, (rr - 2.6) * s, hz + 3.4), 0.75, "tooth", segs=6, r1=0.08))
    for o in shaft:
        parts.append(K.group(o, "head"))
    # three jaws: outer skin with segment bands, bright rims along the seams, the dark lining
    for i, mid in enumerate(MIDS):
        a0, a1 = SPANS[i][0] + gap / 2, SPANS[i][1] - gap / 2
        jaw = [sector(a0, a1, hz, H - 0.4, -wall, 0.0, "worm", "jaw"),
               sector(a0 + 0.5, a1 - 0.5, hz + 0.2, H - 1.2, -wall - 0.25, -wall + 0.1, "maw_in", "jaw_lining"),
               sector(a0 - 0.5, a0 + 3.0, hz, H - 0.6, -wall - 0.1, 0.2, "worm_lt", "jaw_rim", na=2),
               sector(a1 - 3.0, a1 + 0.5, hz, H - 0.6, -wall - 0.1, 0.2, "worm_lt", "jaw_rim", na=2)]
        z = hz + 3.4
        while z < H - 5.0:
            jaw.append(sector(a0 + 4.0, a1 - 4.0, z, z + 0.5, -0.2, 0.1, "worm_dk", "jaw_band", nz=1))
            z += 6.5
        for o in jaw:
            parts.append(K.group(o, f"jaw{i}"))
    # the ring of sand round the hole, and the burst (hidden under the ground in most frames)
    n = 22
    for k in range(n):
        a = 2 * math.pi * k / n + rnd.uniform(-0.12, 0.12)
        rr = P["dust_r"] + rnd.uniform(-2.0, 2.5)
        sz = rnd.uniform(3.4, 5.6)
        parts.append(K.group(K.ellipsoid("ring", (rr * math.cos(a), rr * math.sin(a), 0.2), (sz, sz * rnd.uniform(0.8, 1.2), sz * rnd.uniform(0.3, 0.5)),
                                         "dust" if k % 3 else "dust_dk", segs=12, rings=8), "ring"))
    for k in range(46):
        a = rnd.uniform(0, 2 * math.pi)
        rr = rnd.uniform(0.0, 15.0)
        sz = rnd.uniform(2.2, 4.6)
        parts.append(K.group(K.ellipsoid("burst", (rr * math.cos(a), rr * math.sin(a) * 0.55, BURST_REST + rnd.uniform(0.5, 3.0)), (sz * 1.3, sz, sz * rnd.uniform(0.4, 0.7)),
                                         "dust" if k % 4 else "dust_dk", segs=12, rings=8), "burst"))
    K.node(parts, "body", (0, 0, 0), root, (P["v0"], P["u0"], 0))
    return root


def pose_fn(i):
    opening, rise, ring_s, burst_s = POSES[i]
    off = Vector((P["v0"], P["u0"], 0.0))

    def about(m):       # a matrix (px, model axes) applied about the worm's axis -> tiles
        t = Matrix.Translation(off * K.PX)
        s = Matrix.Scale(K.PX, 4)
        return t @ s @ m @ s.inverted() @ t.inverted()

    def scale(sx, sz):
        return Matrix.Diagonal((sx, sx, sz, 1.0))

    def fn(root):
        body = next(o for o in root.children if o.name == "body")
        up = Matrix.Translation(Vector((0, 0, rise)))
        mats = dict(head=about(up), ring=about(scale(ring_s, min(1.0, ring_s))),
                    burst=about(scale(burst_s, burst_s * 0.9) @ Matrix.Translation(Vector((0, 0, -BURST_REST))) if burst_s > 0
                                else Matrix.Translation(Vector((0, 0, -BURST_REST - 300)))))
        for k, mid in enumerate(MIDS):
            a = math.radians(mid)
            piv = Matrix.Translation(Vector((P["R"] * math.cos(a), P["R"] * math.sin(a), P["hinge"])))
            rot = piv @ Matrix.Rotation(math.radians(opening), 4, Vector((-math.sin(a), math.cos(a), 0))) @ piv.inverted()
            mats[f"jaw{k}"] = about(up @ rot)
        rest = K.pose(body, mats)
        return lambda: K.pose(body, {}, rest)
    return fn


VARIANTS = {f"frame{i:02d}": pose_fn(i) for i in range(FRAMES)}

if __name__ == "__main__":
    K.main(KEY, "sandworm", build, P, variants=VARIANTS)
