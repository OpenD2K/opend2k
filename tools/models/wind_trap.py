"""Wind trap (Templates.bin WIND_TRAP; ATREIDES / HARKONNEN / ORDOS WIND TRAP). Footprint 2 x 3 tiles
(occupied 0x333). After the original art: two big half-dome "clam shell" collectors standing one
behind the other, their open faces turned to the east-south-east with glowing cyan vent vanes;
a copper turbine with a fan and pipes at the south-east corner; all on a concrete pad.
House look: Atreides pale sand shells, Harkonnen dark weathered steel, Ordos gold.
"""

import math

import d2klib as L

SHELL = {"atreides": "hull_light", "harkonnen": "shell_hk", "ordos": "shell_or"}
TRIM = {"atreides": "copper", "harkonnen": "steel_dark", "ordos": "bronze"}


def shell(name, house, rx, ry, h, at, yaw):
    """A half dome (closed towards -X, open face on the x = 0 plane facing +X) with vent vanes."""
    parts = []
    prof = []
    n = 8
    for i in range(n + 1):
        t = (math.pi / 2) * i / n
        prof.append((math.cos(t), math.sin(t) * h))
    d = L.lathe(f"{name}_dome", prof, segs=12, material=SHELL[house], a0=math.pi / 2, a1=3 * math.pi / 2)
    for v in d.data.vertices:
        v.co.x *= rx
        v.co.y *= ry
    parts.append(d)
    # rim (arch along the open face) in the trim colour, with a house-coloured inner band
    outer = [(ry * math.cos(math.pi * i / 12), h * math.sin(math.pi * i / 12)) for i in range(13)]
    inner = [(y * 0.88, z * 0.90) for (y, z) in reversed(outer)]
    big = [(y * 1.04, z * 1.03) for (y, z) in outer]
    parts.append(L.prism(f"{name}_rim", big + inner, 0.06, axis="X", center=(0.0, 0, 0), material=TRIM[house]))
    # meridian ribs over the back of the shell
    for k, a in enumerate((math.radians(140), math.radians(180), math.radians(220))):
        pts = [(math.cos(t) * 1.02, math.sin(t) * h * 1.015) for t in [(math.pi / 2) * i / 8 for i in range(9)]]
        strip = pts + [(x * 0.96, z * 0.96) for (x, z) in reversed(pts)]
        rib = L.prism(f"{name}_mrib{k}", [(x, z) for (x, z) in strip], 0.035, axis="Y", material=TRIM[house])
        for v in rib.data.vertices:
            v.co.x *= rx
        rib.rotation_euler = (0, 0, a)
        parts.append(rib)
    inner2 = [(y * 0.80, z * 0.83) for (y, z) in reversed(outer)]
    band = [(y * 0.88, z * 0.90) for (y, z) in outer]
    parts.append(L.prism(f"{name}_band", band + inner2, 0.03, axis="X", center=(0.0, 0, 0), material="HOUSE"))
    # dark recessed vent panel + glowing vanes
    panel = [(y * 0.80, z * 0.83) for (y, z) in outer]
    parts.append(L.prism(f"{name}_panel", panel, 0.02, axis="X", center=(-0.03, 0, 0), material="grille"))
    for k, zf in enumerate((0.18, 0.36, 0.54, 0.70)):
        z = zf * h * 0.83
        half = ry * 0.80 * math.sqrt(max(0.0, 1 - (z / (h * 0.83)) ** 2)) * 0.9
        vane = L.box(f"{name}_vane{k}", (0.03, 2 * half, 0.035), (0, 0, 0), material="glow_cyan")
        vane.rotation_euler = (math.radians(-12), 0, 0)
        vane.location = (0.0, 0, z + 0.02)
        parts.append(vane)
        parts.append(L.box(f"{name}_louvre{k}", (0.05, 2 * half, 0.012), (0.01, 0, z + 0.055), material=TRIM[house]))
    # foot ring
    parts.append(L.lathe(f"{name}_foot", [(1.04, 0.0), (1.04, 0.06), (0.98, 0.08)], segs=12, material="concrete_dk",
                         a0=math.pi / 2, a1=3 * math.pi / 2, cap_cut=True))
    foot = parts[-1]
    for v in foot.data.vertices:
        v.co.x *= rx
        v.co.y *= ry
    j = L.join(parts, name)
    j.rotation_euler = (0, math.radians(-8), math.radians(yaw))  # leaning back a little, open face up
    j.location = at
    return j


