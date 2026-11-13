"""Spice refinery (Templates.bin REFINERY; ATREIDES / HARKONNEN / ORDOS REFINERY). Footprint 3 x 3
(occupied 0x777), harvester dock = exit point 1 = cell (2, 1): the middle of the east column, where
the harvester parks facing south-east (economy.md, state 14). After the original art: two big
round processing tanks with radial ribbed roofs and tall stacks (the large one south-west, the
smaller one north), a round unloading pad on the east side with a pipe crane over it, conveyor
and copper pipes between them, a small control dome, all on concrete.
"""

import math

import d2klib as L

SKIN = {"atreides": "hull_light", "harkonnen": "shell_hk", "ordos": "shell_or"}
TRIM = {"atreides": "copper", "harkonnen": "steel_dark", "ordos": "bronze"}


def tank(name, house, r, wall, dome, at, stack_h, ribs=16):
    p = []
    prof = [(r, 0.0), (r, wall * 0.8), (r * 1.03, wall), (r * 0.96, wall + 0.02)]
    for i in range(1, 6):
        t = (math.pi / 2) * i / 6
        prof.append((r * 0.96 * math.cos(t), wall + 0.02 + dome * math.sin(t)))
    prof.append((0.0, wall + 0.02 + dome))
    p.append(L.lathe(f"{name}_body", prof, segs=24, material=SKIN[house]))
    # radial roof ribs following the roof curve
    curve = [(x * 1.0, z + 0.012) for (x, z) in prof[3:]]
    strip = curve + [(x * 0.97, z - 0.018) for (x, z) in reversed(curve)]
    for k in range(ribs):
        a = 2 * math.pi * k / ribs
        rib = L.prism(f"{name}_rib{k}", [(x, z) for (x, z) in strip if x > 0.12], 0.024, axis="Y", material=TRIM[house])
        rib.rotation_euler = (0, 0, a)
        p.append(rib)
    # house band + foot
    p.append(L.cyl(f"{name}_band", r * 1.012, 0.06, center=(0, 0, wall * 0.55), segs=24, material="HOUSE"))
    p.append(L.cyl(f"{name}_foot", r * 1.06, 0.05, center=(0, 0, 0.025), segs=24, material="concrete_dk"))
    # stack
    p.append(L.cyl(f"{name}_stack", 0.055, stack_h, center=(0, 0, stack_h / 2), segs=10, material="bronze"))
    p.append(L.cyl(f"{name}_collar", 0.09, 0.05, center=(0, 0, wall + dome + 0.03), segs=12, material=TRIM[house]))
    p.append(L.cyl(f"{name}_cap", 0.08, 0.05, center=(0, 0, stack_h + 0.02), r2=0.05, segs=10, material="steel_dark"))
    p.append(L.box(f"{name}_lamp", (0.03, 0.03, 0.03), (0, 0, stack_h + 0.06), material="glow_amber"))
    j = L.join(p, name)
    j.location = at
    return j


