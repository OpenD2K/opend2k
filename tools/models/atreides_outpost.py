"""Atreides outpost (radar) - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_outpost.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_OUTPOST .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3061 main, 3062 damaged, no base; animation 26 = 31 overlay frames, HAS_ANIMATION:
the brown radar dish turning in the south-east court; build-up 26 kept):
  * a fort turned about 34 degrees on a broad battered base (82 x 76 px at the foot): thick cream walls with
    round corner bastions and house-colour stripes on their tops (pale on the lit south-west wall, dark on the south-east and north-east walls);
  * the west half roofed: a dark brown hipped roof with copper ridge ribs (an X); the east half open: a black
    pit at the north with a white dome on its rim, and in front of it the radar - a tilted brown dish on a
    round base;
  * white ball posts on the corners (three in a cluster at the north-west, one with a mast at the north-east),
    aerials at the south and south-east, sandstone rubble at the east and south-west feet.
Animation: the node "dish" turns once in 31 poses (each rendered whole; the overlay = what differs from idle).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_OUTPOST"
P = dict(
    ang=-34.0, c=(1.0, 29.0), hl=35.0, hw=33.5, wall=7.0, wall_h=13.0, base_h=1.5, skirt=3.0, cut=18.0,
    dish_xy=(10.0, 13.0), dish_r=8.0,        # the radar court (south-east): the dish
    pit_xy=(19.0, 42.0),                    # the black pit court (north-east)
)


def rot(c, t, s):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def build(damaged=False):
    main, dish = [], []
    A, c, hl, hw, wt, wh = P["ang"], P["c"], P["hl"], P["hw"], P["wall"], P["wall_h"]
    bh = P["base_h"]
    # the battered base everything stands on (the court lies on its top, the walls rise wall_h above it)
    L, W_ = hl + P["skirt"], hw + P["skirt"]

    def octagon(grow):                                    # the base's plan: the rectangle with its corners cut (S least)
        cut = dict(w=P["cut"], s=P["cut"] * 0.4, e=P["cut"], n=P["cut"] * 1.2)
        l, w = L + grow, W_ + grow
        pts = [(-l, -w + cut["w"]), (-l + cut["w"], -w), (l - cut["s"], -w), (l, -w + cut["s"]),
               (l, w - cut["e"]), (l - cut["e"], w), (-l + cut["n"], w), (-l, w - cut["n"])]
        return [rot(c, t, s_) for (t, s_) in pts]
    main.append(K.prism("base", octagon(0.0), 0.0, bh, "wall", 0.8, P["skirt"] - 0.5))
    main.append(K.prism("base_foot", octagon(1.0), 0.0, 1.8, "foot", 0.3))
    n0 = len(main)
    main.append(K.obox("floor", c, A, hl - 1.0, hw - 1.0, 0.0, 2.0, "floor", 0.2))
    walls = (("sw", rot(c, 0.0, -hw + wt), A, hl, "roof2", 0.8), ("se", rot(c, hl - wt, 0.0), A + 90.0, hw, "stone_dk", 0.3),
             ("ne", rot(c, 0.0, hw - wt), A, hl, "stone_dk", 0.3), ("nw", rot(c, -hl + wt, 0.0), A + 90.0, hw, "stone", 0.55))
    for name, wc, wa, wl, mt, shade in walls:
        main.append(K.obox(f"wall_{name}", wc, wa, wl - P["cut"] * 0.45, wt, 0.0, wh, mt, 0.7, 0.8))
        # the house stripe on the wall top
        k = 0.72 if name in ("sw", "ne") else 0.6
        main.append(K.house(K.obox(f"stripe_{name}", wc, wa, wl * k * 0.9, 1.4, wh - 0.2, wh + 0.6, "HOUSE", 0.1), shade))
        for i in range(5):
            t = -wl * 0.8 + i * wl * 0.4
            p = (wc[0] + math.cos(math.radians(wa)) * t, wc[1] + math.sin(math.radians(wa)) * t)
            main.append(K.obox(f"wall_{name}_rib{i}", p, wa + 90.0, wt * 0.95, 0.5, 1.0, wh - 0.6, "roof2" if name == "sw" else "stone_dk"))
    for i, (st, ss) in enumerate(((-1, -1), (1, -1), (1, 1))):                 # round bastions on the west, south, east corners
        p = rot(c, st * (hl - wt), ss * (hw - wt))
        main.append(K.cyl(f"bastion{i}", p, wt + 1.5, -bh, wh + 0.6, "roof2" if ss < 0 and st < 0 else "stone", 20, wt + 0.4))
        main.append(K.cyl(f"bastion{i}_top", p, wt - 0.5, wh + 0.4, wh + 1.2, "wall", 20))
        if i < 2:                                        # white ball posts on the west and south corners
            main.append(K.ball(f"bastion{i}_ball", (p[0], p[1], wh + 3.6), 3.4, "white", 14))
        else:
            main.extend(K.lamp(f"bastion{i}_lamp", (p[0], p[1], wh + 1.3), 0.9))
    # ---- the roofed west half: a dark hipped roof with copper ribs
    rc = rot(c, -hl * 0.42, 0.0)
    rl, rw = hl * 0.52 - wt * 0.3, hw - wt * 1.9
    main.append(K.obox("hall", rc, A, rl, rw, 0.0, wh - 1.0, "block_dk", 0.3))
    main.append(K.obox("hall_roof", rc, A, rl, rw, wh - 1.2, wh + 5.0, "copper_dk", 0.2, 0.18))
    for i, (st, ss) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
        p0 = rot(rc, st * rl, ss * rw)
        p1 = rot(rc, st * rl * 0.18, ss * rw * 0.18)
        main.append(K.beam(f"hall_ridge{i}", (p0[0], p0[1], wh - 1.0), (p1[0], p1[1], wh + 5.1), 1.1, 0.8, "copper"))
    main.append(K.obox("hall_cap", rc, A, rl * 0.2, rw * 0.2, wh + 4.6, wh + 5.8, "copper_dk", 0.2))
    # the partition between the halves
    main.append(K.obox("partition", rot(c, hl * 0.1, 0.0), A + 90.0, hw - wt * 1.5, 2.0, 0.0, wh - 1.0, "stone_dk", 0.3))
    # ---- the open east half: the black pit, its white dome, house panels
    pc = P["pit_xy"]
    main.append(K.obox("pit", pc, A, 11.0, 10.5, 1.9, 2.6, "burnt"))
    # the wall between the pit court (north-east) and the radar court (south-east)
    mid = ((pc[0] + P["dish_xy"][0]) / 2, (pc[1] + P["dish_xy"][1]) / 2)
    main.append(K.obox("cross_wall", mid, A, 12.0, 2.2, 0.0, wh - 1.0, "stone_dk", 0.3))
    main.append(K.dome("pit_dome", rot(pc, -8.0, 4.0), 4.4, 4.0, 4.6, "white", 18))
    main.append(K.cyl("pit_dome_drum", rot(pc, -8.0, 4.0), 5.0, 0.0, 4.4, "wall", 18))
    main.append(K.house(K.obox("pit_panel", rot(pc, -6.0, -6.0), A, 5.0, 1.6, 2.0, 5.0, "HOUSE", 0.2), 0.22))
    main.extend(K.pipe("pit_pipe", [(*rot(pc, 4.0, 8.0), 4.0), (*rot(pc, 4.0, -2.0), 4.0), (*rot(pc, -2.0, -2.0), 3.0)], 1.1, "copper"))
    # ---- the radar: round base, post, tilted dish (node "dish")
    dx, dy = P["dish_xy"]
    dz = 7.0
    main.append(K.obox("dish_court", (dx, dy), A, 11.5, 10.5, 1.9, 2.5, "tank"))
    dz += bh
    main.append(K.cyl("dish_base", (dx, dy), 9.5, 1.8, 3.6, "skirt", 28))
    main.append(K.lathe_loop("dish_base_ring", (dx, dy), [(9.8, 3.2), (9.8, 4.2), (8.2, 4.2), (8.2, 3.2)], "copper", 28))
    main.append(K.cyl("dish_base_top", (dx, dy), 8.2, 3.4, 3.9, "copper_dk", 28))
    dish.append(K.cyl("dish_post", (dx, dy), 1.6, 3.8 + bh, dz + 4.0, "copper_dk", 10))
    r = P["dish_r"]
    # a square dish leaning back 50 degrees, facing south in the idle pose
    lean = math.radians(50.0)
    pts = []
    for (u, w) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        pts.append((dx + u * r, dy + (w * r) * math.cos(lean) + 0.5, dz + 5.5 + (w * r) * math.sin(lean)))
    import bmesh
    bm = bmesh.new()
    nrm = (0.0, -math.sin(lean), math.cos(lean))
    K.rings_solid(bm, [[(x - nrm[0] * 0.9, y - nrm[1] * 0.9, z - nrm[2] * 0.9) for (x, y, z) in pts], pts])
    dish.append(K.new_obj("dish_plate", bm, "skirt", 0.2))
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        dish.append(K.beam(f"dish_rim{i}", (a[0] + nrm[0] * 0.3, a[1] + nrm[1] * 0.3, a[2] + nrm[2] * 0.3), (b[0] + nrm[0] * 0.3, b[1] + nrm[1] * 0.3, b[2] + nrm[2] * 0.3), 1.0, 1.0, "copper"))
    mid = (dx, dy + 0.5, dz + 5.5)
    dish.append(K.beam("dish_feed", (mid[0] + nrm[0] * 0.5, mid[1] + nrm[1] * 0.5, mid[2] + nrm[2] * 0.5),
                       (mid[0] + nrm[0] * 5.5, mid[1] + nrm[1] * 5.5, mid[2] + nrm[2] * 5.5), 0.8, 0.8, "cream_pipe"))
    for i in range(2):
        s = -0.5 + i
        dish.append(K.beam(f"dish_bar{i}", (dx + s * r, pts[0][1], pts[0][2] + 0.3), (dx + s * r, pts[2][1], pts[2][2] + 0.3), 0.6, 0.5, "copper_dk"))
    for o in main[n0:]:                                  # everything so far stands on the base
        o.location.z += bh
    # wall-top details: merlon blocks along the outer edges, a pipe run and vents on the inner faces
    for name, wc, wa, wl, mt, shade in walls:
        ca, sa = math.cos(math.radians(wa)), math.sin(math.radians(wa))
        for i in range(6):
            t = -wl * 0.62 + i * wl * 0.25
            side = -1.0 if name in ("sw", "nw") else 1.0
            q = (wc[0] + ca * t + sa * side * (wt * 0.55), wc[1] + sa * t - ca * side * (wt * 0.55))
            main.append(K.obox(f"merlon_{name}{i}", q, wa, 1.6, 1.0, wh - 0.4, wh + 1.5, mt, 0.2))
    main.extend(K.pipe("court_pipe", [(*rot(c, hl * 0.2, -hw + wt * 1.9), 4.0), (*rot(c, hl * 0.7, -hw + wt * 1.9), 4.0), (*rot(c, hl * 0.7, -hw + wt * 2.6), 2.0)], 1.0, "copper"))
    main.extend(K.vent("court_vent", rot(c, hl - wt * 1.72, 6.0), A + 90.0, 3.0, 5.0, 9.0, 0.5, 3, "stone_dk"))
    # ---- ball posts (a cluster of three at the north-west, one with a mast at the north-east), aerials, rubble
    for i, (p, h, br) in enumerate((((-41.0, 57.0), 4.0, 4.2), ((-33.0, 64.0), 5.0, 4.2), ((-31.0, 52.0), 5.0, 3.8), ((33.0, 57.0), 6.0, 4.0))):
        main.append(K.cyl(f"post{i}", p, 2.4, 0.0, h, "stone", 12))
        main.append(K.ball(f"post{i}_ball", (p[0], p[1], h + br * 0.6), br, "white", 14))
    main.append(K.house(K.box("post_panel", -38.0, -32.0, 56.0, 59.0, 0.0, 5.0, "HOUSE", 0.2), 0.5))
    main.append(K.tube("mast_ne", (35.0, 59.0, 0.0), (35.0, 59.0, 24.0), 0.6, "copper_dk", 6))
    main.append(K.beam("mast_ne_bar", (32.0, 59.0, 19.0), (39.0, 59.0, 21.0), 0.6, 0.6, "copper"))
    main.append(K.beam("mast_ne_bar2", (35.0, 59.0, 15.0), (41.0, 59.0, 13.0), 0.6, 0.6, "copper"))
    for i, (p, h) in enumerate((((-30.0, -8.0), 13.0), ((33.0, -16.0), 7.0))):
        main.append(K.tube(f"aerial{i}", (p[0], p[1], 0.0), (p[0], p[1], h), 0.6, "copper_dk", 6))
        main.append(K.ball(f"aerial{i}_ball", (p[0], p[1], 2.4), 2.8, "white", 12))
        main.append(K.beam(f"aerial{i}_stay", (p[0], p[1], h * 0.7), (p[0] + 6.0, p[1] + 5.0, 0.3), 0.5, 0.5, "copper"))
    main.extend(K.pipe("se_pipe", [(24.0, -9.0, 3.0), (32.0, -14.0, 3.0), (43.0, -20.0, 2.0)], 1.3, "orange"))
    main.extend(K.pipe("se_pipe2", [(28.0, -17.0, 2.0), (38.0, -13.0, 2.0)], 1.0, "copper"))
    main.extend(K.rocks("rock_e", (44.0, 22.0), 5.0, 3, 5))
    main.extend(K.rocks("rock_sw", (-11.0, -8.0), 4.2, 5, 4))
    main.extend(K.rocks("rock_ne", (38.0, 56.0), 3.6, 7, 3))
    if damaged:
        K.hole(main, [], rot(rc, 2.0, -3.0), 6.5, bh + wh + 2.0, 3, depth=5.0, panel=("rust", "skirt"), rib="copper")
        K.scorch(main, pc, 7.0, bh + 2.8, 5, 5)
        K.wall_gash(main, rot(c, 6.0, -hw - 3.0), A, 7.0, 2.0, 8.5, 7, 1.5)
        K.wall_gash(main, rot(c, hl + 3.0, 6.0), A + 90.0, 6.0, 2.0, 8.5, 8, 1.5)
        K.scorch(main, rot(c, hl - wt, -8.0), 5.0, bh + wh + 0.7, 9, 5)
    return dict(nodes=dict(main=main, dish=dish), origins=dict(dish=(dx, dy, dz)))


def turn(deg):
    def pose(nodes):
        nodes["dish"].rotation_euler = (0.0, 0.0, -math.radians(deg))
    return pose


N = 31
if __name__ == "__main__":
    K.run(KEY, build, poses=[turn(360.0 * k / N) for k in range(1, N)], spec=dict(anim_frames=[-1] + list(range(N - 1)),
          flags=dict(shadow_azimuth=35.0, fill=(0.3, 90.0, 35.0), sky_strength=0.1, sun_strength=5.1)), P=P)
