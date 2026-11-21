"""Reimagined Harkonnen wind trap - the redesign with real materials and human-scale detail (docs/3d-models.md
"Wind trap redesigns"), the Harkonnen twin of reimagined_wind_trap.py (the approved Atreides one).

The identity is the main pack's model (harkonnen_wind_trap.py, read from the Harkonnen frames): the wind trap's
layout - two pointed scoops one behind the other on a diagonal, mouths to the south-east with three cyan lines, the
machinery east of them - in the Harkonnen dress: dark gun-metal shells 2 px deeper than the Atreides ones, a brass
rim, grey steel lips, a brown louvre wall, and the house colour as three armour patches along the west flank of
each scoop plus one at the apex behind the rim (not a crest band). What is new here:

  * the scoops are armoured: a black core under big dark ARMOUR PLATES with true joints and corner rivets (five
    close gun-metal tones), heavy dull-brass rib flanges with bolts, a pale steel spine along the crest with a
    short spike at every rib, the house patches as bolted-on armour panels, a brass rim with a keystone and two
    steel buttresses, a mouth that is a real recess (a brown steel duct with a seven-blade turbine) behind a heavy
    grille: three brown slats carrying the lit cyan strips, two plain slats between them, three posts;
  * the machinery is Harkonnen: a blackened generator drum with brass hoops and a big extractor fan in a cowl on
    its crown, a rust drum, a control BUNKER (battered dark plating, an armoured door, a wall fan, a railed roof
    with a ladder, a dark dome with a brass ring), two heavy double-collared chimney stacks, brass pipes with
    hazard-yellow hand wheels, barrels, lamp posts, a weather mast (vane, anemometer, beacon, whip antenna);
  * the same human-scale parts as the Atreides one (service doors with frames, a step, a lit window and a lamp,
    maintenance ladders to a roof hatch, louvred vents, a control panel with status lights, stencilled unit
    numbers, hazard edges, warning plates) and the same surface pass: this file is the Harkonnen house on
    windtrap_common.py (Scoop, plate, door, ladder, railing, rotor, ..., surface(), build(), run()) - its palette,
    its two part builders (scoop / machinery) and the grime colours that have to read on dark metal. The weathering
    knobs are the same two: WEATHERING 1.0, MOTTLE 1.0 (--weathering X --mottle Y).

    blender -b --factory-startup --python tools/models/reimagined_wind_trap_harkonnen.py -- --out DIR [--work DIR]
        [--no-bake] [--bake-size 2048] [--lod 0.34] [--weathering 1.0] [--mottle 1.0] [--preview DIR]
        [--turntable DIR --samples 24] [--tris]
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined HARKONNEN_WIND_TRAP

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up).
Nothing is copied from the original art.
"""
import math
import os
import random
import sys

import bmesh
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import windtrap_common as RW  # noqa: E402  (helpers, the surface / weathering pass, build, run)
import atreides_wind_trap as AW  # noqa: E402  (the wind trap's layout: P)

K = RW.K
P = dict(AW.P, deep=34.5)              # the Atreides layout; the Harkonnen shells are 2 px deeper (harkonnen_wind_trap.py)
beam, tube, obox, polar = RW.beam, RW.tube, RW.obox, RW.polar
unit, plate, bolt, arch_band, quad_bar = RW.unit, RW.plate, RW.bolt, RW.arch_band, RW.quad_bar
new_obj, rings_solid = RW.new_obj, RW.rings_solid

KEY = "HARKONNEN_WIND_TRAP"
NODES = dict(
    main="everything that does not move",
    turbine_s="the south scoop's intake turbine behind the grille (spins about the scoop's axis)",
    turbine_n="the north scoop's intake turbine",
    fan_0="the big extractor fan in the cowl on the generator drum's crown (vertical axis)",
    fan_1="the wall fan in the bunker's south face (horizontal axis, out of the wall)",
    vane="the wind vane on the weather mast",
    anemometer="the cup anemometer on the mast's arm",
)
ANCHOR_DOC = dict(
    fx_vent_0="the south mouth (centre of the opening)", fx_vent_1="the north mouth",
    fx_vent_2="the extractor fan's outlet on the generator drum: heat shimmer",
    fx_smoke_0="the open top of the tall stack", fx_smoke_1="the open top of the short stack",
    fx_beacon_0="the red beacon on the mast", fx_beacon_1="the red lamp on the tall stack's rim",
    fx_light_0="the lamp post at the bunker", fx_light_1="the lamp post east of the machinery",
)

# ------------------------------------------------------------------------------------------------
# Palette: the Harkonnen tones (harkonnen_kit.HCOL, read from the Harkonnen frames) on this generator's material
# names. Names the shared helpers use (windtrap_common.COL: door, rail, slat, ...) are re-coloured; h_* are this
# model's own.

