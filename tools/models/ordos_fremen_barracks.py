"""Fremen barracks (the sietch) - the original design rebuilt as SCULPTED ROCK (second attempt; done in the Ordos batch
with tools/models/ordos_kit.py).

    blender -b --factory-startup --python tools/models/ordos_fremen_barracks.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=FREMEN_BARRACKS .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 54, 2 x 2 cells: main 3880; the art has NO base, NO damaged frame, no animation):
  * one round outcrop of mottled orange-brown rock, about 58 px across, highest at the back (north), with a ragged
    outline; paler on its east and south flanks, dark in the north-west;
  * in its middle the wide black mouth of the cave (about 28 x 22 px), its floor a little paler at the front;
  * a trodden grey-green floor runs out of the mouth to the south-south-west through a gap in the rock.
The first attempt (a horseshoe of prisms round a black disc) read as a ball in a ring. This one is a heightfield
sculpted with fractal noise: a ring-shaped ridge with a steep inner cliff, strata and boulders, its faces coloured
by height, slope and noise in four rock tones (the original's pixel-scale mottling), the pit floor and inner cliff
black-violet, the path its own surface. No regular solids.
"""
import math
import os
import sys

import bmesh
from mathutils import Vector, noise

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "FREMEN_BARRACKS"
# All of the sietch's materials are of the weather class "dark": switch its cavity dirt off, or the inside of the cave
# (one big cavity) bakes grey-olive instead of black. The rock's mottling comes from the face tones.
K.V3.WEATHER["dark"] = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
P = dict(
    c=(0.0, 2.5), rx=27.5, ry_n=36.5, ry_s=38.5, sq=2.9,          # the outcrop's plan: centre, half width, reach north / south
    pit=(0.0, 8.0), pit_rx=14.0, pit_ry=9.0,             # the cave mouth
    h_n=10.0, h_s=5.0, h_e=8.5,                          # crest height at the north, the south, extra at the east
    path_to=(-8.0, -31.0), path_w=7.0,                    # the path: from the pit towards this point, half width
    step=1.25, seed=3.0,
)


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): firelight from inside the sietch - the
# cave's floor and inner wall glow warm (the 2D glow layer), and the dark rock faces round the mouth catch it
# (their red-brown albedo is what the 3D shader tints the glow with; the black-violet cave would glow violet there,
# so its 3D emission is off (lit() k3d 0) and 3D gets a warm fx_light on the cave's back wall instead).
GLOW = dict(floor=K.AK.lit("cave_fire", "cave", (0.045, 0.015, 0.004), 0.0), wall=K.AK.lit("cave_glow", "cave", (0.20, 0.075, 0.02), 0.0),
            rock=K.AK.lit("crag_fire", "crag_dk", (0.30, 0.12, 0.04), 0.5))


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def fbm(x, y, scale, octaves=4, z=0.0):
    return noise.fractal(Vector((x / scale, y / scale, z + P["seed"])), 1.0, 2.0, octaves)


def field(x, y):
    """-> (height, kind): kind 0 rock, 1 pit (floor / inner cliff), 2 path, -1 outside."""
    c, pit = P["c"], P["pit"]
    dx, dy = x - c[0], y - c[1]
    th = math.atan2(dy, dx)
    ry = P["ry_n"] if dy > 0 else P["ry_s"]
    R = 1.0 + 0.10 * fbm(math.cos(th) * 30, math.sin(th) * 30, 14.0, 3) + 0.05 * fbm(x, y, 5.0, 2)      # ragged outline
    r = (abs(dx / P["rx"]) ** P["sq"] + abs(dy / ry) ** P["sq"]) ** (1.0 / P["sq"]) / R      # 1 at the outline (a squarish oval)
    px, py = (x - pit[0]) / P["pit_rx"], (y - pit[1]) / P["pit_ry"]
    q = math.hypot(px, py) / (1.0 + 0.12 * fbm(x, y, 7.0, 2, 5.0))    # 1 at the pit's edge
    # the path corridor: distance to the segment pit -> path_to
    ax, ay = P["path_to"][0] - pit[0], P["path_to"][1] - pit[1]
    L2 = ax * ax + ay * ay
    t = max(0.0, min(1.0, ((x - pit[0]) * ax + (y - pit[1]) * ay) / L2))
    dpath = math.hypot(x - pit[0] - ax * t, y - pit[1] - ay * t) + 1.5 * fbm(x, y, 6.0, 2, 9.0)
    w = P["path_w"] * (1.0 - 0.25 * t)
    if r > 1.0 and not (dpath < w and t < 0.97):
        return 0.0, -1
    if q < 1.0:
        return 0.3 + 0.5 * smooth(0.6, 1.0, q), 1
    if dpath < w:
        return 0.5 + 0.4 * fbm(x, y, 4.0, 2, 3.0), 2
    north = max(0.0, math.sin(th))
    east = max(0.0, math.cos(th))
    hmax = P["h_s"] + (P["h_n"] - P["h_s"]) * north ** 0.8 + (P["h_e"] - P["h_s"]) * east * (1 - north)
    # ring profile: a steep inner cliff at the pit, the crest close behind it, a long ragged outer slope
    inner = smooth(1.0, 1.25, q)
    outer = 1.0 - smooth(0.84, 1.0, r) ** 1.5              # a broad top, then down to the sand
    h = hmax * inner * (0.2 + 0.8 * outer)
    h *= 0.8 + 0.45 * fbm(x, y, 9.0, 4)                      # boulders
    h += 1.6 * fbm(x, y, 3.0, 3, 2.0)                         # rubble
    strata = round(h / 3.2) * 3.2
    h = h + (strata - h) * 0.45                               # ledges
    h *= smooth(w, w + 5.0, dpath)                            # the gap the path runs through
    return max(0.25, h), 0


