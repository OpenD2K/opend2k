"""Reimagined Atreides refinery, redesign v2 - the wind trap recipe (docs/3d-models.md "Refinery redesign v2"): the
approved model EXACTLY as it is, plus human-scale detail, real materials and the wind traps' lived-in weathering.

The first attempt (reimagined_refinery.py, a big-shape redesign) was rejected: "our first one looks better". So here
nothing of the approved model is re-formed. Every one of its parts is built by its own generator
(refinery_pilot_fable.py on its 3D layout: the two drums with the ribbed cream roofs, arches, house bands and rust
skirts, the five flues, the bridge with the navy house roof and the dome, the near-black tank with the tall twin flue,
the yellow unloading pad, the inlet socket, the white steps, the wedge with the orange pipe) - same shapes, places,
proportions and colours, same anchors fx_flue_0..4 / fx_inlet_0, same nodes `base` / `main`. What is new:

  * human-scale parts, all outside the harvester's reach (KEEP below): a service door with a lamp at the top of the
    white steps and hand rails on them, the ladders continued down to the ground and up over the rims to railed
    roof landings, an extractor fan on each roof, a louvred vent on the west skirt, stencilled unit numbers on the
    rims; on the bridge a door with a lamp, a control panel, louvres in the three wall vents, lit windows, a railed
    roof edge and a weather mast (vane, anemometer, beacon, whip antenna); in the yard between the drums a flanged
    orange pipe with a valve and a red hand wheel, two barrels; rungs and bands on the flues, a red beacon on the
    tallest; a ladder, a gauge and a valve at the tank; in the free north-east corner a cooler with a big fan, a
    control cabinet with a door, a lamp post; small lit studs in the pad's floor (below the rims: nothing on the pad
    is higher than before);
  * materials and weathering: building_common's surface pass, dirtier than the wind traps (WEATHERING 1.6, MOTTLE 1.8: the owner asked for more dirt) with
    the Atreides grime colours; the cream roofs (since 2026-11-19 nearly the paint's full grime and dust: the approved model's brown grungy roofs) and the signal-yellow pad are kinds of their own that keep their
    colour (little dust film: KIND / WEAR below), the white steps take less of it; soot round the five flue tops, and
    the refinery's own stain (SURFACE_EXTRA = spice): cinnamon spice dust on the pad round
    the plate and towards the inlet, and running down the north drum's wall below the socket;
  * moving parts are nodes of their own (the engine's Building life spins them by name), effect points are empties
    (NODES / ANCHOR_DOC below).

    blender -b --factory-startup --python tools/models/reimagined_refinery_v2.py -- --out DIR [--work DIR]
        [--no-bake] [--bake-size 2048] [--lod 0.34] [--weathering 1.6] [--mottle 1.8] [--preview DIR]
        [--turntable DIR --samples 24] [--tris]
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined ATREIDES_REFINERY
    check:   .venv/bin/python -I tools/models/refinery_dock_check.py artpacks/models-reimagined \
                 --refinery DIR/models/building/ATREIDES_REFINERY.glb --scale 1.25 --allow-turn 5

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up).
Nothing is copied from the original art.
"""
import math
import os
import sys

from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import building_common as BC  # noqa: E402  (helpers, the surface / weathering pass, build, run)
import refinery_dock as RD  # noqa: E402  (the dock's numbers)

RF, K, PX = BC.RF, BC.K, BC.PX
beam, tube, obox, polar = BC.beam, BC.tube, BC.obox, BC.polar
RF.layout_3d()                       # the pack's glb is the 3D layout (docs/3d-models.md "Docking")
P = RF.P

KEY = "ATREIDES_REFINERY"

NODES = dict(
    base="the approved model's ground parts (drum walls, tank, pad), as before",
    main="everything else that does not move",
    fan_0="the extractor fan on the west drum's roof (vertical axis)",
    fan_1="the extractor fan on the north drum's roof",
    fan_2="the cooler's fan in the north-east corner (5 px across: the one seen at the default zoom)",
    vane="the wind vane on the bridge's weather mast",
    anemometer="the cup anemometer on the mast's arm",
)
ANCHOR_DOC = dict(
    fx_flue_0="..4: the five open flue tops, unchanged (the refinery vent effect: flames and smoke while unloading)",
    fx_inlet_0="the harvester inlet, unchanged (docking)",
    fx_vent_0="the west roof's extractor: a warm puff", fx_vent_1="the north roof's extractor", fx_vent_2="the cooler's outlet",
    fx_beacon_0="the red beacon on the weather mast", fx_beacon_1="the red lamp on the tallest flue's rim",
    fx_light_0="the lamp over the bridge's door", fx_light_1="the lamp over the door at the top of the white steps",
    fx_light_2="the lamp over the control cabinet's door", fx_light_3="the lamp post by the cooler",
)

# ------------------------------------------------------------------------------------------------
# The dock: what the harvester needs stays as it is. Everything this file ADDS keeps out of the cylinder the 1.25x
# harvester sweeps when it turns on the dock centre (radius 29.5 px below 8 px of its own height, 26.7 above, 37.8 high;
# refinery_dock_check.py) with a little air - so the check's numbers cannot change - except parts no higher than the
# pad's rims (the deck).
KEEP_LOW, KEEP_HIGH, KEEP_TOP, KEEP_SPLIT = 30.0, 27.5, 40.5, 12.5


