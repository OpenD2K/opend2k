"""Harkonnen heavy factory, redesign 2 (2026-11-13): the original's forms with a real, open vehicle exit, laid out
inside its footprint cells (Graphics: 3D shows it at scale 1.0). Layout shared by the four heavy factories:
tools/models/heavy_factory_kit.py (read its docstring: cells, exit path, podium / hall / garage / apron, nodes).
Art 18 is shared with the IMPERIAL HEAVY FACTORY: these frames and the glb (merged under both names) replace it too;
the house colour follows the owner.

    blender -b --factory-startup --python tools/models/harkonnen_heavy_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_HEAVY_FACTORY .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3331 base, 3332 main, 3333 damaged; animation 42 = 10 ADDITIVE welding flashes,
kept original; build-up 41 kept): the Atreides layout in the Harkonnen dress -
  * a soot-dark plated platform with a broad pale concrete kerb round its west / south-west foot and ramp;
  * the works soot-dark with a brass rim, two large house-colour domes, house-colour bars over the south-west door
    and on the south-east wall, two heavy brown chimneys at the north-east edge, a pale tank lying in front;
  * a plain dark turntable deck with a small steel dome.
New: the open garage under the works, the bay door, forecourt and pale apron to the exit cell (see the kit).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import heavy_factory_kit as HF  # noqa: E402

KEY = "HARKONNEN_HEAVY_FACTORY"
P = dict(HF.P)
P.update(c=(0.0, 24.0), ang=-38.5, round=11.0, ph=14.0, hall=(-28.5, 13.0, -19.5, 21.0), hall_h=38.0, apron_end=-34.0)      # heavy_factory_fit.py
DRESS = dict(HF.DRESS, podium="stone", kerb="white", slab="white", apron="hf_apron", apron_line="stone_dk", lane=("HOUSE", 0.3),
             hall="stone_dk", hall_rim="hk_brass", hall_roof="hk_black", post="hk_black", post_cap="hk_brass", band=("HOUSE", 0.2),
             bastion="stone_dk", bastion_top="hk_black", ring=("HOUSE", 0.16), slope_rib="stone_dk", se_rib="wall", shutter="steel_dk")


def build(damaged=False):
    HF.install(K, apron=(0.820, 0.800, 0.740))
    cx = HF.layout(HF.Ctx(K, P, damaged, dict(DRESS)))
    main, ph, H, bh = cx.main, P["ph"], P["hall_h"], P["bay_h"]
    h0, h1, hs0, hs1 = cx.P["hall"]
    hl, hw, d, rr = P["hl"], P["hw"], P["inset"], P["round"]
    b0, b1, _bs = cx.P["bay"]
    zr = HF.flat_roof(cx)
    # two large house-colour domes in brass drums
    da, db = cx.R(h0 + 9.6, hs1 - 9.0), cx.R(h0 + 10.0, hs0 + 9.6)
    for nm, q, r in (("dome_a", da, 8.8), ("dome_b", db, 8.4)):
        main.append(K.cyl(nm + "_drum", q, r + 0.6, zr - 0.2, zr + 1.5, "hk_brass", 24))
        main.append(K.house(K.dome(nm, q, r, zr + 1.3, r * 0.92, "HOUSE", 24), 0.16))
    # two heavy chimneys at the north-east edge of the roof, a pipe down to the terrace
    for i, (t, h) in enumerate(((h1 - 10.0, H + 21.0), (h1 - 3.4, H + 17.0))):
        q = cx.R(t, hs1 - 3.4)
        parts, top = K.stack(f"stack{i}", q, 2.6, zr - 0.5, h, "copper", 3)
        main.extend(parts)
        main.append(K.obox(f"stack{i}_base", q, cx.A, 3.3, 3.3, zr - 0.5, zr + 6.0, "rust", 0.3))
        cx.anchors.append((f"fx_flue_{i}", top, 0.7))
    main.extend(K.pipe("roof_pipe", [cx.R(h1 - 16.0, hs1 - 3.4, zr + 1.0), cx.R(h1 - 22.0, hs1 - 3.4, zr + 1.0), cx.R(h1 - 22.0, hs1 - 9.0, zr + 1.0)], 1.2, "copper"))
    main.append(cx.ob("roof_hatch", h1 - 8.0, hs0 + 9.0, 4.4, 3.4, zr - 0.2, zr + 1.5, "stone", 0.3))
    main.append(cx.ob("roof_hatch_lid", h1 - 8.0, hs0 + 9.0, 3.6, 2.6, zr + 1.4, zr + 1.9, "hk_black", 0.15))
    # house bars over the door (the original's red bars on the south-west wall) and on the south-east wall
    for i, t in enumerate([b0 + 2.5 + (b1 - b0 - 5.0) * k / 5 for k in range(6)]):
        main.append(K.house(cx.ob(f"door_bar{i}", t, hs0 - 0.3, 1.2, 0.5, bh + 12.0, H - 3.0, "HOUSE"), 0.2))
    HF.facade(cx, 0.2)
    main.append(K.house(cx.tbox("works_band", h0, b0 - 1.6, hs0 - 0.5, hs0 + 0.1, ph + 0.6, ph + 3.6, "HOUSE"), 0.2))
    # the corner drum with a steel dome, brass caps on the gate posts, a lamp post on the south corner
    bx, by, bz = HF.bastion(cx)
    main.append(K.dome("bastion_dome", (bx, by), 4.0, bz, 3.0, "hk_plate", 18))
    for i, t in enumerate((b0 - 1.1, b1 + 1.1)):
        main.append(K.ball(f"gate_ball{i}", cx.R(t, -hw + d + 0.6, ph + 5.4), 1.7, "hk_gold", 12))
    sc = cx.R(hl - rr - 0.5, -hw + rr + 0.5)
    main.append(K.cyl("south_post", sc, 2.8, ph, ph + 2.4, "hk_black", 12))
    main.append(K.ball("south_ball", (sc[0], sc[1], ph + 4.0), 2.6, "hk_plate", 14))
    # the terrace: the plain dark turntable deck with a steel dome, the pale tank, lamps
    tc = cx.R(25.5, -9.0)
    main.append(K.cyl("table_base", tc, 7.4, ph, ph + 1.3, "hk_black", 28))
    main.append(K.cyl("table_mid", tc, 5.2, ph + 1.2, ph + 2.0, "stone_dk", 24))
    main.append(K.dome("table_dome", tc, 3.6, ph + 1.9, 3.0, "hk_plate", 20))
    t0, t1 = cx.R(27.5, 4.0, ph + 3.2), cx.R(27.5, 17.0, ph + 3.2)
    main.append(K.tank("front_tank", t0, t1, 3.0, "hk_plate", 16, 0.4))
    main.append(K.hoop("front_tank_band", t0, t1, 0.44, 0.56, 3.25, "hk_brass", 16))
    for i, (t, s_) in enumerate(((hl - d - 1.6, 12.0), (hl - d - 1.6, -4.0), (-hl + d + 1.6, hs0 - 2.0))):
        main.extend(K.lamp(f"lamp{i}", cx.R(t, s_, ph + 1.8), 0.8))
    if damaged:
        K.hole(main, [], cx.R(h1 - 14.0, hs0 + 12.0), 6.0, zr, 3, depth=4.0, panel=("block", "stone"))
        K.scorch(main, da, 7.0, lambda x, y: zr + 1.3 + 8.0 * math.sqrt(max(0.05, 1 - ((x - da[0]) ** 2 + (y - da[1]) ** 2) / 8.8 ** 2)), 5, 5)
        K.scorch(main, tc, 8.0, ph + 2.2, 6, 6)
        K.wall_gash(main, cx.R(h1 + 0.3, 6.0), cx.A + 90.0, 6.0, ph + 4.0, ph + 14.0, 8)
        K.wall_gash(main, cx.R((h0 + b0) / 2, hs0 - 0.3), cx.A, 3.5, bh + 4.0, bh + 13.0, 9)
        K.scorch(main, cx.R(30.0, -2.0), 5.0, ph + 0.5, 10, 5)
    return HF.finish(cx)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", glb_names=["HARKONNEN_HEAVY_FACTORY", "IMPERIAL_HEAVY_FACTORY"],
                                flags=dict(shadow_azimuth=35.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
