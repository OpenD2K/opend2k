"""Harkonnen outpost - round 2: built from scratch with tools/models/harkonnen_kit.py from a ruler reading of the
Harkonnen frame (round 1 re-dressed the Atreides fort and stayed flat: outline 0.79). Art 19 is shared with the
SPECIAL HARKONNEN OUTPOST (same frames; the glb is merged under both names).

    blender -b --factory-startup --python tools/models/harkonnen_outpost.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_OUTPOST .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3335 main, 3336 damaged, no base; animation 27 = 30 overlay frames of the turning
radar; build-up 26 kept). Everything stands turned about -34 degrees; it is not a walled square but a cluster:
  * the HALL (west): a tall block whose roof is a square frame - pale rim, dark recessed centre with an X of
    girders, a golden bar lying across it; its sunlit south-west wall is pale panelled plating;
  * the SOUTH WING: a lower block in front (pale south-west wall again) carrying the radar on a round brass-rimmed
    base - an upright golden ring dish (node "dish");
  * the EAST ARM: a low dark wing running south-east, a golden bar on it, a dark ball at its end;
  * the NORTH-EAST COURT: a low dark slab with a house-colour dome;
  * dark bastion domes with thin house rings round their feet: west, north-west, north, north-north-east,
    north-east, east (the biggest), front-middle, south;
  * pale ball posts (north-west with a mast, north-east, south-west), orange rubble west, south, east, north-west.
Animation: the dish turns once in the 30 overlay frames (29 poses).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402

KEY = "HARKONNEN_OUTPOST"
P = dict(
    ang=-34.0,
    hall=(-12.0, 28.0), hall_hl=15.0, hall_hw=14.5, hall_h=20.0,
    wing=(2.5, -1.5), wing_hl=14.0, wing_hw=12.0, wing_h=12.0,
    arm=(26.5, -5.5), arm_hl=10.0, arm_hw=5.5, arm_h=5.5,
    court=(22.0, 44.0), court_hl=16.0, court_hw=14.0, court_h=8.0,
    dish=(11.0, 10.0), dish_r=8.0,
    apron=(-2.0, 24.0), apron_hl=39.0, apron_hw=30.0,
    # bastions: (x, y, drum height, dome radius)
    bast=((-40.0, 24.0, 10.0, 7.0), (-32.0, 51.5, 10.0, 6.0), (-15.0, 61.0, 10.0, 6.0), (15.5, 62.0, 10.0, 6.5), (34.0, 54.0, 9.0, 5.0),
          (37.0, 39.5, 10.0, 7.5), (-13.0, 0.0, 10.0, 6.0), (11.0, -16.0, 9.0, 6.5), (29.5, 4.5, 8.0, 5.5)),
)


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): the golden bars on the hall and the
# east arm are light strips, the dish's feed a beacon, the pale balls on the posts are lamp globes.
GLOW = dict(bar=K.AK.lit("hk_gold_lit", "hk_gold", (0.95, 0.62, 0.20), 0.35), globe=K.AK.lit("hk_plate_lit", "hk_plate", (0.05, 0.044, 0.031), 0.4))


def rot(c, t, s):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def block(main, name, c, hl, hw, h, wall="stone_dk", lit="white", roof="stone_dk", panels=5):
    """A plated block: dark walls, a pale panelled south-west wall, a rim and a darker roof."""
    A = P["ang"]
    main.append(K.obox(name, c, A, hl, hw, 0.0, h, wall, 0.5, 0.97))
    main.append(K.obox(f"{name}_sw", rot(c, 0.0, -hw - 0.15), A, hl - 0.8, 0.5, 1.0, h - 1.0, lit, 0.1))
    for i in range(panels + 1):
        t = -hl + 0.8 + i * (2 * hl - 1.6) / panels
        main.append(K.obox(f"{name}_pil{i}", rot(c, t, -hw - 0.4), A, 0.55, 0.5, 0.6, h - 0.6, "stone", 0.1))
    main.append(K.obox(f"{name}_rim", c, A, hl + 0.4, hw + 0.4, h - 1.2, h + 0.5, "hk_plate", 0.2))
    main.append(K.obox(f"{name}_roof", c, A, hl - 1.6, hw - 1.6, h + 0.2, h + 0.9, roof, 0.2))


def build(damaged=False):
    main, dish = [], []
    A = P["ang"]
    # ---- the apron everything stands on (low, dark, cut corners)
    ac, al, aw = P["apron"], P["apron_hl"], P["apron_hw"]
    cut = 16.0
    pts = [(-al, -aw + cut), (-al + cut, -aw), (al - cut, -aw), (al, -aw + cut), (al, aw - cut), (al - cut, aw), (-al + cut, aw), (-al, aw - cut)]
    main.append(K.prism("apron", [rot(ac, t, s) for (t, s) in pts], 0.0, 3.0, "stone", 0.6, 2.0))
    # ---- the hall and its framed roof
    hc, hl, hw, hh = P["hall"], P["hall_hl"], P["hall_hw"], P["hall_h"]
    block(main, "hall", hc, hl, hw, hh)
    main.append(K.obox("sw_block", (-16.0, -4.0), A, 4.5, 3.0, 0.0, 7.0, "stone_dk", 0.5))
    main.append(K.obox("north_block", (0.0, 58.0), A, 10.0, 5.0, 0.0, 11.0, "stone_dk", 0.6, 0.9))
    main.append(K.obox("hall_well", hc, A, hl - 4.5, hw - 4.5, hh + 0.5, hh + 1.2, "stone_dk", 0.1))
    for i, (st, ss) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
        p0, p1 = rot(hc, st * (hl - 2.0), ss * (hw - 2.0)), rot(hc, 0.0, 0.0)
        main.append(K.beam(f"hall_x{i}", (*p0, hh + 1.4), (*p1, hh + 1.4), 1.2, 0.6, "hk_plate", 0.1))
    p0, p1 = rot(hc, -11.0, 6.0), rot(hc, 9.0, -3.0)
    main.append(K.beam("hall_bar", (*p0, hh + 2.6), (*p1, hh + 2.6), 1.7, 1.2, "hk_gold", 0.2))
    q = rot(hc, 7.0, -6.0)
    main.append(K.beam("hall_bar2", (*p1, hh + 2.4), (*q, hh + 2.4), 1.3, 1.0, "hk_gold", 0.2))
    main.append(K.plate("hall_plate", rot(hc, -9.0, -9.5), A, 2.6, 1.8, hh + 1.2, 0.3))
    main.append(K.plate("hall_plate2", rot(hc, 8.0, 12.0), A, 1.8, 1.8, hh + 1.2, 0.3))
    # ---- the south wing with the radar base
    wc, wl, ww, wh = P["wing"], P["wing_hl"], P["wing_hw"], P["wing_h"]
    block(main, "wing", wc, wl, ww, wh, panels=4)
    dx, dy = P["dish"]
    main.append(K.cyl("dish_base", (dx, dy), 11.0, wh, wh + 2.4, "stone_dk", 32))
    main.append(K.lathe_loop("dish_base_ring", (dx, dy), [(11.4, wh + 1.4), (11.4, wh + 3.0), (9.6, wh + 3.0), (9.6, wh + 1.4)], "copper", 32))
    main.append(K.cyl("dish_base_top", (dx, dy), 9.6, wh + 2.2, wh + 2.8, "hk_black", 28))
    main.append(K.cyl("dish_mount", (dx, dy), 3.6, wh + 2.6, wh + 6.0, "copper_dk", 14, 2.4))
    # the dish (node "dish"): an upright golden ring leaning back a little, facing south-west at rest
    import bmesh
    from mathutils import Vector
    dz = wh + 6.0
    lean = math.radians(72.0)
    mid = Vector((dx, dy, dz + P["dish_r"] * 0.95))
    u, w = Vector((1.0, 0.0, 0.0)), Vector((0.0, math.cos(lean), math.sin(lean)))
    n = u.cross(w)
    r = P["dish_r"]
    segs = 24
    for name, r0, r1, th, lift, mt in (("dish_ring", r - 2.4, r, 1.6, 0.0, "hk_brass"), ("dish_back", 0.0, r - 1.6, 0.5, -0.5, "copper")):
        bm = bmesh.new()
        outer = [mid + n * lift + (u * math.cos(2 * math.pi * k / segs) + w * math.sin(2 * math.pi * k / segs)) * r1 for k in range(segs)]
        if r0 > 0:
            inner = [mid + n * lift + (u * math.cos(2 * math.pi * k / segs) + w * math.sin(2 * math.pi * k / segs)) * r0 for k in range(segs)]
            rings = [[tuple(p - n * th) for p in outer], [tuple(p) for p in outer], [tuple(p) for p in inner], [tuple(p - n * th) for p in inner]]
            vs = [[bm.verts.new(p) for p in ring] for ring in rings]
            for a_, b_ in zip(vs, vs[1:] + vs[:1]):
                for i in range(segs):
                    j = (i + 1) % segs
                    bm.faces.new([a_[i], a_[j], b_[j], b_[i]])
        else:
            K.rings_solid(bm, [[tuple(p - n * th) for p in outer], [tuple(p) for p in outer]])
        dish.append(K.new_obj(name, bm, mt))
    dish.append(K.beam("dish_feed", tuple(mid), tuple(mid + n * 5.0), 0.9, 0.9, "hk_gold"))
    dish.append(K.beam("dish_bar", tuple(mid - u * (r - 1.0)), tuple(mid + u * (r - 1.0)), 0.8, 0.8, "hk_brass"))
    dish.append(K.tube("dish_post", (dx, dy, dz - 0.5), (dx, dy, dz + r * 0.4), 1.3, "copper_dk", 8))
    # ---- the east arm, the north-east court
    rc, rl, rw, rh = P["arm"], P["arm_hl"], P["arm_hw"], P["arm_h"]
    main.append(K.obox("arm", rc, A, rl, rw, 0.0, rh, "stone_dk", 0.5, 0.94))
    main.append(K.obox("arm_roof", rc, A, rl - 1.0, rw - 1.0, rh - 0.2, rh + 0.6, "hk_black", 0.2))
    p0, p1 = rot(rc, -3.0, -1.0), rot(rc, 9.0, -1.0)
    main.append(K.beam("arm_bar", (*p0, rh + 1.6), (*p1, rh + 1.6), 1.7, 1.2, "hk_gold", 0.2))
    e = rot(rc, rl + 4.0, -1.0)
    main.append(K.cyl("arm_ball_foot", e, 3.0, 0.0, 3.0, "stone_dk", 12))
    main.append(K.ball("arm_ball", (e[0], e[1], 6.0), 5.6, "hk_black", 16))
    cc, cl, cw, ch = P["court"], P["court_hl"], P["court_hw"], P["court_h"]
    main.append(K.obox("court", cc, A, cl, cw, 0.0, ch, "stone_dk", 0.6, 0.95))
    main.append(K.obox("court_roof", cc, A, cl - 1.5, cw - 1.5, ch - 0.2, ch + 0.6, "stone", 0.2))
    main.append(K.obox("court_pit", rot(cc, 2.0, -2.0), A, 6.0, 5.0, ch + 0.4, ch + 0.9, "hk_black", 0.1))
    hd = rot(cc, -9.0, -4.0)
    main.append(K.cyl("hdome_drum", hd, 4.4, ch, ch + 2.0, "hk_brass", 16))
    main.append(K.house(K.dome("hdome", hd, 3.3, ch + 1.8, 3.2, "HOUSE", 16), 0.4))
    main.extend(K.pipe("court_pipe", [(*rot(cc, 6.0, 8.0), ch + 1.6), (*rot(cc, 6.0, -6.0), ch + 1.6), (*rot(cc, -2.0, -6.0), ch + 1.2)], 1.0, "copper"))
    # ---- bastions: drum, house ring, dark dome
    for i, (bx, by, bh, br) in enumerate(P["bast"]):
        main.append(K.cyl(f"bast{i}", (bx, by), br + 1.4, 0.0, bh, "stone_dk", 20, br + 0.6))
        main.append(K.house(K.lathe_loop(f"bring{i}", (bx, by), [(br + 0.7, bh + 0.0), (br + 0.7, bh + 0.8), (br - 0.1, bh + 0.8), (br - 0.1, bh + 0.0)], "HOUSE", 24), 0.2))
        main.append(K.dome(f"bdome{i}", (bx, by), br, bh + 0.6, br * 0.85, "hk_black", 20))
    # ---- ball posts, mast, rubble
    for i, (p, h, br) in enumerate((((-33.0, 66.0), 9.0, 3.2), ((-27.0, 67.0), 6.0, 3.0), ((-41.0, 58.0), 3.0, 3.0), ((31.0, 61.0), 5.0, 2.8), ((-21.0, -12.0), 2.0, 4.0))):
        main.append(K.cyl(f"post{i}", p, 1.8, 0.0, h, "stone_dk", 10))
        main.append(K.ball(f"post{i}_ball", (p[0], p[1], h + br * 0.6), br, "hk_plate", 12))
    main.append(K.tube("mast_nw", (-32.0, 68.0, 0.0), (-32.0, 68.0, 18.0), 0.6, "copper_dk", 6))
    main.append(K.tube("mast_ne", (33.0, 62.0, 0.0), (33.0, 62.0, 16.0), 0.6, "copper_dk", 6))
    main.append(K.beam("mast_ne_bar", (31.0, 62.0, 13.0), (37.0, 62.0, 15.0), 0.6, 0.6, "copper"))
    for i, (p, q) in enumerate((((-16.0, -4.0, 6.0), (-20.0, -12.0, 0.3)), ((-12.0, -6.0, 6.0), (-13.0, -14.0, 0.3)))):
        main.append(K.beam(f"sw_strut{i}", p, q, 1.2, 1.2, "copper_dk"))
    for i, (q, r_, n_) in enumerate((((-36.0, 9.0), 9.0, 10), ((8.0, -24.0), 5.5, 6), ((40.0, 30.0), 4.0, 5), ((-26.0, 63.0), 6.0, 7), ((22.0, -14.0), 4.5, 5))):
        main.extend(K.rocks(f"rub{i}", q, r_, 50 + i, n_, 0.7))
    if damaged:
        K.hole(main, [], rot(hc, 3.0, 2.0), 6.5, hh + 1.0, 3, depth=5.0, panel=("stone", "hk_plate"), rib="hk_brass")
        K.scorch(main, cc, 8.0, ch + 0.8, 5, 6)
        K.wall_gash(main, rot(hc, -3.0, -hw - 0.6), A, 7.0, 4.0, 15.0, 7, 0.9)
        K.wall_gash(main, rot(wc, 2.0, -ww - 0.6), A, 5.0, 2.0, 9.0, 8, 0.9)
        K.scorch(main, rot(rc, 0.0, 0.0), 5.0, rh + 0.8, 9, 4)
    K.AK.relight(main, {"hall_bar": GLOW["bar"], "arm_bar": GLOW["bar"], **{f"post{i}_ball": GLOW["globe"] for i in range(5)}})
    K.AK.relight(dish, {"dish_feed": GLOW["bar"]})
    return dict(nodes=dict(main=main, dish=dish), origins=dict(dish=(dx, dy, dz)))


def turn(deg):
    def pose(nodes):
        nodes["dish"].rotation_euler = (0.0, 0.0, -math.radians(deg))
    return pose


N = 30
if __name__ == "__main__":
    K.run(KEY, build, poses=[turn(360.0 * k / N) for k in range(1, N)], spec=dict(anim_frames=[-1] + list(range(N - 1)),
          glb_names=["HARKONNEN_OUTPOST", "SPECIAL_HARKONNEN_OUTPOST"],
          flags=dict(shadow_azimuth=35.0, fill=(0.3, 90.0, 35.0), sky_strength=0.09, sun_strength=4.3)), P=P)
