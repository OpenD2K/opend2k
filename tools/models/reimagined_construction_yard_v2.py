"""Reimagined Atreides construction yard, redesign v2 - the wind trap recipe (docs/3d-models.md "Construction yard
redesign v2"; the refinery's twin, reimagined_refinery_v2.py): the approved model EXACTLY as it is, plus human-scale
detail, real materials and lived-in dirt.

Nothing of the approved model is re-formed. Its parts are built by its own generator (atreides_construction_yard.py:
the cream drum on its machine block, the round walled yard with the berm, ribs and copper claws, the white cabin, the
crane house with the post and the cream boom, the east wing with the two house-colour roof panels and the brown
conveyor, the dish, the gate with the house bars) and get the round-2 detail pass (reimagined_detail.py: the plates,
ledges, sidings, hatches, rails of the model in the pack) - the same 17,580 triangles at the same places. What is new:

  * human-scale parts, all clear of the crane (KEEP below), the yard's floor and the gate: a door with a lamp and
    a ladder on the wing's ledge, a railing round the ledge's deck, a ladder up the conveyor wall and the unit number
    on it, a weather mast (vane, anemometer, beacon) on the conveyor's end block, crates and barrels; a door with a
    lamp, a ladder, a roof extractor and an exhaust stack on the crane house; a railing along the yard's inner edge
    (the half away from the crane) and a ladder up the berm from the front step; an inspection ladder up the drum to a
    small railed landing on its crest; in the free north-east corner (behind the conveyor, beside the dish) a cooler
    with a big fan, a control cabinet with a door, a lamp post, a valve; a red lamp on the crane's pivot;
  * materials and dirt: building_common's surface pass at the refinery's final level (WEATHERING 1.6, MOTTLE 1.8),
    tuned for a brown building (less dust film, darker earth-brown stains: WEAR / GRIME_W / STAIN below), the cream
    drum / crest / dish as the kind "cream", the white boom and cabin 0.85 of it, light painted ladders and rails,
    soot at the exhaust, grit everywhere, oil and scorch marks on the yard's floor (SURFACE_EXTRA);
  * THE CRANE: nodes `crane`, `hook`, `load`, `glow` with the approved generator's parts, origins, rest pose and its
    12 poses (Y.POSES / Y.ANIM, written as node poses for the pack: OUT/variants/, building_common.write_poses -
    the main pack's variants do not fit a model with more nodes, the loader wants the node list name for name);
  * moving parts are nodes of their own (the engine's Building life spins them by name), effect points are empties.

    blender -b --factory-startup --python tools/models/reimagined_construction_yard_v2.py -- --out DIR [--work DIR]
        --lod 0.36 [--no-bake] [--bake-size 2048] [--weathering 1.6] [--mottle 1.8] [--preview DIR]
        [--turntable DIR --samples 24] [--tris]
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined ATREIDES_CONSTRUCTION_YARD
             (the glb, its simplified version, the stats and the pose variants)

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the art's centre, z up).
Nothing is copied from the original art.
"""
import math
import os
import sys

from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import building_common as BC  # noqa: E402  (helpers, the surface / weathering pass, build, run)
import reimagined_detail as RD  # noqa: E402  (round 2's detail pass on the approved parts)
import atreides_construction_yard as Y  # noqa: E402  (the approved model: parts, crane poses)

K, PX = BC.K, BC.PX
beam, tube, obox, polar = BC.beam, BC.tube, BC.obox, BC.polar
P = Y.P

KEY = "ATREIDES_CONSTRUCTION_YARD"

NODES = dict(
    main="everything that does not move",
    crane="the boom on its post (origin = the pivot): turned by the poses",
    hook="cable and hook (origin = the boom tip at rest): moved / stretched by the poses",
    load="the load on the hook (hidden at rest)",
    glow="the assembly glow in the yard (hidden at rest)",
    fan_0="the cooler's fan in the north-east corner (5 px across: the one seen at the default zoom)",
    fan_1="the extractor on the crane house's roof",
    vane="the wind vane on the weather mast (the conveyor's end block)",
    anemometer="the cup anemometer on the mast's arm",
)
ANCHOR_DOC = dict(
    fx_light_0="the lamps at the gate, unchanged", fx_light_1="the lamp on the wing's south corner, unchanged",
    fx_light_2="the lamp over the door in the wing's ledge", fx_light_3="over the crane house's door",
    fx_light_4="over the control cabinet's door", fx_light_5="the lamp post by the cooler",
    fx_vent_0="the cooler's outlet: a warm puff", fx_vent_1="the crane house's extractor",
    fx_smoke_0="the crane engine's exhaust stack", fx_beacon_0="the red beacon on the weather mast",
    fx_beacon_1="the red lamp on the crane's pivot",
)


EXHAUST = [0.0, 0.0, 0.0]            # the crane engine's stack top (set by crane_house(): soot round it)


class Frame:
    """A block's own axes: t along ang (deg), s to its left."""

    def __init__(self, c, ang):
        self.c, self.ang = c, ang
        self.u, self.n = BC.unit(ang), BC.unit(ang + 90.0)

    def at(self, t, s, z=None):
        p = (self.c[0] + self.u[0] * t + self.n[0] * s, self.c[1] + self.u[1] * t + self.n[1] * s)
        return p if z is None else (p[0], p[1], z)


