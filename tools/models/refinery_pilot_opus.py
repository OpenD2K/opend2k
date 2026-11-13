"""Atreides spice refinery - the ORIGINAL design rebuilt as a modern hard-surface model (pilot, Opus).
Same method and kit as combat_tank_v3.py / devastator_v1.py (their mesh helpers, weathering and atlas bake).

    blender -b --factory-startup --python tools/models/refinery_pilot_opus.py -- \
        [--out DIR]            write DIR/models/building/ATREIDES_REFINERY.glb (nodes "base" + "main", the split
                               the original draws: default artpacks/models-refinery-opus)
        [--out-whole DIR]      the same model with no base node (render_pack then renders it as ONE view - lit and
                               shadowed as a whole - which compose_pack.py splits into the base / main frames)
        [--no-bake]            plain colours instead of the baked atlas (fast iteration)
        [--bake-size 2048]
        [--set key=value ...]  override a proportion in P (px)
        [--turntable DIR]      review renders (house colour previewed Atreides blue)
        [--samples 64] [--device cpu|gpu]

    2D pack (docs/art-pipeline.md "Rendered from 3D"), see tools/models/refinery_opus_check/build.sh:
      render_pack.py on --out-whole (colour + shadow of the whole refinery) and on --out (the main node alone:
      only its coverage is used), then refinery_opus_check/compose_pack.py writes artpacks/rendered-refinery-opus.

What the game draws (re/notes/rendering.md §3, DATA.R16 frames 3105-3107, Templates.bin ATREIDES REFINERY): a
finished, idle refinery is TWO layers - the base frame (pass A, under every unit: the drum WALLS with their arches
and house band, the blue tank, the low platform and the yellow unloading pad) and the main frame (Y-sorted with the
units: the drum ROOFS, the poles and chimney, the centre block, the cupola, the tall pipe and the drums' ground
shadow). Its template flags (0x200700) have no HAS_ANIMATION bit, so nothing animates on the building; a
harvester unloading draws its own direction-less EX_HARV frames on the pad (they stay original), which is why the
pad sits exactly where the original's is and why the walls must stay in the base frame (a docked harvester at
cell (2, 1) is drawn BEFORE the main frame).

Conventions: Blender units = tiles, +X east, +Y north, +Z up, origin on the ground at the centre of the 3 x 3
footprint (the grid rule of render_pack: x 1:1, y compressed by sin 45 about that point). Everything below is in
ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up), read off the composite
with the 45 deg camera: screen x = 48 + x, screen y = 48 - 0.707 (y + z) in building pixels.
"""
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402  generic helpers (join, bake, camera)
import combat_tank_v3 as V3  # noqa: E402  the kit: mesh helpers, materials, weathering, atlas

KEY = "ATREIDES_REFINERY"
PX = V3.PX
log = V2.log
srgb_to_linear = V2.srgb_to_linear

P = dict(
    # the two processing drums (centre x, y; radius; heights)
    drumL=(-21.5, -1.5, 20.0), drumR=(24.0, 32.5, 20.5), r=20.5,   # x, y, radius
    plinth_r0=25.2, plinth_h=10.5,            # plinth: a skirt flaring out to the ground
    band=(11.0, 14.6),                         # house band
    arch=(14.6, 24.2), bays=16, pier_w=1.6, spring=20.2,   # arched wall: sill .. lintel top, bay count, pier width
    lip=(24.0, 25.6), lip_in=1.9,              # dark roof lip (ring, base frame; width)
    roof=(24.9, 28.4),                         # roof: rim height .. centre height (shallow cone)
    ribs=24, hub_r=3.4,
    # left drum: twin poles; right drum: chimney with a railed platform and the orange arm
    poleA=(-21.6, -0.4, 59.5), poleB=(-18.5, -2.6, 55.5), pole_r=1.35,
    chim=(24.0, 32.0, 60.5), chim_r=2.1, chim_deck=51.5,
    arm=((24.0, 29.5, 29.6), (34.0, 13.5, 26.6)),
    # centre block between the drums (on the line through their centres), cupola behind it
    block=(0.0, 16.0), block_len=22.0, block_w=13.0, block_h=14.0, block_roof=(14.6, 22.0),
    cupola=(-7.5, 31.0), cupola_r=6.6, cupola_h=21.0,
    tank=(10.0, 12.0), tank_r=4.3, tank_h=15.0, pipe_r=2.3, pipe_top=64.0,
    strut=((16.0, 4.0, 21.0), (12.0, 9.0, 0.0)),
    # unloading pad: a square turned 45 deg (the harvester backs on facing south-east), centre and side
    pad=(27.0, 0.0), pad_side=33.0, pad_h=2.4, pad_inset=3.4,
    grow=1.0,
)

