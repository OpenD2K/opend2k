"""Smuggler star port - the original design rebuilt with tools/models/ordos_kit.py (done in the Ordos batch: its pad
is the Ordos star port's, the rest is its own art).

    blender -b --factory-startup --python tools/models/ordos_smuggler_star_port.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=SMUGGLER_STAR_PORT .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 55, a base-only art: frame 3882 is drawn under units; damaged 3884; no animation;
build-up 46 kept):
  * the round dark-grey landing pad (r 32) with a pale diagonal stripe;
  * round its north half a bank of orange sandstone with a brass ledge; on it two towers - a gold dome on a pale
    fluted drum at the north, a dark bronze dome on a fluted drum at the north-east - a pale brass pylon between
    pad and towers, a small dome on the west bank;
  * at the south rim a red fluted drum with a gold dome on a sandstone foot; pale guide arcs on the sand.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "SMUGGLER_STAR_PORT"
P = dict(
    pad=(0.0, 3.0), pad_r=33.5, pit=2.4,
)


def tower(main, tag, c, r, h, wall, dome_mat):
    main.append(K.cyl(f"{tag}_drum", c, r, 0.0, h, wall, 24))
    for i in range(14):
        a = 360.0 / 14 * i
        main.append(K.beam(f"{tag}_flute{i}", K.polar(c, r + 0.1, a, 1.0), K.polar(c, r + 0.1, a, h - 1.0), 1.4, 0.8, "grey_dk" if wall == "grey" else "rib"))
    main.append(K.lathe_loop(f"{tag}_ring", c, [(r - 0.2, h - 0.6), (r + 0.8, h - 0.6), (r + 0.8, h + 0.8), (r - 0.2, h + 1.0)], "roof2", 24))
    main.extend(K.rdome(f"{tag}_dome", c, r - 0.4, h + 0.8, r * 0.85, dome_mat, ribs=0, segs=24))


def build(damaged=False):
    main = []
    c, r, pz = P["pad"], P["pad_r"], P["pit"]
    main.extend(K.skirt("rock", [K.polar(c, r + 1.0, 20.0 * i) for i in range(18)], 2.0, 3, 2.0, material="stone_dk", dark="block_dk", lumps=8))
    main.append(K.cyl("pad", c, r, 0.0, pz, "grey_dk", 48))
    main.append(K.lathe_loop("pad_rim", c, [(r - 1.5, pz - 0.2), (r + 0.6, pz - 0.2), (r + 0.6, pz + 0.7), (r - 1.5, pz + 0.5)], "block_dk", 48))
    main.append(K.beam("stripe", (c[0] - 25.0, c[1] - 14.0, pz), (c[0] + 25.0, c[1] + 14.0, pz), 9.0, 0.16, "grey"))
    main.append(K.beam("stripe2", (c[0] - 9.0, c[1] + 17.0, pz), (c[0] + 9.0, c[1] - 17.0, pz), 2.0, 0.14, "grey"))
    for i in range(12):
        main.append(K.seam(f"pad_seam{i}", K.polar(c, r * 0.3, 30.0 * i, pz + 0.05), K.polar(c, r - 1.6, 30.0 * i, pz + 0.05), 0.35, 0.15))
    # the sandstone bank round the north half with a brass ledge
    bank = [K.polar(c, r + 17.5 + 4.0 * math.sin(math.radians(a)), a) for a in range(15, 196, 15)] + [K.polar(c, r - 1.0, a) for a in range(195, 14, -15)]
    main.extend(K.skirt("bank", bank, 9.0, 5, 2.0, material="sand", dark="sand_dk", lumps=14))
    main.append(K.arc("cliff", c, r - 1.8, r - 0.4, 18.0, 192.0, 0.0, 8.6, "block_dk", 24))
    main.append(K.arc("ledge", c, r - 1.5, r + 0.8, 20.0, 190.0, 8.4, 9.6, "roof", 24, 0.3))
    for i in range(8):
        a = 30.0 + 20.0 * i
        main.append(K.beam(f"ledge_rib{i}", K.polar(c, r - 1.7, a, 2.6), K.polar(c, r - 1.7, a, 9.8), 1.3, 0.7, "rib"))
    tower(main, "tn", (-9.0, 41.0), 9.5, 19.0, "grey", "bronze")
    tower(main, "te", (27.0, 37.0), 10.5, 19.0, "grey", "bronze_dk")
    main.append(K.ball("te_lamp", (16.0, 34.0, 19.0), 1.3, "cyan", 8))
    main.append(K.obox("pylon", (15.0, 30.0), 20.0, 2.2, 2.2, 0.0, 16.0, "white", 0.3, 0.5))
    for s in (-1, 1):
        main.append(K.beam(f"pylon_arm{s}", (15.0, 30.0, 9.0), (15.0 + s * 7.0, 30.0 - 3.0, 4.0), 1.6, 1.2, "roof"))
    wd = (-38.0, 24.0)
    main.extend(K.rdome("wdome", wd, 4.6, 9.0, 4.0, "roof", ribs=0, segs=16))
    main.append(K.cyl("wdome_drum", wd, 5.0, 0.0, 9.2, "roof2", 16))
    for i, (x, y) in enumerate(((-22.0, 40.0), (8.0, 52.0), (40.0, 24.0))):
        main.append(K.cyl(f"bank_pit{i}", (x, y), 2.4, 8.6, 9.3, "sand_dk", 10))
    # the south drum: red flutes, a gold dome, on a sandstone foot
    sd = (2.0, c[1] - r - 27.0)
    main.extend(K.skirt("sfoot", [K.polar((sd[0], sd[1] + 10.0), 18.0, 30.0 * i) for i in range(12)], 6.0, 7, 2.0, material="sand", dark="sand_dk", lumps=8))
    main.append(K.cyl("sdrum", sd, 9.5, 0.0, 12.0, "bronze_dk", 24))
    for i in range(16):
        a = 22.5 * i
        main.append(K.beam(f"sdrum_flute{i}", K.polar(sd, 9.6, a, 1.2), K.polar(sd, 9.6, a, 11.0), 1.9, 0.8, "red"))
    main.append(K.lathe_loop("sdrum_ring", sd, [(9.2, 11.4), (10.4, 11.4), (10.4, 12.8), (9.2, 13.0)], "roof", 24))
    main.extend(K.rdome("sdome", sd, 9.2, 12.8, 7.0, "bronze", ribs=0, segs=24))
    main.extend(K.lamp("slamp", (sd[0] - 4.0, sd[1] - 10.5, 2.0), 0.9))
    main.append(K.obox("slab_w", (-37.0, 9.0), 10.0, 8.0, 2.6, 0.0, 2.4, "white", 0.3))
    main.extend(K.rubble("rub_e", (40.0, 8.0), 4.5, 9, 4))
    main.extend(K.rubble("rub_s", (-12.0, -50.0), 4.0, 11, 3))
    for s, a0 in ((0, 196.0), (1, -34.0)):
        for i in range(3):
            main.append(K.arc(f"guide{s}_{i}", c, r + 7.0 + 4.0 * i, r + 9.0 + 4.0 * i, a0 + 2.0 * i, a0 + 22.0 - 2.0 * i, 0.0, 1.0, "stone", 8, 0.2))
    if damaged:
        K.hole(main, [], (c[0] - 12.0, c[1] + 6.0), 7.0, pz + 0.05, 3, depth=2.0, panel=("grey", "grey_dk"))
        K.hole(main, [], (c[0] + 14.0, c[1] - 8.0), 5.0, pz + 0.05, 5, depth=2.0, panel=("grey", "grey_dk"))
        K.scorch(main, (c[0] + 2.0, c[1] + 16.0), 8.0, pz + 0.1, 7, 6)
        K.wall_gash(main, K.polar((26.0, 39.0), 10.8, 250.0), 160.0, 5.0, 3.0, 11.0, 9, 0.8)
        K.wall_gash(main, K.polar(sd, 9.9, 280.0), 10.0, 5.0, 3.0, 10.0, 11, 0.8)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(flags=K.flags(-0.2, shadow_azimuth=25.0)), P=P)
