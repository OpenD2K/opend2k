"""Reimagined Atreides refinery - a "big-shape redesign" of the Reimagined pack (docs/3d-models.md "Big-shape
redesigns"; the first one was the harvester, reimagined_harvester.py). The design is the original's (two drums with
a skirt, a house-colour band, a tier of arches and a ribbed cream roof; flues on the roofs; a bridge with a dark
house-colour roof and a dome between them; a near-black tank with the tallest twin flue; the yellow unloading pad on
the dock cell) on the 3D layout of refinery_pilot_fable.py (refinery_dock.py: the pad on the dock cell, nothing in the
harvester's turning circle, the inlet socket where the docked harvester's pipe ends). Its BIG FORMS are rebuilt for
the game's 45-degree camera at zoom 1-2:

  * the drums in three tones with real depth: a flared rust skirt of ten large plates over a near-black core (the
    joints are dark gaps) with chunky light buttresses; the house band 2.5 px tall standing 0.7 px proud; twelve big
    arches with 1.15 px deep reveals (dark insides; lit windows at night) instead of twenty shallow ones; a cornice
    overhanging 0.8 px; a dark neck under a heavy brown rim;
  * the roofs as eight cream panels split by dark seams around a raised mid-tone lantern ring (a step that throws a
    circular shadow) and a dark hub with the flues;
  * the signal-yellow unloading pad with yellow / black hazard-striped rims and a steel turntable plate, flat at the deck
    height (nothing higher than the surface the harvester stands on);
  * a boxy steel unloading housing on the north drum's wall behind the inlet socket;
  * chunkier flues with dark bands at the same open tops (anchors fx_flue_0..4 unchanged), a ribbed near-black tank,
    the bridge's house roof a readable navy;
  * a dirtier surface in the manner of combat_tank_v3.py: stronger cavity dirt, grime and run-off streaks, sand
    drifted against the walls, tyre-worn pad.

    blender -b --factory-startup --python tools/models/reimagined_refinery.py -- \
        [--out DIR]          DIR/models/building/ATREIDES_REFINERY.glb, ATREIDES_REFINERY_lod1.glb,
                             DIR/models.ini.ATREIDES_REFINERY (anchors for the pack's models.ini), DIR/stats/*.json
        [--no-bake]          geometry only
        [--bake-size 2048] [--lod 0.36]
        [--preview DIR]      quick plain-colour views (no bake)
        [--turntable DIR] [--samples 40] [--work DIR]
        [--tris]             log the triangles per part
    install: tools/models/refinery_reimagined_check/install.sh DIR artpacks/models-reimagined

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up), on the
helpers / palette / layout of refinery_pilot_fable.py (imported as RF) and the Reimagined bake (reimagined_kit.py).
Nodes "base" and "main" as the main pack's model; anchors fx_flue_0..4 and fx_inlet_0 at the same places.
Nothing is copied from the original art.
"""
import json
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402
import refinery_pilot_fable as RF  # noqa: E402  (sets the building palette on import)
import refinery_dock as RD  # noqa: E402

KEY = "ATREIDES_REFINERY"
PX = V3.PX
log = V2.log
lathe, lathe_loop, obox, tube, polar, cone_z = RF.lathe, RF.lathe_loop, RF.obox, RF.tube, RF.polar, RF.cone_z
rings_solid, new_obj, shade = V3.rings_solid, V3.new_obj, V3.shade

# the redesign's own proportions (px); the layout itself (drum centres / radii, tank, pad, inlet) is RF.P after
# RF.layout_3d()
Q = dict(
    skirt_plates=10, skirt_gap=0.75, buttress_w=2.3, buttress_out=1.7,
    band_h=2.5, band_proud=0.7, skirt_in=0.6,
    arches=12, arch_depth=1.15,
    cornice_out=0.8, rim_out=0.9,
    roof_panels=8, roof_gap=0.8, lantern_r=8.6, lantern_up=1.3,
    pad_rim=2.4, stripe=2.9,
    house_band=0.8, band_drop=0.8, house_roof=0.16, house_ring=0.4,
)

