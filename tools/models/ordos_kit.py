"""Shared kit of the Ordos buildings batch (tools/models/ordos_<name>.py): tools/models/atreides_kit.py (mesh helpers,
weathering, atlas bake, battle damage, run()) with the Ordos design language on top. Measuring / pack scripts:
tools/models/ordos_check/ (a copy of atreides_check; the building is chosen by D2K_BUILDING=ORDOS_<NAME>).

    blender -b --factory-startup --python tools/models/ordos_<name>.py -- [--out DIR] [--no-bake] [--no-variants] ...
    D2K_BUILDING=ORDOS_<NAME> .venv/bin/python -I tools/models/ordos_check/pack.py

Design language (read from the original Ordos set; mean colour of the sprites about (125, 95, 57)):
  * polished brass / gold shells - domes, barrel vaults, bulbs - with a pale yellow glint on the sunlit side that
    falls off quickly into dark bronze: about 17 % of the picture is bright gold, 65 % is bronze and dark brown;
  * dark bronze ribs and hoops over every shell, riveted rims;
  * near-black teal glass orbs with a cyan glint (lamps, sensor bulbs) on bronze collars, thin CYAN light strips
    (fixed colour, not the house colour);
  * small house-colour (Ordos green) panels, bands and domes: 4-6 % of the picture;
  * the buildings stand on a ragged brown rock skirt (the original's rocky feet), with orange rubble around.
The Atreides kit's material names are re-coloured (roof = bright gold, roof2 = dustier gold, wall = brass,
rib / rim = dark bronze, block = brown plating, white = pale gold, stone = brown rock), so its helpers (vent, flue,
hole, wall_gash ...) come out in Ordos tones.
"""
import math
import os
import random
import sys

import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import atreides_kit as AK  # noqa: E402
from atreides_kit import *  # noqa: E402,F401,F403  (box, prism, arc, sweep, vault, tank, dome, ball, cyl, pipe, flue, vent, ...)
from atreides_kit import V3, RF, lathe, lathe_loop, tube, beam, obox, polar, circle, star, rings_solid, new_obj, shade, house, log  # noqa: E402,F401

