"""Atreides wind trap - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_wind_trap.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_WIND_TRAP .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3064 main, 3065 damaged, no base; animation 10 = 10 overlay frames, HAS_ANIMATION:
only the two mouths with the cyan louvres, their glow running; build-up 10 = the generic rock mound, kept):
  * two identical cream scoops (hoods with a pointed-arch section: the mouth (34 px wide, 29 high) is the largest section,
    the shell rounds off 32 px to the back) standing one behind the other on a
    diagonal, mouths to the south-east; each mouth is a dark arch with three cyan louvres, a copper rim, and a
    cream lip (ramp) on the ground in front of it;
  * house colour: a band over the crest at the back of each scoop and a stripe behind the mouth's rim;
  * east of them the copper machinery: two generator drums, an orange pipe loop, a lower pump block.
Animation: the louvres' glow runs upwards (overlay frames made from the idle render, no extra geometry).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_WIND_TRAP"
PT = 1.6        # the scoops' section: a pointed arch (atreides_kit.sweep point=)
P = dict(
    mouth_s=(-3.5, -0.5), mouth_n=(12.0, 37.0),     # midpoints of the mouths' base lines
    face_ang=37.0,                                   # direction of the base line (the mouth faces face_ang - 90)
    half=17.8, tall=1.776, deep=32.5, lip=12.0,      # the shell: the mouth (half width, height
)                                                    # half * tall) is its largest section, `deep` px to the back tip


def unit(ang):
    return math.cos(math.radians(ang)), math.sin(math.radians(ang))


def scoop(main, tag, m, damaged=False):
    fa = P["face_ang"]
    bx, by = unit(fa + 90.0)                # towards the back
    sx, sy = unit(fa)                       # along the base line
    R, sq, D = P["half"], P["tall"], P["deep"]
    H = R * sq

    def rad(t):                             # the section's half width at t (0 = mouth, 1 = back tip)
        return R * max(0.0, 1.0 - t * t) ** 0.31
    a = (m[0], m[1], 0.0)
    b = (m[0] + bx * D, m[1] + by * D, 0.0)
    n = 14
    prof = [(i / n, rad(i / n)) for i in range(n)] + [(1.0, 0.3)]
    # sweep's arc angle 0 = d x z = the north-east end of the base line, 180 = its south-west end
    shell = K.sweep(f"{tag}_shell", a, b, prof, "roof", 32, 0.0, sq, (0.0, 180.0), PT)
    main.append(shell)
    main.append(K.sweep(f"{tag}_foot", (m[0] - bx * 0.5, m[1] - by * 0.5, 0.0), (b[0] + bx * 1.0, b[1] + by * 1.0, 0.0),
                        [(t, r + 1.2) for (t, r) in prof], "foot", 32, 0.0, 2.4 / (R + 1.2), (0.0, 180.0), PT))
    # house colour: a band over the crest, a stripe behind the rim (top and the south-west flank)
    main.append(K.house(K.sweep(f"{tag}_crest", a, b, [(t, rad(t) + 0.3) for t in (0.66, 0.72, 0.78)], "HOUSE", 32, 0.0, sq, (55.0, 140.0), PT), 0.4))
    for i, arc in enumerate(((84.0, 128.0), (156.0, 180.0))):
        main.append(K.house(K.sweep(f"{tag}_stripe{i}", a, b, [(t, rad(t) + 0.3) for t in (0.07, 0.12, 0.17)], "HOUSE", 32, 0.0, sq, arc, PT), 0.36))
    for i, t in enumerate((0.24, 0.36, 0.48, 0.62, 0.86)):    # panel ribs over the shell
        main.append(K.sweep(f"{tag}_rib{i}", a, b, [(t - 0.006, rad(t) + 0.22), (t + 0.006, rad(t) + 0.22)], "roof2", 32, 0.0, sq, (0.0, 180.0), PT))
    for i, ang in enumerate((22.0, 45.0, 68.0, 90.0, 112.0, 135.0, 158.0)):   # seams running back over the shell
        pts = []
        for k in range(9):
            t = 0.2 + 0.7 * k / 8
            r = rad(t) + 0.1
            ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
            pts.append((m[0] + bx * D * t + sx * r * ca, m[1] + by * D * t + sy * r * ca, r * (1 - abs(ca) ** PT) * sq))
        for k in range(8):
            main.append(K.seam(f"{tag}_seam{i}_{k}", pts[k], pts[k + 1], 0.22, 0.25))
    # weathered / replaced panels: a few plates of another tone between the seams
    import random
    rnd = random.Random(7 if tag == "s" else 11)
    for i in range(7):
        t0 = rnd.choice((0.24, 0.36, 0.48, 0.62))
        t1 = {0.24: 0.36, 0.36: 0.48, 0.48: 0.62, 0.62: 0.86}[t0]
        a0 = rnd.choice((22.0, 45.0, 68.0, 90.0, 112.0, 135.0))
        main.append(K.sweep(f"{tag}_patch{i}", a, b, [(t, rad(t) + 0.12) for t in (t0 + 0.012, (t0 + t1) / 2, t1 - 0.012)], rnd.choice(("roof2", "stone", "wall")), 6, 0.0, sq, (a0 + 1.5, a0 + 21.0), PT))
    # the mouth: copper rim, dark arch wall set back, three louvres with a cyan upper face
    f0 = (m[0] - bx * 0.8, m[1] - by * 0.8, 0.0)
    f1 = (m[0] + bx * 0.6, m[1] + by * 0.6, 0.0)
    main.append(K.sweep(f"{tag}_rim", f0, f1, [(0.0, R + 0.5), (1.0, R + 0.5)], "copper", 28, 0.0, sq, (0.0, 180.0), PT))
    main.append(K.sweep(f"{tag}_dark", (m[0] - bx * 1.0, m[1] - by * 1.0, 0.0), (m[0] + bx * 2.0, m[1] + by * 2.0, 0.0),
                        [(0.0, R - 1.2), (1.0, R - 1.2)], "tank", 28, 0.0, sq, (0.0, 180.0), PT))
    main.append(K.beam(f"{tag}_post", (m[0] - bx * 0.9, m[1] - by * 0.9, 0.0), (m[0] - bx * 0.9, m[1] - by * 0.9, H - 1.5), 0.8, 0.8, "copper_dk"))
    for i, z in enumerate((5.5, 11.5, 17.5)):
        w = (R - 1.6) * max(0.05, 1 - z / (H - 1.2)) ** (1 / PT)
        c = (m[0] - bx * 1.6, m[1] - by * 1.6)
        main.append(K.beam(f"{tag}_slat{i}", (c[0] - sx * w, c[1] - sy * w, z), (c[0] + sx * w, c[1] + sy * w, z), 1.6, 0.8, "steel_dk"))
        main.append(K.beam(f"{tag}_glow{i}", (c[0] - sx * w - bx * 0.5, c[1] - sy * w - by * 0.5, z + 0.5),
                           (c[0] + sx * w - bx * 0.5, c[1] + sy * w - by * 0.5, z + 0.45), 1.0, 0.55, "glow"))
    # the lip: a cream ramp on the ground in front of the mouth
    L = P["lip"]
    q = [(m[0] - sx * (R + 0.5), m[1] - sy * (R + 0.5)), (m[0] + sx * (R + 0.5), m[1] + sy * (R + 0.5)),
         (m[0] + sx * (R - 3.0) - bx * L, m[1] + sy * (R - 3.0) - by * L), (m[0] - sx * (R - 3.0) - bx * L, m[1] - sy * (R - 3.0) - by * L)]
    main.append(K.ramp(f"{tag}_lip", q[2:], q[:2], 0.0, 1.2, 3.4, "wall", 0.3))
    main.append(K.ramp(f"{tag}_lip_in", [(q[2][0] * 0.85 + q[3][0] * 0.15, q[2][1] * 0.85 + q[3][1] * 0.15), (q[2][0] * 0.15 + q[3][0] * 0.85, q[2][1] * 0.15 + q[3][1] * 0.85)],
                       [(q[0][0] * 0.85 + q[1][0] * 0.15, q[0][1] * 0.85 + q[1][1] * 0.15), (q[0][0] * 0.15 + q[1][0] * 0.85, q[0][1] * 0.15 + q[1][1] * 0.85)],
                       0.5, 1.5, 3.7, "roof", 0.1))
    if damaged:
        def zf(x, y):
            t = ((x - m[0]) * bx + (y - m[1]) * by) / D
            s = ((x - m[0]) * sx + (y - m[1]) * sy)
            r = rad(t)
            return sq * r * (1 - min(0.95, abs(s) / max(r, 1.0)) ** PT)
        K.hole(main, [shell], (m[0] + bx * 14.0 - sx * 4.0, m[1] + by * 14.0 - sy * 4.0), 6.5, zf, 3 if tag == "s" else 4, depth=7.0)
        K.hole(main, [shell], (m[0] + bx * 6.0 + sx * 7.0, m[1] + by * 6.0 + sy * 7.0), 3.6, zf, 6 if tag == "s" else 7, depth=6.0)
        K.scorch(main, (m[0] + bx * 20.0, m[1] + by * 20.0), 5.0, zf, 9 if tag == "s" else 10, 5)
        main.append(K.beam(f"{tag}_slat_broken", (m[0] - bx * 1.6 - sx * 6.0, m[1] - by * 1.6 - sy * 6.0, 10.2), (m[0] - bx * 4.0 + sx * 3.0, m[1] - by * 4.0 + sy * 3.0, 4.0), 2.2, 1.0, "burnt"))


def build(damaged=False):
    main = []
    scoop(main, "s", P["mouth_s"], damaged)
    scoop(main, "n", P["mouth_n"], damaged)
    # ---- machinery east of the scoops
    main.append(K.obox("mach_base", (17.0, 11.0), 30.0, 9.0, 7.0, 0.0, 3.0, "foot", 0.4))
    main.append(K.tank("gen0", (10.0, 14.0, 8.0), (24.0, 20.0, 8.0), 6.2, "copper", 16, 0.4))
    main.append(K.hoop("gen0_band", (10.0, 14.0, 8.0), (24.0, 20.0, 8.0), 0.42, 0.58, 6.5, "copper_dk", 16))
    main.append(K.tank("gen1", (13.0, 5.0, 6.0), (24.0, 9.5, 6.0), 4.8, "rust", 14, 0.4))
    main.append(K.hoop("gen1_band", (13.0, 5.0, 6.0), (24.0, 9.5, 6.0), 0.2, 0.3, 5.1, "copper", 14))
    main.append(K.obox("pump", (15.0, -15.0), 20.0, 7.5, 5.5, 0.0, 8.0, "skirt", 0.5, 0.85))
    main.append(K.dome("pump_dome", (15.0, -15.0), 4.2, 7.8, 3.4, "copper"))
    main.extend(K.pipe("loop", [(21.0, 0.0, 6.5), (25.5, -4.0, 6.5), (25.5, -11.0, 4.5), (21.0, -14.0, 4.5)], 1.4, "orange"))
    main.extend(K.pipe("riser", [(24.0, 16.0, 9.0), (24.0, 16.0, 14.0), (20.0, 26.0, 14.0), (20.0, 30.0, 8.0)], 1.1, "copper"))
    main.extend(K.pipe("feed", [(8.0, -12.0, 3.0), (4.0, -8.0, 3.0)], 1.2, "copper_dk"))
    # more plumbing: valves, a pipe bundle to the scoops, a small tank, brackets
    main.extend(K.pipe("bundle0", [(9.0, 16.0, 3.0), (4.0, 20.0, 3.0), (4.0, 26.0, 6.0)], 0.9, "copper"))
    main.extend(K.pipe("bundle1", [(11.0, 5.0, 2.5), (6.0, 1.0, 2.5), (2.0, -4.0, 2.5)], 0.9, "copper_dk"))
    main.extend(K.pipe("bundle2", [(24.0, 20.0, 12.0), (28.0, 14.0, 12.0), (28.0, 14.0, 3.0)], 0.8, "orange"))
    main.append(K.cyl("valve0", (18.0, 9.5), 1.8, 10.0, 12.5, "steel", 10))
    main.append(K.cyl("valve1", (19.0, 18.0), 1.6, 13.5, 16.0, "copper_dk", 10))
    main.append(K.tank("tank_small", (22.0, -8.0, 3.0), (27.0, -6.0, 3.0), 2.6, "rust", 12, 0.4))
    for i, (x, y) in enumerate(((12.0, 12.0), (21.0, 16.0), (16.0, 3.0), (23.0, 6.0))):
        main.append(K.box(f"bracket{i}", x - 0.8, x + 0.8, y - 0.8, y + 0.8, 0.0, 4.0, "steel_dk"))
    fl, top = K.flue("flue", (25.0, 24.0), 1.3, 6.0, 16.0)
    main.extend(fl)
    for i, p in enumerate(((7.5, -21.0, 7.6), (29.0, 0.0, 9.0))):
        main.extend(K.lamp(f"lamp{i}", p, 0.6))
    if damaged:
        K.scorch(main, (19.0, 16.0), 5.0, 11.6, 31, 4)
        K.wall_gash(main, (13.0, -20.2), 20.0, 4.0, 1.5, 6.5, 32)
    return dict(nodes=dict(main=main), anchors=[("fx_flue_0", top, 0.4)])


def glow(m):
    """The mouth's box on the model canvas (x, v = y + z)."""
    sx, sy = unit(P["face_ang"])
    R = P["half"]
    H = R * P["tall"]
    return dict(x0=m[0] - sx * R - 2.0, x1=m[0] + sx * R + 2.0, v0=m[1] - sy * R - 2.0, v1=m[1] + sy * R + H + 1.0, waves=1.5, lo=0.55, hi=1.5, min_diff=30)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="glow", glow=[glow(P["mouth_s"]), dict(glow(P["mouth_n"]), phase=0.35)],
                                flags=dict(shadow_azimuth=30.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
