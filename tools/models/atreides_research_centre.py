"""Research centre (art 14, shared by Atreides and Harkonnen) - rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_research_centre.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_RESEARCH_CENTRE .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3320 main, 3321 damaged, no base; animation 53 = 20 overlay frames, HAS_ANIMATION:
thin blue glowing rims under three of the pods, pulsing; build-up 53 kept):
  * an organic cluster, like mushrooms on a root: ten round grey caps of different sizes on dark stalks - the
    biggest on top of a dark trunk, a second in front of it, a third on the body's front, the others round the
    foot - growing from a dark lumpy root mass;
  * thick pale roots running from the body out to the west, south-west, south and south-east caps;
  * house colour: collars under some caps, a band on the trunk, pale bands on three roots.
Animation: the cyan rims under three pods brighten and dim (overlay frames made from the idle render).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_RESEARCH_CENTRE"
P = dict(
    off=(0.0, -2.0), body=(2.0, 18.0), body_r=31.0, trunk=(13.0, 40.0), sun_el=58.0, sh_az=20.0, scale=0.95, cap=1.02, zs=0.87, body_h=11.0,
)
# The caps ("mushrooms"): (x, y, z of the centre, radius, flatten, house collar, glow rim), read off the ruler sheet
PODS = [
    (9.5, 38.0, 54.0, 16.0, 0.9, True, False),       # the big one on top of the trunk
    (0.5, 34.0, 36.0, 11.5, 0.9, False, True),      # in front of it
    (10.0, 20.0, 18.0, 9.5, 0.9, False, True),       # on the body's front
    (-34.0, 36.0, 19.0, 5.5, 0.9, False, False),     # west, on a tall thin stalk
    (-39.5, 3.0, 6.0, 7.5, 0.85, False, False),      # far west, low
    (-20.0, -9.0, 5.0, 5.5, 0.85, True, True),       # south-west, small
    (-8.0, -25.5, 8.0, 6.5, 0.85, False, False),     # south
    (10.0, -18.0, 3.0, 6.5, 0.5, True, False),       # south, a flat dish
    (28.5, -5.5, 7.0, 8.5, 0.85, False, False),      # south-east
    (33.0, 31.0, 10.0, 8.5, 0.9, False, False),      # east, in the trunk's shade
]
# the pale roots that run from the body out to the caps: points (x, y, z), radius, material
ROOTS = [
    ([(-2.0, 24.0, 18.0), (-16.0, 19.0, 9.0), (-28.0, 20.0, 4.0)], 4.2, "wall"),       # the broad pale west root ...
    ([(-3.0, 24.0, 15.0), (-18.0, 17.0, 7.0), (-32.0, 11.0, 3.0)], 4.4, "white"),       # ... (two strands side by side)
    ([(-4.0, 16.0, 11.0), (-5.0, 3.0, 5.0), (-6.0, -12.0, 3.0)], 3.0, "stone"),         # the pale strand running south
    ([(9.0, 12.0, 9.0), (18.0, 4.0, 6.0), (25.0, 0.0, 4.0)], 3.4, "stone_dk"),          # darker ones to the south-east / east
    ([(16.0, 24.0, 9.0), (24.0, 27.0, 6.0)], 2.6, "block_dk"),
    ([(4.0, 4.0, 4.0), (9.0, -10.0, 3.0)], 2.4, "block_dk"),
]


def pod(main, i, x, y, z, r, fl, collar, glow, damaged):
    """One cap: a slightly flattened ball, paler on top, on a dark stalk."""
    seg = 10
    prof = [(max(0.02, r * math.sin(math.pi * k / seg)), z - r * fl * math.cos(math.pi * k / seg)) for k in range(seg + 1)]
    o = K.lathe(f"pod{i}", (x, y), prof, "cap", 28)
    main.append(o)
    # the paler crown (upper third), a seam belt under it
    crown = [(r * math.sin(math.pi * k / 20) + 0.12, z + r * fl * math.cos(math.pi * k / 20) + 0.12) for k in range(8, -1, -1)]
    crown = [(max(0.02, a), b) for (a, b) in crown]
    main.append(K.lathe(f"pod{i}_crown", (x, y), crown, "steel_dk", 28))
    # the warm highlight where the sun strikes (west-south-west, high): a low blister
    main.append(K.ball(f"pod{i}_hi", (x - 0.44 * r, y - 0.24 * r, z + 0.62 * r * fl), r * 0.36, "cap_hi", 12))
    zb = z - r * fl
    main.append(K.cyl(f"pod{i}_stalk", (x, y), r * 0.45, 0.0, zb + r * 0.3, "tank", 14, r * 0.3))
    main.append(K.cyl(f"pod{i}_foot", (x, y), r * 0.6, 0.0, 1.4, "foot", 14))
    main.append(K.lathe_loop(f"pod{i}_belt", (x, y), [(r + 0.2, z - 0.8), (r + 0.2, z - 0.1), (r * 0.985, z + 0.2), (r * 0.985, z - 1.0)], "block_dk", 28))
    if collar or i in (1, 4, 8):
        main.append(K.house(K.lathe_loop(f"pod{i}_collar", (x, y), [(r * 0.92, zb + r * 0.36), (r * 1.02, zb + r * 0.8), (r * 0.8, zb + r * 0.12), (r * 0.6, zb - 0.1)], "HOUSE", 24), 0.45))
    if glow:
        main.append(K.lathe_loop(f"pod{i}_glow", (x, y), [(r * 0.97, z - r * fl * 0.42), (r * 0.99, z - r * fl * 0.32), (r * 0.93, z - r * fl * 0.5), (r * 0.9, z - r * fl * 0.56)], "glow", 24))
    if damaged and i in (0, 2, 4, 8):
        def zf(px, py):
            d2 = ((px - x) ** 2 + (py - y) ** 2) / (r * r)
            return z + r * fl * math.sqrt(max(0.05, 1 - d2))
        K.hole(main, [o], (x - r * 0.25, y - r * 0.2), r * 0.45, zf, 30 + i, depth=r * 0.5, panel=("slab_dk", "slab"), rib="steel")


def build(damaged=False):
    main = []
    ox, oy = P["off"]
    bc, br = (P["body"][0] + ox, P["body"][1] + oy), P["body_r"]
    t = (P["trunk"][0] + ox, P["trunk"][1] + oy)
    bh = P["body_h"]

    def mound(x, y):                                     # height of the root mass at (x, y)
        d = math.hypot(x - bc[0], y - bc[1]) / br
        return bh * (1.0 - min(1.0, d) ** 1.5) if d < 1.0 else 0.0
    # the dark root mass everything grows from: a lumpy mound (a main dome and three smaller ones), the trunk
    main.append(K.lathe("body", bc, [(br * (1.0 - (k / 8.0) ** 1.5) ** (1 / 1.5) if k < 8 else 0.02, bh * k / 8.0) for k in range(9)], "tank", 36))
    main.append(K.cyl("body_foot", bc, br + 1.0, 0.0, 1.6, "foot", 36))
    for i, (dx, dy, r, h) in enumerate(((18.0, 12.0, 13.0, 15.0), (-6.0, 20.0, 12.0, 17.0), (14.0, -4.0, 10.0, 11.0))):
        main.append(K.dome(f"lump{i}", (bc[0] + dx, bc[1] + dy), r, 0.0, h, "block_dk" if i % 2 else "tank", 20))
    main.append(K.lathe("trunk", t, [(11.0, 8.0), (8.5, 22.0), (7.0, 36.0), (8.0, 44.0)], "tank", 24))
    # the dark cone the caps sit on: from the mound up to the trunk
    main.append(K.lathe("cone", ((bc[0] + t[0]) / 2, (bc[1] + t[1]) / 2), [(br * 0.74, 0.0), (br * 0.64, 8.0), (br * 0.5, 18.0), (br * 0.34, 28.0), (6.0, 36.0)], "tank", 32))
    for k in range(6):
        a = 200.0 + 28.0 * k
        main.append(K.beam(f"trunk_fin{k}", K.polar(t, 10.6, a, 10.0), K.polar(t, 7.4, a, 38.0), 1.4, 1.0, "block_dk", 0.2))
    main.append(K.house(K.lathe_loop("trunk_band", t, [(8.2, 30.0), (8.4, 30.4), (7.8, 34.4), (7.6, 34.0)], "HOUSE", 24), 0.3))
    def place(x, y, z):                                  # the cluster scaled about the body centre
        return (P["body"][0] + (x - P["body"][0]) * P["scale"] + ox, P["body"][1] + (y - P["body"][1]) * P["scale"] + oy, z * P["zs"])
    for i, pd in enumerate(PODS):
        x, y, z = place(*pd[:3])
        pod(main, i, x, y, z, pd[3] * P["cap"], *pd[4:], damaged)
    for i, (pts, r, mt) in enumerate(ROOTS):
        pts = [place(*q) for q in pts]
        pts = [(x, y, max(z, mound(x, y) + r * 0.55)) for (x, y, z) in pts]      # the roots lie on the mound
        for j, (a, b) in enumerate(zip(pts[:-1], pts[1:])):
            main.append(K.tube(f"root{i}_{j}", a, b, r, mt, 12, r * 0.86))
            main.append(K.ball(f"root{i}_k{j}", a, r * 1.02, mt, 12))
        main.append(K.ball(f"root{i}_end", pts[-1], r * 0.9, mt, 12))
    main.append(K.house(K.sweep("root_band0", place(20.0, 0.0, 4.6), place(25.0, -4.0, 3.8), [(0.0, 3.7), (1.0, 3.6)], "HOUSE", 12), 0.85))
    main.append(K.house(K.sweep("root_band1", place(-21.0, 21.5, 7.0), place(-25.0, 22.0, 5.6), [(0.0, 3.8), (1.0, 3.7)], "HOUSE", 12), 0.8))
    main.append(K.house(K.sweep("root_band2", place(-6.0, 0.0, 4.6), place(-6.6, -6.0, 3.8), [(0.0, 3.6), (1.0, 3.5)], "HOUSE", 12), 0.6))
    main.extend(K.rocks("rock_w", (-27.0 + ox, 20.0 + oy), 4.0, 3, 4))
    main.extend(K.rocks("rock_se", (24.0 + ox, -15.0 + oy), 3.4, 5, 3))
    if damaged:
        K.scorch(main, (bc[0] - 6.0, bc[1] - 6.0), 7.0, 12.0, 50, 6)
        K.wall_gash(main, K.polar(t, 9.0, 250.0), 160.0, 4.0, 14.0, 28.0, 51, 0.8)
    return dict(nodes=dict(main=main))


def glows():
    out = []
    for i, (x, y, z, r, fl, _c, g) in enumerate(PODS):
        x = P["body"][0] + (x - P["body"][0]) * P["scale"] + P["off"][0]
        y = P["body"][1] + (y - P["body"][1]) * P["scale"] + P["off"][1]
        z, r = z * P["zs"], r * P["cap"]
        if g:
            out.append(dict(x0=x - r - 1.5, x1=x + r + 1.5, v0=y + z - r - 2.0, v1=y + z + 1.0, waves=0.0, phase=0.3 * i, lo=0.35, hi=1.6, min_diff=30))
    return out


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(glb_names=["ATREIDES_RESEARCH_CENTRE", "HARKONNEN_RESEARCH_CENTRE"], anim="glow", glow=glows(),
                                        flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.45, 90.0, 35.0), sky_strength=0.12, sun_strength=6.2)), P=P)