COL = dict(
    roof=(0.930, 0.700, 0.330),      # bright gold shells
    roof2=(0.800, 0.580, 0.270),     # dustier gold
    wall=(0.700, 0.500, 0.270),      # brass walls
    white=(1.000, 0.930, 0.620),     # pale gold highlights (caps, balls)
    rib=(0.330, 0.220, 0.110),       # dark bronze ribs and hoops
    rim=(0.420, 0.270, 0.120),
    block=(0.520, 0.380, 0.220),     # brown plating
    block_dk=(0.280, 0.200, 0.115),
    skirt=(0.400, 0.280, 0.165),
    stone=(0.520, 0.390, 0.240),     # the rock skirts
    stone_dk=(0.310, 0.225, 0.135),
    foot=(0.200, 0.145, 0.085),
    gold=(0.950, 0.720, 0.330),      # polished (metallic) brass: domes, bulbs
    bronze=(0.520, 0.340, 0.160),    # metallic bronze
    bronze_dk=(0.270, 0.180, 0.090),
    glass=(0.020, 0.090, 0.085),     # the teal-black orbs
    cyan=(0.100, 0.950, 0.900),      # light strips, orb glints
    cyan_dk=(0.050, 0.450, 0.450),
    tealp=(0.180, 0.420, 0.360),     # fixed dull teal plating (not the house colour)
    grey=(0.700, 0.680, 0.630),      # pale stone floors / ramps (heavy factory apron, star port pad stripe)
    grey_dk=(0.230, 0.225, 0.220),      # the landing pads' dark stone (weather class "dark": no dust film)
    padf=(1.000, 0.900, 0.420),      # the refinery pad's yellow floor
    sand=(0.780, 0.570, 0.360),      # pale sandstone banks (smuggler star port)
    sand_dk=(0.520, 0.350, 0.210),
    crag=(0.400, 0.265, 0.195),      # dark red-brown crags (the sietch)
    crag_dk=(0.170, 0.105, 0.085),
    crag_mid=(0.570, 0.400, 0.295),  # sunlit crag
    crag_hi=(0.800, 0.620, 0.470),   # its pale speckles
    cave=(0.060, 0.045, 0.080),      # the black-violet inside of the sietch
    serp=(1.000, 0.840, 0.480),      # the barracks' serpent: clean pale gold (its own name: no run-off stains)
    serp_hi=(1.000, 0.930, 0.640),
    pathf=(0.400, 0.410, 0.340),     # its trodden grey-green floor
)
SPEC = dict(
    roof=(0.35, 0.38, "paint"), roof2=(0.3, 0.45, "paint"), wall=(0.25, 0.5, "paint"), white=(0.3, 0.35, "paint"),
    rib=(0.4, 0.5, "steel"), rim=(0.4, 0.5, "steel"), block=(0.2, 0.6, "paint"), block_dk=(0.2, 0.65, "paint"),
    skirt=(0.2, 0.65, "steel"), stone=(0.0, 0.85, "paint"), stone_dk=(0.0, 0.9, "paint"),
    gold=(0.75, 0.3, "steel"), bronze=(0.6, 0.4, "steel"), bronze_dk=(0.5, 0.5, "steel"),
    glass=(0.3, 0.12, "dark"), cyan=(0.0, 0.3, "dark"), cyan_dk=(0.0, 0.4, "dark"), tealp=(0.3, 0.45, "steel"),
    grey=(0.0, 0.7, "paint"), grey_dk=(0.1, 0.75, "dark"), padf=(0.0, 0.45, "paint"),
    sand=(0.0, 0.85, "paint"), sand_dk=(0.0, 0.9, "paint"), crag=(0.0, 0.9, "dark"), crag_dk=(0.0, 0.9, "dark"),
    crag_mid=(0.0, 0.9, "dark"), crag_hi=(0.0, 0.85, "dark"), cave=(0.0, 0.95, "dark"), pathf=(0.0, 0.9, "dark"),
    serp=(0.3, 0.36, "paint"), serp_hi=(0.3, 0.34, "paint"),
)
FILM = dict(
    roof=(0.800, 0.580, 0.290), roof2=(0.680, 0.490, 0.250), wall=(0.600, 0.430, 0.240), white=(0.930, 0.820, 0.520),
    rib=(0.300, 0.210, 0.120), rim=(0.360, 0.250, 0.130), block=(0.450, 0.330, 0.200), block_dk=(0.270, 0.200, 0.120),
    skirt=(0.360, 0.260, 0.160), stone=(0.480, 0.360, 0.220), stone_dk=(0.300, 0.220, 0.135),
    gold=(0.780, 0.570, 0.280), bronze=(0.440, 0.300, 0.160), bronze_dk=(0.250, 0.175, 0.095), tealp=(0.200, 0.360, 0.300),
    grey=(0.640, 0.600, 0.540), grey_dk=(0.300, 0.280, 0.250), padf=(0.950, 0.800, 0.360),
)
V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.update(FILM)
V3.UV_SCALE.update(cyan=0.3, cyan_dk=0.3, glass=0.4)
# emission (combat_tank_v2.EMIT): the cyan light strips and glints, the teal glass orbs (lamps, sensor bulbs) dimly
V3.V2.EMIT.update(cyan=(0.15, 0.95, 0.90), cyan_dk=(0.04, 0.40, 0.40), glass=(0.05, 0.40, 0.36))
V3.GRIME_SCALE.update(white=0.4, gold=0.5, padf=0.4, grey=0.4, serp=0.25, serp_hi=0.25)
V3.FILM.update(serp=(0.960, 0.800, 0.460), serp_hi=(0.980, 0.880, 0.600))
# the render look of the batch (pack flags): harder light and deeper shadows than the Atreides set - Ordos brass
# reads by its glints against dark bronze
LOOK = dict(look="Very High Contrast", fill=(0.22, 90.0, 35.0), sky_strength=0.075, sun_strength=5.2)


def flags(exposure=-0.25, **kw):
    """Pack flags for a building's spec: the batch look + its own overrides (shadow_azimuth, ...)."""
    return dict(LOOK, exposure=exposure, **kw)
# (grey / grey_dk have no alias: pads and ramps keep their own tone, without the sand drift of "block")
AK.ALIAS.update(gold="roof", bronze="skirt", bronze_dk="skirt", tealp="skirt", padf="pad", sand="wall", sand_dk="wall")
V3.PREVIEW_HOUSE = RF.PREVIEW_HOUSE = (0.16, 0.42, 0.26)      # Ordos green


# ------------------------------------------------------------------------------------------------
# Ordos parts (px)


def orb(name, p, r, collar=True, glint=True):
    """A teal-black glass orb with a cyan glint, on a bronze collar: p = its centre (x, y, z)."""
    out = [AK.ball(f"{name}_g", p, r, "glass", 14)]
    if collar:
        out.append(AK.cyl(f"{name}_c", (p[0], p[1]), r * 0.95, p[2] - r * 0.95, p[2] - r * 0.45, "bronze_dk", 12))
    if glint:    # the glint sits up-left, towards the sun and the camera
        out.append(AK.ball(f"{name}_h", (p[0] - r * 0.42, p[1] - r * 0.30, p[2] + r * 0.62), r * 0.42, "cyan", 8))
    return out


