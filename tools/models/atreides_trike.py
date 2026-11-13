"""Trike - the ORIGINAL design rebuilt as a modern hard-surface model (the method and kit of combat_tank_v3.py,
through tools/models/atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_trike.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=trike .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin TRIKE: owner mask 27 (Atreides, Harkonnen, Emperor, Fremen side - not Ordos, who have the raider),
unit art 7 = DATA.R16 frames 1870-1901, no barrel art; model key TRIKE. One art for all those houses.

How the original reads (visual hull of the 32 silhouettes + the cardinal views, tools/models/vehicle_check):
  * a three-wheeler 24 px long: two fat dark wheels at the rear (u -6.5, 6 px off the centre line) and one at the
    front (u +11.5), joined by a slim sand / peach spine;
  * between the rear wheels a house-colour cab block (dark blue on top, a light slab at the very rear), sand
    fenders beside it;
  * in front of the cab a dark cockpit trough with the cyan strip (canopy light) along the centre line;
  * a long thin black twin gun on top of the cab reaching forward to the middle of the spine;
  * a sand fork / mudguard over the front wheel.
House colour 13.7 % of the sprite, a quarter of it near-black (wheels, guns, trough).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.36, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.13), (0.25, 0.29), (0.5, 0.56), (0.75, 0.80), (1.0, 1.0)))
KEY = "TRIKE"

P = dict(
    v0=-0.2,                        # the body sits a little left of the turning axis (as the original frames)
    rear=dict(u=-6.4, v=5.9, r=3.75, w=3.4),
    front=dict(u=11.3, r=3.6, w=3.5),
    spine=dict(v=1.9, u=(-3.0, 9.4), z=(3.3, 5.3)),
    delta=[(1.9, 2.4), (4.6, -3.0), (4.6, -4.6), (1.9, -4.6)],          # right half (v, u) of the deck widening towards the rear axle,
    cab=dict(v=3.1, u=(-9.8, -2.4), z=(2.6, 9.0), nose=2.0, rise=1.6),
    slab=dict(v=3.0, u=(-10.5, -9.5), z=(3.4, 9.6)),
    trough=dict(v=1.7, u=(-2.6, 5.4), z=(4.8, 7.0)),
    gun=dict(v=0.9, u=(-6.6, 5.6), z=10.6, r=0.45),
    fork=dict(v=2.4, u=(7.2, 12.4), z=(5.3, 7.6)),
)
COL = dict(peach=(0.880, 0.715, 0.600), peach_dk=(0.640, 0.480, 0.390), gunblack=(0.080, 0.075, 0.075))
SPEC = dict(peach=(0.0, 0.4, "paint"), peach_dk=(0.0, 0.55, "paint"), gunblack=(0.4, 0.5, "dark"))
FILM = dict(peach=(0.760, 0.600, 0.490), peach_dk=(0.540, 0.400, 0.320))
K.palette(COL, SPEC, FILM, uv_scale=dict(gunblack=0.4))


def build():
    root = K.root(KEY)
    parts = []
    rw, fw, sp, cab, sl, tr, gn, fk = (P[k] for k in ("rear", "front", "spine", "cab", "slab", "trough", "gun", "fork"))
    # right side, mirrored: rear wheel, its fender and axle
    K.wheel(parts, rw["u"], rw["v"], rw["r"], rw["w"], lugs=9)
    parts.append(K.cyl("axle", 0.8, 0.0, rw["v"], rw["u"], rw["r"], "dark", segs=10, axis="v"))
    parts.append(K.pod("fender", cab["v"] - 0.2, rw["v"] - rw["w"] / 2 + 0.5, rw["u"] - 2.6, rw["u"] + 2.9, 3.4, 6.3, "peach", chamfer=0.7, crown=0.3))
    parts.append(K.box("fender_lip", rw["v"] - rw["w"] / 2 + 0.2, rw["v"] + rw["w"] / 2 - 0.4, rw["u"] - 1.9, rw["u"] + 2.2, 6.75, 7.2, "peach_dk", 0.15))
    parts.append(K.tube("gun", (gn["v"], gn["u"][0], gn["z"]), (gn["v"], gn["u"][1], gn["z"]), gn["r"], "gunblack", segs=8))
    parts.append(K.box("gun_mount", gn["v"] - 0.5, gn["v"] + 0.5, gn["u"][0] + 0.6, gn["u"][0] + 3.2, cab["z"][1] - 0.2, gn["z"] + 0.1, "gunblack", 0.1))
    # the fork over the front wheel
    parts.append(K.box("fork_arm", fw["w"] / 2 + 0.15, fw["w"] / 2 + 0.75, fw["u"] - 2.4, fw["u"] + 0.5, fw["r"] - 0.6, fk["z"][0] + 0.4, "peach_dk", 0.1))
    parts.append(K.plan_prism("delta", P["delta"], 3.1, 4.6, "peach", 0.25))
    K.mirror_all(parts)
    # centre line: spine, cab, slab, cockpit trough + cyan strip, front wheel + mudguard
    parts.append(K.prism_uz("spine", [(sp["u"][0], sp["z"][0]), (sp["u"][1], sp["z"][0] + 0.9), (sp["u"][1], sp["z"][1] + 0.9), (sp["u"][0], sp["z"][1])],
                            -sp["v"], sp["v"], "peach", 0.3))
    parts.append(K.box("spine_under", -sp["v"] + 0.3, sp["v"] - 0.3, sp["u"][0], sp["u"][1] - 3.0, sp["z"][0] - 0.35, sp["z"][0] + 0.2, "peach_dk"))
    c0, c1 = cab["u"]
    z0, z1 = cab["z"]
    parts.append(K.box("cab_base", -cab["v"] - 0.25, cab["v"] + 0.25, c0 + 0.3, c1 + 0.2, z0 - 0.6, z0 + 1.3, "dark", 0.2))
    parts.append(K.house(K.prism_uz("cab", [(c0, z0 + 1.0), (c1, z0 + 1.0), (c1, z1 - cab["nose"] - 0.6), (c1 - cab["nose"], z1), (c0 + 0.5, z1 + cab["rise"])],
                                    -cab["v"], cab["v"], "HOUSE", 0.35), 0.34, 0.7))
    parts.append(K.house(K.box("slab", -sl["v"], sl["v"], sl["u"][0], sl["u"][1], sl["z"][0], sl["z"][1], "HOUSE", 0.25), 1.0, 0.95))
    for sg in (1, -1):       # dark shoulders beside the cab, tapering forward to the cockpit
        parts.append(K.wedge("cab_rail", [(sg * (cab["v"] - 0.1), c0 + 0.8), (sg * 4.7, c0 + 0.8), (sg * 4.7, -3.2), (sg * 2.2, 0.6), (sg * (cab["v"] - 0.1), 0.6)][::sg],
                             [(sg * (cab["v"] - 0.1), c0 + 1.2), (sg * 4.3, c0 + 1.2), (sg * 4.3, -3.4), (sg * 2.0, 0.2), (sg * (cab["v"] - 0.1), 0.2)][::sg],
                             z0 + 0.8, z1 - 0.5, "steel_dk", 0.2))
    parts.append(K.box("trough_bed", -2.6, 2.6, c1 - 0.4, 3.4, 3.6, 5.6, "peach_dk", 0.3))
    parts.append(K.box("cab_seam", -cab["v"] - 0.03, cab["v"] + 0.03, c0 + 2.6, c0 + 2.95, z0 + 1.6, z1 + cab["rise"], "recess"))
    t0, t1 = tr["u"]
    parts.append(K.prism_uz("trough", [(t0, tr["z"][0]), (t1, tr["z"][0] + 0.5), (t1, tr["z"][1] - 0.6), (t0, tr["z"][1] + 0.9)], -tr["v"], tr["v"], "glass", 0.25))
    parts.append(K.prism_uz("cyan", [(t0 + 0.5, tr["z"][1] + 0.75), (t1 - 0.6, tr["z"][1] - 0.55), (t1 - 0.6, tr["z"][1] - 0.3), (t0 + 0.5, tr["z"][1] + 1.0)],
                            -0.55, 0.55, "cyan"))
    K.wheel(parts, fw["u"], 0.0, fw["r"], fw["w"], lugs=9, name="wheel_f")
    parts.append(K.pod("mudguard", -fk["v"], fk["v"], fk["u"][0], fk["u"][1], fk["z"][0], fk["z"][1], "peach", chamfer=0.8, crown=0.4))
    parts.append(K.box("fork_bridge", -fw["w"] / 2 - 0.7, fw["w"] / 2 + 0.7, fw["u"] - 2.6, fw["u"] - 1.6, fk["z"][0] - 0.6, fk["z"][0] + 0.3, "peach_dk", 0.1))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    for i, sg in enumerate((1, -1)):
        K.anchor(f"fx_muzzle_{i}", (sg * gn["v"] + P["v0"], gn["u"][1], gn["z"]), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "trike", build, P)