WING = Frame(P["wing_c"], P["wing_ang"])                       # t towards the south-east, s towards the north-east
CH = Frame((P["crane_pivot"][0] + 1.0, P["crane_pivot"][1] - 9.0), -50.0)      # the crane house
WA = P["wing_ang"]

# ------------------------------------------------------------------------------------------------
# The crane: what it sweeps stays free. The boom turns about the post from its rest direction (-55.5 deg) clockwise
# to -117.5 deg; its underside is 24.3 px high at the post and 16.9 px at the tip (29.1 px out), the hook and the
# load travel under the tip (down to the ground at the western end, into the yard at rest). Everything this file ADDS
# stays below the boom with air and out of the ring the load travels in, and inside the approved model's plan bounds
# (the placement on the cells is computed from them: models.cpp fitToFootprint).
SWEEP = (-128.0, -48.0)             # deg about the pivot (the head and the load are 7 deg wide)
BOUNDS = [0.0, 0.0, 0.0, 0.0]       # x0, x1, y0, y1 of the approved model (set by approved())


def keep_out(objs, label, origin=(0.0, 0.0, 0.0)):
    """Raise if a vertex of the new parts `objs` (px; origin = a moving node's place) is in the crane's way or
    outside the approved model's plan."""
    cp = P["crane_pivot"]
    bad = []
    for o in objs:
        for v in o.data.vertices:
            x, y, z = v.co.x + origin[0], v.co.y + origin[1], v.co.z + origin[2]
            if not (BOUNDS[0] <= x <= BOUNDS[1] and BOUNDS[2] <= y <= BOUNDS[3]):
                bad.append((o.name, "outside the plan", round(x, 1), round(y, 1), round(z, 1)))
            r = math.hypot(x - cp[0], y - cp[1])
            a = math.degrees(math.atan2(y - cp[1], x - cp[0]))
            if r < 34.0 and SWEEP[0] < a < (SWEEP[1] if z < 10.0 else SWEEP[1] - 3.0):    # (above 10 px only the head: 4.5 deg)
                top = 24.3 - 7.4 * min(r, 29.1) / 29.1 - 1.5
                if r > 22.0:
                    top = min(top, 4.0)
                if z > top:
                    bad.append((o.name, "in the crane's way", round(x, 1), round(y, 1), round(z, 1)))
    if bad:
        raise SystemExit(f"{label}: {len(bad)} vertices misplaced, e.g. {bad[:4]}")
    return objs


class Probe:
    """The approved model's surfaces (its parts' base meshes, px): where a wall or a roof really is, with the detail
    pass's plates, sidings and trims on it."""

    def __init__(self, objs):
        self.bvh, _ = RD._bvh([o for o in objs if o.type == "MESH"])

    def wall(self, c, out_ang, z, reach=5.0):
        """How far (px) the surface stands out of the point c (x, y) along out_ang at height z; None = nothing."""
        ox, oy = BC.unit(out_ang)
        hit = self.bvh.ray_cast(Vector((c[0] + ox * reach, c[1] + oy * reach, z)), Vector((-ox, -oy, 0.0)), reach + 3.0)
        return None if hit[0] is None else reach - hit[3]

    def snap(self, c, out_ang, zs, half=0.0):
        """The furthest the wall stands out of c over the heights zs and +-half px along it (plates, frames)."""
        ax, ay = BC.unit(out_ang + 90.0)
        ds = [self.wall((c[0] + ax * a, c[1] + ay * a), out_ang, z) for z in zs for a in ((-half, 0.0, half) if half else (0.0,))]
        ds = [d for d in ds if d is not None and d < 2.5]
        return max(ds) if ds else 0.0

    def top(self, x, y, z=75.0):
        hit = self.bvh.ray_cast(Vector((x, y, z)), Vector((0.0, 0.0, -1.0)), z + 1.0)
        return 0.0 if hit[0] is None else hit[0].z

    def flat(self, c, r, label):
        """The roof under a disc (c, r): its height; the build stops if something stands there."""
        zs = [self.top(c[0] + r * math.cos(a), c[1] + r * math.sin(a)) for a in [i * math.pi / 6 for i in range(12)]] + [self.top(*c)]
        if max(zs) - min(zs) > 0.35:
            raise SystemExit(f"{label}: the roof at ({c[0]:.1f}, {c[1]:.1f}) r {r} is not free ({min(zs):.2f}..{max(zs):.2f} px)")
        return max(zs)


    def spot(self, cands, r, label):
        """The first of the candidate points whose roof is free under a disc of r: (point, height)."""
        for c in cands:
            zs = [self.top(c[0] + r * math.cos(a), c[1] + r * math.sin(a)) for a in [i * math.pi / 6 for i in range(12)]] + [self.top(*c)]
            if max(zs) - min(zs) <= 0.2:
                return c, max(zs)
        raise SystemExit(f"{label}: no free roof among the candidates")


PROBE = None


# ------------------------------------------------------------------------------------------------
# Palette: the approved model's own (atreides_kit / refinery_pilot_fable) + the fittings (building_common)

