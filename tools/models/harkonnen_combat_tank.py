"""Harkonnen combat tank - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py,
the method of combat_tank_v3.py / devastator_v1.py).

    blender -b --factory-startup --python tools/models/harkonnen_combat_tank.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=harkonnen_combat_tank .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin HARKONNEN COMBAT TANK: owner mask 10 (Harkonnen, Emperor), unit art 18 = DATA.R16 frames 2286-2317,
barrel art 20 = frames 2350-2381 (the turret, 32 facings); model key HARKONNEN_COMBAT_TANK, nodes body + turret
+ barrel, anchor fx_muzzle_0.

How the original reads (the cardinal views, tools/models/vehicle_check):
  * an H in plan, 28 px long and 21 wide: two heavy track pods joined by a lower grey-beige hull, the rear gap
    between the pods open;
  * each pod carries a white armour cover over its middle 16 px with a house-colour band across both ends; behind
    the rear band a grey cover slopes down to the black track end, in front of the front band the track nose shows;
  * a lower grey skirt along the outside of each pod;
  * the turret is a squat rounded block on a dark base ring, pale on top with a dark house-colour block on its
    front half, a cyan lamp low on its right cheek, and a short thin gun (9 px past the turret, 2 px thick).
House colour 8.7 % of the body, 15.5 % of the turret (the turret's is the ramp's dark half).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.36, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_turret=0.85, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.27), (0.5, 0.54), (0.75, 0.80), (1.0, 1.0)))
KEY = "HARKONNEN_COMBAT_TANK"
K.PREVIEW_HOUSE = (0.78, 0.22, 0.20)

P = dict(
    v0=0.0,
    track=dict(v=(3.7, 9.4), u=(-14.4, 13.4), z=4.7),
    cover=dict(v=(3.3, 8.3), u=(-8.6, 7.8), z=(4.2, 7.3)),
    band=2.3,
    skirt=dict(v=(8.0, 9.9), u=(-9.4, 8.6), z=(2.9, 5.7)),
    slope=dict(u=-13.9, z=4.3),
    hull=dict(v=3.8, u=(-8.8, 8.6), z=(1.8, 5.7)),
    turret=dict(v=4.6, u=(-5.6, 5.4), z=(6.0, 7.5, 12.0), du=-0.5, dv=0.0),
    block=dict(v=2.5, u=(-0.6, 4.6), z=13.0),
    gun=dict(tip=14.7, z=9.3, r=1.35),
)
COL = dict(hk_white=(0.960, 0.930, 0.870), hk_grey=(0.640, 0.590, 0.530), hk_dk=(0.400, 0.360, 0.320), hk_gun=(0.560, 0.550, 0.560),
           rust=(0.520, 0.300, 0.160))
SPEC = dict(hk_white=(0.0, 0.34, "paint"), hk_grey=(0.0, 0.5, "paint"), hk_dk=(0.0, 0.6, "paint"), hk_gun=(0.5, 0.35, "steel"),
            rust=(0.0, 0.7, "paint"))
FILM = dict(hk_white=(0.780, 0.730, 0.660), hk_grey=(0.560, 0.510, 0.450), hk_dk=(0.360, 0.320, 0.280), hk_gun=(0.500, 0.490, 0.480))
K.palette(COL, SPEC, FILM)


def build():
    root = K.root(KEY)
    parts = []
    tr, cv, sk, hl, tu, bl, gn = (P[k] for k in ("track", "cover", "skirt", "hull", "turret", "block", "gun"))
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
        parts.append(K.house(K.pod("band", cv["v"][0] - 0.15, cv["v"][1] + 0.15, a_, b_, z0 - 0.4, z1 + 0.15, "HOUSE", chamfer=0.3, crown=0.3), 1.0, 0.75))
    # the grey cover sloping down to the rear track end, a short dark nose in front
    sl = P["slope"]
    parts.append(K.prism_uz("rear_slope", [(sl["u"], sl["z"] - 1.2), (c0 + 0.1, z0 - 0.6), (c0 + 0.1, z1 - 0.2), (sl["u"] + 0.6, sl["z"])],
                            tr["v"][0] - 0.25, tr["v"][1] + 0.1, "hk_grey", 0.35))
    parts.append(K.seam("slope_seam", cv["v"][0] + 0.5, cv["v"][1] - 0.4, (sl["u"] + c0) / 2 - 0.15, (sl["u"] + c0) / 2 + 0.15, (sl["z"] + z1) / 2 + 0.1, 0.5, "hk_dk"))
    parts.append(K.prism_uz("nose", [(c1 - 0.1, z0 - 0.6), (u1 - 1.6, tr["z"] - 0.4), (u1 - 1.8, tr["z"] + 0.5), (c1 - 0.1, z1 - 0.9)],
                            cv["v"][0] + 0.5, cv["v"][1] - 0.4, "hk_dk", 0.2))
    # outer skirt with bolted plates
    parts.append(K.pod("skirt", sk["v"][0], sk["v"][1], sk["u"][0], sk["u"][1], sk["z"][0], sk["z"][1], "hk_grey", chamfer=0.6, crown=0.3))
    for k in range(1, 5):
        ru = sk["u"][0] + (sk["u"][1] - sk["u"][0]) * k / 5
        parts.append(K.box("skirt_gap", sk["v"][0] + 0.1, sk["v"][1] + 0.03, ru - 0.12, ru + 0.12, sk["z"][0] + 0.3, sk["z"][1] + 0.03, "hk_dk"))
    parts.append(K.box("rust_strip", sk["v"][1] - 0.5, sk["v"][1] + 0.05, 1.8, 2.8, sk["z"][0] + 0.2, sk["z"][1] - 0.2, "rust"))
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

    # turret: dark base ring, pale rounded block, dark house block on the front half, lamp, short gun
    tp_ = []
    t0, t1 = tu["u"]
    tv = tu["v"]
    zb, zm, zt = tu["z"]
    base = [(-tv + 1.4, t0), (tv - 1.4, t0), (tv, t0 + 1.6), (tv, t1 - 1.6), (tv - 1.4, t1), (-tv + 1.4, t1), (-tv, t1 - 1.6), (-tv, t0 + 1.6)]
    tp_.append(K.plan_prism("tur_base", base, zb, zm + 0.2, "steel_dk", 0.2))
    up = [(v * 0.95, u * 0.96 + (t0 + t1) * 0.02) for (v, u) in base]
    tp_.append(K.pillow("tur_top", up, zm, zt, "hk_white", edge=0.9, drop=1.2, bev=0.3))
    tp_.append(K.house(K.pod("tur_block", -bl["v"], bl["v"], bl["u"][0], bl["u"][1], zt - 0.6, bl["z"], "HOUSE", chamfer=0.5, crown=0.3), 0.3, 0.6))
    tp_.append(K.seam("tur_seam", -tv + 1.0, tv - 1.0, bl["u"][0] - 1.2, bl["u"][0] - 0.95, zt + 0.02, 0.3, "hk_dk"))
    tp_.append(K.tube_z("tur_hatch", 1.25, -1.6, t0 + 2.6, zt - 0.2, zt + 0.45, "hk_grey", segs=12))
    tp_.append(K.pod("tur_cheek", tv - 0.6, tv + 1.7, t1 - 5.2, t1 - 0.9, zm - 0.6, zm + 2.3, "hk_grey", chamfer=0.5, crown=0.3))
    tp_.append(K.box("tur_lamp", tv + 0.1, tv + 1.4, t1 - 1.0, t1 - 0.6, zm - 0.1, zm + 1.5, "cyan", 0.1))
    tp_.append(K.box("tur_mantlet", -1.7, 1.7, t1 - 0.6, t1 + 0.7, gn["z"] - 1.5, gn["z"] + 1.4, "steel_dk", 0.25))
    gp = []
    K.barrel(gp, t1 + 0.4, gn["tip"], gn["z"], gn["r"], material="hk_gun", muzzle=0.2, collar="steel_dk", segs=14)
    tur = K.node(tp_, "turret", (0, 0, zb), root, (tu["dv"], tu["du"], 0))
    gun = K.node(gp, "barrel", (0, t1 + 0.4, gn["z"]), tur, (tu["dv"], tu["du"], 0))
    K.anchor("fx_muzzle_0", (tu["dv"], gn["tip"] + tu["du"], gn["z"]), gun)
    return root


if __name__ == "__main__":
    K.main(KEY, "harkonnen_combat_tank", build, P)
