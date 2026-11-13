"""Ordos wind trap - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_wind_trap.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_WIND_TRAP .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 38: main 3612, damaged 3613, animation 12 = 10 overlay frames: the cyan louvres
pulse; build-up 10 kept):
  * two tall brass "clams" - half domes standing one behind the other on the south-west / north-east diagonal,
    their flat mouths facing south-east: each mouth a dark leaning face with three cyan light louvres (longest at
    the bottom) inside a gold arch; ribbed gold shells with house-colour panels on their west flanks, rivets
    along the arch;
  * at the south-east foot a small brass pump box with a pale pipe, a bronze junction between the clams.
Animation: the louvres' glow runs upwards (overlay frames made from the idle render, no extra geometry).
"""
import math
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_WIND_TRAP"
P = dict(
    face=-56.0,                         # the mouths face south-east
    c_n=(4.5, 43.0), r_n=19.5, h_n=32.0,     # the north clam: centre of its chord on the ground, radius, height
    c_s=(-10.5, 7.5), r_s=19.5, h_s=33.0,     # the south clam
    cut=0.8, lean=0.2, slant=0.0,                  # the mouth: a plane at cut * r in front of the centre, leaning back by lean per px of height
    pump=(14.0, -8.0),
)


def clam(main, tag, c, R, h, damaged):
    fa = math.radians(P["face"])
    fx, fy = math.cos(fa), math.sin(fa)            # the mouth's normal
    sx, sy = -fy, fx                               # along the chord (towards the north-east)
    n, steps = 24, 9
    rings, ends = [], []
    for k in range(steps + 1):
        t = math.pi / 2 * min(k, steps - 0.25) / steps
        r, z = R * math.cos(t) ** 0.8, h * math.sin(t)
        off = P["cut"] * R - P["lean"] * z         # the mouth's plane: in front of the centre, leaning back
        al = math.acos(max(-0.999, min(0.999, off / max(r, 1e-3))))
        ring = []
        for i in range(n + 1):
            a = fa + al + (2 * math.pi - 2 * al) * i / n
            ring.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a), z))
        rings.append(ring)
        ends.append((ring[0], ring[-1]))
    bm = bmesh.new()
    K.rings_solid(bm, rings)
    shell = K.new_obj(f"{tag}_shell", bm, "wall")
    main.append(shell)
    # the dark mouth, proud of the cut by a hair
    bm = bmesh.new()
    vs = [(bm.verts.new((a[0] + fx * 0.25, a[1] + fy * 0.25, a[2])), bm.verts.new((b[0] + fx * 0.25, b[1] + fy * 0.25, b[2]))) for (a, b) in ends]
    for (a0, b0), (a1, b1) in zip(vs, vs[1:]):
        bm.faces.new([b0, a0, a1, b1])
    main.append(K.new_obj(f"{tag}_mouth", bm, "block"))
    # the gold arch round the mouth with rivets, a bronze sill
    for k in range(steps):
        for s in (0, 1):
            p, q = ends[k][s], ends[k + 1][s]
            main.append(K.tube(f"{tag}_arch{k}_{s}", (p[0] + fx * 0.3, p[1] + fy * 0.3, p[2]), (q[0] + fx * 0.3, q[1] + fy * 0.3, q[2]), 1.25, "roof", 8))
            if k % 2 == 0:
                main.append(K.ball(f"{tag}_rivet{k}_{s}", (p[0] + fx * 1.3, p[1] + fy * 1.3, p[2] + 0.6), 0.75, "bronze_dk", 6))
    a, b = ends[0]
    main.append(K.beam(f"{tag}_sill", (a[0] + fx, a[1] + fy, 1.0), (b[0] + fx, b[1] + fy, 1.0), 2.4, 2.0, "rim", 0.2))
    # three cyan louvres
    def edge(side, tz):
        k = max(0.0, min(steps - 0.01, tz * steps))
        k0 = int(k)
        return [ends[k0][side][j] + (ends[k0 + 1][side][j] - ends[k0][side][j]) * (k - k0) for j in range(3)]
    for i, tz in enumerate((0.20, 0.42, 0.64)):       # they slant: the north-east end lower than the south-west end
        A, B = edge(0, tz - P["slant"]), edge(1, tz + P["slant"])
        p0 = [A[j] + (B[j] - A[j]) * 0.14 for j in range(3)]
        p1 = [A[j] + (B[j] - A[j]) * 0.86 for j in range(3)]
        main.append(K.beam(f"{tag}_slat{i}", (p0[0] + fx * 0.5, p0[1] + fy * 0.5, p0[2] - 2.2), (p1[0] + fx * 0.5, p1[1] + fy * 0.5, p1[2] - 2.6), 1.2, 0.6, "rib"))
        main.append(K.beam(f"{tag}_glow{i}", (p0[0] + fx * 0.7, p0[1] + fy * 0.7, p0[2]), (p1[0] + fx * 0.7, p1[1] + fy * 0.7, p1[2]), 3.4, 0.7, "cyan"))

    def zf(x, y):
        d2 = ((x - c[0]) ** 2 + (y - c[1]) ** 2) / (R * R)
        return h * math.sqrt(max(0.02, 1 - d2 ** 0.8))
    # ribs over the shell (meridians) and two hoops
    for i in range(2, n - 1, 3):
        for k in range(steps - 1):
            p, q = rings[k][i], rings[k + 1][i]
            main.append(K.beam(f"{tag}_rib{i}_{k}", p, q, 1.1, 0.5, "rim"))
    for k in (3, 6):
        for i in range(n):
            main.append(K.beam(f"{tag}_hoop{k}_{i}", rings[k][i], rings[k][i + 1], 0.9, 0.45, "rib"))
    for (i0, k0) in ((n - 6, 3), (n - 8, 4), (n - 5, 5)):       # the hard pale highlight beside the mouth's upper-left edge
        q = [rings[k0][i0], rings[k0][i0 + 2], rings[k0 + 2][i0 + 2], rings[k0 + 2][i0]]
        bm = bmesh.new()
        K.rings_solid(bm, [q, [(p[0] + (p[0] - c[0]) * 0.03, p[1] + (p[1] - c[1]) * 0.03, p[2] + 0.45) for p in q]])
        main.append(K.new_obj(f"{tag}_glint{i0}_{k0}", bm, "white"))
    # house-colour panels on the west flank (between ribs)
    for j, (i0, k0, sh) in enumerate(((15, 1, 0.26), (18, 3, 0.34), (20, 1, 0.26), (15, 5, 0.36), (12, 3, 0.3), (21, 5, 0.3))):
        q = [rings[k0][i0], rings[k0][i0 + 2], rings[k0 + 2][i0 + 2], rings[k0 + 2][i0]]
        m = [sum(p[a] for p in q) / 4 for a in range(3)]
        bm = bmesh.new()
        lo = [tuple(m[a] + (p[a] - m[a]) * 0.74 for a in range(3)) for p in q]
        out = [(p[0] + (p[0] - c[0]) * 0.035, p[1] + (p[1] - c[1]) * 0.035, p[2] + 0.5) for p in lo]
        K.rings_solid(bm, [lo, out])
        main.append(K.house(K.new_obj(f"{tag}_panel{j}", bm, "HOUSE"), sh))
    main.append(K.cyl(f"{tag}_foot", c, R + 1.2, 0.0, 1.6, "foot", 28))
    if damaged:
        px, py = c[0] - fx * R * 0.45 - sx * R * 0.2, c[1] - fy * R * 0.45 - sy * R * 0.2
        K.hole(main, [shell], (px, py), R * 0.3, zf, 3 if tag == "n" else 5, depth=6.0, cut_up=14.0)
        K.wall_gash(main, (c[0] + fx * 1.2 + sx * 3.0, c[1] + fy * 1.2 + sy * 3.0), P["face"] + 90.0, R * 0.3, 4.0, 13.0, 7 if tag == "n" else 8, 0.9)
        K.scorch(main, (c[0] - fx * R * 0.3 + sx * R * 0.4, c[1] - fy * R * 0.3 + sy * R * 0.4), 5.0, zf, 11, 4)