def keep_out(objs, label, origin=(0.0, 0.0, 0.0)):
    """Raise if a vertex of the new parts `objs` (px; origin = a moving node's place) is inside the keep-out or
    outside the footprint."""
    bad = []
    for o in objs:
        for v in o.data.vertices:
            x, y, z = v.co.x + origin[0], v.co.y + origin[1], v.co.z + origin[2]
            if max(abs(x), abs(y)) > RD.BOX - 0.1:
                bad.append((o.name, "outside the cells", round(x, 1), round(y, 1), round(z, 1)))
            d = RD.dist((x, y), RD.DOCK)
            if z > P["pad_h"] + 1e-3 and ((z < KEEP_SPLIT and d < KEEP_LOW) or (z < KEEP_TOP and d < KEEP_HIGH)):
                bad.append((o.name, "in the harvester's reach", round(x, 1), round(y, 1), round(z, 1)))
    if bad:
        raise SystemExit(f"{label}: {len(bad)} vertices misplaced, e.g. {bad[:4]}")
    return objs


def drop(objs, prefixes):
    """Remove the parts whose names start with one of `prefixes` from the list `objs` (and the scene)."""
    import bpy
    for o in [o for o in objs if o.name.startswith(prefixes)]:
        objs.remove(o)
        bpy.data.objects.remove(o)


# ------------------------------------------------------------------------------------------------
# Palette: the approved model's own (refinery_pilot_fable.COL) + the fittings (building_common) + two of its own

COL = dict(
    stencil_lt=(0.860, 0.810, 0.690),   # the unit numbers on the brown rims: pale stencil paint
    barrel=(0.300, 0.340, 0.360),       # a blue-grey drum
)
SPEC = dict(stencil_lt=(0.0, 0.55, "paint"), barrel=(0.0, 0.5, "paint"))
# The pad stays a signal yellow (the rejected redesign's grime turned it into a grey octagon at zoom 1; with the
# paint's dust film and the sand that lies on everything low it went ochre here too): its floor is a kind of its
# own, "signal" - paint that is swept: little film, top dust and foot sand, few stains (the rows of building_common's
# WEAR / GRIME_W). Its cream rims take 0.9 and the white steps 0.8 of the weathering.
# The roofs stay the bright cream with the fine brown ribs (what reads as Dune 2000; the first redesign lost it): on
# the wind traps' curved shells the tan dust only gathers on the crest, on these two big flat discs the paint's dust
# film and top dust would tan the whole roof. Kind "cream": the paint's stains, run-off, chips and wear (the dirt of
# the model before), but a thin film and little dust on top.
KIND = dict(pad="signal", roof="cream", roof2="cream")
WEAR = dict(signal=(0.040, 0.20, 0.34, 0.60, 0.35, 0.25, 0.22, 0.16, 0.16, 0.22),
            cream=(0.050, 0.46, 0.40, 0.65, 0.40, 0.26, 0.38, 0.32, 0.40, 0.50))
GRIME_W = dict(signal=(0.65, 0.35, 0.25, 0.15, 0.00, 0.30), cream=(0.85, 0.65, 0.60, 0.85, 0.75, 0.55))
MAT_GRIME = dict(pad_rim=0.9, white=0.8)
# spice: its colour, and how much of it each material takes (by name, else by kind)
SPICE = (0.740, 0.400, 0.150)
SPICE_DK = (0.500, 0.250, 0.100)
SPICE_W = dict(pad=0.60, pad_dk=0.90, pad_rim=0.50, inlet=0.22, arch_in=0.0, recess=0.0, burnt=0.0, soot=0.0)
SPICE_KIND = dict(paint=0.60, steel=0.60, dark=0.30, rubber=0.30)