# new materials (sRGB, (metallic, roughness, kind), dust film)
COL = dict(
    core=(0.060, 0.054, 0.048),       # under the skirt plates / roof panels: the hard dark seams
    buttress=(0.760, 0.620, 0.450),   # the skirt's buttresses: light rust
    lantern=(0.640, 0.520, 0.380),    # the raised roof ring: mid tan
    hazard=(0.090, 0.082, 0.072),     # the black of the hazard stripes
    steel_rf=(0.600, 0.585, 0.550),   # the unloading housing, the turntable plate
    pad=(1.000, 0.820, 0.200),        # the pad's floor and stripes: a saturated signal yellow (it must read at zoom 1)
)
SPEC = dict(core=(0.0, 0.9, "dark"), buttress=(0.25, 0.55, "steel"), lantern=(0.2, 0.5, "paint"),
            hazard=(0.0, 0.7, "dark"), steel_rf=(0.6, 0.42, "steel"))
FILM = dict(pad=(0.980, 0.800, 0.240), buttress=(0.600, 0.480, 0.340), lantern=(0.540, 0.440, 0.330), steel_rf=(0.500, 0.480, 0.440))
V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.update(FILM)
# the surface pass (combat_tank_v3's weights, a little dirtier): film, blotch, cavity, soft, grime, streak, chip, low dust
V3.WEATHER["paint"] = (0.85, 0.80, 1.00, 0.75, 0.95, 0.95, 0.55, 0.65)
V3.WEATHER["steel"] = (0.80, 0.40, 0.95, 0.55, 0.60, 0.50, 0.15, 0.45)
V3.WEATHER["dark"] = (0.0, 0.0, 0.55, 0.12, 0.0, 0.0, 0.0, 0.25)
V3.DIRT.update(cavity=(0.060, 0.050, 0.040), soft=(0.520, 0.420, 0.310), blotch=(0.600, 0.500, 0.370),
               grime=(0.340, 0.260, 0.185), streak=(0.360, 0.270, 0.185), chip=(0.240, 0.190, 0.150))
V3.GRIME_SCALE.update(pad=0.3, hazard=0.3, pad_rim=0.5)


# ------------------------------------------------------------------------------------------------
# Mesh helpers (px)


def sector(name, c, prof_out, prof_in, a0, a1, material, steps=4, gap=0.0, bev=0.0):
    """A shell sector of revolution about the vertical through c between the angles a0..a1 (deg): the cross
    section is the outer profile (r, z) bottom to top and the inner one; the sector's ends are drawn in by gap/2 px
    (a constant joint width at every radius)."""
    rings = []
    for i in range(steps + 1):
        s = i / steps
        ring = []
        for (r, z) in list(prof_out) + list(prof_in)[::-1]:
            g = math.degrees(gap / 2 / max(r, 0.5))
            a = math.radians(a0 + g + s * (a1 - a0 - 2 * g))
            ring.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a), z))
        rings.append(ring)
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, bev, 2)


def radial_prism(name, c, ang, prof_rz, half_w, material, bev=0.0):
    """A block standing radially on a drum at ang (deg): side profile (r, z) extruded +-half_w across."""
    a = math.radians(ang)
    ax, ay = math.cos(a), math.sin(a)
    tx, ty = -ay, ax
    bm = bmesh.new()
    rings_solid(bm, [[(c[0] + ax * r + tx * w, c[1] + ay * r + ty * w, z) for (r, z) in prof_rz] for w in (-half_w, half_w)])
    return new_obj(name, bm, material, bev, 2)


def roof_panel(name, c, r0, r1, a0, a1, gap, thick, material, lift=0.0, steps=4):
    """A panel of the conical roof between radii r0..r1 and angles a0..a1 (deg), its sides drawn in by gap/2."""
    rings = []
    for i in range(steps + 1):
        s = i / steps
        ring = []
        for (r, dz) in ((r0, 0.0), (r1, 0.0), (r1, -thick), (r0, -thick)):
            g = math.degrees(gap / 2 / r)
            a = math.radians(a0 + g + s * (a1 - a0 - 2 * g))
            p = (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
            ring.append((p[0], p[1], cone_z(c, p) + lift + dz))
        rings.append(ring)
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, 0.12, 1)


# ------------------------------------------------------------------------------------------------
# Parts