COL = dict(
    cream=(0.950, 0.900, 0.780),     # roof segments
    rib=(0.760, 0.660, 0.520),       # radial roof ribs
    lip=(0.360, 0.270, 0.180),       # dark brown roof lip
    hub=(0.420, 0.330, 0.230),
    wall=(0.940, 0.910, 0.840),      # piers, lintel, spandrels: warm white
    arch=(0.290, 0.250, 0.200),      # the recess behind the arches
    plinth=(0.860, 0.810, 0.720),
    slat=(0.560, 0.490, 0.390),
    copper=(0.560, 0.400, 0.250),    # poles, chimney, pipe, strut
    copper_dk=(0.230, 0.160, 0.100),  # caps, collars
    pipe_or=(0.880, 0.520, 0.180),   # the orange arm
    block=(0.880, 0.840, 0.750),
    concrete=(0.640, 0.610, 0.550),
    pad=(0.960, 0.790, 0.380),       # unloading pad: yellow
    pad_frame=(0.940, 0.900, 0.810),
    lamp=(1.000, 0.980, 0.900),
    HOUSE=(0.80, 0.80, 0.80),
)
SPEC = dict(
    cream=(0.0, 0.40, "paint"), rib=(0.0, 0.50, "paint"), lip=(0.0, 0.55, "paint"), hub=(0.2, 0.5, "steel"),
    wall=(0.0, 0.40, "paint"), arch=(0.0, 0.90, "dark"), plinth=(0.0, 0.60, "paint"), slat=(0.0, 0.70, "dark"),
    copper=(0.5, 0.40, "steel"), copper_dk=(0.4, 0.55, "steel"), pipe_or=(0.3, 0.40, "steel"),
    block=(0.0, 0.45, "paint"), concrete=(0.0, 0.80, "paint"), pad=(0.0, 0.45, "paint"),
    pad_frame=(0.0, 0.45, "paint"), lamp=(0.0, 0.20, "paint"), HOUSE=(0.0, 0.32, "house"),
)
FILM = dict(
    cream=(0.840, 0.780, 0.670), rib=(0.640, 0.550, 0.430), wall=(0.840, 0.800, 0.720), plinth=(0.640, 0.570, 0.470),
    block=(0.780, 0.740, 0.660), concrete=(0.580, 0.550, 0.500), pad=(0.860, 0.710, 0.380),
    pad_frame=(0.820, 0.780, 0.690), copper=(0.500, 0.380, 0.250), pipe_or=(0.760, 0.480, 0.200),
    lip=(0.380, 0.300, 0.210),
)
PREVIEW_HOUSE = (0.33, 0.45, 0.65)
KEEP = ("HOUSE",)

V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.update(FILM)
V3.SOOT.clear()
V3.GRIME_SCALE.update(pad=0.6, pad_frame=0.6)
V3.UV_SCALE.update(arch=0.3, slat=0.4, copper_dk=0.4, lamp=0.1, rib=0.6, HOUSE=0.06)
V3.P["house_low"] = 0.7
V3.WEATHER["paint"] = (0.60, 0.45, 0.85, 0.45, 0.55, 0.55, 0.35, 0.40)

new_obj, hull_pts = V3.new_obj, V3.hull_pts


def shade(o, k):
    o["d2k_shade"] = k
    return o


