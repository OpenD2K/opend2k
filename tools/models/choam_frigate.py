"""CHOAM frigate (the starport's delivery ship) - the ORIGINAL design rebuilt as a modern hard-surface model
(kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/choam_frigate.py -- [--out DIR] [--no-bake] ...
    FACINGS=12 ... tools/models/vehicle_check/run.sh choam_frigate TAG

Templates.bin CHOAM FRIGATE: owner mask 16, unit art 27 = DATA.R16 frame 2784: ONE frame for every direction (64 x 51
px, the ship lying from the upper left to the lower right: heading about 130 degrees, rendered as facing 12 = 135),
no barrel art; model key CHOAM_FRIGATE. The pack renders facing 12 only (FACINGS=12).

How the original reads:
  * a fat lozenge hull about 76 px long and 34 wide, grey-brown steel, carried on short dark landing legs;
  * its back is covered with ribbed cargo hatches in two rows either side of a spine;
  * amidships a tan bridge block framed in the house colour;
  * yellow hazard bands across both ends, a few cyan lights, small house-colour patches fore and aft.
House colour 4.4 % of the sprite; mean luminance 115 (a dark ship), 5 % brighter than 200.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.36, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.5, contour_azimuth=-45.0, sharpen=0.6,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.26), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "CHOAM_FRIGATE"
K.PREVIEW_HOUSE = (0.78, 0.22, 0.20)

P = dict(
    v0=0.0,
    u0=0.0,
    hull=dict(L=40.0, W=17.6, z=(8.0, 16.0), n=1.8),      # half length, half width, bottom / deck, superellipse power
    bridge=dict(v=(-15.0, -3.0), u=(0.0, 13.0), z=22.0),
    legs=dict(v=11.5, u=(-13.0, 13.0), r=3.0),
)
COL = dict(fr_hull=(0.600, 0.560, 0.490), fr_lt=(0.860, 0.820, 0.720), fr_dk=(0.290, 0.260, 0.230), fr_tan=(0.780, 0.630, 0.480),
           fr_yel=(0.820, 0.640, 0.200))
SPEC = dict(fr_hull=(0.3, 0.5, "steel"), fr_lt=(0.2, 0.45, "paint"), fr_dk=(0.3, 0.6, "steel"), fr_tan=(0.0, 0.45, "paint"),
            fr_yel=(0.0, 0.45, "paint"))
FILM = dict(fr_hull=(0.520, 0.480, 0.420), fr_lt=(0.720, 0.680, 0.600), fr_dk=(0.280, 0.250, 0.220), fr_tan=(0.660, 0.540, 0.420),
            fr_yel=(0.680, 0.540, 0.220))
K.palette(COL, SPEC, FILM)


def half_width(u):
    hl = P["hull"]
    t = min(1.0, abs(u) / hl["L"])
    return hl["W"] * (1.0 - t ** hl["n"]) ** (1.0 / hl["n"])


def outline(scale=1.0, n=40):
    hl = P["hull"]
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        e = 2.0 / hl["n"]
        pts.append((scale * hl["W"] * math.copysign(abs(c) ** e, c), scale * hl["L"] * math.copysign(abs(s) ** e, s)))
    return pts


def build():
    root = K.root(KEY)
    parts = []
    hl, br, lg = (P[k] for k in ("hull", "bridge", "legs"))
    z0, z1 = hl["z"]
    L = hl["L"]
    # hull: a dark keel, the main body, a crowned deck
    parts.append(K.plan_prism("keel", outline(0.8), z0 - 1.6, z0 + 0.4, "fr_dk", 0.3, top=outline(0.93)))
    parts.append(K.plan_prism("hull", outline(1.0), z0, z1 - 1.5, "fr_hull", 0.4))
    parts.append(K.plan_prism("deck", outline(1.0), z1 - 1.5, z1, "fr_hull", 0.3, top=outline(0.9)))
    parts.append(K.plan_prism("belt", outline(1.015), z0 + 2.2, z0 + 3.2, "fr_dk", 0.1))
    # cargo hatches: two rows of ribbed plates either side of the spine, from end to end (not under the bridge)
    step = 8.0
    u = -L + 9.0
    while u < L - 9.0:
        if True:
            w = min(half_width(u + 0.4), half_width(u + step - 0.4)) * 0.86
            if w > 4.0:
                for sg in (-1, 1):
                    a_, b_ = sorted((sg * 1.6, sg * w))
                    if sg < 0 and br["u"][0] - 2.5 < u + step / 2 < br["u"][1] + 2.5:
                        continue
                    parts.append(K.pod("hatch", a_, b_, u + 0.35, u + step - 0.35, z1 - 0.3, z1 + 1.5, "fr_lt", chamfer=1.4, crown=1.0))
                    parts.append(K.box("hatch_rib", a_ + 0.6, b_ - 0.6, u + step / 2 - 0.3, u + step / 2 + 0.3, z1 + 1.3, z1 + 1.75, "fr_hull", 0.08))
        u += step
    parts.append(K.box("spine", -1.3, 1.3, -L + 5.0, L - 5.0, z1 - 0.3, z1 + 1.3, "fr_dk", 0.3))
    for uu in (-L + 9.0, -L + 20.0, L - 20.0, L - 9.0):
        parts.append(K.tube_z("spine_cap", 1.5, 0.0, uu, z1 + 1.0, z1 + 1.8, "fr_hull", segs=12))
    # yellow hazard bands across both ends, house patches fore and aft
    for sg in (-1, 1):
        ub = sg * (L - 5.2)
        w = half_width(ub) * 0.82
        a_, b_ = sorted((ub - 2.4, ub + 2.0))
        parts.append(K.box("hazard", -w, w, a_, b_, z1 - 0.2, z1 + 0.6, "fr_yel", 0.1))
        for k in range(-2, 3):
            parts.append(K.box("hazard_gap", k * w / 2.6 - 0.45, k * w / 2.6 + 0.45, a_ - 0.03, b_ + 0.03, z1 - 0.1, z1 + 0.65, "fr_dk"))
    parts.append(K.house(K.box("patch_f", 6.0, 10.5, L - 14.5, L - 11.0, z1 - 0.1, z1 + 1.1, "HOUSE", 0.2), 0.9, 0.7))
    parts.append(K.house(K.box("patch_r", -11.0, -6.5, -L + 10.5, -L + 14.0, z1 - 0.1, z1 + 1.1, "HOUSE", 0.2), 0.9, 0.7))
    parts.append(K.house(K.box("patch_s", hl["W"] - 1.2, hl["W"] + 0.2, -7.0, -1.0, z0 + 3.6, z1 - 2.0, "HOUSE", 0.1), 0.8, 0.6))
    # bridge: a tan block in a house-colour frame, dark roof hatch, cyan lights
    b0, b1 = br["u"]
    v0, v1 = br["v"]
    parts.append(K.house(K.box("bridge_frame", v0 - 0.9, v1 + 0.9, b0 - 0.9, b1 + 0.9, z1 - 0.2, z1 + 1.4, "HOUSE", 0.25), 0.9, 0.6))
    parts.append(K.pod("bridge", v0, v1, b0, b1, z1 + 0.6, br["z"], "fr_tan", chamfer=1.2, crown=0.6))
    parts.append(K.box("bridge_roof", v0 + 2.2, v1 - 2.2, b0 + 2.2, b1 - 2.2, br["z"] - 0.1, br["z"] + 0.5, "fr_dk", 0.2))
    parts.append(K.box("bridge_glass", v0 + 1.5, v1 - 1.5, b1 - 0.2, b1 + 0.15, z1 + 1.8, br["z"] - 1.0, "glass", 0.05))
    for (cv, cu) in ((9.5, 10.0), (-9.5, 10.0), (9.5, -11.0), (-9.5, -11.0), (0.0, L - 2.4), (0.0, -L + 2.4)):
        parts.append(K.box("light", cv - 0.7, cv + 0.7, cu - 0.7, cu + 0.7, z1 + 0.6, z1 + 1.3, "cyan", 0.1))
    # landing legs
    for sg in (-1, 1):
        for lu in lg["u"]:
            parts.append(K.tube_z("leg", lg["r"], sg * lg["v"], lu, 0.6, z0 + 0.5, "fr_dk", segs=12))
            parts.append(K.tube_z("foot", lg["r"] * 1.7, sg * lg["v"], lu, 0.0, 0.8, "fr_dk", segs=12))
    # engines at the stern
    for sg in (-1, 1):
        K.nozzle(parts, (sg * 5.5, -L + 2.2, z0 + 4.0), (0, -1, 0), 2.4, 2.6, "fr_dk")
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], P["u0"], 0))
    for i, sg in enumerate((1, -1)):
        K.anchor(f"fx_exhaust_{i}", (sg * 5.5 + P["v0"], -L - 0.4 + P["u0"], z0 + 4.0), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "choam_frigate", build, P)