def drum(base, main, tag, c, hub_r):
    """One drum: base = skirt, band, arches, cornice, neck; main = rim and roof. hub_r: the dark hub's radius (the
    flues stand on it)."""
    P = RF.P
    rf, rb = P["r_foot"], P["r_band"]
    zb0 = P["z_band0"] - Q["band_drop"]           # a lower band: a taller arch tier
    zb1 = zb0 + Q["band_h"]
    # plinth and the dark core the skirt plates stand on (what shows in their joints)
    base.append(lathe(f"{tag}_foot", c, [(rf + 0.35, 0.0), (rf + 0.35, 0.55), (rf - 0.2, 1.0)], "foot", 72))
    # the skirt is drawn in (skirt_in) so that the buttresses stand proud and still end inside the cells / out of the
    # dock's keep-out (the drums stand 0.4 px from the footprint edge)
    rf = rf - Q["skirt_in"] - 0.45
    rb = rb - Q["skirt_in"]
    base.append(lathe(f"{tag}_core", c, [(rf - 0.6, 0.6), (rb - 0.7, zb0 + 0.2)], "core", 48))
    n = Q["skirt_plates"]
    step = 360.0 / n
    a_off = -27.0 if tag == "dr" else 10.0           # a joint (buttress) where the north drum's inlet housing sits
    for k in range(n):
        a0 = a_off + k * step
        base.append(sector(f"{tag}_skirt{k}", c, [(rf + 0.45, 0.9), (rb + 0.05, zb0)], [(rb - 0.9, zb0), (rf - 0.6, 0.9)],
                           a0, a0 + step, "skirt", steps=6, gap=Q["skirt_gap"]))
        # a horizontal strap across the plate (a step half-way up: a shadow line)
        def sr(z):
            return rf + 0.45 + (rb + 0.05 - rf - 0.45) * (z - 0.9) / (zb0 - 0.9)
        base.append(sector(f"{tag}_strap{k}", c, [(sr(4.0) + 0.45, 4.0), (sr(5.3) + 0.45, 5.3)], [(sr(5.3) - 0.6, 5.3), (sr(4.0) - 0.6, 4.0)],
                           a0, a0 + step, "buttress", steps=6, gap=Q["skirt_gap"] + 1.6))
        bo, bw = Q["buttress_out"], Q["buttress_w"] / 2
        base.append(radial_prism(f"{tag}_buttress{k}", c, a0, [(rb - 1.0, 0.9), (rf + bo - 0.3, 0.9), (rf + bo - 0.3, 2.0), (rb + 0.75, zb0 + 0.1), (rb - 1.0, zb0 + 0.1)],
                                 bw, "buttress", 0.25))
    # the house band: a ring standing proud with a dark undercut (its shadow line) and a dark top edge
    bp = Q["band_proud"]
    base.append(lathe(f"{tag}_undercut", c, [(rb - 0.4, zb0 - 0.1), (rb - 0.4, zb0 + 0.35)], "core", 72))
    band = lathe(f"{tag}_band", c, [(rb + bp - 0.25, zb0 + 0.3), (rb + bp, zb0 + 0.55), (rb + bp, zb1 - 0.35), (rb + bp - 0.3, zb1)], "HOUSE", 72)
    base.append(shade(band, Q["house_band"]))
    base.append(lathe(f"{tag}_band_cap", c, [(rb + bp - 0.3, zb1 - 0.02), (rb + bp - 0.1, zb1 + 0.15), (P["r_arch0"] - 0.2, zb1 + 0.35)], "rim", 72))
    # arches: big bays with deep reveals over a dark back wall
    za0, za1 = zb1 + 0.3, P["z_arch1"]
    ra0, ra1 = P["r_arch0"], P["r_arch1"]
    d = Q["arch_depth"]
    base.append(lathe(f"{tag}_back", c, [(ra0 - d - 0.1, za0 - 0.2), (ra1 - d - 0.1, za1)], "arch_in", 72))
    base.append(RF.arch_tier(f"{tag}_arches", c, Q["arches"], za0, za1, ra0, ra1, d, "wall"))
    # the cornice: a heavy ledge overhanging the arches
    co = Q["cornice_out"]
    zc = P["z_corn"]
    base.append(lathe(f"{tag}_cornice", c, [(ra1 - 0.2, za1 - 0.05), (ra1 + co, za1 + 0.35), (ra1 + co, zc + 0.5), (ra1 + co - 0.4, zc + 0.8),
                                            (P["r_neck"], zc + 0.8)], "wall", 72))
    base.append(lathe(f"{tag}_neck", c, [(P["r_neck"] - 0.3, zc + 0.6), (P["r_neck"] - 0.3, P["z_rim0"] + 0.2)], "neck", 48))
    # --- main: the rim (heavy, overhanging the neck) and the roof
    ro, rr, zr0, zroof = Q["rim_out"], P["r_rim"], P["z_rim0"], P["z_roof"]
    main.append(lathe_loop(f"{tag}_rim", c, [(P["r_neck"] + 0.1, zr0 - 0.4), (rr + ro, zr0 - 0.2), (rr + ro + 0.3, zr0 + 0.5), (rr + ro + 0.3, zr0 + 1.5),
                                             (rr + 0.2, zroof - 0.5), (P["r_roof"] + 0.5, zroof + 0.15), (P["r_roof"] - 0.8, zroof - 0.1),
                                             (P["r_roof"] - 0.8, zr0 + 0.6), (P["r_neck"] + 0.1, zr0 + 0.3)], "rim", 72))
    # the roof's dark core cone (seen in the panel joints and round the lantern)
    lr = Q["lantern_r"]
    main.append(lathe(f"{tag}_roofcore", c, [(P["r_roof"] + 0.1, zroof - 0.75), (lr - 1.0, cone_z(c, (c[0] + lr, c[1])) - 0.6)], "core", 48))
    nr = Q["roof_panels"]
    a_rp = 22.5
    for k in range(nr):
        a0 = a_rp + 360.0 * k / nr
        main.append(roof_panel(f"{tag}_panel{k}", c, lr + 0.75, P["r_roof"] + 0.25, a0, a0 + 360.0 / nr, Q["roof_gap"], 0.6,
                               "roof" if k % 2 == 0 else "roof2", lift=0.05))
        # a raised rib down the middle of every panel (the original's radial ribs, now a few heavy ones)
        am = a0 + 180.0 / nr
        p0, p1 = polar(c, lr + 1.4, am), polar(c, P["r_roof"] - 0.4, am)
        main.append(RF.beam(f"{tag}_rib{k}", (*p0, cone_z(c, p0) + 0.35), (*p1, cone_z(c, p1) + 0.35), 0.9, 0.6, "rib", 0.12))
    # the lantern: a raised ring (mid tan) with dark vent slots round its side, a dark hub
    zl = cone_z(c, (c[0] + lr, c[1]))
    up = Q["lantern_up"]
    main.append(lathe(f"{tag}_lantern", c, [(lr, zl - 0.6), (lr, zl + up - 0.25), (lr - 0.35, zl + up), (hub_r + 1.2, P["z_apex"] + up - 0.2),
                                            (hub_r + 1.0, P["z_apex"] + up)], "lantern", 48))
    for k in range(6):
        a = 360.0 * (k + 0.5) / 6
        p = polar(c, lr + 0.02, a)
        main.append(obox(f"{tag}_slot{k}", p, a, 0.18, 1.8, zl + 0.05, zl + up - 0.45, "recess"))
    main.append(lathe(f"{tag}_hub", c, [(hub_r + 1.0, P["z_apex"] + up - 0.1), (hub_r, P["z_apex"] + up + 0.9), (hub_r, P["z_apex"] + up + 1.5)], "rim", 24))