def build(damaged=False):
    main = []
    clam(main, "n", P["c_n"], P["r_n"], P["h_n"], damaged)
    clam(main, "s", P["c_s"], P["r_s"], P["h_s"], damaged)
    # the pump box at the south-east foot, its pale pipe, the junction between the clams
    p = P["pump"]
    main.append(K.obox("pump", p, 30.0, 5.4, 4.2, 0.0, 6.0, "wall", 0.5))
    main.append(K.obox("pump_lid", p, 30.0, 5.8, 4.6, 5.6, 6.8, "roof2", 0.3))
    main.append(K.obox("pump_cap", (p[0] - 1.0, p[1] + 0.5), 30.0, 2.6, 2.2, 7.2, 8.6, "white", 0.3))
    main.extend(K.pipe("pump_pipe", [(p[0] + 4.0, p[1] + 4.0, 5.0), (p[0] + 6.0, p[1] + 10.0, 5.0), (p[0] + 2.0, p[1] + 16.0, 4.0)], 1.3, "white"))
    main.extend(K.pipe("pump_pipe2", [(p[0] - 5.0, p[1] + 2.0, 3.0), (p[0] - 10.0, p[1] + 6.0, 3.0)], 1.3, "bronze"))
    main.extend(K.skirt("mound", [K.polar((13.0, 13.0), 10.0, 40.0 * i) for i in range(9)], 4.0, 5, 2.0, material="block_dk", dark="skirt", lumps=6))
    main.append(K.cyl("mound_top", (13.0, 13.0), 7.5, 3.6, 4.8, "block", 16))
    j = (19.0, 24.0)
    main.append(K.obox("junction", j, 40.0, 6.0, 4.5, 0.0, 7.0, "block", 0.5))
    main.append(K.obox("junction_lid", j, 40.0, 5.0, 3.6, 6.8, 8.2, "wall", 0.3))
    main.append(K.house(K.obox("junction_panel", (j[0] - 2.5, j[1] - 5.0), 40.0, 2.6, 0.5, 1.5, 5.5, "HOUSE"), 0.35))
    main.extend(K.orb("lamp", (j[0] + 5.0, j[1] + 3.0, 6.0), 1.6))
    main.extend(K.rubble("rub", (-22.0, -12.0), 3.0, 4, 3))
    if damaged:
        K.scorch(main, p, 4.0, 7.6, 21, 3)
    return dict(nodes=dict(main=main))


def glow(c, R, h):
    """The mouth's box on the model canvas (x, v = y + z)."""
    return dict(x0=c[0] - R * 0.6, x1=c[0] + R * 1.1, v0=c[1] - R * 0.75, v1=c[1] + R * 0.5 + h * 0.75, waves=1.0, lo=0.55, hi=1.35, channel=2, min_diff=40)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="glow", glow=[glow(P["c_s"], P["r_s"], P["h_s"]), dict(glow(P["c_n"], P["r_n"], P["h_n"]), phase=0.35)],
                                flags=K.flags(-0.22, shadow_azimuth=30.0)), P=P)
