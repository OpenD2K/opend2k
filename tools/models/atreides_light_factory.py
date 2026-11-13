"""Atreides light factory - the original design rebuilt with tools/models/atreides_kit.py (fan roof).

    blender -b --factory-startup --python tools/models/atreides_light_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_LIGHT_FACTORY .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3325 base: under units - the dark exit floor at the east and the sandstone boulders
at the north-east and south-west corners; 3326 main; 3327 damaged; animation 35 = 10 ADDITIVE overlay frames (small
blue welding flashes, BF_ANIM_ALPHA) - kept original, they add light to whatever is under them; build-up 35 kept):
  * ONE roof shaped like an open fan lying on the ground plan: its long brown ribs run nearly parallel, converging slightly
    towards a far point in the east (plan x +120, y +20); its west edge stands on the ground. Two ribs cut it into
    three long cream panels. Lengthwise the roof is an arch: up from
    the west edge to a crown (about 15 px) a third of the way from the east, then down to an eave ~8 px up, which
    stands on brown struts over the exit. In the picture the arch makes every rib a curve bulging north. The
    panels stop at different distances from the east point, so the eave is a stepped diagonal - the north panel
    reaches the point, the south one stops half way; the east third of the roof is dusty and in shade.
  * house colour: a pale spine of raised panels across the two northern panels in the middle, pale caps on the
    west ends of the two northern panels, a trim round the south-west corner.
  * behind (north) a dark hood with a brown tower and a lamp; north-west two white flues and two pale domes on a
    grey machine block; a dark pump under the south panel; boulders north-east and south-west.
Free choices: lamps, the pump, machine block details.
"""
import math
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_LIGHT_FACTORY"
P = dict(
    apex=(120.0, 20.0), R=(165.0, 160.0, 156.5, 152.0),   # the fan's (far) centre on the ground plan; radius of the west edge at each rib
    th=(175.1, 179.6, 185.0, 196.0),                # the rib angles (panel boundaries), north to south
    r_in=(84.0, 89.0, 93.0, 105.0),                   # where the roof starts at each boundary (the stepped east eave)
    z_e=10.0, crown_z=16.0, tc=0.5, crown=4.5, thick=2.0,   # eave height, crown height and place, panel crown, thickness
    hood=(0.0, 43.0), hood_r=17.5, hood_h=10.0,
    flue0=(-36.0, 41.0), flue1=(-29.0, 37.0),
    sun_el=58.0, sh_az=35.0,
)


def lin(th, vals):
    T = P["th"]
    for i in range(len(T) - 1):
        if th <= T[i + 1] + 1e-6:
            return vals[i] + (vals[i + 1] - vals[i]) * (th - T[i]) / (T[i + 1] - T[i])
    return vals[-1]


def rin(th):
    return lin(th, P["r_in"])


def rout(th):
    return lin(min(max(th, P["th"][0]), P["th"][-1]), P["R"])


def roof_pt(th, r, lift=0.0):
    """Plan angle th (deg), plan radius r -> model (x, y, z) on the roof's upper surface."""
    T = P["th"]
    i = 0
    while i < len(T) - 2 and th > T[i + 1]:
        i += 1
    u = min(1.0, max(0.0, (th - T[i]) / (T[i + 1] - T[i])))
    thc = min(max(th, T[0]), T[-1])
    r0 = rin(thc)
    t = min(1.0, max(0.0, (r - r0) / (rout(thc) - r0)))   # 0 = east eave, 1 = west edge
    tc = P["tc"]
    if t > tc:
        z = P["crown_z"] * (1.0 - ((t - tc) / (1.0 - tc)) ** 2.0)
    else:
        z = P["z_e"] + (P["crown_z"] - P["z_e"]) * (1.0 - ((tc - t) / tc) ** 2.0)
    z += P["crown"] * math.sin(math.pi * u) * min(1.0, 3.0 * (1.0 - t))
    a = math.radians(th)
    return (P["apex"][0] + r * math.cos(a), P["apex"][1] + r * math.sin(a), max(0.0, z) + lift)


def sheet(name, th0, th1, r0, r1, material, lift=0.0, nth=4, nr=14, thick=None, rim=None):
    """A piece of the roof between plan angles th0..th1 and radii r0..r1 (numbers or functions of th), given a
    thickness downwards; `rim` = material of its edge faces (the fascia)."""
    f0 = r0 if callable(r0) else (lambda th: r0)
    f1 = r1 if callable(r1) else (lambda th: r1)
    bm = bmesh.new()
    grid = []
    for i in range(nth + 1):
        th = th0 + (th1 - th0) * i / nth
        grid.append([bm.verts.new(roof_pt(th, f0(th) + (f1(th) - f0(th)) * j / nr, lift)) for j in range(nr + 1)])
    for i in range(nth):
        for j in range(nr):
            bm.faces.new([grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]])
    o = K.new_obj(name, bm, material)
    m = o.modifiers.new("solid", "SOLIDIFY")
    m.thickness = P["thick"] if thick is None else thick
    m.offset = -1.0
    if rim:
        o.data.materials.append(K.V3.mat(rim))
        m.material_offset_rim = 1
    return o


