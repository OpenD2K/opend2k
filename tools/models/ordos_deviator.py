"""Deviator - the ORIGINAL design rebuilt as a modern hard-surface model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/ordos_deviator.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=ordos_deviator .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin DEVIATOR: owner mask 4 (Ordos), unit art 23 = DATA.R16 frames 2656-2687, no barrel art (the launcher
is part of the body frames and never turns on its own); model key DEVIATOR, anchors fx_muzzle_0..5.

How the original reads (the cardinal views, tools/models/vehicle_check):
  * a boxy pink-peach hull 22 px long and 19 wide with a sloped front plate; the tracks only show as dark corners
    under its skirts;
  * over the rear half a tall brown launcher box overhanging the stern (13 px long, 10 wide, its top 15 px up),
    ribbed along its roof, 3 x 2 tubes in its front face with house-colour missile heads;
  * on the front right corner a rounded house-colour cab pod (10 px long, 6 wide) with a cyan lamp - the vehicle is
    asymmetric; the front left is a plain deck with panel lines.
House colour 15.8 % of the sprite (the ramp's light half); a fifth of the sprite is brighter than 200.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.42, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.65, contour_azimuth=-45.0, sharpen=0.7,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.27), (0.5, 0.54), (0.75, 0.80), (1.0, 1.0)))
KEY = "DEVIATOR"
K.PREVIEW_HOUSE = (0.20, 0.55, 0.35)

P = dict(
    v0=-0.7,
    track=dict(v=(4.6, 9.0), u=(-10.0, 12.0), z=4.2),
    hull=dict(v=9.6, u=(-9.0, 12.4), z=(1.6, 6.2), slope=5.0),
    rack=dict(v=(-5.2, 4.8), u=(-14.4, 0.2), z=(6.4, 13.4), pitch=8.0),
    cab=dict(v=(3.6, 9.9), u=(3.4, 13.6), z=(1.2, 8.8)),
)
COL = dict(pk=(0.930, 0.760, 0.660), pk_md=(0.720, 0.540, 0.460), pk_dk=(0.420, 0.290, 0.240), pk_lt=(0.990, 0.930, 0.850))
SPEC = dict(pk=(0.0, 0.4, "paint"), pk_md=(0.0, 0.5, "paint"), pk_dk=(0.0, 0.6, "paint"), pk_lt=(0.0, 0.35, "paint"))
FILM = dict(pk=(0.760, 0.620, 0.530), pk_md=(0.600, 0.460, 0.390), pk_dk=(0.380, 0.270, 0.220), pk_lt=(0.800, 0.730, 0.650))
K.palette(COL, SPEC, FILM)


def build():
    root = K.root(KEY)
    parts = []
    tr, hl, rk, cb = (P[k] for k in ("track", "hull", "rack", "cab"))
    u0, u1 = tr["u"]
    prof = [(u0 + 1.8, 0.0), (u1 - 1.8, 0.0), (u1 - 0.2, 1.5), (u1, 2.6), (u1 - 1.0, tr["z"]), (u0 + 1.0, tr["z"]), (u0, 2.6), (u0 + 0.2, 1.5)]
    K.track(parts, prof, tr["v"][0], tr["v"][1], wheels=((u0 + 2.2, 2.2, 1.8), (u1 - 2.2, 2.2, 1.8)))
    K.mirror_all(parts)
    h0, h1 = hl["u"]
    z0, z1 = hl["z"]
    hv = hl["v"]
    # the hull: a box with a sloped front plate, skirts over the tracks
    parts.append(K.prism_uz("hull", [(h0, z0), (h1 - 0.6, z0), (h1, z0 + 1.2), (h1 - hl["slope"], z1), (h0, z1)], -hv, hv, "pk", 0.35))
    for sg in (-1, 1):
        a_, b_ = sorted((sg * (hv - 0.3), sg * (hv + 0.25)))
        parts.append(K.box("skirt", a_, b_, h0 + 0.8, h1 - hl["slope"] - 0.6, z0 + 0.5, z1 - 0.9, "pk_md", 0.15))
        for k in range(1, 4):
            ru = h0 + 0.8 + (h1 - hl["slope"] - h0 - 1.4) * k / 4
            parts.append(K.box("skirt_gap", a_ - 0.03, b_ + 0.03, ru - 0.12, ru + 0.12, z0 + 0.6, z1 - 1.0, "pk_dk"))
    # front left deck: a pale plate with panel lines; a paler strip across the glacis
    parts.append(K.pod("deck", -hv + 0.7, cb["v"][0] - 0.5, 0.2, h1 - hl["slope"] - 0.3, z1 - 0.2, z1 + 0.45, "pk_lt", chamfer=0.5, crown=0.25))
    for dv in (-6.2, -2.8):
        parts.append(K.seam("deck_seam", dv, dv + 0.25, 0.6, h1 - hl["slope"] - 0.7, z1 + 0.47, 0.3, "pk_dk"))
    parts.append(K.tube_z("hatch", 1.3, -4.4, 3.6, z1 + 0.3, z1 + 0.9, "pk_md", segs=12))
    sm = (z1 - z0 - 1.2) / hl["slope"]
    for k, uu in enumerate((h1 - 3.6, h1 - 1.8)):
        zz = z0 + 1.2 + (h1 - uu) * sm
        parts.append(K.box("glacis_rib", -hv + 0.8, cb["v"][0] - 0.6, uu - 0.3, uu + 0.3, zz - 0.3, zz + 0.3, "pk_md", 0.1))
    parts.append(K.box("rear_plate", -hv + 0.8, hv - 0.8, h0 - 0.3, h0 + 0.3, z0 + 0.6, z1 - 0.6, "pk_dk", 0.1))
    # the launcher: a pedestal and the pitched box with 3 x 2 tubes, ribs along its roof
    r0, r1 = rk["u"]
    rz0, rz1 = rk["z"]
    rv0, rv1 = rk["v"]
    parts.append(K.box("pedestal", rv0 + 1.2, rv1 - 1.2, h0 + 0.6, r1 - 1.6, z1 - 0.2, rz0 + 1.2, "pk_dk", 0.25))
    made = K.rocket_rack(parts, r0, r1, rv0, rv1, rz0, rz1, 3, 2, pitch_deg=rk["pitch"], case="pk_md", tube_mat="recess", tip="HOUSE")
    for o in made:
        if o.name.startswith("rack_tip"):
            K.house(o, 1.0, 0.8)
    import math
    from mathutils import Matrix, Vector
    m = Matrix.Translation(Vector((0, r0, rz0))) @ Matrix.Rotation(math.radians(rk["pitch"]), 4, "X") @ Matrix.Translation(Vector((0, -r0, -rz0)))
    extra = []
    n = 7
    for k in range(n):       # roof ribs along the launcher: alternating pale and dark strips
        a_ = rv0 + 0.4 + (rv1 - rv0 - 0.8) * k / n
        b_ = a_ + (rv1 - rv0 - 0.8) / n
        extra.append(K.box("rack_rib", a_ + 0.12, b_ - 0.12, r0 + 0.5, r1 - 0.5, rz1 - 0.1, rz1 + (0.45 if k % 2 == 0 else 0.15),
                           "pk" if k % 2 == 0 else "pk_dk", 0.1))
    for uu in (r0 + 3.2, r0 + 6.4, r0 + 9.6):
        for sg, vv in ((-1, rv0), (1, rv1)):
            extra.append(K.box("rack_band", vv - 0.12, vv + 0.12, uu - 0.35, uu + 0.35, rz0 + 0.3, rz1 + 0.2, "pk_dk"))
    extra.append(K.box("rack_back", rv0 + 0.6, rv1 - 0.6, r0 - 0.25, r0 + 0.3, rz0 + 0.7, rz1 - 0.7, "pk_dk", 0.1))
    for col in range(3):     # the missile heads fill the tube mouths (the original's green dots)
        for row in range(2):
            hv_, hz_ = rv0 + (rv1 - rv0) * (col + 0.5) / 3, rz0 + (rz1 - rz0) * (row + 0.5) / 2
            extra.append(K.house(K.cyl("rack_head", 1.2, r1 - 0.2, r1 + 0.3, hv_, hz_, "HOUSE", segs=12, bev=0.1), 1.0, 0.8))
    for o in extra:
        o.data.transform(m)
    parts.extend(extra)
    # the cab pod on the front right corner
    c0, c1 = cb["u"]
    parts.append(K.house(K.pod("cab", cb["v"][0], cb["v"][1], c0, c1, cb["z"][0], cb["z"][1], "HOUSE", chamfer=1.7, crown=1.5, bev=0.5), 1.0, 0.55))
    parts.append(K.box("cab_window", cb["v"][0] + 1.3, cb["v"][1] - 1.3, c1 - 0.25, c1 + 0.12, cb["z"][0] + 3.6, cb["z"][1] - 2.0, "glass", 0.1))
    parts.append(K.box("cab_lamp", cb["v"][1] - 0.25, cb["v"][1] + 0.12, c1 - 3.4, c1 - 2.0, cb["z"][0] + 3.2, cb["z"][0] + 4.4, "cyan", 0.05))
    parts.append(K.seam("cab_seam", cb["v"][0] + 0.9, cb["v"][1] - 0.9, c0 + 3.0, c0 + 3.25, cb["z"][1] + 0.02, 0.4, "recess"))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    cw, ch = (rv1 - rv0) / 3, (rz1 - rz0) / 2
    i = 0
    for row in range(2):
        for col in range(3):
            K.anchor(f"fx_muzzle_{i}", tuple(m @ Vector((rv0 + cw * (col + 0.5) + P["v0"], r1, rz0 + ch * (row + 0.5)))), body)
            i += 1
    return root


if __name__ == "__main__":
    K.main(KEY, "ordos_deviator", build, P)
