"""Siege tank - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_siege_tank.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=siege_tank .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin SIEGE TANK: owner mask 127 (all houses), unit art 11 = DATA.R16 frames 1998-2029, barrel art 15 =
frames 2126-2157 (the turret, 32 facings); model key SIEGE_TANK, nodes body + turret (+ barrel).

How the original reads (visual hull of the 32 silhouettes + the cardinal views, tools/models/vehicle_check):
  * an H in plan: two long track pods (24 px, 3.5 px wide, 5.5-9 px off the centre line) reaching past a shorter
    hull at both ends; everything a pale stone grey-beige, a fifth of the sprite brighter than 200;
  * the pods' covers are ribbed across, a cyan lamp on each front tip;
  * the hull's front plate is the house colour (5 % of the body), its rear deck darker;
  * over the hull's rear a tall gun cradle: two round ribbed cheeks with a slot between them (body art);
  * the barrel art is the GUN alone, lying high in the cradle: a 22 px steel tube with a pale breech block and a
    house band behind it, a short tail sticking out to the rear; it turns about the vehicle's axis.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.5, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.6, contour_turret=0.8, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.13), (0.25, 0.27), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "SIEGE_TANK"

P = dict(
    v0=0.0,
    track=dict(v=(5.5, 9.3), u=(-13.0, 12.0), z=5.4),
    cover=dict(v=(5.2, 9.6), u=(-11.6, 10.8), z=(4.7, 7.0), n=5),
    hull=dict(v=5.4, u=(-10.0, 8.4), z=(1.8, 6.2)),
    cradle=dict(u=-5.6, r=4.7, z=8.8, v=(1.5, 5.1)),
    plate=dict(v=5.0, u=(4.6, 9.0), z=(4.0, 6.9)),
    gun=dict(u=(-14.2, 11.5), z=15.8, r=2.1, block=(-4.8, 0.8), bv=3.7, bh=2.6, pitch=14.0),
)
COL = dict(stone=(0.830, 0.795, 0.765), stone_dk=(0.470, 0.435, 0.410), stone_md=(0.640, 0.600, 0.570), barrel=(0.580, 0.580, 0.630))
SPEC = dict(stone=(0.0, 0.4, "paint"), stone_dk=(0.0, 0.55, "paint"), stone_md=(0.0, 0.5, "paint"), barrel=(0.5, 0.3, "steel"))
FILM = dict(stone=(0.700, 0.660, 0.630), stone_dk=(0.420, 0.390, 0.360), stone_md=(0.560, 0.520, 0.490), barrel=(0.500, 0.500, 0.540))
K.palette(COL, SPEC, FILM)


def full(half):
    """Right-half (v, u) plan, rear -> front -> closed counter-clockwise polygon."""
    return list(half) + [(-v, u) for (v, u) in reversed(half)]


def build():
    root = K.root(KEY)
    parts = []
    tr, cv, hl, pl, gn = (P[k] for k in ("track", "cover", "hull", "plate", "gun"))
    u0, u1 = tr["u"]
    prof = [(u0 + 1.8, 0.0), (u1 - 1.8, 0.0), (u1 - 0.2, 1.7), (u1, 3.0), (u1 - 1.0, tr["z"]), (u0 + 1.0, tr["z"]), (u0, 3.0), (u0 + 0.2, 1.7)]
    K.track(parts, prof, tr["v"][0], tr["v"][1], wheels=((u0 + 2.3, 2.6, 2.0), (u1 - 2.3, 2.6, 2.0)))
    # ribbed armour cover over the track: n crowned plates with dark gaps
    c0, c1 = cv["u"]
    step = (c1 - c0) / cv["n"]
    parts.append(K.pod("cover", cv["v"][0], cv["v"][1], c0, c1, cv["z"][0], cv["z"][1], "stone", chamfer=0.9, crown=0.5))
    for k in range(1, cv["n"]):                 # shallow ribs across the cover
        ru = c0 + k * step
        parts.append(K.box("cover_rib", cv["v"][0] + 0.25, cv["v"][1] - 0.25, ru - 0.5, ru + 0.5, cv["z"][1] - 0.15, cv["z"][1] + 0.4, "stone", 0.2))
        parts.append(K.box("cover_gap", cv["v"][0] - 0.03, cv["v"][1] + 0.03, ru + 0.55, ru + 0.8, cv["z"][0] + 0.6, cv["z"][1] - 0.25, "stone_dk"))
    parts.append(K.box("skirt", tr["v"][1] - 0.25, tr["v"][1] + 0.3, c0 + 1.5, c1 - 1.5, 2.9, cv["z"][0] + 0.2, "stone_dk", 0.15))
    parts.append(K.box("lamp", cv["v"][0] + 0.7, cv["v"][1] - 0.7, c1 - 0.2, c1 + 0.6, cv["z"][0] + 0.8, cv["z"][1] - 0.6, "cyan", 0.1))
    K.mirror_all(parts)
    parts.append(K.box("hull", -hl["v"], hl["v"], hl["u"][0], hl["u"][1], hl["z"][0], hl["z"][1], "stone_md", 0.4))
    parts.append(K.pod("deck_plate", -3.9, 3.9, -0.6, 4.2, hl["z"][1] - 0.2, hl["z"][1] + 0.7, "stone", chamfer=0.7, crown=0.3))
    parts.append(K.tube_z("hatch", 1.3, 1.6, 1.8, hl["z"][1] + 0.6, hl["z"][1] + 1.1, "stone_dk", segs=12))
    parts.append(K.box("deck_rear", -hl["v"] + 0.6, hl["v"] - 0.6, hl["u"][0] + 0.5, hl["u"][0] + 3.6, hl["z"][1] - 0.2, hl["z"][1] + 0.25, "stone_dk", 0.12))
    for gv in (-2.6, -0.9, 0.8, 2.5):
        parts.append(K.seam("grille", gv, gv + 0.5, hl["u"][0] + 0.9, hl["u"][0] + 3.2, hl["z"][1] + 0.28, 0.3))
    parts.append(K.house(K.prism_uz("plate", [(pl["u"][0], pl["z"][1]), (pl["u"][0], pl["z"][0]), (pl["u"][1], pl["z"][0]), (pl["u"][1], pl["z"][0] + 1.0)],
                                    -pl["v"], pl["v"], "HOUSE", 0.2), 1.0, 0.7))
    # the gun cradle over the hull's rear: two round ribbed cheeks with a slot between them
    cr = P["cradle"]
    import math
    prof = [(cr["u"] - cr["r"], hl["z"][1] - 0.2)] + [(cr["u"] - cr["r"] * math.cos(math.pi * k / 10), cr["z"] + cr["r"] * math.sin(math.pi * k / 10)) for k in range(11)] \
        + [(cr["u"] + cr["r"], hl["z"][1] - 0.2)]
    for sg in (-1, 1):
        a_, b_ = sorted((sg * cr["v"][0], sg * cr["v"][1]))
        parts.append(K.prism_uz("cheek", prof, a_, b_, "stone", 0.3))
        for k in (2, 4, 6, 8):                     # ribs round the cheek
            ang = math.pi * k / 10
            ru, rz = cr["u"] - (cr["r"] + 0.05) * math.cos(ang), cr["z"] + (cr["r"] + 0.05) * math.sin(ang)
            parts.append(K.box("cheek_rib", a_ - 0.04, b_ + 0.04, ru - 0.22, ru + 0.22, rz - 0.5, rz + 0.1, "stone_dk"))
    parts.append(K.box("cradle_floor", -cr["v"][0] - 0.1, cr["v"][0] + 0.1, cr["u"] - cr["r"] + 0.5, cr["u"] + cr["r"] - 0.5, hl["z"][1] - 0.2, cr["z"] + 0.6, "recess", 0.1))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))

    # the gun (the original's barrel art: it turns about the vehicle's axis in the cradle)
    tp_ = []
    g0, g1 = gn["u"]
    gz = gn["z"]
    b0, b1 = gn["block"]
    tp_.append(K.box("breech", -gn["bv"], gn["bv"], b0, b1, gz - gn["bh"], gz + gn["bh"], "stone_md", 0.45))
    tp_.append(K.house(K.box("band", -gn["bv"] - 0.12, gn["bv"] + 0.12, b0 - 1.5, b0 + 0.1, gz - gn["bh"] + 0.1, gz + gn["bh"] - 0.1, "HOUSE", 0.3), 0.9, 0.6))
    tp_.append(K.box("tail", -gn["bv"] + 0.2, gn["bv"] - 0.2, g0 + 3.0, b0 - 1.3, gz - gn["bh"] + 0.4, gz + gn["bh"] - 0.4, "stone_dk", 0.4))
    tp_.append(K.cyl("tail_cap", gn["r"] * 0.9, g0, g0 + 3.2, 0, gz, "barrel", segs=16, bev=0.1))
    tp_.append(K.seam("breech_seam", -gn["bv"] - 0.03, gn["bv"] + 0.03, (b0 + b1) / 2 - 0.15, (b0 + b1) / 2 + 0.15, gz + gn["bh"] + 0.03, 1.2))
    gp = []
    K.barrel(gp, b1 - 0.3, g1, gz, gn["r"], material="barrel", muzzle=0.25, collar="stone_dk", segs=20)
    gp.append(K.cyl("recoil", gn["r"] + 0.3, b1 + 0.3, b1 + 3.0, 0, gz, "barrel", segs=20, bev=0.1))
    from mathutils import Matrix, Vector
    mp = Matrix.Translation(Vector((0, 0, gz))) @ Matrix.Rotation(math.radians(gn["pitch"]), 4, "X") @ Matrix.Translation(Vector((0, 0, -gz)))
    for o in tp_ + gp:
        o.data.transform(mp)
    tur = K.node(tp_, "turret", (0, 0, gz), root)
    gun = K.node(gp, "barrel", tuple(mp @ Vector((0, b1, gz))), tur)
    K.anchor("fx_muzzle_0", tuple(mp @ Vector((0, g1, gz))), gun)
    return root


if __name__ == "__main__":
    K.main(KEY, "siege_tank", build, P)
