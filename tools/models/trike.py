"""Trike (Templates.bin TRIKE; also used by RAIDER / STEALTH RAIDER through their own art in the
original - here only TRIKE). After the original art: a light three-wheeled scout, one front wheel on
a fork, two big rear wheels with mudguards beside a rear engine block, a tapered copper body with a
cyan canopy, a small gun on top. ~0.72 x 0.44 tiles.
"""

import math

import d2klib as L


def wheel(root, name, x, y, r, w, sx):
    tyre = L.lathe(f"{name}_tyre", [(r * 0.62, -w / 2), (r * 0.92, -w / 2), (r, -w * 0.3), (r, w * 0.3),
                                    (r * 0.92, w / 2), (r * 0.62, w / 2)], segs=14, material="dark", axis="X")
    rim = L.cyl(f"{name}_rim", r * 0.62, w * 0.9, axis="X", segs=10, material="hull_copper")
    hub = L.cyl(f"{name}_hub", r * 0.25, w * 1.1, axis="X", segs=8, material="steel")
    for o in (tyre, rim, hub):
        o.location = (x, y, r)
    wn = L.join([tyre, rim, hub], name)
    return L.node(wn, name, (x, y, r), root)


def build():
    root = L.empty("TRIKE")
    # wheels
    wheel(root, "wheel_F", 0.0, 0.25, 0.085, 0.06, 0)
    wheel(root, "wheel_L", -0.165, -0.21, 0.10, 0.08, -1)
    wheel(root, "wheel_R", 0.165, -0.21, 0.10, 0.08, 1)
    p = []
    # body: tapered fuselage
    p.append(L.hull_pts("body", L.sym([
        (0.035, 0.30, 0.13), (0.04, 0.33, 0.18), (0.03, 0.26, 0.21),
        (0.10, 0.10, 0.10), (0.11, 0.10, 0.22), (0.08, 0.02, 0.25),
        (0.12, -0.16, 0.12), (0.12, -0.16, 0.24), (0.10, -0.28, 0.13), (0.10, -0.28, 0.22)]),
        material="hull_copper", bevel=0.01))
    # belly / chassis
    p.append(L.box("chassis", (0.14, 0.50, 0.05), (0, -0.02, 0.09), material="steel_dark", bevel=0.006))
    # engine block between the rear wheels + exhausts
    p.append(L.box("engine", (0.16, 0.14, 0.10), (0, -0.25, 0.15), material="steel_dark", bevel=0.01))
    for sx in (-1, 1):
        p.append(L.cyl(f"exh{sx}", 0.016, 0.08, center=(sx * 0.05, -0.33, 0.19), axis="Y", segs=8, material="steel"))
    # rear axle
    p.append(L.cyl("axle", 0.022, 0.30, center=(0, -0.21, 0.10), axis="X", segs=8, material="steel"))
    # mudguards over the rear wheels (house colour on top, like the original's markings)
    for sx in (-1, 1):
        g = L.lathe(f"guard{sx}", [(0.115, -0.05), (0.125, -0.05), (0.125, 0.05), (0.115, 0.05)], segs=8,
                    material="hull_copper", a0=math.radians(105), a1=math.radians(255), axis="X")
        g.location = (sx * 0.165, -0.21, 0.10)
        p.append(g)
        p.append(L.box(f"stripe{sx}", (0.012, 0.18, 0.04), (sx * 0.115, -0.07, 0.19), material="HOUSE"))
    # front fork + headlight
    for sx in (-1, 1):
        p.append(L.hull_pts(f"fork{sx}", [(sx * 0.04, 0.25, 0.085), (sx * 0.05, 0.25, 0.085), (sx * 0.04, 0.27, 0.085),
                                          (sx * 0.035, 0.24, 0.19), (sx * 0.045, 0.24, 0.19), (sx * 0.035, 0.26, 0.19)],
                            material="steel"))
    p.append(L.box("lamp", (0.04, 0.012, 0.02), (0, 0.335, 0.165), material="glow_amber"))
    # canopy (cyan glass, the original's bright cyan cockpit)
    p.append(L.sphere("canopy", 1.0, center=(0, 0.06, 0.22), segs=12, rings=8, material="glow_cyan",
                      scale=(0.06, 0.12, 0.05)))
    p.append(L.box("house_top", (0.12, 0.05, 0.012), (0, -0.12, 0.245), material="HOUSE", bevel=0.003))
    body = L.join(p, "body")
    L.node(body, "body", (0, 0, 0), root)
    # gun: a short twin-barrel mount behind the canopy
    g = []
    g.append(L.box("mount", (0.07, 0.07, 0.04), (0, -0.07, 0.27), material="steel_dark", bevel=0.006))
    for sx in (-1, 1):
        g.append(L.cyl(f"bar{sx}", 0.011, 0.22, center=(sx * 0.018, 0.05, 0.28), axis="Y", segs=6, material="steel"))
    gun = L.join(g, "gun")
    L.node(gun, "gun", (0, -0.07, 0.27), root)
    return root


MODELS = {
    "unit/TRIKE": dict(build=build, view=dict(dist=1.7, target_z=0.12)),
}