COL = dict(
    stencil_lt=(0.860, 0.810, 0.690),   # the unit number on the brown conveyor: pale stencil paint
    barrel=(0.300, 0.340, 0.360),       # a blue-grey drum
    crate=(0.560, 0.470, 0.330),        # crates: sand-coloured board
    crate_dk=(0.330, 0.270, 0.190),     # their battens
    grating=(0.600, 0.580, 0.540),      # the landing on the drum: light steel grating
    # Ladders and railings: LIGHT, and painted rather than bare metal - the owner disliked the refinery's black
    # ladders, and the fittings' metallic rail steel (0.7 metallic) still reads near-black against a cream drum (a
    # metal shows the sky and the ground, not its colour: first build of this model). The round-2 detail pass's
    # railings (rd_rail, near-black 0.30, 0.29, 0.275) take the same paint.
    rail=(0.760, 0.730, 0.650),
    rd_rail=(0.760, 0.730, 0.650),
)
SPEC = dict(stencil_lt=(0.0, 0.55, "paint"), barrel=(0.0, 0.5, "paint"), crate=(0.0, 0.75, "paint"), crate_dk=(0.0, 0.8, "paint"),
            grating=(0.3, 0.55, "paint"), rail=(0.1, 0.5, "rail"), rd_rail=(0.1, 0.5, "rail"))
# The dirt (the owner: "we need to add dirt for sure"): the refinery's final level. The cream drum, the yard's crest
# and the dish are the kind "cream" - nearly the paint's full grime and dust (the model before had brown, blotchy
# cream); the white boom and cabin take 0.85 of the weathering; the cabin's window stays glass.
# Against the refinery the yard is mostly BROWN (the conveyor, the crane house, the yard's floor) and stone: the dust
# film and the dust on top, which suit cream roofs, turned the first build pale and grey beside the model before (deep
# browns, dark blotches). So here every kind takes less film / top dust / blown sand and keeps its colour, and the
# dirt is the dark kind: stains, run-off, grime at the foot, in a browner tone (STAIN / RUNOFF / LOW below).
# Thin rails are their own kind: paint that takes no cavity shade (a ladder close to a wall would bake black).
KIND = dict(roof="cream", roof2="cream", glow_dk="glass")
#            mottle film  soft  cavity wear chips grime streak dust  sand
WEAR = dict(cream=(0.120, 0.26, 0.40, 0.65, 0.40, 0.26, 0.40, 0.34, 0.20, 0.45),
            paint=(0.120, 0.24, 0.36, 0.60, 0.35, 0.20, 0.30, 0.26, 0.20, 0.45),
            steel=(0.100, 0.05, 0.35, 0.60, 0.50, 0.00, 0.28, 0.14, 0.05, 0.30),
            rail=(0.030, 0.10, 0.06, 0.08, 0.20, 0.10, 0.10, 0.00, 0.10, 0.30))
#               blotch run-off low  soot  rust  recess sand
GRIME_W = dict(cream=(0.90, 0.65, 0.60, 0.85, 0.75, 0.30), paint=(0.85, 0.55, 0.55, 0.80, 0.75, 0.25),
               steel=(0.75, 0.32, 0.45, 0.80, 0.00, 0.10), rail=(0.20, 0.00, 0.25, 0.50, 0.00, 0.00))
MAT_GRIME = dict(white=0.85)
STAIN = (0.220, 0.165, 0.100)        # the large patches: a dark earth brown, as on the model before
GRIT = (0.250, 0.185, 0.115)         # fine brown specks over everything (the model before's fine-grained mottle)
RUNOFF = (0.380, 0.290, 0.190)
LOW = (0.300, 0.235, 0.160)
OIL = (0.110, 0.095, 0.080)
SCORCH = (0.075, 0.068, 0.062)


