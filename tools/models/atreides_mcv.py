"""MCV (mobile construction vehicle) - the ORIGINAL design rebuilt as a modern hard-surface model (kit:
atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_mcv.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=mcv .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin MCV: owner mask 127 (all houses), unit art 12 = DATA.R16 frames 2030-2061, no barrel art; key MCV.
Deploying has no art of its own (the unit is replaced by the construction yard's build-up), so no pose variants.

How the original reads (visual hull of the 32 silhouettes + the cardinal views, tools/models/vehicle_check):
  * a long open truck frame, 44 px from bumper to nose: a slim peach spine with an axle of two double wheels near
    each end (u -17 and +16, 19 px across), a peach bumper bar at the rear;
  * amidships a brown machinery block and, hung on its RIGHT side, a big house-colour container (13 % of the
    sprite) - the vehicle is not symmetric;
  * a grey steel boom with yellow hazard marks rising from the block to a peach cab over the front axle, the cab's
    narrow nose reaching 6 px past the front wheels.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.27, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.13), (0.25, 0.28), (0.5, 0.54), (0.75, 0.80), (1.0, 1.0)))
KEY = "MCV"

P = dict(
    v0=0.0,
    axle=dict(uf=15.2, ur=-17.4, v=(4.5, 7.7), r=3.9, w=3.0),
    spine=dict(v=1.5, u=(-21.0, 18.0), z=(3.0, 5.8)),
    bumper=dict(v=6.6, u=(-22.9, -21.2), z=(2.2, 4.8)),
    block=dict(v=(-4.2, 2.4), u=(-9.0, 5.0), z=(3.0, 8.6)),
    box=dict(v=(2.3, 8.8), u=(-9.8, 2.8), z=(3.4, 9.6)),
    cab=dict(v=4.3, u=(8.0, 19.6), z=(4.4, 13.2), nose=(25.0, 2.1)),
    boom=dict(v=1.8, pts=[(-6.5, 8.4), (-3.0, 12.4), (9.0, 14.6), (9.0, 12.0), (-2.0, 10.0), (-5.0, 7.4)]),
)
COL = dict(peach=(0.830, 0.665, 0.550), peach_dk=(0.560, 0.420, 0.340), brown=(0.380, 0.290, 0.235), tyre_lt=(0.200, 0.185, 0.165), yellow=(0.850, 0.650, 0.100))
SPEC = dict(peach=(0.0, 0.4, "paint"), peach_dk=(0.0, 0.55, "paint"), brown=(0.0, 0.6, "paint"), yellow=(0.0, 0.5, "paint"))
FILM = dict(peach=(0.700, 0.550, 0.450), peach_dk=(0.480, 0.360, 0.300), tyre_lt=(0.260, 0.240, 0.210), brown=(0.380, 0.290, 0.240), yellow=(0.700, 0.540, 0.150))
K.palette(COL, SPEC, FILM, uv_scale=dict(yellow=0.3))


def build():
    root = K.root(KEY)
    parts = []
    ax, sp, bm, bl, bx, cab, bo = (P[k] for k in ("axle", "spine", "bumper", "block", "box", "cab", "boom"))
    for u in (ax["uf"], ax["ur"]):
        for v in ax["v"]:
            K.wheel(parts, u, v, ax["r"], ax["w"], lugs=10)
        parts.append(K.cyl("axle", 0.9, 0.0, ax["v"][1], u, ax["r"], "dark", segs=10, axis="v"))
        parts.append(K.box("axle_beam", sp["v"] - 0.2, ax["v"][0] - ax["w"] / 2 + 0.2, u - 1.2, u + 1.2, ax["r"] - 0.6, sp["z"][1] - 0.3, "peach", 0.2))
        parts.append(K.box("guard", ax["v"][0] - ax["w"] / 2 + 0.3, ax["v"][1] + ax["w"] / 2 - 0.3, u - 0.8, u + 0.8, 2 * ax["r"] - 0.4, 2 * ax["r"] + 0.25, "peach_dk", 0.1))
    K.mirror_all(parts)
    parts.append(K.box("spine", -sp["v"], sp["v"], sp["u"][0], sp["u"][1], sp["z"][0], sp["z"][1], "peach", 0.3))
    parts.append(K.box("spine_under", -sp["v"] + 0.3, sp["v"] - 0.3, sp["u"][0] + 1, sp["u"][1] - 1, sp["z"][0] - 0.7, sp["z"][0] + 0.1, "brown"))
    for su in (-13.0, -11.0, 7.0, 9.0):
        parts.append(K.box("spine_clamp", -sp["v"] - 0.3, sp["v"] + 0.3, su, su + 0.8, sp["z"][0] - 0.2, sp["z"][1] + 0.3, "peach_dk", 0.1))
    parts.append(K.box("bumper", -bm["v"], bm["v"], bm["u"][0], bm["u"][1], bm["z"][0], bm["z"][1], "peach", 0.3))
    parts.append(K.pod("block", bl["v"][0], bl["v"][1], bl["u"][0], bl["u"][1], bl["z"][0], bl["z"][1], "brown", chamfer=0.8, crown=0.3))
    parts.append(K.box("block_top", bl["v"][0] + 0.8, bl["v"][1] - 0.6, bl["u"][0] + 1.0, bl["u"][1] - 4.0, bl["z"][1] - 0.2, bl["z"][1] + 0.7, "steel_dk", 0.2))
    parts.append(K.tube_z("stack", 0.8, bl["v"][0] + 1.4, bl["u"][0] + 1.6, bl["z"][1], bl["z"][1] + 2.4, "steel_dk", segs=10))
    parts.append(K.box("block_side", bl["v"][0] - 0.5, bl["v"][0] + 0.2, bl["u"][0] + 1.2, bl["u"][1] - 1.2, bl["z"][0] + 0.8, bl["z"][1] - 1.6, "peach_dk", 0.15))
    # the house-colour container on the right
    parts.append(K.house(K.pod("box", bx["v"][0], bx["v"][1], bx["u"][0], bx["u"][1], bx["z"][0], bx["z"][1], "HOUSE", chamfer=0.6, crown=0.35, bev=0.25), 1.0, 0.42))
    um = (bx["u"][0] + bx["u"][1]) / 2
    parts.append(K.box("box_seam", bx["v"][0] - 0.03, bx["v"][1] + 0.05, um - 0.15, um + 0.15, bx["z"][0] + 0.4, bx["z"][1] + 0.03, "recess"))
    parts.append(K.box("box_lug", (bx["v"][0] + bx["v"][1]) / 2 - 0.3, (bx["v"][0] + bx["v"][1]) / 2 + 0.3, um - 0.9, um + 0.9, bx["z"][1] - 0.1, bx["z"][1] + 0.7, "steel_dk", 0.1))
    parts.append(K.box("box_frame", bx["v"][0] + 0.3, bx["v"][1] + 0.2, bx["u"][0] + 0.4, bx["u"][1] - 0.4, bx["z"][0] - 0.9, bx["z"][0] + 0.2, "dark", 0.15))
    # boom with hazard marks, cab and nose
    parts.append(K.prism_uz("boom", bo["pts"], -bo["v"], bo["v"], "steel", 0.3))
    for hu in (0.5, 5.0):
        parts.append(K.box("hazard", -bo["v"] - 0.05, bo["v"] + 0.05, hu, hu + 1.5, 11.0, 14.3, "yellow", 0.05))
    c0, c1 = cab["u"]
    z0, z1 = cab["z"]
    parts.append(K.prism_uz("cab", [(c0, z0), (c1, z0), (c1, z1 - 2.2), (c1 - 2.0, z1), (c0, z1)], -cab["v"], cab["v"], "brown", 0.4))
    parts.append(K.box("cab_roof", -cab["v"] + 0.5, cab["v"] - 0.5, c0 + 0.6, c1 - 2.4, z1 - 0.1, z1 + 0.5, "steel", 0.15))
    parts.append(K.prism_uz("cab_glass", [(c1 - 1.9, z1 - 0.05), (c1 - 0.1, z1 - 2.05), (c1, z1 - 1.85), (c1 - 1.8, z1 + 0.15)], -cab["v"] + 0.7, cab["v"] - 0.7, "glass"))
    nu, nv = cab["nose"]
    parts.append(K.prism_uz("nose", [(c1 - 0.3, z0 + 0.6), (nu, z0 + 1.6), (nu, z1 - 3.4), (c1 - 0.3, z1 - 2.4)], -nv, nv, "peach", 0.35))
    parts.append(K.box("nose_tip", -nv - 0.2, nv + 0.2, nu - 0.9, nu + 0.2, z0 + 1.4, z1 - 3.2, "peach_dk", 0.15))
    for sg in (-1, 1):
        parts.append(K.box("cab_hazard", sg * cab["v"] - 0.06, sg * cab["v"] + 0.06, c0 + 1.2, c0 + 3.4, z0 + 1.6, z0 + 2.8, "yellow"))
    K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    return root


if __name__ == "__main__":
    K.main(KEY, "mcv", build, P)
