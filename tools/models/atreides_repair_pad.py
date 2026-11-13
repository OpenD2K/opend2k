"""Repair pad (art 9, shared by the three houses) - rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_repair_pad.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_REPAIR_PAD .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (a base-only art: frame 3120 = everything, drawn under units - the vehicle being repaired
stands on it; 3122 = damaged; animation 50 = 14 overlay frames, HAS_ANIMATION: guide lights on the floor running
inwards along the four diagonals, red, then yellow, then green at the centre; build-up 50 kept):
  * a round platform (r 43) of grey slabs with a cross of seams, a pale raised rim with a thin house-colour line;
  * four house-colour balls on posts round the rim - a large one on a drum at the north, small ones at the west,
    east and south-south-west;
  * a comb of copper pipes at the south foot; sandstone rubble at the south-west and south-east.
Animation: drawn by the pack driver as small lights at the model's light positions (spec "dots").
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_REPAIR_PAD"
P = dict(c=(0.0, -2.0), r=43.0, floor=2.6, rim=4.4, north=(4.0, 43.0), ball_w=165.0, ball_e=-16.0, ball_s=254.0)
ARMS = (45.0, 135.0, 225.0, 315.0)
RADII = (31.0, 25.0, 19.0, 13.0, 7.0)


def dots():
    c = P["c"]
    return [(c[0] + r * math.cos(math.radians(a)), c[1] + r * math.sin(math.radians(a)), P["floor"] + 0.4) for a in ARMS for r in RADII]


def dot_frames():
    red, yel, grn = (255, 60, 40), (255, 210, 50), (70, 255, 80)
    seq = [[], [(0, red)], [(0, red), (1, red)], [(1, red), (2, red)], [(1, yel), (2, yel)], [(2, yel), (3, yel)], [(3, yel), (4, yel)],
           [(3, grn), (4, grn)], [(4, grn), (3, grn)], [(4, grn), (3, grn)], [(4, grn)], [(4, grn)], [(4, (40, 150, 50))], []]
    return [[[arm * len(RADII) + i, col] for arm in range(len(ARMS)) for (i, col) in fr] for fr in seq]


def build(damaged=False):
    main = []
    c, r, fz, rz = P["c"], P["r"], P["floor"], P["rim"]
    main.append(K.cyl("foot", c, r + 1.6, 0.0, 1.6, "foot", 64))
    main.append(K.cyl("floor", c, r - 2.0, 0.0, fz, "slab_dk", 64))
    main.append(K.lathe_loop("rim", c, [(r + 0.8, 0.0), (r + 0.4, rz - 0.4), (r - 0.2, rz), (r - 3.0, rz), (r - 3.6, rz - 0.5), (r - 3.6, 0.0)], "stone", 64))
    main.append(K.house(K.lathe_loop("rim_line", c, [(r - 0.9, rz - 0.1), (r - 0.9, rz + 0.25), (r - 2.0, rz + 0.25), (r - 2.0, rz - 0.1)], "HOUSE", 64), 0.75))
    # floor plates: rings and a cross of seams, darker plates in alternate sectors
    for i, rr in enumerate((12.0, 26.0)):
        main.append(K.lathe_loop(f"ring{i}", c, [(rr + 0.3, fz - 0.2), (rr + 0.3, fz + 0.08), (rr - 0.3, fz + 0.08), (rr - 0.3, fz - 0.2)], "recess", 48))
    for i in range(8):
        a = 22.5 + 45.0 * i
        main.append(K.seam(f"seam{i}", K.polar(c, 3.0, a, fz + 0.02), K.polar(c, r - 3.8, a, fz + 0.02), 0.5, 0.3))
        if i % 2 == 0:
            main.append(K.arc(f"plate{i}", c, 12.6, 25.6, a + 2.0, a + 43.0, fz - 0.3, fz + 0.06, "slab", 6))
    main.append(K.cyl("hub", c, 3.2, fz - 0.2, fz + 0.3, "steel_dk", 16))
    for a in ARMS:                                       # the guide lights' sockets
        for rr in RADII:
            main.append(K.cyl(f"socket{int(a)}_{int(rr)}", K.polar(c, rr, a), 0.9, fz - 0.1, fz + 0.2, "recess", 8))
    # the balls on their posts
    n = P["north"]
    main.append(K.cyl("north_drum", n, 8.0, 0.0, 10.0, "stone_dk", 24, 7.2))
    main.append(K.cyl("north_cap", n, 6.4, 9.6, 11.0, "wall", 24))
    main.append(K.house(K.ball("north_ball", (n[0], n[1], 13.0), 5.6, "HOUSE", 16), 0.55))
    main.append(K.box("north_arm", n[0] + 4.0, n[0] + 11.0, n[1] - 6.0, n[1] - 2.0, 0.0, 7.0, "wall", 0.4))
    for i, a in enumerate((P["ball_w"], P["ball_e"], P["ball_s"])):
        p = K.polar(c, r + 0.5, a)
        main.append(K.cyl(f"post{i}", p, 2.8, 0.0, rz + 1.0, "stone_dk", 12))
        main.append(K.house(K.ball(f"ball{i}", (p[0], p[1], rz + 3.2), 3.3, "HOUSE", 14), 0.55))
    # the comb of copper pipes at the south foot, rubble
    for i in range(5):
        x = c[0] - 7.0 + 4.0 * i
        y0 = c[1] - math.sqrt(max(1.0, r * r - (x - c[0]) ** 2)) + 1.0
        main.extend(K.pipe(f"comb{i}", [(x, y0, 2.4), (x, y0 - 12.0, 2.4), (x, y0 - 17.0, 0.4)], 1.2, "copper" if i % 2 else "copper_dk"))
        main.extend(K.lamp(f"comb_lamp{i}", (x, y0 - 16.0, 1.6), 0.6, "lamp"))
    main.extend(K.rocks("rock_sw", (-36.0, -40.0), 8.0, 3, 8))
    main.extend(K.rocks("rock_se", (35.0, -38.0), 4.5, 5, 4))
    if damaged:
        K.scorch(main, (c[0] - 12.0, c[1] + 8.0), 10.0, fz + 0.1, 3, 8)
        K.scorch(main, (c[0] + 14.0, c[1] - 12.0), 8.0, fz + 0.1, 4, 7)
        K.hole(main, [], (c[0] + 6.0, c[1] + 18.0), 6.0, fz + 0.1, 5, depth=2.0, panel=("slab", "slab_dk"), rib="steel_dk")
        K.hole(main, [], (c[0] - 20.0, c[1] - 14.0), 4.5, fz + 0.1, 6, depth=2.0, panel=("slab", "slab_dk"), rib="steel_dk")
        K.wall_gash(main, K.polar(c, r + 0.9, 250.0), 160.0, 7.0, 0.5, rz, 7, 0.8)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(glb_names=['REPAIR_PAD'], anim="dots", dots=dots(), dot_frames=dot_frames(), dot_radius=0.8,
                                flags=dict(shadow_azimuth=30.0, fill=(0.3, 90.0, 35.0), sky_strength=0.09, sun_strength=3.5)), P=P)