COL = dict(
    shell_core=(0.105, 0.095, 0.090),   # under the plates: the joints
    h_arm_a=(0.470, 0.405, 0.365),      # the armour plates: dark warm gun-metal in close tones
    h_arm_b=(0.420, 0.365, 0.335),
    h_arm_c=(0.445, 0.400, 0.370),
    h_arm_d=(0.370, 0.325, 0.300),
    h_arm_e=(0.400, 0.370, 0.360),      # a replaced plate now and then: a cooler grey stock
    h_rib=(0.520, 0.400, 0.270),        # rib flanges, caps: dull brass
    h_brass=(0.800, 0.570, 0.240),      # the rim, hoops, pipes: bright brass
    h_brass_dk=(0.420, 0.290, 0.140),
    h_plate=(0.600, 0.570, 0.520),      # the crest spine: pale worn plating
    h_lip=(0.530, 0.522, 0.505),        # the lips, buttresses: grey steel
    h_tread=(0.300, 0.290, 0.285),      # the lip's inlay: dark tread plate
    h_bunker=(0.420, 0.385, 0.360),     # the bunker, porches: soot-dark plating
    h_bunker_dk=(0.310, 0.285, 0.270),
    h_drum=(0.300, 0.280, 0.265),       # the generator drum, stacks: blackened steel
    h_fin=(0.560, 0.450, 0.340),        # the plain louvre slats: brown
    h_black=(0.215, 0.200, 0.195),      # spikes, cornices: blackened plating
    h_rivet=(0.560, 0.530, 0.490),      # rivet heads: worn bright
    slat=(0.300, 0.235, 0.180),         # the lit louvre slats, duct fittings: brown steel (a brown louvre wall)
    duct=(0.230, 0.190, 0.160),
    door=(0.250, 0.235, 0.230),         # door leaves: dark iron paint
    door_fr=(0.520, 0.500, 0.470),      # frames, handles, small fittings
    rail=(0.500, 0.480, 0.440),         # railings, ladders
    stencil=(0.760, 0.720, 0.640),      # stencilled numbers: pale on the dark plates
    red_p=(0.850, 0.640, 0.130),        # hand wheels, the vane's fin: hazard yellow (red is the house colour here)
    foot=(0.200, 0.185, 0.180),
)
SPEC = dict(
    shell_core=(0.2, 0.75, "dark"), h_arm_a=(0.25, 0.46, "paint"), h_arm_b=(0.25, 0.52, "paint"), h_arm_c=(0.25, 0.49, "paint"),
    h_arm_d=(0.25, 0.55, "paint"), h_arm_e=(0.25, 0.50, "paint"), h_rib=(0.45, 0.42, "steel"), h_brass=(0.55, 0.34, "steel"),
    h_brass_dk=(0.6, 0.45, "steel"), h_plate=(0.5, 0.40, "steel"), h_lip=(0.4, 0.45, "steel"), h_tread=(0.6, 0.55, "steel"),
    h_bunker=(0.0, 0.60, "paint"), h_bunker_dk=(0.0, 0.62, "paint"), h_drum=(0.5, 0.45, "steel"), h_fin=(0.4, 0.5, "steel"),
    h_black=(0.3, 0.6, "dark"), h_rivet=(0.8, 0.35, "steel"), slat=(0.5, 0.48, "steel"), duct=(0.6, 0.55, "steel"),
    door=(0.1, 0.5, "paint"), stencil=(0.0, 0.55, "paint"), red_p=(0.0, 0.45, "paint"), foot=(0.0, 0.8, "dark"),
)
UV_SCALE = dict(h_black=0.6, h_rivet=0.6, h_tread=0.6)

# The grime that has to read on dark metal: oil-dark stains, pale dusty run-off and a pale dusty splash at the foot
# (on the cream Atreides shells they are all darker than the paint). Strengths (WEAR, GRIME_W, WEAR_GROW) and the
# two knobs are the shared ones.
STAIN = (0.200, 0.168, 0.140)
RUNOFF = (0.600, 0.505, 0.390)
LOW = (0.520, 0.440, 0.330)
CAVITY = (0.085, 0.072, 0.062)
SHELL_MATS = ("h_arm_a", "h_arm_b", "h_arm_c", "h_arm_d", "h_arm_e", "h_rib", "h_lip")   # get the scoops' own streaks
LIP_MATS = ("h_lip",)                  # of those, the lip: run-off from the sill, no rib rust

RIBS = (0.19, 0.40, 0.63, 0.84)
BANDS_T = (0.035, 0.19, 0.40, 0.63, 0.84, 0.965)
BANDS_A = (0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0)
TONES = ("h_arm_a", "h_arm_a", "h_arm_a", "h_arm_b", "h_arm_b", "h_arm_b", "h_arm_c", "h_arm_c", "h_arm_d", "h_arm_d", "h_arm_e")
PATCH_T = ((0.215, 0.335), (0.455, 0.560), (0.670, 0.800))     # the three house armour patches of a flank (t ranges)
PATCH_A = (134.0, 166.0)
STACKS = (((25.0, 24.0), 1.7, 20.0), ((28.2, 19.2), 1.3, 15.5))    # (x, y), radius, top
FLUE_TOP = [(c[0], c[1], z) for (c, _, z) in STACKS]               # the soot gathers round both stack tops


def stack(name, c, r, z0, z1, material="h_drum", collar="h_brass_dk", collars=2):
    """The Harkonnen chimney (after harkonnen_kit.stack): a heavy pipe with thick collars, a flared sooty mouth."""
    out = [tube(f"{name}_p", (c[0], c[1], z0), (c[0], c[1], z1), r, material, 12),
           K.cyl(f"{name}_foot", c, r * 1.55, z0, z0 + 5.0, "steel_dk", 12, r * 1.2)]
    for i in range(collars):
        z = z0 + 5.0 + (z1 - z0 - 5.0) * (i + 0.8) / (collars + 0.9)
        out.append(K.cyl(f"{name}_col{i}", c, r * 1.3, z - 0.7, z + 0.7, collar, 12))
    out.append(K.cyl(f"{name}_lip", c, r * 1.2, z1 - 1.5, z1 + 0.1, "soot", 12, r * 1.38))
    out.append(K.cyl(f"{name}_bore", c, r * 0.95, z1 - 0.5, z1 + 0.2, "burnt", 12))
    return out


def corner_bolts(parts, name, sc, t0, t1, a0, a1, lift, inset=0.95, size=0.36, h=0.22, material="h_rivet"):
    """A rivet in each corner of the shell patch t0..t1 x a0..a1 (inset px from its edges)."""
    tm, am = (t0 + t1) / 2, (a0 + a1) / 2
    per_deg = max(0.05, (sc.S(tm, am + 0.5) - sc.S(tm, am - 0.5)).length)
    dt, da = inset / sc.D, inset / per_deg
    for i, (t, a) in enumerate(((t0 + dt, a0 + da), (t1 - dt, a0 + da), (t1 - dt, a1 - da), (t0 + dt, a1 - da))):
        parts.append(bolt(f"{name}_r{i}", sc.S(t, a, lift), sc.N(t, a), size, h, material))


# ------------------------------------------------------------------------------------------------
# A scoop


