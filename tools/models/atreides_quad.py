"""Quad - the ORIGINAL design rebuilt as a modern hard-surface model (kit: tools/models/atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_quad.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=quad .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin QUAD: owner mask 127 (all houses), unit art 8 = DATA.R16 frames 1902-1933, no barrel art; key QUAD.

How the original reads (visual hull of the 32 silhouettes + the cardinal views, tools/models/vehicle_check):
  * four fat near-black wheels in a square (u -5.8 / +4.6, 7.3 px off the centre line, radius 4), each under a
    bright house-colour mudguard - the house colour is a fifth of the sprite and its lightest part;
  * between them a narrow cream tub (7 px wide, 14 long): cream side rails, a brown floor, a cyan light in the
    middle, dark axles to the wheels;
  * two black gun tubes side by side over the tub's front, reaching 3 px past the front wheels.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.5, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.26), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "QUAD"

P = dict(
    v0=0.0,
    wheel=dict(uf=4.5, ur=-5.9, v=7.5, r=3.4, w=4.8),
    guard=dict(du=(-2.2, 2.2), dv=(-2.0, 1.7), z=(5.7, 7.6)),
    tub=dict(v=3.5, u=(-7.1, 6.2), z=(2.0, 7.2), frame=4.2),
    floor=dict(v=2.6, u=(-5.4, 4.4), z=6.3),
    gun=dict(v=2.0, u=(0.6, 10.6), z=7.5, r=0.6),
)
COL = dict(brown=(0.540, 0.410, 0.320), gunblack=(0.080, 0.075, 0.075))
SPEC = dict(brown=(0.0, 0.6, "paint"), gunblack=(0.4, 0.5, "dark"))
FILM = dict(brown=(0.480, 0.370, 0.290))
K.palette(COL, SPEC, FILM, uv_scale=dict(gunblack=0.4))


def build():
    root = K.root(KEY)
    parts = []
    wh, gd, tb, fl, gn = (P[k] for k in ("wheel", "guard", "tub", "floor", "gun"))
    for u in (wh["uf"], wh["ur"]):
        K.wheel(parts, u, wh["v"], wh["r"], wh["w"], lugs=10)
        parts.append(K.cyl("axle", 0.9, 0.0, wh["v"], u, wh["r"], "dark", segs=10, axis="v"))
        parts.append(K.box("arm", tb["v"] - 0.3, wh["v"] - wh["w"] / 2 + 0.3, u - 1.1, u + 1.1, wh["r"] + 0.4, gd["z"][0] + 0.3, "dark", 0.15))
        parts.append(K.house(K.pod("guard", wh["v"] + gd["dv"][0], wh["v"] + gd["dv"][1], u + gd["du"][0], u + gd["du"][1], gd["z"][0], gd["z"][1],
                                   "HOUSE", chamfer=0.9, crown=0.45, bev=0.25), 1.0, 0.62))
    # cream side rail of the tub
    parts.append(K.pod("rail", fl["v"], tb["v"], tb["u"][0], tb["u"][1], tb["z"][0], tb["z"][1], "cream", chamfer=0.6, crown=0.3))
    parts.append(K.box("frame", tb["v"] - 0.2, tb["frame"], tb["u"][0] + 0.8, tb["u"][1] - 0.8, tb["z"][0], tb["z"][1] - 1.3, "dark", 0.2))
    parts.append(K.tube("gun", (gn["v"], gn["u"][0], gn["z"]), (gn["v"], gn["u"][1], gn["z"]), gn["r"], "gunblack", segs=10))
    parts.append(K.tube("gun_sleeve", (gn["v"], gn["u"][0] - 0.4, gn["z"]), (gn["v"], gn["u"][0] + 1.8, gn["z"]), gn["r"] + 0.25, "gunblack", segs=10))
    K.mirror_all(parts)
    u0, u1 = tb["u"]
    parts.append(K.box("tub", -fl["v"] - 0.1, fl["v"] + 0.1, u0 + 0.3, u1 - 0.3, tb["z"][0] - 0.5, fl["z"], "brown", 0.2))
    parts.append(K.pod("nose", -tb["v"] + 0.3, tb["v"] - 0.3, u1 - 1.9, u1 + 0.3, tb["z"][0] + 0.2, tb["z"][1] - 0.5, "cream", chamfer=0.7, crown=0.3))
    parts.append(K.pod("tail", -tb["v"] + 0.3, tb["v"] - 0.3, u0 - 0.3, u0 + 2.2, tb["z"][0] + 0.2, tb["z"][1] - 0.2, "cream", chamfer=0.7, crown=0.3))
    parts.append(K.box("engine", -1.7, 1.7, u0 + 2.4, u0 + 4.6, fl["z"] - 0.1, fl["z"] + 1.3, "steel_dk", 0.25))
    parts.append(K.box("light_base", -1.3, 1.3, -1.6, 1.4, fl["z"] - 0.1, fl["z"] + 0.9, "steel_dk", 0.2))
    parts.append(K.box("light", -0.75, 0.75, -1.0, 0.8, fl["z"] + 0.8, fl["z"] + 1.25, "cyan", 0.1))
    parts.append(K.box("gun_bridge", -gn["v"] - 0.8, gn["v"] + 0.8, gn["u"][0] - 0.2, gn["u"][0] + 1.3, fl["z"], gn["z"] + 0.2, "steel_dk", 0.2))
    for su in (-3.2, 2.6):
        parts.append(K.seam("rib", -tb["v"] - 0.02, tb["v"] + 0.02, su, su + 0.3, tb["z"][1] + 0.02, 0.5))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    for i, sg in enumerate((1, -1)):
        K.anchor(f"fx_muzzle_{i}", (sg * gn["v"] + P["v0"], gn["u"][1], gn["z"]), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "quad", build, P)
