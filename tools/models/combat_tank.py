"""Combat tank, three house designs (Templates.bin: ATREIDES / HARKONNEN / ORDOS COMBAT TANK).

Common layout after the original art: an "H" plan (two long track pods with armoured fenders that
run past a lower central hull at both ends), a turret slightly behind the centre with one long
gun. Atreides: pale angular armour, wedge turret. Harkonnen: heavier brown hull, steel caps on the
track ends, boxy turret with a dark sloped visor and a cyan sensor. Ordos: rounded pale side pods,
egg-shaped turret with a big cyan canopy and a long thin gun.
Size: ~0.86 x 0.66 tiles, turret top ~0.32 tiles.
"""

import math

import d2klib as L

LEN = 0.86
TRACK_X = 0.235      # track centre line
TRACK_W = 0.17
PIVOT = (0.0, -0.03, 0.215)


def tracks(root, house):
    parts = []
    belt = [(-0.40, 0.0), (0.40, 0.0), (0.445, 0.05), (0.43, 0.10), (0.385, 0.14), (-0.385, 0.14),
            (-0.43, 0.10), (-0.445, 0.05)]
    for side, sx in (("L", -1), ("R", 1)):
        x = sx * TRACK_X
        b = L.prism(f"belt_{side}", belt, TRACK_W, axis="X", center=(x, 0, 0), material="dark", bevel=0.008)
        parts.append(b)
        # tread cleats on the exposed belt ends
        for y in (-0.42, 0.42):
            parts.append(L.box(f"cleat_{side}{y}", (TRACK_W + 0.006, 0.03, 0.03), (x, y, 0.07), material="steel_dark"))
        # road wheels (own nodes: origin on the axle)
        for i, y in enumerate((-0.31, -0.155, 0.0, 0.155, 0.31)):
            w = L.cyl(f"wheel_{side}{i}", 0.052, 0.03, center=(x + sx * (TRACK_W / 2 + 0.006), y, 0.058), axis="X",
                      segs=12, material="steel_dark", bevel=0.006)
            hub = L.cyl(f"hub_{side}{i}", 0.022, 0.012, center=(x + sx * (TRACK_W / 2 + 0.024), y, 0.058), axis="X",
                        segs=8, material="steel")
            wn = L.join([w, hub], f"wheel_{side}{i}")
            L.node(wn, f"wheel_{side}{i}", (x, y, 0.058), root)
        # sprocket / idler
        for nm, y in (("sprocket", 0.39), ("idler", -0.39)):
            s = L.cyl(f"{nm}_{side}", 0.045, TRACK_W + 0.012, center=(x, y, 0.07), axis="X", segs=10,
                      material="steel" if house == "harkonnen" else "steel_dark")
            L.node(s, f"{nm}_{side}", (x, y, 0.07), root)
    return parts


def fenders_atreides(parts):
    prof = [(-0.42, 0.125), (0.40, 0.125), (0.445, 0.15), (0.40, 0.195), (-0.36, 0.205), (-0.43, 0.17)]
    for side, sx in (("L", -1), ("R", 1)):
        x = sx * TRACK_X
        parts.append(L.prism(f"fender_{side}", prof, 0.205, axis="X", center=(x, 0, 0), material="hull", bevel=0.012))
        # side skirt over the upper wheels
        parts.append(L.box(f"skirt_{side}", (0.016, 0.66, 0.05), (x + sx * 0.104, -0.02, 0.105), material="hull",
                           bevel=0.005))
        # raised armour block on the fender (the chunky look of the sprite)
        parts.append(L.hull_pts(f"armour_{side}", [
            (x - 0.085, -0.30, 0.20), (x + 0.085, -0.30, 0.20), (x - 0.085, 0.24, 0.195), (x + 0.085, 0.24, 0.195),
            (x - 0.07, -0.27, 0.235), (x + 0.07, -0.27, 0.235), (x - 0.07, 0.19, 0.23), (x + 0.07, 0.19, 0.23)],
            material="hull_light", bevel=0.008))
        # house panels: front of each fender and a stripe on the rear
        parts.append(L.hull_pts(f"house_f_{side}", [
            (x - 0.07, 0.27, 0.193), (x + 0.07, 0.27, 0.193), (x - 0.07, 0.36, 0.190), (x + 0.07, 0.36, 0.190),
            (x - 0.07, 0.27, 0.203), (x + 0.07, 0.27, 0.203), (x - 0.07, 0.355, 0.200), (x + 0.07, 0.355, 0.200)],
            material="HOUSE"))
        # stowage box on the rear of each fender
        parts.append(L.box(f"stow_{side}", (0.12, 0.08, 0.05), (x, -0.36, 0.215), material="hull_tan", bevel=0.006))


