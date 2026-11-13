"""Missile tank - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_missile_tank.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=missile_tank .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin MISSILE TANK: owner mask 27 (Atreides, Harkonnen, Emperor, Fremen side), unit art 6 = DATA.R16
frames 1838-1869, NO barrel art (the launcher is part of the body frames); model key MISSILE_TANK.

How the original reads (the cardinal views; the hull of a near-square vehicle says little):
  * a square tracked carrier, 19 x 19 px: dark tracks under pale fender pods, the two front pods bright cream,
    a small cream cab between them with a cyan window, teal lamps low on the rear corners;
  * over the rear two thirds a big box launcher, pitched up towards the front: cream ribbed roof with a house
    stripe along the middle and house bands along both edges, a house-colour front face with two cream tube
    columns and red missile tips, a dark rear face;
  * house colour 14.5 % of the sprite (the launcher's bands and front), few near-black pixels (5 %).
"""
import math
import os
import sys

from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.34, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.6, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.13), (0.25, 0.27), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "MISSILE_TANK"

P = dict(
    v0=-0.4,
    track=dict(v=(5.8, 10.1), u=(-9.7, 9.7), z=5.2),
    hull=dict(v=5.9, u=(-9.7, 9.5), z=(1.4, 5.6)),
    fender=dict(v=(5.0, 10.4), u=(-9.9, 9.8), split=1.6, z=(4.8, 7.4), z_rear=6.4),
    cab=dict(v=2.4, u=(3.0, 9.0), z=(4.4, 8.0)),
    rack=dict(v=6.0, u=(-10.2, 2.4), z=(6.4, 12.6), pitch=3.0, stripe=1.1, band=1.3, spine=(1.9, 0.9)),
)
COL = dict(rackc=(0.800, 0.730, 0.640), rib=(0.520, 0.460, 0.390), red=(0.780, 0.200, 0.150), teal=(0.050, 0.520, 0.600))
SPEC = dict(rackc=(0.0, 0.42, "paint"), rib=(0.0, 0.6, "paint"), red=(0.0, 0.35, "dark"), teal=(0.0, 0.25, "dark"))
K.palette(COL, SPEC, dict(rackc=(0.700, 0.630, 0.550), rib=(0.460, 0.410, 0.350)), uv_scale=dict(red=0.2, teal=0.2))


