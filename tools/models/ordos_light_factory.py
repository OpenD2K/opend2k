"""Ordos light factory - the original design rebuilt with tools/models/ordos_kit.py (round 2).

    blender -b --factory-startup --python tools/models/ordos_light_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_LIGHT_FACTORY .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 52: base 3873 = under units: only the dark sill along the open south-east side and
the rubble; main 3874; damaged 3875; animation 37 = 10 ADDITIVE overlay frames (welding flashes) kept original;
build-up 35 kept) - region by region from the ruler sheet at 12x:
  * one long low canopy lying almost west-east (about 88 px, rising a little to the east), wide at the west
    (28 px) and tapering to the east: DARK bronze-brown ribbed panels over its whole length; on the west half a
    row of four pale house-colour plates running down its south flank; the west end face and a curved lip at the
    north-west are bright gold;
  * across its middle, turned about -55 degrees, a raised gold cabin with pale skylights along its ridge and a
    house-colour frame line along its south-west foot;
  * from under the canopy's south edge a bright yellow chute slopes down to the south-west (the brightest part
    of the picture);
  * south-east of the cabin orange lit panels with a house plate, then the dark grey-blue sill of the open side
    with bronze struts;
  * brass bulbs on posts at the west end (three) and at the chute's foot, teal-black glass orbs at the chute's
    foot and on the east half, orange rubble at the north-east.
Round 1 (one regular vault at 33 degrees, half dark, half gold; IoU 0.76) is replaced by this layout.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_LIGHT_FACTORY"
P = dict(
    a=(-43.0, 25.0), b=(35.0, 31.0), r_w=26.0, r_e=12.0, z0=5.0, squash=0.4,     # the canopy: west / east end of its axis, half widths
    cabin=(1.0, 30.0), cabin_ang=-55.0, cabin_hl=21.0, cabin_hw=7.0, cabin_z=(9.0, 17.0),
    chute=((-17.0, 7.0), (-22.0, -21.0)), chute_w=7.0,
    wing=((-22.0, 10.0), (-15.0, -20.0)), wing_r=13.0,
)


def lerp(p, q, t):
    return tuple(p[i] + (q[i] - p[i]) * t for i in range(len(p)))


def build(damaged=False):
    base, main = [], []
    a, b, rw, re, z0, sq = P["a"], P["b"], P["r_w"], P["r_e"], P["z0"], P["squash"]
    ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    nx, ny = -uy, ux

    def pt(t, s=0.0):
        """Point at t (0 west .. 1 east) along the axis, s = fraction of the local half width towards the north."""
        r = rw + (re - rw) * t
        return (a[0] + ux * L * t + nx * r * s, a[1] + uy * L * t + ny * r * s)

    def zf(x, y):
        t = max(0.0, min(1.0, ((x - a[0]) * ux + (y - a[1]) * uy) / L))
        r = rw + (re - rw) * t
        s = ((x - a[0]) * nx + (y - a[1]) * ny) / r
        return z0 + r * sq * math.sqrt(max(0.02, 1 - s * s))
    # ---- the canopy: a tapering low vault, dark, with ribs; walls on the north side and the two ends
    can = K.sweep("canopy", (a[0], a[1], z0), (b[0], b[1], z0), [(0.0, rw), (1.0, re)], "skirt", 16, 0.0, squash=sq, arc=(0.0, 180.0))
    main.append(can)
    for i in range(15):
        t = (i + 0.5) / 15
        r = rw + (re - rw) * t
        main.append(K.sweep(f"can_rib{i}", (a[0], a[1], z0), (b[0], b[1], z0), [(t - 0.006, r + 0.3), (t + 0.006, r + 0.3)], "bronze" if i % 3 else "roof2", 16, 0.0, squash=sq, arc=(0.0, 180.0)))
    for s, m in ((0.45, "rib"), (-0.45, "rib"), (0.0, "skirt")):                 # spars along the canopy
        for i in range(8):
            p0, p1 = pt(i / 8, s), pt((i + 1) / 8, s)
            main.append(K.beam(f"can_spar{s}_{i}", (*p0, zf(*p0) + 0.15), (*p1, zf(*p1) + 0.15), 1.3, 0.4, m))
    main.append(K.sweep("can_end_w", (a[0], a[1], z0), (b[0], b[1], z0), [(-0.012, rw + 0.6), (0.02, rw + 0.6)], "white", 16, 0.0, squash=sq, arc=(0.0, 180.0)))
    main.append(K.sweep("can_end_e", (a[0], a[1], z0), (b[0], b[1], z0), [(0.985, re + 0.5), (1.01, re + 0.5)], "bronze", 16, 0.0, squash=sq, arc=(0.0, 180.0)))
    wall_n = [pt(0.0, 0.96), pt(1.0, 0.96), pt(1.0, 0.8), pt(0.0, 0.86)]
    main.append(K.prism("wall_n", [wall_n[0], wall_n[3], wall_n[2], wall_n[1]], 0.0, z0 + 0.6, "block", 0.2))
    base.append(K.prism("floor", [pt(0.02, -0.9), pt(0.98, -0.9), pt(0.98, 0.8), pt(0.02, 0.8)], 0.0, 0.7, "block_dk"))
    base.append(K.prism("sill", [pt(0.45, -1.12), pt(1.0, -1.12), pt(1.0, -0.9), pt(0.45, -0.9)], 0.0, 2.2, "grey_dk", 0.2))
    main.append(K.prism("edge_s", [pt(0.0, -1.02), pt(1.0, -1.02), pt(1.0, -0.94), pt(0.0, -0.94)], z0 - 0.8, z0 + 0.9, "bronze", 0.15))
    # the bright curved lip at the north-west
    for i in range(6):
        p0, p1 = pt(0.10 + 0.05 * i, 0.98), pt(0.15 + 0.05 * i, 0.98)
        main.append(K.beam(f"lip{i}", (*p0, z0 + 1.6 + 0.5 * i), (*p1, z0 + 2.1 + 0.5 * i), 2.4, 1.6, "white"))
    # four pale house plates running down the west half's south flank
    for i in range(4):
        p = pt(0.07 + 0.075 * i, -0.28 - 0.17 * i)
        q = pt(0.07 + 0.075 * i + 0.045, -0.28 - 0.17 * i - 0.2)
        main.append(K.house(K.beam(f"plate{i}", (*p, zf(*p) + 0.35), (*q, zf(*q) + 0.35), 4.4, 0.5, "HOUSE"), 0.62))
    # struts of the open side (east half), their feet in the base frame
    for i in range(5):
        t = 0.5 + 0.11 * i
        p0, p1 = pt(t, -0.98), pt(t + 0.01, -1.45)
        main.append(K.tube(f"strut{i}", (*p0, z0 - 0.4), (*p1, 0.3), 1.0, "bronze", 8))
        base.append(K.cyl(f"strut_foot{i}", p1, 1.7, 0.0, 0.9, "bronze_dk", 8))
    # ---- the cabin across the middle
    cc, ca, cl, cw = P["cabin"], P["cabin_ang"], P["cabin_hl"], P["cabin_hw"]
    c0, c1 = P["cabin_z"]

    def cat(t, s=0.0):
        r = math.radians(ca)
        return (cc[0] + math.cos(r) * t - math.sin(r) * s, cc[1] + math.sin(r) * t + math.cos(r) * s)
    main.append(K.obox("cabin", cc, ca, cl, cw, z0, c1, "roof", 0.6))
    main.append(K.obox("cabin_roof", cc, ca, cl - 1.2, cw - 1.4, c1 - 0.2, c1 + 0.7, "roof2", 0.2))
    main.append(K.obox("cabin_ridge", cc, ca, cl - 2.0, 1.0, c1 + 0.5, c1 + 1.4, "white", 0.2))
    for i, t in enumerate((-12.0, -1.0, 10.0)):
        main.append(K.house(K.obox(f"sky{i}", cat(t, -2.6), ca, 3.4, 1.9, c1 + 0.5, c1 + 1.2, "HOUSE", 0.15), 0.88))
        main.append(K.obox(f"sky{i}_n", cat(t + 1.0, 2.8), ca, 2.6, 1.6, c1 + 0.5, c1 + 1.1, "white", 0.15))
    for i in range(7):                                    # the house frame line along its south-west foot (dashes)
        main.append(K.house(K.obox(f"frame{i}", cat(-cl + 2.5 + 5.0 * i, -cw - 0.7), ca, 1.6, 0.5, c0 - 1.0, c0 + 0.4, "HOUSE"), 0.34))
    for i, t in enumerate((-10.0, -3.0, 4.0, 11.0)):
        main.extend(K.vent(f"cabin_vent{i}", cat(cl + 0.05, 0.0) if False else cat(t, -cw - 0.05), ca, 2.0, c0 + 1.6, c1 - 2.0, 0.4, 2, "roof2"))
    main.extend(K.spire("cabin_mast", cat(-cl + 2.0, 2.0), c1, c1 + 8.0, 0.5))
    # orange lit panels south-east of the cabin with a house plate
    for i in range(3):
        p, q = pt(0.47 + 0.05 * i, -0.2), pt(0.50 + 0.05 * i, -0.85)
        main.append(K.beam(f"panel{i}", (*p, zf(*p) + 0.3), (*q, zf(*q) + 0.3), 3.6, 0.5, "roof" if i % 2 == 0 else "wall"))
    p, q = pt(0.60, -0.5), pt(0.62, -0.85)
    main.append(K.house(K.beam("panel_house", (*p, zf(*p) + 0.45), (*q, zf(*q) + 0.45), 3.6, 0.5, "HOUSE"), 0.5))
    # the south-west wing: a lower dark vault from under the canopy towards the south-west (the chute lies on it)
    wg0, wg1 = P["wing"]
    main.append(K.vault("wing", wg0, wg1, P["wing_r"], 1.0, "block_dk", 12, P["wing_r"] * 0.3, ends=3.0))
    for i in range(4):
        tt = (i + 0.5) / 4
        main.append(K.hoop(f"wing_rib{i}", (*wg0, 1.5), (*wg1, 1.5), tt - 0.025, tt + 0.025, P["wing_r"] + 0.3, "bronze", 12, arc=(0.0, 180.0), squash=0.32))
    # ---- the bright chute to the south-west
    (hx, hy), (fx, fy) = P["chute"]
    cw2 = P["chute_w"]
    main.append(K.beam("chute", (hx, hy, z0 + 5.0), (fx, fy, 3.2), cw2 * 2, 1.2, "white", 0.2))
    d = math.hypot(fx - hx, fy - hy)
    sx, sy = -(fy - hy) / d, (fx - hx) / d
    for s in (-1, 1):
        main.append(K.beam(f"chute_rail{s}", (hx + sx * s * cw2, hy + sy * s * cw2, z0 + 5.8), (fx + sx * s * cw2, fy + sy * s * cw2, 4.0), 1.3, 1.6, "roof"))
    for i in range(5):
        t = (i + 0.5) / 5
        px, py, pz = hx + (fx - hx) * t, hy + (fy - hy) * t, z0 + 5.0 + (3.2 - z0 - 5.0) * t
        main.append(K.beam(f"chute_bar{i}", (px - sx * cw2, py - sy * cw2, pz + 0.7), (px + sx * cw2, py + sy * cw2, pz + 0.7), 0.7, 0.3, "roof2"))
    # ---- bulbs, orbs, rubble
    for i, (x, y, r) in enumerate(((-40.0, 36.0, 3.4), (-45.0, 24.0, 3.2), (-47.0, 19.0, 2.8), (-9.0, -8.0, 3.2))):
        main.extend(K.bulb(f"bulb{i}", (x, y), r, 5.0))
    main.extend(K.orb("orb_s", (-5.0, -10.0, 4.0), 2.6))
    op = pt(0.72, -0.35)
    main.extend(K.orb("orb_e", (*op, zf(*op) + 1.4), 2.6))
    base.extend(K.rubble("rub", (41.0, 50.0), 7.5, 3, 6))
    main.extend(K.rubble("rub_w", (-42.0, 44.0), 3.5, 5, 3))
    main.extend(K.rubble("rub_s", (-34.0, 2.0), 3.5, 7, 3))
    if damaged:
        K.hole(main, [can], pt(0.2, 0.1), 6.0, zf, 3, depth=6.0, panel=("bronze", "roof2"), cut_up=8.0)
        K.hole(main, [can], pt(0.8, 0.0), 4.5, zf, 5, depth=5.0, panel=("bronze", "roof2"), cut_up=8.0)
        K.scorch(main, cc, 6.0, c1 + 0.8, 7, 5)
        K.wall_gash(main, cat(3.0, -cw - 0.3), ca, 5.0, c0 + 1.0, c1 - 1.5, 9, 0.8)
    return dict(nodes=dict(base=base, main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="keep", flags=K.flags(-0.35, shadow_azimuth=30.0)), P=P)