def spice(T, name, kind, ctx, cur, rough_s, metal_s, W):
    """SURFACE_EXTRA: the refinery's own stain. Spice dust (cinnamon) where it is handled: on the pad round the plate
    and towards the inlet (patches; only what lies low), and on the north drum's wall under the inlet socket
    (streaks on upright faces, blotches on the rest; the steel socket itself only gets a dusting)."""
    k = SPICE_W.get(name, SPICE_KIND.get(kind, 0.0))
    if not k:
        return cur, rough_s, metal_s
    nodes, links = T.nodes, T.links
    z = ctx["pos"].outputs["Z"]

    def fall(p, r_out, r_in, zs=1.0):                     # 1 within r_in px of p, 0 beyond r_out (heights x zs)
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = (1.0, 1.0, zs)
        links.new(T.geo.outputs["Position"], mp.inputs["Vector"])
        d = nodes.new("ShaderNodeVectorMath")
        d.operation = "DISTANCE"
        links.new(mp.outputs[0], d.inputs[0])
        d.inputs[1].default_value = (p[0] * PX, p[1] * PX, p[2] * PX * zs)
        return T.remap(d.outputs["Value"], r_out * PX, r_in * PX, 0.0, 1.0)

    dock = T.op("MAXIMUM", fall((*RD.DOCK, 1.5), 15.5, 5.0), fall((22.5, 9.5, 1.5), 11.0, 3.0))
    low = T.remap(z, 4.0 * PX, 2.4 * PX, 0.0, 1.0)
    patch = T.mul(T.remap(T.noise(6.5, 4.0, offset=51.0), 0.30, 0.58, 0.0, 1.0), T.remap(T.noise(21.0, 3.0, offset=53.0), 0.30, 0.70, 0.55, 1.0))
    f_dock = T.mul(T.mul(dock, low), T.op("MAXIMUM", patch, T.mul(dock, 0.45)))
    il = RF.inlet_anchor()
    streak = T.remap(T.noise(28.0, 2.0, mapping=(1.0, 1.0, 0.06), offset=57.0), 0.36, 0.66, 0.25, 1.0)
    blot = T.remap(T.noise(9.0, 3.0, offset=59.0), 0.30, 0.68, 0.30, 1.0)
    under = T.remap(z, (il.z - 0.4) * PX, (il.z - 3.4) * PX, 0.25, 1.0)        # (the socket's underside is 3 px below its axis)
    f_in = T.mul(T.mul(fall((il.x, il.y, il.z - 3.5), 11.5, 3.0, 0.5), T.mixf(ctx["upright"], blot, streak)),
                 T.mul(under, T.remap(z, (il.z + 3.4) * PX, (il.z + 1.0) * PX, 0.0, 1.0)))
    f = T.mul(T.op("MAXIMUM", f_dock, f_in), min(0.9, k * W))
    cur = T.mix(f, cur, T.rgb(SPICE))
    cur = T.mix(T.mul(T.mul(f, f), 0.5), cur, T.rgb(SPICE_DK))
    return cur, T.mixf(f, rough_s, 0.9), T.mixf(f, metal_s, 0.0)


# ------------------------------------------------------------------------------------------------
# The approved model, part by part (refinery_pilot_fable.build without its node / export code)


def approved(main, moving, anchors):
    base = []
    RF.drum(base, main, "dl", P["d1"])
    RF.drum(base, main, "dr", P["d2"])
    RF.stacks(base, main)
    RF.bridge(base, main)
    RF.pad(base, main)
    RF.extras(base, main)
    # the 80 skirt fins carry a 0.12 px modelled bevel (9 in 10 of their triangles) that the Reimagined bake's bevel
    # normal replaces anyway: plain bars, except the few near the dock (their corners are the dock check's nearest
    # part) and those at the footprint's edge (the plan bounds): these stay exactly as they are
    for o in base:
        if "_fin" in o.name and o.modifiers:
            c = sum((v.co for v in o.data.vertices), Vector()) / len(o.data.vertices)
            if RD.dist((c.x, c.y), RD.DOCK) > 36.0 and max(max(abs(v.co.x), abs(v.co.y)) for v in o.data.vertices) < RD.BOX - 1.0:
                o.modifiers.clear()
    moving.append((base, "base", (0.0, 0.0, 0.0), None))
    tops = RF.stack_tops()
    for i, (key, power) in enumerate(RF.FLUE_ANCHORS):
        x, y, z, _r = tops[key]
        anchors.append((f"fx_flue_{i}", (x, y, z), power))
    q = RF.inlet_anchor()
    anchors.append(("fx_inlet_0", (q.x, q.y, q.z), 1.0))


# ------------------------------------------------------------------------------------------------
# Shells for building_common.plate / stencil on the model's walls: t = px along the wall (the text runs towards
# falling t), ang = px down from z0


class Round:
    """A wall of revolution round c seen from the direction az (deg): radius r_of(z)."""
    D = 1.0

    def __init__(self, c, az, r_of, z0):
        self.c, self.az, self.r_of, self.z0 = c, az, r_of, z0

    def S(self, t, ang, lift=0.0):
        z = self.z0 - ang
        r = self.r_of(z)
        a = math.radians(self.az) - t / r
        return Vector((self.c[0] + (r + lift) * math.cos(a), self.c[1] + (r + lift) * math.sin(a), z))


def rim_r(z):
    """The roof rim's outer face (refinery_pilot_fable.drum: (r_rim, z_rim0 + 1) .. (r_rim - 0.5, z_roof - 0.6))."""
    z0, z1 = P["z_rim0"] + 1.0, P["z_roof"] - 0.6
    return P["r_rim"] - 0.5 * min(1.0, max(0.0, (z - z0) / (z1 - z0)))


def skirt_r(z):
    """The skirt's face (without the fins)."""
    z0, z1 = P["z_foot"] * 0.5, P["z_band0"]
    return P["r_skirt"] + 0.5 + (P["r_band"] - 0.2 - P["r_skirt"] - 0.5) * (z - z0) / (z1 - z0)


# ------------------------------------------------------------------------------------------------
# The drums


