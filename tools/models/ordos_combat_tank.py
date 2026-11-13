"""Ordos combat tank - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py; the
hull follows harkonnen_combat_tank.py - the two originals share the H layout).

    blender -b --factory-startup --python tools/models/ordos_combat_tank.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=ordos_combat_tank .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin ORDOS COMBAT TANK: owner mask 100 (Ordos, Mercenaries, Smugglers), unit art 25 = DATA.R16 frames
2720-2751, barrel art 26 = frames 2752-2783 (the turret); model key ORDOS_COMBAT_TANK, nodes body + turret + barrel,
anchor fx_muzzle_0.

How the original reads (the cardinal views, tools/models/vehicle_check):
  * the same H as the Harkonnen tank (28 x 21 px): two track pods, a lower beige hull between them, open rear gap;
  * the pods' covers are cream with a thin pale house band at both ends; the pods' outer flanks are dark steel with
    a pale blue-grey window amidships;
  * the turret is a low house-colour horseshoe (open to the front) holding a cyan canopy dome in its mouth;
    the gun comes out under the dome, 11 px past the turret, 3 px thick.
House colour 7 % of the body (the ramp's light end), 31 % of the turret.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.5, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_turret=0.85, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.27), (0.5, 0.54), (0.75, 0.80), (1.0, 1.0)))
KEY = "ORDOS_COMBAT_TANK"
K.PREVIEW_HOUSE = (0.20, 0.55, 0.35)

P = dict(
    v0=0.0,
    track=dict(v=(3.7, 9.1), u=(-14.4, 13.4), z=4.7),
    cover=dict(v=(3.3, 8.3), u=(-8.6, 7.8), z=(4.2, 7.3)),
    band=1.5,
    skirt=dict(v=(8.0, 10.4), u=(-6.0, 5.0), z=(2.9, 5.7)),
    slope=dict(u=-13.9, z=4.3),
    hull=dict(v=3.8, u=(-8.8, 7.2), z=(1.8, 5.7)),
    turret=dict(rv=6.6, ru=6.5, u=-1.5, z=(6.6, 9.8), du=0.0, dv=0.0),
    dome=dict(u=0.4, rv=4.0, ru=4.0, z=9.4, h=5.2),
    gun=dict(tip=16.3, z=9.2, r=1.3),
)
COL = dict(hk_white=(0.900, 0.850, 0.760), hk_grey=(0.600, 0.550, 0.480), hk_dk=(0.400, 0.360, 0.320), hk_gun=(0.560, 0.550, 0.560),
           rust=(0.520, 0.300, 0.160), window=(0.640, 0.760, 0.820), teal=(0.030, 0.300, 0.330))
SPEC = dict(hk_white=(0.0, 0.34, "paint"), hk_grey=(0.0, 0.5, "paint"), hk_dk=(0.0, 0.6, "paint"), hk_gun=(0.5, 0.35, "steel"),
            rust=(0.0, 0.7, "paint"), window=(0.3, 0.2, "dark"), teal=(0.3, 0.15, "dark"))
FILM = dict(hk_white=(0.780, 0.730, 0.660), hk_grey=(0.560, 0.510, 0.450), hk_dk=(0.360, 0.320, 0.280), hk_gun=(0.500, 0.490, 0.480))
K.palette(COL, SPEC, FILM)


def build():
    root = K.root(KEY)
    parts = []
    tr, cv, sk, hl, tu, gn = (P[k] for k in ("track", "cover", "skirt", "hull", "turret", "gun"))
    u0, u1 = tr["u"]
    prof = [(u0 + 2.2, 0.0), (u1 - 2.2, 0.0), (u1 - 0.3, 1.6), (u1, 3.0), (u1 - 1.2, tr["z"]), (u0 + 1.2, tr["z"]), (u0, 3.0), (u0 + 0.3, 1.6)]
    K.track(parts, prof, tr["v"][0], tr["v"][1], wheels=((u0 + 2.5, 2.5, 2.0), (u1 - 2.5, 2.5, 2.0)))
    c0, c1 = cv["u"]
    z0, z1 = cv["z"]
    b = P["band"]
    # white cover between the two house bands
    parts.append(K.pod("cover", cv["v"][0], cv["v"][1], c0 + b - 0.2, c1 - b + 0.2, z0, z1, "hk_white", chamfer=0.3, crown=0.45))
    for k in range(1, 4):
        ru = c0 + b + (c1 - c0 - 2 * b) * k / 4
        parts.append(K.seam("cover_seam", cv["v"][0] + 0.3, cv["v"][1] - 0.3, ru - 0.12, ru + 0.12, z1 + 0.02, 0.3, "hk_dk"))
    for (a_, b_) in ((c1 - b, c1), (c0, c0 + b)):
        parts.append(K.house(K.pod("band", cv["v"][0] - 0.15, cv["v"][1] + 0.15, a_, b_, z0 - 0.4, z1 + 0.15, "HOUSE", chamfer=0.3, crown=0.3), 1.0, 0.9))
    # the grey cover sloping down to the rear track end, a short dark nose in front
    sl = P["slope"]
    parts.append(K.prism_uz("rear_slope", [(sl["u"], sl["z"] - 1.2), (c0 + 0.1, z0 - 0.6), (c0 + 0.1, z1 - 0.2), (sl["u"] + 0.6, sl["z"])],
                            tr["v"][0] - 0.25, tr["v"][1] + 0.1, "hk_grey", 0.35))
    parts.append(K.seam("slope_seam", cv["v"][0] + 0.5, cv["v"][1] - 0.4, (sl["u"] + c0) / 2 - 0.15, (sl["u"] + c0) / 2 + 0.15, (sl["z"] + z1) / 2 + 0.1, 0.5, "hk_dk"))
    parts.append(K.prism_uz("nose", [(c1 - 0.1, z0 - 0.6), (u1 - 1.6, tr["z"] - 0.4), (u1 - 1.8, tr["z"] + 0.5), (c1 - 0.1, z1 - 0.9)],
                            cv["v"][0] + 0.5, cv["v"][1] - 0.4, "hk_dk", 0.2))
    # outer skirt with bolted plates
    parts.append(K.pod("skirt", sk["v"][0], sk["v"][1], sk["u"][0], sk["u"][1], sk["z"][0], sk["z"][1], "hk_dk", chamfer=0.6, crown=0.3))
    for k in range(1, 5):
        ru = sk["u"][0] + (sk["u"][1] - sk["u"][0]) * k / 5
        parts.append(K.box("skirt_gap", sk["v"][0] + 0.1, sk["v"][1] + 0.03, ru - 0.12, ru + 0.12, sk["z"][0] + 0.3, sk["z"][1] + 0.03, "hk_dk"))
    parts.append(K.box("window", sk["v"][1] - 0.5, sk["v"][1] + 0.08, -2.6, 1.6, sk["z"][0] + 0.5, sk["z"][1] - 0.5, "window", 0.1))
    K.mirror_all(parts)

    # the hull between the pods: paler front deck, darker engine deck with grilles, open behind
    h0, h1 = hl["u"]
    hz0, hz1 = hl["z"]
    parts.append(K.box("hull", -hl["v"], hl["v"], h0, h1, hz0, hz1, "hk_grey", 0.3))
    parts.append(K.pod("deck_front", -hl["v"] + 0.4, hl["v"] - 0.4, 1.0, h1 - 0.3, hz1 - 0.2, hz1 + 0.5, "mid", chamfer=0.5, crown=0.25))
    parts.append(K.box("deck_rear", -hl["v"] + 0.3, hl["v"] - 0.3, h0 + 0.3, -1.4, hz1 - 0.2, hz1 + 0.3, "hk_grey", 0.12))
    for gv in (-2.6, -1.2, 0.2, 1.6):
        parts.append(K.seam("grille", gv, gv + 0.7, h0 + 0.8, h0 + 3.6, hz1 + 0.33, 0.3))
    parts.append(K.box("rear_plate", -hl["v"] + 0.2, hl["v"] - 0.2, h0 - 0.3, h0 + 0.4, hz0 + 0.3, hz1 - 0.3, "recess", 0.1))
    parts.append(K.box("front_plate", -hl["v"] + 0.3, hl["v"] - 0.3, h1 - 0.3, h1 + 0.5, hz0 + 0.4, hz1 - 0.5, "hk_dk", 0.15))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))

    # turret: a low house-colour horseshoe open to the front, the cyan canopy dome in its mouth, the gun under it
    import math
    tp_ = []
    rv, ru, tu0 = tu["rv"], tu["ru"], tu["u"]
    zb, zt = tu["z"]
    dm = P["dome"]

    def arc(r_v, r_u, a0, a1, n):
        return [(r_v * math.sin(math.radians(a0 + (a1 - a0) * k / n)), tu0 + r_u * math.cos(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]

    # the angle runs from the front (0) clockwise; the horseshoe covers 38..322 degrees
    shoe = arc(rv, ru, 26, 334, 22) + arc(rv - 3.0, ru - 3.0, 334, 26, 22)
    tp_.append(K.house(K.plan_prism("shoe", shoe[::-1], zb, zt, "HOUSE", 0.4), 0.42, 0.5))
    rim = arc(rv - 0.7, ru - 0.7, 32, 328, 22) + arc(rv - 2.2, ru - 2.2, 328, 32, 22)
    tp_.append(K.house(K.plan_prism("shoe_rim", rim[::-1], zt - 0.1, zt + 0.5, "HOUSE", 0.2), 0.75, 0.8))
    tp_.append(K.plan_prism("tur_floor", arc(rv - 2.6, ru - 2.6, 0, 360, 20)[:-1][::-1], zb, zt - 0.7, "steel_dk", 0.1))
    tp_.append(K.plan_prism("tur_neck", [(-2.6, tu0 + 1.0), (2.6, tu0 + 1.0), (2.1, tu0 + ru + 0.3), (-2.1, tu0 + ru + 0.3)], zb, zt + 0.6, "hk_dk", 0.25))
    tp_.append(K.ellipsoid("dome_rim", (0, dm["u"], dm["z"]), (dm["rv"] + 0.35, dm["ru"] + 0.35, 1.5), "recess"))
    tp_.append(K.ellipsoid("dome", (0, dm["u"], dm["z"]), (dm["rv"], dm["ru"], dm["h"]), "teal"))
    tp_.append(K.ellipsoid("dome_light", (0, dm["u"] + 0.5, dm["z"] + dm["h"] * 0.55), (dm["rv"] * 0.58, dm["ru"] * 0.6, dm["h"] * 0.5), "cyan"))
    tp_.append(K.box("dome_frame", -0.16, 0.16, dm["u"] - dm["ru"] - 0.1, dm["u"] + dm["ru"] + 0.1, dm["z"], dm["z"] + dm["h"] + 0.12, "glass"))
    tp_.append(K.box("tur_rear", -1.6, 1.6, dm["u"] - dm["ru"] - 1.0, dm["u"] - dm["ru"] + 1.6, zt - 0.2, dm["z"] + dm["h"] + 0.5, "steel_dk", 0.3))
    t1 = tu0 + ru
    gp = []
    K.barrel(gp, t1 - 1.2, gn["tip"], gn["z"], gn["r"], material="hk_gun", muzzle=0.2, collar="steel_dk", segs=14)
    tur = K.node(tp_, "turret", (0, 0, zb), root, (tu["dv"], tu["du"], 0))
    gun = K.node(gp, "barrel", (0, t1 - 1.2, gn["z"]), tur, (tu["dv"], tu["du"], 0))
    K.anchor("fx_muzzle_0", (tu["dv"], gn["tip"] + tu["du"], gn["z"]), gun)
    return root


if __name__ == "__main__":
    K.main(KEY, "ordos_combat_tank", build, P)