def bulb(name, c, r, z0, h=None, material="gold", post=True):
    """A brass bulb (the onion-like balls on the corners of Ordos buildings) on a bronze post: c = (x, y), bottom z0."""
    h = r * 1.9 if h is None else h
    out = []
    if post:
        out.append(AK.cyl(f"{name}_p", c, r * 0.55, 0.0 if z0 < r else z0 - r * 0.8, z0 + r * 0.3, "bronze_dk", 10))
    prof = [(r * 0.45, z0), (r * 0.9, z0 + h * 0.22), (r, z0 + h * 0.45), (r * 0.8, z0 + h * 0.7), (r * 0.42, z0 + h * 0.9), (0.05, z0 + h)]
    out.append(lathe(f"{name}_b", c, prof, material, 14))
    out.append(lathe_loop(f"{name}_r", c, [(r * 1.0, z0 + h * 0.4), (r * 1.06, z0 + h * 0.45), (r * 1.0, z0 + h * 0.5), (r * 0.95, z0 + h * 0.45)], "bronze_dk", 14))
    return out


def rdome(name, c, r, z0, h, material="gold", ribs=8, rib="rib", band=True, segs=28, cap=None, a0=0.0):
    """A ribbed brass dome: meridian ribs, a hoop at its foot, optionally a small cap (material) on top."""
    out = [AK.dome(name, c, r, z0, h, material, segs)]
    for i in range(ribs):
        a = math.radians(a0 + 360.0 * i / ribs)
        pts = []
        for k in range(7):
            t = math.pi / 2 * k / 6.4
            pts.append((c[0] + math.cos(a) * r * math.cos(t) * 1.01, c[1] + math.sin(a) * r * math.cos(t) * 1.01, z0 + h * math.sin(t) * 1.01 + 0.05))
        for k in range(6):
            out.append(beam(f"{name}_rib{i}_{k}", pts[k], pts[k + 1], max(0.5, r * 0.07), 0.35, rib))
    if band:
        out.append(lathe_loop(f"{name}_hoop", c, [(r + 0.1, z0 - 0.5), (r + 0.45, z0 - 0.2), (r + 0.45, z0 + 0.9), (r + 0.05, z0 + 1.1)], rib, segs))
    if cap:
        out.append(AK.dome(f"{name}_cap", c, r * 0.3, z0 + h * 0.94, r * 0.22, cap, 12))
    return out


def rvault(name, a, b, r, z0, material="roof", h=None, ribs=5, rib="rib", ends=0.0, segs=16, edge=True):
    """A ribbed barrel vault a -> b (x, y points) with dark hoops across it and bronze sills along its feet."""
    h = r if h is None else h
    out = [AK.vault(name, a, b, r, z0, material, segs, h, ends=ends)]
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    for i in range(ribs):
        t = (i + 0.5) / ribs
        hw = 0.55 / L
        out.append(AK.hoop(f"{name}_rib{i}", (a[0], a[1], z0), (b[0], b[1], z0), t - hw, t + hw, r + 0.3, rib, segs, arc=(0.0, 180.0), squash=(h + 0.3) / (r + 0.3)))
    if edge:
        ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
        nx, ny = -math.sin(math.radians(ang)), math.cos(math.radians(ang))
        m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        for s in (-1, 1):
            out.append(obox(f"{name}_sill{s}", (m[0] + nx * s * r, m[1] + ny * s * r), ang, L / 2, 0.7, z0 - 0.6, z0 + 0.9, "rim", 0.15))
    return out


def strip(name, p0, p1, w=0.9, lift=0.35):
    """A cyan light strip lying on a surface between two 3D points (a dark bed with the bright bar on it)."""
    return [beam(f"{name}_bed", p0, p1, w + 0.7, lift, "recess"), beam(f"{name}_l", (p0[0], p0[1], p0[2] + lift * 0.6), (p1[0], p1[1], p1[2] + lift * 0.6), w, lift, "cyan")]


