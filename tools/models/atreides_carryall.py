"""Carryall - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_carryall.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=carryall .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin CARRYALL and CARRYALL2 (owner mask 127, all houses) share ONE art: unit art 16 = DATA.R16 frames
2158-2189 (32 facings, no loaded / empty difference, no barrel art, no shadow pixels: the game draws a flyer's
shadow itself); model key CARRYALL (the group name: both templates find it).

How the original reads (it is itself a rendered model; the north, east and south frames solved together for
position along the craft and height, tools/models/vehicle_check):
  * a tall tapering fuselage (15 px high, 10-15 wide) from a bulbous nose 33 px ahead of the turning axis (a cyan
    window arc across it) to the gantry 25 px behind it;
  * a straight house-colour wing slab on top of it, over the axis (34 px span, 15 px chord: 23 % of the sprite),
    panel seams;
  * a pale cream engine nacelle under each wing tip (20 px long, 9 px thick, dark exhaust at the rear, a stub pod
    outboard, two little skids);
  * at the tail the cargo gantry: a 26 px crossbar at wing height and a 20 px leg hanging DOWN from each end -
    a thin outer plate with a clamp pad at its foot and braces to the bar; short prongs on top.
The craft is 62 px long, 58 across and 25 high: bigger than a tile in every direction.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.34, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.5, contour_azimuth=-45.0, sharpen=0.6,
              tone=((0.0, 0.0), (0.12, 0.14), (0.25, 0.28), (0.5, 0.53), (0.75, 0.79), (1.0, 1.0)))
KEY = "CARRYALL"

P = dict(
    zc=12.0,                        # the fuselage's axis above the feet of the gantry legs
    fus=[(-22.0, 3.0, 3.0, 6.5), (-12.0, 4.4, 4.8, 3.0), (0.0, 5.6, 7.0, 0.0), (12.0, 6.2, 7.8, 0.0), (20.0, 7.6, 7.4, -0.3), (25.0, 8.4, 7.0, -0.6),
         (29.8, 6.8, 5.4, -1.0), (33.2, 2.8, 2.4, -1.2)],          # u, half width, half height, dz
    wing=dict(v=17.2, u=(-7.6, 7.6), z=21.6, t=1.0),
    nac=dict(v=20.8, u=(-10.6, 11.4), z=19.3, r=5.1),
    stub=dict(v=28.0, u=(-3.8, 3.8), r=2.4),
    tail=dict(bar_u=(-28.6, -21.4), bar_v=12.6, bar_z=(18.4, 21.8), leg_v=(6.8, 12.6), leg_u=(-28.0, -22.2)),
)
COL = dict(olive=(0.660, 0.660, 0.560), olive_dk=(0.440, 0.440, 0.370), ivory=(0.900, 0.890, 0.760), exhaust=(0.330, 0.220, 0.180))
SPEC = dict(olive=(0.1, 0.4, "paint"), olive_dk=(0.1, 0.5, "paint"), ivory=(0.0, 0.35, "paint"), exhaust=(0.3, 0.6, "dark"))
FILM = dict(olive=(0.580, 0.580, 0.490), olive_dk=(0.400, 0.400, 0.340), ivory=(0.780, 0.770, 0.660))
K.palette(COL, SPEC, FILM, weather=dict(paint=(0.6, 0.5, 0.9, 0.5, 0.5, 0.5, 0.4, 0.0)))


def build():
    root = K.root(KEY)
    parts = []
    zc = P["zc"]
    wg, nc, sb, tl = (P[k] for k in ("wing", "nac", "stub", "tail"))
    zn = nc["z"]
    # right side, mirrored: nacelle, stub pod, skids, gantry leg
    K.nozzle(parts, (nc["v"], nc["u"][0] + 1.2, zn), (0, -1, 0), nc["r"] * 0.62, 2.4, "olive_dk", glow="exhaust")
    n = 16
    rings = []
    for (u, k) in ((nc["u"][0] + 1.0, 0.7), (nc["u"][0] + 3.0, 1.0), (nc["u"][1] - 4.0, 1.0), (nc["u"][1] - 1.4, 0.84), (nc["u"][1], 0.45)):
        rings.append([(nc["v"] + nc["r"] * k * math.cos(2 * math.pi * i / n), u, zn + nc["r"] * k * math.sin(2 * math.pi * i / n)) for i in range(n)])
    parts.append(K.loft("nacelle", rings, "ivory", 0.15))
    for bu in (nc["u"][0] + 5.0, nc["u"][1] - 6.5):
        parts.append(K.cyl("nac_band", nc["r"] + 0.15, bu, bu + 0.8, nc["v"], zn, "olive_dk", segs=16))
    parts.append(K.cyl("stub", sb["r"], sb["u"][0], sb["u"][1], sb["v"], zn, "olive", segs=12, bev=0.5))
    parts.append(K.box("stub_arm", nc["v"] + nc["r"] - 0.4, sb["v"], -1.2, 1.2, zn - 0.8, zn + 0.8, "olive_dk", 0.2))
    for su in (-5.0, -2.0):
        parts.append(K.tube("skid", (nc["v"] + 0.8, su, zn - nc["r"] + 0.4), (nc["v"] + 2.2, su - 2.4, zn - nc["r"] - 3.2), 0.6, "olive_dk", segs=6))
    l0, l1 = tl["leg_u"]
    # the leg: a thin outer plate hanging from the bar, a clamp pad at its foot, braces from the bar
    parts.append(K.box("leg", tl["leg_v"][1] - 1.7, tl["leg_v"][1], l0, l1, 1.5, tl["bar_z"][0] + 0.4, "olive", 0.4))
    parts.append(K.box("leg_foot", tl["leg_v"][0], tl["leg_v"][1] + 0.3, l0 - 0.6, l1 + 0.6, 0.0, 2.8, "olive", 0.5))
    parts.append(K.box("leg_rib", tl["leg_v"][1] - 0.2, tl["leg_v"][1] + 0.25, l0 + 1.0, l1 - 1.0, 5.0, 15.0, "olive_dk", 0.1))
    for bu in (l0 + 1.2, l1 - 1.2):
        parts.append(K.tube("brace", (2.0, bu, tl["bar_z"][0] + 0.3), (tl["leg_v"][0] + 1.0, bu, 2.4), 0.8, "olive_dk", segs=8))
    parts.append(K.box("prong", tl["leg_v"][1] - 1.6, tl["leg_v"][1] - 0.2, l0 + 1.0, l1 - 1.0, tl["bar_z"][1] - 0.3, tl["bar_z"][1] + 3.4, "olive", 0.2))
    K.mirror_all(parts)
    # fuselage: tall, tapering and rising to the gantry
    rings = []
    for (u, w, h, dz) in P["fus"]:
        rings.append([(w * math.cos(2 * math.pi * i / 20), u, zc + dz + h * math.sin(2 * math.pi * i / 20)) for i in range(20)])
    parts.append(K.loft("fuselage", rings, "olive", 0.0))
    parts.append(K.ellipsoid("window", (0, 28.4, zc + 1.6), (5.9, 2.7, 3.0), "cyan", segs=16, rings=8))
    parts.append(K.ellipsoid("nose_cap", (0, 27.2, zc + 2.6), (5.4, 3.9, 3.4), "olive", segs=16, rings=8))
    parts.append(K.box("spine", -1.3, 1.3, -9.0, 20.0, zc + 5.6, zc + 7.6, "olive_dk", 0.5))
    parts.append(K.box("pylon", -4.2, 4.2, wg["u"][0] + 1.0, wg["u"][1] - 1.0, zc + 4.0, wg["z"] + 0.2, "olive", 0.6))
    # wing: house slab with seams
    parts.append(K.house(K.box("wing", -wg["v"], wg["v"], wg["u"][0], wg["u"][1], wg["z"] - wg["t"], wg["z"] + wg["t"], "HOUSE", 0.3), 0.85, 0.75))
    for sv in (-13.0, -8.5, 8.5, 13.0):
        parts.append(K.seam("wing_seam", sv - 0.2, sv + 0.2, wg["u"][0] + 0.2, wg["u"][1] - 0.2, wg["z"] + wg["t"] + 0.02, 0.3, "steel_dk"))
    parts.append(K.box("wing_cap", -3.4, 3.4, wg["u"][0] - 0.4, wg["u"][1] + 0.4, wg["z"] + wg["t"] - 0.1, wg["z"] + wg["t"] + 0.9, "olive", 0.3))
    parts.append(K.box("bar", -tl["bar_v"], tl["bar_v"], tl["bar_u"][0], tl["bar_u"][1], tl["bar_z"][0], tl["bar_z"][1], "olive", 0.5))
    parts.append(K.box("bar_block", -3.6, 3.6, tl["bar_u"][0] - 0.6, tl["bar_u"][1] + 1.6, tl["bar_z"][0] - 1.2, tl["bar_z"][1] + 0.9, "olive_dk", 0.5))
    body = K.node(parts, "body", (0, 0, 0), root)
    for i, sg in enumerate((1, -1)):
        K.anchor(f"fx_exhaust_{i}", (sg * nc["v"], nc["u"][0] - 1.4, zn), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "carryall", build, P)