# ------------------------------------------------------------------------------------------------
# Mesh helpers (px: x east, y north, z up)


def lathe(name, prof, c, material, segs=48, closed=True):
    """Solid of revolution about the vertical axis through c of a closed (r, z) profile."""
    bm = bmesh.new()
    rings = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        ca, sa = math.cos(a), math.sin(a)
        rings.append([bm.verts.new((c[0] + max(r, 0.005) * ca, c[1] + max(r, 0.005) * sa, z)) for r, z in prof])
    n = len(prof)
    for i in range(segs):
        A, B = rings[i], rings[(i + 1) % segs]
        for j in range(n if closed else n - 1):
            k = (j + 1) % n
            bm.faces.new([A[j], B[j], B[k], A[k]])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    return new_obj(name, bm, material, 0.0)


def vcyl(name, r, x, y, z0, z1, material, segs=16, r1=None, bev=0.0):
    rr = r if r1 is None else r1
    return lathe(name, [(0.0, z0), (r, z0), (rr, z1), (0.0, z1)], (x, y), material, segs)


def tube(name, p0, p1, r, material, segs=12):
    p0, p1 = Vector(p0), Vector(p1)
    d = (p1 - p0).normalized()
    a = Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0))
    u = d.cross(a).normalized()
    v = d.cross(u).normalized()
    bm = bmesh.new()
    rings = []
    for p in (p0, p1):
        rings.append([tuple(p + r * (math.cos(2 * math.pi * i / segs) * u + math.sin(2 * math.pi * i / segs) * v))
                      for i in range(segs)])
    V3.rings_solid(bm, rings)
    return new_obj(name, bm, material, 0.0)


def radial(c, ang, r, s):
    """Point at radius r, tangential offset s from the direction ang about c (plan)."""
    ca, sa = math.cos(ang), math.sin(ang)
    return (c[0] + r * ca - s * sa, c[1] + r * sa + s * ca)


def radial_solid(name, c, ang, prof_sz, r0, r1, material):
    """A (s, z) polygon extruded radially from r0 to r1 at direction ang (arched spandrels, piers, slats)."""
    bm = bmesh.new()
    rings = [[(*radial(c, ang, r, s), z) for (s, z) in prof_sz] for r in (r0, r1)]
    V3.rings_solid(bm, rings)
    return new_obj(name, bm, material, 0.0)


def obox(name, cx, cy, ang, L, W, z0, z1, material, z1b=None, bev=0.0):
    """Box along direction ang (L along, W across); top z1 on the -W side rising to z1b on the +W side."""
    ca, sa = math.cos(ang), math.sin(ang)
    pts = []
    for sl in (-L / 2, L / 2):
        for sw in (-W / 2, W / 2):
            x, y = cx + sl * ca - sw * sa, cy + sl * sa + sw * ca
            pts.append((x, y, z0))
            pts.append((x, y, z1 if (z1b is None or sw < 0) else z1b))
    return hull_pts(name, pts, material, bev, 2)


# ------------------------------------------------------------------------------------------------
# Parts