def skirt(name, pts, z1, seed=1, out_px=3.0, material="stone", dark="stone_dk", rubble=True, lumps=None):
    """The ragged rock foot under an Ordos building: polygon pts (x, y; counter-clockwise) widened by about out_px at
    the ground, battered up to z1, with faceted rock lumps along its edge. Returns a list of objects."""
    rnd = random.Random(seed)
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    dense = []
    for i, p in enumerate(pts):
        q = pts[(i + 1) % len(pts)]
        n = max(1, int(math.hypot(q[0] - p[0], q[1] - p[1]) / 5.0))
        for k in range(n):
            dense.append((p[0] + (q[0] - p[0]) * k / n, p[1] + (q[1] - p[1]) * k / n))

    def ring(z, grow, jit):
        r = []
        for j, (x, y) in enumerate(dense):
            d = math.hypot(x - cx, y - cy) or 1.0
            g = grow + (random.Random(seed * 131 + j).uniform(-1.0, 1.0) * jit)
            r.append((x + (x - cx) / d * g, y + (y - cy) / d * g, z))
        return r
    bm = bmesh.new()
    rings_solid(bm, [ring(0.0, out_px, out_px * 0.45), ring(z1 * 0.55, out_px * 0.45, out_px * 0.3), ring(z1, 0.0, 0.0)])
    out = [new_obj(name, bm, material, 0.2)]
    n = len(dense) // 2 if lumps is None else lumps
    for i in range(n):
        x, y = dense[rnd.randrange(len(dense))]
        d = math.hypot(x - cx, y - cy) or 1.0
        g = out_px * rnd.uniform(0.2, 0.9)
        q = (x + (x - cx) / d * g, y + (y - cy) / d * g)
        rr = rnd.uniform(1.2, 2.6)
        hh = rnd.uniform(0.35, 0.9) * z1
        sp = star(q, rr, seed * 7 + i, 6)
        tp = star((q[0] - (x - cx) / d * rr * 0.4, q[1] - (y - cy) / d * rr * 0.4), rr * 0.5, seed * 9 + i, 6)
        bm = bmesh.new()
        rings_solid(bm, [[(a, b, 0.0) for (a, b) in sp], [(a, b, hh) for (a, b) in tp]])
        out.append(new_obj(f"{name}_l{i}", bm, dark if i % 3 == 0 else material, 0.15))
    return out


def rubble(name, c, r, seed, n=4):
    """Orange sandstone rubble (the original's debris piles at the feet)."""
    return AK.rocks(name, c, r, seed, n)


def spire(name, c, z0, z1, r=0.6, material="bronze_dk", knob="gold"):
    """A thin mast with a small knob."""
    return [tube(f"{name}_m", (c[0], c[1], z0), (c[0], c[1], z1), r, material, 8), AK.ball(f"{name}_k", (c[0], c[1], z1 + r), r * 1.5, knob, 8)]


def inlet(name, mouth=(14.5, 2.6, 16.3), length=2.4, r_out=2.7, r_in=2.15, r_lip=3.1):
    """The harvester's unloading socket (as the Atreides refinery's, tools/models/refinery_pilot_fable.py inlet()):
    a steel socket whose mouth is at the point where tools/models/harvester_v1.py's pipe ends on the dock, its
    axis pointing horizontally south-east. Goes in the MAIN node (drawn over the docked harvester).
    -> (objects, the back point of the socket (x, y, z) to plumb it to something)."""
    from mathutils import Vector
    d = Vector((math.sqrt(0.5), -math.sqrt(0.5), 0.0))
    o = Vector(mouth) - d * length
    side = Vector((d.y, -d.x, 0.0))

    def ring(t, r, segs=18):
        return [tuple(o + d * t + (side * math.cos(2 * math.pi * i / segs) + Vector((0, 0, 1)) * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
    prof = [(0.0, r_in), (0.0, r_out), (length - 0.9, r_out), (length - 0.5, r_lip), (length, r_lip), (length, r_in)]
    bm = bmesh.new()
    rs = [[bm.verts.new(p) for p in ring(t, r)] for (t, r) in prof]
    for a, b in zip(rs, rs[1:] + rs[:1]):
        for i in range(len(a)):
            j = (i + 1) % len(a)
            bm.faces.new([a[i], a[j], b[j], b[i]])
    out = [new_obj(f"{name}_socket", bm, "inlet")]
    bm = bmesh.new()
    rings_solid(bm, [ring(0.2, r_in + 0.02), ring(length - 1.6, r_in + 0.02)])
    out.append(new_obj(f"{name}_bore", bm, "burnt"))
    bm = bmesh.new()
    rings_solid(bm, [ring(length - 2.0, r_out + 0.18), ring(length - 1.45, r_out + 0.18)])
    out.append(new_obj(f"{name}_band", bm, "bronze_dk"))
    return out, tuple(o)


# ------------------------------------------------------------------------------------------------


def run(key, build, poses=(), spec=None, P=None, rest=None):
    """atreides_kit.run() with the Ordos batch's output folder: artpacks/models-ordos-<name> (name = the key without
    "ORDOS_", lower case; other houses' buildings done in this batch keep their prefix: fremen_barracks)."""
    if "--" not in sys.argv:
        sys.argv.append("--")
    if "--out" not in sys.argv:
        short = key.replace("ORDOS_", "").lower()
        sys.argv += ["--out", os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-ordos-" + short)]
    AK.run(key, build, poses=poses, spec=spec, P=P, rest=rest)