def roof_fan(out, moving, anchors, tag, c, az, node, vent):
    """An extractor on the roof cone: a low cowl with a guard, the rotor a node of its own."""
    p = polar(c, 10.9, az)
    z = RF.cone_z(c, p)
    out.append(K.cyl(f"{tag}_fan_base", p, 2.7, z - 0.4, z + 0.3, "rim", 16))
    out.append(K.lathe_loop(f"{tag}_fan_cowl", p, [(1.95, z + 0.25), (2.4, z + 0.25), (2.4, z + 1.5), (1.95, z + 1.5)], "steel", 16))
    out.append(K.cyl(f"{tag}_fan_well", p, 1.95, z + 0.25, z + 0.38, "duct_dk", 16))
    for k, a in enumerate((az + 45.0, az + 135.0)):
        out.append(beam(f"{tag}_fan_guard{k}", polar(p, 2.2, a, z + 1.5), polar(p, 2.2, a + 180.0, z + 1.5), 0.2, 0.16, "door_fr"))
    parts = BC.rotor(node, 5, 0.35, 1.8, 0.7, 1.15, 0.12, 24.0, "blade", hub=(0.42, -0.08, 0.12, 0.3), tips=0.45)
    origin = (p[0], p[1], z + 0.9)
    keep_out(parts, node, origin)
    moving.append((parts, node, origin, (0, 0, 1)))
    anchors.append((f"fx_vent_{vent}", (p[0], p[1], z + 1.9), 0.4))


def drum_ladder(out, tag, c, az, middle):
    """The maintenance ladder: from the ground up the skirt to the house band, (middle: up the arcade to the cornice
    - the west drum has that part already) and from the cornice over the rim to the roof, with grab rails and a railed
    landing on the roof's edge."""
    def run(nm, prof):
        pa = [polar(c, r, az - 2.4, z) for (r, z) in prof]
        pb = [polar(c, r, az + 2.4, z) for (r, z) in prof]
        BC.ladder(out, f"{tag}_{nm}", pa, pb, "rail")
    band = (P["r_band"] + 0.7, P["z_band1"])
    corn = (P["r_arch1"] + 0.9, P["z_corn"] + 0.2)
    top = (P["r_rim"] + 0.5, P["z_roof"] + 0.3)
    if middle:
        run("lad", [(P["r_skirt"] + 2.1, 0.15), band, corn, top])
    else:
        run("lad_low", [(P["r_skirt"] + 2.1, 0.15), band])
        run("lad_top", [corn, top])
    for s in (-1, 1):                                    # grab rails over the roof's edge
        a = az + s * 2.4
        pts = [polar(c, top[0], a, top[1]), polar(c, top[0], a, top[1] + 1.8), polar(c, P["r_roof"] - 0.3, a, top[1] + 1.8),
               polar(c, P["r_roof"] - 0.3, a, P["z_roof"] + 0.05)]
        for k, (p0, p1) in enumerate(zip(pts[:-1], pts[1:])):
            out.append(beam(f"{tag}_lad_grab{s}_{k}", p0, p1, 0.24, 0.24, "copper_dk"))
        BC.railing(out, f"{tag}_landing{s}", [polar(c, P["r_roof"] - 0.3, az + s * d, P["z_roof"] + 0.05) for d in (5.5, 14.0, 22.5)], 2.0)


def rim_number(out, tag, c, text):
    """The unit number stencilled on the roof rim's south face."""
    w, h = 1.35, 2.2
    BC.stencil(out, f"{tag}_num", Round(c, -90.0, rim_r, P["z_rim0"] + 1.35), text, w * (1.45 * (len(text) - 1) + 1.0) / 2, 0.0, w, h, 0.16, "stencil_lt")


def west_drum(main, moving, anchors):
    out = []
    c = P["d1"]
    # the door at the top of the white steps: a small porch in the arcade, hand rails down the steps
    a = P["stairs_ang"]
    top_z = 10.4                                          # the top step (refinery_pilot_fable.extras)
    out.append(obox("dl_porch", polar(c, 17.9, a), a, 1.3, 2.25, top_z, 16.7, "wall", 0.0, 0.97))
    out.append(obox("dl_porch_cap", polar(c, 18.0, a), a, 1.55, 2.5, 16.7, 17.15, "rib"))
    lamp = BC.door(out, "dl_door", polar(c, 19.2, a), a, top_z, 2.5, 4.9)
    drop(out, ("dl_door_step", "dl_door_hz"))             # (the door's own ground step: here it stands on the white steps)
    out.append(obox("dl_door_sill", polar(c, 19.55, a), a, 0.35, 1.7, top_z, top_z + 0.22, "door_fr"))
    anchors.append(("fx_light_1", lamp, 0.6))
    rf = P["r_foot"] - 24.3
    tx, ty = BC.unit(a + 90.0)
    for s in (-1, 1):
        def q(r, z, s=s):
            p = polar(c, r, a)
            return (p[0] + tx * s * 5.2, p[1] + ty * s * 5.2, z)
        feet = [q(rf + 26.5, 4.4), q(rf + 25.2, 7.4), q(rf + 24.3, 10.4)]          # one post on each of the three steps
        for k, f in enumerate(feet):
            out.append(beam(f"dl_steprail{s}_post{k}", f, (f[0], f[1], f[2] + 2.0), 0.24, 0.24, "rail"))
        for k, (f0, f1) in enumerate(zip(feet[:-1], feet[1:])):
            for nm, dz, w in (("top", 2.0, 0.3), ("mid", 1.05, 0.18)):
                out.append(beam(f"dl_steprail{s}_{nm}{k}", (f0[0], f0[1], f0[2] + dz), (f1[0], f1[1], f1[2] + dz), w, 0.24 if nm == "top" else w, "rail"))
    drum_ladder(out, "dl", c, -48.0, False)               # (the approved model's ladder: band to cornice, at -48 deg)
    out.extend(K.vent("dl_vent", polar(c, skirt_r(3.8) + 1.25, -75.0), 15.0, 1.5, 2.4, 5.2, 0.95, 3, "door_fr"))
    rim_number(out, "dl", c, "01")
    roof_fan(out, moving, anchors, "dl", c, 250.0, "fan_0", 0)
    main.extend(keep_out(out, "west drum"))


