"""Ornithopter - the ORIGINAL design rebuilt as a modern hard-surface model, with its three wing phases (kit:
atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/atreides_ornithopter.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=ornithopter .venv/bin/python -I tools/models/vehicle_check/pack.py         (the 2D pack)

Templates.bin ORNITHOPTER: owner mask 127 (all houses), unit art 17 = DATA.R16 frames 2190-2285: THREE animation
rows of 32 facings - the wing beat: row 0 wings swung forward and up (a V seen from behind: the tips 12 px ahead
of and 11 px above the roots), row 1 straight out and level, row 2 swung back (6 px) and a little down (solved from
the north / east / south frames). No barrel art, no shadow pixels (the game draws a flyer's shadow itself). Model key ORNITHOPTER.
The main model has the wings level (= row 1); variants/ORNITHOPTER_anim00/01/02.glb are the three rows, for the 2D pack
(vehicle_check/pack.py) and for Graphics: 3D (tools/models/export_poses.py writes them into a pack as
variants/ORNITHOPTER_row00.glb + ORNITHOPTER_row02.glb, geometry only; the game shows the row the 2D frame would: docs/3d-models.md "Animation").

How the original reads: a dragonfly - a small brown thorax and dark head (a cyan eye), two long pale peach wings
from its top (19 px each, 5 px wide, lighter towards the tips), two short hind winglets, a long thin
house-colour tail boom (15 % of the sprite) ending in small peach tail fins round a dark tip.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.26, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.45, contour_azimuth=-45.0, sharpen=0.6,
              tone=((0.0, 0.0), (0.12, 0.14), (0.25, 0.29), (0.5, 0.55), (0.75, 0.81), (1.0, 1.0)))
KEY = "ORNITHOPTER"

P = dict(
    zc=5.0,
    thorax=dict(u=2.0, r=(3.0, 4.6, 2.8)),
    head=dict(u=6.2, r=(2.1, 2.2, 1.8)),
    boom=dict(u=(-16.4, 0.0), r=(1.15, 1.5)),
    tail=dict(u=(-23.8, -18.0), v=5.0),
    wing=dict(root=(1.0, 0.6), len=19.2, chord=(4.4, 6.2, 4.8), sweep=0.6, t=0.5, dz=1.5),
    hind=dict(root=(1.2, -2.4), len=6.6, chord=3.3),
    # where the right wing points (v, u, z) in the art's three animation rows: forward and up, straight out, back and a little down
    phases=((9.5, 12.0, 10.6), (1.0, 0.0, 0.0), (18.5, -6.0, -2.5)),
)
COL = dict(wing=(0.820, 0.700, 0.600), wing_dk=(0.640, 0.530, 0.450), chitin=(0.450, 0.350, 0.300), chitin_dk=(0.230, 0.170, 0.150))
SPEC = dict(wing=(0.0, 0.3, "paint"), wing_dk=(0.0, 0.45, "paint"), chitin=(0.1, 0.45, "paint"), chitin_dk=(0.2, 0.5, "dark"))
FILM = dict(wing=(0.740, 0.630, 0.540), wing_dk=(0.580, 0.480, 0.410), chitin=(0.420, 0.340, 0.290))
K.palette(COL, SPEC, FILM, weather=dict(paint=(0.5, 0.4, 0.8, 0.4, 0.3, 0.3, 0.2, 0.0)))


def wing_plate(name, root, length, chords, sweep, t, zc, material, sg):
    """A thin tapered wing along +v (sg = side): root (v, u), chords at root / middle / tip, swept back by `sweep`."""
    import bmesh
    n = 8
    rings = []
    for i in range(n + 1):
        f = i / n
        c = chords[0] + (chords[1] - chords[0]) * min(1.0, f * 2) if f < 0.5 else chords[1] + (chords[2] - chords[1]) * (f - 0.5) * 2
        if i == n:
            c *= 0.45
        v = sg * (root[0] + length * f)
        u = root[1] - sweep * f * f
        th = t * (1.0 - 0.5 * f)
        ring = [(v, u - c / 2, zc), (v, u, zc - th), (v, u + c / 2, zc), (v, u, zc + th)]
        rings.append(ring if sg > 0 else ring[::-1])
    bm = bmesh.new()
    K.rings_solid(bm, rings)
    return K.new_obj(name, bm, material)


def build():
    root = K.root(KEY)
    parts = []
    zc = P["zc"]
    th, hd, bo, tl, wg, hn = (P[k] for k in ("thorax", "head", "boom", "tail", "wing", "hind"))
    parts.append(K.ellipsoid("thorax", (0, th["u"], zc), th["r"], "chitin", segs=16, rings=10))
    parts.append(K.ellipsoid("head", (0, hd["u"], zc - 0.1), hd["r"], "chitin_dk", segs=14, rings=8))
    parts.append(K.ellipsoid("eye", (0, hd["u"] + 0.75, zc + 0.45), (1.15, 0.9, 0.8), "cyan", segs=12, rings=6))
    parts.append(K.box("hump", -1.0, 1.0, -0.6, 3.2, zc + 1.6, zc + 2.7, "chitin_dk", 0.4))
    parts.append(K.house(K.tube("boom", (0, bo["u"][0], zc), (0, bo["u"][1], zc), bo["r"][0], "HOUSE", segs=12, r1=bo["r"][1]), 0.8, 0.6))
    for ru in (-11.5, -7.5, -3.5):
        parts.append(K.cyl("boom_ring", 1.6, ru, ru + 0.5, 0, zc, "chitin", segs=12))
    t0, t1 = tl["u"]
    parts.append(K.cyl("tail_tip", 1.2, t0 + 0.6, bo["u"][0] + 0.2, 0, zc, "chitin_dk", segs=10, r1=0.8))
    for sg in (-1, 1):
        parts.append(K.plan_prism("tail_fin", [(sg * 0.5, t1 - 0.2), (sg * tl["v"], t0 + 0.4), (sg * tl["v"], t0 - 0.4), (sg * 0.5, t0 + 1.2)][::sg],
                                  zc - 0.2, zc + 0.2, "wing_dk", 0.05))
        parts.append(wing_plate("hind", hn["root"], hn["len"], (hn["chord"] * 0.8, hn["chord"], hn["chord"] * 0.7), 1.2, 0.2, zc + 0.6, "wing_dk", sg))
        w = wing_plate("wing", wg["root"], wg["len"], wg["chord"], wg["sweep"], wg["t"], zc + wg["dz"], "wing", sg)
        parts.append(K.group(w, "wing_R" if sg > 0 else "wing_L"))
        v_ = wing_plate("wing_vein", wg["root"], wg["len"] * 0.8, (0.5, 0.5, 0.3), wg["sweep"] * 0.64, wg["t"] + 0.12, zc + wg["dz"], "wing_dk", sg)
        parts.append(K.group(v_, "wing_R" if sg > 0 else "wing_L"))
    parts.append(K.plan_prism("tail_vfin", [(-0.22, t1), (0.22, t1), (0.22, t0 + 0.2), (-0.22, t0 + 0.2)], zc, zc + 2.0, "wing_dk", 0.05))
    body = K.node(parts, "body", (0, 0, 0), root)
    K.anchor("fx_muzzle_0", (0, hd["u"] + 1.6, zc - 0.6), body)
    return root


def phase(k):
    """Variant: the wings at the angle of animation row k."""
    def apply(rt):
        body = next(o for o in rt.children if o.name == "body")
        from mathutils import Matrix, Vector
        dv, du, dz = P["phases"][k]
        hinge = Vector((P["wing"]["root"][0], P["wing"]["root"][1], P["zc"] + P["wing"]["dz"])) * K.PX
        mats = {}
        for name, sg in (("wing_R", 1), ("wing_L", -1)):
            piv = Matrix.Translation(Vector((sg * hinge.x, hinge.y, hinge.z)))
            rot = Vector((sg, 0, 0)).rotation_difference(Vector((sg * dv, du, dz)).normalized()).to_matrix().to_4x4()
            mats[name] = piv @ rot @ piv.inverted()
        rest = K.pose(body, mats)
        return lambda: K.pose(body, {}, rest)
    return apply


if __name__ == "__main__":
    K.main(KEY, "ornithopter", build, P, variants={f"anim{k:02d}": phase(k) for k in range(3)})