def yard_stains(T, name, kind, ctx, cur, rough_s, metal_s, W):
    """SURFACE_EXTRA: the yard's own marks. Fine brown grit over every surface; oil and grease in patches on the yard's floor and round its hub, a scorched
    arc along the inner south-east wall where the assembly glow burns, and dark drips down the inner wall."""
    if kind in ("glass", "rail") or name.startswith("HOUSE"):
        return cur, rough_s, metal_s
    nodes, links = T.nodes, T.links
    ci, ri = P["pit_in"]
    grit = T.mul(T.remap(T.noise(38.0, 3.0, offset=71.0), 0.50, 0.64, 0.0, 1.0), T.remap(T.noise(6.0, 3.0, offset=73.0), 0.30, 0.70, 0.30, 1.0))
    cur = T.mix(T.mul(grit, min(0.55, 0.30 * W * BC.MAT_GRIME.get(name, 1.0))), cur, T.rgb(GRIT))
    mp = nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 1.0, 0.0)
    links.new(T.geo.outputs["Position"], mp.inputs["Vector"])
    d = nodes.new("ShaderNodeVectorMath")
    d.operation = "DISTANCE"
    links.new(mp.outputs[0], d.inputs[0])
    d.inputs[1].default_value = (ci[0] * PX, ci[1] * PX, 0.0)
    r = d.outputs["Value"]
    inside = T.remap(r, (ri + 0.3) * PX, (ri - 0.6) * PX, 0.0, 1.0)
    z = ctx["pos"].outputs["Z"]
    floor = T.mul(inside, T.remap(z, 4.0 * PX, 2.6 * PX, 0.0, 1.0))
    oil = T.mul(T.remap(T.noise(7.5, 4.0, offset=61.0), 0.42, 0.60, 0.0, 1.0), T.remap(T.noise(23.0, 3.0, offset=63.0), 0.30, 0.70, 0.5, 1.0))
    oil = T.op("MAXIMUM", oil, T.mul(T.remap(r, 7.5 * PX, 3.5 * PX, 0.0, 1.0), 0.7))           # round the hub
    f_oil = T.mul(T.mul(floor, oil), min(0.75, 0.42 * W))
    # the scorched arc: towards the south-east wall, on the floor's outer ring and the wall's foot
    q = nodes.new("ShaderNodeVectorMath")
    q.operation = "DISTANCE"
    links.new(mp.outputs[0], q.inputs[0])
    g = polar(ci, ri - 1.5, -38.0)
    q.inputs[1].default_value = (g[0] * PX, g[1] * PX, 0.0)
    arc = T.mul(T.remap(q.outputs["Value"], 13.0 * PX, 3.0 * PX, 0.0, 1.0), T.remap(r, (ri - 6.5) * PX, (ri - 2.0) * PX, 0.0, 1.0))
    arc = T.mul(T.mul(arc, inside), T.remap(z, 7.5 * PX, 3.0 * PX, 0.0, 1.0))
    f_sc = T.mul(T.mul(arc, T.remap(T.noise(12.0, 3.0, offset=67.0), 0.25, 0.70, 0.45, 1.0)), min(0.7, 0.36 * W))
    # drips down the inner wall
    streak = T.remap(T.noise(30.0, 2.0, mapping=(1.0, 1.0, 0.05), offset=69.0), 0.45, 0.72, 0.0, 1.0)
    f_dr = T.mul(T.mul(T.mul(inside, ctx["upright"]), streak), min(0.6, 0.30 * W))
    cur = T.mix(f_oil, cur, T.rgb(OIL))
    cur = T.mix(f_sc, cur, T.rgb(SCORCH))
    cur = T.mix(f_dr, cur, T.rgb(BC.RUNOFF))
    f = T.op("MAXIMUM", f_oil, f_sc)
    return cur, T.mixf(f_oil, T.mixf(f_sc, rough_s, 0.92), 0.55), T.mixf(f, metal_s, 0.0)


# ------------------------------------------------------------------------------------------------
# The approved model, node for node (atreides_kit.assemble's nodes with reimagined_build's detail pass)


def approved(main, moving, anchors):
    global PROBE
    RD.tag_helpers(K)
    RD.install("building", 34000, skip_nodes=("glow", "load", "hook"))      # (reimagined_build.build / SKIP_NODES)
    built = Y.build(False)
    nodes = built["nodes"]
    for name in ("main", "crane"):
        added, tris = RD.detail(nodes[name], RD.STATE["profile"], RD.STATE["budget"] - RD.STATE["used"], name)
        RD.STATE["used"] += tris
        nodes[name] += added
    RD.uninstall()
    import bpy
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()         # (with the parts' bevels, as the joined nodes will have them)
    n = sum(len(p.vertices) - 2 for name in nodes for o in nodes[name] for p in o.evaluated_get(deps).data.polygons)
    if n != 17580:
        raise SystemExit(f"the approved model: {n} triangles, not the pack's 17,580 - its generator or the detail pass changed")
    PROBE = Probe(nodes["main"] + nodes["crane"])
    xs = [v.co.x for name in ("main", "crane") for o in nodes[name] for v in o.data.vertices]
    ys = [v.co.y for name in ("main", "crane") for o in nodes[name] for v in o.data.vertices]
    BOUNDS[:] = [min(xs), max(xs), min(ys), max(ys)]
    # the red lamp on the crane's pivot: on the axis, so it stays where it is while the crane turns (part of the crane)
    cp = P["crane_pivot"]
    zb = PROBE.top(cp[0], cp[1])
    nodes["crane"].append(K.cyl("pivot_beacon_s", cp[:2], 0.4, zb - 0.1, zb + 0.4, "steel_dk", 8))
    nodes["crane"].append(K.ball("pivot_beacon", (cp[0], cp[1], zb + 0.7), 0.45, "red", 8))
    anchors.append(("fx_beacon_1", (cp[0], cp[1], zb + 0.7), 0.4))
    main.extend(nodes["main"])
    for name in ("crane", "hook", "load", "glow"):       # the pose nodes: the generator's parts, origins (pivots)
        moving.append((nodes[name], name, built["origins"][name], "fixed"))
    anchors.extend(built["anchors"])


# ------------------------------------------------------------------------------------------------
# Fittings


def wall_door(out, name, c, out_ang, z0, material, w=2.6, h=4.9):
    """A service door (building_common.door) on a wall of the approved model: a plain surround over the wall's plates
    or siding, the door on it. c = (x, y) on the wall's nominal face. Returns the lamp's point."""
    d = PROBE.snap(c, out_ang, (z0 + 1.0, z0 + 2.6, z0 + 4.4, z0 + 5.5), w / 2 + 0.7) + 0.12
    ox, oy = BC.unit(out_ang)
    out.append(obox(f"{name}_surround", (c[0] + ox * (d - 0.9), c[1] + oy * (d - 0.9)), out_ang + 90.0, w / 2 + 0.95, 0.9, z0, z0 + h + 0.85, material))
    return BC.door(out, name, (c[0] + ox * d, c[1] + oy * d), out_ang, z0, w, h)