def north_drum(main, moving, anchors):
    out = []
    c = P["d2"]
    drum_ladder(out, "dr", c, -76.0, True)
    rim_number(out, "dr", c, "02")
    roof_fan(out, moving, anchors, "dr", c, 150.0, "fan_1", 1)
    # rungs and two bands on the thick flue (its south face)
    x, y, _z, _r = RF.stack_tops()["right"]
    for k in range(11):
        z = 32.5 + 1.5 * k
        r = 2.5 if z < 38.5 else 2.25
        out.append(beam(f"pr_rung{k}", (x - 0.65, y - r - 0.12, z), (x + 0.65, y - r - 0.12, z), 0.18, 0.18, "rail"))
    for z in (44.0, 57.0):
        out.append(RF.lathe(f"pr_band{int(z)}", (x, y), [(2.5, z), (2.5, z + 0.6)], "copper_dk", 16))
    main.extend(keep_out(out, "north drum"))


# ------------------------------------------------------------------------------------------------
# The bridge between the drums: its south-east wall faces the yard and the camera


def weather_mast(out, moving, anchors, base, zt, e):
    """A weather mast standing on base (x, y, z): arm along e (unit x, y), beacon, whip antenna; the vane and the cup
    anemometer are nodes of their own."""
    mx, my, z0 = base
    out.append(K.cyl("mast_foot", (mx, my), 0.7, z0 - 0.2, z0 + 0.5, "door_fr", 8))
    out.append(tube("mast", (mx, my, z0), (mx, my, zt), 0.3, "rail", 8, 0.2))
    arm0, arm1 = (mx - e[0] * 2.3, my - e[1] * 2.3, zt - 2.4), (mx + e[0] * 2.3, my + e[1] * 2.3, zt - 2.4)
    out.append(beam("mast_arm", arm0, arm1, 0.22, 0.22, "rail"))
    out.append(beam("mast_brace", (mx, my, zt - 4.2), (mx + e[0] * 1.5, my + e[1] * 1.5, zt - 2.5), 0.16, 0.16, "rail"))
    out.append(tube("mast_whip", (mx - e[0] * 1.2, my - e[1] * 1.2, zt - 2.4), (mx - e[0] * 1.2, my - e[1] * 1.2, zt + 4.4), 0.1, "steel_dk", 5, 0.05))
    bp = (arm0[0], arm0[1], zt - 2.05)
    out.append(K.cyl("beacon_s", bp[:2], 0.36, zt - 2.3, zt - 2.05, "steel_dk", 8))
    out.append(K.ball("beacon", (bp[0], bp[1], bp[2] + 0.2), 0.42, "red", 8))
    anchors.append(("fx_beacon_0", (bp[0], bp[1], bp[2] + 0.2), 0.5))
    vparts = [beam("vane_bar", (-1.7, 0.0, 0.0), (1.5, 0.0, 0.0), 0.16, 0.16, "rail"),
              obox("vane_fin", (-1.45, 0.0), 0.0, 0.62, 0.06, -0.55, 0.55, "red_p"),
              K.ball("vane_nose", (1.6, 0.0, 0.0), 0.26, "red_p", 6),
              tube("vane_hub", (0.0, 0.0, -0.45), (0.0, 0.0, 0.2), 0.22, "door_fr", 6)]
    moving.append((vparts, "vane", (mx, my, zt + 0.45), (0, 0, 1)))
    aparts = [tube("anem_hub", (0.0, 0.0, -0.3), (0.0, 0.0, 0.2), 0.2, "door_fr", 6)]
    for k in range(3):
        a = 120.0 * k
        aparts.append(beam(f"anem_arm{k}", (0.0, 0.0, 0.05), polar((0.0, 0.0), 1.0, a, 0.05), 0.1, 0.1, "rail"))
        aparts.append(K.ball(f"anem_cup{k}", polar((0.0, 0.0), 1.05, a, 0.05), 0.34, "steel_dk", 6))
    moving.append((aparts, "anemometer", (arm1[0], arm1[1], zt - 1.95), (0, 0, 1)))


