"""Atreides high tech factory - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_high_tech_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_HIGH_TECH_FACTORY .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3109 main, 3110 damaged, no base; animation 32 = 10 ADDITIVE overlay frames, kept
original; build-up 32 kept):
  * west: a tall cream hangar block turned about 25 degrees, a big house-colour roundel on its roof, a
    house-colour door low on its south-west wall;
  * east: the launch tower - an open lattice (six splayed legs, rings, X braces) carrying a round copper deck with
    a golden dome, a tall thin mast with a collar and a dark cap; a big dark hoop round the legs' feet;
  * south-west: a pump station - orange and cream pipes, a white ball tank; a brown aerial at the west.
Free choices: vents, seams, the legs' bracing, lamps.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_HIGH_TECH_FACTORY"
P = dict(
    ang=-33.0, hall=(-10.0, 30.5), hall_hl=19.5, hall_hw=23.5, hall_h=34.0,
    deck=(15.0, 18.0, 41.0), deck_r=14.5, mast=44.0,
    hoop=(23.0, -1.0, 12.0), hoop_r=21.0, foot_r=19.0, neck_r=6.5, sun_el=64.0, sh_az=59.0, pump=(1.0, 4.0), foot=(23.0, 1.0),
)


def rot(c, t, s):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def build(damaged=False):
    main = []
    A, hc, hl, hw, hh = P["ang"], P["hall"], P["hall_hl"], P["hall_hw"], P["hall_h"]
    # ---- the hangar block
    main.append(K.obox("hall_foot", hc, A, hl + 1.2, hw + 1.2, 0.0, 2.4, "foot", 0.3))
    main.append(K.obox("hall", hc, A, hl, hw, 0.0, hh * 0.62, "slab", 2.5, 0.96))
    main.append(K.obox("hall_up", hc, A, hl * 0.96, hw * 0.96, hh * 0.6, hh, "roof", 2.5, 0.92))
    for i in range(9):
        t = -hl + (i + 0.5) * 2 * hl / 9
        main.append(K.obox(f"hall_rib{i}", rot(hc, t, -hw - 0.1), A, 0.5, 0.5, 1.0, hh * 0.6, "slab_dk"))
    main.append(K.obox("hall_cornice", hc, A, hl * 0.885 + 0.5, hw * 0.885 + 0.5, hh - 1.2, hh + 0.5, "wall", 0.3))
    main.append(K.obox("hall_roof", hc, A, hl * 0.885 - 1.2, hw * 0.885 - 1.2, hh + 0.3, hh + 1.0, "roof2", 0.2))
    main.append(K.house(K.cyl("roundel", rot(hc, 3.0, 0.0), 13.0, hh + 0.8, hh + 1.6, "HOUSE", 36), 0.55))
    main.append(K.lathe_loop("roundel_rim", rot(hc, 3.0, 0.0), [(13.7, hh + 0.8), (13.7, hh + 1.9), (12.8, hh + 1.9), (12.8, hh + 0.8)], "wall", 36))
    for i in range(2):
        main.append(K.seam(f"roundel_seam{i}", (*K.polar(rot(hc, 3.0, 0.0), 12.5, A + 90.0 * i), hh + 1.65), (*K.polar(rot(hc, 3.0, 0.0), 12.5, A + 90.0 * i + 180.0), hh + 1.65), 0.4, 0.3))
    for i, t in enumerate((-11.0, 11.0)):                # pilasters and the house door on the south-west wall
        main.append(K.obox(f"hall_pil{i}", rot(hc, t, -hw - 0.2), A, 1.2, 0.8, 0.0, hh - 1.2, "wall", 0.2))
    for i, t in enumerate((-5.0, 2.0)):
        main.append(K.house(K.obox(f"door{i}", rot(hc, t - 4.0, -hw - 0.3), A, 3.0, 0.8, 8.0, 17.0, "HOUSE", 0.15), 0.8))
    main.append(K.obox("door_frame", rot(hc, -5.5, -hw - 0.1), A, 7.6, 0.7, 17.0, 18.6, "wall", 0.2))
    for i, s_ in enumerate((-7.0, 0.0, 7.0)):
        main.extend(K.vent(f"hall_vent{i}", rot(hc, hl + 0.05, s_), A + 90.0, 2.4, 16.0, 22.0))
    main.extend(K.vent("hall_vent_sw", rot(hc, 10.0, -hw - 0.05), A, 3.0, 20.0, 26.0))
    # ---- the launch tower: an open truss - six splayed legs, three polygon rings, X braces in every bay, a thin
    # core pipe - carrying the round copper deck; the big dark hoop round its foot
    dx, dy, dz = P["deck"]
    dr = P["deck_r"]
    main.append(K.cyl("deck", (dx, dy), dr, dz - 1.6, dz, "copper", 36))
    main.append(K.lathe_loop("deck_rim", (dx, dy), [(dr + 0.6, dz - 1.9), (dr + 0.6, dz + 0.7), (dr - 0.8, dz + 0.7), (dr - 0.8, dz - 1.9)], "orange", 36))
    main.append(K.cyl("deck_plate", (dx, dy), dr - 3.0, dz - 0.1, dz + 0.3, "skirt", 28))
    for k in range(8):
        main.append(K.seam(f"deck_seam{k}", K.polar((dx, dy), 3.0, 45.0 * k, dz + 0.32), K.polar((dx, dy), dr - 1.2, 45.0 * k, dz + 0.32), 0.4, 0.3))
    main.append(K.dome("deck_dome", (dx + 3.5, dy + 3.0), 6.2, dz + 0.2, 5.6, "orange", 20))
    main.append(K.lathe_loop("deck_skirt", (dx, dy), [(dr - 1.0, dz - 4.5), (dr - 0.4, dz - 1.6), (dr - 2.4, dz - 1.6), (dr - 3.0, dz - 4.5)], "copper_dk", 36))
    hx, hy, hz = P["hoop"]
    fc = P["foot"]                                         # centre of the legs' feet
    NL = 6
    levels = [0.0, 0.3, 0.58, 0.82, 1.0]                   # along the legs, foot -> deck

    def node(k, t):
        a = 30.0 + 360.0 * k / NL
        f = K.polar(fc, P["foot_r"], a, 0.0)
        d = K.polar((dx, dy), P["neck_r"], a, dz - 3.0)
        return tuple(f[i] + (d[i] - f[i]) * t for i in range(3))
    for k in range(NL):
        main.append(K.beam(f"leg{k}", node(k, 0.0), node(k, 1.0), 2.2, 2.2, "skirt" if k % 2 else "copper", 0.3))
        main.append(K.cyl(f"leg{k}_foot", node(k, 0.0)[:2], 2.8, 0.0, 1.8, "foot", 10))
        for li in range(1, len(levels) - 1):
            main.append(K.beam(f"ring{k}_{li}", node(k, levels[li]), node((k + 1) % NL, levels[li]), 1.3, 1.3, "copper_dk" if li % 2 else "rust"))
        for li in range(len(levels) - 1):                  # X braces
            main.append(K.beam(f"xa{k}_{li}", node(k, levels[li]), node((k + 1) % NL, levels[li + 1]), 0.8, 0.8, "copper"))
            main.append(K.beam(f"xb{k}_{li}", node((k + 1) % NL, levels[li]), node(k, levels[li + 1]), 0.8, 0.8, "copper_dk"))
    main.append(K.tube("core", ((dx + hx) / 2, (dy + hy) / 2, 0.0), (dx, dy, dz - 2.0), 1.8, "copper_dk", 10))
    main.append(K.cyl("core_tank", ((dx + hx) / 2, (dy + hy) / 2), 5.0, 0.0, 6.0, "block_dk", 16))
    main.append(K.lathe_loop("hoop", (hx, hy), [(P["hoop_r"] + 1.2, hz - 1.3), (P["hoop_r"] + 1.2, hz + 1.3), (P["hoop_r"] - 1.2, hz + 1.3), (P["hoop_r"] - 1.2, hz - 1.3)], "steel_dk", 48))
    for k in range(6):
        p = K.polar((hx, hy), P["hoop_r"], 60.0 * k)
        main.append(K.tube(f"hoop_post{k}", (p[0], p[1], 0.0), (p[0], p[1], hz), 0.9, "copper_dk", 6))
        main.append(K.beam(f"hoop_tie{k}", (p[0], p[1], hz), node(k, 0.3), 0.7, 0.7, "copper_dk"))
    mx, my = dx - 1.0, dy + 4.0
    mt = dz + P["mast"]
    main.append(K.cyl("mast", (mx, my), 2.4, dz, mt, "copper", 10, 1.6))
    main.append(K.cyl("mast_collar", (mx, my), 4.0, dz + P["mast"] * 0.62, dz + P["mast"] * 0.62 + 2.4, "steel", 12))
    main.append(K.cyl("mast_cap", (mx, my), 2.8, mt - 1.0, mt + 1.6, "recess", 10))
    main.append(K.cyl("mast_base", (mx, my), 3.4, dz, dz + 3.0, "copper_dk", 12))
    for k in range(3):                                     # ladder hoops up the mast
        z = dz + P["mast"] * (0.2 + 0.13 * k)
        main.append(K.cyl(f"mast_ring{k}", (mx, my), 2.9, z, z + 0.7, "copper_dk", 10))
    n_pump = len(main)
    # ---- the pump station (south-west), the aerial (west)
    main.append(K.obox("pump", (-18.0, -6.0), -25.0, 8.0, 5.0, 0.0, 5.0, "stone_dk", 0.5, 0.9))
    main.append(K.ball("pump_ball", (-33.0, -10.0, 4.0), 4.2, "white", 14))
    main.append(K.cyl("pump_ball_foot", (-33.0, -10.0), 2.6, 0.0, 1.6, "foot", 10))
    main.extend(K.pipe("pump_pipe0", [(-40.0, 6.0, 3.0), (-30.0, 2.0, 3.0), (-22.0, -2.0, 6.0)], 1.6, "orange"))
    main.extend(K.pipe("pump_pipe1", [(-32.0, -8.0, 5.0), (-24.0, -12.0, 7.0), (-14.0, -10.0, 6.0)], 1.4, "cream_pipe"))
    main.extend(K.pipe("pump_pipe2", [(-16.0, -8.0, 7.0), (-6.0, -16.0, 7.0), (0.0, -18.0, 2.0)], 1.5, "orange"))
    main.extend(K.pipe("pump_pipe3", [(-24.0, 0.0, 6.0), (-20.0, 10.0, 9.0), (-18.0, 18.0, 9.0)], 1.3, "cream_pipe"))
    for i in range(3):
        x = -30.0 + 6.0 * i
        main.append(K.beam(f"pump_strut{i}", (x + 4.0, -8.0 - i, 9.0), (x + 2.0, -16.0 - i, 0.3), 1.0, 1.0, "roof2"))
    main.append(K.tube("aerial", (-32.0, 22.0, 0.0), (-32.0, 22.0, 20.0), 0.7, "copper_dk", 6))
    main.append(K.beam("aerial_bar", (-35.0, 22.0, 16.0), (-29.0, 22.0, 18.0), 0.6, 0.6, "copper"))
    for o in main[n_pump:]:
        o.location.x += P["pump"][0]
        o.location.y += P["pump"][1]
    main.extend(K.rocks("rock_se", (30.0, -12.0), 4.5, 3, 4))
    for i, p in enumerate((rot(hc, -hl * 0.9, -hw * 0.9), rot(hc, hl * 0.9, -hw * 0.9))):
        main.extend(K.lamp(f"lamp{i}", (p[0], p[1], hh + 0.9), 0.7))
    if damaged:
        K.hole(main, [], rot(hc, -6.0, 2.0), 7.0, hh + 1.2, 3, depth=4.0, panel=("roof", "roof2"))
        K.hole(main, [], rot(hc, 9.0, -6.0), 4.0, hh + 1.2, 4, depth=4.0, panel=("roof", "roof2"))
        K.wall_gash(main, rot(hc, -2.0, -hw - 0.5), A, 8.0, 10.0, 20.0, 5, 0.9)
        K.scorch(main, (dx - 3.0, dy - 2.0), 7.0, dz + 0.4, 6, 6)
        K.wall_gash(main, rot(hc, hl + 0.4, 3.0), A + 90.0, 6.0, 6.0, 16.0, 7, 0.9)
    return dict(nodes=dict(main=main), anchors=[("fx_light_0", (mx, my, mt + 1.8), 0.6)])


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="keep", flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09)), P=P)