def fenders_harkonnen(parts):
    prof = [(-0.43, 0.12), (0.42, 0.12), (0.45, 0.16), (0.42, 0.21), (-0.42, 0.215), (-0.45, 0.17)]
    for side, sx in (("L", -1), ("R", 1)):
        x = sx * TRACK_X
        parts.append(L.prism(f"fender_{side}", prof, 0.21, axis="X", center=(x, 0, 0), material="hull_tan", bevel=0.01))
        parts.append(L.box(f"skirt_{side}", (0.02, 0.72, 0.06), (x + sx * 0.106, 0, 0.10), material="hull_brown",
                           bevel=0.006))
        # steel end caps (front / rear of each track pod)
        for y in (0.40, -0.40):
            parts.append(L.cyl(f"cap_{side}{y}", 0.06, 0.20, center=(x, y, 0.16), axis="X", segs=10, material="steel",
                               bevel=0.006))
        # light armour panels on top
        parts.append(L.box(f"plate_{side}", (0.16, 0.50, 0.03), (x, -0.02, 0.225), material="hull_light", bevel=0.008))
        parts.append(L.box(f"house_{side}", (0.16, 0.06, 0.012), (x, 0.28, 0.212), material="HOUSE"))
        parts.append(L.box(f"house_r_{side}", (0.16, 0.06, 0.012), (x, -0.30, 0.243), material="HOUSE"))


def fenders_ordos(parts):
    for side, sx in (("L", -1), ("R", 1)):
        x = sx * TRACK_X
        pod = L.sphere(f"pod_{side}", 1.0, center=(x, 0.0, 0.16), segs=16, rings=8, material="hull_light",
                       scale=(0.105, 0.43, 0.075))
        parts.append(pod)
        parts.append(L.box(f"skirt_{side}", (0.016, 0.62, 0.05), (x + sx * 0.10, 0, 0.11), material="hull_tan",
                           bevel=0.005))
        parts.append(L.sphere(f"house_{side}", 1.0, center=(x, 0.02, 0.222), segs=12, rings=6, material="HOUSE",
                              scale=(0.045, 0.30, 0.018)))
        parts.append(L.box(f"lamp_{side}", (0.03, 0.03, 0.02), (x, 0.42, 0.17), material="glow_cyan"))


def hull_body(parts, house):
    mat = {"atreides": "hull", "harkonnen": "hull_brown", "ordos": "hull_tan"}[house]
    prof = [(-0.30, 0.05), (0.22, 0.05), (0.32, 0.12), (0.27, 0.20), (-0.27, 0.215), (-0.31, 0.17)]
    width = 0.32 if house != "harkonnen" else 0.34
    parts.append(L.prism("hull", prof, width, axis="X", material=mat, bevel=0.01))
    # rear engine deck grille
    parts.append(L.box("grille", (width - 0.08, 0.10, 0.012), (0, -0.225, 0.214), material="grille"))
    for i in range(4):
        parts.append(L.box(f"slat{i}", (width - 0.09, 0.008, 0.01), (0, -0.265 + i * 0.026, 0.222), material="steel_dark"))
    # front glacis detail: two lights
    lamp = "glow_amber" if house != "ordos" else "glow_cyan"
    for sx in (-1, 1):
        parts.append(L.box(f"light{sx}", (0.035, 0.012, 0.022), (sx * 0.10, 0.30, 0.15), material=lamp))
    # a towing hook / exhausts at the back
    for sx in (-1, 1):
        parts.append(L.cyl(f"exhaust{sx}", 0.016, 0.06, center=(sx * 0.11, -0.31, 0.17), axis="Y", segs=8,
                           material="steel_dark"))