def drum_walls(parts, c, tag, skip=None):
    """Plinth, house band and the arched wall of one drum (base frame). skip(ang): bays hidden by the block."""
    R = c[2]
    z_b0, z_b1 = P["band"]
    z_s, z_t = P["arch"]
    ph = P["plinth_h"]
    parts.append(lathe(f"{tag}_plinth", [(0, 0), (P["plinth_r0"], 0), (P["plinth_r0"] - 0.3, 1.2), (R + 0.6, ph), (0, ph)],
                       c, "plinth", 64))
    for k in range(44):     # vertical slats on the plinth
        a = 2 * math.pi * (k + 0.5) / 44
        parts.append(hull_pts(f"{tag}_slat{k}", [
            (*radial(c, a, P["plinth_r0"] - 0.35, s), 1.3) for s in (-0.6, 0.6)] + [
            (*radial(c, a, P["plinth_r0"] + 0.25, s), 1.3) for s in (-0.6, 0.6)] + [
            (*radial(c, a, R + 0.45, s), ph - 0.6) for s in (-0.6, 0.6)] + [
            (*radial(c, a, R + 1.0, s), ph - 0.6) for s in (-0.6, 0.6)], "slat"))
    parts.append(shade(lathe(f"{tag}_band", [(R - 0.5, z_b0), (R + 0.95, z_b0), (R + 0.95, z_b1), (R - 0.5, z_b1)],
                             c, "HOUSE", 64), 0.78))
    # recess behind the arches, sill, lintel
    parts.append(lathe(f"{tag}_core", [(0, z_s - 0.2), (R - 1.4, z_s - 0.2), (R - 1.4, z_t), (0, z_t)], c, "arch", 64))
    lt0 = min(z_t - 0.5, P["spring"] + 0.5 * (2 * math.pi * R / P["bays"] - P["pier_w"]) + 0.3)
    parts.append(lathe(f"{tag}_lintel", [(R - 1.4, lt0), (R + 0.45, lt0), (R + 0.45, z_t), (R - 1.4, z_t)], c, "wall", 64))
    n = P["bays"]
    bay = 2 * math.pi * R / n
    w = bay - P["pier_w"]
    for k in range(n):
        a = 2 * math.pi * k / n + math.pi / n
        if skip and skip(a):
            continue
        # piers between bays
        ap = a + math.pi / n
        parts.append(radial_solid(f"{tag}_pier{k}", c, ap, [(-P["pier_w"] / 2, z_s), (P["pier_w"] / 2, z_s),
                                                            (P["pier_w"] / 2, lt0), (-P["pier_w"] / 2, lt0)],
                                  R - 1.4, R + 0.4, "wall"))
        # spandrels: the arch head (rectangle minus a half circle)
        hw = w / 2 + 0.05
        prof = [(-hw, lt0 + 0.05), (-hw, P["spring"])]
        for i in range(1, 12):
            t = math.pi - math.pi * i / 12
            prof.append((hw * math.cos(t) * 0.98, P["spring"] + hw * math.sin(t) * 0.98))
        prof += [(hw, P["spring"]), (hw, lt0 + 0.05)]
        parts.append(radial_solid(f"{tag}_spandrel{k}", c, a, prof, R - 1.3, R + 0.1, "wall"))


def drum_lip(parts, c, tag):
    R = c[2]
    l0, l1 = P["lip"]
    parts.append(lathe(f"{tag}_lip", [((R - P["lip_in"]), l0), (R + 0.7, l0), (R + 0.8, l1 - 0.5), (R + 0.3, l1),
                                      ((R - P["lip_in"]), l1)], c, "lip", 64))


def drum_roof(parts, c, tag):
    R = c[2]
    z0, z1 = P["roof"]
    rr = (R - P["lip_in"]) + 0.2
    parts.append(lathe(f"{tag}_roof", [(0, z0 - 0.6), (rr, z0 - 0.6), (rr, z0), (0, z1)], c, "cream", 64))
    for k in range(P["ribs"]):
        a = 2 * math.pi * (k + 0.25) / P["ribs"]

        def zs(r):
            return z0 + (z1 - z0) * (1 - r / rr)
        pts = []
        for r in (P["hub_r"] - 0.2, rr - 0.1):
            for s in (-0.38, 0.38):
                x, y = radial(c, a, r, s)
                pts += [(x, y, zs(r) - 0.3), (x, y, zs(r) + 0.5)]
        parts.append(hull_pts(f"{tag}_rib{k}", pts, "rib"))
    parts.append(vcyl(f"{tag}_hub", P["hub_r"], c[0], c[1], z1 - 1.0, z1 + 1.0, "hub", 20))