def scoop(main, moving, anchors, tag, m, number):
    sc = RW.Scoop(tag, m)
    R, sq, D = sc.R, sc.sq, sc.D
    H = R * sq
    PT = RW.PT
    rnd = random.Random(23 if tag == "s" else 29)
    wall, u_in = 1.5, 6.2
    fa = P["face_ang"]
    # ---- the shell: a black core, hollow behind the mouth (the intake), closed at the back
    n = 32
    angs = [180.0 * i / n for i in range(n + 1)]
    t_in = u_in / D
    rings = [[tuple(sc.S(t_in, a, -wall)) for a in angs], [tuple(sc.S(0.0, a, -wall)) for a in angs]]
    for i in range(14):
        rings.append([tuple(sc.S(i / 14, a)) for a in angs])
    rings.append([tuple(sc.at(1.0, a, 0.3)) for a in angs])
    bm = bmesh.new()
    rings_solid(bm, rings)
    main.append(new_obj(f"{tag}_shell", bm, "shell_core"))
    prof = [(i / 14, sc.rad(i / 14)) for i in range(3, 14)] + [(1.0, 0.3)]
    a3, b3 = sc.L(0.0, 0.0, 0.0), sc.L(0.0, D, 0.0)
    main.append(K.sweep(f"{tag}_foot", a3, (b3[0] + sc.bx, b3[1] + sc.by, 0.0), [(t, r + 1.3) for (t, r) in prof], "foot", 24, 0.0,
                        2.6 / (R + 1.3), (0.0, 180.0), PT))
    # ---- armour plates with real joints, one of five close gun-metal tones each; rivets on the flank the camera sees
    for i, (t0, t1) in enumerate(zip(BANDS_T[:-1], BANDS_T[1:])):
        edges = BANDS_A if i < 4 else (0.0, 45.0, 90.0, 135.0, 180.0)
        for k, (a0, a1) in enumerate(zip(edges[:-1], edges[1:])):
            main.append(plate(f"{tag}_pl{i}_{k}", sc, t0, t1, a0, a1, 0.34, 0.6, rnd.choice(TONES), gap=0.7, na=3 if i < 4 else 4))
            if a1 > 90.0 and i < 4:
                corner_bolts(main, f"{tag}_pl{i}_{k}", sc, t0, t1, a0, a1, 0.34, 1.25)
    main.append(plate(f"{tag}_tipcap", sc, 0.955, 0.995, 0.0, 180.0, 0.6, 0.65, "h_rib", na=8, nt=2))
    # ---- heavy rib flanges over the joints, bolted; the pale spine along the crest with a spike at every rib
    for i, t in enumerate(RIBS):
        hw = 0.62 / D
        main.append(plate(f"{tag}_rib{i}", sc, t - hw, t + hw, 0.0, 180.0, 0.85, 0.7, "h_rib", na=18, nt=1))
        for k, ang in enumerate((79.0, 101.0, 123.0, 146.0, 168.0)):
            main.append(bolt(f"{tag}_rib{i}_b{k}", sc.S(t, ang, 0.83), sc.N(t, ang), 0.52, 0.34, "h_brass_dk"))
        p = sc.S(t, 90.0, 1.0)
        main.append(K.cyl(f"{tag}_spike{i}", (p.x, p.y), 0.6, p.z, p.z + 1.5, "h_black", 6, 0.07))
    main.append(plate(f"{tag}_spine", sc, 0.045, 0.935, 85.0, 95.0, 1.05, 0.8, "h_plate", na=2, nt=18))
    # ---- house colour: three bolted armour patches along the south-west flank, one at the apex behind the rim
    for i, (t0, t1) in enumerate(PATCH_T):
        main.append(K.house(plate(f"{tag}_patch{i}", sc, t0, t1, PATCH_A[0], PATCH_A[1], 0.82, 0.5, "HOUSE", na=6, nt=3), 0.38))
        corner_bolts(main, f"{tag}_patch{i}", sc, t0, t1, PATCH_A[0], PATCH_A[1], 0.82, 0.8, 0.4, 0.24, "h_brass")
    main.append(K.house(plate(f"{tag}_apex", sc, 0.06, 0.16, 62.0, 100.0, 0.82, 0.5, "HOUSE", na=8, nt=3), 0.42))
    # ---- the mouth: brass rim with bolts and a keystone, steel buttresses, a brown duct, the turbine, the grille
    main.append(arch_band(f"{tag}_rim", sc, -0.95, 0.75, R + 0.75, R - 1.1, "h_brass"))
    for k in range(13):
        ang = 12.0 + 13.0 * k
        p = sc.at(0.0, ang, R - 0.15)
        main.append(bolt(f"{tag}_rimb{k}", (p.x - sc.bx * 0.95, p.y - sc.by * 0.95, p.z), (-sc.bx, -sc.by, 0.0), 0.5, 0.32, "h_brass_dk"))
    main.append(beam(f"{tag}_keystone", sc.L(0.0, -0.75, H - 2.6), sc.L(0.0, -0.75, H + 0.9), 2.3, 1.5, "h_rib"))
    main.append(K.ball(f"{tag}_keylamp", sc.L(0.0, -1.7, H - 1.2), 0.42, "red", 8))
    for sgn in (-1, 1):
        main.append(obox(f"{tag}_buttress{sgn}", sc.L(sgn * (R + 0.2), -0.35, 0.0)[:2], fa, 1.8, 1.55, 0.0, 6.2, "h_lip", 0.2, 0.78))
        main.append(obox(f"{tag}_buttcap{sgn}", sc.L(sgn * (R + 0.2), -0.35, 0.0)[:2], fa, 1.55, 1.35, 6.2, 6.7, "h_black"))
    ri = R - wall
    main.append(arch_band(f"{tag}_duct", sc, 0.3, u_in - 0.2, ri + 0.1, ri - 0.3, "duct"))
    for k, u in enumerate((1.9, 3.5, 5.1)):                 # duct stiffening hoops
        main.append(arch_band(f"{tag}_ducth{k}", sc, u - 0.2, u + 0.2, ri - 0.2, ri - 0.75, "slat", 14))
    back = [(p.x + sc.bx * (u_in - 0.25), p.y + sc.by * (u_in - 0.25), p.z) for p in (sc.at(0.0, a, ri - 0.2) for a in angs)]
    bm = bmesh.new()
    rings_solid(bm, [back, [(x + sc.bx * 0.2, y + sc.by * 0.2, z) for (x, y, z) in back]])
    main.append(new_obj(f"{tag}_back", bm, "duct_dk"))
    zf = 3.7                                                # the sill: level with the top of the lip
    main.append(obox(f"{tag}_sill", sc.L(0.0, u_in / 2 - 0.1, 0.0)[:2], fa, sc.half_at(ri, zf) + 0.4, u_in / 2, 0.0, zf, "grate"))
    for k in range(6):                                      # grating bars
        u = 0.9 + k * 0.85
        w = sc.half_at(ri, zf + 0.2) - 0.5
        main.append(quad_bar(f"{tag}_grate{k}", sc, -w, w, [(u - 0.22, zf), (u + 0.22, zf), (u + 0.22, zf + 0.2), (u - 0.22, zf + 0.2)], "slat"))
    # the turbine: a bellmouth ring on struts, the rotor a node of its own (seven broad blades)
    zc, rt, ut = 13.6, 8.4, 4.5
    axis = Vector((-sc.bx, -sc.by, 0.0))
    c3 = Vector(sc.L(0.0, ut, zc))
    ring_pts = [(rt + 0.3, -0.9), (rt + 1.25, -0.9), (rt + 0.95, 0.9), (rt + 0.3, 0.9)]
    side = Vector((sc.sx, sc.sy, 0.0))
    up = Vector((0, 0, 1))
    segs = 24
    bm = bmesh.new()
    vs = [[bm.verts.new(tuple(c3 + (side * math.cos(2 * math.pi * i / segs) + up * math.sin(2 * math.pi * i / segs)) * r - axis * u))
           for i in range(segs)] for (r, u) in ring_pts]
    for a_, b_ in zip(vs, vs[1:] + vs[:1]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new([a_[i], a_[j], b_[j], b_[i]])
    main.append(new_obj(f"{tag}_bellmouth", bm, "slat"))
    for k, a in enumerate((35.0, 145.0, 215.0, 325.0)):     # struts from the ring to the duct
        d = side * math.cos(math.radians(a)) + up * math.sin(math.radians(a))
        reach = rt + 1.0
        while reach < rt + 12.0:
            q = d * (reach + 0.25)
            if zc + q.z < zf + 0.2 or abs(q.dot(side)) > sc.half_at(ri - 0.4, zc + q.z):
                break
            reach += 0.25
        main.append(beam(f"{tag}_strut{k}", tuple(c3 + d * (rt + 1.0)), tuple(c3 + d * reach), 0.5, 0.35, "slat"))
    parts = RW.rotor(f"{tag}_turbine", 7, 1.6, rt, 2.3, 3.8, 0.34, 34.0, "blade", hub=(2.0, -0.7, 0.5, 2.1), tips=0.9)
    moving.append((parts, f"turbine_{tag}", tuple(c3), tuple(axis)))
    anchors.append((f"fx_vent_{0 if tag == 's' else 1}", sc.L(0.0, 0.5, zc), 1.0))
    # the grille: three posts, three brown slats carrying the lit strips, two plain slats between them
    post_top = sc.half_at(ri, 0.0) * sq - 1.2
    main.append(beam(f"{tag}_post", sc.L(0.0, -0.05, zf), sc.L(0.0, -0.05, post_top), 0.8, 1.0, "h_brass_dk"))
    sp = 8.6
    sp_top = ri * sq * (1.0 - ((sp + 0.5) / ri) ** PT) - 0.3
    for sgn in (-1, 1):
        main.append(beam(f"{tag}_post{sgn}", sc.L(sgn * sp, -0.05, zf), sc.L(sgn * sp, -0.05, sp_top), 0.6, 0.8, "slat"))
    for i, z in enumerate((5.6, 11.5, 17.5)):
        w = sc.half_at(ri - 0.2, z + 0.5) - 0.15
        a_, b_ = (-0.75, z - 0.3), (1.35, z + 0.5)
        ln = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        nu, nz = -(b_[1] - a_[1]) / ln, (b_[0] - a_[0]) / ln            # the upper face's normal (out and up)
        main.append(quad_bar(f"{tag}_slat{i}", sc, -w, w, [a_, b_, (b_[0] - nu * 0.7, b_[1] - nz * 0.7), (a_[0] - nu * 0.7, a_[1] - nz * 0.7)], "slat"))
        f0, f1 = 0.07, 0.55                                              # the lit strip along the leading edge of the upper face
        p0 = (a_[0] + (b_[0] - a_[0]) * f0 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f0 + nz * 0.02)
        p1 = (a_[0] + (b_[0] - a_[0]) * f1 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f1 + nz * 0.02)
        sec = [p0, p1, (p1[0] + nu * 0.14, p1[1] + nz * 0.14), (p0[0] + nu * 0.14, p0[1] + nz * 0.14)]
        for sgn in (-1, 1):                                             # left / right of the centre post
            s0, s1 = 0.8, w - 0.7
            cuts = [s0] + ([sp] if s1 > sp + 2.0 else []) + [s1]        # broken at the side posts
            for k, (c0, c1) in enumerate(zip(cuts[:-1], cuts[1:])):
                x0, x1 = c0 + (0.5 if k else 0.16), c1 - (0.5 if k + 2 < len(cuts) else 0.16)
                main.append(quad_bar(f"{tag}_lens{i}_{sgn}_{k}", sc, min(sgn * x0, sgn * x1), max(sgn * x0, sgn * x1), sec, "glow_a" if (k + i) % 2 == 0 else "glow_b"))
        for sgn in (-1, 1):                                             # end brackets to the duct
            main.append(quad_bar(f"{tag}_slatend{i}_{sgn}", sc, sgn * (w - 0.5), sgn * (w + 0.5), [(-0.5, z - 0.8), (1.2, z - 0.4), (1.2, z + 0.9), (-0.5, z + 0.5)], "h_brass_dk"))
    for i, z in enumerate((8.55, 14.5)):                                # the plain slats
        w = sc.half_at(ri - 0.2, z + 0.4) - 0.15
        main.append(quad_bar(f"{tag}_fin{i}", sc, -w, w, [(-0.45, z - 0.2), (1.0, z + 0.35), (1.0, z + 0.0), (-0.45, z - 0.55)], "h_fin"))
    # ---- the lip: the grey steel ramp in front of the mouth, its tread-plate inlay, a hazard edge at the front
    Lp = P["lip"]
    q = [sc.L(-(R + 0.5), 0.0, 0.0)[:2], sc.L(R + 0.5, 0.0, 0.0)[:2], sc.L(R - 3.0, -Lp, 0.0)[:2], sc.L(-(R - 3.0), -Lp, 0.0)[:2]]
    main.append(K.ramp(f"{tag}_lip", q[2:], q[:2], 0.0, 1.2, 3.4, "h_lip", 0.3))

    def lerp(a_, b_, f):
        return (a_[0] * (1 - f) + b_[0] * f, a_[1] * (1 - f) + b_[1] * f)
    main.append(K.ramp(f"{tag}_lip_in", [lerp(q[2], q[3], 0.15), lerp(q[2], q[3], 0.85)], [lerp(q[0], q[1], 0.15), lerp(q[0], q[1], 0.85)],
                       0.5, 1.5, 3.7, "h_tread", 0.1))
    nblk = 12
    for k in range(nblk):
        s0 = -(R - 4.2) + 2 * (R - 4.2) * k / nblk
        s1 = -(R - 4.2) + 2 * (R - 4.2) * (k + 1) / nblk
        main.append(beam(f"{tag}_hz{k}", sc.L(s0, -Lp + 1.45, 1.78), sc.L(s1, -Lp + 1.45, 1.78), 0.9, 0.16, "hz_y" if k % 2 else "hz_k"))
    for sgn in (-1, 1):                                                 # treads along the inlay's sides
        for k in range(5):
            u = -Lp + 2.6 + k * 1.8
            zz = 1.5 + (3.7 - 1.5) * (u + Lp) / Lp + 0.16
            main.append(beam(f"{tag}_tread{sgn}_{k}", sc.L(sgn * (R - 6.4), u, zz), sc.L(sgn * (R - 9.4), u, zz), 0.4, 0.14, "door_fr"))
    # ---- the flank the camera sees: a maintenance ladder to a roof hatch (between the 2nd and 3rd patch), the unit
    # number above the first patch, a service door in a porch under the 2nd rib, a vent at the back
    tl = 0.597
    stand = 0.8
    la = [tuple(sc.S(tl - 0.72 / D, a, stand)) for a in range(176, 104, -9)]
    lb = [tuple(sc.S(tl + 0.72 / D, a, stand)) for a in range(176, 104, -9)]
    RW.ladder(main, f"{tag}_ladder", la, lb)
    for k, a in enumerate((172.0, 150.0, 128.0, 110.0)):                # stand-offs
        for dt in (-0.72 / D, 0.72 / D):
            main.append(beam(f"{tag}_lad_st{k}_{dt > 0}", tuple(sc.S(tl + dt, a, 0.2)), tuple(sc.S(tl + dt, a, stand)), 0.22, 0.22, "rail"))
    hp, hn = sc.S(tl - 0.045, 104.0, 0.3), sc.N(tl - 0.045, 104.0)      # the hatch beside the spine
    main.append(K.tube(f"{tag}_hatch", tuple(hp), tuple(hp + hn * 0.75), 1.5, "h_rib", 12))
    main.append(K.tube(f"{tag}_hatch_lid", tuple(hp + hn * 0.75), tuple(hp + hn * 1.0), 1.25, "door", 12))
    main.append(beam(f"{tag}_hatch_bar", tuple(hp + hn * 1.15 - Vector((sc.bx, sc.by, 0)) * 0.8), tuple(hp + hn * 1.15 + Vector((sc.bx, sc.by, 0)) * 0.8), 0.2, 0.2, "door_fr"))
    RW.stencil(main, f"{tag}_num", sc, number, 0.345, 129.5, 1.7, 10.5, 0.42)
    td = 0.397
    rd = sc.rad(td)
    base = sc.L(-(rd - 0.9), td * D, 0.0)[:2]
    main.append(obox(f"{tag}_porch", base, fa + 90.0, 2.15, 2.3, 0.0, 6.5, "h_bunker", 0.0, 0.94))
    main.append(obox(f"{tag}_porch_cap", base, fa + 90.0, 2.5, 2.65, 6.5, 7.05, "h_black"))
    door_c = sc.L(-(rd - 0.9) - 2.3, td * D, 0.0)[:2]
    RW.door(main, f"{tag}_door", door_c, fa + 180.0, 0.0, 2.5, 4.9)
    RW.warning_plate(main, f"{tag}_warn", sc.L(-(rd - 0.9) - 2.26, td * D + 1.72, 0.0)[:2], fa + 180.0, 3.3, 1.25)
    tv = 0.90
    rv = sc.rad(tv)
    main.extend(K.vent(f"{tag}_vent", sc.L(-(rv - 0.5), tv * D, 0.0)[:2], fa + 90.0, 1.5, 2.2, 4.3, 0.55, 3, "door_fr"))
    jb = sc.L(-(sc.rad(0.2) + 0.3), 0.2 * D, 0.0)
    main.append(obox(f"{tag}_jbox", jb[:2], fa + 90.0, 0.9, 0.5, 1.4, 3.6, "door_fr"))
    cpts = [sc.L(-(sc.rad(t) + 0.75), t * D, 1.9) for t in (0.2, 0.25, 0.3, 0.338)]
    for k, (p0, p1) in enumerate(zip(cpts[:-1], cpts[1:])):
        main.append(tube(f"{tag}_conduit{k}", p0, p1, 0.3, "rubber", 6))
        main.append(obox(f"{tag}_clamp{k}", ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2), fa + 90.0, 0.25, 0.5, 1.3, 2.4, "door_fr"))
    return sc


