"""Spice harvester (Templates.bin HARVESTER). After the original art: a big beetle-like domed hull
with rib bands on six pale balloon tyres, a rusty spice scoop / cutter drum at the front (own node
"scoop", hinged), a vertical exhaust / unloading stack near the front (own node "pipe"), a rusty
dump door at the rear. ~1.15 x 0.80 tiles, dome top ~0.5 tiles.
"""

import math

import d2klib as L


def balloon(root, name, x, y, r, w):
    prof = [(r * 0.35, -w / 2), (r * 0.80, -w / 2), (r * 0.96, -w * 0.32), (r, 0), (r * 0.96, w * 0.32),
            (r * 0.80, w / 2), (r * 0.35, w / 2)]
    tyre = L.lathe(f"{name}_t", prof, segs=16, material="tyre", axis="X")
    hub = L.cyl(f"{name}_h", r * 0.38, w * 1.08, axis="X", segs=10, material="bronze")
    cap = L.cyl(f"{name}_c", r * 0.16, w * 1.2, axis="X", segs=8, material="steel_dark")
    for o in (tyre, hub, cap):
        o.location = (x, y, r)
    j = L.join([tyre, hub, cap], name)
    return L.node(j, name, (x, y, r), root)


def build():
    root = L.empty("HARVESTER")
    R, W = 0.135, 0.15
    for side, sx in (("L", -1), ("R", 1)):
        for i, y in enumerate((0.27, -0.01, -0.29)):
            balloon(root, f"wheel_{side}{i}", sx * 0.335, y, R, W)
    p = []
    # chassis + axles
    p.append(L.box("chassis", (0.46, 0.80, 0.10), (0, -0.01, 0.15), material="steel_dark", bevel=0.015))
    for y in (0.27, -0.01, -0.29):
        p.append(L.cyl(f"axle{y}", 0.035, 0.62, center=(0, y, R), axis="X", segs=8, material="steel_dark"))
    # domed hull (ellipsoid) + skirt
    p.append(L.sphere("dome", 1.0, center=(0, -0.03, 0.22), segs=24, rings=12, material="hull_light",
                      scale=(0.27, 0.47, 0.27)))
    p.append(L.lathe("skirt", [(1.0, -0.5), (1.06, 0.0), (1.0, 0.5)], segs=24, material="hull", center=(0, 0, 0)))
    sk = p[-1]
    sk.scale = (0.285, 0.49, 0.06)
    sk.location = (0, -0.03, 0.22)
    # rib bands across the dome
    for i, y in enumerate((-0.30, -0.12, 0.06, 0.22)):
        t = (y + 0.03) / 0.47
        k = math.sqrt(max(0.0, 1 - t * t))
        rib = L.cyl(f"rib{i}", 1.0, 0.03, axis="Y", segs=20, material="hull")
        rib.scale = (0.27 * k + 0.012, 1, 0.27 * k + 0.012)
        rib.location = (0, y, 0.22)
        p.append(rib)
    # spine plates and a hatch with the house marking
    for k, y in enumerate((-0.30, 0.02)):
        p.append(L.box(f"spine{k}", (0.10, 0.14, 0.03), (0, y, 0.48 - abs(y + 0.05) * 0.18), material="hull", bevel=0.008))
    p.append(L.box("house", (0.14, 0.10, 0.02), (0, -0.16, 0.47), material="HOUSE", bevel=0.005))
    p.append(L.cyl("hatch", 0.045, 0.03, center=(0, 0.10, 0.48), segs=12, material="hull_tan", bevel=0.004))
    # cab windows at the front of the dome
    p.append(L.hull_pts("visor", L.sym([(0.10, 0.38, 0.30), (0.08, 0.40, 0.36), (0.11, 0.35, 0.29), (0.09, 0.37, 0.37)]),
                        material="glass"))
    # rear dump door (rusty) + lights
    p.append(L.hull_pts("dump", L.sym([(0.16, -0.47, 0.13), (0.16, -0.52, 0.16), (0.12, -0.50, 0.33), (0.12, -0.45, 0.33),
                                       (0.16, -0.44, 0.13)]), material="rust", bevel=0.008))
    for sx in (-1, 1):
        p.append(L.box(f"tail{sx}", (0.04, 0.012, 0.025), (sx * 0.10, -0.52, 0.24), material="glow_amber"))
        p.append(L.box(f"house_s{sx}", (0.012, 0.20, 0.04), (sx * 0.272, -0.05, 0.26), material="HOUSE"))
    body = L.join(p, "body")
    L.node(body, "body", (0, 0, 0), root)
    # front scoop: arms + toothed cutter drum (hinged at the front of the chassis)
    s = []
    for sx in (-1, 1):
        s.append(L.box(f"arm{sx}", (0.04, 0.20, 0.05), (sx * 0.18, 0.47, 0.16), material="rust", bevel=0.006))
    s.append(L.cyl("drum", 0.075, 0.38, center=(0, 0.58, 0.10), axis="X", segs=14, material="rust", bevel=0.006))
    for k in range(10):
        a = 2 * math.pi * k / 10
        for j, x in enumerate((-0.12, 0.0, 0.12)):
            tooth = L.box(f"tooth{k}_{j}", (0.05, 0.03, 0.025), (0, 0, 0), material="steel")
            tooth.location = (x + (0.03 if k % 2 else -0.03), 0.58 + 0.085 * math.cos(a), 0.10 + 0.085 * math.sin(a))
            tooth.rotation_euler = (a, 0, 0)
            s.append(tooth)
    s.append(L.hull_pts("hood", L.sym([(0.21, 0.44, 0.15), (0.21, 0.56, 0.21), (0.21, 0.66, 0.15), (0.16, 0.48, 0.25),
                                       (0.16, 0.60, 0.22)]), material="hull_tan", bevel=0.008))
    scoop = L.join(s, "scoop")
    L.node(scoop, "scoop", (0, 0.42, 0.16), root)
    # exhaust / unloading stack
    q = []
    q.append(L.cyl("stack", 0.035, 0.30, center=(0.09, 0.24, 0.55), segs=10, material="steel_dark"))
    q.append(L.cyl("collar", 0.05, 0.04, center=(0.09, 0.24, 0.43), segs=10, material="bronze"))
    q.append(L.cyl("cap", 0.05, 0.03, center=(0.09, 0.24, 0.71), segs=10, material="rust", r2=0.04))
    pipe = L.join(q, "pipe")
    L.node(pipe, "pipe", (0.09, 0.24, 0.42), root)
    return root


MODELS = {
    "unit/HARVESTER": dict(build=build, view=dict(dist=2.4, target_z=0.2)),
}
