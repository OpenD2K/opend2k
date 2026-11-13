"""Atreides star port - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_star_port.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_STAR_PORT .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (a base-only art: frame 3322 = everything, under units - the frigate lands on it; 3324 =
damaged; animation 46 = 8 overlay frames, HAS_ANIMATION: a handful of landing lights coming on in the pit; build-up
46 kept):
  * a round landing pit (r 32): a near-black floor with radial girders - pale where the sun reaches them round the
    south - and a ring; its wall is a crescent, thick and tall round the north and running out to nothing at the
    south-west and south-east;
  * on the wall: a big cream dome at the west-north-west with a house-colour crescent, a small and a large copper
    dome in house-colour collars at the north and north-east, a cream dome between them, cream blocks with house
    panels at the east;
  * the gate house at the south: a cream block with two copper domes in house collars, steps, rubble;
  * low striped fence arcs outside the wall at the south-west and south-east.
Animation: drawn by the pack driver as small lights (spec "dots").
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_STAR_PORT"
P = dict(
    c=(0.0, 9.5), r=43.2, wall=8.0, wall_h=8.0, pit=1.0, gate=(2.0, -33.0), gate_h=8.0, ecc=5.0, sun_el=46.0, sh_az=55.0,
)
LIGHTS = [(-8.0, 15.0), (-2.0, 7.0), (8.0, 9.0), (4.0, 17.0), (-12.0, 5.0), (12.0, 19.0), (0.0, 23.0), (-4.0, -1.0)]


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): the dome tips are beacons, the gate
# house's louvre is a lit window. (The pit's landing lights are drawn by the game: fx_landing_N.)
GLOW = dict(tip=K.lit("orange_lit", "orange", (1.00, 0.70, 0.30)), slat=K.lit("steel_lit", "steel", (0.95, 0.70, 0.38), 0.1),
            dark=K.lit("recess_lit", "recess", (0.60, 0.38, 0.16), 0.08))


def glow(main):
    return K.relight(main, {"dome_n_tip": GLOW["tip"], "dome_ne_tip": GLOW["tip"], "gate_dome0_tip": GLOW["tip"], "gate_dome1_tip": GLOW["tip"],
                            "gate_vent_s": GLOW["slat"], "gate_vent_d": GLOW["dark"]})


def copper_dome(main, name, p, r, z, k=0.5):
    main.append(K.house(K.cyl(f"{name}_collar", p, r + 1.6, z - 0.4, z + 1.4, "HOUSE", 20), k))
    main.append(K.dome(name, p, r, z + 1.2, r * 0.95, "copper", 20))
    main.append(K.cyl(f"{name}_tip", p, r * 0.22, z + 1.0 + r * 0.9, z + 1.6 + r * 0.95, "orange", 8))


def build(damaged=False):
    main = []
    c, r, w, wh = P["c"], P["r"], P["wall"], P["wall_h"]
    # the pit (centre c, radius ri) and its wall: a crescent - thick and tall round the north, running out to nothing
    # at the south-west and south-east (an eccentric ring: the outer circle's centre lies `ecc` north of the pit's)
    ci = c
    co = (c[0], c[1] + P["ecc"])
    ri = r - w
    # the wall: only round the north half (as the original: the pit's south edge meets the ground), full height from
    # east to west, running down to nothing over 30 degrees at both ends
    ro = ri + w
    main.append(K.arc("wall", ci, ri, ro, 10.0, 170.0, 0.0, wh, "stone", 40, 0.6))
    main.append(K.arc("wall_end_e", ci, ri, ro - 2.0, -15.0, 10.0, 0.0, 1.5, "stone", 8, 0.4, wh))
    main.append(K.arc("wall_end_w", ci, ri, ro - 2.0, 170.0, 195.0, 0.0, wh, "stone", 8, 0.4, 1.5))
    main.append(K.arc("wall_skirt", ci, ro - 0.5, ro + 2.5, 8.0, 172.0, 0.0, wh * 0.45, "stone_dk", 40, 0.5))
    main.append(K.arc("wall_lip", ci, ri - 0.4, ri + 1.8, -6.0, 186.0, wh - 0.1, wh + 0.7, "roof", 40))
    for k in range(11):
        a = 15.0 + 15.0 * k
        main.append(K.beam(f"wall_rib{k}", K.polar(ci, ro - 0.6, a, wh + 0.25), K.polar(ci, ri + 1.6, a, wh + 0.25), 1.1, 0.5, "stone_dk" if k % 2 else "block_dk"))
    # the pit: near-black floor, radial girders (pale where the sun reaches them, round the south), rings
    main.append(K.cyl("pit_floor", ci, ri + 0.2, 0.0, P["pit"], "burnt", 64))
    for k in range(12):
        a = 15.0 + 30.0 * k
        south = 180.0 < a < 360.0
        main.append(K.beam(f"girder{k}", K.polar(ci, 7.0, a, P["pit"] + 0.5), K.polar(ci, ri - 0.4, a, P["pit"] + 0.5), 4.0 if south else 2.0, 1.0,
                           ("stone" if k % 4 == 1 else "block_dk") if south else "recess", 0.15))
    main.append(K.arc("pit_ring_s", ci, 22.0, 24.0, 185.0, 355.0, P["pit"], P["pit"] + 1.3, "block_dk", 24))
    main.append(K.arc("pit_ring_n", ci, 21.0, 24.0, -5.0, 185.0, P["pit"], P["pit"] + 1.3, "recess", 24))
    main.append(K.arc("pit_arc_w", ci, ri - 5.0, ri - 2.5, 120.0, 200.0, P["pit"], P["pit"] + 1.6, "stone", 16))      # the pale arc inside the west wall
    main.append(K.cyl("pit_hub", ci, 7.0, P["pit"], P["pit"] + 1.2, "burnt", 24))
    for i, p in enumerate(LIGHTS):
        main.append(K.cyl(f"light_socket{i}", p, 1.0, P["pit"], P["pit"] + 0.5, "recess", 8))
    # ---- on the wall: domes, blocks (positions read off the ruler sheet)
    p = (-34.5, 29.0)
    main.append(K.cyl("dome_w_drum", p, 11.0, 0.0, wh + 1.0, "wall", 24))
    main.append(K.dome("dome_w", p, 10.4, wh + 0.8, 9.0, "white", 24))
    main.append(K.house(K.arc("dome_w_crescent", p, 10.6, 12.2, 110.0, 250.0, wh - 1.0, wh + 1.6, "HOUSE", 10), 0.75))
    copper_dome(main, "dome_n", (-20.0, 49.0), 4.8, wh + 1.5)
    copper_dome(main, "dome_ne", (30.0, 45.0), 9.6, wh + 1.5)
    p = (4.0, 45.0)
    main.append(K.cyl("dome_c_drum", p, 9.0, 0.0, wh + 1.2, "wall", 20))
    main.append(K.dome("dome_c", p, 9.2, wh + 1.0, 8.0, "roof", 20))
    for i, q in enumerate(((26.0, 27.0), (31.5, 22.0))):
        main.append(K.obox(f"east_block{i}", q, 130.0, 4.8, 4.0, 0.0, wh + 2.0 - i, "white", 0.6, 0.9))
        main.append(K.house(K.obox(f"east_panel{i}", (q[0] - 2.4, q[1] - 2.8), 130.0, 3.2, 0.5, wh - 4.0 - i, wh + 0.5 - i, "HOUSE", 0.1), 0.8))
    main.append(K.obox("ne_block", (40.0, 36.0), 100.0, 8.0, 5.0, 0.0, wh + 3.0, "block_dk", 0.6, 0.9))
    main.append(K.house(K.obox("west_band", (-37.0, 15.0), 105.0, 4.0, 2.0, 0.0, 6.0, "HOUSE", 0.4), 0.45))
    # ---- the gate house at the south: a low rounded cream block with two copper domes under house-colour caps
    g = P["gate"]
    gh = P["gate_h"]
    main.append(K.cyl("gate_round", (g[0], g[1] + 4.5), 20.0, 0.0, gh, "roof", 32, 18.0))
    main.append(K.box("gate", g[0] - 19.0, g[0] + 19.0, g[1] + 2.0, g[1] + 10.0, 0.0, gh + 1.0, "roof2", 1.2, 0.92))
    main.append(K.cyl("gate_step", (g[0], g[1] - 2.0), 13.0, 0.0, 2.0, "stone", 28, 12.0))
    for i, x in enumerate((-8.0, 8.0)):
        q = (g[0] + x - 2.0, g[1] + 1.0)
        copper_dome(main, f"gate_dome{i}", q, 4.8, gh + 0.2)
        main.append(K.house(K.dome(f"gate_dome{i}_cap", q, 2.6, gh + 3.6, 2.2, "HOUSE", 12), 0.5))
    main.extend(K.vent("gate_vent", (g[0], g[1] - 14.0), 0.0, 3.0, 1.6, 4.0))
    main.extend(K.rocks("rock_s", (g[0] - 15.0, g[1] - 12.0), 5.0, 3, 5))
    # ---- fence arcs outside the pit, south-west and south-east
    for i, (a0, a1) in enumerate(((196.0, 234.0), (312.0, 353.0))):
        for j, rr in enumerate((ri + 8.0, ri + 11.5, ri + 15.0)):
            main.append(K.arc(f"fence{i}_{j}", ci, rr, rr + 2.4, a0 + 3.0 * j, a1 - 3.0 * j, 0.0, 2.0, "stone" if j % 2 == 0 else "stone_dk", 8, 0.2))
    c = ci
    rm = ri + 4.0
    if damaged:
        K.scorch(main, (c[0] - 6.0, c[1] + 4.0), 12.0, P["pit"] + 1.4, 3, 9)
        K.hole(main, [], K.polar(c, rm, 150.0), 5.5, wh + 0.4, 4, depth=2.0, panel=("stone", "roof2"), rib="steel_dk")
        K.hole(main, [], K.polar(c, rm, 60.0), 5.0, wh + 0.4, 5, depth=2.0, panel=("stone", "roof2"), rib="steel_dk")
        K.scorch(main, (4.0, 45.0), 5.0, wh + 7.8, 6, 4)
        K.wall_gash(main, (g[0] + 6.0, g[1] - 13.2), 0.0, 5.0, 1.0, 4.5, 7)
    glow(main)
    return dict(nodes=dict(main=main))


def dot_frames():
    grn, red, amb = (60, 230, 80), (240, 70, 50), (250, 200, 60)
    cols = [grn, grn, amb, grn, grn, red, grn, amb]
    out = []
    for fk, n in enumerate((1, 2, 3, 4, 6, 6, 6, 1)):
        out.append([[i, cols[i]] for i in range(n)])
    return out


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="dots", dots=[(x, y, P["pit"] + 0.6) for (x, y) in LIGHTS], dot_frames=dot_frames(), dot_radius=0.7,
                                flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09, sun_strength=4.0)), P=P)