def wall_ladder(out, name, c, out_ang, z0, z1, grab_in=1.4, half=0.75):
    """A ladder up a wall of the approved model from z0 to just over its top edge z1, with grab rails bent over the
    edge onto the roof. c = (x, y) on the wall's nominal face."""
    d = PROBE.snap(c, out_ang, [z0 + (z1 - z0) * k / 6 for k in range(1, 6)], half) + 0.9
    ox, oy = BC.unit(out_ang)
    ax, ay = BC.unit(out_ang + 90.0)

    def q(s, dd, z):
        return (c[0] + ox * dd + ax * s * half, c[1] + oy * dd + ay * s * half, z)
    BC.ladder(out, name, [q(-1, d, z0), q(-1, d, z1 + 0.35)], [q(1, d, z0), q(1, d, z1 + 0.35)])
    for s in (-1, 1):
        pts = [q(s, d, z1 + 0.35), q(s, d, z1 + 2.0), q(s, -grab_in, z1 + 2.0), q(s, -grab_in, z1 + 0.05)]
        for k, (p0, p1) in enumerate(zip(pts[:-1], pts[1:])):
            out.append(beam(f"{name}_grab{s}_{k}", p0, p1, 0.24, 0.24, "rail"))


def roof_fan(out, moving, anchors, tag, p, node, vent, r=1.8):
    """An extractor on a flat roof: a low cowl with a guard, the rotor a node of its own."""
    z = PROBE.flat(p, r + 0.9, tag)
    out.append(K.cyl(f"{tag}_base", p, r + 0.9, z - 0.2, z + 0.3, "steel_dk", 16))
    out.append(K.lathe_loop(f"{tag}_cowl", p, [(r + 0.15, z + 0.25), (r + 0.6, z + 0.25), (r + 0.6, z + 1.5), (r + 0.15, z + 1.5)], "steel", 16))
    out.append(K.cyl(f"{tag}_well", p, r + 0.15, z + 0.25, z + 0.38, "duct_dk", 16))
    for k, a in enumerate((45.0, 135.0)):
        out.append(beam(f"{tag}_guard{k}", polar(p, r + 0.4, a, z + 1.5), polar(p, r + 0.4, a + 180.0, z + 1.5), 0.2, 0.16, "door_fr"))
    parts = BC.rotor(node, 5, 0.35, r, 0.7, 1.15, 0.12, 24.0, "blade", hub=(0.42, -0.08, 0.12, 0.3), tips=0.45)
    origin = (p[0], p[1], z + 0.9)
    keep_out(parts, node, origin)
    moving.append((parts, node, origin, (0, 0, 1)))
    anchors.append((f"fx_vent_{vent}", (p[0], p[1], z + 1.9), 0.4))


def barrel(out, name, p, material, z0=0.0):
    out.append(K.cyl(name, p, 1.0, z0, z0 + 2.7, material, 10))
    out.append(K.cyl(f"{name}_lid", p, 0.8, z0 + 2.7, z0 + 2.78, "steel_dk", 10))
    for z in (0.9, 1.8):
        out.append(K.cyl(f"{name}_hoop{int(z * 10)}", p, 1.06, z0 + z, z0 + z + 0.16, "steel_dk", 10))


def crate(out, name, c, ang, hl, hw, h, z0=0.0):
    out.append(obox(name, c, ang, hl, hw, z0, z0 + h, "crate", 0.08))
    for k, t in enumerate((-0.62, 0.62)):
        ux, uy = BC.unit(ang)
        out.append(obox(f"{name}_b{k}", (c[0] + ux * hl * t, c[1] + uy * hl * t), ang, 0.18, hw + 0.08, z0 + 0.05, z0 + h + 0.08, "crate_dk"))


class Flat:
    """A flat wall for building_common.stencil: the text starts at p0 (x, y) and runs along run_ang, read from the
    side out_ang; z0 = the text's foot."""
    D = 1.0

    def __init__(self, p0, run_ang, out_ang, z0):
        self.p0, self.run, self.out, self.z0 = p0, BC.unit(run_ang), BC.unit(out_ang), z0

    def S(self, t, ang, lift=0.0):
        return Vector((self.p0[0] - self.run[0] * t + self.out[0] * lift, self.p0[1] - self.run[1] * t + self.out[1] * lift, self.z0 - ang))


def weather_mast(out, moving, anchors, base, zt, e):
    """A weather mast standing on base (x, y, z): arm along e (unit x, y), beacon, whip antenna; the vane and the cup
    anemometer are nodes of their own (reimagined_refinery_v2's)."""
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


# ------------------------------------------------------------------------------------------------
# The east wing: its ledge (the low deck along the south-west and the south-east), the conveyor wall, the end block