# ------------------------------------------------------------------------------------------------
# The machinery east of the scoops


def machinery(main, moving, anchors):
    main.append(obox("mach_base", (17.0, 11.0), 30.0, 9.0, 7.0, 0.0, 3.0, "foot", 0.4))
    # ---- the big generator drum: blackened steel, brass hoops, saddles, an inspection cover, the extractor fan
    ga, gb, gr = Vector((10.0, 14.0, 8.0)), Vector((24.0, 20.0, 8.0)), 6.2
    gd = (gb - ga).normalized()
    gang = math.degrees(math.atan2(gd.y, gd.x))
    gs = Vector((gd.y, -gd.x, 0.0))                      # the side the camera sees (south-east)
    main.append(K.tank("gen0", tuple(ga), tuple(gb), gr, "h_drum", 20, 0.4))
    main.append(K.hoop("gen0_band", tuple(ga), tuple(gb), 0.46, 0.60, gr + 0.35, "h_brass", 20))
    main.append(K.hoop("gen0_flange", tuple(ga), tuple(gb), 0.115, 0.155, gr + 0.45, "h_brass_dk", 20))
    for k in range(5):
        t = 0.67 + 0.058 * k
        main.append(K.hoop(f"gen0_fin{k}", tuple(ga), tuple(gb), t, t + 0.024, gr + 0.5, "h_brass_dk", 16))
    for k, t in enumerate((0.24, 0.78)):
        c = ga + (gb - ga) * t
        main.append(obox(f"gen0_saddle{k}", (c.x, c.y), gang + 90.0, 5.3, 0.9, 3.0, 6.4, "steel_dk"))
    cd = (gs * 0.72 + Vector((0, 0, 0.69))).normalized()
    cc = ga + (gb - ga) * 0.53
    main.append(tube("gen0_cover", tuple(cc + cd * (gr + 0.1)), tuple(cc + cd * (gr + 0.75)), 1.75, "steel", 12))
    main.append(tube("gen0_cover_in", tuple(cc + cd * (gr + 0.75)), tuple(cc + cd * (gr + 0.9)), 1.2, "h_brass_dk", 12))
    for k in range(6):
        a = math.radians(60.0 * k)
        e2 = cd.cross(gd).normalized()
        main.append(bolt(f"gen0_cover_b{k}", cc + cd * (gr + 0.75) + (gd * math.cos(a) + e2 * math.sin(a)) * 1.45, cd, 0.32, 0.22))
    tb = ga + (gb - ga) * 0.86 + gs * (gr * 0.93) + Vector((0, 0, -1.2))   # terminal box + conduit to the base
    main.append(obox("gen0_term", (tb.x, tb.y), gang, 1.3, 0.55, 5.6, 8.4, "steel_dk"))
    main.append(obox("gen0_term_lid", (tb.x + gs.x * 0.5, tb.y + gs.y * 0.5), gang, 1.0, 0.1, 6.0, 8.0, "door_fr"))
    main.append(tube("gen0_term_c", (tb.x - gd.x * 0.6, tb.y - gd.y * 0.6, 5.7), (tb.x - gd.x * 0.6, tb.y - gd.y * 0.6, 3.0), 0.3, "rubber", 6))
    # the extractor: a round steel cowl on the drum's crown, the fan under a guard (plainly seen from the game camera)
    fc = ga + (gb - ga) * 0.22
    fr = 2.75
    z0, z1 = 8.0 + gr - 1.2, 8.0 + gr + 1.5
    main.append(K.cyl("cowl", (fc.x, fc.y), fr + 0.75, z0, z1, "h_lip", 16, fr + 0.35))
    main.append(K.cyl("cowl_well", (fc.x, fc.y), fr + 0.05, z1 - 0.05, z1 + 0.04, "duct_dk", 16))
    main.append(K.lathe_loop("cowl_ring", (fc.x, fc.y), [(fr + 0.05, z1), (fr + 0.5, z1), (fr + 0.5, z1 + 0.75), (fr + 0.05, z1 + 0.75)], "h_brass_dk", 16))
    for k, a in enumerate((gang + 45.0, gang + 135.0)):
        main.append(tube(f"cowl_guard{k}", polar((fc.x, fc.y), fr + 0.3, a, z1 + 0.9 + 0.3 * k), polar((fc.x, fc.y), fr + 0.3, a + 180.0, z1 + 0.9 + 0.3 * k), 0.26, "door_fr", 8))
    for k in range(8):                                    # bolts round the cowl's foot
        px, py, _ = polar((fc.x, fc.y), fr + 0.85, 45.0 * k + 22.0, 0.0)
        main.append(bolt(f"cowl_b{k}", (px, py, z0 + 1.0), Vector((px - fc.x, py - fc.y, 0.0)).normalized(), 0.34, 0.22))
    moving.append((RW.rotor("fan_0", 6, 0.5, fr - 0.15, 1.0, 1.75, 0.14, 26.0, "blade", hub=(0.6, -0.08, 0.12, 0.4), tips=0.5), "fan_0",
                   (fc.x, fc.y, z1 + 0.3), (0, 0, 1)))
    anchors.append(("fx_vent_2", (fc.x, fc.y, z1 + 1.0), 0.5))
    # ---- the smaller rust drum
    ha, hb = (13.0, 5.0, 6.0), (24.0, 9.5, 6.0)
    main.append(K.tank("gen1", ha, hb, 4.8, "rust", 16, 0.4))
    main.append(K.hoop("gen1_band", ha, hb, 0.2, 0.3, 5.1, "h_brass", 16))
    main.append(K.hoop("gen1_band2", ha, hb, 0.7, 0.75, 5.1, "h_brass_dk", 16))
    hang = math.degrees(math.atan2(hb[1] - ha[1], hb[0] - ha[0]))
    for k, t in enumerate((0.3, 0.82)):
        c = (ha[0] + (hb[0] - ha[0]) * t, ha[1] + (hb[1] - ha[1]) * t)
        main.append(obox(f"gen1_saddle{k}", c, hang + 90.0, 4.2, 0.7, 3.0, 4.6, "steel_dk"))
    # ---- the control bunker (the pump block, 1 px east / 3 px north of the Atreides one as in the main pack's model):
    # battered dark plating, an armoured door, a control panel, a wall fan, a railed roof with a ladder, a dark dome
    pc, pang, phl, phw, ph = (16.0, -12.0), 20.0, 7.5, 5.5, 8.0
    ex, ey = unit(pang)
    nx, ny = -ey, ex                                      # north (across); the camera sees the south face (-n)

    def pp(e, d, z=None):                                 # e along the house, d out of the south face
        q = (pc[0] + ex * e - nx * d, pc[1] + ey * e - ny * d)
        return q if z is None else (q[0], q[1], z)
    main.append(obox("pump", pc, pang, phl, phw, 0.0, ph, "h_bunker", 0.5, 0.85))
    main.append(obox("pump_plinth", pc, pang, phl + 0.25, phw + 0.25, 0.0, 1.3, "foot"))
    main.append(obox("pump_cornice", pc, pang, phl * 0.85 + 0.35, phw * 0.85 + 0.35, ph - 0.1, ph + 0.45, "h_black"))
    for k, (e, d) in enumerate(((-phl, -phw), (phl, -phw), (phl, phw), (-phl, phw))):          # corner armour straps
        q = pp(e * 0.93, -d * 0.93)
        main.append(obox(f"pump_strap{k}", q, pang, 0.55, 0.55, 0.0, ph - 0.3, "h_bunker_dk", 0.0, 0.5))
    main.append(K.dome("pump_dome", pc, 4.2, ph + 0.4, 3.4, "h_drum"))
    main.append(K.lathe_loop("pump_dome_ring", pc, [(4.2, ph + 0.3), (4.6, ph + 0.3), (4.6, ph + 1.0), (4.2, ph + 1.0)], "h_brass", 24))
    main.append(K.cyl("pump_dome_cap", pc, 0.9, ph + 3.6, ph + 4.3, "h_brass_dk", 10))
    main.append(K.ball("pump_dome_lamp", (pc[0], pc[1], ph + 4.45), 0.4, "red", 8))
    de, pe, ve = 1.0, -1.95, -4.6                          # along the south face: door, control panel, wall fan (the ladder: 5.3)
    main.append(obox("porch", pp(de, 5.25), pang, 2.0, 0.75, 0.0, 6.4, "h_bunker_dk", 0.0, 0.97))
    main.append(obox("porch_cap", pp(de, 5.3), pang, 2.3, 1.0, 6.4, 6.85, "h_black"))
    lamp_d = RW.door(main, "door", pp(de, 6.0), pang - 90.0, 0.0, 2.6, 5.0)
    main.append(obox("panel", pp(pe, 5.45), pang, 0.85, 0.4, 2.5, 4.9, "steel_dk"))
    main.append(obox("panel_face", pp(pe, 5.87), pang, 0.7, 0.05, 2.7, 4.7, "door_fr"))
    for k, (dl, z, mname) in enumerate(((-0.35, 4.25, "green"), (0.0, 4.25, "red"), (0.35, 4.25, "red"), (-0.2, 3.3, "glass"), (0.25, 3.3, "glass"))):
        main.append(obox(f"panel_led{k}", pp(pe + dl, 5.94), pang, 0.12 if z > 4 else 0.2, 0.04, z - (0.12 if z > 4 else 0.3), z + (0.12 if z > 4 else 0.3), mname))
    RW.warning_plate(main, "panel_warn", pp(pe, 5.36), pang - 90.0, 5.3, 1.3)
    # the wall fan: a round housing in the south face, the rotor a node (axis out of the wall), a guard cross
    fz, out = 4.3, Vector((-nx, -ny, 0.0))
    main.append(tube("wfan_house", pp(ve, 4.6, fz), pp(ve, 5.75, fz), 1.75, "door_fr", 14))
    main.append(tube("wfan_well", pp(ve, 5.75, fz), pp(ve, 5.8, fz), 1.45, "duct_dk", 14))
    for k, a in enumerate((45.0, 135.0)):
        dv = Vector((ex, ey, 0.0)) * math.cos(math.radians(a)) + Vector((0, 0, 1)) * math.sin(math.radians(a))
        c = Vector(pp(ve, 6.3, fz))
        main.append(beam(f"wfan_guard{k}", tuple(c - dv * 1.6), tuple(c + dv * 1.6), 0.2, 0.16, "rail"))
    moving.append((RW.rotor("fan_1", 5, 0.3, 1.32, 0.55, 0.95, 0.1, 28.0, "blade", hub=(0.36, -0.06, 0.1, 0.25)), "fan_1",
                   pp(ve, 6.0, fz), tuple(out)))
    # roof: railing on three sides, the ladder up the south face
    tl_, tw_ = phl * 0.85 - 0.35, phw * 0.85 - 0.35
    zr = ph + 0.45
    RW.railing(main, "roof_rail", [pp(-tl_, -tw_, zr), pp(-tl_, tw_, zr), pp(4.5, tw_, zr)], 2.0)
    RW.railing(main, "roof_rail2", [pp(6.1, tw_, zr), pp(tl_, tw_, zr), pp(tl_, -tw_, zr)], 2.0)
    RW.ladder(main, "roof_ladder", [pp(4.7, 6.1, 0.15), pp(4.7, 5.15, zr + 2.0)], [pp(5.9, 6.1, 0.15), pp(5.9, 5.15, zr + 2.0)])
    # the weather mast on the roof's north-east corner: vane (node), anemometer (node), beacon, whip antenna
    mx, my = pp(tl_ - 0.9, -(tw_ - 0.9))
    zt = 19.6
    main.append(K.cyl("mast_foot", (mx, my), 0.7, zr - 0.3, zr + 0.5, "door_fr", 8))
    main.append(tube("mast", (mx, my, zr), (mx, my, zt), 0.3, "rail", 8, 0.2))
    arm0, arm1 = (mx - ex * 2.3, my - ey * 2.3, zt - 2.4), (mx + ex * 2.3, my + ey * 2.3, zt - 2.4)
    main.append(beam("mast_arm", arm0, arm1, 0.22, 0.22, "rail"))
    main.append(beam("mast_brace", (mx, my, zt - 4.2), (mx + ex * 1.5, my + ey * 1.5, zt - 2.5), 0.16, 0.16, "rail"))
    main.append(tube("mast_whip", (mx - ex * 1.2, my - ey * 1.2, zt - 2.4), (mx - ex * 1.2, my - ey * 1.2, zt + 4.4), 0.1, "steel_dk", 5, 0.05))
    bp = (arm0[0], arm0[1], zt - 2.4 + 0.35)
    main.append(K.cyl("beacon_s", bp[:2], 0.36, zt - 2.3, zt - 2.05, "steel_dk", 8))
    main.append(K.ball("beacon", (bp[0], bp[1], bp[2] + 0.2), 0.42, "red", 8))
    anchors.append(("fx_beacon_0", (bp[0], bp[1], bp[2] + 0.2), 0.5))
    vparts = [beam("vane_bar", (-1.7, 0.0, 0.0), (1.5, 0.0, 0.0), 0.16, 0.16, "rail"),
              obox("vane_fin", (-1.45, 0.0), 0.0, 0.62, 0.06, -0.55, 0.55, "red_p"),
              K.ball("vane_nose", (1.6, 0.0, 0.0), 0.26, "red_p", 6),
              tube("vane_hub", (0.0, 0.0, -0.45), (0.0, 0.0, 0.2), 0.22, "door_fr", 6)]
    moving.append((vparts, "vane", (mx, my, zt + 0.45), (0, 0, 1)))
    aparts = [tube("anem_hub", (0.0, 0.0, -0.3), (0.0, 0.0, 0.2), 0.2, "door_fr", 6)]
    for k in range(3):
        a = 120.0 * k
        aparts.append(beam(f"anem_arm{k}", (0.0, 0.0, 0.05), polar((0.0, 0.0), 1.0, a, 0.05), 0.1, 0.1, "rail"))
        aparts.append(K.ball(f"anem_cup{k}", polar((0.0, 0.0), 1.05, a, 0.05), 0.34, "steel_dk", 6))
    moving.append((aparts, "anemometer", (arm1[0], arm1[1], zt - 2.4 + 0.45), (0, 0, 1)))
    # ---- plumbing: the approved runs (the loop moved with the bunker) in brass and dark copper, flanges, supports,
    # valves with hand wheels
    RW.flanged_pipe(main, "loop", [(22.0, 3.0, 6.5), (26.5, -1.0, 6.5), (26.5, -8.0, 4.5), (23.0, -10.4, 4.5)], 1.4, "h_brass", "h_brass_dk")
    RW.flanged_pipe(main, "riser", [(24.0, 16.0, 9.0), (24.0, 16.0, 14.0), (20.0, 26.0, 14.0), (20.0, 30.0, 8.0)], 1.1, "copper")
    RW.flanged_pipe(main, "feed", [(9.0, -9.0, 3.0), (5.3, -6.2, 3.0)], 1.2, "copper_dk")
    RW.flanged_pipe(main, "bundle0", [(9.0, 16.0, 3.0), (4.0, 20.0, 3.0), (4.0, 26.0, 6.0)], 0.9, "copper")
    RW.flanged_pipe(main, "bundle1", [(11.0, 5.0, 2.5), (6.0, 1.0, 2.5), (2.0, -4.0, 2.5)], 0.9, "copper_dk", "copper")
    RW.flanged_pipe(main, "bundle2", [(24.0, 20.0, 12.0), (27.2, 14.6, 12.0), (27.2, 14.6, 3.0)], 0.8, "h_brass", "h_brass_dk")
    for k, (x, y, z) in enumerate(((26.5, -4.5, 3.9), (20.0, 28.0, 7.0), (27.2, 14.6, 0.0))):        # pipe supports
        main.append(obox(f"support{k}", (x, y), 0.0, 0.5, 0.5, 0.0, max(z, 1.0), "steel_dk"))
    main.append(K.cyl("valve0", (18.0, 9.5), 1.8, 10.0, 12.5, "steel", 10))
    RW.handwheel(main, "valve0_w", (18.0, 9.5), 13.3, 1.25)
    main.append(K.cyl("valve1", (19.0, 18.0), 1.6, 13.5, 16.0, "h_brass_dk", 10))
    RW.handwheel(main, "valve1_w", (19.0, 18.0), 16.8, 1.1)
    main.append(K.cyl("gauge", (16.2, 8.2), 0.6, 10.2, 11.3, "door_fr", 8))
    main.append(K.cyl("gauge_glass", (16.2, 8.2), 0.45, 11.3, 11.38, "glass", 8))
    for i, (x, y) in enumerate(((12.0, 12.0), (21.0, 16.0), (16.0, 3.0), (23.0, 6.0))):
        main.append(K.box(f"bracket{i}", x - 0.8, x + 0.8, y - 0.8, y + 0.8, 0.0, 4.0, "steel_dk"))
    # ---- the two chimney stacks: heavy, double-collared, rungs up the tall one, a red lamp on its rim
    for i, (c, r, z1) in enumerate(STACKS):
        main.extend(stack(f"stack{i}", c, r, 0.0, z1))
        anchors.append((f"fx_smoke_{i}", (c[0], c[1], z1), 0.45 if i == 0 else 0.35))
    (sx_, sy_), sr, sz = STACKS[0]
    for k in range(8):                                     # rungs up the tall stack (south side)
        main.append(beam(f"stack_rung{k}", (sx_ - 0.75, sy_ - sr - 0.25, 6.4 + 1.5 * k), (sx_ + 0.75, sy_ - sr - 0.25, 6.4 + 1.5 * k), 0.18, 0.18, "rail"))
    lp = (sx_ - sr * 1.05, sy_ - sr * 1.05, sz - 0.2)
    main.append(K.cyl("stack_lamp_s", lp[:2], 0.3, sz - 1.0, sz - 0.4, "steel_dk", 6))
    main.append(K.ball("stack_lamp", lp, 0.36, "red", 8))
    anchors.append(("fx_beacon_1", lp, 0.4))
    main.append(beam("stack_tie", (sx_, sy_, 12.0), (STACKS[1][0][0], STACKS[1][0][1], 12.0), 0.5, 0.4, "steel_dk"))
    # ---- barrels, lamp posts, a cable trunk
    for k, (x, y, rr, hh, mname) in enumerate(((28.3, 7.0, 0.95, 2.4, "rust"), (29.3, 9.0, 0.95, 2.4, "h_drum"), (27.4, 9.0, 0.95, 2.4, "h_bunker"))):
        main.append(K.cyl(f"barrel{k}", (x, y), rr, 0.0, hh, mname, 10))
        main.append(K.cyl(f"barrel{k}_r", (x, y), rr + 0.08, hh * 0.3, hh * 0.38, "h_black", 10))
        main.append(K.cyl(f"barrel{k}_r2", (x, y), rr + 0.08, hh * 0.66, hh * 0.74, "h_black", 10))
        main.append(K.cyl(f"barrel{k}_lid", (x, y), rr - 0.15, hh, hh + 0.08, "steel_dk", 10))
    for i, p in enumerate(((8.5, -18.0, 7.6), (29.0, 0.0, 9.0))):
        main.append(K.cyl(f"lamp{i}_foot", p[:2], 0.55, 0.0, 0.8, "steel_dk", 8))
        main.append(tube(f"lamp{i}_post", (p[0], p[1], 0.0), (p[0], p[1], p[2] - 0.4), 0.24, "rail", 6))
        main.append(K.cyl(f"lamp{i}_cap", p[:2], 0.42, p[2] + 0.72, p[2] + 0.95, "steel_dk", 8, 0.2))
        main.extend(K.lamp(f"lamp{i}", p, 0.6))
        anchors.append((f"fx_light_{i}", (p[0], p[1], p[2] + 0.25), 0.6))
    main.extend(K.pipe("cable", [pp(7.3, 1.0, 1.0), (27.0, -6.0, 0.6), (29.0, -1.2, 0.6)], 0.42, "rubber", 6))
    return lamp_d


# ------------------------------------------------------------------------------------------------

RW.setup(KEY, [("south scoop", lambda main, moving, anchors: scoop(main, moving, anchors, "s", P["mouth_s"], "03")),
               ("north scoop", lambda main, moving, anchors: scoop(main, moving, anchors, "n", P["mouth_n"], "04")),
               ("machinery", machinery)],
         col=COL, spec=SPEC, uv_scale=UV_SCALE, P=P, NODES=NODES, ANCHOR_DOC=ANCHOR_DOC, RIBS=RIBS, SHELL_MATS=SHELL_MATS,
         LIP_MATS=LIP_MATS, FLUE_TOP=FLUE_TOP, STAIN=STAIN, RUNOFF=RUNOFF, LOW=LOW, CAVITY=CAVITY)

if __name__ == "__main__":
    RW.main()
