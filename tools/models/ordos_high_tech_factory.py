"""Ordos high tech factory - the original design rebuilt with tools/models/ordos_kit.py (round 2).

    blender -b --factory-startup --python tools/models/ordos_high_tech_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_HIGH_TECH_FACTORY .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 43: main 3657, damaged 3658, no base; animation 34 = 10 ADDITIVE overlay frames kept
original; build-up 32 kept) - from the ruler sheet at 12x, corner by corner:
  * a boxy hull RAISED ON STILTS, turned 38 degrees (long side north-west to south-east): its roof corners sit at
    W (-30, v62), S (2, v38), E (20, v62), N (-11, v86), so the hull is about 40 x 30 px, from 14 to 40 px above the
    ground; the south-west wall is brightly lit pale fluted brass, the south-east end in shade, dark brown;
  * a cream eave with a pale house-colour trim along the south-west edge of the roof; the roof dark brown with
    gold cross beams;
  * on the roof: a pale winged dish (a leaning cream triangle) in the middle, a pale house-colour cone at the east
    with the tall mast; a teal-black glass orb with a cyan glint on the east corner;
  * under the hull: splayed gold legs with braces (bright at the south-west, dark lattice at the east), a
    house-colour framed lift door under the south-east end;
  * at the south-west foot three bulbs (brass, pale house dome, brass); rubble at the west.
Round 1 (a hangar on the ground with a separate pod on legs, IoU 0.71) is replaced by this hull on stilts.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_HIGH_TECH_FACTORY"
P = dict(
    ang=-38.0, c=(-4.0, 26.0), hl=22.75, hw=18.5, z0=0.0, z1=34.0, vault=3.5, annex_l=9.0, annex_h=21.0,
    cone=(16.0, 9.0), cone_r=7.5, mast=37.0, dish=(4.0, -6.0),
    splay=8.0, lift=(14.0, -15.0), lat=13.0,
    bulbs=(-29.0, -5.0),
)


def at(t, s=0.0):
    a = math.radians(P["ang"])
    return (P["c"][0] + math.cos(a) * t - math.sin(a) * s, P["c"][1] + math.sin(a) * t + math.cos(a) * s)


def build(damaged=False):
    main = []
    A, hl, hw, z0, z1 = P["ang"], P["hl"], P["hw"], P["z0"], P["z1"]
    # ---- the hull
    hull = K.obox("hull", P["c"], A, hl, hw, z0, z1, "wall", 0.9)
    main.append(hull)
    main.append(K.obox("hull_floor", P["c"], A, hl - 1.0, hw - 1.0, z0 - 1.6, z0 + 0.2, "block_dk", 0.3))
    main.append(K.obox("hull_belt", P["c"], A, hl + 0.4, hw + 0.4, z0 + 1.0, z0 + 3.4, "rim", 0.2))
    for i in range(13):                                   # the lit south-west wall: pale flutes
        t = -hl + 1.6 + (2 * hl - 3.2) * i / 12
        main.append(K.obox(f"flute{i}", at(t, -hw - 0.15), A, 0.85, 0.55, z0 + 3.6, z1 - 3.0, "white" if i % 2 else "roof"))
    for i in range(8):                                    # the shaded south-east end: dark ribs, two house slits
        s = -hw + 1.8 + (2 * hw - 3.6) * i / 7
        main.append(K.obox(f"rib_se{i}", at(hl + 0.15, s), A + 90.0, 0.9, 0.55, z0 + 3.6, z1 - 3.0, "rib" if i % 2 else "bronze_dk"))
    main.append(K.obox("end_se", at(hl + 0.05, 0.0), A + 90.0, hw - 0.6, 0.3, z0 + 3.4, z1 - 2.6, "block_dk"))
    for i, s in enumerate((-6.0, 5.0)):
        main.append(K.house(K.obox(f"slit{i}", at(hl + 0.5, s), A + 90.0, 1.2, 0.4, z0 + 8.0, z1 - 7.0, "HOUSE"), 0.32))
    # the eave: cream band with a pale house trim along the south-west edge, bronze on the others
    main.append(K.obox("eave_sw", at(0.0, -hw - 0.6), A, hl + 1.0, 1.1, z1 - 3.0, z1 + 0.3, "white", 0.25))
    main.append(K.house(K.obox("eave_trim", at(0.0, -hw - 1.2), A, hl + 0.6, 0.6, z1 - 4.6, z1 - 3.0, "HOUSE"), 0.7))
    main.append(K.obox("eave_se", at(hl + 0.6, 0.0), A + 90.0, hw + 1.0, 1.1, z1 - 2.6, z1 + 0.3, "bronze", 0.25))
    main.append(K.obox("eave_nw", at(-hl - 0.6, 0.0), A + 90.0, hw + 1.0, 1.1, z1 - 2.6, z1 + 0.3, "roof2", 0.25))
    main.append(K.obox("eave_ne", at(0.0, hw + 0.6), A, hl + 1.0, 1.1, z1 - 2.6, z1 + 0.3, "bronze", 0.25))
    # the roof: a very low vault of dark brown panels, gold cross beams
    vz = P["vault"]
    roof = K.vault("roof", at(-hl + 0.4, 0.0), at(hl - 0.4, 0.0), hw - 0.4, z1 - 0.2, "block_dk", 12, vz)
    main.append(roof)

    def zf(x, y):
        a = math.radians(A)
        s = -(x - P["c"][0]) * math.sin(a) + (y - P["c"][1]) * math.cos(a)
        return z1 - 0.2 + vz * math.sqrt(max(0.02, 1 - (s / (hw - 0.4)) ** 2))
    for i in range(6):
        t = -hl + 3.0 + (2 * hl - 6.0) * i / 5
        main.append(K.hoop(f"roof_beam{i}", (*at(-hl + 0.4, 0.0), z1 - 0.2), (*at(hl - 0.4, 0.0), z1 - 0.2), (t + hl - 0.4) / (2 * hl - 0.8) - 0.015, (t + hl - 0.4) / (2 * hl - 0.8) + 0.015,
                             hw - 0.1, "roof" if i % 2 == 0 else "bronze", 12, arc=(0.0, 180.0), squash=(vz + 0.3) / (hw - 0.1)))
    for s in (-0.55, 0.0, 0.55):
        p0, p1 = at(-hl + 1.0, s * hw), at(hl - 1.0, s * hw)
        main.append(K.beam(f"roof_spar{s}", (*p0, zf(*p0) + 0.2), (*p1, zf(*p1) + 0.2), 0.9, 0.4, "bronze" if s else "roof2"))
    # the winged dish: a cream triangle leaning back on a short post, a pale house plate under it
    dp = at(*P["dish"])
    dz = zf(*dp)
    w0, w1, tip = at(P["dish"][0] - 9.0, P["dish"][1] + 5.0), at(P["dish"][0] + 3.0, P["dish"][1] - 8.0), at(P["dish"][0] + 8.0, P["dish"][1] + 7.0)
    import bmesh
    bm = bmesh.new()
    K.rings_solid(bm, [[(*w0, dz + 5.5), (*w1, dz + 2.0), (*tip, dz + 9.0)], [(w0[0], w0[1], dz + 6.3), (w1[0], w1[1], dz + 2.8), (tip[0], tip[1], dz + 9.8)]])
    main.append(K.new_obj("dish", bm, "white"))
    bm = bmesh.new()
    m0, m1 = [(w0[i] + w1[i]) / 2 for i in range(2)], [(w0[i] + tip[i]) / 2 for i in range(2)]
    K.rings_solid(bm, [[(*w0, dz + 6.35), (*m0, dz + 4.6), (*m1, dz + 8.1)], [(*w0, dz + 6.75), (*m0, dz + 5.0), (*m1, dz + 8.5)]])
    main.append(K.house(K.new_obj("dish_plate", bm, "HOUSE"), 0.85))
    main.append(K.tube("dish_post", (*dp, dz), (dp[0], dp[1], dz + 5.0), 1.0, "bronze_dk", 8))
    # the cone at the east with the mast, the orb on the east corner
    cc = at(*P["cone"])
    cr = P["cone_r"]
    cz = zf(*cc) - 0.5
    main.append(K.cyl("cone_drum", cc, cr + 0.8, z1 - 1.0, cz + 2.4, "bronze", 20))
    main.append(K.house(K.lathe("cone", cc, [(cr, cz + 2.0), (cr * 0.8, cz + 5.0), (cr * 0.4, cz + 8.5), (1.6, cz + 10.5)], "HOUSE", 20), 0.62))
    main.append(K.house(K.lathe_loop("cone_band", cc, [(cr + 0.1, cz + 1.6), (cr + 0.7, cz + 1.6), (cr + 0.5, cz + 3.4), (cr - 0.3, cz + 3.4)], "HOUSE", 20), 0.3))
    main.append(K.tube("mast", (cc[0], cc[1], cz + 9.5), (cc[0], cc[1], cz + P["mast"]), 2.0, "bronze_dk", 8, 1.5))
    main.append(K.cyl("mast_cap", cc, 2.1, cz + P["mast"] - 1.2, cz + P["mast"] + 0.8, "soot", 8))
    op = at(hl + 2.0, hw - 1.0)
    main.append(K.cyl("orb_arm", op, 2.6, z1 - 12.0, z1 - 7.0, "bronze_dk", 10))
    main.extend(K.orb("orb", (op[0], op[1], z1 - 5.0), 4.6))
    # the shaded annex on the south-east end: a lower dark bronze block with ribs, standing on the ground
    al, ah = P["annex_l"], P["annex_h"]
    ac = at(hl + al, 1.0)
    main.append(K.obox("annex", ac, A, al, hw - 2.0, 0.0, ah, "skirt", 0.8, 0.9))
    main.append(K.obox("annex_top", ac, A, al * 0.9 - 0.6, (hw - 2.0) * 0.9 - 0.6, ah - 0.2, ah + 0.8, "block_dk", 0.2))
    for i in range(6):
        s = -hw + 4.0 + (2 * hw - 8.0) * i / 5
        main.append(K.beam(f"annex_rib{i}", (*at(hl + 2 * al + 0.2, s + 1.0), 0.5), (*at(hl + 2 * al * 0.95, (s + 1.0) * 0.92), ah - 1.0), 1.2, 0.7, "rib" if i % 2 else "bronze"))
    for i in range(4):
        t = hl + 1.5 + (2 * al - 3.0) * i / 3
        main.append(K.beam(f"annex_rib_sw{i}", (*at(t, -hw + 2.8), 0.5), (*at(t, -hw + 3.4), ah - 1.0), 1.2, 0.7, "bronze" if i % 2 else "roof2"))
    # ---- the stilts: splayed legs with braces; gold on the lit south-west side, dark at the east
    sp = P["splay"]
    corners = [(-hl + 2.0, -hw + 1.5), (hl - 2.0, -hw + 1.5), (hl + 2 * al - 2.0, hw - 3.0), (-hl + 2.0, hw - 1.5), (0.0, -hw + 1.5), (hl + 2 * al - 2.0, -hw + 3.0)]
    tops = [at(t, s) for (t, s) in corners]
    feet = [at(t + sp * (1 if t > 1 else -1 if t < -1 else 0), s + sp * (1 if s > 1 else -1 if s < -1 else 0)) for (t, s) in corners]
    lit = (True, True, False, False, True, False)
    for i in range(6):
        main.append(K.tube(f"leg{i}", (*feet[i], 0.0), (*tops[i], z0 + 6.0), 1.25, "roof" if lit[i] else "bronze_dk", 8))
        main.append(K.cyl(f"leg{i}_foot", feet[i], 2.3, 0.0, 1.3, "bronze_dk", 8))
    for n, (i, j) in enumerate(((0, 4), (4, 1), (1, 5), (5, 2), (2, 3), (3, 0))):
        for k, (u0, u1) in enumerate(((0.1, 0.9), (0.9, 0.1))):
            a0 = [feet[i][m] + (tops[i][m] - feet[i][m]) * u0 for m in range(2)] + [(z0 + 6.0) * u0]
            b0 = [feet[j][m] + (tops[j][m] - feet[j][m]) * u1 for m in range(2)] + [(z0 + 6.0) * u1]
            main.append(K.tube(f"brace{n}_{k}", a0, b0, 0.55, ("roof2" if k else "roof") if lit[i] and lit[j] else ("grey" if k else "bronze"), 6))
    main.append(K.tube("core", (*at(-4.0, 2.0), 0.0), (*at(-4.0, 2.0), z0), 2.6, "bronze_dk", 10))
    # the lattice aprons in front of the south-west wall and round the south-east end: rows of X braces between the
    # ground and the hull's foot (the original's lace of struts) - gold on the lit side, dark / grey at the east
    def lattice(tag, p_a, p_b, q_a, q_b, n, mats, top=9.0):
        """X braces between the ground line p_a -> p_b and the upper line q_a -> q_b (at height top)."""
        for i in range(n):
            g0 = [p_a[m] + (p_b[m] - p_a[m]) * i / n for m in range(2)]
            g1 = [p_a[m] + (p_b[m] - p_a[m]) * (i + 1) / n for m in range(2)]
            u0 = [q_a[m] + (q_b[m] - q_a[m]) * i / n for m in range(2)]
            u1 = [q_a[m] + (q_b[m] - q_a[m]) * (i + 1) / n for m in range(2)]
            main.append(K.tube(f"{tag}_a{i}", (*g0, 0.2), (*u1, top), 0.6, mats[i % len(mats)], 6))
            main.append(K.tube(f"{tag}_b{i}", (*g1, 0.2), (*u0, top), 0.6, mats[(i + 1) % len(mats)], 6))
            main.append(K.tube(f"{tag}_g{i}", (*g0, 0.4), (*g1, 0.4), 0.6, mats[0], 6))
    lat = P["lat"]
    lattice("lat_sw", at(-hl - 2.0, -hw - lat), at(hl + 2.0, -hw - lat), at(-hl, -hw - 0.5), at(hl, -hw - 0.5), 6, ("roof", "roof2"))
    lattice("lat_se", at(hl + 4.0, -hw - lat), at(hl + 2 * P["annex_l"] + lat, -hw + 6.0), at(hl + 1.0, -hw + 2.0), at(hl + 2 * P["annex_l"], -hw + 5.0), 3, ("bronze_dk", "grey"), 11.0)
    lattice("lat_e", at(hl + 2 * P["annex_l"] + lat, -hw + 6.0), at(hl + 2 * P["annex_l"] + lat, hw - 4.0), at(hl + 2 * P["annex_l"], -hw + 5.0), at(hl + 2 * P["annex_l"], hw - 5.0), 4, ("bronze_dk", "grey"), 11.0)
    # the lift under the south-east end: a dark shaft with a house-colour frame
    lp = at(*P["lift"])
    main.append(K.obox("lift", lp, A, 4.6, 4.0, 0.0, 13.0, "block_dk", 0.3))
    for s in (-1, 1):
        main.append(K.house(K.obox(f"lift_post{s}", at(P["lift"][0] + s * 4.4, P["lift"][1] - 4.1), A, 0.7, 0.5, 0.0, 12.4, "HOUSE"), 0.32))
    main.append(K.house(K.obox("lift_head", at(P["lift"][0], P["lift"][1] - 4.1), A, 5.0, 0.5, 11.0, 12.4, "HOUSE"), 0.42))
    # ---- three bulbs at the south-west foot, rubble
    bp = P["bulbs"]
    main.extend(K.bulb("bulb0", (bp[0] - 6.5, bp[1] + 3.5), 3.0, 1.0))
    main.extend(K.bulb("bulb1", (bp[0] + 5.5, bp[1] - 4.5), 3.0, 1.0))
    main.append(K.cyl("bulb_base", bp, 4.8, 0.0, 2.0, "bronze_dk", 16))
    main.append(K.house(K.dome("bulb_dome", bp, 4.2, 1.8, 4.2, "HOUSE", 16), 0.7))
    main.extend(K.rubble("rub", (-42.0, 16.0), 5.0, 3, 5))
    main.extend(K.rubble("rub2", (28.0, -16.0), 3.5, 5, 3))
    main.extend(K.rubble("rub3", (-22.0, 2.0), 3.0, 7, 3))
    if damaged:
        K.hole(main, [roof], at(-8.0, 5.0), 6.0, zf, 3, depth=6.0, panel=("block", "roof2"), cut_up=8.0)
        K.hole(main, [roof], at(8.0, -6.0), 4.5, zf, 5, depth=6.0, panel=("block", "roof2"), cut_up=8.0)
        K.wall_gash(main, at(-4.0, -hw - 0.5), A, 7.0, z0 + 6.0, z1 - 6.0, 7, 0.9)
        K.wall_gash(main, at(hl + 0.5, 3.0), A + 90.0, 5.0, z0 + 6.0, z1 - 6.0, 8, 0.9)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", flags=K.flags(0.2, shadow_azimuth=30.0)), P=P)