def bridge(main, moving, anchors):
    out = []
    c, d1, d2 = P["bridge_c"], P["d1"], P["d2"]
    ang = math.degrees(math.atan2(d2[1] - d1[1], d2[0] - d1[0]))
    ax, ay = BC.unit(ang)
    nx, ny = -ay, ax                                      # towards the north-west (the dome's side)
    hl, se, h = P["bridge_half"], P["bridge_se"], P["bridge_h"]
    out_ang = ang - 90.0

    def bp(e, d, z=None):                                 # e along the wall, d out of its south-east face
        q = (c[0] + ax * e - nx * (se + d), c[1] + ay * e - ny * (se + d))
        return q if z is None else (q[0], q[1], z)
    # the door (the approved pipe run crosses the wall at 6 px: the lamp sits above it)
    BC.door(out, "br_door", bp(0.2, 0.0), out_ang, 0.0, 2.6, 4.9, lamp=False)
    out.append(obox("br_lamp_arm", bp(0.2, 0.45), ang, 0.2, 0.45, 7.75, 8.0, "door_fr"))
    out.append(obox("br_lamp_hood", bp(0.2, 1.1), ang, 0.75, 0.6, 7.95, 8.2, "door_fr"))
    lamp = bp(0.2, 1.15, 7.5)
    out.append(K.ball("br_lamp", lamp, 0.42, "lamp", 8))
    anchors.append(("fx_light_0", lamp, 0.6))
    # the control panel with status lights, a warning plate
    pe = 2.55
    out.append(obox("br_panel", bp(pe, 0.3), ang, 0.8, 0.35, 2.4, 4.8, "steel_dk"))
    out.append(obox("br_panel_face", bp(pe, 0.68), ang, 0.65, 0.05, 2.6, 4.6, "door_fr"))
    for k, (dl, z, mname) in enumerate(((-0.35, 4.2, "green"), (0.0, 4.2, "green"), (0.35, 4.2, "red"), (-0.2, 3.25, "glass"), (0.25, 3.25, "glass"))):
        small = z > 4
        out.append(obox(f"br_panel_led{k}", bp(pe + dl, 0.75), ang, 0.12 if small else 0.2, 0.04, z - (0.12 if small else 0.3),
                        z + (0.12 if small else 0.3), mname))
    BC.warning_plate(out, "br_warn", bp(-2.35, 0.0), out_ang, 2.9, 1.3)
    # louvres in the three wall vents, a row of lit windows under the roof
    for k in (-1, 0, 1):
        out.extend(K.vent(f"br_louvre{k}", bp(2.6 * k, 0.12), ang, 0.72, 12.3, 17.2, 0.3, 4, "door_fr"))
        out.append(obox(f"br_win_fr{k}", bp(2.6 * k, 0.02), ang, 0.95, 0.12, 20.6, 23.4, "door_fr"))
        out.append(obox(f"br_win{k}", bp(2.6 * k, 0.1), ang, 0.7, 0.08, 20.95, 23.05, "win_lit"))
    # the roof: a railing along the free south-east edge with short returns, the weather mast
    rc = (c[0] - ax * 1.5 - nx * 1.5, c[1] - ay * 1.5 - ny * 1.5)   # the house roof's centre (refinery_pilot_fable.bridge)

    def rp(e, n, z=h + 0.45):
        return (rc[0] + ax * e + nx * n, rc[1] + ay * e + ny * n, z)
    el = hl - 1.25
    BC.railing(out, "br_rail", [rp(-el, -5.2), rp(-el, -7.8), rp(el, -7.8), rp(el, -5.2)], 2.1)
    weather_mast(out, moving, anchors, rp(hl - 2.2, 3.5, h + 1.2), h + 12.2, (ax, ay))
    main.extend(keep_out(out, "bridge"))


# ------------------------------------------------------------------------------------------------
# The yard between the drums, the tall flue and its tank, the cooler in the north-east corner, the pad's studs


def yard(main, moving, anchors):
    out = []
    d1, d2 = P["d1"], P["d2"]
    zp = 3.2
    p0 = polar(d2, skirt_r(zp) + 0.2, -92.0, zp)
    p2 = polar(d1, skirt_r(zp) + 0.2, 20.0, zp)
    elbow = (-4.4, 1.0)
    BC.flanged_pipe(out, "yard_pipe", [p0, (*elbow, zp), p2], 0.9, "orange")
    out.append(K.cyl("yard_valve", elbow, 1.45, 1.8, 4.7, "steel", 10))
    BC.handwheel(out, "yard_valve_w", elbow, 5.7, 1.2)
    for k, (a, b) in enumerate(((p0, elbow), (elbow, p2))):
        m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        out.append(obox(f"yard_support{k}", m, 0.0, 0.5, 0.5, 0.0, zp - 0.6, "steel_dk"))
    for k, (p, m) in enumerate((((-0.9, -4.4), "rust"), ((0.8, -2.7), "barrel"))):
        out.append(K.cyl(f"yard_barrel{k}", p, 1.0, 0.0, 2.7, m, 10))
        out.append(K.cyl(f"yard_barrel{k}_lid", p, 0.8, 2.7, 2.78, "steel_dk", 10))
        for z in (0.9, 1.8):
            out.append(K.cyl(f"yard_barrel{k}_hoop{int(z * 10)}", p, 1.06, z, z + 0.16, "steel_dk", 10))
    main.extend(keep_out(out, "yard"))


