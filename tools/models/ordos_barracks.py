"""Ordos barracks - the original design rebuilt with tools/models/ordos_kit.py (round 2: the serpent is sculpted
along the original's own path, not a regular coil).

    blender -b --factory-startup --python tools/models/ordos_barracks.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_BARRACKS .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 39: main 3615, damaged 3616, no base, no animation; build-up 15 kept) - from the
ruler sheet at 12x:
  * a thick pale-gold serpent: its thin tail starts at the big glass orb at the south-west foot, the body climbs
    the west side getting thicker, makes one loop at the top that stands up towards the viewer (outer size about
    28 x 32 px on the picture, a dark eye in the middle with a pale-gold ball in it), passes its own body a second
    time in front and lower, and ends in a blunt head resting on the east shoulder (a cyan eye on it);
  * under it a dark brown root mound; short gold roots at the front with pale house-colour bands, darker roots
    with house bands on the east side; teal-black glass orbs with cyan glints at the feet (the big one at the
    tail, two on the east side), small cyan lamps;
  * orange sandstone rubble at the north-east.
The door (tools/models/barracks_door.py, redesign 2026-11-13): a trained soldier appears inside the footprint at px
(7, 2) from its centre and walks straight south. Graphics: 3D shows this model 7.5 px south of where it is authored
(its picture reaches north of the cells), so there he appears at model px (7, 9.5) - under the serpent's head. A
doorway with a gold lintel stands there (door plane y = 6, the hall behind it hollow; only 11 px high: the serpent's
head rests on its roof and hides the upper half of the 22 px tall soldier while he is inside), the root mound is
cut open for a paved path out to the south, the roots that ran across it were moved aside and the head lies a
little higher.
The serpent is a chain of short tube segments along a spline through hand-placed points with a radius that swells
and wobbles; round 1's regular 1.35-turn helix on a round trunk (IoU 0.77) is gone.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402
import barracks_door as BD  # noqa: E402

KEY = "ORDOS_BARRACKS"
P = dict(
    off=(2.0, 0.0),                                        # moves the whole building
    ring=(-9.0, 22.0, 31.0), ring_r=11.0, tilt=50.0,      # the loop: centre, radius of its centre line, how far it stands up
    tube=6.0,                                              # the body's radius at the loop
    drop=0.0, fwd=3.0, head=10.0, reach=0.9,                                     # the second pass runs this much lower / further south
    mound=(-6.0, 1.0), mound_rx=23.0, mound_ry=24.0, mound_h=8.0,
    door=(7.0, 6.0, -18.5, 6.0, 10.0, 11.0),      # the doorway: centre line x, door plane y, the path's south end, depth, width, height
)


def spline(pts, n):
    """Catmull-Rom through pts [(x, y, z, r)], n samples per span."""
    out = []
    q = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(q) - 2):
        p0, p1, p2, p3 = q[i - 1], q[i], q[i + 1], q[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3)
                             for j in range(4)))
    out.append(pts[-1])
    return out


def serpent():
    C, R, tl, tb = P["ring"], P["ring_r"], math.radians(P["tilt"]), P["tube"]
    ctrl = [(-19.0, -13.0, 4.0, 3.4), (-22.5, -5.0, 8.0, 4.4), (-24.0, 4.0, 14.0, 5.4), (-23.5, 11.0, 22.0, 6.2)]
    sweep0, sweep1 = -150.0, 270.0
    k_n = 15
    for k in range(k_n + 1):
        f = k / k_n
        ph = math.radians(sweep0 + (sweep1 - sweep0) * f)
        late = max(0.0, (f - 0.55) / 0.45)                 # the second pass: lower and further south
        ctrl.append((C[0] + R * math.cos(ph), C[1] + R * math.sin(ph) * math.cos(tl) - P["fwd"] * late,
                     C[2] + R * math.sin(ph) * math.sin(tl) - P["drop"] * late, tb * (1.0 - 0.06 * late)))
    last = ctrl[-1]
    hd = P["head"] / 10.0                              # how far the head reaches east
    ctrl += [(last[0] + 8.0 * hd, last[1] - 1.0, last[2] - 2.0, tb * 0.98), (last[0] + 14.5 * hd, last[1] - 3.0, last[2] - 3.6, tb * 1.05),
             (last[0] + 18.0 * hd, last[1] - 6.0, last[2] - 5.2, tb * 0.8)]              # (the head lies on the doorway's roof)
    return spline(ctrl, 3)


def build(damaged=False):
    main = []
    ox, oy = P["off"]
    rnd = random.Random(5)
    # ---- the root mound: a dark lumpy foot
    mc = (P["mound"][0] + ox, P["mound"][1] + oy)
    ring = [(mc[0] + math.cos(math.radians(a)) * P["mound_rx"] * (1 + 0.12 * math.sin(math.radians(a * 3 + 40))),
             mc[1] + math.sin(math.radians(a)) * P["mound_ry"] * (1 + 0.10 * math.cos(math.radians(a * 2)))) for a in range(0, 360, 24)]
    # the mound is cut open for the doorway and its path: a slot from the south rim up to behind the hall
    dx, dy, dpath, ddep, dwid, dhgt = P["door"]
    sx0, sx1, sy1 = dx - dwid / 2 - 4.6, dx + dwid / 2 + 4.6, dy + ddep + 1.6     # (the skirt widens ~3 px at the ground)
    slot, done = [], False
    for i, p_ in enumerate(ring):
        q_ = ring[(i + 1) % len(ring)]
        south = p_[1] < mc[1] and q_[1] < mc[1]
        if not (p_[1] < mc[1] and sx0 <= p_[0] <= sx1):
            slot.append(p_)
        if south and p_[0] < sx0 <= q_[0] and not done:
            t = (sx0 - p_[0]) / (q_[0] - p_[0])
            slot += [(sx0, p_[1] + (q_[1] - p_[1]) * t), (sx0, sy1), (sx1, sy1)]
            done = True
        if south and p_[0] < sx1 <= q_[0]:
            t = (sx1 - p_[0]) / (q_[0] - p_[0])
            slot.append((sx1, p_[1] + (q_[1] - p_[1]) * t))
    main.extend(K.skirt("mound", slot, P["mound_h"], 4, 3.0, material="block_dk", dark="stone_dk", lumps=0))
    ex = dx + dwid / 2 + 2.6                               # the mound's rest east of the path
    main.extend(K.skirt("mound_e", [(ex, -9.0 + oy), (ex + 3.5, -7.0 + oy), (ex + 6.0, 0.0 + oy), (ex + 5.0, 7.0 + oy), (ex, 7.0 + oy)], P["mound_h"] - 2.0, 9, 1.4,
                        material="block_dk", dark="stone_dk", lumps=5))
    import bpy
    lumps = K.skirt("mound_l", ring, P["mound_h"], 4, 3.0, material="block_dk", dark="stone_dk", lumps=22)
    bpy.data.objects.remove(lumps[0])                      # (only the rock lumps of the uncut rim, none in the slot)
    for o in lumps[1:]:
        cx_ = sum(v.co.x for v in o.data.vertices) / len(o.data.vertices) / K.V3.PX
        cy_ = sum(v.co.y for v in o.data.vertices) / len(o.data.vertices) / K.V3.PX
        if sx0 - 3.0 < cx_ < sx1 + 3.0 and cy_ < sy1 + 3.0:
            bpy.data.objects.remove(o)
        else:
            main.append(o)
    main.append(K.lathe("trunk", (mc[0] - 2.0, mc[1] + 8.0), [(7.2, 0.0), (7.0, 8.0), (6.4, 15.0), (5.0, 19.0)], "block_dk", 18))
    dmain, glow, danch = BD.door(K, "door", dx, dy, dpath, width=dwid, depth=ddep, height=dhgt, wall="block_dk", trim="roof", dark="stone_dk",
                                 bands=False, damaged=damaged)
    main.extend(dmain)
    # roots: gold at the front (pale house bands), darker on the east side (house bands), orbs at their feet
    roots = [((-5.0, 12.0, 15.0), (-5.6, 2.0, 9.0), (-5.0, -9.0, 2.0), "roof", 0.62, 0), ((11.0, 12.0, 15.0), (17.0, 4.0, 9.0), (16.5, -6.0, 2.0), "roof2", 0.34, 0),
             ((-10.0, 8.0, 14.0), (-10.0, -2.0, 8.0), (-9.0, -14.0, 2.0), "wall", 0.34, 0), ((10.0, 16.0, 14.0), (20.0, 12.0, 9.0), (27.0, 8.0, 3.0), "skirt", 0.34, 1),
             ((8.0, 22.0, 15.0), (18.0, 24.0, 10.0), (26.0, 24.0, 4.0), "skirt", 0.3, 1), ((9.0, 12.0, 13.0), (25.5, 5.0, 8.0), (27.0, -5.0, 2.0), "bronze", 0.34, 0)]             # (the two front roots stand left / right of the path)
    for i, (p0, p1, p2, mat, band, has_orb) in enumerate(roots):
        p0, p1, p2 = [(p[0] * P["reach"] + ox, 8.0 + (p[1] - 8.0) * P["reach"] + oy, p[2]) for p in (p0, p1, p2)]
        main.append(K.tube(f"root{i}_a", p0, p1, 3.4, mat, 10, 3.0))
        main.append(K.ball(f"root{i}_k", p1, 3.05, mat, 10))
        main.append(K.tube(f"root{i}_b", p1, p2, 3.0, mat, 10, 2.0))
        m0 = [p1[j] + (p2[j] - p1[j]) * 0.25 for j in range(3)]
        m1 = [p1[j] + (p2[j] - p1[j]) * 0.5 for j in range(3)]
        main.append(K.house(K.tube(f"root{i}_band", m0, m1, 3.0, "HOUSE", 10), band))
        if has_orb:
            main.extend(K.orb(f"root{i}_orb", (p2[0] + 1.0, p2[1], p2[2] + 2.6), 3.6))
    # ---- the serpent
    pts = [(p[0] + ox, p[1] + oy, p[2], p[3] * (1.0 + 0.05 * math.sin(i * 1.7) + 0.03 * rnd.uniform(-1, 1))) for i, p in enumerate(serpent())]
    n = len(pts)
    for k in range(n - 1):
        a, b = pts[k], pts[k + 1]
        mat = "serp_hi" if k % 4 == 1 else "serp"
        main.append(K.tube(f"body{k}", a[:3], b[:3], a[3], mat, 14, b[3]))
        main.append(K.ball(f"body_j{k}", a[:3], a[3] * 1.003, mat, 14))
        if k % 9 == 4:                                     # a few thin bronze seams
            main.append(K.tube(f"body_seam{k}", a[:3], [a[j] + (b[j] - a[j]) * 0.18 for j in range(3)], a[3] + 0.25, "rim", 14))
    head = pts[-1]
    main.append(K.ball("head", head[:3], head[3] * 1.25, "serp_hi", 14))
    main.extend(K.orb("head_eye", (head[0] - 5.0, head[1] - 5.5, head[2] + 5.0), 1.9, collar=False))
    tail = pts[0]
    main.extend(K.orb("tail_orb", (tail[0] + 1.5, tail[1] - 2.0, 5.2), 5.4))
    for i, k in enumerate((10, 22)):                      # pale house patches on the body
        p = pts[k]
        main.append(K.house(K.ball(f"body_patch{i}", (p[0] + 1.5, p[1] - p[3] * 0.55, p[2] + p[3] * 0.5), p[3] * 0.5, "HOUSE", 10), 0.72))
    C = P["ring"]
    main.append(K.ball("eye_dark", (C[0] + ox, C[1] + oy + 3.0, C[2] - 2.0), P["ring_r"] - 2.0, "block_dk", 14))
    main.append(K.ball("eye_ball", (C[0] + ox, C[1] + oy - 1.0, C[2] + 1.0), 3.6, "white", 12))
    # small lamps, rubble
    for i, (x, y, z) in enumerate(((8.0, 8.0, 12.5), (-14.0, -2.0, 9.0), (2.0, 20.0, 18.0))):
        main.append(K.ball(f"lamp{i}", (x + ox, y + oy, z), 1.2, "cyan", 8))
    main.extend(K.rubble("rub", (20.0 + ox, 47.0 + oy), 8.0, 3, 6))
    main.extend(K.rubble("rub_s", (-6.0 + ox, -22.0 + oy), 3.0, 5, 3))
    if damaged:
        for i, k in enumerate((14, 30, 44)):
            p = pts[min(k, n - 1)]
            K.scorch(main, (p[0], p[1]), p[3] * 0.7, p[2] + p[3] * 0.86, 30 + i, 4)
        for i, k in enumerate((20, 38)):                  # torn plates of the body, still attached
            p = pts[min(k, n - 1)]
            main.append(K.ball(f"body_hole{i}", (p[0] + 1.0, p[1] - 2.0, p[2] + p[3] * 0.55), p[3] * 0.62, "burnt", 10))
            for j in range(3):
                main.append(K.beam(f"body_tear{i}_{j}", (p[0] + 1.0 + 2.0 * (j - 1), p[1] - 2.0, p[2] + p[3] * 0.9),
                                   (p[0] + 3.0 * (j - 1), p[1] - 4.5, p[2] + p[3] * 1.25), 1.6, 0.35, "roof2"))
        K.wall_gash(main, (mc[0] + 4.0, mc[1] - P["mound_ry"] + 2.0), 0.0, 6.0, 3.0, 10.0, 5, 1.0)
    return dict(nodes=dict(main=main, glow=glow), anchors=danch)


if __name__ == "__main__":
    BD.install(K, path=(0.760, 0.720, 0.640))
    K.run(KEY, build, spec=dict(flags=K.flags(-0.15, shadow_azimuth=30.0)), P=P)