def stacks(base, main):
    """The five open flues at RF's stack tops (the anchors fx_flue_0..4) - thicker, with dark bands - and the
    near-black tank with the tall twin flue."""
    P = RF.P
    d1, d2, tk = P["d1"], P["d2"], P["tank"]
    tops = RF.stack_tops()
    z1 = P["z_apex"] + Q["lantern_up"] + 1.2
    RF.open_pipe(main, "pl_a", tops["left_a"], 1.35, z1, "copper_dk", lip=0.3, segs=12)
    RF.open_pipe(main, "pl_b", tops["left_b"], 1.75, z1, "copper", lip=0.4, segs=12)
    main.append(lathe("pl_clamp", (d1[0] + 0.4, d1[1]), [(3.3, 44.0), (3.3, 45.4)], "copper_dk", 14))
    RF.open_pipe(main, "pr", tops["right"], 2.8, z1, "copper", lip=0.55, segs=16, steps=((2.8, 38.0), (2.5, 39.0)))
    main.append(lathe("pr_collar", d2, [(3.5, 49.8), (4.1, 50.4), (4.1, 52.8), (3.5, 53.4)], "white", 16))
    for s in (-1, 1):
        main.append(obox(f"pr_lug{s}", (d2[0] + s * 5.0, d2[1]), 0.0, 1.6, 1.0, 50.6, 52.6, "white", 0.15))
    # the tank: plinth, a ribbed near-black body, a wide house ring, a steel top
    tr, th = P["tank_r"] + 0.3, P["tank_h"]
    base.append(lathe("tank_plinth", tk, [(tr + 1.2, 0.0), (tr + 1.2, 0.8), (tr + 0.6, 1.2)], "foot", 24))
    base.append(lathe("tank", tk, [(tr, 0.8), (tr, th - 0.8)], "tank", 24))
    for k, z in enumerate((2.4, 4.9)):
        base.append(lathe(f"tank_hoop{k}", tk, [(tr + 0.45, z), (tr + 0.45, z + 0.8)], "block_dk", 24))
    ring = lathe("tank_ring", tk, [(tr + 0.3, 7.0), (tr + 0.4, 7.3), (tr + 0.4, 9.6), (tr + 0.3, 9.9)], "HOUSE", 24)
    base.append(shade(ring, Q["house_ring"]))
    base.append(lathe("tank_top", tk, [(tr + 0.5, th - 1.0), (tr + 0.5, th - 0.2), (tr - 0.4, th + 0.3), (3.2, th + 1.1)], "steel_rf", 24))
    RF.open_pipe(main, "pt_cream", tops["tall_cream"], 1.3, th + 0.5, "cream_pipe", lip=0.25, segs=12)
    RF.open_pipe(main, "pt_copper", tops["tall_copper"], 2.2, th + 0.8, "copper", lip=0.45, segs=14)
    main.append(lathe("pt_collar", tk, [(3.6, th + 0.8), (3.6, th + 2.2), (2.9, th + 2.9)], "block", 16))
    for z in (32.0, 52.0):
        main.append(lathe(f"pt_clamp{int(z)}", (tk[0] - 0.5, tk[1]), [(3.4, z), (3.4, z + 1.3)], "copper_dk", 14))