def fan(root, house, at):
    p = [L.cyl("fan_hub", 0.05, 0.05, axis="X", segs=10, material="steel")]
    for k in range(6):
        b = L.box(f"blade{k}", (0.012, 0.035, 0.14), (0, 0, 0.08), material=TRIM[house])
        b.rotation_euler = (2 * math.pi * k / 6, math.radians(20), 0)
        b.location = (0, 0, 0)
        p.append(b)
    j = L.join(p, "fan")
    j.location = at
    from mathutils import Vector  # noqa: F401
    return L.node(j, "fan", at, root)


def build(house):
    root = L.empty("WIND_TRAP")
    base = []
    base.append(L.box("pad", (1.94, 2.94, 0.05), (0, 0, 0.025), material="concrete", bevel=0.015))
    for y in (-0.5, 0.5):
        base.append(L.box(f"seam{y}", (1.90, 0.015, 0.004), (0, y, 0.051), material="concrete_dk"))
    base.append(L.box("seamx", (0.015, 2.9, 0.004), (0, 0, 0.051), material="concrete_dk"))
    b = L.join(base, "base")
    L.node(b, "base", (0, 0, 0), root)
    s1 = shell("shell_N", house, 0.72, 0.62, 1.05, (0.22, 0.58, 0.05), -18)
    L.node(s1, "shell_N", (0.12, 0.55, 0.05), root)
    s2 = shell("shell_S", house, 0.72, 0.62, 1.0, (-0.18, -0.38, 0.05), -18)
    L.node(s2, "shell_S", (-0.30, -0.35, 0.05), root)
    # turbine + tank + pipes (south-east corner)
    m = []
    m.append(L.cyl("turbine", 0.16, 0.42, center=(0.52, -1.05, 0.23), axis="X", segs=14, material="copper", bevel=0.01))
    for x in (0.31, 0.73):
        m.append(L.cyl(f"ring{x}", 0.18, 0.04, center=(x, -1.05, 0.23), axis="X", segs=14, material="bronze"))
    m.append(L.box("cradle", (0.40, 0.26, 0.10), (0.52, -1.05, 0.10), material="steel_dark", bevel=0.01))
    m.append(L.cyl("tank", 0.13, 0.42, center=(0.68, -0.55, 0.26), segs=14, material="copper", bevel=0.01))
    m.append(L.sphere("tank_top", 0.13, center=(0.68, -0.55, 0.47), segs=14, rings=6, material="copper",
                      scale=(1, 1, 0.5)))
    m.append(L.box("tank_house", (0.02, 0.10, 0.12), (0.81, -0.55, 0.30), material="HOUSE"))
    # pipes: turbine -> south shell, tank -> north shell (running along the pad)
    m.append(L.cyl("pipe1", 0.045, 0.30, center=(0.18, -1.05, 0.23), axis="X", segs=10, material="bronze"))
    m.append(L.cyl("pipe1b", 0.045, 0.40, center=(0.05, -0.87, 0.23), axis="Y", segs=10, material="bronze"))
    m.append(L.cyl("pipe2", 0.045, 0.30, center=(0.68, -0.85, 0.20), axis="Y", segs=10, material="bronze"))
    m.append(L.cyl("pipe3", 0.04, 0.95, center=(0.72, 0.0, 0.10), axis="Y", segs=10, material="bronze"))
    m.append(L.cyl("pipe3b", 0.04, 0.22, center=(0.62, 0.47, 0.10), axis="X", segs=10, material="bronze"))
    m.append(L.cyl("valve", 0.07, 0.06, center=(0.72, -0.10, 0.10), axis="Y", segs=10, material="steel"))
    m.append(L.box("light", (0.03, 0.03, 0.03), (0.68, -0.55, 0.56), material="glow_amber"))
    mm = L.join(m, "turbine")
    L.node(mm, "turbine", (0.52, -1.05, 0.23), root)
    fan(root, house, (0.76, -1.05, 0.23))
    return root


V = dict(dist=4.6, target_z=0.35)
MODELS = {
    "building/ATREIDES_WIND_TRAP": dict(build=lambda: build("atreides"), view=V, aliases=["building/WIND_TRAP"]),
    "building/HARKONNEN_WIND_TRAP": dict(build=lambda: build("harkonnen"), view=V),
    "building/ORDOS_WIND_TRAP": dict(build=lambda: build("ordos"), view=V),
}