def build(house):
    root = L.empty("REFINERY")
    base = []
    base.append(L.box("slab", (2.94, 2.94, 0.05), (0, 0, 0.025), material="concrete", bevel=0.015))
    for c in (-0.5, 0.5):
        base.append(L.box(f"seamx{c}", (0.015, 2.9, 0.004), (c, 0, 0.051), material="concrete_dk"))
        base.append(L.box(f"seamy{c}", (2.9, 0.015, 0.004), (0, c, 0.051), material="concrete_dk"))
    b = L.join(base, "base")
    L.node(b, "base", (0, 0, 0), root)
    t1 = tank("tank_main", house, 0.70, 0.30, 0.16, (-0.70, -0.62, 0.05), 0.95)
    L.node(t1, "tank_main", (-0.70, -0.62, 0.05), root)
    t2 = tank("tank_north", house, 0.50, 0.24, 0.14, (-0.40, 0.78, 0.05), 1.15, ribs=12)
    L.node(t2, "tank_north", (-0.40, 0.78, 0.05), root)
    # unloading pad at the dock cell (+1, 0): low round platform with a grille and lights
    pad = []
    pad.append(L.cyl("pad", 0.50, 0.07, center=(1.0, 0.0, 0.085), segs=24, material=TRIM[house], bevel=0.01))
    pad.append(L.cyl("pad_top", 0.42, 0.012, center=(1.0, 0.0, 0.125), segs=24, material="steel"))
    pad.append(L.cyl("pad_grate", 0.20, 0.012, center=(1.0, 0.0, 0.130), segs=16, material="grille"))
    for k in range(8):
        a = 2 * math.pi * k / 8 + math.pi / 8
        pad.append(L.box(f"pad_light{k}", (0.03, 0.03, 0.015), (1.0 + 0.46 * math.cos(a), 0.46 * math.sin(a), 0.125),
                         material="glow_amber"))
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        pad.append(L.box(f"pad_bar{k}", (0.80, 0.035, 0.012), (0, 0, 0), material="steel_dark"))
        pad[-1].rotation_euler = (0, 0, a)
        pad[-1].location = (1.0, 0, 0.135)
    pad.append(L.box("pad_house", (0.06, 0.40, 0.03), (1.48, 0, 0.09), material="HOUSE"))
    p = L.join(pad, "pad")
    L.node(p, "pad", (1.0, 0.0, 0.05), root)
    # crane: a column at the west edge of the pad with a pipe arm over it
    c = []
    c.append(L.cyl("column", 0.07, 0.75, center=(0.42, 0.42, 0.42), segs=12, material="bronze"))
    c.append(L.cyl("column_base", 0.12, 0.10, center=(0.42, 0.42, 0.10), segs=12, material=TRIM[house]))
    c.append(L.box("boom", (0.07, 0.07, 0.48), (0, 0, 0), material="steel_dark", bevel=0.01))
    c[-1].rotation_euler = (0, math.radians(90), math.radians(-38))
    c[-1].location = (0.61, 0.27, 0.78)
    c.append(L.cyl("drop", 0.04, 0.42, center=(0.80, 0.12, 0.58), segs=10, material="bronze"))
    c.append(L.cyl("nozzle", 0.065, 0.08, center=(0.80, 0.12, 0.35), segs=10, material="rust", r2=0.045))
    c.append(L.box("crane_house", (0.02, 0.10, 0.18), (0.49, 0.42, 0.55), material="HOUSE"))
    cr = L.join(c, "crane")
    L.node(cr, "crane", (0.42, 0.42, 0.05), root)
    # conveyor between the pad and the main tank + pipes + control dome
    m = []
    m.append(L.box("conveyor", (0.85, 0.22, 0.10), (0, 0, 0), material="steel_dark", bevel=0.01))
    m[-1].rotation_euler = (0, 0, math.radians(-12))
    m[-1].location = (0.22, -0.40, 0.14)
    for k in range(6):
        sl = L.box(f"conv_slat{k}", (0.03, 0.20, 0.012), (-0.10 + k * 0.13, -0.33 - k * 0.028, 0.195),
                   material="rust")
        m.append(sl)
    m.append(L.cyl("pipe_a", 0.045, 1.0, center=(-0.55, 0.12, 0.12), axis="Y", segs=10, material="copper"))
    m.append(L.cyl("pipe_b", 0.045, 0.70, center=(0.10, 0.62, 0.12), axis="X", segs=10, material="copper"))
    m.append(L.cyl("pipe_v", 0.06, 0.10, center=(0.42, 0.62, 0.12), axis="X", segs=10, material="steel"))
    m.append(L.sphere("dome", 0.30, center=(0.62, -1.02, 0.05), segs=16, rings=8, material=SKIN[house],
                      scale=(1, 0.85, 0.75)))
    m.append(L.lathe("dome_win", [(1.005, 0.0), (1.005, 0.06)], segs=16, center=(0, 0, 0), material="glass",
                     a0=math.radians(200), a1=math.radians(340)))
    m[-1].scale = (0.30, 0.255, 1.0)
    m[-1].location = (0.62, -1.02, 0.14)
    m.append(L.cyl("dome_house", 0.305, 0.04, center=(0.62, -1.02, 0.09), segs=16, material="HOUSE"))
    for nm, x, y, h in (("silo_a", 1.16, 1.02, 0.42), ("silo_b", 0.86, 1.14, 0.34), ("silo_c", 1.20, 0.68, 0.30)):
        m.append(L.cyl(nm, 0.13, h, center=(x, y, 0.05 + h / 2), segs=14, material=SKIN[house], bevel=0.01))
        m.append(L.sphere(nm + "_top", 0.13, center=(x, y, 0.05 + h), segs=14, rings=6, material=TRIM[house],
                          scale=(1, 1, 0.45)))
    m.append(L.cyl("pipe_c", 0.035, 0.40, center=(1.0, 0.85, 0.15), axis="X", segs=8, material="copper"))
    mm = L.join(m, "plant")
    L.node(mm, "plant", (0, 0, 0), root)
    return root


V = dict(dist=6.0, target_z=0.35)
MODELS = {
    "building/ATREIDES_REFINERY": dict(build=lambda: build("atreides"), view=V, aliases=["building/REFINERY"]),
    "building/HARKONNEN_REFINERY": dict(build=lambda: build("harkonnen"), view=V),
    "building/ORDOS_REFINERY": dict(build=lambda: build("ordos"), view=V),
}
