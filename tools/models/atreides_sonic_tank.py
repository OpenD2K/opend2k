"""Sonic tank - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_sonic_tank.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=sonic_tank .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin SONIC TANK: owner mask 1 (Atreides only), unit art 13 = DATA.R16 frames 2062-2093, no barrel art
(the emitter is part of the body frames, it never turns on its own); model key SONIC_TANK.

How the original reads (the cardinal views, tools/models/vehicle_check):
  * a low tracked carrier, 24 x 17 px: cream pods over dark tracks, a brown-grey deck, pale lamps on the corners;
  * a house-colour dome in the middle of the deck (9 % of the sprite);
  * the sonic emitter stands tall over it: an upright dish plate across the vehicle (cyan and glowing on its
    front face, dark purple on its back, 20 px above the ground), on a dark purple mount, and a long pale lilac
    resonator tube through its middle, from the rear of the deck to 12 px ahead of the centre.
The original frames carry no ground shadow; the pack renders one.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.36, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.6, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.13), (0.25, 0.27), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "SONIC_TANK"

P = dict(
    v0=0.0,
    track=dict(v=(4.8, 8.3), u=(-13.6, 12.4), z=5.0),
    pod=dict(v=(4.5, 8.6), u=(-12.6, 11.8), z=(4.4, 6.8)),
    hull=dict(v=4.8, u=(-12.2, 11.2), z=(1.6, 5.8)),
    dome=dict(u=-2.6, r=(4.4, 4.8, 3.0), z=6.0),
    dish=dict(u=4.2, v=4.7, z=(11.0, 26.0), t=1.0, bulge=1.2),
    tube=dict(u=(-8.4, 15.0), z=18.2, r=1.15),
    mount=dict(v=2.2, u=(1.4, 6.2), z=(5.6, 12.6)),
)
COL = dict(purple=(0.430, 0.330, 0.560), purple_dk=(0.270, 0.210, 0.340), cyan=(0.300, 0.980, 1.000), lilac=(0.860, 0.850, 0.960), brown=(0.540, 0.480, 0.420),
           lamp=(0.700, 0.800, 0.900))
SPEC = dict(purple=(0.2, 0.4, "paint"), purple_dk=(0.2, 0.5, "dark"), lilac=(0.3, 0.25, "steel"), brown=(0.0, 0.6, "paint"), lamp=(0.0, 0.3, "dark"))
FILM = dict(purple=(0.400, 0.320, 0.500), lilac=(0.760, 0.750, 0.840), brown=(0.440, 0.390, 0.340))
K.palette(COL, SPEC, FILM, uv_scale=dict(lamp=0.2))


def build():
    root = K.root(KEY)
    parts = []
    tr, pd, hl, dm, ds, tb, mt = (P[k] for k in ("track", "pod", "hull", "dome", "dish", "tube", "mount"))
    u0, u1 = tr["u"]
    prof = [(u0 + 1.6, 0.0), (u1 - 1.6, 0.0), (u1 - 0.2, 1.5), (u1, 2.6), (u1 - 0.9, tr["z"]), (u0 + 0.9, tr["z"]), (u0, 2.6), (u0 + 0.2, 1.5)]
    K.track(parts, prof, tr["v"][0], tr["v"][1], wheels=((u0 + 2.0, 2.3, 1.7), (u1 - 2.0, 2.3, 1.7)))
    p0, p1 = pd["u"]
    um = (p0 + p1) / 2
    parts.append(K.pod("pod_f", pd["v"][0], pd["v"][1], um + 0.2, p1, pd["z"][0], pd["z"][1], "cream", chamfer=0.9, crown=0.4))
    parts.append(K.pod("pod_r", pd["v"][0], pd["v"][1], p0, um - 0.2, pd["z"][0], pd["z"][1], "cream", chamfer=0.9, crown=0.4))
    for lu in (p0 - 0.1, p1 - 0.9):
        parts.append(K.box("lamp", pd["v"][0] + 0.9, pd["v"][1] - 0.9, lu, lu + 1.0, pd["z"][0] + 0.5, pd["z"][1] + 0.1, "lamp", 0.1))
    parts.append(K.box("strut", 2.2, 3.0, ds["u"] - 2.4, ds["u"] - 0.2, hl["z"][1], ds["z"][0] + 2.0, "purple_dk", 0.15))
    K.mirror_all(parts)
    parts.append(K.box("hull", -hl["v"], hl["v"], hl["u"][0], hl["u"][1], hl["z"][0], hl["z"][1], "brown", 0.4))
    parts.append(K.box("deck_f", -3.6, 3.6, 5.6, hl["u"][1] - 0.5, hl["z"][1] - 0.2, hl["z"][1] + 0.5, "mid", 0.2))
    parts.append(K.box("deck_r", -3.8, 3.8, hl["u"][0] + 0.5, -6.0, hl["z"][1] - 0.2, hl["z"][1] + 0.6, "mid", 0.2))
    parts.append(K.house(K.ellipsoid("dome", (0, dm["u"], dm["z"]), dm["r"], "HOUSE", segs=20, rings=10), 0.62, 0.5))
    parts.append(K.tube_z("dome_ring", 4.9, 0.0, dm["u"], hl["z"][1] - 0.1, hl["z"][1] + 0.45, "purple_dk", segs=20))
    # the emitter: mount, dish plate (cyan front, purple back), resonator tube
    parts.append(K.box("mount", -mt["v"], mt["v"], mt["u"][0], mt["u"][1], mt["z"][0], mt["z"][1], "purple_dk", 0.4))
    z0, z1 = ds["z"]
    n = 8
    back, front = [], []
    rows = 6
    rings_b, rings_f = [], []
    for j in range(rows + 1):
        zz = z0 + (z1 - z0) * j / rows
        kz = 1.0 - (2.0 * j / rows - 1.0) ** 2
        rb, rf = [], []
        for i in range(n + 1):
            vv = -ds["v"] + 2 * ds["v"] * i / n
            kv = 1.0 - (vv / ds["v"]) ** 2
            off = -ds["bulge"] * kv * (0.4 + 0.6 * kz)             # the dish is hollow towards the front
            rb.append((vv, ds["u"] - ds["t"] + off, zz))
            rf.append((vv, ds["u"] + off, zz))
        rings_b.append(rb)
        rings_f.append(rf)
    import bmesh
    for name, rr, material, flip in (("dish_back", rings_b, "purple", True), ("dish_face", rings_f, "cyan", False)):
        bm = bmesh.new()
        vs = [[bm.verts.new(p) for p in r] for r in rr]
        for a, b in zip(vs, vs[1:]):
            for i in range(n):
                f = [a[i], a[i + 1], b[i + 1], b[i]]
                bm.faces.new(f[::-1] if flip else f)
        parts.append(K.new_obj(name, bm, material))
    rim = [rings_b[0][i] for i in range(n + 1)] + [r[-1] for r in rings_b[1:]] + [rings_b[-1][i] for i in range(n - 1, -1, -1)] + [r[0] for r in rings_b[-2:0:-1]]
    rimf = [(v, u + ds["t"], z) for (v, u, z) in rim]
    bm = bmesh.new()
    va, vb = [bm.verts.new(p) for p in rim], [bm.verts.new(p) for p in rimf]
    m = len(rim)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new([va[i], va[j], vb[j], vb[i]])
    parts.append(K.new_obj("dish_rim", bm, "purple_dk"))
    parts.append(K.box("dish_bar", -ds["v"] - 0.15, ds["v"] + 0.15, ds["u"] - ds["t"] - ds["bulge"] * 0.5 - 0.3, ds["u"] - ds["t"] - ds["bulge"] * 0.5 + 0.2,
                       tb["z"] - 0.5, tb["z"] + 0.5, "purple_dk"))
    t0, t1 = tb["u"]
    parts.append(K.cyl("tube", tb["r"], t0, t1, 0, tb["z"], "lilac", segs=16))
    parts.append(K.cyl("tube_tip", tb["r"] + 0.35, t1 - 1.6, t1, 0, tb["z"], "lilac", segs=16, bev=0.1))
    parts.append(K.cyl("tube_bore", tb["r"] * 0.7, t1 - 0.3, t1 + 0.02, 0, tb["z"], "recess", segs=10))
    parts.append(K.cyl("tube_end", tb["r"] + 0.5, t0 - 0.2, t0 + 1.8, 0, tb["z"], "purple", segs=16, bev=0.1))
    parts.append(K.box("tube_post", -0.8, 0.8, t0 + 0.2, t0 + 1.6, hl["z"][1], tb["z"], "purple_dk", 0.15))
    for cu in (-3.0, 7.0):
        parts.append(K.cyl("tube_ring", tb["r"] + 0.25, cu, cu + 0.7, 0, tb["z"], "purple", segs=16))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    K.anchor("fx_muzzle_0", (P["v0"], t1, tb["z"]), body)
    K.anchor("fx_emitter_0", (P["v0"], ds["u"], (z0 + z1) / 2), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "sonic_tank", build, P)
