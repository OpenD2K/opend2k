"""Shared kit of the Atreides vehicle / aircraft generators (tools/models/atreides_<unit>.py): the mesh helpers,
weathering and bake of combat_tank_v3.py / devastator_v1.py / harvester_v1.py behind one small interface, plus the
parts the vehicles share - wheels, track pods, armour pods, barrels, rocket racks, jet nozzles, house panels - and
the generic command line (build -> silhouette IoU -> bake -> glb (+ standalone turret, + pose variants) -> review
renders).

A generator looks like this:

    import atreides_vehicle_kit as K
    RENDER = dict(...)                      # light / look of the 2D pack (read by vehicle_check/pack.py)
    P = dict(...)                           # proportions in ORIGINAL SPRITE PIXELS (u forward, v right, z up)
    K.palette(COL, SPEC, FILM)              # the vehicle's materials on top of the kit's
    def build():                            # -> root; K.node(parts, "body"), K.node(tparts, "turret", pivot) ...
    K.main("TRIKE", "trike", build, P)

Conventions (docs/3d-models.md "New models"): Blender units = tiles, +X east, +Y north = the vehicle's front, +Z up,
origin on the ground on the turning axis; node "body", optional "turret" (turns about its origin, which must lie on
the turning axis: the game draws the turret frame at the unit's position whatever the body's facing) with child
"barrel"; empties "fx_muzzle_N" at barrel tips, "fx_exhaust_N" etc. Materials: everything is baked into ONE atlas
material "body" except "HOUSE" (the house-colour mask; COLOR_0 = its shade).
"""
import copy
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402  generic helpers (join, bake, camera)
import combat_tank_v3 as V3  # noqa: E402  mesh helpers, materials, weathering, atlas

# devastator_v1 retunes V3's weathering tables on import: keep the tank's (the approved Atreides look)
_KEEP = {k: copy.deepcopy(getattr(V3, k)) for k in ("COL", "SPEC", "FILM", "DIRT", "SOOT", "GRIME_SCALE", "WEATHER", "UV_SCALE")}
import devastator_v1 as DV   # noqa: E402  more helpers, flat-colour IoU, review renders
for _k, _v in _KEEP.items():
    getattr(V3, _k).clear()
    getattr(V3, _k).update(_v)

PX = V3.PX
log = V2.log
srgb_to_linear = V2.srgb_to_linear
PREVIEW_HOUSE = (0.33, 0.45, 0.65)   # Atreides blue, previews only
KEEP = ("HOUSE",)

# The kit's palette: the tank's Atreides colours under generic names (sRGB). Generators add their own.
COL = dict(
    cream=(0.918, 0.864, 0.781),     # light armour: warm cream
    white=(0.970, 0.960, 0.930),     # the brightest plates
    sand=(0.800, 0.690, 0.560),      # peach / sand-coloured frames
    panel=(0.429, 0.407, 0.352),     # dark olive-grey plates
    mid=(0.660, 0.621, 0.533),       # dusty grey-beige
    dark=(0.176, 0.165, 0.143),      # dark steel: chassis, axles
    recess=(0.060, 0.057, 0.050),    # near-black: seams, bores, sockets
    tyre=(0.105, 0.097, 0.085),      # rubber
    tyre_lt=(0.300, 0.280, 0.250),   # a tyre's dusty tread
    hub=(0.560, 0.540, 0.500),       # wheel hubs
    tread=(0.094, 0.088, 0.077),     # track belt
    link=(0.138, 0.127, 0.110),      # track links
    gun=(0.517, 0.495, 0.451),       # barrels
    steel=(0.640, 0.630, 0.600),
    steel_dk=(0.330, 0.320, 0.300),
    cyan=(0.200, 0.880, 0.950),      # the original's cyan lights / canopy lines
    glass=(0.110, 0.180, 0.240),     # dark canopy glass
    HOUSE=(0.80, 0.80, 0.80),
)
SPEC = dict(
    cream=(0.0, 0.34, "paint"), white=(0.0, 0.34, "paint"), sand=(0.0, 0.4, "paint"), panel=(0.0, 0.6, "paint"),
    mid=(0.0, 0.55, "paint"), dark=(0.3, 0.6, "dark"), recess=(0.0, 0.9, "dark"), tyre=(0.0, 0.8, "rubber"),
    tyre_lt=(0.0, 0.8, "rubber"), hub=(0.4, 0.45, "steel"), tread=(0.0, 0.75, "rubber"), link=(0.3, 0.55, "steel"),
    gun=(0.2, 0.36, "steel"), steel=(0.6, 0.35, "steel"), steel_dk=(0.5, 0.45, "steel"),
    cyan=(0.0, 0.2, "dark"), glass=(0.3, 0.15, "dark"), HOUSE=(0.0, 0.32, "house"),
)
FILM = dict(
    cream=(0.759, 0.704, 0.616), white=(0.770, 0.743, 0.693), sand=(0.690, 0.590, 0.480), panel=(0.506, 0.473, 0.407),
    mid=(0.572, 0.533, 0.451), tyre=(0.230, 0.210, 0.180), tyre_lt=(0.380, 0.350, 0.300), hub=(0.520, 0.490, 0.440),
    tread=(0.209, 0.193, 0.165), link=(0.231, 0.215, 0.182), gun=(0.550, 0.517, 0.462), steel=(0.560, 0.540, 0.500),
    steel_dk=(0.340, 0.325, 0.300),
)
P = dict(house_low=0.6)


