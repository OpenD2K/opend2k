"""Ordos construction yard - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_construction_yard.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_CONSTRUCTION_YARD .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 41: main 3651, damaged 3652, no base; animation 2 = 14 overlay frames, HAS_ANIMATION:
the crane; build-up 0 kept original):
  * everything stands on a ragged brown rock foot;
  * north: a big lying gold drum (axis north-west to south-east) with dark hoops, a house-colour flank and a
    teal-black glass eye with a cyan glint on its south-east end, on a house-colour cradle and a brown machine block;
  * south-west: the round assembly yard - a bright pale-gold berm round its south-west half, a brown floor;
  * west: the crane - a bronze tower at the north-west and a gold boom lying towards the yard;
  * east: a brown C-shaped wing wrapping two ribbed house-colour roof plates, a pipe knob at the north-east.
Animation (12 poses -> the 14 overlay frames, as the original's strip): the boom swings clockwise out to the
south-west, away from the building, lowers its hook to the ground outside the yard, lifts a load, swings back over
the yard and on a little towards the east, lowers the load; then the pale blue assembly glow in the yard.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_CONSTRUCTION_YARD"
P = dict(
    west=[(-46.0, 54.0), (-47.0, 28.0), (-39.0, 15.0), (-23.0, 24.0), (-20.0, 50.0), (-31.0, 61.0)],
    drum_a=(-17.0, 64.0, 18.0), drum_b=(2.0, 31.0, 15.0), drum_r=12.0,
    yard=(-13.0, 4.0), yard_r=13.25, berm_r=25.25, berm_h=8.5,
    crane_pivot=(-36.0, 40.0, 21.0), crane_tip=(-20.0, 18.0, 16.0),
    wing=(18.0, 19.0), wing_r0=13.0, wing_r1=23.5, wing_h=9.0,
)


def build(damaged=False):
    main, crane, hook, load, glow = [], [], [], [], []
    gc0, wc0 = P["yard"], P["wing"]
    main.extend(K.skirt("rock_yard", [K.polar(gc0, P["berm_r"] - 1.0, 24.0 * i) for i in range(15)], 4.0, 3, 2.5, material="stone_dk", dark="block_dk", lumps=12))
    main.extend(K.skirt("rock_wing", [K.polar(wc0, P["wing_r1"] + 0.5, -112.0 + 23.0 * i) for i in range(11)], 4.0, 4, 2.5, material="stone_dk", dark="block_dk", lumps=10))
    main.extend(K.skirt("rock_west", P["west"], 9.0, 5, 2.5, material="stone_dk", dark="block_dk", lumps=14))
    # ---- the assembly yard
    gc, gr, br, bh = P["yard"], P["yard_r"], P["berm_r"], P["berm_h"]
    main.append(K.cyl("yard_floor", gc, gr + 1.0, 0.0, 3.0, "skirt", 36))
    main.append(K.cyl("yard_hub", gc, 4.0, 2.8, 3.5, "block_dk", 16))
    for i in range(8):
        main.append(K.seam(f"yard_seam{i}", K.polar(gc, 4.2, 45.0 * i, 3.05), K.polar(gc, gr, 45.0 * i, 3.05), 0.4, 0.2))
    main.append(K.arc("berm", gc, gr, br, 150.0, 345.0, 0.0, bh, "white", 20, 0.6))
    main.append(K.arc("berm_top", gc, gr + 0.8, br - 1.2, 153.0, 342.0, bh - 0.2, bh + 0.8, "white", 20, 0.3))
    main.append(K.arc("berm_in", gc, gr - 0.6, gr + 0.4, 150.0, 345.0, 2.5, bh + 0.2, "rim", 20))
    for i in range(9):
        a = 160.0 + 22.0 * i
        main.append(K.beam(f"berm_rib{i}", K.polar(gc, br + 0.4, a, 0.6), K.polar(gc, br - 2.0, a, bh + 0.5), 1.4, 0.8, "roof2"))
    main.append(K.arc("berm_back", gc, gr, gr + 4.0, -15.0, 150.0, 0.0, 5.0, "block", 16, 0.4))
    main.extend(K.bulb("berm_bulb", K.polar(gc, br - 1.0, 196.0), 2.4, 3.0))
    # ---- the drum on its machine block and house-colour cradle
    a, b, r = P["drum_a"], P["drum_b"], P["drum_r"]
    ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    main.append(K.obox("machine", (mid[0] - 2.0, mid[1] - 2.0), ang, 15.0, 10.5, 0.0, 9.0, "block", 0.6))
    main.append(K.house(K.obox("cradle", (b[0] - 6.0, b[1] - 1.0), ang, 6.5, 11.5, 0.0, 12.5, "HOUSE", 0.5), 0.28))
    main.append(K.obox("cradle_cap", (b[0] - 6.0, b[1] - 1.0), ang, 7.0, 12.0, 12.2, 13.4, "rim", 0.3))
    for i, t in enumerate((-8.0, 0.0, 8.0)):
        q = (b[0] - 6.0 + math.cos(math.radians(ang)) * 6.6 + math.cos(math.radians(ang + 90)) * t, b[1] - 1.0 + math.sin(math.radians(ang)) * 6.6 + math.sin(math.radians(ang + 90)) * t)
        main.append(K.obox(f"cradle_fin{i}", q, ang, 0.6, 1.0, 0.0, 12.0, "bronze_dk"))
    drum = K.tank("drum", a, b, r, "roof", 24, 0.45)
    main.append(drum)
    for i, t in enumerate((0.14, 0.32, 0.5, 0.68, 0.86)):
        main.append(K.hoop(f"drum_hoop{i}", a, b, t - 0.014, t + 0.014, r + 0.35, "rib", 24))
    main.append(K.house(K.hoop("drum_flank", a, b, 0.34, 0.985, r + 0.25, "HOUSE", 24, arc=(125.0, 200.0)), 0.36))
    d = [(b[j] - a[j]) for j in range(3)]
    L = math.sqrt(sum(v * v for v in d))
    d = [v / L for v in d]
    e = [b[j] + d[j] * 0.5 for j in range(3)]
    main.extend(K.orb("drum_eye", (e[0], e[1], e[2] - 1.0), 4.6, collar=False))
    main.extend(K.pipe("drum_pipe", [(a[0] - 4.0, a[1] - 9.0, 9.0), (a[0] - 12.0, a[1] - 14.0, 9.0), (a[0] - 12.0, a[1] - 14.0, 2.0)], 1.3, "bronze"))
    # ---- the crane: tower at the north-west, the boom lying towards the yard
    cp, tip = P["crane_pivot"], P["crane_tip"]
    main.append(K.obox("crane_house", (cp[0] + 1.0, cp[1] + 1.0), 40.0, 6.5, 5.5, 0.0, 15.0, "block", 0.5, 0.85))
    main.append(K.cyl("crane_post", (cp[0], cp[1]), 3.4, 0.0, cp[2] - 1.0, "bronze_dk", 16))
    main.append(K.cyl("crane_cap", (cp[0], cp[1]), 4.4, cp[2] - 1.6, cp[2] + 0.4, "wall", 16))
    main.append(K.obox("crane_house2", (cp[0] - 2.0, cp[1] - 16.0), 80.0, 6.0, 4.0, 0.0, 11.0, "skirt", 0.5, 0.9))
    main.append(K.dome("crane_dome", (cp[0] - 2.0, cp[1] - 16.0), 3.6, 10.6, 3.0, "gold"))
    for i in range(3):
        main.extend(K.lamp(f"crane_lamp{i}", (cp[0] - 5.0 + 0.4 * i, cp[1] - 7.0 - 5.0 * i, 12.0 - i), 0.7))
    crane.append(K.beam("boom", (cp[0], cp[1], cp[2] + 2.2), (tip[0], tip[1], tip[2] + 1.8), 4.6, 3.2, "roof", 0.35))
    crane.append(K.beam("boom_rail", (cp[0], cp[1], cp[2] + 4.0), (tip[0], tip[1], tip[2] + 3.6), 1.5, 0.5, "rib"))
    for s in (-1, 1):
        crane.append(K.beam(f"boom_edge{s}", (cp[0] + s * 1.5, cp[1] + s * 1.4, cp[2] + 2.2), (tip[0] + s * 1.5, tip[1] + s * 1.4, tip[2] + 1.8), 0.7, 3.5, "white"))
    crane.append(K.obox("boom_back", (cp[0], cp[1]), 132.0, 3.8, 3.2, cp[2] + 0.2, cp[2] + 4.8, "bronze", 0.4))
    crane.append(K.cyl("boom_head", (tip[0], tip[1]), 2.4, tip[2] - 0.6, tip[2] + 3.8, "bronze_dk", 12))
    hook.append(K.tube("boom_cable", (tip[0], tip[1], tip[2]), (tip[0], tip[1], tip[2] - 6.5), 0.4, "steel_dk", 6))
    hook.append(K.cyl("boom_hook", (tip[0], tip[1]), 1.5, tip[2] - 8.0, tip[2] - 6.2, "bronze", 8, 0.5))
    load.append(K.box("load_box", tip[0] - 2.6, tip[0] + 2.6, tip[1] - 2.0, tip[1] + 2.0, tip[2] - 4.4, tip[2], "bronze_dk", 0.3))
    load.append(K.box("load_band", tip[0] - 2.8, tip[0] + 2.8, tip[1] - 0.7, tip[1] + 0.7, tip[2] - 4.5, tip[2] + 0.1, "wall"))
    # the assembly glow in the yard (hidden at rest): a pale blue arc along the inner wall, pale rays, the new part
    glow.append(K.arc("glow_arc", gc, gr - 3.6, gr - 0.6, 170.0, 330.0, 3.0, 5.6, "glow", 16))
    glow.append(K.arc("glow_arc2", gc, gr - 7.0, gr - 4.8, 190.0, 310.0, 3.0, 3.6, "glow_dk", 12))
    for i, an in enumerate((200.0, 235.0, 270.0, 305.0)):
        glow.append(K.beam(f"glow_ray{i}", K.polar(gc, 3.0, an, 3.4), K.polar(gc, gr - 3.8, an, 3.4), 1.0, 0.5, "lamp"))
    glow.append(K.obox("glow_part", (gc[0] - 1.0, gc[1] + 1.0), 30.0, 3.4, 2.4, 3.0, 6.4, "white", 0.4))
    glow.append(K.obox("glow_part2", (gc[0] + 1.5, gc[1] - 1.5), 30.0, 1.6, 1.4, 6.2, 8.0, "lamp", 0.3))
    # ---- the east wing: a brown C wrapping two ribbed house-colour roof plates
    wc, w0, w1, wh = P["wing"], P["wing_r0"], P["wing_r1"], P["wing_h"]
    wing = K.arc("wing", wc, w0, w1, -100.0, 115.0, 0.0, wh, "skirt", 24, 0.8)
    main.append(wing)
    main.append(K.arc("wing_top", wc, w0 + 1.6, w1 - 1.6, -98.0, 113.0, wh - 0.2, wh + 1.0, "block", 24, 0.3))
    main.append(K.arc("wing_rim", wc, w1 - 1.0, w1 + 0.2, -100.0, 115.0, wh - 1.4, wh + 0.5, "wall", 24))
    for i in range(10):
        an = -92.0 + 22.0 * i
        main.append(K.beam(f"wing_rib{i}", K.polar(wc, w1 + 0.3, an, 0.5), K.polar(wc, w1 + 0.1, an, wh - 1.2), 1.4, 0.8, "rib"))
    main.append(K.cyl("wing_floor", wc, w0 + 0.5, 0.0, wh - 1.6, "skirt", 28))
    for i, (px, py, hl) in enumerate(((11.0, 20.0, 8.5), (22.0, 10.0, 8.5))):
        main.append(K.obox(f"plate{i}_bed", (px, py), -32.0, hl + 0.8, 5.6, 3.5, 7.0, "rim", 0.3))
        main.append(K.house(K.obox(f"plate{i}", (px, py), -32.0, hl, 4.8, 6.8, 7.9, "HOUSE", 0.2), 0.3))
        for k in range(-2, 3):
            q = (px + math.cos(math.radians(-32.0)) * k * hl * 0.36, py + math.sin(math.radians(-32.0)) * k * hl * 0.36)
            main.append(K.obox(f"plate{i}_s{k}", q, -32.0, 0.35, 4.8, 7.8, 8.15, "recess"))
    kp = (46.0, 46.0)
    main.extend(K.pipe("knob_pipe", [(*K.polar(wc, w1 - 2.0, 50.0), 5.0), (kp[0], kp[1], 5.0)], 1.6, "bronze"))
    main.append(K.lathe_loop("knob", kp, [(2.0, 3.6), (4.0, 3.6), (4.0, 6.4), (2.0, 6.4)], "wall", 14))
    main.extend(K.orb("wing_orb", (*K.polar(wc, w1 - 4.5, -95.0), wh + 1.5), 2.0))
    main.extend(K.rubble("rub", (40.0, 2.0), 3.0, 6, 3))
    if damaged:
        def zf(x, y):
            t = ((x - a[0]) * d[0] + (y - a[1]) * d[1])
            px, py = a[0] + d[0] * t, a[1] + d[1] * t
            s2 = (x - px) ** 2 + (y - py) ** 2
            return a[2] + (b[2] - a[2]) * t / L + math.sqrt(max(1.0, r * r - s2))
        K.hole(main, [drum], (mid[0] - 4.0, mid[1] + 3.0), 6.0, zf, 3, depth=7.0, cut_up=10.0)
        K.hole(main, [drum], (b[0] - 5.0, b[1] + 4.0), 3.6, zf, 5, depth=6.0, cut_up=10.0)
        K.hole(main, [], K.polar(wc, (w0 + w1) / 2, 20.0), 4.5, wh + 1.0, 7, depth=3.0, panel=("block", "skirt"))
        K.scorch(main, (11.0, 20.0), 6.0, 8.2, 9, 5)
        K.scorch(main, gc, 9.0, 3.1, 11, 6)
        K.wall_gash(main, K.polar(gc, br + 0.4, 250.0), 160.0, 6.0, 1.5, 7.5, 13, 0.9)
        K.wall_gash(main, K.polar(wc, w1 + 0.4, -40.0), 50.0, 6.0, 1.5, 7.0, 14, 0.9)
    return dict(nodes=dict(main=main, crane=crane, hook=hook, load=load, glow=glow),
                origins=dict(crane=cp, hook=P["crane_tip"], load=P["crane_tip"], glow=(gc[0], gc[1], 3.0)),
                anchors=[("fx_light_0", (e[0], e[1], e[2]), 0.6)])


def tip_at(deg):
    cp, tip = P["crane_pivot"], P["crane_tip"]
    a = math.radians(deg)
    dx, dy = tip[0] - cp[0], tip[1] - cp[1]
    return (cp[0] + dx * math.cos(a) - dy * math.sin(a), cp[1] + dx * math.sin(a) + dy * math.cos(a), tip[2])


def frame(deg=0.0, drop=1.0, loaded=False, glow=0.0):
    """One animation pose: crane angle (negative = clockwise seen from above), cable stretch (1 = hook up, 2.0 = hook
    on the ground), with / without the load on the hook, size of the assembly glow in the yard (0 = none)."""
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


# The original's 14 overlay frames: the boom swings clockwise out to the south-west (1-2), lowers its hook to the
# ground outside the yard and picks up a load (3), lifts it (4), swings back (5-6) and a little past its rest
# position towards the east (7), lowering the load into the yard; then the assembly glow (strongest 8-10).
POSES = [frame(-16.0), frame(-34.0), frame(-50.0, 2.0, True), frame(-50.0, 1.0, True), frame(-28.0, 1.0, True),
         frame(-8.0, 1.0, True, 0.5), frame(10.0, 1.6, True, 0.75), frame(14.0, glow=1.0), frame(14.0, glow=0.94), frame(14.0, glow=1.0),
         frame(8.0, glow=0.8), frame(3.0, glow=0.55)]
ANIM = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1]

if __name__ == "__main__":
    K.run(KEY, build, poses=POSES, rest=rest, spec=dict(anim_frames=ANIM, flags=K.flags(-0.4, shadow_azimuth=55.0)), P=P)