def main_parts(parts):
    cL, cR = P["drumL"], P["drumR"]
    drum_roof(parts, cL, "L")
    drum_roof(parts, cR, "R")
    # twin poles on the left drum
    zr = P["roof"][1]
    for nm, (x, y, zt) in (("poleA", P["poleA"]), ("poleB", P["poleB"])):
        r = P["pole_r"]
        parts.append(vcyl(nm, r, x, y, zr - 2, zt - 1.2, "copper", 14))
        parts.append(vcyl(nm + "_cap", r * 1.3, x, y, zt - 1.2, zt, "copper_dk", 14))
        parts.append(vcyl(nm + "_collar", r * 1.4, x, y, zr + 6, zr + 7.2, "copper_dk", 14))
        parts.append(vcyl(nm + "_foot", r * 1.9, x, y, zr - 0.5, zr + 1.6, "copper_dk", 14))
    # chimney with a railed platform
    x, y, zt = P["chim"]
    r = P["chim_r"]
    zd = P["chim_deck"]
    parts.append(vcyl("chim", r, x, y, zr - 2, zt - 1.6, "copper", 18))
    parts.append(vcyl("chim_cap", r * 1.35, x, y, zt - 1.6, zt, "copper_dk", 18))
    parts.append(vcyl("chim_foot", r * 1.9, x, y, zr - 0.5, zr + 2.2, "copper_dk", 18))
    parts.append(lathe("chim_deck", [(r, zd), (4.6, zd), (4.6, zd + 0.8), (r, zd + 0.8)], (x, y), "wall", 24))
    for k in range(8):
        a = 2 * math.pi * k / 8
        parts.append(vcyl(f"chim_post{k}", 0.3, x + 4.2 * math.cos(a), y + 4.2 * math.sin(a), zd + 0.8, zd + 3.2, "wall", 6))
    parts.append(lathe("chim_rail", [(3.95, zd + 2.9), (4.5, zd + 2.9), (4.5, zd + 3.4), (3.95, zd + 3.4)], (x, y), "wall", 24))
    # the orange arm from the chimney's foot to the rim (towards the pad) with a bracket at its end
    (a0, a1) = P["arm"]
    parts.append(tube("arm", a0, a1, 0.95, "pipe_or"))
    parts.append(hull_pts("arm_bracket", [(a1[0] + dx, a1[1] + dy, a1[2] + dz) for dx in (-1.3, 1.3)
                                          for dy in (-1.3, 1.3) for dz in (-2.2, 0.9)], "copper_dk"))
    # centre block: walls, a sloped house-colour roof with a lamp
    bx, by = P["block"]
    ang = math.atan2(cR[1] - cL[1], cR[0] - cL[0])
    L, W, H = P["block_len"], P["block_w"], P["block_h"]
    r0, r1 = P["block_roof"]
    parts.append(shade(obox("block_roof", bx, by, ang, L + 1.0, W + 1.2, H - 0.4, r0, "HOUSE", z1b=r1), 0.42))
    # roof plate ribs + lamp
    ca, sa = math.cos(ang), math.sin(ang)
    for sl in (-L / 2 + 2.5, L / 2 - 2.5):
        parts.append(obox(f"block_rib{sl:+.0f}", bx + sl * ca, by + sl * sa, ang, 0.9, W + 1.4, H - 0.2, r0 + 0.5,
                          "copper_dk", z1b=r1 + 0.5))
    ln = (bx - 0.5 * sa, by + 0.5 * ca)
    zl = (r0 + r1) / 2 + 0.5
    parts.append(obox("lamp", ln[0], ln[1], ang, 2.2, 2.2, zl - 1.0, zl + 1.0, "lamp"))
    # cupola
    cx, cy = P["cupola"]
    cr, ch = P["cupola_r"], P["cupola_h"]
    prof = [(0, 0), (cr, 0), (cr, ch)]
    for i in range(1, 9):
        t = math.pi / 2 * i / 9
        prof.append((cr * math.cos(t), ch + cr * 0.85 * math.sin(t)))
    prof.append((0, ch + cr * 0.85))
    parts.append(lathe("cupola", prof, (cx, cy), "block", 32))
    parts.append(lathe("cupola_ring", [(cr - 0.2, ch - 1.2), (cr + 0.5, ch - 1.2), (cr + 0.5, ch), (cr - 0.2, ch)],
                       (cx, cy), "copper_dk", 32))
    parts.append(vcyl("cupola_vent", 1.0, cx, cy, ch + cr * 0.8, ch + cr * 0.85 + 1.6, "copper_dk", 10))
    # the tall pipe above the tank
    tx, ty = P["tank"]
    parts.append(vcyl("pipe", P["pipe_r"], tx, ty, P["tank_h"], P["pipe_top"] - 1.4, "copper", 16))
    parts.append(vcyl("pipe_cap", P["pipe_r"] * 1.35, tx, ty, P["pipe_top"] - 1.4, P["pipe_top"], "copper_dk", 16))
    parts.append(vcyl("pipe_collar", P["pipe_r"] * 1.45, tx, ty, 31.0, 32.4, "copper_dk", 16))