def build(damaged=False):
    base, main = [], []
    T = P["th"]
    # ---- base frame (under units): the dark exit floor under the eave, boulders
    base.append(K.obox("ramp", (20.0, 19.0), 38.0, 11.0, 4.0, 0.0, 1.2, "floor", 0.2))
    for i in range(5):
        base.append(K.obox(f"ramp_bar{i}", (12.0 + 4.0 * i, 13.0 + 3.1 * i), 128.0, 3.8, 0.35, 1.1, 1.5, "steel_dk"))
    base.extend(K.rocks("rock_ne", (40.0, 47.0), 10.5, 3, 8, 0.9))
    base.extend(K.rocks("rock_sw", (-41.0, -15.0), 6.5, 5, 6))
    base.extend(K.rocks("rock_sw2", (-32.0, -8.0), 4.5, 7, 4))
    # ---- the fan roof: three panels (cream; their east thirds dusty), ribs
    for i in range(3):
        split = lambda th: rin(th) + 0.3 * (rout(th) - rin(th))      # noqa: E731
        main.append(sheet(f"panel{i}", T[i], T[i + 1], split, rout, "roof" if i != 1 else "roof2", rim="wall", nr=12))
        main.append(sheet(f"panel{i}_e", T[i], T[i + 1], rin, split, "stone_dk", rim="tank", nr=6))
    for i in range(4):
        n = 16
        pts = [roof_pt(T[i], rin(T[i]) + (rout(T[i]) - rin(T[i])) * j / n, 0.1) for j in range(n + 1)]
        for j in range(n):
            main.append(K.beam(f"rib{i}_{j}", pts[j], pts[j + 1], 1.5 if 0 < i < 3 else 1.1, 0.9, "rim"))
    for k, f in enumerate((0.56,)):                      # one faint cross seam
        for i in range(3):
            n = 4
            pts = [roof_pt(T[i] + (T[i + 1] - T[i]) * j / n, rin(T[i] + (T[i + 1] - T[i]) * j / n) + f * (rout(T[i]) - rin(T[i])), 0.15) for j in range(n + 1)]
            for j in range(n):
                main.append(K.beam(f"seam{k}_{i}_{j}", pts[j], pts[j + 1], 0.6, 0.5, "rib"))
    # a pale house-colour trim along the south side of every rib (the original's blue lines between the bands)
    for i in range(3):
        main.append(K.house(sheet(f"trim{i}", T[i] + 0.35, T[i] + 0.95, lambda th: rin(th) + 0.34 * (rout(th) - rin(th)), lambda th: rout(th) - 4.0, "HOUSE", 0.22, 1, 10, 0.4), 0.8))
    # house colour: the pale spine across the two northern panels, pale west caps, the south-west corner trim
    for i in range(2):
        rs = 112.0 + 3.0 * i
        main.append(K.house(sheet(f"spine{i}", T[i] + 0.6, T[i + 1] - 0.6, rs, rs + 8.0, "HOUSE", 0.7, 3, 3, 0.9), 0.9))
        main.append(sheet(f"spine{i}_frame", T[i] + 0.4, T[i + 1] - 0.4, rs - 0.8, rs + 8.8, "rim", 0.45, 3, 3, 0.5))
        main.append(K.house(sheet(f"cap{i}", T[i] + 0.4, T[i + 1] - 0.4, lambda th: rout(th) - 3.2, lambda th: rout(th) + 0.5, "HOUSE", 0.5, 3, 2, 1.2), 0.9))
    main.append(K.house(sheet("corner", T[3] - 0.9, T[3] + 0.2, lambda th: rout(th) - 12.0, lambda th: rout(th) + 0.6, "HOUSE", 0.4, 2, 5, 1.0), 0.6))
    main.append(sheet("west_edge", T[2] + 0.3, T[3] - 1.0, lambda th: rout(th) - 2.0, lambda th: rout(th) + 0.6, "white", 0.4, 4, 1, 1.2))
    # brown struts under the stepped east eave
    for i, th in enumerate((177.0, 180.5, 184.0, 188.0, 192.0)):
        p = roof_pt(th, rin(th) + 2.5, -P["thick"])
        main.append(K.beam(f"strut{i}", p, (p[0] + 0.5, p[1] - 1.5, 0.3), 2.0, 1.6, "rust", 0.2))
        main.append(K.cyl(f"strut{i}_foot", (p[0] + 0.5, p[1] - 1.5), 1.6, 0.0, 1.2, "foot", 8))
    # ---- the dark hood behind, tower, lamp
    hc = P["hood"]
    main.append(K.cyl("hood_wall", hc, P["hood_r"] + 0.5, 0.0, 5.0, "block_dk", 32))
    main.append(K.dome("hood", hc, P["hood_r"], 4.5, P["hood_h"], "tank", 32))
    for i in range(5):
        a = 20.0 + 35.0 * i
        main.append(K.beam(f"hood_rib{i}", K.polar(hc, P["hood_r"] + 0.2, a, 5.0), K.polar(hc, 2.0, a, 4.5 + P["hood_h"]), 0.8, 0.6, "copper_dk"))
    main.append(K.obox("tower", (hc[0] + 4.0, hc[1] + 7.0), 10.0, 3.0, 2.6, 0.0, 15.0, "skirt", 0.3, 0.8))
    main.append(K.obox("tower_head", (hc[0] + 4.0, hc[1] + 7.0), 10.0, 3.6, 3.2, 14.0, 16.5, "copper_dk", 0.2))
    main.append(K.obox("east_block", (27.0, 41.0), 10.0, 9.0, 5.0, 0.0, 8.0, "block_dk", 0.5, 0.85))
    main.extend(K.lamp("tower_lamp", (hc[0] + 12.0, hc[1] + 2.0, 13.6), 1.0, "lamp"))
    main.extend(K.pipe("hood_pipe", [(hc[0] - 4.0, hc[1] - 4.0, 13.0), (hc[0] + 4.0, hc[1] - 2.0, 15.0), (hc[0] + 9.0, hc[1] + 3.0, 15.0)], 1.2, "orange"))
    # ---- two white flues and two pale domes on the machine block, north-west
    f0, f1 = P["flue0"], P["flue1"]
    main.append(K.obox("flue_block", (-32.0, 37.0), -35.0, 9.0, 5.0, 0.0, 7.0, "stone_dk", 0.6, 0.9))
    for i, f in enumerate((f0, f1)):
        main.append(K.cyl(f"flue{i}", f, 3.2, 0.0, 14.0 - i, "white", 16))
        main.append(K.cyl(f"flue{i}_lip", f, 3.6, 13.0 - i, 14.4 - i, "wall", 16))
        main.append(K.cyl(f"flue{i}_bore", f, 2.2, 13.6 - i, 14.6 - i, "burnt", 12))
    main.append(K.dome("flue_dome0", (-38.0, 34.0), 4.0, 4.0, 4.4, "white", 14))
    main.append(K.house(K.dome("flue_dome1", (-41.0, 29.5), 3.8, 2.0, 4.2, "HOUSE", 14), 0.95))
    # ---- the pump under the south panel
    main.append(K.obox("pump", (-8.0, -10.0), 30.0, 5.5, 3.6, 0.0, 6.0, "steel_dk", 0.5))
    main.append(K.house(K.obox("pump_band", (-8.5, -11.5), 30.0, 4.5, 2.4, 5.8, 6.6, "HOUSE", 0.1), 0.4))
    main.append(K.cyl("pump_tank", (-3.0, -6.0), 3.2, 0.0, 7.5, "block_dk", 12))
    if damaged:
        for k, (th, r) in enumerate(((177.0, 130.0), (182.0, 140.0), (189.0, 135.0), (181.0, 105.0))):
            x, y, z = roof_pt(th, r)
            K.hole(main, [], (x, y), 4.6 if k < 3 else 3.4, z + 0.4, 20 + k, depth=2.0, cut_up=4.0)
        x, y, z = roof_pt(177.5, 150.0)
        K.scorch(main, (x, y), 5.0, z + 0.3, 31, 5)
        K.scorch(main, hc, 8.0, lambda px, py: 4.5 + P["hood_h"] * math.sqrt(max(0.05, 1 - ((px - hc[0]) ** 2 + (py - hc[1]) ** 2) / P["hood_r"] ** 2)), 50, 5)
    tops = [(f[0], f[1], 14.4 - i) for i, f in enumerate((f0, f1))]
    return dict(nodes=dict(base=base, main=main), anchors=[("fx_flue_0", tops[0], 0.6), ("fx_flue_1", tops[1], 0.5)])


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="keep", flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0),
                                                                sky_strength=0.09, sun_strength=4.3)), P=P)