def palette(col=None, spec=None, film=None, weather=None, dirt=None, uv_scale=None, grime_scale=None):
    """Install the kit's materials + the generator's into the tank's tables (call once, before build())."""
    V3.COL.update(COL)
    V3.SPEC.update(SPEC)
    V3.FILM.update(FILM)
    V3.COL.update(col or {})
    V3.SPEC.update(spec or {})
    V3.FILM.update(film or {})
    V3.SOOT.clear()
    V3.WEATHER.update(weather or {})
    V3.DIRT.update(dirt or {})
    V3.UV_SCALE.update(dict(recess=0.4, dark=0.5, tyre=0.5, tyre_lt=0.5, steel_dk=0.6, cyan=0.2, glass=0.3, HOUSE=0.06))
    V3.UV_SCALE.update(uv_scale or {})
    V3.GRIME_SCALE.update(grime_scale or {})
    DV.PREVIEW_HOUSE = PREVIEW_HOUSE
    DV.P["house_low"] = P["house_low"]


# ------------------------------------------------------------------------------------------------
# Mesh helpers (px: x = v right, y = u forward, z up)

box, prism_uz, cyl, loft, new_obj, rings_solid = V3.box, V3.prism_uz, V3.cyl, V3.loft, V3.new_obj, V3.rings_solid
hull_pts, round_poly, inset, along = V3.hull_pts, V3.round_poly, V3.inset, V3.along
shade, sphere, plan_prism, pillow, prism_vz, tube_z, ring_u, arc_shell = (DV.shade, DV.sphere, DV.plan_prism, DV.pillow, DV.prism_vz,
                                                                         DV.tube_z, DV.ring_u, DV.arc_shell)


def house(o, k=1.0, low=None):
    """A house-colour part: COLOR_0 shade k (1 = the ramp's full range), its lower edge darkened to `low`."""
    return shade(o, k, low)


def group(o, name):
    """Mark a part as belonging to a moving group: a vertex group that survives the join (pose())."""
    o["d2k_group"] = name
    return o