def base_parts(parts):
    cL, cR = P["drumL"], P["drumR"]
    ang_lr = math.atan2(cR[1] - cL[1], cR[0] - cL[0])

    def hidden_by_block(c, toward):
        def f(a):   # bays facing the block (inside the centre block) are skipped
            d = math.atan2(toward[1] - c[1], toward[0] - c[0])
            return abs((a - d + math.pi) % (2 * math.pi) - math.pi) < math.radians(22)
        return f
    drum_walls(parts, cL, "L", hidden_by_block(cL, P["block"]))
    drum_walls(parts, cR, "R", hidden_by_block(cR, P["block"]))
    drum_lip(parts, cL, "L")
    drum_lip(parts, cR, "R")
    bx, by = P["block"]
    parts.append(obox("block", bx, by, ang_lr, P["block_len"], P["block_w"], 0.0, P["block_h"], "block", bev=0.4))
    # low platform between the left drum, the tank and the pad
    plat = [(-6.0, -12.0), (10.0, -9.0), (16.0, 4.0), (15.0, 20.0), (1.0, 24.0), (-8.0, 8.0)]
    parts.append(hull_pts("platform", [(x, y, z) for x, y in plat for z in (0.0, 2.2)], "concrete", 0.3, 2))
    # blue tank (house, dark shade) with a white top ring and dark hoops
    tx, ty = P["tank"]
    tr, th = P["tank_r"], P["tank_h"]
    parts.append(shade(vcyl("tank", tr, tx, ty, 0.0, th, "HOUSE", 24), 0.5))
    parts.append(lathe("tank_ring", [(tr - 0.6, th - 0.6), (tr + 0.4, th - 0.6), (tr + 0.4, th + 1.0), (tr - 0.6, th + 1.0)],
                       (tx, ty), "wall", 24))
    parts.append(vcyl("tank_lid", tr - 0.5, tx, ty, th - 0.2, th + 0.6, "copper_dk", 24))
    for zz in (4.0, 9.0):
        parts.append(lathe(f"tank_hoop{zz:.0f}", [(tr - 0.2, zz), (tr + 0.35, zz), (tr + 0.35, zz + 0.8), (tr - 0.2, zz + 0.8)],
                           (tx, ty), "copper_dk", 24))
    parts.append(tube("strut", *P["strut"], 0.9, "copper"))
    # the unloading pad: a frame with the yellow deck set into it, turned 45 deg
    px, py = P["pad"]
    s, h, ins = P["pad_side"], P["pad_h"], P["pad_inset"]
    a = math.radians(45.0)
    parts.append(obox("pad_frame", px, py, a, s, s, 0.0, h, "pad_frame", bev=0.35))
    parts.append(obox("pad_deck", px, py, a, s - 2 * ins, s - 2 * ins, h - 0.5, h + 0.02, "pad"))
    # bollards at the pad's outer corners
    for k in range(4):
        aa = a + math.pi / 4 + k * math.pi / 2
        d = (s / 2 - 1.2) * math.sqrt(2)
        parts.append(vcyl(f"pad_post{k}", 0.75, px + d * math.cos(aa), py + d * math.sin(aa), h, h + 2.2, "copper_dk", 10))
    return ang_lr


