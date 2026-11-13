"""Mercenary heavy factory, redesign 2 (2026-11-13): the original's forms with a real, open vehicle exit, laid out
inside its footprint cells (Graphics: 3D shows it at scale 1.0; the first model was shrunk to 0.635). Built with
tools/models/ordos_kit.py (the Ordos batch's palette) on the layout shared by the four heavy factories:
tools/models/heavy_factory_kit.py (read its docstring: cells, exit path, podium / hall / garage / apron, nodes).

    blender -b --factory-startup --python tools/models/ordos_mercenary_heavy_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=MERCENARY_HEAVY_FACTORY .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 56: base 3885 = under units: the pale stone ramp on the south-west side with its gate
posts; main 3886; damaged 3887; animation 44 = 8 ADDITIVE overlay frames kept original; build-up 41 kept):
  * the turned platform with the pale ramp and brass-ball gate posts, the dark ribbed south-east slope;
  * a long gabled shed along the long axis: pale fluted walls with house-colour doors, a roof of orange-brown
    strips with a dark hatch, two tall chimneys side by side, a brass dome at its south-east end, a brass ball on
    the west corner.
New: the shed stands over an open garage; the bay door in its south-west wall, forecourt and pale apron lead to
the exit cell (see the kit).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402
import heavy_factory_kit as HF  # noqa: E402

KEY = "MERCENARY_HEAVY_FACTORY"
P = dict(HF.P)
P.update(c=(0.0, 23.0), ang=-41.0, hl=43.0, round=10.0, ph=14.0, hall=(-35.0, 14.0, -12.0, 19.5), hall_h=42.0, apron_end=-34.0)      # heavy_factory_fit.py (hall_h = the eaves)
RISE = 7.0
DRESS = dict(HF.DRESS, podium="stone", kerb="wall", slab="foot", apron="hf_apron", apron_line="grey_dk", lane=("HOUSE", 0.5),
             hall="white", hall_rim="rim", post="bronze", post_cap="bronze_dk", band=("HOUSE", 0.5),
             bastion="stone", bastion_top="block_dk", ring=("HOUSE", 0.4), slope_rib="bronze_dk", se_rib="bronze")


def build(damaged=False):
    HF.install(K, apron=(0.800, 0.780, 0.720))
    cx = HF.layout(HF.Ctx(K, P, damaged, dict(DRESS)))
    main, ph, H, bh = cx.main, P["ph"], P["hall_h"], P["bay_h"]
    h0, h1, hs0, hs1 = cx.P["hall"]
    hl, hw, d, rr = P["hl"], P["hw"], P["inset"], P["round"]
    b0, b1, _bs = cx.P["bay"]
    zf = HF.gable_roof(cx, RISE, strips=("wall", "roof"), deck="roof2", ridge="white")
    sm = (hs0 + hs1) / 2
    # fluted walls: flutes on the south-west wall (beside and over the door) and the south-east gable wall
    n = 16
    for i in range(n):
        t = h0 + 1.6 + (h1 - h0 - 3.2) * i / (n - 1)
        z0 = ph + 0.3 if t < b0 - 2.2 or t > b1 + 2.2 else bh + 5.6
        main.append(cx.ob(f"shed_flute{i}", t, hs0 - 0.12, 0.8, 0.5, z0, H - 1.2, "roof2" if i % 2 else "white"))
    for i in range(7):
        s_ = hs0 + 2.0 + (hs1 - hs0 - 4.0) * i / 6
        main.append(cx.ob(f"shed_rib{i}", h1 + 0.12, s_, 0.8, 0.5, ph + 0.3, H - 1.2, "rib", turn=90.0))
    main.append(cx.poly("shed_gable_se", [(h1 - 0.2, hs0), (h1 + 0.3, hs0), (h1 + 0.3, hs1), (h1 - 0.2, hs1)], H - 1.0, H + 0.2, "wall"))
    HF.facade(cx, 0.5, vent=False)
    # the dark hatch on the roof, two tall chimneys side by side
    hp = cx.R(h1 - 26.0, sm - (hs1 - hs0) * 0.25)
    main.append(K.cyl("shed_hatch", hp, 3.4, zf(*hp) - 1.0, zf(*hp) + 0.9, "block_dk", 14))
    for i, t in enumerate((h1 - 17.5, h1 - 11.5)):
        sp = cx.R(t, sm + 4.5 + 2.5 * i)
        fl, top = K.flue(f"stack{i}", sp, 1.7, H, H + RISE + 19.0 + 3.0 * i, "bronze", "bronze_dk")
        main.extend(fl)
        main.append(K.obox(f"stack{i}_foot", sp, cx.A, 2.8, 2.8, H - 1.0, zf(*sp) + 4.0, "bronze_dk", 0.3))
        cx.anchors.append((f"fx_flue_{i}", top, 0.7))
    # brass: the ball on the west corner drum, the gate posts, the dome at the south-east end, the south corner
    bx, by, bz = HF.bastion(cx)
    main.extend(K.bulb("bastion_bulb", (bx, by), 4.4, bz - 0.2))
    for i, t in enumerate((b0 - 1.1, b1 + 1.1)):
        main.extend(K.bulb(f"gate_bulb{i}", cx.R(t, -hw + d + 0.6), 2.0, ph + 4.0, post=False))
    dp = cx.R(hl - d - 9.5, -hw + d + 12.0)
    main.append(K.cyl("dome_drum", dp, 6.4, ph, ph + 2.0, "bronze_dk", 20))
    main.extend(K.rdome("dome", dp, 6.0, ph + 1.8, 6.0, "gold", ribs=0, segs=20))
    sc = cx.R(hl - rr - 0.5, -hw + rr + 0.5)
    main.extend(K.bulb("south_bulb", sc, 2.6, ph + 0.4))
    t0, t1 = cx.R(h1 + 8.0, 6.0, ph + 2.8), cx.R(h1 + 8.0, 17.0, ph + 2.8)
    main.append(K.tank("terrace_tank", t0, t1, 2.7, "bronze", 16, 0.4))
    main.append(K.hoop("terrace_tank_band", t0, t1, 0.42, 0.58, 2.9, "gold", 16))
    for i, (t, s_) in enumerate(((hl - d - 1.6, 12.0), (hl - d - 1.6, -4.0), (-hl + d + 1.6, hs0 - 2.0))):
        main.extend(K.lamp(f"lamp{i}", cx.R(t, s_, ph + 1.8), 0.8))
    if damaged:
        K.hole(main, cx.roof_parts, cx.R(h1 - 20.0, sm + 5.0), 6.0, zf, 3, depth=6.0, panel=("roof2", "bronze"), cut_up=6.0)
        K.hole(main, cx.roof_parts, cx.R(h1 - 6.0, sm - 6.0), 4.5, zf, 5, depth=6.0, panel=("roof2", "bronze"), cut_up=6.0)
        K.wall_gash(main, cx.R(h0 + 5.0, hs0 - 0.3), cx.A, 4.0, ph + 3.0, ph + 14.0, 9)
        K.wall_gash(main, cx.R(h1 + 0.3, sm + 2.0), cx.A + 90.0, 5.0, ph + 4.0, ph + 14.0, 8)
        K.scorch(main, cx.R(30.0, -2.0), 5.0, ph + 0.5, 10, 5)
    return HF.finish(cx)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", flags=K.flags(-0.1, shadow_azimuth=35.0)), P=P)
