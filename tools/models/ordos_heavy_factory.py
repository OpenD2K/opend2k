"""Ordos heavy factory, redesign 2 (2026-11-13): the original's forms with a real, open vehicle exit, laid out inside
its footprint cells (Graphics: 3D shows it at scale 1.0). Layout shared by the four heavy factories:
tools/models/heavy_factory_kit.py (read its docstring: cells, exit path, podium / hall / garage / apron, nodes).

    blender -b --factory-startup --python tools/models/ordos_heavy_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_HEAVY_FACTORY .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 36: base 3605 = under units: the pale grey stone ramp on the south-west side between
gate posts with brass balls; main 3606; damaged 3607; animation 43 = 10 ADDITIVE welding flashes kept original;
build-up 41 kept):
  * a brown stone platform turned about 41 degrees, its south-east slope dark with bronze ribs, house-colour bands;
  * the works - a tall brass block with two big gold domes with house-colour crowns, a bronze tower with a house
    panel and a chimney at its north-east edge; a small turntable with a gold dome; brass bulbs on the corners.
The original's picture reaches 8 px into the walkable front row; the model keeps out of it (centre 5 px further
north than the best silhouette fit), so units crossing the front row do not cut through it in 3D.
New: the open garage under the works, the bay door, forecourt and pale apron to the exit cell (see the kit).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402
import heavy_factory_kit as HF  # noqa: E402

KEY = "ORDOS_HEAVY_FACTORY"
P = dict(HF.P)
P.update(c=(0.0, 19.5), ang=-41.0, round=10.5, ph=14.0, hall=(-26.5, 11.0, -20.0, 20.0), hall_h=34.5, apron_end=-36.0)      # heavy_factory_fit.py
DRESS = dict(HF.DRESS, podium="stone", kerb="wall", slab="foot", apron="hf_apron", apron_line="grey_dk", lane=("HOUSE", 0.3),
             hall="roof2", hall_rim="rim", hall_roof="block_dk", post="bronze", post_cap="bronze_dk", band=("HOUSE", 0.3),
             bastion="stone", bastion_top="block_dk", ring=("HOUSE", 0.3), slope_rib="bronze_dk", se_rib="bronze")


def build(damaged=False):
    HF.install(K, apron=(0.800, 0.780, 0.720))
    cx = HF.layout(HF.Ctx(K, P, damaged, dict(DRESS)))
    main, ph, H, bh = cx.main, P["ph"], P["hall_h"], P["bay_h"]
    h0, h1, hs0, hs1 = cx.P["hall"]
    hl, hw, d, rr = P["hl"], P["hw"], P["inset"], P["round"]
    b0, b1, _bs = cx.P["bay"]
    zr = HF.flat_roof(cx)
    # two big gold domes with house-colour crowns
    da, db = cx.R(h0 + 10.5, hs1 - 10.0), cx.R(h0 + 10.5, hs0 + 10.0)
    for nm, q, r in (("dome_a", da, 9.6), ("dome_b", db, 9.2)):
        main.append(K.cyl(nm + "_drum", q, r - 1.2, zr - 0.2, zr + 1.4, "wall", 24))
        main.extend(K.rdome(nm, q, r, zr + 1.2, r * 0.9, "gold", ribs=6, segs=24))
        main.append(K.house(K.dome(nm + "_crown", (q[0] + 0.8, q[1] + 1.2), r * 0.62, zr + 1.2 + r * 0.56, r * 0.5, "HOUSE", 18), 0.3))
    # the bronze tower with a house panel and the chimney at the north-east edge of the roof
    tp, sp = cx.R(h1 - 12.0, hs1 - 4.6), cx.R(h1 - 4.5, hs1 - 2.8)
    main.append(K.obox("tower", tp, cx.A, 3.6, 3.6, zr - 0.5, H + 11.0, "skirt", 0.4, 0.88))
    main.append(K.obox("tower_cap", tp, cx.A, 4.0, 4.0, H + 10.2, H + 12.0, "bronze_dk", 0.2))
    main.append(K.house(cx.ob("tower_panel", h1 - 8.3, hs1 - 4.6, 2.2, 0.5, zr + 2.0, H + 8.0, "HOUSE", turn=90.0), 0.3))
    fl, top = K.flue("stack", sp, 1.5, zr - 0.5, H + 20.0, "bronze", "bronze_dk")
    main.extend(fl)
    main.append(K.obox("stack_foot", sp, cx.A, 2.6, 2.6, zr - 0.5, zr + 6.0, "bronze", 0.3))
    cx.anchors.append(("fx_flue_0", top, 0.7))
    main.extend(K.pipe("roof_pipe", [cx.R(h1 - 7.0, hs0 + 4.0, zr + 0.9), cx.R(h1 - 2.5, hs0 + 4.0, zr + 0.9), cx.R(h1 - 2.5, hs0 + 14.0, zr + 0.9)], 1.1, "bronze"))
    main.append(cx.ob("roof_hatch", h1 - 7.0, hs0 + 10.0, 3.4, 3.0, zr - 0.2, zr + 1.5, "block", 0.3))
    HF.facade(cx, 0.3)
    main.append(K.house(cx.tbox("works_band", h0, b0 - 1.6, hs0 - 0.5, hs0 + 0.1, ph + 0.6, ph + 3.0, "HOUSE"), 0.3))
    # brass bulbs: the corner drum, the gate posts, the south corner
    bx, by, bz = HF.bastion(cx)
    main.extend(K.bulb("bastion_bulb", (bx, by), 4.2, bz - 0.2))
    for i, t in enumerate((b0 - 1.1, b1 + 1.1)):
        main.extend(K.bulb(f"gate_bulb{i}", cx.R(t, -hw + d + 0.6), 2.0, ph + 4.0, post=False))
    sc = cx.R(hl - rr - 0.5, -hw + rr + 0.5)
    main.append(K.cyl("south_post", sc, 2.8, ph, ph + 1.6, "stone_dk", 12))
    main.extend(K.bulb("south_bulb", sc, 3.2, ph + 1.4))
    # the terrace: the turntable with a gold dome, a bronze tank, lamps
    tc = cx.R(26.0, -9.0)
    main.append(K.cyl("table_base", tc, 7.4, ph, ph + 1.2, "steel_dk", 28))
    main.append(K.house(K.lathe_loop("table_ring", tc, [(7.1, ph + 1.0), (7.1, ph + 1.9), (4.8, ph + 1.9), (4.8, ph + 1.0)], "HOUSE", 28), 0.3))
    main.extend(K.rdome("table_dome", tc, 4.4, ph + 1.7, 4.6, "gold", ribs=0, segs=20))
    t0, t1 = cx.R(28.0, 5.0, ph + 3.0), cx.R(28.0, 17.0, ph + 3.0)
    main.append(K.tank("terrace_tank", t0, t1, 2.9, "bronze", 16, 0.4))
    main.append(K.hoop("terrace_tank_band", t0, t1, 0.42, 0.58, 3.1, "gold", 16))
    for i, (t, s_) in enumerate(((hl - d - 1.6, 12.0), (hl - d - 1.6, -4.0), (-hl + d + 1.6, hs0 - 2.0))):
        main.extend(K.lamp(f"lamp{i}", cx.R(t, s_, ph + 1.8), 0.8))
    if damaged:
        K.hole(main, [], cx.R(h1 - 12.0, hs0 + 16.0), 6.0, zr, 3, depth=4.0, panel=("block", "stone"))
        K.scorch(main, da, 7.0, lambda x, y: zr + 1.2 + 8.6 * math.sqrt(max(0.05, 1 - ((x - da[0]) ** 2 + (y - da[1]) ** 2) / 9.6 ** 2)), 5, 5)
        K.scorch(main, tc, 8.0, ph + 2.2, 6, 6)
        K.wall_gash(main, cx.R(h1 + 0.3, 6.0), cx.A + 90.0, 6.0, ph + 4.0, ph + 14.0, 8)
        K.wall_gash(main, cx.R((h0 + b0) / 2, hs0 - 0.3), cx.A, 3.0, bh + 4.0, bh + 13.0, 9)
        K.scorch(main, cx.R(30.0, -2.0), 5.0, ph + 0.5, 10, 5)
    return HF.finish(cx)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", flags=K.flags(-0.1, shadow_azimuth=35.0)), P=P)
