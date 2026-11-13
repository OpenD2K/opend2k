"""Atreides spice silo - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_silo.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_SILO .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (art 8: frames 3112..3115 = the four fill levels the game picks by the stored spice, 3116
= damaged (one picture), no base, no animation; build-up 38 = the small rock mound shared with the turrets, kept):
  * a low tan dome on a drum: a white foot ring, above it a dark recessed belt with piers; three house-colour
    patches on the dome (north-west, east, front); in the belt's front-left bays the spice level shows: empty (dark), then an
    orange fill rising in three steps;
  * a small copper valve at the south-east foot.
Poses: the node "fill" (the spice behind the window) scaled up in three steps -> frames 3113..3115.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_SILO"
P = dict(c=(0.0, -6.0), r=14.2, ring_h=9.0, h=7.5, win0=195.0, win1=300.0, win_z0=4.2, win_z1=8.2)


def build(damaged=False):
    main, fill = [], []
    c, r = P["c"], P["r"]
    main.append(K.cyl("ring", c, r + 2.0, 0.0, 4.4, "white", 40, r + 0.8))
    main.append(K.cyl("belt", c, r - 0.4, 3.8, P["ring_h"], "tank", 40))
    main.append(K.cyl("eave", c, r + 0.5, P["ring_h"] - 0.9, P["ring_h"] + 0.1, "wall", 40))
    for k in range(12):
        a = 15.0 + 30.0 * k
        main.append(K.beam(f"pier{k}", K.polar(c, r + 0.1, a, 3.9), K.polar(c, r + 0.1, a, P["ring_h"] - 0.8), 1.0, 0.8, "stone_dk"))
    main.append(K.cyl("foot", c, r + 1.4, 0.0, 1.0, "foot", 40))
    dome = K.dome("dome", c, r, P["ring_h"] - 0.2, P["h"], "stone", 40, 8)
    main.append(dome)

    def zf(x, y):
        return P["ring_h"] + P["h"] * math.sqrt(max(0.03, 1 - ((x - c[0]) ** 2 + (y - c[1]) ** 2) / (r * r)))
    # the window belt across the front: dark recess, frame, mullions
        main.append(K.arc("win_sill", c, r - 1.0, r + 0.9, P["win0"] - 2.0, P["win1"] + 2.0, P["win_z0"] - 0.9, P["win_z0"], "wall", 16))
    main.append(K.arc("win_head", c, r - 2.6, r + 0.7, P["win0"] - 2.0, P["win1"] + 2.0, P["win_z1"], P["win_z1"] + 0.9, "wall", 16))
    for i in range(4):
        a = P["win0"] + (P["win1"] - P["win0"]) * (i + 0.5) / 4 + (P["win1"] - P["win0"]) / 8
        if i < 3:
            main.append(K.beam(f"mullion{i}", K.polar(c, r + 0.4, a, P["win_z0"]), K.polar(c, r + 0.4, a, P["win_z1"]), 0.9, 0.7, "wall"))
    # the spice behind the window (node "fill": origin at the sill, scaled in z by the poses)
    fill.append(K.arc("spice", c, r - 2.2, r + 0.1, P["win0"] + 1.0, P["win1"] - 1.0, P["win_z0"], P["win_z1"] - 0.2, "orange", 16))
    # house colour patches
    for i, (a0, a1, t0, t1, k) in enumerate(((112.0, 150.0, 0.55, 0.82, 0.55), (-12.0, 30.0, 0.5, 0.85, 0.32), (258.0, 284.0, 0.62, 0.88, 0.6))):
        pts_o = [K.polar(c, r * t1, a0 + (a1 - a0) * j / 6) for j in range(7)]
        pts_i = [K.polar(c, r * t0, a1 + (a0 - a1) * j / 6) for j in range(7)]
        import bmesh
        bm = bmesh.new()
        ring = pts_o + pts_i
        K.rings_solid(bm, [[(x, y, zf(x, y) - 0.4) for (x, y) in ring], [(x, y, zf(x, y) + 0.3) for (x, y) in ring]])
        main.append(K.house(K.new_obj(f"patch{i}", bm, "HOUSE"), k))
    for i in range(6):                                  # seams from the crown
        a = 30.0 + 60.0 * i
        pts = [K.polar(c, r * t, a) for t in (0.2, 0.45, 0.7, 0.9)]
        for j in range(3):
            main.append(K.seam(f"seam{i}_{j}", (*pts[j], zf(*pts[j]) + 0.05), (*pts[j + 1], zf(*pts[j + 1]) + 0.05), 0.25, 0.25))
    main.append(K.cyl("crown", c, 2.6, P["ring_h"] + P["h"] - 0.6, P["ring_h"] + P["h"] + 0.7, "rib", 14))
    main.extend(K.pipe("valve", [(*K.polar(c, r + 0.5, 318.0), 2.2), (*K.polar(c, r + 3.0, 318.0), 2.2), (*K.polar(c, r + 3.0, 318.0), 0.3)], 1.1, "copper"))
    main.extend(K.lamp("lamp", (*K.polar(c, r - 1.0, 245.0), P["win_z1"] + 1.3), 0.5))
    if damaged:
        K.hole(main, [dome], (c[0] - 1.0, c[1] + 3.0), 5.5, zf, 3, depth=5.0)
        K.hole(main, [dome], (c[0] + 7.0, c[1] - 4.0), 3.0, zf, 5, depth=4.0)
        K.scorch(main, (c[0] - 6.0, c[1] - 5.0), 4.0, zf, 7, 4)
    return dict(nodes=dict(main=main, fill=fill), origins=dict(fill=(c[0], c[1], P["win_z0"])))


def level(k):
    def pose(nodes):
        nodes["fill"].scale = (1.0, 1.0, k)
    return pose


if __name__ == "__main__":
    K.run(KEY, build, poses=[level(0.36), level(0.68), level(1.0)], rest=level(0.001),
          spec=dict(levels=[-1, 0, 1, 2], flags=dict(shadow_azimuth=25.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
