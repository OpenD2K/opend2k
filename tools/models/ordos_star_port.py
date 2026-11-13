"""Ordos star port - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_star_port.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_STAR_PORT .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 51, a base-only art: frame 3870 is drawn under units - the frigate lands on it;
damaged 3872; animation 48 = 8 overlay frames: the landing lights on the pad come on one after another; build-up 46
kept):
  * a round dark-grey landing pad (r 32) with a pale diagonal stripe;
  * along its north rim a brass arc wall with house-colour panels, and a row of round things on it: a big gold
    dome at the west, a teal-black glass orb with a cyan glint in a gold ring, a pale house-colour dome, a second
    bigger orb at the north-east, a brass block with house stripes at the east;
  * at the south rim two small gold domes side by side; pale ribbed guide arcs on the sand at the west and east.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_STAR_PORT"
P = dict(
    pad=(1.0, 3.0), pad_r=33.0, pit=2.4,
)
LIGHTS = [(5.0, -14.0), (21.0, 2.0), (1.0, 8.0), (-27.0, 6.0), (27.0, 8.0), (-10.0, 20.0), (10.0, 20.0)]


def build(damaged=False):
    main = []
    c, r, pz = P["pad"], P["pad_r"], P["pit"]
    main.extend(K.skirt("rock", [K.polar(c, r + 1.0, 20.0 * i) for i in range(18)], 2.0, 3, 2.0, material="stone_dk", dark="block_dk", lumps=10))
    main.append(K.cyl("pad", c, r, 0.0, pz, "grey_dk", 48))
    main.append(K.lathe_loop("pad_rim", c, [(r - 1.5, pz - 0.2), (r + 0.6, pz - 0.2), (r + 0.6, pz + 0.7), (r - 1.5, pz + 0.5)], "block_dk", 48))
    main.append(K.lathe_loop("pad_ring", c, [(r * 0.55, pz), (r * 0.6, pz), (r * 0.6, pz + 0.12), (r * 0.55, pz + 0.12)], "concrete_dk", 40))
    # the pale diagonal stripe and two cross marks (clipped to the pad by keeping them short)
    main.append(K.beam("stripe", (c[0] - 25.0, c[1] - 14.0, pz), (c[0] + 25.0, c[1] + 14.0, pz), 10.0, 0.16, "concrete"))
    main.append(K.beam("stripe2", (c[0] - 9.0, c[1] + 17.0, pz), (c[0] + 9.0, c[1] - 17.0, pz), 2.0, 0.14, "concrete_dk"))
    for i in range(12):
        main.append(K.seam(f"pad_seam{i}", K.polar(c, r * 0.62, 30.0 * i, pz + 0.05), K.polar(c, r - 1.6, 30.0 * i, pz + 0.05), 0.35, 0.15))
    for i, (x, y) in enumerate(LIGHTS):
        main.append(K.cyl(f"light{i}", (x, y), 1.0, pz - 0.1, pz + 0.35, "recess", 8))
    # the arc wall along the north rim with house panels
    main.append(K.arc("wall", c, r - 8.0, r - 1.0, 40.0, 140.0, 0.0, 8.5, "wall", 24, 0.4))
    main.append(K.arc("wall_top", c, r - 7.2, r - 1.8, 42.0, 138.0, 8.3, 9.3, "bronze", 24, 0.2))
    for i in range(7):
        a = 53.0 + 12.5 * i
        main.append(K.house(K.arc(f"panel{i}", c, r - 8.5, r - 7.9, a - 4.0, a + 4.0, 3.2, 7.4, "HOUSE", 4), 0.3 if i % 2 else 0.42))
        main.append(K.beam(f"wall_rib{i}", K.polar(c, r - 8.2, a + 6.2, 2.6), K.polar(c, r - 8.2, a + 6.2, 8.4), 1.3, 0.7, "rib"))
    main.append(K.arc("ramp_e", c, r - 9.0, r - 4.0, 8.0, 40.0, 0.0, 2.6, "roof", 10, 0.3, z1b=8.5))
    main.append(K.arc("ramp_w", c, r - 8.0, r - 3.0, 140.0, 165.0, 0.0, 8.5, "block", 8, 0.3, z1b=2.6))
    # the brown mass between the wall and the row behind it
    terr = [K.polar(c, r - 3.0, a) for a in range(30, 156, 14)] + [K.polar(c, r + 15.0, a) for a in range(156, 29, -14)]
    main.extend(K.skirt("terrace", terr, 5.0, 8, 1.5, material="block", dark="block_dk", lumps=8))
    # the row of round things on the wall
    wd = (-37.0, 33.0)
    main.append(K.cyl("wdome_drum", wd, 12.5, 0.0, 5.0, "block", 28))
    main.extend(K.rdome("wdome", wd, 12.0, 4.8, 9.0, "roof", ribs=8, segs=28))
    main.append(K.house(K.dome("wdome_cap", (wd[0] + 1.0, wd[1] + 1.0), 4.4, 12.2, 2.6, "HOUSE", 14), 0.3))
    o1 = (-19.0, 46.0)
    main.append(K.cyl("orb1_drum", o1, 8.0, 0.0, 7.0, "block", 20))
    main.append(K.lathe_loop("orb1_ring", o1, [(5.0, 6.6), (8.4, 6.6), (8.4, 9.0), (5.0, 9.4)], "gold", 20))
    main.extend(K.orb("orb1", (o1[0], o1[1], 10.5), 5.6, collar=False))
    pd = (8.0, 42.0)
    main.append(K.cyl("pdome_drum", pd, 9.6, 0.0, 9.0, "bronze", 24))
    main.append(K.dome("pdome", pd, 9.0, 8.8, 7.0, "grey", 24))
    for i in range(6):
        a = math.radians(60.0 * i + 20.0)
        main.append(K.house(K.ball(f"pdome_dot{i}", (pd[0] + math.cos(a) * 5.6, pd[1] + math.sin(a) * 5.6, 13.6), 1.2, "HOUSE", 6), 0.3))
    main.append(K.lathe_loop("pdome_ring", pd, [(9.0, 8.4), (9.8, 8.4), (9.8, 9.6), (9.0, 9.8)], "roof", 24))
    o2 = (30.0, 46.0)
    main.append(K.cyl("orb2_drum", o2, 11.0, 0.0, 6.0, "block_dk", 24))
    main.extend(K.rdome("orb2_base", o2, 10.6, 5.8, 5.0, "bronze", ribs=8, segs=24))
    main.append(K.lathe_loop("orb2_ring", o2, [(5.5, 9.6), (7.6, 9.6), (7.6, 11.4), (5.5, 11.8)], "gold", 20))
    main.extend(K.orb("orb2", (o2[0], o2[1], 13.2), 6.6, collar=False))
    eb = (40.0, 27.0)
    main.append(K.obox("eblock", eb, 60.0, 9.0, 6.0, 0.0, 9.0, "skirt", 0.5))
    main.append(K.obox("eblock_top", eb, 60.0, 8.2, 5.2, 8.8, 9.8, "roof2", 0.2))
    for i in range(4):
        q = (eb[0] + math.cos(math.radians(60.0)) * (-6.0 + 4.0 * i) + 5.3, eb[1] + math.sin(math.radians(60.0)) * (-6.0 + 4.0 * i) - 3.0)
        main.append(K.house(K.obox(f"eblock_stripe{i}", q, 60.0, 1.0, 0.5, 2.0, 7.6, "HOUSE"), 0.3))
    main.extend(K.spire("mast", (43.0, 38.0), 0.0, 15.0, 0.5))
    # the twin domes at the south rim
    tb = (0.0, c[1] - r - 7.0)
    main.append(K.obox("twin_base", tb, 0.0, 15.0, 6.5, 0.0, 3.6, "block", 0.6))
    for s in (-1, 1):
        main.extend(K.rdome(f"twin{s}", (tb[0] + s * 7.8, tb[1]), 7.6, 3.4, 6.4, "bronze", ribs=6, segs=20))
        main.extend(K.lamp(f"twin_lamp{s}", (tb[0] + s * 7.4, tb[1] - 6.0, 3.4), 0.7))
    main.extend(K.rubble("twin_rub", (tb[0] + 2.0, tb[1] - 9.0), 5.0, 4, 5))
    # pale guide arcs on the sand, west and east
    for s, a0 in ((0, 192.0), (1, -30.0)):
        for i in range(3):
            main.append(K.arc(f"guide{s}_{i}", c, r + 7.0 + 4.0 * i, r + 9.0 + 4.0 * i, a0 + 2.0 * i, a0 + 20.0 - 2.0 * i, 0.0, 1.0, "stone", 8, 0.2))
    if damaged:
        K.hole(main, [], (c[0] - 12.0, c[1] + 6.0), 7.0, pz + 0.05, 3, depth=2.0, panel=("grey", "grey_dk"))
        K.hole(main, [], (c[0] + 14.0, c[1] - 8.0), 5.0, pz + 0.05, 5, depth=2.0, panel=("grey", "grey_dk"))
        K.scorch(main, (c[0] + 2.0, c[1] + 16.0), 8.0, pz + 0.1, 7, 6)
        K.scorch(main, wd, 6.0, lambda x, y: 13.0, 9, 4)
        K.wall_gash(main, K.polar(c, r - 8.4, 100.0), 10.0, 6.0, 2.5, 8.0, 11, 0.8)
    return dict(nodes=dict(main=main))


def dot_frames():
    grn, red, amb = (60, 230, 80), (240, 70, 50), (250, 200, 60)
    cols = [amb, amb, grn, grn, red, red, amb]
    out = []
    for n in (1, 2, 3, 3, 7, 7, 6, 1):          # the original lights 1, 2, 3, 3, 7, 7, 6, 1 lamps
        out.append([[i, cols[i]] for i in range(n)])
    return out


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="dots", dots=[(x, y, P["pit"] + 0.5) for (x, y) in LIGHTS], dot_frames=dot_frames(), dot_radius=0.7,
                                        flags=K.flags(-0.2, shadow_azimuth=25.0)), P=P)
