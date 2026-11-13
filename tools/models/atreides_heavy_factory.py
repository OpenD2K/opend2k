"""Atreides heavy factory, redesign 2 (2026-11-13): the original's forms with a real, open vehicle exit, laid out
inside its footprint cells (Graphics: 3D shows it at scale 1.0). Layout shared by the four heavy factories:
tools/models/heavy_factory_kit.py (read its docstring: cells, exit path, podium / hall / garage / apron, nodes).

    blender -b --factory-startup --python tools/models/atreides_heavy_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_HEAVY_FACTORY .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3057 base: under units - the bright south-west ramp with two pale house-colour door
plates between brown gate posts; 3058 main; 3059 damaged; animation 41 = 10 ADDITIVE welding flashes, kept original;
build-up 41 kept):
  * a cream diamond platform with a low rim, turned about 41 degrees, round bastion with a white dome on the west
    corner, a white lamp ball on the south corner, its south-east slope in shade with brown ribs;
  * north-west: the works - cream walls, a dark roof with a brown rim, two cream domes (one in a house-colour
    collar), house-colour panels on its south-east wall, a brown tower and a tall copper chimney at its east corner;
  * in the middle, where the unit appears: a white dome on a house-colour ring (kept as the garage's turntable ring
    and a small dome on the terrace).
New: the works stand over an open garage; a unit appears on the turntable inside, and drives out through the bay
door, the forecourt and the pale apron to the exit cell. Free choices: vents, seams, lamps, lit windows, tank.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402
import heavy_factory_kit as HF  # noqa: E402

KEY = "ATREIDES_HEAVY_FACTORY"
P = dict(HF.P)
P.update(c=(0.0, 23.5), ang=-40.0, round=11.0, ph=14.0, hall=(-28.0, 13.0, -18.0, 24.0), hall_h=37.0, apron_end=-34.0)      # heavy_factory_fit.py


def build(damaged=False):
    HF.install(K, apron=(0.930, 0.900, 0.820))
    cx = HF.layout(HF.Ctx(K, P, damaged, dict(HF.DRESS, podium="wall", kerb="white", apron="hf_apron", bastion="wall")))
    main, ph, H, bh = cx.main, P["ph"], P["hall_h"], P["bay_h"]
    h0, h1, hs0, hs1 = cx.P["hall"]
    hl, hw = P["hl"], P["hw"]
    b0, b1, _bs = cx.P["bay"]
    d, rr = P["inset"], P["round"]
    zr = HF.flat_roof(cx)
    # the two domes on the north-west half of the roof
    da, db = cx.R(h0 + 8.8, hs1 - 8.4), cx.R(h0 + 9.2, hs0 + 8.8)
    main.append(K.cyl("dome_a_drum", da, 8.0, zr - 0.2, zr + 1.4, "wall", 24))
    main.append(K.dome("dome_a", da, 7.4, zr + 1.2, 7.0, "roof", 24))
    main.append(K.house(K.cyl("dome_b_collar", db, 8.0, zr - 0.2, zr + 1.6, "HOUSE", 24), 0.32))
    main.append(K.dome("dome_b", db, 6.3, zr + 1.4, 6.1, "white", 24))
    # roof machinery over the garage: a hatch, vents, a copper pipe run
    main.append(cx.ob("roof_hatch", h1 - 9.0, hs1 - 12.0, 4.6, 3.6, zr - 0.2, zr + 1.6, "block", 0.3))
    main.append(cx.ob("roof_hatch_lid", h1 - 9.0, hs1 - 12.0, 3.8, 2.8, zr + 1.5, zr + 2.0, "steel_dk", 0.15))
    for i, s_ in enumerate((hs0 + 5.0, hs0 + 11.0)):
        main.append(cx.ob(f"roof_fan{i}", h1 - 6.0, s_, 2.2, 2.2, zr - 0.2, zr + 1.8, "skirt", 0.3))
        main.append(K.cyl(f"roof_fan{i}_g", cx.R(h1 - 6.0, s_), 1.6, zr + 1.7, zr + 2.0, "recess", 12))
    main.extend(K.pipe("roof_pipe", [cx.R(h0 + 18.5, hs1 - 12.0, zr + 0.9), cx.R(h0 + 18.5, hs0 + 4.0, zr + 0.9), cx.R(h1 - 10.0, hs0 + 4.0, zr + 0.9)], 1.0, "copper"))
    # the brown tower and the chimney at the hall's east corner, on the terrace
    tp, sp = cx.R(h1 - 17.0, hs1 - 4.6), cx.R(h1 - 9.0, hs1 - 2.6)
    main.append(K.obox("tower", tp, cx.A, 3.6, 3.6, zr - 0.5, H + 11.0, "skirt", 0.4, 0.88))
    main.append(K.obox("tower_cap", tp, cx.A, 4.0, 4.0, H + 10.2, H + 12.0, "copper_dk", 0.2))
    main.append(K.house(cx.ob("tower_panel", h1 - 13.3, hs1 - 4.6, 2.2, 0.5, zr + 2.0, H + 8.0, "HOUSE", turn=90.0), 0.3))
    fl, top = K.flue("stack", sp, 1.5, zr - 0.5, H + 20.0)
    main.extend(fl)
    main.append(K.obox("stack_foot", sp, cx.A, 2.6, 2.6, zr - 0.5, zr + 6.0, "rust", 0.3))
    cx.anchors.append(("fx_flue_0", top, 0.7))
    main.extend(K.pipe("tower_pipe", [cx.R(h1 - 5.0, hs1 - 2.6, zr + 3.0), cx.R(h1 + 1.2, hs1 - 2.6, zr + 3.0), cx.R(h1 + 1.2, hs1 - 2.6, ph + 0.3)], 1.1, "copper"))
    # house panels and lit windows on the hall's south-east wall; vents and windows over the bay door
    for i, s_ in enumerate((hs0 + 5.0, hs0 + 13.0, hs0 + 21.0)):
        main.append(K.house(cx.ob(f"hall_panel{i}", h1 + 0.25, s_, 2.6, 0.4, ph + 3.5, H - 5.5, "HOUSE", turn=90.0), 0.3))
    for i, s_ in enumerate((hs0 + 9.0, hs0 + 17.0)):
        cx.window(f"win_se{i}", h1 + 0.2, s_, 0.9, H - 10.5, H - 6.5, turn=90.0)
    for i, t in enumerate([b0 + 3.5 + (b1 - b0 - 7.0) * k / 3 for k in range(4)]):
        cx.window(f"win_sw{i}", t, hs0 - 0.2, 1.7, bh + 8.0, bh + 10.6)
    main.extend(K.vent("hall_vent", cx.R((h0 + b0) / 2, hs0 - 0.05), cx.A, 2.4, bh + 2.0, bh + 9.0))
    # the west bastion with its white dome, lamp balls on the gate posts and the south corner
    bx, by, bz = HF.bastion(cx)
    main.append(K.dome("bastion_dome", (bx - 0.6, by - 0.8), 4.4, bz, 4.2, "white", 18))
    for i, t in enumerate((b0 - 1.1, b1 + 1.1)):
        main.append(K.ball(f"gate_ball{i}", cx.R(t, -hw + d + 0.6, ph + 5.6), 1.9, "white", 12))
    sc = cx.R(hl - rr - 0.5, -hw + rr + 0.5)
    main.append(K.cyl("south_post", sc, 2.6, ph, ph + 1.4, "stone_dk", 12))
    main.append(K.ball("south_ball", (sc[0], sc[1], ph + 4.0), 3.2, "white", 14))
    # the terrace: the small turntable dome of the original, a lying tank, lamps
    tc = cx.R(25.5, -9.0)
    main.append(K.cyl("table_base", tc, 7.2, ph, ph + 1.2, "steel_dk", 28))
    main.append(K.house(K.lathe_loop("table_ring", tc, [(6.9, ph + 1.0), (6.9, ph + 1.9), (4.6, ph + 1.9), (4.6, ph + 1.0)], "HOUSE", 28), 0.3))
    main.append(K.dome("table_dome", tc, 4.4, ph + 1.7, 4.6, "white", 20))
    t0, t1 = cx.R(28.0, 5.0, ph + 3.0), cx.R(28.0, 17.0, ph + 3.0)
    main.append(K.tank("terrace_tank", t0, t1, 2.9, "roof", 16, 0.4))
    main.append(K.hoop("terrace_tank_band", t0, t1, 0.42, 0.58, 3.1, "rim", 16))
    for i, (t, s_) in enumerate(((hl - d - 1.6, 12.0), (hl - d - 1.6, -4.0), (-hl + d + 1.6, hs0 - 2.0))):
        main.extend(K.lamp(f"lamp{i}", cx.R(t, s_, ph + 1.8), 0.8))
    if damaged:
        K.hole(main, [], cx.R(h1 - 14.0, hs0 + 12.0), 6.0, zr, 3, depth=4.0, panel=("block", "stone"))
        K.scorch(main, da, 6.0, lambda x, y: zr + 1.2 + 7.0 * math.sqrt(max(0.05, 1 - ((x - da[0]) ** 2 + (y - da[1]) ** 2) / 7.4 ** 2)), 5, 5)
        K.scorch(main, tc, 8.0, ph + 2.2, 6, 6)
        K.wall_gash(main, cx.R(h1 + 0.3, 6.0), cx.A + 90.0, 6.0, ph + 4.0, ph + 14.0, 8)
        K.wall_gash(main, cx.R((h0 + b0) / 2, hs0 - 0.3), cx.A, 3.5, bh + 4.0, bh + 13.0, 9)
        K.scorch(main, cx.R(30.0, -2.0), 5.0, ph + 0.5, 10, 5)
    return HF.finish(cx)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", flags=dict(shadow_azimuth=35.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