def build(whole=False):
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    nodes = []
    for name, fn in (("base", base_parts), ("main", main_parts)):
        parts = []
        fn(parts)
        node = V3.finish_node(parts, ("whole_" + name) if whole else name, (0, 0, 0))
        node.data.transform(Matrix.Scale(P["grow"], 4))
        node.parent = root
        nodes.append(node)
    bpy.context.view_layer.update()
    return root, nodes


# ------------------------------------------------------------------------------------------------
# Materials


def mat(name):
    m = V3.mat(name)
    c = PREVIEW_HOUSE if name == "HOUSE" else COL.get(name, V3.COL[name])
    m.diffuse_color = (*srgb_to_linear(c), 1)
    return m


def surface(nt, name):
    """V3's weathering + refinery specifics: spice-orange staining low on the walls and round the pad, soot at
    the chimney and pole tops, the roof segments darker towards the lip."""
    c, r, m = _V3_SURFACE(nt, name)
    nodes, links = nt.nodes, nt.links
    geo = nodes.new("ShaderNodeNewGeometry")
    pos = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], pos.inputs[0])

    def remap(sock, a_, b_, c_, d_):
        mr = nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = a_
        mr.inputs["From Max"].default_value = b_
        mr.inputs["To Min"].default_value = c_
        mr.inputs["To Max"].default_value = d_
        links.new(sock, mr.inputs["Value"])
        return mr.outputs[0]

    def mix(fac, a_, col):
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        links.new(fac, mx.inputs["Factor"])
        links.new(a_, mx.inputs[6])
        mx.inputs[7].default_value = (*srgb_to_linear(col), 1)
        return mx.outputs[2]

    def mul(a_, b_):
        mm = nodes.new("ShaderNodeMath")
        mm.operation = "MULTIPLY"
        mm.use_clamp = True
        links.new(a_, mm.inputs[0])
        if isinstance(b_, float):
            mm.inputs[1].default_value = b_
        else:
            links.new(b_, mm.inputs[1])
        return mm.outputs[0]

    def noise(scale, off):
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = 3.0
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Location"].default_value = (off, off * 0.7, off * 1.3)
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]

    if name in ("plinth", "wall", "concrete", "pad_frame", "block", "slat"):
        # spice stain: rusty orange-brown, low on the walls
        low = remap(pos.outputs["Z"], 2.0 * PX, 9.0 * PX, 1.0, 0.0)
        c = mix(mul(mul(low, remap(noise(9.0, 31.0), 0.35, 0.7, 0.2, 1.0)), 0.55), c, (0.55, 0.36, 0.20))
    if name in ("copper", "copper_dk", "hub"):
        # soot at the tops of the stacks
        hi = remap(pos.outputs["Z"], 45.0 * PX, 62.0 * PX, 0.0, 1.0)
        c = mix(mul(mul(hi, remap(noise(14.0, 3.0), 0.25, 0.75, 0.4, 1.0)), 0.8), c, (0.08, 0.065, 0.05))
    if name == "pad":
        # tyre / hopper grime towards the centre of the deck and its approach (south-east half)
        dx = nodes.new("ShaderNodeMath")
        dx.operation = "SUBTRACT"
        links.new(pos.outputs["X"], dx.inputs[0])
        dx.inputs[1].default_value = P["pad"][0] * PX
        c = mix(mul(remap(noise(7.0, 12.0), 0.4, 0.75, 0.0, 0.55), remap(dx.outputs[0], -6 * PX, 10 * PX, 0.2, 1.0)),
                c, (0.62, 0.48, 0.25))
    return c, r, m


_V3_SURFACE = V3.surface_tree


