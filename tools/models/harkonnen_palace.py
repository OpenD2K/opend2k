"""Harkonnen palace - built from scratch with tools/models/harkonnen_kit.py (the original is its own design: a black
engine-block of a keep).

    blender -b --factory-startup --python tools/models/harkonnen_palace.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_PALACE .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3603 main, 3604 damaged, no base; animation 57 = 21 opaque overlay frames of the
east flank with an orange furnace flicker - they are cut from the original picture, so they are written BLANK here;
build-up 56 kept). The picture is taller than its 96 px art box (it overhangs the footprint to the south):
  * a square keep turned 45 degrees: blackened fluted walls, a round bastion on each corner (west, south, east;
    the north one hidden) - each a drum with three stacked brass rings under a dark cap, pale fluted lower walls
    on the sunlit west and south;
  * on the keep a long upper block (48 x 23 px, long axis north-west to south-east) with FOUR fat standing
    pipes on its corners, each with a brass mouth inside a house-colour ring; a pale brass lying cylinder joins
    the two south-west pipes, a dark one the two north-east ones; a house-colour "V" trim on the roof between;
  * on the south-west face under the pale cylinder a big sloping house-colour panel striped with gold bars;
  * small house lamps on the south-west wall, house bars on the south-east wall, a pale machine and rubble at the
    south-east foot.
Free choices: flutes, vents, the pipes' bores, lamps.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402

KEY = "HARKONNEN_PALACE"
P = dict(
    c=(6.0, -20.0), half=27.5, keep_h=34.0,                 # the keep: square, half side, turned 45 deg
    bast=((-35.0, -12.0), (7.0, -55.0), (31.0, -22.0), (3.0, 27.0)), bast_r=11.0, bast_h=35.0,
    pa=(-24.0, 6.0), pb=(-8.5, 20.0), pc=(10.0, -28.0), pd=(26.5, -16.0), pipe_r=7.3, pipe_z=57.5, up_h=50.0, se=(30.0, -48.0, 12.0, 9.5, 22.0), spire=(2.0, 12.0, 62.0),
)


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): the four pipes' mouths burn like
# furnaces (the original's animation is an orange furnace flicker), the gold bars of the banner panel glow dimly,
# tall lit slits = the pale flutes of the south-west wall.
GLOW = dict(bore=K.AK.lit("copper_fire", "copper", (1.00, 0.46, 0.10), 0.5), hole=K.AK.lit("burnt_fire", "burnt", (1.00, 0.55, 0.15), 0.04),
            bar=K.AK.lit("hk_gold_lit", "hk_gold", (0.55, 0.36, 0.10), 0.5), slit=K.AK.lit("stone_lit", "stone", (0.34, 0.15, 0.05), 0.1))


def glow(main):
    m = {"banner_bar": GLOW["bar"]}
    for i in range(4):
        m[f"pipe{i}_bore"] = GLOW["bore"]
        m[f"pipe{i}_hole"] = GLOW["hole"]
    for i in (0, 2, 4, 6, 8):
        m[f"flute_sw{i}$"] = GLOW["slit"]
    return K.AK.relight(main, m)


def flutes(out, name, c, r0, r1, z0, z1, a0, a1, n, mat):
    for k in range(n):
        a = a0 + (a1 - a0) * (k + 0.5) / n
        out.append(K.beam(f"{name}{k}", K.polar(c, r0, a, z0), K.polar(c, r1, a, z1), 1.3, 1.0, mat, 0.1))


def build(damaged=False):
    main = []
    c, hf, kh = P["c"], P["half"], P["keep_h"]
    # ---- the keep: battered square turned 45 degrees, fluted walls
    main.append(K.obox("keep_foot", c, 45.0, hf + 2.5, hf + 2.5, 0.0, 3.0, "hk_black", 0.5))
    main.append(K.obox("keep", c, 45.0, hf, hf, 0.0, kh, "stone_dk", 0.8, 0.9))
    main.append(K.obox("keep_rim", c, 45.0, hf * 0.9 + 0.6, hf * 0.9 + 0.6, kh - 1.6, kh + 0.6, "hk_black", 0.3))
    for side, ang in (("sw", 225.0), ("se", 315.0)):
        nx, ny = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        tx, ty = -ny, nx
        for i in range(9):
            t = (i - 4) * hf * 0.2
            p0 = (c[0] + nx * (hf + 0.3) + tx * t, c[1] + ny * (hf + 0.3) + ty * t)
            p1 = (c[0] + nx * (hf * 0.9 + 0.3) + tx * t * 0.9, c[1] + ny * (hf * 0.9 + 0.3) + ty * t * 0.9)
            main.append(K.beam(f"flute_{side}{i}", (*p0, 1.0), (*p1, kh - 2.0), 1.6, 1.0, ("stone_dk" if i % 2 else "stone") if side == "sw" else "stone_dk", 0.1))
    # ---- corner bastions: drum, pale fluted foot (sunlit ones), three brass rings, dark cap
    br, bh = P["bast_r"], P["bast_h"]
    for i, b in enumerate(P["bast"]):
        lit = i < 2
        main.append(K.cyl(f"bast{i}", b, br + 1.2, 0.0, bh, "hk_plate" if lit else "stone_dk", 24, br))
        flutes(main, f"bast{i}_fl", b, br + 1.5, br + 0.4, 0.6, bh * 0.55, 150.0, 330.0, 9, "white" if lit else "stone")
        for j in range(3):
            z = bh - 9.5 + 3.4 * j
            main.append(K.lathe_loop(f"bast{i}_ring{j}", b, [(br + 1.6 - 0.3 * j, z), (br + 2.2 - 0.3 * j, z + 1.1), (br + 1.6 - 0.3 * j, z + 2.2), (br - 0.5, z + 2.2), (br - 0.5, z)], "hk_brass", 24))
        main.append(K.dome(f"bast{i}_cap", b, br - 0.4, bh + 0.6, 3.6, "hk_shell", 24))
        main.append(K.cyl(f"bast{i}_foot", b, br + 2.2, 0.0, 2.0, "hk_black", 24))
    # ---- the upper block and its four pipes
    pa, pb, pc, pd = P["pa"], P["pb"], P["pc"], P["pd"]
    uc = ((pa[0] + pb[0] + pc[0] + pd[0]) / 4, (pa[1] + pb[1] + pc[1] + pd[1]) / 4)
    ang = math.degrees(math.atan2(pc[1] - pa[1], pc[0] - pa[0]))
    ul = math.hypot(pc[0] - pa[0], pc[1] - pa[1]) / 2
    uw = math.hypot(pb[0] - pa[0], pb[1] - pa[1]) / 2
    uh, pz, pr = P["up_h"], P["pipe_z"], P["pipe_r"]
    main.append(K.obox("upper", uc, ang, ul + 2.0, uw + 2.0, kh - 1.0, uh, "hk_black", 0.6, 0.96))
    main.append(K.obox("upper_roof", uc, ang, ul - 4.0, uw - 3.0, uh - 0.2, uh + 1.0, "stone_dk", 0.3))
    for i, p in enumerate((pa, pb, pc, pd)):
        main.append(K.cyl(f"pipe{i}", p, pr, kh - 6.0, pz, "copper_dk" if i in (1, 3) else "rust", 24))
        main.append(K.lathe_loop(f"pipe{i}_lip", p, [(pr + 0.9, pz - 2.4), (pr + 0.9, pz + 0.4), (pr - 2.2, pz + 0.4), (pr - 2.2, pz - 2.4)], "hk_brass", 24))
        main.append(K.house(K.lathe_loop(f"pipe{i}_house", p, [(pr + 0.5, pz + 0.2), (pr + 0.5, pz + 1.2), (pr - 1.9, pz + 1.2), (pr - 1.9, pz + 0.2)], "HOUSE", 24), 0.42))
        main.append(K.cyl(f"pipe{i}_bore", p, pr - 2.2, pz - 2.0, pz - 0.8, "copper", 16))
        main.append(K.cyl(f"pipe{i}_hole", p, pr - 4.2, pz - 1.0, pz - 0.5, "burnt", 12))
        for j in range(2):
            z = kh + 4.0 + 6.0 * j
            main.append(K.cyl(f"pipe{i}_band{j}", p, pr + 0.4, z, z + 1.2, "hk_black", 24))
    sx_, sy_, sz_ = P["spire"]                           # the dark central spire behind the north-east tube
    main.append(K.obox("spire", (sx_, sy_), 45.0, 6.0, 6.0, uh - 2.0, sz_, "hk_black", 0.6, 0.8))
    main.append(K.obox("spire_cap", (sx_, sy_), 45.0, 5.2, 5.2, sz_ - 0.5, sz_ + 1.2, "stone_dk", 0.3))
    zt = uh + 2.6
    main.append(K.tank("tube_sw", (*pa, zt), (*pc, zt), 5.2, "hk_plate", 20, 0.2))
    main.append(K.tank("tube_ne", (*pb, zt), (*pd, zt), 5.0, "hk_shell", 20, 0.2))
    for i, t in enumerate((0.3, 0.5, 0.7)):
        main.append(K.hoop(f"tube_sw_rib{i}", (*pa, zt), (*pc, zt), t - 0.012, t + 0.012, 5.45, "hk_brass", 20))
        main.append(K.hoop(f"tube_ne_rib{i}", (*pb, zt), (*pd, zt), t - 0.012, t + 0.012, 5.25, "stone", 20))
    # the house "V" on the roof between the tubes, towards the south-east end
    ax, ay = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    nx, ny = -ay, ax
    v0 = (uc[0] + ax * 4.0, uc[1] + ay * 4.0)
    for i, s in enumerate((-1, 1)):
        q = (uc[0] + ax * 16.0 + nx * s * 5.0, uc[1] + ay * 16.0 + ny * s * 5.0)
        main.append(K.house(K.beam(f"vee{i}", (*v0, uh + 1.3), (*q, uh + 1.3), 1.5, 0.6, "HOUSE"), 0.25))
    # ---- the sloping house panel on the south-west face, striped with gold bars
    def sw(t, d):                                        # along a -> c (px from a), d px out to the south-west
        return (pa[0] + ax * t - nx * d, pa[1] + ay * t - ny * d)
    t0, t1 = 3.0, 31.0
    lo, hi = [sw(t0, 21.5), sw(t1, 21.5)], [sw(t1, 7.0), sw(t0, 7.0)]
    main.append(K.ramp("banner_body", lo, hi, kh - 12.0, kh - 9.0, kh + 6.0, "stone_dk", 0.2))
    import bmesh
    def strip(name, ta, tb, lift, house_k=None, mat="hk_gold"):
        pts = [(sw(ta, 21.0), kh - 8.6), (sw(tb, 21.0), kh - 8.6), (sw(tb, 7.5), kh + 5.6), (sw(ta, 7.5), kh + 5.6)]
        bm = bmesh.new()
        K.rings_solid(bm, [[(x, y, z + lift - 0.4) for ((x, y), z) in pts], [(x, y, z + lift) for ((x, y), z) in pts]])
        o = K.new_obj(name, bm, "HOUSE" if house_k else mat)
        return K.house(o, house_k) if house_k else o
    main.append(strip("banner", t0 + 0.6, t1 - 0.6, 0.5, 0.6))
    for i in range(5):
        ta = t0 + 3.0 + i * 5.4
        main.append(strip(f"banner_bar{i}", ta, ta + 1.7, 0.9))
    # ---- lamps, bars, machine, rubble
    for i, (t, d, z) in enumerate(((-2.0, 30.0, 12.0), (12.0, 30.5, 9.0))):
        q = sw(t, d)
        main.append(K.house(K.obox(f"wall_lamp{i}", q, ang, 2.6, 0.8, z, z + 4.0, "HOUSE", 0.1), 0.5))
    for i in range(3):
        q = (c[0] + 15.0 + 2.6 * i, c[1] - 15.5 + 2.6 * i)
        main.append(K.house(K.obox(f"se_bar{i}", (q[0] + 4.6, q[1] - 4.6), 45.0, 0.8, 0.6, 8.0, 16.0, "HOUSE"), 0.22))
    ex, ey, el, ew, eh = P["se"]                         # the south-east annex: a dark machine block, the pale machine in front
    main.append(K.obox("annex", (ex, ey), 45.0, el, ew, 0.0, eh, "stone_dk", 0.8, 0.9))
    main.append(K.obox("annex_roof", (ex, ey), 45.0, el * 0.9 - 1.0, ew * 0.9 - 1.0, eh - 0.2, eh + 0.7, "hk_black", 0.2))
    main.append(K.obox("machine", (ex + 2.0, ey - 11.0), 20.0, 7.0, 4.0, 0.0, 6.0, "wall", 0.5, 0.9))
    main.append(K.tank("machine_tank", (ex - 3.0, ey - 13.0, 6.5), (ex + 7.0, ey - 9.0, 6.5), 3.0, "hk_plate", 12, 0.4))
    main.extend(K.rocks("rub_s", (-10.0, -52.0), 6.0, 31, 6, 0.6))
    main.extend(K.rocks("rub_se", (40.0, -34.0), 5.0, 32, 5, 0.6))
    main.extend(K.rocks("rub_w", (-42.0, -20.0), 5.0, 33, 5, 0.6))
    main.extend(K.pipe("pipe_s", [(16.0, -40.0, 4.0), (22.0, -36.0, 4.0), (24.0, -30.0, 9.0)], 1.5, "copper"))
    if damaged:
        K.hole(main, [], (uc[0] + 2.0, uc[1] - 1.0), 5.5, uh + 1.0, 3, depth=4.0, panel=("stone", "hk_shell"), rib="hk_brass")
        K.scorch(main, sw(18.0, 14.0), 6.0, lambda x, y: kh + 0.5, 5, 5)
        K.wall_gash(main, (c[0] - 14.0, c[1] - 26.0), -45.0 + 0.0, 7.0, 6.0, 20.0, 7, 1.2)
        K.wall_gash(main, (c[0] + 20.0, c[1] - 20.0), 45.0, 6.0, 8.0, 20.0, 8, 1.2)
        K.scorch(main, P["bast"][1], 5.0, bh + 4.4, 9, 4)
    glow(main)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="blank", flags=dict(shadow_azimuth=40.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