def build(damaged=False):
    st = P["step"]
    n = int(42 / st)
    H, Kd = {}, {}
    for j in range(-n, n + 1):
        for i in range(-n, n + 1):
            H[i, j], Kd[i, j] = field(i * st, j * st)
    tones = ("crag_dk", "crag", "crag_mid", "crag_hi", "cave", "pathf")
    bms = {t: bmesh.new() for t in tones}
    for j in range(-n, n):
        for i in range(-n, n):
            corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            kinds = [Kd[c] for c in corners]
            if all(k < 0 for k in kinds):
                continue
            x, y = (i + 0.5) * st, (j + 0.5) * st
            hs = [H[c] for c in corners]
            hm = sum(hs) / 4
            slope_x = (hs[1] + hs[2] - hs[0] - hs[3]) / 2 / st      # rises towards the east
            slope_y = (hs[2] + hs[3] - hs[0] - hs[1]) / 2 / st      # rises towards the north
            if kinds.count(1) >= 2:
                tone = "cave"
            elif kinds.count(2) >= 3:
                tone = "pathf"
            else:
                # faces turned to the south / east are sunlit and pale, the north-west and the inner cliff dark;
                # fine noise gives the original's pixel mottling
                lit = 0.42 - 0.05 * slope_y + 0.03 * slope_x + 0.013 * x - 0.007 * y
                lit += 0.42 * noise.noise(Vector((x / 1.5, y / 1.5, 7.0))) + 0.25 * fbm(x, y, 6.0, 2, 4.0)
                if 1 in kinds:
                    lit -= 0.5
                tone = "crag_dk" if lit < 0.2 else "crag" if lit < 0.48 else "crag_mid" if lit < 0.78 else "crag_hi"
            bm = bms[tone]
            vs = [bm.verts.new((c[0] * st, c[1] * st, H[c] if Kd[c] >= 0 else 0.0)) for c in corners]
            f = bm.faces.new(vs)
            if tone == "crag_dk" and 1 in kinds:        # the inner cliff at the mouth: lit by the fire (slot 1)
                f.material_index = 1
    main = []
    for t in tones:
        if bms[t].faces:
            bmesh.ops.remove_doubles(bms[t], verts=bms[t].verts, dist=1e-4)
            main.append(K.new_obj("rock_" + t, bms[t], t))
            if t == "crag_dk":
                main[-1].data.materials.append(K.V3.mat(GLOW["rock"]))
    # The cave and the path as their own big surfaces (in the atlas the 1 px rock faces bleed into each other -
    # right for mottled rock, wrong for the black mouth): the pit floor, the dark inner cliff as a ragged wall just
    # inside the pit's edge (as high as the rock behind it), the trodden floor of the path.
    pit = P["pit"]
    m = 40

    def edge(k, f=0.985):
        a = 2 * math.pi * k / m
        x, y = pit[0] + math.cos(a) * P["pit_rx"] * f, pit[1] + math.sin(a) * P["pit_ry"] * f
        return x, y, a
    bm = bmesh.new()
    K.rings_solid(bm, [[(edge(k)[0], edge(k)[1], 0.0) for k in range(m)], [(edge(k)[0], edge(k)[1], 0.95) for k in range(m)]])
    main.append(K.new_obj("cave_floor", bm, GLOW["floor"]))
    bm = bmesh.new()
    lo, hi = [], []
    for k in range(m + 1):
        x, y, a = edge(k % m, 1.0)
        ox, oy = pit[0] + math.cos(a) * P["pit_rx"] * 1.32, pit[1] + math.sin(a) * P["pit_ry"] * 1.32
        top = max(1.0, field(ox, oy)[0] * 0.93) if field(ox, oy)[1] == 0 else 1.0
        lo.append(bm.verts.new((x, y, 0.5)))
        hi.append(bm.verts.new((x + math.cos(a) * 1.6, y + math.sin(a) * 1.6, top)))
    for k in range(m):
        bm.faces.new([lo[k], lo[k + 1], hi[k + 1], hi[k]])
    main.append(K.new_obj("cave_wall", bm, GLOW["wall"]))
    ax, ay = P["path_to"][0] - pit[0], P["path_to"][1] - pit[1]
    L = math.hypot(ax, ay)
    ux, uy = ax / L, ay / L
    left, right = [], []
    for k in range(13):
        t = 0.34 + 0.56 * k / 12
        w = P["path_w"] * (1.0 - 0.25 * t) - 0.6
        cx, cy = pit[0] + ax * t, pit[1] + ay * t
        j = 0.9 * noise.noise(Vector((k * 0.7, 3.0, P["seed"])))
        left.append((cx - uy * (w + j), cy + ux * (w + j)))
        right.append((cx + uy * (w - j), cy - ux * (w - j)))
    main.append(K.prism("path", left + right[::-1], 0.0, 1.05, "pathf", 0.15))
    # the fire inside (3D: a warm lamp on the cave's back wall, half way up the inner cliff)
    return dict(nodes=dict(main=main), anchors=[("fx_light_0", (pit[0], pit[1] + P["pit_ry"] + 0.6, 5.0), 1.2)])


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(damaged=False, flags=K.flags(-0.1, shadow_azimuth=30.0, contour=0.0)), P=P)