def procedural_materials():
    out = {}
    for name, m in list(V3._mats.items()):
        if name in KEEP:
            continue
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        c, r, mt = surface(nt, name)
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        out[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
    return out


def bake_and_merge(objs, size, samples=24):
    procs = procedural_materials()
    V3.uv_unwrap(objs, margin=0.002)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = V3.pack_mr(images, size)
    body_mat = V3.export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_refinery_opus_bake"))
    for o in objs:
        me = o.data
        for s in o.material_slots:
            if s.material and not s.material.name.startswith(KEEP):
                s.material = body_mat
        idx_map, keep = {}, []
        for s in me.materials:
            key = s.name if s else ""
            if key not in idx_map:
                idx_map[key] = len(keep)
                keep.append(s)
        new_index = {si: idx_map[(s.name if s else "")] for si, s in enumerate(me.materials)}
        old_idx = [p.material_index for p in me.polygons]
        me.materials.clear()
        for s in keep:
            me.materials.append(s)
        for p, oi in zip(me.polygons, old_idx):
            p.material_index = new_index[oi]
    return body_mat


# ------------------------------------------------------------------------------------------------
# Review renders


def review_renders(root, out_dir, samples, size=900):
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    hm = bpy.data.materials.get("HOUSE")
    if hm:
        hm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb_to_linear(PREVIEW_HOUSE), 1)
        t = hm.node_tree.nodes.get("HOUSE_TINT")
        if t:
            t.inputs[6].default_value = (*srgb_to_linear(PREVIEW_HOUSE), 1)
    w = bpy.data.worlds.new("rev")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.42, 0.36, 0.30, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.7
    scene.world = w
    sd = bpy.data.lights.new("rev_sun", "SUN")
    sd.energy = 3.5
    sd.angle = math.radians(3)
    sun = bpy.data.objects.new("rev_sun", sd)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(40), 0, math.radians(-60))
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -0.001))
    g = bpy.context.active_object
    gm = bpy.data.materials.new("rev_ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb_to_linear((0.77, 0.63, 0.43)), 1)
    g.data.materials.append(gm)
    cd = bpy.data.cameras.new("rev_cam")
    cd.type = "ORTHO"
    cam = bpy.data.objects.new("rev_cam", cd)
    scene.collection.objects.link(cam)
    scene.camera = cam

    def shot(name, yaw_deg, elev_deg, scale, target=(0.0, 0.0, 0.5)):
        cd.ortho_scale = scale
        e, y = math.radians(elev_deg), math.radians(yaw_deg)
        tgt = Vector(target)
        d = Vector((math.sin(y) * math.cos(e), -math.cos(y) * math.cos(e), math.sin(e)))
        cam.location = tgt + 30 * d
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, name + ".png")
        bpy.ops.render.render(write_still=True)

    for k in range(8):
        shot(f"turn_{k}", 45 * k, 35, 4.6)
    shot("closeup_wall", -20, 18, 1.8, (-0.66, -0.4, 0.45))
    shot("closeup_centre", 10, 30, 2.0, (0.25, 0.4, 0.8))
    shot("closeup_pad", 40, 40, 1.7, (0.9, 0.0, 0.1))
    shot("closeup_roof", -30, 60, 2.0, (0.69, 1.06, 1.0))


# ------------------------------------------------------------------------------------------------


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, whole=None, bake=True, bake_size=2048, turntable=None, samples=64)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--out-whole":
            opt["whole"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--set":
            k, v = argv[i + 1].split("=", 1)
            if k not in P:
                raise SystemExit("unknown proportion " + k)
            P[k] = float(v) if isinstance(P[k], float) else type(P[k])(eval(v))   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None and opt["whole"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-refinery-opus")
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root, nodes = build()
    for name in list(V3._mats):
        mat(name)
    log(f"built: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s")
    if opt["bake"] and (opt["out"] or opt["whole"] or opt["turntable"]):
        t1 = time.time()
        bake_and_merge(nodes, opt["bake_size"])
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
    if opt["out"]:
        path = os.path.join(opt["out"], "models", "building", KEY + ".glb")
        V3.export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
    if opt["whole"]:
        for n in nodes:
            n.name = "whole_" + n.name
        path = os.path.join(opt["whole"], "models", "building", KEY + ".glb")
        V3.export_glb(root, path)
        for n in nodes:
            n.name = n.name[len("whole_"):]
        log(f"exported {path} (whole: no base node)")
    if opt["turntable"]:
        t2 = time.time()
        review_renders(root, opt["turntable"], opt["samples"])
        log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")


if __name__ == "__main__":
    main()