def turret_atreides(root):
    px, py, pz = PIVOT
    tp = []
    b = [(0.15, 0.13), (0.175, 0.04), (0.175, -0.15), (0.12, -0.215)]
    t = [(0.095, 0.06), (0.12, -0.01), (0.12, -0.14), (0.075, -0.185)]
    pts = [(x, py + y, pz) for x, y in b] + [(x, py + y, pz + 0.105) for x, y in t]
    tp.append(L.hull_pts("turret_body", L.sym(pts), material="hull_light", bevel=0.01))
    # side armour cheeks carrying the house colour
    for sx in (-1, 1):
        tp.append(L.hull_pts(f"cheek{sx}", [
            (sx * 0.172, py + 0.02, pz + 0.012), (sx * 0.172, py - 0.13, pz + 0.012), (sx * 0.185, py + 0.0, pz + 0.05),
            (sx * 0.185, py - 0.12, pz + 0.05), (sx * 0.14, py + 0.01, pz + 0.08), (sx * 0.14, py - 0.12, pz + 0.08)],
            material="HOUSE"))
    # commander hatch + sensor
    tp.append(L.cyl("hatch", 0.04, 0.025, center=(0.04, py - 0.08, pz + 0.115), segs=10, material="hull", bevel=0.004))
    tp.append(L.box("sensor", (0.03, 0.025, 0.04), (-0.055, py - 0.10, pz + 0.125), material="steel_dark"))
    tp.append(L.box("periscope", (0.05, 0.012, 0.015), (-0.055, py - 0.087, pz + 0.135), material="glass"))
    tp.append(L.cyl("antenna", 0.004, 0.22, center=(-0.09, py - 0.17, pz + 0.20), segs=4, material="steel_dark"))
    tp.append(L.box("bustle", (0.20, 0.05, 0.05), (0, py - 0.215, pz + 0.05), material="hull", bevel=0.008))
    tb = L.join(tp, "turret")
    L.node(tb, "turret", PIVOT, root)
    mant = L.hull_pts("mantlet", L.sym([(0.045, py + 0.07, pz + 0.025), (0.045, py + 0.13, pz + 0.035),
                                        (0.035, py + 0.07, pz + 0.085), (0.03, py + 0.12, pz + 0.07)]),
                      material="hull", bevel=0.005)
    gun = L.cyl("gun", 0.022, 0.38, center=(0, py + 0.31, pz + 0.05), axis="Y", segs=10, material="steel")
    sleeve = L.cyl("sleeve", 0.03, 0.10, center=(0, py + 0.17, pz + 0.05), axis="Y", segs=10, material="hull")
    brake = L.cyl("brake", 0.032, 0.055, center=(0, py + 0.49, pz + 0.05), axis="Y", segs=8, material="steel_dark",
                  bevel=0.004)
    bar = L.join([mant, gun, sleeve, brake], "barrel")
    L.node(bar, "barrel", (0, py + 0.10, pz + 0.05), tb)
    return tb