def wing(main, moving, anchors):
    out = []
    sw = WA - 90.0                                        # the ledge's south-west face looks this way
    face = -(P["wing_hw"] + 2.0)                          # s of that face (atreides_construction_yard: wing_ledge)
    deck = PROBE.top(*WING.at(12.0, -13.9))
    # a door with a lamp in the ledge's south-west face, a ladder up to the deck beside it
    lamp = wall_door(out, "wing_door", WING.at(12.2, face), sw, 0.0, "stone_dk")
    anchors.append(("fx_light_2", lamp, 0.6))
    wall_ladder(out, "wing_ladder", WING.at(20.4, face), sw, 0.15, deck, grab_in=1.3)
    # the deck's railing: along the south-west edge (a gap at the ladder), round the south corner to the end block
    BC.railing(out, "wing_rail_a", [WING.at(9.6, face + 1.55, deck), WING.at(19.3, face + 1.55, deck)], 2.0)
    BC.railing(out, "wing_rail_b", [WING.at(21.5, face + 1.55, deck), WING.at(22.45, face + 1.55, deck), WING.at(22.45, -6.4, deck)], 2.0)
    # the conveyor's south-west wall above the roof: a short ladder up to the belt, the unit number
    cf = 9.5 - 4.6                                        # s of that wall
    zr = PROBE.top(*WING.at(5.0, cf - 1.0))
    zc = P["wing_z"] + 5.5
    wall_ladder(out, "conv_ladder", WING.at(5.0, cf), sw, zr + 0.1, zc, grab_in=1.2, half=0.65)
    lift = PROBE.snap(WING.at(10.5, cf), sw, (14.4, 15.0, 15.6), 1.6) + 0.1
    BC.stencil(out, "conv_num", Flat(WING.at(8.85, cf), WA, sw, 13.95), "01", 0.0, 0.0, 1.35, 2.2, lift, "stencil_lt")
    # the weather mast on the conveyor's end block (the south-east corner, far from the crane)
    m, zm = PROBE.spot([WING.at(t, s) for s in (10.5, 9.0, 7.5, 6.0, 4.5, 3.0) for t in (21.5, 20.0, 18.5, 17.0)], 0.8, "weather mast")
    weather_mast(out, moving, anchors, (m[0], m[1], zm), zm + 11.5, WING.u)
    # crates and a barrel in the forecourt at the wing's foot, two barrels and a crate at its south-east end
    crate(out, "crate0", WING.at(2.6, -10.6), WA, 1.4, 1.3, 2.5)
    crate(out, "crate1", WING.at(5.5, -10.3), WA + 12.0, 1.1, 0.9, 1.7)
    crate(out, "crate2", WING.at(2.7, -10.7), WA + 30.0, 0.9, 0.8, 1.4, 2.5)
    barrel(out, "barrel0", WING.at(-0.4, -10.2), "rust")
    barrel(out, "barrel1", WING.at(26.6, 6.2), "barrel")
    barrel(out, "barrel2", WING.at(26.3, 8.5), "rust")
    crate(out, "crate3", WING.at(26.6, 10.9), WA + 90.0, 1.1, 1.0, 2.2)
    main.extend(keep_out(out, "wing"))


# ------------------------------------------------------------------------------------------------
# The crane house (the boom turns over it: everything stays low)


def crane_house(main, moving, anchors):
    out = []
    lamp = wall_door(out, "ch_door", CH.at(-3.6, -6.0), -140.0, 0.0, "skirt")
    anchors.append(("fx_light_3", lamp, 0.6))
    zr = PROBE.top(*CH.at(6.0, -3.2))
    wall_ladder(out, "ch_ladder", CH.at(8.0, -3.2), -50.0, 0.15, zr, grab_in=1.3)
    fp, _ = PROBE.spot([CH.at(t, s) for t in (4.4, 3.4, 2.4, 5.2, -4.0, -5.0) for s in (2.2, 1.2, 0.0, -1.5, -2.8, 3.0)], 2.6, "extractor")
    roof_fan(out, moving, anchors, "ch_fan", fp, "fan_1", 1, r=1.6)
    # the crane engine's exhaust: a thin stack with a cap at the roof's west corner
    e, ze = PROBE.spot([CH.at(t, s) for t in (-5.6, -4.6, -3.6, 5.6) for s in (-3.6, -2.6, -1.6, 3.0)], 0.9, "exhaust")
    EXHAUST[:] = [e[0], e[1], ze + 3.6]
    out.append(K.cyl("ch_stack_foot", e, 0.85, ze - 0.1, ze + 0.5, "steel_dk", 8))
    out.append(tube("ch_stack", (e[0], e[1], ze), (e[0], e[1], ze + 3.4), 0.5, "copper_dk", 8))
    out.append(K.cyl("ch_stack_lip", e, 0.65, ze + 3.2, ze + 3.5, "steel_dk", 8))
    out.append(K.cyl("ch_stack_bore", e, 0.38, ze + 3.5, ze + 3.54, "burnt", 8))
    anchors.append(("fx_smoke_0", (e[0], e[1], ze + 3.6), 0.35))
    main.extend(keep_out(out, "crane house"))


# ------------------------------------------------------------------------------------------------
# The yard (the round walled pit): a railing along its inner edge on the half away from the crane, a ladder up the
# berm from the front step, barrels and a crate by the west box