def build():
    root = K.root(KEY)
    parts = []
    tr, hl, fd, cab, rk = (P[k] for k in ("track", "hull", "fender", "cab", "rack"))
    u0, u1 = tr["u"]
    prof = [(u0 + 1.6, 0.0), (u1 - 1.6, 0.0), (u1 - 0.2, 1.6), (u1, 2.9), (u1 - 0.9, tr["z"]), (u0 + 0.9, tr["z"]), (u0, 2.9), (u0 + 0.2, 1.6)]
    K.track(parts, prof, tr["v"][0], tr["v"][1], wheels=((u0 + 2.2, 2.5, 1.9), (u1 - 2.2, 2.5, 1.9)))
    # fender pods over the track: bright cream at the front, dusty grey-beige behind
    parts.append(K.pod("fender_f", fd["v"][0], fd["v"][1], fd["split"], fd["u"][1], fd["z"][0], fd["z"][1], "cream", chamfer=2.2, crown=0.5))
    parts.append(K.pod("fender_r", fd["v"][0], fd["v"][1], fd["u"][0], fd["split"] - 0.5, fd["z"][0], fd["z_rear"], "mid", chamfer=2.0, crown=0.3))
    parts.append(K.box("skirt", tr["v"][1] - 0.3, tr["v"][1] + 0.25, u0 + 2.6, u1 - 2.8, 2.6, fd["z"][0] + 0.2, "panel", 0.15))
    parts.append(K.box("lamp", tr["v"][1] - 0.9, tr["v"][1] + 0.5, u0 - 0.15, u0 + 0.9, 2.6, 4.3, "teal", 0.1))
    K.mirror_all(parts)
    parts.append(K.box("hull", -hl["v"], hl["v"], hl["u"][0], hl["u"][1], hl["z"][0], hl["z"][1], "panel", 0.4))
    parts.append(K.prism_uz("glacis", [(hl["u"][1] - 0.2, hl["z"][0] + 0.6), (hl["u"][1] + 0.9, hl["z"][0] + 1.6), (hl["u"][1] + 0.5, hl["z"][1] - 0.6), (hl["u"][1] - 0.2, hl["z"][1])],
                            -fd["v"][0], fd["v"][0], "dark", 0.2))
    c0, c1 = cab["u"]
    parts.append(K.prism_uz("cab", [(c0, cab["z"][0]), (c1, cab["z"][0]), (c1, cab["z"][1] - 1.7), (c1 - 1.9, cab["z"][1]), (c0, cab["z"][1])], -cab["v"], cab["v"], "cream", 0.35))
    parts.append(K.prism_uz("window", [(c1 - 1.75, cab["z"][1] - 0.1), (c1 - 0.15, cab["z"][1] - 1.55), (c1 - 0.05, cab["z"][1] - 1.35), (c1 - 1.65, cab["z"][1] + 0.1)],
                            -cab["v"] + 1.3, cab["v"] - 1.3, "cyan"))
    # the launcher (built level, then pitched up about its rear lower edge)
    r0, r1 = rk["u"]
    z0, z1 = rk["z"]
    w = rk["v"]
    rack = [K.box("rack", -w, w, r0, r1, z0, z1, "rackc", 0.3)]
    sw, sh = rk["spine"]
    rack.append(K.box("rack_spine", -sw, sw, r0 + 0.3, r1 - 0.2, z1 - 0.2, z1 + sh, "rackc", 0.25))
    rack.append(K.house(K.box("rack_stripe", -rk["stripe"], rk["stripe"], r0 + 0.2, r1 - 0.1, z1 + sh - 0.3, z1 + sh + 0.14, "HOUSE", 0.1), 0.8, 0.9))
    um = (r0 + r1) / 2
    rack.append(K.house(K.box("rack_strap", -w - 0.1, w + 0.1, um - rk["band"], um + rk["band"], z0 + 0.3, z1 + 0.12, "HOUSE", 0.1), 0.7, 0.6))
    for sg in (-1, 1):
        for k in range(1, 4):                       # ribs on the roof: the tubes underneath
            rv = sg * (sw + (w - sw) * k / 4)
            rack.append(K.seam("rack_rib", rv - 0.2, rv + 0.2, r0 + 0.4, r1 - 0.4, z1 + 0.03, 0.4, "rib"))
        for sz in (z0 + 2.0, z0 + 4.0):            # tube rows along the launcher's sides
            a, b = sorted((sg * (w - 0.3), sg * (w + 0.05)))
            rack.append(K.box("rack_side", a, b, r0 + 0.5, r1 - 0.5, sz - 0.25, sz + 0.25, "rib"))
    rack.append(K.house(K.box("rack_face", -w + 0.5, w - 0.5, r1 - 0.3, r1 + 0.12, z0 + 0.35, z1 - 0.3, "HOUSE", 0.05), 0.42, 0.9))
    for sg in (-1, 1):                              # two cream tube columns with red tips in the front face
        cv = sg * 3.2
        rack.append(K.box("tube_col", cv - 1.35, cv + 1.35, r1 - 0.2, r1 + 0.3, z0 + 0.6, z1 - 0.5, "cream", 0.08))
        for cz in (z0 + 1.6, z0 + 3.4, z0 + 5.0):
            rack.append(K.cyl("tube", 0.62, r1 - 0.1, r1 + 0.34, cv, cz, "recess", segs=10))
            rack.append(K.cyl("tip", 0.42, r1, r1 + 0.75, cv, cz, "red", segs=8, r1=0.1))
    rack.append(K.house(K.box("rack_rear", -w + 0.7, w - 0.7, r0 - 0.12, r0 + 0.3, z0 + 0.5, z1 - 0.5, "HOUSE", 0.05), 0.45, 0.8))
    m = Matrix.Translation(Vector((0, r0, z0))) @ Matrix.Rotation(math.radians(rk["pitch"]), 4, "X") @ Matrix.Translation(Vector((0, -r0, -z0)))
    for o in rack:
        o.data.transform(m)
    parts.extend(rack)
    parts.append(K.box("rack_bed", -w + 1.0, w - 1.0, r0 + 0.5, r1 - 1.0, hl["z"][1] - 0.3, z0 + 0.6, "dark", 0.2))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    tip = m @ Vector((0, r1 + 0.8, (z0 + z1) / 2))
    for i, sg in enumerate((1, -1)):
        K.anchor(f"fx_muzzle_{i}", (sg * 3.2 + P["v0"], tip.y, tip.z), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "missile_tank", build, P)