def turret_harkonnen(root):
    px, py, pz = PIVOT
    tp = []
    tp.append(L.hull_pts("turret_body", L.sym([
        (0.14, py + 0.08, pz), (0.15, py - 0.16, pz), (0.13, py + 0.06, pz + 0.10), (0.14, py - 0.15, pz + 0.105),
        (0.10, py + 0.13, pz + 0.02), (0.09, py + 0.10, pz + 0.07)]), material="hull_tan", bevel=0.01))
    # dark sloped visor across the front
    tp.append(L.hull_pts("visor", L.sym([
        (0.11, py + 0.125, pz + 0.025), (0.10, py + 0.095, pz + 0.075), (0.115, py + 0.105, pz + 0.02),
        (0.105, py + 0.075, pz + 0.08)]), material="glass"))
    tp.append(L.box("sensor", (0.03, 0.02, 0.02), (0.07, py + 0.10, pz + 0.07), material="glow_cyan"))
    tp.append(L.box("roof", (0.20, 0.16, 0.02), (0, py - 0.06, pz + 0.11), material="hull_light", bevel=0.006))
    for sx in (-1, 1):
        tp.append(L.box(f"house{sx}", (0.012, 0.16, 0.05), (sx * 0.147, py - 0.05, pz + 0.05), material="HOUSE"))
    tb = L.join(tp, "turret")
    L.node(tb, "turret", PIVOT, root)
    gun = L.cyl("gun", 0.017, 0.38, center=(0, py + 0.30, pz + 0.05), axis="Y", segs=8, material="steel")
    sleeve = L.cyl("sleeve", 0.03, 0.08, center=(0, py + 0.14, pz + 0.05), axis="Y", segs=10, material="steel_dark")
    tip = L.cyl("tip", 0.024, 0.03, center=(0, py + 0.48, pz + 0.05), axis="Y", segs=8, material="steel_dark")
    bar = L.join([gun, sleeve, tip], "barrel")
    L.node(bar, "barrel", (0, py + 0.10, pz + 0.05), tb)
    return tb


def turret_ordos(root):
    px, py, pz = PIVOT
    tp = []
    tp.append(L.sphere("turret_body", 1.0, center=(0, py - 0.02, pz + 0.03), segs=16, rings=10, material="hull_light",
                       scale=(0.145, 0.18, 0.09)))
    tp.append(L.cyl("ring", 0.14, 0.03, center=(0, py - 0.02, pz + 0.01), segs=16, material="hull_tan"))
    tp.append(L.sphere("canopy", 1.0, center=(0, py + 0.03, pz + 0.075), segs=14, rings=8, material="glow_cyan",
                       scale=(0.075, 0.11, 0.05)))
    tp.append(L.box("house", (0.20, 0.05, 0.012), (0, py - 0.15, pz + 0.04), material="HOUSE", bevel=0.004))
    tb = L.join(tp, "turret")
    L.node(tb, "turret", PIVOT, root)
    gun = L.cyl("gun", 0.015, 0.42, center=(0, py + 0.33, pz + 0.04), axis="Y", segs=8, material="steel")
    sleeve = L.cyl("sleeve", 0.026, 0.07, center=(0, py + 0.15, pz + 0.04), axis="Y", segs=10, material="hull_tan")
    bar = L.join([gun, sleeve], "barrel")
    L.node(bar, "barrel", (0, py + 0.12, pz + 0.04), tb)
    return tb


def build(house):
    root = L.empty("COMBAT_TANK")
    parts = tracks(root, house)
    {"atreides": fenders_atreides, "harkonnen": fenders_harkonnen, "ordos": fenders_ordos}[house](parts)
    hull_body(parts, house)
    h = L.join(parts, "hull")
    L.node(h, "hull", (0, 0, 0), root)
    {"atreides": turret_atreides, "harkonnen": turret_harkonnen, "ordos": turret_ordos}[house](root)
    return root


MODELS = {
    "unit/ATREIDES_COMBAT_TANK": dict(build=lambda: build("atreides"), view=dict(dist=2.0, target_z=0.15)),
    "unit/HARKONNEN_COMBAT_TANK": dict(build=lambda: build("harkonnen"), view=dict(dist=2.0, target_z=0.15)),
    "unit/ORDOS_COMBAT_TANK": dict(build=lambda: build("ordos"), view=dict(dist=2.0, target_z=0.15)),
}
