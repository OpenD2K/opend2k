"""Reimagined Atreides wind trap - the redesign with real materials and human-scale detail (docs/3d-models.md
"Wind trap redesigns"; the first of the three, approved by the owner). The identity is the approved round-2
model's (atreides_wind_trap.py: two cream scoops one behind the other on a diagonal, mouths to the south-east with
three cyan louvres in a dark arch under a copper rim, a cream lip in front, house colour as a band over the crest
and a stripe behind the rim, the copper machinery east of them) - same layout, proportions, footprint and colours
(atreides_wind_trap.P). What is new:

  * the scoops are real shells: a dark core under cream PLATES with true joints (each plate one of five close cream
    tones), bolted rib flanges, a mouth that is a real recess (a steel duct with a turbine behind the louvres);
  * the louvres are steel slats carrying framed, segmented LIT PANELS (dark glass that glows) instead of plain cyan
    bars;
  * human-scale parts: service doors with frames, a step, a lit window and a lamp, maintenance ladders up the flanks
    to a roof hatch, a railed roof with a ladder on the control house, louvred vents, a control panel with status
    lights, hand wheels, pipe flanges and supports, lamp posts, conduits, a weather mast (vane, anemometer, beacon,
    whip antenna), stencilled unit numbers, hazard edges and warning plates;
  * materials: painted metal, bare / polished steel, copper, rubber, glass, roughness that differs per material;
    baked ambient occlusion in the ORM map (reimagined_kit.py);
  * weathering (the owner: "add some dirt as the previous model had"): large stains, run-off under joints and
    overhangs, rust out of the rib flanges, sand on top and in recesses, dirtier lower parts, grime in the creases,
    chipped edges, soot at the flue - its strength is windtrap_common's WEATHERING / MOTTLE (0 = the clean first
    test model);
  * moving parts are separate nodes (the engine's Building life spins them by name), effect points are empties
    (NODES / ANCHOR_DOC below).

This file is the Atreides house: its palette, the two part builders (scoop / machinery) and the layout. The helpers,
the surface / weathering pass, the bake and the command line are windtrap_common.py (shared with the Harkonnen and
Ordos wind traps):

    blender -b --factory-startup --python tools/models/reimagined_wind_trap.py -- --out DIR [--work DIR]
        [--no-bake] [--bake-size 2048] [--lod 0.34] [--weathering 1.0] [--mottle 1.0] [--preview DIR]
        [--turntable DIR --samples 24] [--tris]
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined

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
import windtrap_common as WC  # noqa: E402  (helpers, the surface / weathering pass, build, run)
import atreides_wind_trap as AW  # noqa: E402  (the approved layout: P)

K = WC.K
P = AW.P
PT = AW.PT
beam, tube, obox, polar = WC.beam, WC.tube, WC.obox, WC.polar
new_obj, rings_solid = WC.new_obj, WC.rings_solid
unit, Scoop, plate, bolt, arch_band, quad_bar, rotor = WC.unit, WC.Scoop, WC.plate, WC.bolt, WC.arch_band, WC.quad_bar, WC.rotor
stencil, door, railing, ladder, handwheel, flanged_pipe, warning_plate = (
    WC.stencil, WC.door, WC.railing, WC.ladder, WC.handwheel, WC.flanged_pipe, WC.warning_plate)

KEY = "ATREIDES_WIND_TRAP"

# Moving parts (separate nodes; origin on the spin axis, the axis is the node's local +Z in Blender = local +Y in the
# glb) and effect points (empties). Kept in one place: docs/3d-models.md quotes them.
NODES = dict(
    main="everything that does not move",
    turbine_s="the south scoop's intake turbine behind the louvres (spins about the scoop's axis)",
    turbine_n="the north scoop's intake turbine",
    fan_0="the cooler fan on top of the big generator drum (vertical axis)",
    vane="the wind vane on the weather mast (turns about the vertical: wind direction)",
    anemometer="the cup anemometer on the mast's arm (spins about the vertical)",
)
ANCHOR_DOC = dict(
    fx_vent_0="the south mouth (centre of the opening): air shimmer / dust drawn in; size = the opening's scale",
    fx_vent_1="the north mouth",
    fx_vent_2="the cooler's outlet on the generator drum: heat shimmer",
    fx_smoke_0="the open top of the exhaust flue (same point as fx_flue_0, which the round-2 model had)",
    fx_flue_0="(kept from the round-2 model) the open top of the exhaust flue",
    fx_beacon_0="the red beacon on the mast: a blinking light",
    fx_light_0="the lamp post at the control house (SceneLights' warm lamp)",
    fx_light_1="the lamp post east of the machinery",
)

# ------------------------------------------------------------------------------------------------
# Palette: the cream shells (the fittings are windtrap_common's; the copper machinery is atreides_kit's)

COL = dict(
    roof_a=(0.975, 0.930, 0.800),       # the plates: the original's warm cream in close tones
    roof_b=(0.930, 0.885, 0.760),
    roof_c=(0.968, 0.900, 0.745),
    roof_d=(0.880, 0.845, 0.740),
    roof_e=(0.820, 0.770, 0.650),       # a replaced plate now and then: a dustier stock
    rib_c=(0.850, 0.785, 0.650),        # the rib flanges
)
SPEC = dict(
    roof_a=(0.0, 0.44, "paint"), roof_b=(0.0, 0.50, "paint"), roof_c=(0.0, 0.47, "paint"),
    roof_d=(0.0, 0.53, "paint"), roof_e=(0.0, 0.56, "paint"), rib_c=(0.0, 0.48, "paint"),
)
FLUE_TOP = (25.0, 24.0, 16.0)        # px: the open top of the exhaust flue (machinery(): K.flue)
SHELL_MATS = ("roof_a", "roof_b", "roof_c", "roof_d", "roof_e", "rib_c", "wall")   # get the scoops' own streaks
LIP_MATS = ("wall",)                 # of those, the lip: run-off from the sill, no rib rust


# ------------------------------------------------------------------------------------------------
# A scoop


RIBS = (0.24, 0.36, 0.48, 0.62, 0.86)
BANDS_T = (0.035, 0.24, 0.36, 0.48, 0.62, 0.86, 0.965)
BANDS_A = (0.0, 22.0, 45.0, 68.0, 90.0, 112.0, 135.0, 158.0, 180.0)
TONES = ("roof_a", "roof_a", "roof_a", "roof_b", "roof_b", "roof_b", "roof_c", "roof_c", "roof_d", "roof_d", "roof_e")


def scoop(main, moving, anchors, tag, m, number):
    sc = Scoop(tag, m)
    R, sq, D = sc.R, sc.sq, sc.D
    H = R * sq
    rnd = random.Random(7 if tag == "s" else 11)
    wall, u_in = 1.5, 6.2
    # ---- the shell: a dark core, hollow behind the mouth (the intake), closed at the back
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
    main.append(K.sweep(f"{tag}_foot", a3, (b3[0] + sc.bx, b3[1] + sc.by, 0.0), [(t, r + 1.2) for (t, r) in prof], "foot", 24, 0.0,
                        2.4 / (R + 1.2), (0.0, 180.0), PT))
    # ---- cream plates with real joints, one of five close tones each
    for i, (t0, t1) in enumerate(zip(BANDS_T[:-1], BANDS_T[1:])):
        edges = BANDS_A if i < 5 else (0.0, 45.0, 90.0, 135.0, 180.0)
        for k, (a0, a1) in enumerate(zip(edges[:-1], edges[1:])):
            main.append(plate(f"{tag}_pl{i}_{k}", sc, t0, t1, a0, a1, 0.28, 0.5, rnd.choice(TONES), gap=0.6,
                              na=2 if i < 5 else 4))
    main.append(plate(f"{tag}_tipcap", sc, 0.955, 0.995, 0.0, 180.0, 0.5, 0.6, "rib_c", na=8, nt=2))
    # ---- rib flanges over the joints, bolted on the flank the camera sees and over the crest
    for i, t in enumerate(RIBS):
        hw = 0.42 / D
        main.append(plate(f"{tag}_rib{i}", sc, t - hw, t + hw, 0.0, 180.0, 0.62, 0.5, "rib_c", na=18, nt=1))
        for k, ang in enumerate((79.0, 101.0, 123.0, 146.0, 168.0)):
            main.append(bolt(f"{tag}_rib{i}_b{k}", sc.S(t, ang, 0.6), sc.N(t, ang), 0.42, 0.3))
    # ---- house colour: the band over the crest, the stripe behind the rim (top and the south-west flank)
    main.append(K.house(plate(f"{tag}_crest", sc, 0.655, 0.785, 55.0, 140.0, 0.62, 0.45, "HOUSE", na=10, nt=3), 0.4))
    for i, arc in enumerate(((84.0, 128.0), (156.0, 180.0))):
        main.append(K.house(plate(f"{tag}_stripe{i}", sc, 0.07, 0.17, arc[0], arc[1], 0.62, 0.45, "HOUSE", na=6, nt=3), 0.36))
    # ---- the mouth: copper rim with bolts, a steel duct, the turbine, three louvres with lit panels, a grated sill
    main.append(arch_band(f"{tag}_rim", sc, -0.85, 0.65, R + 0.65, R - 1.0, "copper"))
    for k in range(13):
        ang = 12.0 + 13.0 * k
        p = sc.at(0.0, ang, R - 0.15)
        main.append(bolt(f"{tag}_rimb{k}", (p.x - sc.bx * 0.85, p.y - sc.by * 0.85, p.z), (-sc.bx, -sc.by, 0.0), 0.46, 0.3, "copper_dk"))
    ri = R - wall
    main.append(arch_band(f"{tag}_duct", sc, 0.3, u_in - 0.2, ri + 0.1, ri - 0.3, "duct"))
    for k, u in enumerate((1.9, 3.5, 5.1)):                 # duct stiffening hoops
        main.append(arch_band(f"{tag}_ducth{k}", sc, u - 0.2, u + 0.2, ri - 0.2, ri - 0.75, "slat", 14))
    back = [(p.x + sc.bx * (u_in - 0.25), p.y + sc.by * (u_in - 0.25), p.z) for p in (sc.at(0.0, a, ri - 0.2) for a in angs)]
    bm = bmesh.new()
    rings_solid(bm, [back, [(x + sc.bx * 0.2, y + sc.by * 0.2, z) for (x, y, z) in back]])
    main.append(new_obj(f"{tag}_back", bm, "duct_dk"))
    zf = 3.7                                                # the sill: level with the top of the lip
    main.append(obox(f"{tag}_sill", sc.L(0.0, u_in / 2 - 0.1, 0.0)[:2], P["face_ang"], sc.half_at(ri, zf) + 0.4, u_in / 2, 0.0, zf, "grate"))
    for k in range(6):                                      # grating bars
        u = 0.9 + k * 0.85
        w = sc.half_at(ri, zf + 0.2) - 0.5
        main.append(quad_bar(f"{tag}_grate{k}", sc, -w, w, [(u - 0.22, zf), (u + 0.22, zf), (u + 0.22, zf + 0.2), (u - 0.22, zf + 0.2)], "slat"))
    # the turbine: a bellmouth ring on struts, the rotor a node of its own
    zc, rt, ut = 13.6, 8.4, 4.5
    axis = Vector((-sc.bx, -sc.by, 0.0))
    c3 = Vector(sc.L(0.0, ut, zc))
    ring_pts = []
    for (r, u) in ((rt + 0.3, -0.9), (rt + 1.25, -0.9), (rt + 0.95, 0.9), (rt + 0.3, 0.9)):
        ring_pts.append((r, u))
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
        reach = rt + 1.0                                    # out to the duct wall / down to the sill, not through them
        while reach < rt + 12.0:
            q = d * (reach + 0.25)
            if zc + q.z < zf + 0.2 or abs(q.dot(side)) > sc.half_at(ri - 0.4, zc + q.z):
                break
            reach += 0.25
        main.append(beam(f"{tag}_strut{k}", tuple(c3 + d * (rt + 1.0)), tuple(c3 + d * reach), 0.5, 0.35, "slat"))
    parts = rotor(f"{tag}_turbine", 9, 1.4, rt, 1.7, 2.9, 0.3, 32.0, "blade", hub=(1.7, -0.7, 0.5, 1.9), tips=0.9)
    moving.append((parts, f"turbine_{tag}", tuple(c3), tuple(axis)))
    anchors.append((f"fx_vent_{0 if tag == 's' else 1}", sc.L(0.0, 0.5, zc), 1.0))
    # the louvres: steel slats, the upper face tilted outwards, carrying framed lit panels in segments
    post_top = sc.half_at(ri, 0.0) * sq - 1.2
    main.append(beam(f"{tag}_post", sc.L(0.0, -0.05, zf), sc.L(0.0, -0.05, post_top), 0.7, 0.9, "copper_dk"))
    for i, z in enumerate((5.6, 11.5, 17.5)):
        w = sc.half_at(ri - 0.2, z + 0.5) - 0.15
        a_, b_ = (-0.75, z - 0.3), (1.35, z + 0.5)
        ln = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        nu, nz = -(b_[1] - a_[1]) / ln, (b_[0] - a_[0]) / ln            # the upper face's normal (out and up)
        main.append(quad_bar(f"{tag}_slat{i}", sc, -w, w, [a_, b_, (b_[0] - nu * 0.62, b_[1] - nz * 0.62), (a_[0] - nu * 0.62, a_[1] - nz * 0.62)], "slat"))
        f0, f1 = 0.07, 0.55                                              # the lit strip along the leading edge of the upper face
        p0 = (a_[0] + (b_[0] - a_[0]) * f0 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f0 + nz * 0.02)
        p1 = (a_[0] + (b_[0] - a_[0]) * f1 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f1 + nz * 0.02)
        sec = [p0, p1, (p1[0] + nu * 0.14, p1[1] + nz * 0.14), (p0[0] + nu * 0.14, p0[1] + nz * 0.14)]
        for sgn in (-1, 1):                                             # left / right of the post
            s0, s1 = 0.75, w - 0.7
            nseg = max(1, round((s1 - s0) / 7.0))
            for k in range(nseg):
                x0 = s0 + (s1 - s0) * k / nseg + 0.16
                x1 = s0 + (s1 - s0) * (k + 1) / nseg - 0.16
                main.append(quad_bar(f"{tag}_lens{i}_{sgn}_{k}", sc, min(sgn * x0, sgn * x1), max(sgn * x0, sgn * x1), sec, "glow_a" if (k + i) % 2 == 0 else "glow_b"))
        for sgn in (-1, 1):                                             # end brackets to the duct
            main.append(quad_bar(f"{tag}_slatend{i}_{sgn}", sc, sgn * (w - 0.5), sgn * (w + 0.5), [(-0.5, z - 0.8), (1.2, z - 0.4), (1.2, z + 0.9), (-0.5, z + 0.5)], "copper_dk"))
    # ---- the lip: the cream ramp in front of the mouth, its inlay, a hazard edge at the front
    Lp = P["lip"]
    q = [sc.L(-(R + 0.5), 0.0, 0.0)[:2], sc.L(R + 0.5, 0.0, 0.0)[:2], sc.L(R - 3.0, -Lp, 0.0)[:2], sc.L(-(R - 3.0), -Lp, 0.0)[:2]]
    main.append(K.ramp(f"{tag}_lip", q[2:], q[:2], 0.0, 1.2, 3.4, "wall", 0.3))

    def lerp(a_, b_, f):
        return (a_[0] * (1 - f) + b_[0] * f, a_[1] * (1 - f) + b_[1] * f)
    main.append(K.ramp(f"{tag}_lip_in", [lerp(q[2], q[3], 0.15), lerp(q[2], q[3], 0.85)], [lerp(q[0], q[1], 0.15), lerp(q[0], q[1], 0.85)],
                       0.5, 1.5, 3.7, "roof_b", 0.1))
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
    # ---- the flank the camera sees: a maintenance ladder to a roof hatch, the unit number, a service door, vents
    tl = 0.42
    stand = 0.75
    la = [tuple(sc.S(tl - 0.75 / D, a, stand)) for a in range(176, 100, -9)]
    lb = [tuple(sc.S(tl + 0.75 / D, a, stand)) for a in range(176, 100, -9)]
    ladder(main, f"{tag}_ladder", la, lb)
    for k, a in enumerate((172.0, 150.0, 128.0, 106.0)):                # stand-offs
        for dt in (-0.75 / D, 0.75 / D):
            main.append(beam(f"{tag}_lad_st{k}_{dt > 0}", tuple(sc.S(tl + dt, a, 0.2)), tuple(sc.S(tl + dt, a, stand)), 0.22, 0.22, "rail"))
    hp, hn = sc.S(tl, 97.0, 0.3), sc.N(tl, 97.0)                         # the hatch near the crest
    main.append(K.tube(f"{tag}_hatch", tuple(hp), tuple(hp + hn * 0.7), 1.5, "rib_c", 12))
    main.append(K.tube(f"{tag}_hatch_lid", tuple(hp + hn * 0.7), tuple(hp + hn * 0.95), 1.25, "door", 12))
    main.append(beam(f"{tag}_hatch_bar", tuple(hp + hn * 1.1 - Vector((sc.bx, sc.by, 0)) * 0.8), tuple(hp + hn * 1.1 + Vector((sc.bx, sc.by, 0)) * 0.8), 0.2, 0.2, "door_fr"))
    stencil(main, f"{tag}_num", sc, number, 0.205, 150.0, 1.7, 10.5, 0.36)
    # service door in a small porch between the ribs
    td = 0.55
    rd = sc.rad(td)
    base = sc.L(-(rd - 0.9), td * D, 0.0)[:2]
    main.append(obox(f"{tag}_porch", base, P["face_ang"] + 90.0, 2.15, 2.3, 0.0, 6.5, "roof_d", 0.0, 0.96))
    main.append(obox(f"{tag}_porch_cap", base, P["face_ang"] + 90.0, 2.45, 2.6, 6.5, 6.95, "rib_c"))
    door_c = sc.L(-(rd - 0.9) - 2.3, td * D, 0.0)[:2]
    door(main, f"{tag}_door", door_c, P["face_ang"] + 180.0, 0.0, 2.5, 4.9)
    warning_plate(main, f"{tag}_warn", sc.L(-(rd - 0.9) - 2.26, td * D + 1.72, 0.0)[:2], P["face_ang"] + 180.0, 3.3, 1.25)
    # a louvred vent box low at the back, a conduit along the foot from the porch to a junction box by the mouth
    tv = 0.74
    rv = sc.rad(tv)
    main.extend(K.vent(f"{tag}_vent", sc.L(-(rv - 0.55), tv * D, 0.0)[:2], P["face_ang"] + 90.0, 1.7, 2.4, 4.6, 0.55, 3, "door_fr"))
    jb = sc.L(-(sc.rad(0.3) + 0.3), 0.3 * D, 0.0)
    main.append(obox(f"{tag}_jbox", jb[:2], P["face_ang"] + 90.0, 0.9, 0.5, 1.4, 3.6, "door_fr"))
    cpts = [sc.L(-(sc.rad(t) + 0.75), t * D, 1.9) for t in (0.3, 0.36, 0.42, 0.485)]
    for k, (p0, p1) in enumerate(zip(cpts[:-1], cpts[1:])):
        main.append(tube(f"{tag}_conduit{k}", p0, p1, 0.3, "rubber", 6))
        main.append(obox(f"{tag}_clamp{k}", ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2), P["face_ang"] + 90.0, 0.25, 0.5, 1.3, 2.4, "door_fr"))
    return sc


# ------------------------------------------------------------------------------------------------
# The machinery east of the scoops


def machinery(main, moving, anchors):
    main.append(obox("mach_base", (17.0, 11.0), 30.0, 9.0, 7.0, 0.0, 3.0, "foot", 0.4))
    # ---- the big generator drum: saddles, the band, cooling fins, an inspection cover, a cooler with a fan on top
    ga, gb, gr = Vector((10.0, 14.0, 8.0)), Vector((24.0, 20.0, 8.0)), 6.2
    gd = (gb - ga).normalized()
    gang = math.degrees(math.atan2(gd.y, gd.x))
    gs = Vector((gd.y, -gd.x, 0.0))                      # the side the camera sees (south-east)
    main.append(K.tank("gen0", tuple(ga), tuple(gb), gr, "copper", 20, 0.4))
    main.append(K.hoop("gen0_band", tuple(ga), tuple(gb), 0.42, 0.58, gr + 0.3, "copper_dk", 20))
    main.append(K.hoop("gen0_flange", tuple(ga), tuple(gb), 0.115, 0.15, gr + 0.4, "copper_dk", 20))
    for k in range(5):
        t = 0.66 + 0.058 * k
        main.append(K.hoop(f"gen0_fin{k}", tuple(ga), tuple(gb), t, t + 0.022, gr + 0.45, "copper_dk", 16))
    for k, t in enumerate((0.24, 0.78)):
        c = ga + (gb - ga) * t
        main.append(obox(f"gen0_saddle{k}", (c.x, c.y), gang + 90.0, 5.2, 0.8, 3.0, 6.4, "steel_dk"))
    cd = (gs * 0.72 + Vector((0, 0, 0.69))).normalized()
    cc = ga + (gb - ga) * 0.285
    main.append(tube("gen0_cover", tuple(cc + cd * (gr - 0.2)), tuple(cc + cd * (gr + 0.55)), 1.75, "steel", 12))
    main.append(tube("gen0_cover_in", tuple(cc + cd * (gr + 0.55)), tuple(cc + cd * (gr + 0.7)), 1.2, "copper_dk", 12))
    for k in range(6):
        a = math.radians(60.0 * k)
        e1 = gd
        e2 = cd.cross(gd).normalized()
        main.append(bolt(f"gen0_cover_b{k}", cc + cd * (gr + 0.55) + (e1 * math.cos(a) + e2 * math.sin(a)) * 1.45, cd, 0.32, 0.22))
    tb = ga + (gb - ga) * 0.62 + gs * (gr * 0.93) + Vector((0, 0, -1.2))   # terminal box + conduit to the base
    main.append(obox("gen0_term", (tb.x, tb.y), gang, 1.3, 0.55, 5.6, 8.4, "steel_dk"))
    main.append(obox("gen0_term_lid", (tb.x + gs.x * 0.5, tb.y + gs.y * 0.5), gang, 1.0, 0.1, 6.0, 8.0, "door_fr"))
    main.append(tube("gen0_term_c", (tb.x - gd.x * 0.6, tb.y - gd.y * 0.6, 5.7), (tb.x - gd.x * 0.6, tb.y - gd.y * 0.6, 3.0), 0.3, "rubber", 6))
    # the cooler: a steel box on the drum's crown, a round opening with the fan under a guard
    fc = ga + (gb - ga) * 0.2
    z0, z1 = 8.0 + gr - 0.7, 8.0 + gr + 1.45
    main.append(obox("cooler", (fc.x, fc.y), gang, 2.45, 2.3, z0, z1, "steel"))
    main.extend(K.vent("cooler_v", (fc.x + gs.x * 2.3, fc.y + gs.y * 2.3), gang, 1.5, z0 + 0.75, z1 - 0.4, 0.3, 3, "door_fr"))
    main.append(K.cyl("cooler_well", (fc.x, fc.y), 1.8, z1 - 0.05, z1 + 0.04, "duct_dk", 16))
    main.append(K.lathe_loop("cooler_ring", (fc.x, fc.y), [(1.8, z1), (2.1, z1), (2.1, z1 + 0.62), (1.8, z1 + 0.62)], "door_fr", 16))
    for k, a in enumerate((gang + 45.0, gang + 135.0)):
        main.append(beam(f"cooler_guard{k}", polar((fc.x, fc.y), 1.95, a, z1 + 0.6), polar((fc.x, fc.y), 1.95, a + 180.0, z1 + 0.6), 0.2, 0.16, "door_fr"))
    moving.append((rotor("fan_0", 5, 0.3, 1.6, 0.6, 1.0, 0.1, 24.0, "blade", hub=(0.36, -0.06, 0.1, 0.25)), "fan_0", (fc.x, fc.y, z1 + 0.2), (0, 0, 1)))
    anchors.append(("fx_vent_2", (fc.x, fc.y, z1 + 0.8), 0.35))
    # ---- the smaller rust drum
    ha, hb = (13.0, 5.0, 6.0), (24.0, 9.5, 6.0)
    main.append(K.tank("gen1", ha, hb, 4.8, "rust", 16, 0.4))
    main.append(K.hoop("gen1_band", ha, hb, 0.2, 0.3, 5.1, "copper", 16))
    main.append(K.hoop("gen1_band2", ha, hb, 0.7, 0.75, 5.1, "copper_dk", 16))
    hang = math.degrees(math.atan2(hb[1] - ha[1], hb[0] - ha[0]))
    for k, t in enumerate((0.3, 0.82)):
        c = (ha[0] + (hb[0] - ha[0]) * t, ha[1] + (hb[1] - ha[1]) * t)
        main.append(obox(f"gen1_saddle{k}", c, hang + 90.0, 4.2, 0.7, 3.0, 4.6, "steel_dk"))
    # ---- the control house (the pump block): door, control panel, vent, a railed roof with a ladder, the copper dome
    pc, pang, phl, phw, ph = (15.0, -15.0), 20.0, 7.5, 5.5, 8.0
    ex, ey = unit(pang)
    nx, ny = -ey, ex                                      # north (across); the camera sees the south face (-n)

    def pp(e, d, z=None):                                 # e along the house, d out of the south face
        q = (pc[0] + ex * e - nx * d, pc[1] + ey * e - ny * d)
        return q if z is None else (q[0], q[1], z)
    main.append(obox("pump", pc, pang, phl, phw, 0.0, ph, "skirt", 0.5, 0.85))
    main.append(obox("pump_plinth", pc, pang, phl + 0.12, phw + 0.12, 0.0, 1.1, "foot"))
    main.append(obox("pump_cornice", pc, pang, phl * 0.85 + 0.3, phw * 0.85 + 0.3, ph - 0.1, ph + 0.4, "copper_dk"))
    main.append(K.dome("pump_dome", pc, 4.2, 7.8 + 0.4, 3.4, "copper"))
    main.append(K.lathe_loop("pump_dome_ring", pc, [(4.2, ph + 0.3), (4.55, ph + 0.3), (4.55, ph + 0.95), (4.2, ph + 0.95)], "copper_dk", 24))
    main.append(K.cyl("pump_dome_cap", pc, 0.9, ph + 3.6, ph + 4.3, "copper_dk", 10))
    de, pe, ve = 1.0, -1.95, -4.3                          # along the south face: door, control panel, vent (the ladder: 5.3)
    main.append(obox("porch", pp(de, 5.25), pang, 2.0, 0.75, 0.0, 6.4, "wall", 0.0, 0.97))
    main.append(obox("porch_cap", pp(de, 5.3), pang, 2.25, 0.95, 6.4, 6.8, "rib_c"))
    lamp_d = door(main, "door", pp(de, 6.0), pang - 90.0, 0.0, 2.6, 5.0)
    main.append(obox("panel", pp(pe, 5.45), pang, 0.85, 0.4, 2.5, 4.9, "steel_dk"))
    main.append(obox("panel_face", pp(pe, 5.87), pang, 0.7, 0.05, 2.7, 4.7, "door_fr"))
    for k, (dl, z, mname) in enumerate(((-0.35, 4.25, "green"), (0.0, 4.25, "green"), (0.35, 4.25, "red"), (-0.2, 3.3, "glass"), (0.25, 3.3, "glass"))):
        main.append(obox(f"panel_led{k}", pp(pe + dl, 5.94), pang, 0.12 if z > 4 else 0.2, 0.04, z - (0.12 if z > 4 else 0.3), z + (0.12 if z > 4 else 0.3), mname))
    warning_plate(main, "panel_warn", pp(pe, 5.36), pang - 90.0, 5.3, 1.3)
    main.extend(K.vent("pump_vent", pp(ve, 5.18), pang, 1.2, 3.4, 5.6, 0.5, 3, "door_fr"))
    # roof: railing on three sides, the ladder up the south face
    tl_, tw_ = phl * 0.85 - 0.35, phw * 0.85 - 0.35
    zr = ph + 0.4
    railing(main, "roof_rail", [pp(-tl_, -tw_, zr), pp(-tl_, tw_, zr), pp(4.5, tw_, zr)], 2.0)
    railing(main, "roof_rail2", [pp(6.1, tw_, zr), pp(tl_, tw_, zr), pp(tl_, -tw_, zr)], 2.0)
    ladder(main, "roof_ladder", [pp(4.7, 6.1, 0.15), pp(4.7, 5.15, zr + 2.0)], [pp(5.9, 6.1, 0.15), pp(5.9, 5.15, zr + 2.0)])
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
    # ---- plumbing: the approved runs with flanges, supports, valves with hand wheels, the small tank with a gauge
    flanged_pipe(main, "loop", [(21.0, 0.0, 6.5), (25.5, -4.0, 6.5), (25.5, -11.0, 4.5), (21.0, -14.0, 4.5)], 1.4, "orange")
    flanged_pipe(main, "riser", [(24.0, 16.0, 9.0), (24.0, 16.0, 14.0), (20.0, 26.0, 14.0), (20.0, 30.0, 8.0)], 1.1, "copper")
    flanged_pipe(main, "feed", [(8.0, -12.0, 3.0), (4.0, -8.0, 3.0)], 1.2, "copper_dk")
    flanged_pipe(main, "bundle0", [(9.0, 16.0, 3.0), (4.0, 20.0, 3.0), (4.0, 26.0, 6.0)], 0.9, "copper")
    flanged_pipe(main, "bundle1", [(11.0, 5.0, 2.5), (6.0, 1.0, 2.5), (2.0, -4.0, 2.5)], 0.9, "copper_dk", "copper")
    flanged_pipe(main, "bundle2", [(24.0, 20.0, 12.0), (28.0, 14.0, 12.0), (28.0, 14.0, 3.0)], 0.8, "orange")
    for k, (x, y, z) in enumerate(((25.5, -7.5, 3.3), (20.0, 28.0, 7.0), (28.0, 14.0, 0.0))):        # pipe supports
        main.append(obox(f"support{k}", (x, y), 0.0, 0.5, 0.5, 0.0, max(z, 1.0), "steel_dk"))
    main.append(K.cyl("valve0", (18.0, 9.5), 1.8, 10.0, 12.5, "steel", 10))
    handwheel(main, "valve0_w", (18.0, 9.5), 13.3, 1.25)
    main.append(K.cyl("valve1", (19.0, 18.0), 1.6, 13.5, 16.0, "copper_dk", 10))
    handwheel(main, "valve1_w", (19.0, 18.0), 16.8, 1.1)
    main.append(K.tank("tank_small", (22.0, -8.0, 3.0), (27.0, -6.0, 3.0), 2.6, "rust", 12, 0.4))
    main.append(K.hoop("tank_small_band", (22.0, -8.0, 3.0), (27.0, -6.0, 3.0), 0.44, 0.56, 2.8, "copper_dk", 12))
    main.append(K.cyl("gauge", (24.5, -7.0), 0.6, 5.4, 6.3, "door_fr", 8))
    main.append(K.cyl("gauge_glass", (24.5, -7.0), 0.45, 6.3, 6.38, "glass", 8))
    for i, (x, y) in enumerate(((12.0, 12.0), (21.0, 16.0), (16.0, 3.0), (23.0, 6.0))):
        main.append(K.box(f"bracket{i}", x - 0.8, x + 0.8, y - 0.8, y + 0.8, 0.0, 4.0, "steel_dk"))
    fl, top = K.flue("flue", (25.0, 24.0), 1.3, 6.0, 16.0)
    main.extend(fl)
    main.append(K.cyl("flue_foot", (25.0, 24.0), 2.0, 0.0, 6.2, "steel_dk", 10, 1.5))
    for k in range(4):                                     # rungs up the flue
        main.append(beam(f"flue_rung{k}", (24.3, 22.6, 7.6 + 1.5 * k), (25.7, 22.6, 7.6 + 1.5 * k), 0.18, 0.18, "rail"))
    anchors.append(("fx_flue_0", top, 0.4))
    anchors.append(("fx_smoke_0", top, 0.4))
    # ---- lamp posts (the round-2 model's two lamp beads, now on posts), a cable trunk between the houses
    for i, p in enumerate(((7.5, -21.0, 7.6), (29.0, 0.0, 9.0))):
        main.append(K.cyl(f"lamp{i}_foot", p[:2], 0.55, 0.0, 0.8, "steel_dk", 8))
        main.append(tube(f"lamp{i}_post", (p[0], p[1], 0.0), (p[0], p[1], p[2] - 0.4), 0.24, "rail", 6))
        main.append(K.cyl(f"lamp{i}_cap", p[:2], 0.42, p[2] + 0.72, p[2] + 0.95, "steel_dk", 8, 0.2))   # (small: the camera looks down on the bead)
        main.extend(K.lamp(f"lamp{i}", p, 0.6))
        anchors.append((f"fx_light_{i}", (p[0], p[1], p[2] + 0.25), 0.6))
    main.extend(K.pipe("cable", [pp(7.2, 2.0, 1.0), pp(9.5, 0.5, 0.6), (23.5, -3.2, 0.6), (26.0, 3.5, 0.6), (26.0, 5.5, 2.4)], 0.42, "rubber", 6))
    return lamp_d


# ------------------------------------------------------------------------------------------------

WC.setup(KEY, [("south scoop", lambda main, moving, anchors: scoop(main, moving, anchors, "s", P["mouth_s"], "01")),
               ("north scoop", lambda main, moving, anchors: scoop(main, moving, anchors, "n", P["mouth_n"], "02")),
               ("machinery", machinery)],
         col=COL, spec=SPEC, P=P, PT=PT, NODES=NODES, ANCHOR_DOC=ANCHOR_DOC, RIBS=RIBS, SHELL_MATS=SHELL_MATS, LIP_MATS=LIP_MATS,
         FLUE_TOP=FLUE_TOP)

if __name__ == "__main__":
    WC.main()