def tank(main, moving, anchors):
    out = []
    tk, th = P["tank"], P["tank_h"]
    tops = RF.stack_tops()
    # rungs up the tall copper flue (its south face), the red lamp on its rim
    x, y, z1, _r = tops["tall_copper"]
    for k in range(10):
        z = 15.2 + 1.5 * k
        out.append(beam(f"pt_rung{k}", (x - 0.6, y - 2.07, z), (x + 0.6, y - 2.07, z), 0.18, 0.18, "rail"))
    bq = (x, y - 2.2, z1 - 0.45)
    out.append(beam("pt_beacon_arm", (x, y - 1.7, z1 - 1.0), (x, y - 2.2, z1 - 1.0), 0.3, 0.25, "steel_dk"))
    out.append(K.cyl("pt_beacon_s", bq[:2], 0.36, z1 - 1.1, z1 - 0.7, "steel_dk", 8))
    out.append(K.ball("pt_beacon", bq, 0.42, "red", 8))
    anchors.append(("fx_beacon_1", bq, 0.4))
    # the tank: a ladder up its east side, a gauge on top, a flanged pipe with a valve to the cooler
    la = 48.0
    BC.ladder(out, "tank_ladder", [polar(tk, 5.8, la - 6.5, 0.15), polar(tk, 5.8, la - 6.5, th + 1.7)],
              [polar(tk, 5.8, la + 6.5, 0.15), polar(tk, 5.8, la + 6.5, th + 1.7)])
    g = polar(tk, 3.5, 62.0)
    out.append(K.cyl("tank_gauge", g, 0.6, th + 0.3, th + 1.5, "door_fr", 8))
    out.append(K.cyl("tank_gauge_glass", g, 0.45, th + 1.5, th + 1.58, "glass", 8))
    pa, pb, pc = polar(tk, 5.0, 20.0, 3.0), (31.2, 35.0, 3.0), (34.7, 41.0, 3.0)
    BC.flanged_pipe(out, "tank_pipe", [pa, pb, pc], 0.9, "copper")
    v = (pa[0] + (pb[0] - pa[0]) * 0.55, pa[1] + (pb[1] - pa[1]) * 0.55)
    out.append(K.cyl("tank_valve", v, 1.35, 1.7, 4.4, "steel", 10))
    BC.handwheel(out, "tank_valve_w", v, 5.4, 1.15)
    out.append(obox("tank_pipe_support", ((pb[0] + pc[0]) / 2, (pb[1] + pc[1]) / 2), 0.0, 0.5, 0.5, 0.0, 2.4, "steel_dk"))
    main.extend(keep_out(out, "tank"))


def cooler(main, moving, anchors):
    """The free north-east corner of the solid cell (2, 0): a low cooler with a big fan, a control cabinet, a lamp."""
    out = []
    x0, x1, y0, y1 = 33.6, 46.8, 33.4, 46.8
    out.append(K.box("skid", x0, x1, y0, y1, 0.0, 0.9, "concrete", 0.2))
    n = 8
    for k in range(n):                                    # hazard edge along the front
        xa = x0 + 0.3 + (x1 - x0 - 0.6) * k / n
        xb = x0 + 0.3 + (x1 - x0 - 0.6) * (k + 1) / n
        out.append(K.box(f"skid_hz{k}", xa, xb, y0 - 0.1, y0 + 0.05, 0.3, 0.85, "hz_y" if k % 2 else "hz_k"))
    # the cooler: a steel box with louvres in front, the fan under a guard on top
    out.append(K.box("cooler", 34.7, 41.3, 38.4, 44.6, 0.9, 5.3, "steel", 0.15))
    out.extend(K.vent("cooler_v", (38.0, 38.4), 0.0, 2.3, 2.0, 4.4, 0.3, 4, "door_fr"))
    fc, zf = (38.0, 41.5), 5.3
    out.append(K.cyl("cooler_well", fc, 2.5, zf - 0.05, zf + 0.05, "duct_dk", 16))
    out.append(K.lathe_loop("cooler_ring", fc, [(2.5, zf), (2.9, zf), (2.9, zf + 0.8), (2.5, zf + 0.8)], "door_fr", 16))
    for k, a in enumerate((45.0, 135.0)):
        out.append(beam(f"cooler_guard{k}", polar(fc, 2.7, a, zf + 0.78), polar(fc, 2.7, a + 180.0, zf + 0.78), 0.22, 0.16, "door_fr"))
    parts = BC.rotor("fan_2", 5, 0.4, 2.3, 0.8, 1.4, 0.13, 24.0, "blade", hub=(0.5, -0.08, 0.14, 0.34), tips=0.55)
    origin = (fc[0], fc[1], zf + 0.32)
    keep_out(parts, "fan_2", origin)
    moving.append((parts, "fan_2", origin, (0, 0, 1)))
    anchors.append(("fx_vent_2", (fc[0], fc[1], zf + 1.2), 0.5))
    # the control cabinet: a door with a lamp, a cable to the cooler
    out.append(K.box("cabinet", 42.5, 46.3, 39.8, 44.8, 0.9, 7.4, "block", 0.15, 0.97))
    out.append(K.box("cabinet_cap", 42.3, 46.5, 39.6, 45.0, 7.4, 7.8, "rib"))
    lamp = BC.door(out, "cab_door", (44.4, 39.8), -90.0, 0.9, 2.4, 4.8)
    anchors.append(("fx_light_2", lamp, 0.6))
    out.extend(K.pipe("cab_cable", [(42.5, 42.0, 2.0), (41.9, 42.0, 2.0), (41.9, 42.0, 3.4), (41.3, 42.0, 3.4)], 0.3, "rubber", 6))
    # a lamp post at the front corner, two drums
    lp = (35.2, 35.0, 8.4)
    out.append(K.cyl("post_foot", lp[:2], 0.55, 0.9, 1.6, "steel_dk", 8))
    out.append(tube("post", (lp[0], lp[1], 0.9), (lp[0], lp[1], lp[2] - 0.4), 0.24, "rail", 6))
    out.append(K.cyl("post_cap", lp[:2], 0.42, lp[2] + 0.72, lp[2] + 0.95, "steel_dk", 8, 0.2))
    out.extend(K.lamp("post_lamp", lp, 0.6))
    anchors.append(("fx_light_3", (lp[0], lp[1], lp[2] + 0.25), 0.6))
    for k, (p, m) in enumerate((((45.3, 35.6), "rust"), ((43.2, 35.2), "barrel"))):
        out.append(K.cyl(f"skid_barrel{k}", p, 1.0, 0.9, 3.6, m, 10))
        out.append(K.cyl(f"skid_barrel{k}_lid", p, 0.8, 3.6, 3.68, "steel_dk", 10))
    main.extend(keep_out(out, "cooler"))