def tube(name, p0, p1, r, material, segs=10, r1=None, bev=0.0):
    """Cylinder / cone between two points (v, u, z)."""
    a, b = Vector(p0), Vector(p1)
    d = (b - a).normalized()
    s = d.orthogonal().normalized()
    t = d.cross(s)
    rb = r if r1 is None else r1
    ring0 = [tuple(a + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
    ring1 = [tuple(b + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * rb) for i in range(segs)]
    bm = bmesh.new()
    rings_solid(bm, [ring0, ring1])
    return new_obj(name, bm, material, bev)


def ellipsoid(name, c, r, material, segs=20, rings=12):
    """Ellipsoid centred on c = (v, u, z) with radii r = (rv, ru, rz)."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=1.0)
    bmesh.ops.scale(bm, verts=bm.verts, vec=Vector(r))
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(c))
    return new_obj(name, bm, material)


def wedge(name, pts_vu_bottom, pts_vu_top, z0, z1, material, bev=0.0, segs=2):
    """Plan polygon at z0 lofted to another (same vertex count) at z1: sloped armour blocks."""
    bm = bmesh.new()
    rings_solid(bm, [[(v, u, z0) for (v, u) in pts_vu_bottom], [(v, u, z1) for (v, u) in pts_vu_top]])
    return new_obj(name, bm, material, bev, segs)


def rect(v0, v1, u0, u1):
    """Counter-clockwise plan rectangle (v, u)."""
    return [(v0, u0), (v1, u0), (v1, u1), (v0, u1)]


def mirror_all(parts):
    """Append the mirror image (v -> -v) of every part made so far."""
    for o in list(parts):
        V2.apply_all(o)
        m = V3.mirror_copy(o)
        for k in ("d2k_shade", "d2k_low", "d2k_group"):
            if k in o:
                m[k] = o[k]
        parts.append(m)


# ------------------------------------------------------------------------------------------------
# Shared parts


def wheel(parts, u, v, r, w, tyre="tyre", hub="hub", lugs=10, z=None, hub_r=0.45, name="wheel", squash=1.0):
    """A road wheel on the right (v = its centre plane, w = width): a tyre with rounded shoulders, lugs across the
    tread, a dished hub on both faces. z = axle height (default r)."""
    z = r if z is None else z
    v0, v1 = v - w / 2, v + w / 2
    sh = min(0.35 * w, 0.4 * r)
    bm = bmesh.new()
    rings = []
    segs = 20
    for (vv, rr) in ((v0, r - sh), (v0 + sh * 0.5, r - sh * 0.25), (v0 + sh, r), (v1 - sh, r), (v1 - sh * 0.5, r - sh * 0.25), (v1, r - sh)):
        rings.append([(vv, u + rr * math.cos(2 * math.pi * i / segs), z + squash * rr * math.sin(2 * math.pi * i / segs)) for i in range(segs)])
    rings_solid(bm, rings)
    parts.append(new_obj(name, bm, tyre))
    if lugs:
        bm = bmesh.new()
        for i in range(lugs):
            a = 2 * math.pi * (i + 0.5) / lugs
            c, s = math.cos(a), math.sin(a)
            t = Vector((0, -s, c))
            n = Vector((0, c, s))
            ctr = Vector((0, u, z)) + n * (r - 0.02)
            hw = 0.11 * r
            ring = [ctr + t * (-hw) + n * (-0.1), ctr + t * hw + n * (-0.1), ctr + t * hw * 0.7 + n * 0.14, ctr - t * hw * 0.7 + n * 0.14]
            rings_solid(bm, [[(vv, q.y, z + squash * (q.z - z)) for q in ring] for vv in (v0 + sh * 0.8, v1 - sh * 0.8)])
        parts.append(new_obj(name + "_lugs", bm, "tyre_lt" if tyre == "tyre" else tyre))
    for (a, b) in ((v0 - 0.08, v0 + 0.25), (v1 - 0.25, v1 + 0.08)):
        parts.append(cyl(name + "_hub", r * hub_r, a, b, u, z, hub, segs=14, axis="v", bev=0.08))
        parts.append(cyl(name + "_cap", r * hub_r * 0.4, a - 0.06, b + 0.06, u, z, "dark", segs=10, axis="v"))


def track(parts, profile, v0, v1, belt_t=0.45, pitch=0.8, link_len=0.55, link_h=0.14, rnd=0.7, core=None, wheels=(), name="track"):
    """A track on the right: belt loop along the (u, z) profile (counter-clockwise from the rear bottom) between
    v0 and v1, link pads all round, a dark core, road wheels / idlers ((u, z, r), ...) showing at the open ends."""
    prof = round_poly(profile, rnd, 3)
    inner = inset(prof, belt_t)
    n = len(prof)
    bm = bmesh.new()
    ro = [[bm.verts.new((v, u, z)) for (u, z) in prof] for v in (v0, v1)]
    ri = [[bm.verts.new((v, u, z)) for (u, z) in inner] for v in (v0, v1)]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([ro[0][i], ro[0][j], ro[1][j], ro[1][i]])
        bm.faces.new([ri[0][j], ri[0][i], ri[1][i], ri[1][j]])
        bm.faces.new([ro[0][j], ro[0][i], ri[0][i], ri[0][j]])
        bm.faces.new([ro[1][i], ro[1][j], ri[1][j], ri[1][i]])
    parts.append(new_obj(name + "_belt", bm, "tread"))
    bm = bmesh.new()
    c = link_len * 0.28
    sect = [(-link_len / 2, -0.03), (-link_len / 2, link_h * 0.5), (-link_len / 2 + c, link_h), (link_len / 2 - c, link_h),
            (link_len / 2, link_h * 0.5), (link_len / 2, -0.03)]
    for (p, tg) in along(prof, pitch):
        nrm = Vector((tg.y, -tg.x))
        ring = [p + tg * s + nrm * hh for (s, hh) in sect]
        rings_solid(bm, [[(v, q.x, q.y) for q in ring] for v in (v0 + 0.06, v1 - 0.06)])
    parts.append(new_obj(name + "_links", bm, "link"))
    us = [p[0] for p in profile]
    zs = [p[1] for p in profile]
    if core is None:
        core = (min(us) + 2.6, max(us) - 2.6, min(zs) + 0.7, max(zs) - 0.55)
    parts.append(box(name + "_core", v0 + 0.02, v1 - 0.02, core[0], core[1], core[2], core[3], "recess"))
    for (cu, cz, r) in wheels:
        parts.append(cyl(name + "_wheel", r, v0 + 0.1, v1 - 0.1, cu, cz, "dark", segs=18, bev=0.1, axis="v"))
        parts.append(cyl(name + "_whub", r * 0.42, v0 - 0.08, v1 + 0.08, cu, cz, "link", segs=12, bev=0.05, axis="v"))


def barrel(parts, u0, tip, z, r, v=0.0, material="gun", muzzle=0.0, collar="steel_dk", name="barrel", bore=True, segs=16):
    """A gun tube along u from u0 to tip at height z (v = its lateral offset): optional muzzle step, collar at the
    root, the bore."""
    parts.append(cyl(name, r, u0, tip - (0.5 if muzzle else 0.0), v, z, material, segs=segs))
    if muzzle:
        parts.append(cyl(name + "_muzzle", r + muzzle, tip - 0.9, tip, v, z, material, segs=segs, bev=0.08))
    if collar:
        parts.append(cyl(name + "_collar", r + 0.2, u0, u0 + 0.7, v, z, collar, segs=segs, bev=0.06))
    if bore:
        parts.append(cyl(name + "_bore", r * 0.55, tip - 0.3, tip + 0.015, v, z, "recess", segs=10))


def rocket_rack(parts, u0, u1, v0, v1, z0, z1, cols, rows, pitch_deg=0.0, case="panel", tube_mat="recess", tip="white", name="rack"):
    """A box launcher: cols x rows tubes opening towards +u, pitched up by pitch_deg about its rear lower edge."""
    made = [box(name, v0, v1, u0, u1, z0, z1, case, 0.25)]
    cw, ch = (v1 - v0) / cols, (z1 - z0) / rows
    r = min(cw, ch) * 0.36
    for i in range(cols):
        for j in range(rows):
            cv, cz = v0 + cw * (i + 0.5), z0 + ch * (j + 0.5)
            made.append(cyl(name + "_tube", r, u1 - 0.3, u1 + 0.03, cv, cz, tube_mat, segs=10))
            if tip:
                made.append(cyl(name + "_tip", r * 0.6, u1 - 0.2, u1 + 0.25, cv, cz, tip, segs=8, r1=r * 0.15))
    if pitch_deg:
        m = Matrix.Translation(Vector((0, u0, z0))) @ Matrix.Rotation(math.radians(pitch_deg), 4, "X") @ Matrix.Translation(Vector((0, -u0, -z0)))
        for o in made:
            o.data.transform(m)
    parts.extend(made)
    return made


def nozzle(parts, p, d, r, length, material="steel_dk", glow=None, name="nozzle", segs=14):
    """A jet / exhaust nozzle at p = (v, u, z) pointing along d: a short flared tube with a dark (or glowing) bore."""
    a = Vector(p)
    dn = Vector(d).normalized()
    b = a + dn * length
    parts.append(tube(name, tuple(a), tuple(b), r * 0.8, material, segs=segs, r1=r))
    parts.append(tube(name + "_lip", tuple(b - dn * 0.35), tuple(b), r * 1.08, material, segs=segs))
    parts.append(tube(name + "_bore", tuple(b - dn * 0.25), tuple(b + dn * 0.03), r * 0.78, glow or "recess", segs=segs))


def pod(name, v0, v1, u0, u1, z0, z1, material="cream", chamfer=0.8, crown=0.35, bev=0.2):
    """An armour pod: a box with chamfered plan corners and a slightly crowned top (the tank's pale pods)."""
    c = min(chamfer, (v1 - v0) * 0.45, (u1 - u0) * 0.45)
    plan = [(v0 + c, u0), (v1 - c, u0), (v1, u0 + c), (v1, u1 - c), (v1 - c, u1), (v0 + c, u1), (v0, u1 - c), (v0, u0 + c)]
    return pillow(name, plan, z0, z1, material, edge=crown, drop=min(crown, (z1 - z0) * 0.5), bev=bev)


def seam(name, v0, v1, u0, u1, z, depth=0.25, material="recess"):
    """A dark panel line: a thin near-black strip lying on a surface at height z."""
    return box(name, v0, v1, u0, u1, z - depth, z + 0.04, material)


# ------------------------------------------------------------------------------------------------
# Nodes, anchors, poses


def node(parts, name, origin_px=(0, 0, 0), parent=None, offset_px=(0, 0, 0)):
    """Join parts (px) into the node `name` with its origin at origin_px (v, u, z); vertex groups from group()."""
    for o in parts:
        g = o.get("d2k_group")
        if g:
            o.vertex_groups.new(name=g).add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    j = V3.finish_node(parts, name, origin_px, offset_px)
    if parent is not None:
        j.parent = parent
        bpy.context.view_layer.update()
        if parent.type == "MESH":
            j.matrix_parent_inverse = parent.matrix_world.inverted()
    return j


def root(key):
    r = bpy.data.objects.new(key, None)
    bpy.context.scene.collection.objects.link(r)
    return r


def anchor(name, p_px, parent):
    """An effect anchor (docs/3d-models.md "Anchors"): an empty at p = (v, u, z) px under `parent`."""
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    e.empty_display_size = 0.02
    e.parent = parent
    bpy.context.view_layer.update()
    e.matrix_world = Matrix.Translation(Vector(p_px) * PX)
    return e


def pose(obj, mats, rest=None):
    """Move the vertices of obj's groups by {group: Matrix (tiles)}; returns the rest coordinates (pass them back
    to go to another pose, or pose(obj, {}, rest) to restore)."""
    me = obj.data
    if rest is None:
        rest = [v.co.copy() for v in me.vertices]
    gi = {g.index: g.name for g in obj.vertex_groups}
    for v, co in zip(me.vertices, rest):
        m = None
        for ge in v.groups:
            if ge.weight > 0.5 and gi.get(ge.group) in mats:
                m = mats[gi[ge.group]]
        v.co = (m @ co) if m is not None else co
    me.update()
    return rest


def rot_about(p_px, axis, deg):
    """Rotation by deg about the axis ('X' = v, 'Y' = u, 'Z' or a vector) through p (px) -> Matrix in tiles."""
    piv = Matrix.Translation(Vector(p_px) * PX)
    ax = axis if isinstance(axis, str) else Vector(axis)
    return piv @ Matrix.Rotation(math.radians(deg), 4, ax) @ piv.inverted()


# ------------------------------------------------------------------------------------------------
# Materials, bake


def finish_materials():
    for name in list(V3._mats):
        m = V3.mat(name)
        c = PREVIEW_HOUSE if name == "HOUSE" else V3.COL[name]
        m.diffuse_color = (*srgb_to_linear(c), 1)


SURFACE = None      # optional hook: f(nt, name, (colour, rough, metal)) -> (colour, rough, metal)


def procedural_materials():
    out = {}
    for name, m in list(V3._mats.items()):
        if name in KEEP:
            continue
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        c, r, mt = V3.surface_tree(nt, name)
        if SURFACE is not None:
            c, r, mt = SURFACE(nt, name, (c, r, mt))
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        out[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
    return out


def bake_and_merge(objs, size, samples=24):
    """Bake all materials but HOUSE into one atlas material "body" and remap the slots (V3's bake)."""
    procs = procedural_materials()
    V3.uv_unwrap(objs, margin=0.003)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = V3.pack_mr(images, size)
    body_mat = V3.export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_vehicle_bake"))
    for o in objs:
        me = o.data
        for s in o.material_slots:
            if s.material and not s.material.name.startswith(KEEP):
                s.material = body_mat
        idx_map, keep = {}, []
        for s in me.materials:
            key = s.name if s else ""
            if key not in idx_map:
                idx_map[key] = len(keep)
                keep.append(s)
        new_index = {si: idx_map[(s.name if s else "")] for si, s in enumerate(me.materials)}
        old_idx = [p.material_index for p in me.polygons]
        me.materials.clear()
        for s in keep:
            me.materials.append(s)
        for p, oi in zip(me.polygons, old_idx):
            p.material_index = new_index[oi]
    return body_mat


# ------------------------------------------------------------------------------------------------
# Silhouette IoU against the original (body with the turret hidden, turret alone)


ELEV = 45.0         # camera elevation of the silhouette check; flyers whose art is drawn from straight above set 90


def iou_report(rt, npz_path, flat_dir=None, scale=4, elev=None, align=True):
    import numpy as np
    elev = ELEV if elev is None else elev
    d = np.load(npz_path)
    ox, oy = (int(v) for v in d["origin"])
    n = d["body_opaque"].shape[1]
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.render_aa = "8"
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    size = n * scale
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    rt.parent = yaw
    tmp = os.path.join(bpy.app.tempdir, "d2k_iou_vehicle.png")
    cam = V2.game_camera(scene, elev, 32 * scale, size)
    tur = next((o for o in rt.children if o.name == "turret"), None)
    tur_objs = ([tur] + list(tur.children_recursive)) if tur else []
    meshes = [o for o in rt.children_recursive if o.type == "MESH"]

    def shoot(show):
        for o in meshes:
            o.hide_render = not show(o)
        frames = []
        for f in range(32):
            yaw.rotation_euler = (0, 0, -math.radians(f * 11.25))
            scene.render.filepath = tmp
            bpy.ops.render.render(write_still=True)
            img = bpy.data.images.load(tmp)
            frames.append(np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1].copy())
            bpy.data.images.remove(img)
        return np.array(frames)

    layers = [("body", lambda o: o not in tur_objs)] + ([("turret", lambda o: o in tur_objs)] if tur and "turret_opaque" in d else [])
    out, shift, result = {}, None, {}
    yy, xx = np.mgrid[0:size, 0:size]
    for name, show in layers:
        rgba = shoot(show)
        alpha = rgba[..., 3]
        if shift is None:
            ds = []
            for f in range(32):
                om = d["body_opaque"][f]
                ys, xs = np.nonzero(om)
                co = np.array([xs.mean() + 0.5 - ox, ys.mean() + 0.5 - oy])
                a = alpha[f]
                w = a.sum()
                cr = np.array([((xx + 0.5) * a).sum() / w - size / 2, ((yy + 0.5) * a).sum() / w - size / 2]) / scale
                ds.append(cr - co)
            dmean = np.mean(ds, 0) if align else np.zeros(2)
            shift = (int(round(-dmean[0] * scale)), int(round(-dmean[1] * scale)))
            log(f"IOU: render centroid - original centroid = ({np.mean(ds, 0)[0]:+.2f}, {np.mean(ds, 0)[1]:+.2f}) px -> frames moved "
                f"({shift[0] / scale:+.2f}, {shift[1] / scale:+.2f}) px")
        tx, ty = shift

        def place(a):
            return np.roll(np.roll(a, ty + (oy - n // 2) * scale, 0), tx + (ox - n // 2) * scale, 1)

        ious = []
        for f in range(32):
            r = place(alpha[f]).reshape(n, scale, n, scale).mean((1, 3)) > 0.5
            o = d[name + "_opaque"][f]
            ious.append((r & o).sum() / max(1, (r | o).sum()))
        result[name] = ious
        log(f"IOU {name}: mean {np.mean(ious):.3f} min {np.min(ious):.3f} | " + " ".join(f"{v:.2f}" for v in ious))
        if flat_dir:
            placed = np.stack([place(rgba[f]) for f in range(32)])
            out["rgba" if name == "body" else name + "_rgba"] = (np.clip(placed, 0, 1) * 255 + 0.5).astype(np.uint8)
    for o in meshes:
        o.hide_render = False
    yaw.rotation_euler = (0, 0, 0)
    bpy.data.objects.remove(cam)
    rt.parent = None
    bpy.data.objects.remove(yaw)
    if flat_dir:
        os.makedirs(flat_dir, exist_ok=True)
        np.savez_compressed(os.path.join(flat_dir, "flat.npz"), scale=scale, shift=np.array(shift), iou=np.array(result["body"]), **out)
    return result


# ------------------------------------------------------------------------------------------------
# Command line


def main(key, name, build, props, variants=None, after_build=None):
    """The generator's command line.

      blender -b --factory-startup --python tools/models/atreides_<name>.py -- [--out DIR] [--no-bake]
          [--bake-size 2048] [--set key=value ...] [--iou ORIG.npz [--flat DIR]] [--turntable DIR] [--samples 64]
          [--no-variants] [--variant NAME]

    variants: {name: f(root) -> undo()} pose variants written to DIR/variants/<KEY>_<name>.glb."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=2048, iou=None, flat=None, turntable=None, samples=64, variants=True, variant=None)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--no-variants":
            opt["variants"] = False
        elif a == "--variant":
            opt["variant"] = argv[i + 1]; i += 1
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--iou":
            opt["iou"] = argv[i + 1]; i += 1
        elif a == "--flat":
            opt["flat"] = argv[i + 1]; i += 1
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--set":
            k, v = argv[i + 1].split("=", 1)
            if k not in props:
                raise SystemExit("unknown proportion " + k)
            props[k] = float(v) if isinstance(props[k], float) else eval(v)   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None and opt["iou"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-" + name)

    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    rt = build()
    finish_materials()
    bpy.context.view_layer.update()
    objs = V2.meshes_under(rt)
    log(f"built: {len(objs)} meshes, {V2.tri_count(objs)} triangles in {time.time() - t0:.1f} s")
    if opt["iou"]:
        undo = variants[opt["variant"]](rt) if opt["variant"] else None      # --variant: measure that pose
        iou_report(rt, opt["iou"], opt["flat"])
        if undo:
            undo()
    if opt["bake"] and (opt["out"] or opt["turntable"]):
        t1 = time.time()
        bake_and_merge(objs, opt["bake_size"])
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
    if opt["out"]:
        unit_dir = os.path.join(opt["out"], "models", "unit")
        path = os.path.join(unit_dir, key + ".glb")
        V3.export_glb(rt, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        tur = next((o for o in rt.children if o.name == "turret"), None)
        if tur is not None:
            tpath = os.path.join(unit_dir, key + "_turret.glb")
            V3.export_glb(rt, tpath, only=tur)
            log(f"exported {tpath} (pivot {tuple(round(v, 4) for v in tur.matrix_world.translation)})")
        if variants and opt["variants"]:
            for vn, fn in variants.items():
                undo = fn(rt)
                V3.export_glb(rt, os.path.join(opt["out"], "variants", f"{key}_{vn}.glb"))
                undo()
            log(f"exported {len(variants)} variants -> {os.path.join(opt['out'], 'variants')}")
    if opt["turntable"]:
        t2 = time.time()
        undo = variants[opt["variant"]](rt) if opt["variant"] else None
        DV.review_renders(rt, opt["turntable"], opt["samples"])
        if undo:
            undo()
        log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")