def inlet_housing(main):
    """The unloading station: RF's inlet socket (same mouth: the docked harvester's pipe ends in it) coming out of a
    boxy steel housing on the north drum's wall, a dark louvre on its face, a cap slab."""
    RF.inlet(main)
    o, d = RF.inlet_axis()
    il = RF.P["inlet"]
    back = o + d * il["back"]
    ang = math.degrees(math.atan2(d.y, d.x))
    c = back - d * 2.6
    main.append(obox("inlet_housing", (c.x, c.y), ang, 2.6, 4.2, RF.P["z_band0"] + 0.4, back.z + 4.4, "steel_rf", 0.3))
    main.append(obox("inlet_cap", (c.x - d.x * 0.3, c.y - d.y * 0.3), ang, 3.1, 4.8, back.z + 4.4, back.z + 5.3, "block_dk", 0.2))
    for s in (-1, 1):
        q = c + d * 2.62 + Vector((-d.y, d.x, 0.0)) * (s * 3.0)
        main.append(obox(f"inlet_louvre{s}", (q.x, q.y), ang, 0.08, 0.7, back.z - 1.6, back.z + 3.2, "recess"))


def pad(base):
    """The yellow unloading pad on the dock cell (RF's 3D layout: a 45-degree tray cut to the cell, an octagon),
    its rims striped yellow / black, the apron towards the drum ochre, a steel turntable plate in the middle as
    high as the rims (the deck); nothing higher than that."""
    P = RF.P
    c, hl, hw = P["pad_c"], P["pad_len"], P["pad_wid"]
    h, fl, rim = P["pad_h"], P["pad_floor"], Q["pad_rim"]
    ax, ay = math.cos(math.radians(45)), math.sin(math.radians(45))
    nx, ny = -ay, ax
    tray = []
    split = P["pad_split"]
    for nm, n0, n1, m_ in (("pad_floor", -(hw - rim * 0.5), split, "pad"), ("pad_apron", split, hw - rim * 0.5, "pad_dk")):
        cc = (c[0] + nx * (n0 + n1) / 2, c[1] + ny * (n0 + n1) / 2)
        tray.append(obox(nm, cc, 45.0, hl - rim * 0.5, (n1 - n0) / 2, 0.0, fl if m_ == "pad" else fl - 0.15, m_))
    cc = (c[0] + nx * split, c[1] + ny * split)
    tray.append(obox("pad_kerb", cc, 45.0, hl - rim, 0.5, 0.0, fl + 0.45, "hazard", 0.1))
    st = Q["stripe"]

    def striped(tag, cc, along, length, half_w):
        """A rim of `length` along the direction `along` (deg) in alternating yellow / black blocks."""
        k = max(3, int(round(length / st)))
        if k % 2 == 0:
            k += 1
        unit = length / ((k + 1) // 2 * 2.0 + (k // 2))   # yellow blocks twice as long as the black ones
        a = math.radians(along)
        t = -length / 2
        for i in range(k):
            seg = unit * (2.0 if i % 2 == 0 else 1.0)
            p = (cc[0] + math.cos(a) * (t + seg / 2), cc[1] + math.sin(a) * (t + seg / 2))
            tray.append(obox(f"{tag}{i}", p, along, seg / 2, half_w, 0.0, h, "pad" if i % 2 == 0 else "hazard", 0.12))
            t += seg
    for s in (-1, 1):
        striped(f"pad_rimL{s}_", (c[0] + nx * s * (hw - rim / 2), c[1] + ny * s * (hw - rim / 2)), 45.0, 2 * hl, rim / 2)
        striped(f"pad_rimS{s}_", (c[0] + ax * s * (hl - rim / 2), c[1] + ay * s * (hl - rim / 2)), 135.0, 2 * hw - 2 * rim, rim / 2)
    reach = RD.BOX - c[0]
    tray = RD.clip_plan(tray, c, reach)
    for i, (cc, ang, half) in enumerate(RD.pad_chamfers(c, hl, hw, reach)):
        cc = (cc[0] - (rim / 2) * (1 if cc[0] > c[0] + reach - 0.1 else -1 if cc[0] < c[0] - reach + 0.1 else 0),
              cc[1] - (rim / 2) * (1 if cc[1] > c[1] + reach - 0.1 else -1 if cc[1] < c[1] - reach + 0.1 else 0))
        tray.append(obox(f"pad_rimC{i}", cc, ang, half + rim * 0.2, rim / 2, 0.0, h, "pad", 0.2))
    # the turntable plate: steel, a dark ring and four dark notches
    tray.append(lathe("pad_plate_well", c, [(6.4, fl - 0.12), (6.4, fl + 0.05)], "hazard", 32))
    tray.append(lathe("pad_plate", c, [(5.6, fl - 0.1), (5.6, h - 0.15), (5.3, h)], "steel_rf", 32))
    tray.append(lathe_loop("pad_plate_ring", c, [(2.6, h - 0.06), (2.6, h + 0.02), (2.1, h + 0.02), (2.1, h - 0.06)], "recess", 20))
    for k in range(4):
        p = polar(c, 4.2, 45.0 + 90.0 * k)
        tray.append(obox(f"pad_notch{k}", p, 45.0 + 90.0 * k, 0.9, 0.35, h - 0.08, h + 0.02, "recess"))
    base.extend(tray)


def extras(base, main):
    """The white steps at the south-west of the west drum, the north roof's raised wedge with its orange edge and
    pipe, a hatch on the west roof."""
    P = RF.P
    d1, d2 = P["d1"], P["d2"]
    a = P["stairs_ang"]
    rf = P["r_foot"]
    for k, (r_in, r_out, z) in enumerate(((rf - 2.5, rf + 0.8, 10.8), (rf + 0.6, rf + 2.4, 7.4), (rf + 2.2, rf + 4.0, 4.0))):
        cc = polar(d1, (r_in + r_out) / 2, a)
        base.append(obox(f"step{k}", cc, a, (r_out - r_in) / 2, 5.6, 0.0, z, "white", 0.3))
    base.append(obox("step_cheek", polar(d1, rf + 1.0, a), a, 3.2, 0.6, 0.0, 10.0, "block_dk", 0.15))
    # the wedge: a raised sector of the north roof's panels (lighter, an orange edge)
    w0, w1, lift = -112.5, -67.5, 0.9
    main.append(roof_panel("wedge", d2, Q["lantern_r"] + 0.9, P["r_roof"] + 0.2, w0, w1, 1.2, 0.8, "white", lift=lift + 0.05))
    for aa in (w0 + 0.8, w1 - 0.8):
        p0, p1 = polar(d2, Q["lantern_r"] + 1.0, aa), polar(d2, P["r_roof"], aa)
        main.append(tube(f"wedge_edge{int(aa)}", (*p0, cone_z(d2, p0) + lift + 0.3), (*p1, cone_z(d2, p1) + lift + 0.3), 0.5, "orange", 6))
    ang = -45.0
    p0, p1 = polar(d2, Q["lantern_r"] - 0.5, ang), polar(d2, P["r_roof"] + 0.6, ang)
    main.append(tube("orange_pipe", (*p0, cone_z(d2, p0) + 1.6), (*p1, P["z_roof"] + 1.2), 0.85, "orange", 10))
    p2 = polar(d2, P["r_rim"] + Q["rim_out"] + 1.0, ang)
    main.append(tube("orange_drop", (*p1, P["z_roof"] + 1.2), (*p2, P["z_rim0"] - 1.0), 0.85, "orange", 10))
    hp = polar(d1, 12.0, 300.0)
    main.append(lathe("hatch", hp, [(2.0, cone_z(d1, hp) - 0.2), (2.0, cone_z(d1, hp) + 0.8), (1.5, cone_z(d1, hp) + 1.0)], "rim", 12))


def bridge(base, main):
    """RF's bridge and dome, the house roof a readable navy instead of near-black, a dark window band on the
    dome's pedestal."""
    dome = RF.bridge(base, main)
    for o in main:
        if o.name.startswith("bridge_roof"):
            shade(o, Q["house_roof"])
    dc, r, z = dome["c"], RF.P["dome_r"], RF.P["dome_z"]
    main.append(lathe("dome_windows", dc, [(r + 0.35, z - 5.0), (r + 0.35, z - 3.4)], "recess", 24))
    main.append(lathe("dome_sill", dc, [(r + 0.8, z - 5.6), (r + 0.8, z - 5.0)], "wall", 24))


ANCHORS = {}


def build():
    RF.layout_3d()
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    base, main = [], []
    drum(base, main, "dl", RF.P["d1"], 3.6)
    drum(base, main, "dr", RF.P["d2"], 3.6)
    stacks(base, main)
    inlet_housing(main)
    bridge(base, main)
    pad(base)
    extras(base, main)
    nodes = []
    for name, parts in (("base", base), ("main", main)):
        node = V3.finish_node(parts, name, (0, 0, 0))
        node.parent = root
        nodes.append(node)
    ANCHORS.clear()
    tops = RF.stack_tops()
    for i, (key, power) in enumerate(RF.FLUE_ANCHORS):
        x, y, z, _r = tops[key]
        ANCHORS[f"fx_flue_{i}"] = ((x, y, z), power)
    q = RF.inlet_anchor()
    ANCHORS["fx_inlet_0"] = ((q.x, q.y, q.z), None)
    for name, ((x, y, z), power) in ANCHORS.items():
        e = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(e)
        e.location = (x * PX, y * PX, z * PX)
        if power is not None:
            e.scale = (power, power, power)
        e.parent = root
    bpy.context.view_layer.update()
    return root, nodes


def write_ini(path):
    """The [ATREIDES_REFINERY] section for the pack's models.ini (glb axes: x east, y up, z south; tiles)."""
    with open(path, "w") as f:
        f.write(f"[{KEY}]\n; reimagined_refinery.py (3D layout of refinery_dock.py): fx_flue_N the open flue tops (fire / smoke), "
                "fx_inlet_0 where the docked harvester's pipe ends\n")
        for name, ((x, y, z), power) in ANCHORS.items():
            f.write(f"anchor.{name} = {x * PX:.4f} {z * PX:.4f} {-y * PX:.4f}" + (f" {power:g}\n" if power is not None else "\n"))


def surface(nt, name):
    """RF's building weathering (sand up the walls, run-off streaks, soot under the flue tops, a worn pad) on the
    dirtier tank-v3 weights set above, plus tyre marks across the pad and rust run-off under the cornices."""
    c, r, m = RF.surface(nt, name)
    nodes, links = nt.nodes, nt.links
    if name in ("pad", "pad_dk", "hazard", "steel_rf"):
        geo = nodes.new("ShaderNodeNewGeometry")
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Rotation"].default_value = (0.0, 0.0, math.radians(45.0))
        mp.inputs["Scale"].default_value = (1.0, 0.06, 1.0)
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = 30.0
        n.inputs["Detail"].default_value = 2.0
        links.new(mp.outputs[0], n.inputs["Vector"])
        mr = nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = 0.52
        mr.inputs["From Max"].default_value = 0.68
        mr.inputs["To Min"].default_value = 0.0
        mr.inputs["To Max"].default_value = 0.28 if name == "pad" else 0.25
        links.new(n.outputs["Fac"], mr.inputs["Value"])
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        links.new(mr.outputs[0], mx.inputs["Factor"])
        links.new(c, mx.inputs[6])
        mx.inputs[7].default_value = (*V2.srgb_to_linear((0.36, 0.27, 0.16)), 1)
        c = mx.outputs[2]
    return c, r, m


# ------------------------------------------------------------------------------------------------


def run(o):
    import reimagined_kit as RK
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    V3._mats.clear()
    t0 = time.time()
    root, nodes = build()
    for name in list(V3._mats):
        RF.mat(name)
    tris = V2.tri_count(V2.meshes_under(root))
    log(f"built {KEY}: {len(nodes)} nodes, {tris} triangles in {time.time() - t0:.1f} s")
    if o.get("preview"):
        import reimagined_build as RB
        RB.preview(root, o["preview"], KEY, 6.5)
        return
    if not o.get("out"):
        return
    out = o["out"]
    bld = os.path.join(out, "models", "building")
    path = os.path.join(bld, KEY + ".glb")
    png_dir = o.get("work") or os.path.join(bpy.app.tempdir, "d2k_reimagined_" + KEY)
    pngs = None
    if o.get("bake", True):
        det = dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.016, house_uv=0.6, ao_distance=0.3, dent=0.22, wear=0.5)
        _, pngs = RK.bake_reimagined(V2.meshes_under(root), o.get("bake_size", 2048), surface, png_dir, det)
    V3.export_glb(root, path)
    if pngs:
        RK.glb_patch(path, pngs["color"], pngs["orm"], pngs["normal"])
    log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    lod = os.path.join(bld, KEY + "_lod1.glb")
    lt = RK.export_lod(root, lod, o.get("lod", 0.36))
    RK.strip_images(lod)
    write_ini(os.path.join(out, "models.ini." + KEY))
    os.makedirs(os.path.join(out, "stats"), exist_ok=True)
    json.dump(dict(key=KEY, triangles=tris, lod1=lt, glb_kb=os.path.getsize(path) // 1024, bake=o.get("bake_size", 2048)),
              open(os.path.join(out, "stats", KEY + ".json"), "w"))
    if pngs and o.get("turntable"):
        RK.review(root, o["turntable"], png_dir, KEY, samples=o.get("samples", 40), dist=6.0, target=(0, 0, 0.45),
                  closeups=[("closeup_pad", -50, 30, 3.0, 70, (0.7, -0.1, 0.25)), ("closeup_drum", -100, 22, 3.2, 70, (-0.6, -0.3, 0.5)),
                            ("closeup_game", -90, 45, 4.6, 60, (0.0, 0.1, 0.4))])
    log(f"total {KEY} {time.time() - t0:.1f} s")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    o = dict(bake=True, bake_size=2048, samples=40, lod=0.36)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--preview", "--turntable", "--work"):
            o[a[2:]] = argv[i + 1]; i += 1
        elif a in ("--bake-size", "--samples"):
            o[a[2:].replace("-", "_")] = int(argv[i + 1]); i += 1
        elif a == "--lod":
            o["lod"] = float(argv[i + 1]); i += 1
        elif a == "--no-bake":
            o["bake"] = False
        elif a == "--tris":
            o["tris"] = True
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    run(o)


if __name__ == "__main__":
    main()