def pad_studs(main, moving, anchors):
    """Eight small lit studs in the pad's floor, just inside the rims: their tops stay below the rims (the deck), so
    the harvester's ground and everything the dock check measures are as before."""
    out = []
    c = P["pad_c"]
    s = math.sqrt(0.5)
    for k, (dx, dy) in enumerate(((13.5, 0.0), (-13.5, 0.0), (0.0, 13.5), (0.0, -13.5), (14.3 * s, 14.3 * s), (-14.3 * s, -14.3 * s),
                                  (-12.4 * s, 12.4 * s), (12.4 * s, -12.4 * s))):
        p = (c[0] + dx, c[1] + dy)
        out.append(K.cyl(f"pad_stud{k}_s", p, 0.5, 0.9, 1.5, "door_fr", 6))        # (a pale socket: no dark dots on the pad)
        out.append(RF.lathe(f"pad_stud{k}", p, [(0.36, 1.45), (0.36, 1.66), (0.24, 1.86), (0.02, 1.94)], "lamp", 6))
    top = max(v.co.z for o in out for v in o.data.vertices)
    if top > P["pad_h"] - 0.02:
        raise SystemExit(f"pad studs: {top:.2f} px, above the rims ({P['pad_h']} px)")
    main.extend(out)


# ------------------------------------------------------------------------------------------------

FLUE_TOP = [RF.stack_tops()[k][:3] for (k, _p) in RF.FLUE_ANCHORS]      # soot round all five
REVIEW = dict(dist=9.5, target=(0.0, 0.3, 0.5),
              closeups=[("closeup_drums", -70, 30, 5.2, 70, (-0.35, 0.1, 0.55)), ("closeup_pad", -50, 34, 5.0, 70, (0.75, 0.45, 0.35)),
                        ("closeup_game", -90, 45, 8.0, 60, (0.0, 0.3, 0.5))])
PREVIEW_VIEWS = ((-70, 30, 5.2, (-0.35, 0.1, 0.55)), (-50, 34, 5.0, (0.75, 0.45, 0.35)))

BC.setup(KEY, [("the approved model", approved), ("west drum", west_drum), ("north drum", north_drum), ("bridge", bridge),
               ("yard", yard), ("tank", tank), ("cooler", cooler), ("pad studs", pad_studs)],
         col=COL, spec=SPEC, kind=KIND, wear=WEAR, grime_w=GRIME_W, NODES=NODES, ANCHOR_DOC=ANCHOR_DOC, FLUE_TOP=FLUE_TOP, SHELL_MATS=(), SHELL_STREAKS=None,
         MAT_GRIME=MAT_GRIME, SURFACE_EXTRA=spice, REVIEW=REVIEW, PREVIEW_VIEWS=PREVIEW_VIEWS, PREVIEW_DIST=9.5)

# the owner (2026-11-19): "we need to add dirt for sure" - dirtier than the wind traps' level by default
BC.WEATHERING, BC.MOTTLE = 1.6, 1.8

if __name__ == "__main__":
    BC.main()
