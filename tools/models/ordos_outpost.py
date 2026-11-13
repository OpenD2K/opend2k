"""Ordos outpost - the original design rebuilt with tools/models/ordos_kit.py (round 2).

    blender -b --factory-startup --python tools/models/ordos_outpost.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_OUTPOST .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 37, shared with SPECIAL ORDOS OUTPOST: main 3609, damaged 3610, no base; animation
28 = 31 overlay frames: the radar head turns; build-up 26 kept) - from the ruler sheet at 12x:
  * TWO brass blocks turned 36 degrees, stepped like an S: the bigger court block at the north-west (its lit
    south-west wall bright orange with a yellow top edge, 13 px high) and a lower block set forward at the
    south-east (again a lit south-west wall with a yellow edge);
  * the court block's top: a pale stone court with dark house-colour roof plates, a pale house dome in the middle,
    a second on its east edge, a third on a post outside the north-west;
  * the forward block's top: a dark bronze roof with the radar - a stepped brown cone with a brass head that turns;
  * heavy dark bronze corner towers with brass bulbs and cyan lamps (west, between the blocks, south, east, north,
    north-west), smaller bulbs on posts with aerials around; a dark rock mass with orange rubble and a
    house-dashed cable on the east side.
Round 1 (one rectangle; IoU 0.77) is replaced by the two stepped blocks.
Animation: 30 poses of the node "dish" (one turn in 31 frames), rendered whole; overlay = the difference from idle.
The head turns in place inside its base ring, so no pose can clip the structure.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_OUTPOST"
P = dict(
    ang=-32.0,
    a=(-8.0, 28.0), a_hl=24.5, a_hw=26.0, a_h=19.0,       # the court block: centre, half length (along ang), half width, height
    b=(17.0, 4.0), b_hl=11.0, b_hw=22.0, b_h=13.0,         # the forward block
    wall_t=4.0, radar=(-5.0, -7.0), radar_r=9.0,           # the radar on the forward block (t, s from its centre)
    rock=(35.0, 28.0),
)
N = 31


def at(c, t, s=0.0):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def block(main, tag, c, hl, hw, h, top_mat, wall="wall", sink=1.2):
    A, wt = P["ang"], P["wall_t"]
    main.append(K.obox(f"{tag}_body", c, A, hl, hw, 0.0, h - sink - 0.4, wall, 0.5))
    main.append(K.obox(f"{tag}_top", c, A, hl - wt + 0.5, hw - wt + 0.5, h - sink - 1.0, h - sink, top_mat, 0.2))
    for i, (t, s, l, w) in enumerate(((0.0, -hw + wt / 2, hl, wt / 2), (0.0, hw - wt / 2, hl, wt / 2), (-hl + wt / 2, 0.0, wt / 2, hw), (hl - wt / 2, 0.0, wt / 2, hw))):
        main.append(K.obox(f"{tag}_wall{i}", at(c, t, s), A, l, w, h - sink - 0.8, h, wall, 0.3))
        main.append(K.obox(f"{tag}_cap{i}", at(c, t, s), A, l - 0.6, w - 0.9, h - 0.2, h + 0.6, "white" if i == 0 else "roof2" if i == 3 else "bronze", 0.2))
    n = max(3, int(hl / 3.2))
    for i in range(n):                                    # ribs on the lit south-west wall and the south-east end
        t = -hl + 3.0 + (2 * hl - 6.0) * i / (n - 1)
        main.append(K.obox(f"{tag}_rib_sw{i}", at(c, t, -hw - 0.1), A, 0.7, 0.45, 0.6, h - 2.4, "roof2" if i % 2 else "bronze"))
    n = max(3, int(hw / 3.2))
    for i in range(n):
        s = -hw + 3.0 + (2 * hw - 6.0) * i / (n - 1)
        main.append(K.obox(f"{tag}_rib_se{i}", at(c, hl + 0.1, s), A + 90.0, 0.7, 0.45, 0.6, h - 2.4, "rib"))


def tower(main, tag, p, r, h, lamp=True):
    main.append(K.cyl(f"{tag}_drum", p, r, 0.0, h, "bronze_dk", 16, r * 0.82))
    main.append(K.lathe_loop(f"{tag}_ring", p, [(r * 0.8, h - 0.6), (r * 0.95, h - 0.6), (r * 0.95, h + 0.6), (r * 0.8, h + 0.8)], "bronze", 16))
    main.extend(K.bulb(f"{tag}_bulb", p, r * 0.78, h, post=False))
    if lamp:
        main.append(K.ball(f"{tag}_lamp", (p[0] - r * 0.25, p[1] + r * 0.2, h + r * 1.62), 1.05, "cyan", 8))
        main.append(K.ball(f"{tag}_lamp_base", (p[0] - r * 0.25, p[1] + r * 0.2, h + r * 1.45), 1.3, "glass", 8))


def build(damaged=False):
    main, dish = [], []
    A = P["ang"]
    ca, al, aw, ah = P["a"], P["a_hl"], P["a_hw"], P["a_h"]
    cb, bl, bw, bh = P["b"], P["b_hl"], P["b_hw"], P["b_h"]
    main.extend(K.skirt("rock_a", [at(ca, -al, -aw), at(ca, al, -aw), at(ca, al, aw), at(ca, -al, aw)], 3.0, 3, 2.0, material="stone_dk", dark="block_dk", lumps=10))
    main.extend(K.skirt("rock_b", [at(cb, -bl, -bw), at(cb, bl, -bw), at(cb, bl, bw), at(cb, -bl, bw)], 3.0, 4, 2.0, material="stone_dk", dark="block_dk", lumps=8))
    block(main, "a", ca, al, aw, ah, "block_dk", wall="roof2", sink=5.0)
    block(main, "b", cb, bl, bw, bh, "bronze_dk")
    fz = ah - 5.0
    main.append(K.house(K.obox("band_sw", at(ca, -4.0, -aw - 0.2), A, al * 0.5, 0.4, ah * 0.3, ah * 0.5, "HOUSE"), 0.3))
    for i, (t0, t1) in enumerate(((-0.6, -0.1), (0.2, 0.7))):
        main.extend(K.strip(f"strip{i}", (*at(cb, bl * t0, -bw - 0.3), bh * 0.45), (*at(cb, bl * t1, -bw - 0.3), bh * 0.45), 0.7, 0.3))
    # the court: pale floor patches, dark house roof plates, the pale dome
    for i, (t, s, l, w) in enumerate(((-12.0, -12.0, 8.0, 6.0), (10.0, 12.0, 7.0, 6.0), (-14.0, 12.0, 5.0, 6.0))):
        main.append(K.obox(f"pale{i}", at(ca, t, s), A, l, w, fz - 0.1, fz + 0.25, "grey", 0.1))
    for i, (t, s, l, w, k) in enumerate(((7.0, 1.0, 5.0, 4.5, 0.26), (-3.0, 9.0, 4.0, 3.5, 0.26), (4.0, -9.0, 3.0, 2.6, 0.3))):
        main.append(K.obox(f"plate{i}_bed", at(ca, t, s), A, l + 0.6, w + 0.6, fz - 0.1, fz + 0.5, "rim", 0.2))
        main.append(K.house(K.obox(f"plate{i}", at(ca, t, s), A, l, w, fz + 0.4, fz + 1.0, "HOUSE", 0.15), k))
    dc = at(ca, -6.0, 1.0)
    main.append(K.cyl("court_drum", dc, 7.4, fz - 0.1, fz + 1.8, "bronze", 24))
    main.append(K.house(K.dome("court_dome", dc, 6.2, fz + 1.6, 5.6, "HOUSE", 24), 0.6))
    main.append(K.house(K.lathe_loop("court_dome_band", dc, [(6.3, fz + 1.4), (7.0, fz + 1.4), (7.0, fz + 2.8), (6.3, fz + 3.0)], "HOUSE", 24), 0.3))
    main.append(K.ball("court_dome_glint", (dc[0] - 2.0, dc[1] - 1.5, fz + 6.0), 2.2, "white", 8))
    for i, (p, r) in enumerate(((at(ca, 16.0, aw - 5.0), 5.8), (at(ca, -al - 6.0, aw - 16.0), 6.0))):
        main.append(K.cyl(f"dome{i}_post", p, r * 0.85, 0.0, ah + 1.0, "bronze_dk", 16))
        main.append(K.house(K.dome(f"dome{i}", p, r, ah + 0.8, r * 0.9, "HOUSE", 20), 0.55))
        main.append(K.lathe_loop(f"dome{i}_ring", p, [(r, ah + 0.4), (r + 0.6, ah + 0.4), (r + 0.6, ah + 1.6), (r, ah + 1.8)], "bronze", 20))
        main.append(K.ball(f"dome{i}_glint", (p[0] - r * 0.3, p[1] - r * 0.25, ah + 0.8 + r * 0.72), r * 0.32, "white", 8))
    # the radar on the forward block
    rc = at(cb, *P["radar"])
    rr = P["radar_r"]
    main.append(K.lathe("radar_base", rc, [(rr + 2.0, bh - 1.4), (rr + 1.2, bh + 1.5), (rr - 1.0, bh + 3.0), (rr - 3.5, bh + 3.6)], "bronze", 28))
    main.append(K.lathe_loop("radar_ring", rc, [(rr + 1.1, bh + 1.3), (rr + 1.7, bh + 1.3), (rr + 1.7, bh + 2.1), (rr + 1.1, bh + 2.3)], "roof", 28))
    main.append(K.lathe("radar_step", rc, [(rr - 3.0, bh + 3.4), (rr - 4.0, bh + 5.4), (rr - 5.5, bh + 5.8)], "skirt", 24))
    z = bh + 5.6
    dish.append(K.cyl("dish_post", rc, 2.0, z, z + 2.4, "bronze_dk", 12))
    bowl = (rc[0], rc[1] - 1.2, z + 4.2)
    dish.append(K.sweep("dish_bowl", (bowl[0], bowl[1] + 2.2, bowl[2] - 2.2), (bowl[0], bowl[1] - 1.3, bowl[2] + 1.3), [(0.0, 1.8), (0.35, 4.6), (0.7, 5.9), (1.0, 6.2)], "gold", 18))
    dish.append(K.sweep("dish_in", (bowl[0], bowl[1] - 1.0, bowl[2] + 1.0), (bowl[0], bowl[1] - 1.5, bowl[2] + 1.5), [(0.0, 5.3), (1.0, 5.6)], "bronze_dk", 18))
    dish.append(K.beam("dish_rib", (bowl[0] - 5.8, bowl[1] - 1.7, bowl[2] + 1.7), (bowl[0] + 5.8, bowl[1] - 1.7, bowl[2] + 1.7), 1.1, 0.8, "roof"))
    dish.append(K.ball("dish_knob", (bowl[0], bowl[1] - 2.6, bowl[2] + 2.6), 1.7, "white", 10))
    dish.append(K.ball("dish_lamp", (bowl[0] + 3.4, bowl[1] - 2.0, bowl[2] + 2.2), 0.9, "cyan", 8))
    # heavy corner towers, smaller bulbs on posts with aerials
    for i, (p, r, h) in enumerate(((at(ca, -al, -aw), 5.6, ah), (at(ca, al - 2.0, -aw), 5.4, ah), (at(cb, bl, -bw), 5.4, bh), (at(ca, al, aw), 5.6, ah),
                                   (at(ca, -al, aw), 5.4, ah), (at(cb, bl, bw), 5.6, bh + 1.0), (at(ca, 2.0, aw + 1.0), 4.6, ah))):
        tower(main, f"tower{i}", p, r, h)
    for i, (x, y, h) in enumerate(((-30.0, 66.0, 7.0), (-43.0, 52.0, 7.0), (-21.0, -17.0, 6.0), (30.0, -12.0, 5.0), (33.0, 56.0, 8.0))):
        main.extend(K.bulb(f"post{i}", (x, y), 3.0, h))
        main.extend(K.spire(f"post{i}_aerial", (x + 2.5, y + 1.0), h, h + 9.0, 0.45))
        main.append(K.beam(f"post{i}_arm", (x + 0.5, y + 1.0, h + 6.5), (x + 6.0, y + 3.0, h + 9.0), 0.5, 0.5, "bronze_dk"))
    for i, (p0, p1) in enumerate((((-21.0, -17.0, 5.0), at(ca, al - 4.0, -aw)), ((30.0, -12.0, 4.0), at(cb, bl, -bw + 4.0)))):
        main.append(K.tube(f"cable{i}", p0, (p1[0], p1[1], 4.0), 0.5, "bronze_dk", 6))
    # the dark rock mass with rubble and the house-dashed cable on the east side
    rk = P["rock"]
    main.extend(K.skirt("rock_e", [K.polar(rk, 10.0, 40.0 * i) for i in range(9)], 8.0, 6, 3.0, material="stone_dk", dark="block_dk", lumps=10))
    main.extend(K.rubble("rub_e", (rk[0] + 3.0, rk[1] + 4.0), 6.0, 3, 6))
    main.extend(K.rubble("rub_w", at(ca, -al - 4.0, -aw - 4.0), 4.0, 5, 4))
    for i in range(7):
        p = (20.0 + 2.6 * i, -6.0 + 4.6 * i)
        main.append(K.house(K.obox(f"dash{i}", p, 60.0, 1.1, 0.5, 0.2, 1.0, "HOUSE"), 0.3))
    if damaged:
        K.hole(main, [], at(cb, 6.0, 8.0), 4.5, bh - 1.1, 3, depth=3.0, panel=("bronze", "roof2"))
        K.hole(main, [], at(ca, 10.0, -6.0), 4.5, fz + 0.1, 5, depth=2.0, panel=("stone", "roof2"))
        K.wall_gash(main, at(ca, -8.0, -aw - 0.3), A, 6.0, 1.5, ah - 2.5, 7, 0.8)
        K.wall_gash(main, at(cb, bl + 0.3, 4.0), A + 90.0, 5.0, 1.5, bh - 2.5, 8, 0.8)
        K.scorch(main, dc, 5.0, lambda x, y: fz + 7.0, 9, 4)
    return dict(nodes=dict(main=main, dish=dish), origins=dict(dish=(rc[0], rc[1], z)))


def turn(deg):
    def pose(nodes):
        nodes["dish"].rotation_euler = (0.0, 0.0, -math.radians(deg))
    return pose


if __name__ == "__main__":
    K.run(KEY, build, poses=[turn(360.0 * k / N) for k in range(1, N)],
          spec=dict(anim_frames=[-1] + list(range(N - 1)), glb_names=["ORDOS_OUTPOST", "SPECIAL_ORDOS_OUTPOST"],
                    flags=K.flags(-0.2, shadow_azimuth=30.0)), P=P)