def yard(main, moving, anchors):
    out = []
    (co, ro), (ct, rt), (ci, ri), ph = P["pit_out"], P["pit_top"], P["pit_in"], P["pit_h"]
    zc = ph + 0.5                                          # the crest
    BC.railing(out, "pit_rail", [polar(ci, ri + 1.0, a, zc) for a in range(-168, 31, 11)], 2.0, step=2.9)
    th = -20.0
    tx, ty = BC.unit(th + 90.0)
    prof = [(co, ro + 2.1, 0.15), (co, ro + 0.9, 2.75), (ct, rt + 1.6, ph - 0.05), (ct, rt + 0.9, zc + 0.75)]

    def side(s):
        return [(c[0] + r * math.cos(math.radians(th)) + tx * s * 0.75, c[1] + r * math.sin(math.radians(th)) + ty * s * 0.75, z) for (c, r, z) in prof]
    BC.ladder(out, "pit_ladder", side(-1), side(1))
    for s in (-1, 1):
        top = side(s)[-1]
        g = [top, (top[0], top[1], zc + 2.0), (*polar(ct, rt - 1.0, th), zc + 2.0), (*polar(ct, rt - 1.0, th), zc)]
        g = [g[0], g[1]] + [(p[0] + tx * s * 0.75, p[1] + ty * s * 0.75, p[2]) for p in g[2:]]
        for k, (p0, p1) in enumerate(zip(g[:-1], g[1:])):
            out.append(beam(f"pit_ladder_grab{s}_{k}", p0, p1, 0.24, 0.24, "rail"))
    barrel(out, "barrel3", (-45.2, 2.0), "barrel")
    barrel(out, "barrel4", (-43.5, -0.3), "rust")
    crate(out, "crate4", (-45.4, -3.2), 10.0, 1.2, 1.0, 2.0)
    main.extend(keep_out(out, "yard"))


# ------------------------------------------------------------------------------------------------
# The drum: an inspection ladder up its south-west flank from the machine block's top to a small railed landing


def drum(main, moving, anchors):
    out = []
    a, b, r = Vector(P["drum_a"]), Vector(P["drum_b"]), P["drum_r"]
    d = (b - a).normalized()
    side = d.cross(Vector((0.0, 0.0, 1.0))).normalized()        # south-west
    up = side.cross(d)
    ln = (b - a).length
    t0 = 0.835                                             # between the cradle and the last rib

    def at(t, psi, rr):
        return tuple(a + (b - a) * t + side * (rr * math.cos(math.radians(psi))) + up * (rr * math.sin(math.radians(psi))))
    psis = [-42.0, -22.0, -2.0, 18.0, 38.0, 58.0, 76.0]
    BC.ladder(out, "drum_ladder", [at(t0 - 0.75 / ln, p, r + 1.5) for p in psis], [at(t0 + 0.75 / ln, p, r + 1.5) for p in psis])
    for k, p in enumerate((-30.0, 28.0)):                  # stand-offs
        for s in (-1, 1):
            out.append(beam(f"drum_ladder_st{k}{s}", at(t0 + s * 0.75 / ln, p, r + 0.1), at(t0 + s * 0.75 / ln, p, r + 1.5), 0.2, 0.2, "rail"))
    # the landing on the crest: a grating with a railing on three sides (open towards the ladder)
    hl = 2.3 / ln
    bm_pts = [at(t0 - hl, 77.0, r + 0.9), at(t0 + hl, 77.0, r + 0.9), at(t0 + hl, 103.0, r + 0.9), at(t0 - hl, 103.0, r + 0.9)]
    zt = max(p[2] for p in bm_pts) + 0.1
    import bmesh
    bm = bmesh.new()
    BC.rings_solid(bm, [[(p[0], p[1], zt - 0.3) for p in bm_pts], [(p[0], p[1], zt) for p in bm_pts]])
    out.append(BC.new_obj("drum_landing", bm, "grating"))
    c = [(p[0], p[1], zt) for p in bm_pts]
    BC.railing(out, "drum_rail", [c[0], c[3], c[2], c[1]], 2.0, step=2.4)
    main.extend(keep_out(out, "drum"))


# ------------------------------------------------------------------------------------------------
# The free north-east corner, behind the conveyor and beside the dish: a cooler with a big fan, a control cabinet


