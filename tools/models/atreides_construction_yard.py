"""Atreides construction yard - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_construction_yard.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_CONSTRUCTION_YARD .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (DATA.R16 frame 3103 = main, 3104 damaged, no base frame; animation 0 = 14 overlay frames of
the crane, HAS_ANIMATION; build-up 0 = the MCV unfolding + the generic rock mound, kept original):
  * a fat cream drum, inclined like a mixer (upper end north-west, 38 px up; lower end south-east, 23 px), with two
    house-colour hoops near the upper end and a house-colour collar with a dark mouth at the lower end, on a
    grey-beige machine block;
  * south-west of it the round walled yard: a broad cream berm (outer radius 27, crest 19), a brown floor (r 15);
    copper claws at its south-west foot, a white cabin with a pale window on the berm;
  * the crane: a rust machine house at the west and a long cream boom lying north-west to south-east over the berm;
  * east: a wing with two dark house-colour roof panels, wrapped by a brown curved conveyor wall that starts behind
    the drum and swings round the north-east down to the south-east; a small cream dish on a mast at the far east;
  * a low gate with house-colour bars between the block and the yard.
  * Light: shadows fall to the north-east (sun from the south-west).
Animation (12 poses -> the 14 overlay frames, as the original): the boom swings clockwise out to the WEST, away from
the building, lowers its hook to the ground, lifts a load, swings back and lowers it into the yard; then a blue
assembly glow in the yard.
Free choices: vents, lamps, seams, the hub in the yard, pipe runs.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_CONSTRUCTION_YARD"
P = dict(
    drum_a=(-16.0, 54.0, 27.8), drum_b=(1.0, 21.5, 27.8), drum_r=12.1,
    block_c=(-5.5, 36.5), block_ang=-66.0, block_hl=15.0, block_hw=9.5, block_z=17.0,
    pit_out=((-19.5, 3.5), 21.5), pit_top=((-17.0, 6.5), 18.5), pit_in=((-13.5, 9.5), 14.5), pit_h=8.0,
    wing_c=(21.5, 19.5), wing_ang=-55.0, wing_hl=24.0, wing_hw=14.0, wing_z=12.0,
    crane_pivot=(-34.5, 44.0, 24.0), crane_tip=(-18.0, 20.0, 17.0),
    dish=(42.0, 43.0, 7.0),
)


def unit(ang):
    return math.cos(math.radians(ang)), math.sin(math.radians(ang))


def along(c, ang, t, s=0.0):
    ax, ay = unit(ang)
    return (c[0] + ax * t - ay * s, c[1] + ay * t + ax * s)


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def build(damaged=False):
    main, crane, hook, load, glow = [], [], [], [], []
    # ---- machine block under the drum
    bc, ba, bl, bw, bz = P["block_c"], P["block_ang"], P["block_hl"], P["block_hw"], P["block_z"]
    main.append(K.obox("block_foot", bc, ba, bl + 1.5, bw + 1.5, 0.0, 3.0, "foot", 0.3))
    main.append(K.obox("block", bc, ba, bl, bw, 2.5, bz, "stone_dk", 0.5, 0.92))
    main.append(K.obox("block_top", bc, ba, bl - 1.5, bw - 1.5, bz - 0.5, bz + 1.5, "block_dk", 0.3))
    for i, t in enumerate((-9.0, -3.0, 3.0, 9.0)):
        main.extend(K.vent(f"block_vent{i}", along(bc, ba, t, -bw - 0.05), ba, 2.0, 6.0, 11.0))
    for i, t in enumerate((-5.0, 5.0)):
        main.extend(K.vent(f"block_ventb{i}", along(bc, ba + 90.0, t, -bl - 0.05), ba + 90.0, 2.4, 5.0, 10.0))
    # cradles holding the drum
    a, b, r = P["drum_a"], P["drum_b"], P["drum_r"]
    for i, t in enumerate((0.25, 0.75)):
        p = lerp(a, b, t)
        main.append(K.obox(f"cradle{i}", (p[0], p[1]), ba + 90.0, r * 0.95, 2.0, bz - 1.0, p[2] - r * 0.3, "rust", 0.3))
    # ---- the drum
    drum = K.tank("drum", a, b, r, "roof", 24, 0.45)
    main.append(drum)
    for i, (t0, t1) in enumerate(((0.12, 0.21), (0.28, 0.37), (0.44, 0.53))):   # house panels on the north-east flank
        main.append(K.house(K.hoop(f"drum_band{i}", a, b, t0, t1, r + 0.25, "HOUSE", 24, (100.0, 172.0)), 0.3))
    for i, t in enumerate((0.09, 0.245, 0.405, 0.57, 0.73, 0.9)):
        main.append(K.hoop(f"drum_rib{i}", a, b, t - 0.007, t + 0.007, r + 0.18, "roof2" if i % 2 else "wall", 24))
    # the lower mouth: house-colour collar with a dark bore
    c0, c1 = lerp(a, b, 0.985), lerp(a, b, 1.14)
    main.append(K.house(K.sweep("drum_collar", c0, c1, [(0.0, r * 0.45), (1.0, r * 0.4)], "HOUSE", 20), 0.17))
    main.append(K.sweep("drum_mouth", lerp(a, b, 1.10), lerp(a, b, 1.15), [(0.0, r * 0.24), (1.0, r * 0.24)], "recess", 14))
    main.append(K.sweep("drum_cap", lerp(a, b, -0.06), lerp(a, b, 0.02), [(0.0, r * 0.45), (1.0, r * 0.5)], "rim", 16))
    # ---- the yard: an eccentric berm (broad to the south-west, thin to the east), floor
    (co, ro), (ct, rt), (ci, ri), ph = P["pit_out"], P["pit_top"], P["pit_in"], P["pit_h"]
    pc = ci
    main.append(K.loop_ring("pit_berm", [(co, ro, 0.0), (co, ro - 0.6, 1.8), (ct, rt + 0.6, ph - 0.5), (ct, rt, ph), (ci, ri + 0.8, ph),
                                         (ci, ri, ph - 0.9), (ci, ri, 0.0)], "stone", 64))
    main.append(K.lathe("pit_floor", pc, [(ri + 0.5, 0.0), (ri + 0.5, 1.4), (0.02, 1.4)], "skirt", 48))
    main.append(K.loop_ring("pit_foot", [(co, ro + 1.0, 0.0), (co, ro + 1.0, 1.4), (co, ro - 0.3, 2.2), (co, ro - 0.3, 0.0)], "foot", 64))
    main.append(K.loop_ring("pit_crest", [(ct, rt + 0.3, ph - 0.2), (ct, rt + 0.3, ph + 0.5), (ci, ri + 0.5, ph + 0.5), (ci, ri + 0.5, ph - 0.2)], "roof", 64))
    for k in range(24):                                              # ribs down the berm, west round to the south-east
        ang = 120.0 + 9.0 * k
        p0, p1 = K.polar(co, ro - 0.2, ang, 0.6), K.polar(ct, rt + 0.5, ang, ph - 0.3)
        main.append(K.beam(f"pit_rib{k}", p0, p1, 1.3 if k % 2 else 0.9, 0.8, "roof2" if k % 2 else "copper_dk", 0.1))
    main.append(K.lathe("pit_hub", pc, [(4.2, 1.4), (4.2, 2.2), (3.0, 2.8), (0.02, 2.8)], "rust", 20))
    for k in range(8):
        main.append(K.seam(f"pit_seam{k}", K.polar(pc, 4.4, 45.0 * k + 10.0, 1.5), K.polar(pc, ri - 0.3, 45.0 * k + 10.0, 1.5)))
    main.append(K.lathe_loop("pit_ring", pc, [(10.0, 1.3), (10.0, 1.65), (9.4, 1.65), (9.4, 1.3)], "rib", 40))
    # a low machine block at the south-east foot of the berm
    main.append(K.box("front_block", -19.0, -2.0, -22.5, -10.0, 0.0, 5.0, "stone_dk", 0.5))
    main.append(K.box("front_top", -17.0, -4.0, -22.0, -12.0, 4.6, 6.0, "block_dk", 0.3))
    main.extend(K.vent("front_vent", (-10.5, -24.05), 0.0, 5.0, 1.2, 3.8))
    main.append(K.box("front_step", -3.0, 3.5, -15.0, -5.0, 0.0, 2.0, "concrete_dk", 0.3))
    main.append(K.obox("west_box", (-43.5, 9.0), 100.0, 4.5, 2.5, 0.0, 7.5, "rust", 0.4))
    # the white cabin on the berm, south-west
    cab = K.polar(co, 18.0, 190.0)
    main.append(K.obox("cabin", cab, 100.0, 4.6, 3.4, 3.0, 11.5, "white", 0.4))
    main.append(K.obox("cabin_win", K.polar(co, 20.6, 190.0), 100.0, 3.4, 0.9, 7.0, 10.2, "glow_dk"))
    main.append(K.obox("cabin_roof", cab, 100.0, 5.0, 3.8, 11.2, 12.2, "roof2", 0.2))
    # copper claws at the foot
    for i, ang in enumerate((200.0, 215.0, 232.0, 248.0)):
        p0, p1, p2 = K.polar(co, ro - 4.0, ang, 4.5), K.polar(co, ro + 1.5, ang, 3.2), K.polar(co, ro + 3.5, ang + 4.0, 0.5)
        main.extend(K.pipe(f"claw{i}", [p0, p1, p2], 1.25, "copper" if i % 2 else "orange"))
    # ---- crane house at the west, the boom
    cp, tip = P["crane_pivot"], P["crane_tip"]
    main.append(K.obox("crane_house", (cp[0] + 1.0, cp[1] - 9.0), -50.0, 8.0, 6.0, 0.0, 13.0, "skirt", 0.5, 0.9))
    main.append(K.obox("crane_house2", (cp[0] - 6.0, cp[1] - 23.0), -50.0, 5.0, 4.5, 0.0, 10.0, "rust", 0.4))
    main.append(K.dome("crane_dome", (cp[0] - 7.0, cp[1] - 21.0), 4.8, 9.0, 4.5, "copper"))
    main.append(K.obox("machine", (-23.0, 38.0), -55.0, 11.0, 6.5, 0.0, 18.0, "stone", 0.5, 0.9))
    main.append(K.obox("machine_top", (-23.0, 38.0), -55.0, 8.0, 4.0, 17.5, 19.5, "block_dk", 0.3))
    main.extend(K.vent("machine_vent", K.polar((-23.0, 38.0), 6.55, -145.0), -55.0, 6.0, 7.0, 13.0))
    main.append(K.cyl("crane_post", (cp[0], cp[1]), 3.2, 0.0, cp[2] - 1.0, "copper_dk", 16))
    main.append(K.cyl("crane_cap", (cp[0], cp[1]), 4.2, cp[2] - 1.5, cp[2] + 0.4, "wall", 16))
    main.extend(K.pipe("crane_pipe", [(cp[0] + 6.0, cp[1] - 14.0, 14.5), (cp[0] + 9.0, cp[1] - 11.0, 14.5), (cp[0] + 9.0, cp[1] - 11.0, 6.0)], 1.0, "copper"))
    crane.append(K.beam("boom", (cp[0], cp[1], cp[2] + 2.0), (tip[0], tip[1], tip[2] + 1.6), 4.4, 3.4, "white", 0.35))
    crane.append(K.beam("boom_rail", (cp[0], cp[1], cp[2] + 3.9), (tip[0], tip[1], tip[2] + 3.5), 1.2, 0.5, "rib"))
    crane.append(K.obox("boom_back", (cp[0], cp[1]), 130.0, 3.6, 3.0, cp[2] + 0.2, cp[2] + 4.6, "roof2", 0.4))
    crane.append(K.cyl("boom_head", (tip[0], tip[1]), 2.3, tip[2] - 0.6, tip[2] + 3.6, "copper", 12))
    # the cable and hook (node "hook": origin at the boom tip; the poses move it with the tip and stretch it down)
    hook.append(K.tube("boom_cable", (tip[0], tip[1], tip[2]), (tip[0], tip[1], tip[2] - 6.5), 0.35, "steel_dk", 6))
    hook.append(K.cyl("boom_hook", (tip[0], tip[1]), 1.4, tip[2] - 8.0, tip[2] - 6.2, "rust", 8, 0.5))
    # the load the crane fetches (node "load": origin on its top; hidden at rest)
    load.append(K.box("load_box", tip[0] - 2.6, tip[0] + 2.6, tip[1] - 2.0, tip[1] + 2.0, tip[2] - 4.4, tip[2], "rust", 0.3))
    load.append(K.box("load_band", tip[0] - 2.8, tip[0] + 2.8, tip[1] - 0.7, tip[1] + 0.7, tip[2] - 4.5, tip[2] + 0.1, "copper_dk"))
    # the assembly glow in the yard (node "glow": origin at the yard's centre; hidden at rest): a blue arc along the
    # inner south-east wall, blue streaks on the floor, the pale new part in the middle - as the original paints it
    gc = P["pit_in"][0]
    gr = P["pit_in"][1]
    # (house colour, as the original's glow pixels are: it takes the owner's colour)
    glow.append(K.house(K.arc("glow_arc", gc, gr - 3.4, gr - 0.4, -100.0, 25.0, 1.5, 4.6, "HOUSE", 16), 0.95))
    glow.append(K.house(K.arc("glow_arc2", gc, gr - 6.5, gr - 4.4, -80.0, 5.0, 1.5, 2.0, "HOUSE", 12), 0.6))
    for i, ga in enumerate((-70.0, -35.0, 0.0)):         # (not `a`: the damaged state below still needs the drum's a, b)
        glow.append(K.beam(f"glow_ray{i}", K.polar(gc, 3.0, ga, 1.9), K.polar(gc, gr - 3.6, ga, 1.9), 1.0, 0.5, "lamp"))
    glow.append(K.obox("glow_part", (gc[0] - 1.0, gc[1] + 1.0), 30.0, 3.4, 2.4, 1.5, 5.0, "white", 0.4))
    glow.append(K.obox("glow_part2", (gc[0] + 1.5, gc[1] - 1.5), 30.0, 1.6, 1.4, 4.8, 6.6, "lamp", 0.3))
    # ---- the east wing: a broad block; its roof from the north-east: the brown conveyor (raised), the main roof with
    # two house-colour panels (parallelograms: their long edges run east-west), a lower ledge along the south-west
    wc, wa, wl, ww, wz = P["wing_c"], P["wing_ang"], P["wing_hl"], P["wing_hw"], P["wing_z"]
    main.append(K.obox("wing_foot", along(wc, wa, 8.0, 3.5), wa, wl - 7.5, ww - 2.5, 0.0, 2.6, "foot", 0.3))
    main.append(K.obox("wing_ledge", along(wc, wa, 16.0, -1.0), wa, wl - 16.0, ww + 1.0, 0.0, wz - 4.0, "stone_dk", 1.2))
    main.append(K.obox("wing_block", along(wc, wa, 8.0, 3.5), wa, wl - 8.0, ww - 3.5, 2.0, wz, "block_dk", 1.0))
    main.append(K.obox("wing_rim", along(wc, wa, 8.0, 3.5), wa, wl - 7.7, ww - 3.2, wz - 0.9, wz + 0.5, "rim", 0.25))
    main.append(K.obox("wing_roof", along(wc, wa, 8.0, -0.5), wa, wl - 9.6, 7.0, wz, wz + 0.9, "stone_dk", 0.2))
    main.append(K.obox("conveyor", along(wc, wa, -4.0, 9.5), wa, wl - 3.0, 4.6, 0.0, wz + 5.0, "rim", 0.5))
    main.append(K.obox("conveyor_head", along(wc, wa, -wl + 4.0, 6.0), wa, 7.0, 3.2, 0.0, wz + 4.0, "skirt", 0.5))
    main.append(K.obox("conveyor_belt", along(wc, wa, -4.0, 9.5), wa, wl - 4.2, 3.0, wz + 4.8, wz + 5.5, "skirt", 0.1))
    main.append(K.obox("conveyor_turn", along(wc, wa, wl - 4.5, 4.0), wa + 90.0, 9.0, 4.2, wz - 1.0, wz + 2.6, "rim", 0.5))
    for k in range(6):
        t = -wl + 3.0 + k * 7.0
        main.append(K.obox(f"conveyor_rib{k}", along(wc, wa, t, 9.5), wa, 0.5, 4.9, wz - 1.0, wz + 5.7, "copper_dk"))
    for i, t in enumerate((-2.0, 11.0)):
        q = along(wc, wa, t, -0.6)
        pts = [(q[0] - 8.5, q[1] - 0.5), (q[0] - 1.0, q[1] - 6.0), (q[0] + 8.5, q[1] + 0.5), (q[0] + 1.0, q[1] + 6.0)]
        main.append(K.house(K.prism(f"wing_panel{i}", pts, wz + 0.8, wz + 1.6, "HOUSE", 0.12), 0.13))
        main.append(K.prism(f"wing_frame{i}", pts, wz + 0.7, wz + 1.2, "wall", 0.0, -0.7))
    for i, t in enumerate((8.0, 14.0, 20.0)):
        main.extend(K.vent(f"wing_vent{i}", along(wc, wa, t, -ww - 0.05), wa, 2.2, 3.4, 6.6))
    for i, t in enumerate((-8.0, 0.0, 8.0)):
        main.extend(K.vent(f"wing_vent_e{i}", along(wc, wa + 90.0, t, -wl - 0.05), wa + 90.0, 2.4, 3.4, 6.6))
    for i, t in enumerate((7.0, 14.0, 21.0)):                        # machinery on the ledge
        main.append(K.obox(f"ledge_box{i}", along(wc, wa, t, -ww + 3.0), wa, 2.6, 1.8, wz - 4.2, wz - 1.6, "rust" if i % 2 else "steel", 0.2))
    main.extend(K.pipe("ledge_pipe", [(*along(wc, wa, 5.0, -ww + 3.2), wz - 3.0), (*along(wc, wa, 22.0, -ww + 3.2), wz - 3.0)], 0.8, "copper"))
    # ---- the dish on its mast
    d = P["dish"]
    main.append(K.tube("dish_mast", (d[0], d[1], 0.0), d, 0.9, "copper_dk", 8))
    main.append(K.cyl("dish_foot", (d[0], d[1]), 2.4, 0.0, 2.0, "foot", 10))
    main.append(K.lathe("dish", (d[0], d[1]), [(0.6, d[2] - 0.5), (4.6, d[2] + 1.4), (5.0, d[2] + 2.2), (4.0, d[2] + 2.0), (0.02, d[2] + 0.9)], "roof", 20))
    # ---- the gate with house-colour bars, lamps
    gc = (1.0, 19.5)
    main.append(K.box("gate_block", gc[0] - 4.5, gc[0] + 4.5, gc[1] - 2.5, gc[1] + 3.0, 0.0, 9.5, "block_dk", 0.3))
    for i in range(3):
        main.append(K.house(K.box(f"gate_bar{i}", gc[0] - 3.6 + i * 2.6, gc[0] - 2.0 + i * 2.6, gc[1] - 3.1, gc[1] - 2.4, 0.8, 8.4, "HOUSE"), 0.22))
    main.append(K.box("gate_lintel", gc[0] - 5.0, gc[0] + 5.0, gc[1] - 3.3, gc[1] + 3.0, 9.0, 10.2, "rim", 0.2))
    for i, p in enumerate(((6.5, 16.2, 10.6), (-4.5, 16.2, 10.6), (24.0, -8.0, 8.6), (-27.0, 31.0, 14.0))):
        main.extend(K.lamp(f"lamp{i}", p, 0.65))
    if damaged:
        ax, ay = (b[0] - a[0]), (b[1] - a[1])
        ln = math.hypot(ax, ay)

        def zf(x, y):                        # the drum's upper surface over (x, y)
            t = ((x - a[0]) * ax + (y - a[1]) * ay) / (ln * ln)
            s = ((x - a[0]) * -ay + (y - a[1]) * ax) / ln
            return a[2] + (b[2] - a[2]) * t + math.sqrt(max(4.0, r * r - s * s))
        m = lerp(a, b, 0.55)
        K.hole(main, [drum], (m[0] - 2.5, m[1] - 2.0), 6.0, zf, 3, depth=7.0)
        m = lerp(a, b, 0.2)
        K.hole(main, [drum], (m[0] + 3.0, m[1] + 1.0), 4.0, zf, 5, depth=6.0)
        K.scorch(main, along(wc, wa, 1.0, 0.0), 6.0, wz + 1.8, 7, 6)
        K.wall_gash(main, along(wc, wa, 3.0, -ww - 0.2), wa, 6.0, 2.6, 7.6, 11)
        K.wall_gash(main, along(bc, ba, 2.0, -bw - 0.2), ba, 6.0, 4.0, 13.0, 12)
        K.scorch(main, (pc[0] + 3.0, pc[1] - 2.0), 9.0, 1.6, 9, 7)
        K.scorch(main, K.polar(co, 19.0, 215.0), 5.0, 5.0, 21, 5)
        K.scorch(main, (-23.0, 38.0), 6.0, 19.6, 22, 5)
        K.scorch(main, along(wc, wa, 12.0, 9.5), 5.0, wz + 5.6, 23, 5)
        K.hole(main, [], along(wc, wa, 12.0, -0.5), 4.5, wz + 1.0, 24, depth=3.0, panel=("block", "roof2"))
        K.wall_gash(main, K.polar(co, ro - 2.5, 245.0), 155.0, 5.0, 2.0, 6.5, 13, 1.2)
    return dict(nodes=dict(main=main, crane=crane, hook=hook, load=load, glow=glow),
                origins=dict(crane=cp, hook=P["crane_tip"], load=P["crane_tip"], glow=(P["pit_in"][0][0], P["pit_in"][0][1], 1.5)),
                anchors=[("fx_light_0", (6.5, 16.2, 11.2), 0.6), ("fx_light_1", (24.0, -8.0, 9.2), 0.6)])


def tip_at(deg):
    """The boom tip (px) with the crane turned by deg about the post (negative = clockwise seen from above: the tip
    swings from over the yard out to the west, away from the building)."""
    cp, tip = P["crane_pivot"], P["crane_tip"]
    a = math.radians(deg)
    dx, dy = tip[0] - cp[0], tip[1] - cp[1]
    return (cp[0] + dx * math.cos(a) - dy * math.sin(a), cp[1] + dx * math.sin(a) + dy * math.cos(a), tip[2])


def frame(deg=0.0, drop=1.0, loaded=False, glow=0.0):
    """One animation pose: crane angle, cable stretch (1 = hook up, 2.0 = hook on the ground), with / without the
    load on the hook, size of the assembly glow in the yard (0 = none)."""
    def pose(nodes):
        tip0, t = P["crane_tip"], tip_at(deg)
        d = ((t[0] - tip0[0]) * K.PX, (t[1] - tip0[1]) * K.PX)
        nodes["crane"].rotation_euler = (0.0, 0.0, math.radians(deg))
        hk = nodes["hook"]
        hk.location = (hk.location.x + d[0], hk.location.y + d[1], hk.location.z)
        hk.scale = (1.0, 1.0, drop)
        if loaded:
            ld = nodes["load"]
            ld.scale = (1.0, 1.0, 1.0)
            ld.location = (ld.location.x + d[0], ld.location.y + d[1], ld.location.z - 8.0 * drop * K.PX)
        if glow > 0.0:
            nodes["glow"].scale = (glow, glow, glow)
    return pose


def rest(nodes):
    for n in ("load", "glow"):
        nodes[n].scale = (0.001, 0.001, 0.001)


# The original's 14 overlay frames (studied frame by frame): the boom swings clockwise out to the west (1-2), lowers
# its hook to the ground outside the yard and picks up a load (3), lifts it (4), swings back over the yard (5-6),
# lowers it into the yard (7); then the blue assembly glow in the yard (from 6, strongest 8-10, fading 11-12).
POSES = [frame(-20.0), frame(-42.0), frame(-62.0, 2.0, True), frame(-62.0, 1.0, True), frame(-40.0, 1.0, True),
         frame(-18.0, 1.0, True, 0.5), frame(0.0, 1.7, True, 0.75), frame(glow=1.0), frame(glow=0.94), frame(glow=1.0),
         frame(glow=0.8), frame(glow=0.55)]
ANIM = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1]

if __name__ == "__main__":
    K.run(KEY, build, poses=POSES, rest=rest, spec=dict(anim_frames=ANIM, flags=dict(shadow_azimuth=55.0, fill=(0.3, 90.0, 35.0), sky_strength=0.09)), P=P)