def cooler(main, moving, anchors):
    out = []
    x0, x1, y0, y1 = 26.5, 39.5, 46.2, 57.6
    out.append(K.box("skid", x0, x1, y0, y1, 0.0, 0.9, "concrete", 0.2))
    n = 8
    for k in range(n):                                    # hazard edge along the front
        xa = x0 + 0.3 + (x1 - x0 - 0.6) * k / n
        xb = x0 + 0.3 + (x1 - x0 - 0.6) * (k + 1) / n
        out.append(K.box(f"skid_hz{k}", xa, xb, y0 - 0.1, y0 + 0.05, 0.3, 0.85, "hz_y" if k % 2 else "hz_k"))
    out.append(K.box("cooler", 27.6, 34.2, 50.4, 56.6, 0.9, 5.3, "steel", 0.15))
    out.extend(K.vent("cooler_v", (30.9, 50.4), 0.0, 2.3, 2.0, 4.4, 0.3, 4, "door_fr"))
    fc, zf = (30.9, 53.5), 5.3
    out.append(K.cyl("cooler_well", fc, 2.5, zf - 0.05, zf + 0.05, "duct_dk", 16))
    out.append(K.lathe_loop("cooler_ring", fc, [(2.5, zf), (2.9, zf), (2.9, zf + 0.8), (2.5, zf + 0.8)], "door_fr", 16))
    for k, a in enumerate((45.0, 135.0)):
        out.append(beam(f"cooler_guard{k}", polar(fc, 2.7, a, zf + 0.78), polar(fc, 2.7, a + 180.0, zf + 0.78), 0.22, 0.16, "door_fr"))
    parts = BC.rotor("fan_0", 5, 0.4, 2.3, 0.8, 1.4, 0.13, 24.0, "blade", hub=(0.5, -0.08, 0.14, 0.34), tips=0.55)
    origin = (fc[0], fc[1], zf + 0.32)
    keep_out(parts, "fan_0", origin)
    moving.append((parts, "fan_0", origin, (0, 0, 1)))
    anchors.append(("fx_vent_0", (fc[0], fc[1], zf + 1.2), 0.5))
    # the control cabinet: a door with a lamp, a cable to the cooler and one to the dish's mast
    out.append(K.box("cabinet", 35.4, 39.0, 51.2, 56.4, 0.9, 7.4, "block", 0.15, 0.97))
    out.append(K.box("cabinet_cap", 35.2, 39.2, 51.0, 56.6, 7.4, 7.8, "rib"))
    lamp = BC.door(out, "cab_door", (37.2, 51.2), -90.0, 0.9, 2.4, 4.8)
    anchors.append(("fx_light_4", lamp, 0.6))
    out.extend(K.pipe("cab_cable", [(35.4, 54.0, 2.0), (34.8, 54.0, 2.0), (34.8, 54.0, 3.4), (34.2, 54.0, 3.4)], 0.3, "rubber", 6))
    d = P["dish"]
    out.extend(K.pipe("dish_cable", [(39.0, 52.6, 1.5), (d[0], 52.6, 0.35), (d[0], d[1] + 2.5, 0.35)], 0.28, "rubber", 6))
    # a lamp post at the front corner, two drums
    lp = (27.8, 47.6, 8.4)
    out.append(K.cyl("post_foot", lp[:2], 0.55, 0.9, 1.6, "steel_dk", 8))
    out.append(tube("post", (lp[0], lp[1], 0.9), (lp[0], lp[1], lp[2] - 0.4), 0.24, "rail", 6))
    out.append(K.cyl("post_cap", lp[:2], 0.42, lp[2] + 0.72, lp[2] + 0.95, "steel_dk", 8, 0.2))
    out.extend(K.lamp("post_lamp", lp, 0.6))
    anchors.append(("fx_light_5", (lp[0], lp[1], lp[2] + 0.25), 0.6))
    barrel(out, "skid_barrel0", (37.6, 47.9), "rust", 0.9)
    barrel(out, "skid_barrel1", (35.4, 47.6), "barrel", 0.9)
    # a flanged pipe with a valve and a hand wheel from the cooler to the conveyor's back wall
    back = WING.at(-17.2, 9.5 + 4.6)
    pa, pb, pc = (27.6, 53.5, 3.0), (back[0], 53.5, 3.0), (back[0], back[1] + 0.2, 3.0)
    BC.flanged_pipe(out, "cooler_pipe", [pa, pb, pc], 0.9, "copper")
    v = (pb[0], (pb[1] + pc[1]) / 2 + 2.5)
    out.append(K.cyl("cooler_valve", v, 1.35, 1.7, 4.4, "steel", 10))
    BC.handwheel(out, "cooler_valve_w", v, 5.4, 1.15)
    out.append(obox("cooler_pipe_support", (pb[0], pc[1] + 2.5), 0.0, 0.5, 0.5, 0.0, 2.4, "steel_dk"))
    main.extend(keep_out(out, "cooler"))


# ------------------------------------------------------------------------------------------------

REVIEW = dict(dist=9.0, target=(0.0, 0.5, 0.45),
              closeups=[("closeup_yard", -70, 30, 5.2, 70, (-0.35, 0.1, 0.55)), ("closeup_wing", -50, 34, 5.0, 70, (0.75, 0.45, 0.35)),
                        ("closeup_game", -90, 50, 8.0, 60, (0.0, 0.5, 0.4))])
PREVIEW_VIEWS = ((-70, 30, 5.2, (-0.35, 0.1, 0.55)), (-50, 34, 5.0, (0.75, 0.45, 0.35)), (-90, 55, 7.5, (0.0, 0.5, 0.3)),
                 (-90, 89, 3.4, (-1.0, 1.0, 0.3)), (-90, 60, 3.8, (0.95, 1.1, 0.3)), (-145, 30, 3.2, (-1.2, 0.9, 0.2)),
                 (-140, 25, 3.4, (0.6, 0.2, 0.2)), (-120, 40, 3.5, (-0.2, 0.8, 0.8)))

BC.setup(KEY, [("the approved model", approved), ("wing", wing), ("crane house", crane_house), ("yard", yard), ("drum", drum),
               ("cooler", cooler)],
         col=COL, spec=SPEC, kind=KIND, wear=WEAR, grime_w=GRIME_W, NODES=NODES, ANCHOR_DOC=ANCHOR_DOC, FLUE_TOP=EXHAUST, SHELL_MATS=(),
         SHELL_STREAKS=None, MAT_GRIME=MAT_GRIME, STAIN=STAIN, RUNOFF=RUNOFF, LOW=LOW, SURFACE_EXTRA=yard_stains, REVIEW=REVIEW, PREVIEW_VIEWS=PREVIEW_VIEWS, PREVIEW_DIST=8.0,
         DETAIL=dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.016, house_uv=0.6, ao_distance=0.3, dent=0.16, wear=0.4),
         REST=Y.rest, POSES=Y.POSES, POSE_SPEC=dict(anim_frames=Y.ANIM))

# the owner (2026-11-19): "we need to add dirt for sure" - the refinery's final level
BC.WEATHERING, BC.MOTTLE = 1.6, 1.8

if __name__ == "__main__":
    BC.main()
