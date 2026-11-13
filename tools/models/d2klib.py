"""Shared helpers for the procedural OpenD2K 3D models (run inside Blender: see build_models.py).

Conventions (docs/3d-models.md, "New models"):
  * Blender units = map tiles (1 BU = 32 world px). Blender +X = east, +Y = north, +Z = up
    (the glTF exporter turns this into glTF +X east, +Y up, -Z north).
  * Vehicles face north (+Y); the origin is on the ground at the unit's centre.
  * Buildings: origin on the ground at the centre of the occupied cells; the side seen by the
    default camera (south) is -Y.
  * A node named "turret" (and its children) turns with the turret facing about its origin.
  * Colours: one Principled material per palette entry (base colour authored in sRGB, stored
    linear as glTF wants), metallic, roughness, emission for lights. Materials whose name starts
    with "HOUSE" are tinted with the owner's house colour by the game. COLOR_0 = baked ambient
    occlusion + grime (multiplies the base colour, glTF semantics).
Nothing here is derived from the game's data: shapes are modelled by eye after the original art.
"""

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

# --------------------------------------------------------------------------------------------
# Palette: name -> (sRGB base colour, metallic, roughness, emission strength)
# Dune look: sand-weathered pale metal, beige / brown / grey, copper fittings, cyan lights.

PALETTE = {
    "hull":        ((0.74, 0.67, 0.55), 0.35, 0.55, 0),   # Atreides pale sand armour
    "hull_light":  ((0.83, 0.78, 0.67), 0.30, 0.50, 0),
    "hull_tan":    ((0.66, 0.55, 0.42), 0.35, 0.55, 0),   # Harkonnen / Ordos tan-brown
    "hull_brown":  ((0.48, 0.38, 0.29), 0.35, 0.60, 0),
    "hull_copper": ((0.80, 0.58, 0.44), 0.50, 0.45, 0),   # trike / harvester pinkish copper
    "steel":       ((0.60, 0.60, 0.60), 0.85, 0.35, 0),
    "steel_dark":  ((0.30, 0.30, 0.31), 0.80, 0.45, 0),
    "dark":        ((0.13, 0.12, 0.11), 0.10, 0.85, 0),   # rubber, track belts, recesses
    "grille":      ((0.20, 0.18, 0.16), 0.50, 0.70, 0),
    "copper":      ((0.82, 0.52, 0.24), 0.90, 0.35, 0),   # pipes, fittings
    "brass":       ((0.86, 0.68, 0.36), 0.90, 0.30, 0),
    "rust":        ((0.42, 0.20, 0.13), 0.40, 0.70, 0),   # harvester scoop, worn metal
    "tyre":        ((0.68, 0.52, 0.38), 0.05, 0.85, 0),   # pale balloon tyres (harvester, trike)
    "bronze":      ((0.62, 0.42, 0.24), 0.85, 0.40, 0),
    "shell_hk":    ((0.58, 0.55, 0.52), 0.60, 0.45, 0),   # Harkonnen dark weathered steel
    "shell_or":    ((0.86, 0.62, 0.30), 0.85, 0.35, 0),   # Ordos gold
    "concrete":    ((0.62, 0.58, 0.52), 0.00, 0.90, 0),
    "concrete_dk": ((0.44, 0.41, 0.37), 0.00, 0.90, 0),
    "glass":       ((0.10, 0.13, 0.15), 0.20, 0.08, 0),
    "glow_cyan":   ((0.35, 0.95, 0.95), 0.00, 0.30, 4),   # lights / vents (not house colour)
    "glow_amber":  ((1.00, 0.70, 0.25), 0.00, 0.30, 4),
    "HOUSE":       ((0.80, 0.80, 0.80), 0.40, 0.45, 0),   # tinted per house (luminance x colour)
    "HOUSE_dark":  ((0.55, 0.55, 0.55), 0.40, 0.50, 0),
}
# Preview tint for HOUSE materials (Atreides blue) in the Blender renders only.
PREVIEW_HOUSE = (0.30, 0.45, 0.85)


def srgb_to_linear(c):
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


_mats = {}


def mat(name):
    """The palette material `name` (created once)."""
    if name in _mats and _mats[name].name in bpy.data.materials:
        return _mats[name]
    col, metal, rough, emit = PALETTE[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    lin = srgb_to_linear(col)
    bsdf.inputs["Base Color"].default_value = (*lin, 1)
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Roughness"].default_value = rough
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*lin, 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    # COLOR_0 (AO / grime) multiplies the base colour, as glTF does.
    ca = nt.nodes.new("ShaderNodeVertexColor")
    ca.layer_name = "Col"
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.inputs["Factor"].default_value = 1.0
    mul.inputs[6].default_value = (*lin, 1)
    nt.links.new(ca.outputs["Color"], mul.inputs[7])
    nt.links.new(mul.outputs[2], bsdf.inputs["Base Color"])
    _mats[name] = m
    return m


def reset():
    _mats.clear()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.objects):
        for x in list(coll):
            coll.remove(x)


# --------------------------------------------------------------------------------------------
# Objects

def empty(name, parent=None, loc=(0, 0, 0)):
    o = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(o)
    o.location = loc
    if parent:
        o.parent = parent
    return o


def mesh_obj(name, bm, material, parent=None, loc=(0, 0, 0)):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(mat(material))
    o.location = loc
    if parent:
        o.parent = parent
    return o


def _place(bm, center=(0, 0, 0), rot=None):
    m = Matrix.Translation(Vector(center))
    if rot is not None:
        m = m @ rot
    bmesh.ops.transform(bm, matrix=m, verts=bm.verts)


def box(name, size, center=(0, 0, 0), material="hull", parent=None, bevel=0.0, taper=None):
    """Axis-aligned box. taper=(sx, sy): scale of the top face (wedge shapes)."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        if taper and v.co.z > 0:
            v.co.x *= taper[0]
            v.co.y *= taper[1]
        v.co.x *= size[0]
        v.co.y *= size[1]
        v.co.z *= size[2]
    _place(bm, center)
    o = mesh_obj(name, bm, material, parent)
    if bevel:
        add_bevel(o, bevel)
    return o


def cyl(name, r, h, center=(0, 0, 0), axis="Z", segs=12, material="steel", parent=None, r2=None, bevel=0.0,
        cap=True):
    """Cylinder / cone frustum (r bottom, r2 top) along an axis, centred on `center`."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segs, radius1=r,
                          radius2=r if r2 is None else r2, depth=h)
    rot = {"Z": Matrix(), "X": Matrix.Rotation(math.pi / 2, 4, "Y"), "Y": Matrix.Rotation(-math.pi / 2, 4, "X")}[axis]
    _place(bm, center, rot)
    o = mesh_obj(name, bm, material, parent)
    if bevel:
        add_bevel(o, bevel)
    return o


def lathe(name, profile, segs=16, center=(0, 0, 0), material="hull", parent=None, a0=0.0, a1=2 * math.pi,
          cap_cut=False, axis="Z"):
    """Revolve a (radius, z) profile (bottom to top) about Z. A partial sweep (a0..a1) with
    cap_cut closes the two cut planes. Angle 0 = +X, counter-clockwise from above."""
    bm = bmesh.new()
    full = abs((a1 - a0) - 2 * math.pi) < 1e-6
    n = segs if full else segs + 1
    rings = []
    for i in range(n):
        a = a0 + (a1 - a0) * i / segs
        ring = []
        for (r, z) in profile:
            ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
        rings.append(ring)
    for i in range(segs):
        ra, rb = rings[i], rings[(i + 1) % n]
        for k in range(len(profile) - 1):
            vs = [ra[k], rb[k], rb[k + 1], ra[k + 1]]
            uniq = []
            for v in vs:
                if all((v.co - u.co).length > 1e-7 for u in uniq):
                    uniq.append(v)
            if len(uniq) >= 3:
                bm.faces.new(uniq)
    if not full and cap_cut:
        for ring, flip in ((rings[0], True), (rings[-1], False)):
            # cut face: the profile plus the axis points
            vs = list(ring)
            if profile[-1][0] > 1e-6:
                vs.append(bm.verts.new((0, 0, profile[-1][1])))
            if profile[0][0] > 1e-6:
                vs.append(bm.verts.new((0, 0, profile[0][1])))
            try:
                f = bm.faces.new(vs if not flip else list(reversed(vs)))
            except ValueError:
                pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    rot = {"Z": Matrix(), "X": Matrix.Rotation(math.pi / 2, 4, "Y"), "Y": Matrix.Rotation(-math.pi / 2, 4, "X")}[axis]
    _place(bm, center, rot)
    return mesh_obj(name, bm, material, parent)


def prism(name, pts, depth, axis="X", center=(0, 0, 0), material="hull", parent=None, bevel=0.0):
    """Extrude a 2D outline. axis X: pts are (y, z), extruded along X (side profiles of vehicles);
    axis Y: pts are (x, z) extruded along Y; axis Z: pts are (x, y) extruded up."""
    bm = bmesh.new()
    vs = []
    for (a, b) in pts:
        if axis == "X":
            vs.append(bm.verts.new((-depth / 2, a, b)))
        elif axis == "Y":
            vs.append(bm.verts.new((a, -depth / 2, b)))
        else:
            vs.append(bm.verts.new((a, b, -depth / 2)))
    f = bm.faces.new(vs)
    d = {"X": (depth, 0, 0), "Y": (0, depth, 0), "Z": (0, 0, depth)}[axis]
    r = bmesh.ops.extrude_face_region(bm, geom=[f])
    moved = [e for e in r["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=moved, vec=d)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    _place(bm, center)
    o = mesh_obj(name, bm, material, parent)
    if bevel:
        add_bevel(o, bevel)
    return o


def sphere(name, r, center=(0, 0, 0), segs=16, rings=8, material="hull", parent=None, scale=(1, 1, 1)):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=r)
    for v in bm.verts:
        v.co.x *= scale[0]
        v.co.y *= scale[1]
        v.co.z *= scale[2]
    _place(bm, center)
    return mesh_obj(name, bm, material, parent)


def add_bevel(o, width, segments=1, angle=40):
    m = o.modifiers.new("bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.harden_normals = False
    return m


def mirror_x(o):
    m = o.modifiers.new("mirror", "MIRROR")
    m.use_axis = (True, False, False)
    return m


def set_material(o, material):
    o.data.materials.clear()
    o.data.materials.append(mat(material))


def join(objs, name, parent=None):
    """Join mesh objects (modifiers applied) into one node named `name`, origin kept at the first
    object's parent space origin (0,0,0) unless re-set later."""
    objs = [o for o in objs if o]
    for o in objs:
        apply_modifiers(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    # keep world placement: clear parents first
    for o in objs:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    bpy.ops.object.join()
    j = bpy.context.view_layer.objects.active
    j.name = name
    j.data.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if parent:
        reparent(j, parent)
    return j


def reparent(o, parent):
    bpy.context.view_layer.update()
    mw = o.matrix_world.copy()
    o.parent = parent
    o.matrix_world = mw


def set_origin(o, point):
    """Move the object's origin to `point` (world) keeping the geometry in place."""
    bpy.context.view_layer.update()
    mw = o.matrix_world.copy()
    local = mw.inverted() @ Vector(point)
    o.data.transform(Matrix.Translation(-local))
    o.matrix_world = mw @ Matrix.Translation(local)


def apply_modifiers(o):
    if o.type != "MESH" or not o.modifiers:
        return
    bpy.context.view_layer.objects.active = o
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def array_along(o, count, offset):
    """Copies of a mesh object (linked data duplicated) offset by `offset` each; returns all."""
    out = [o]
    for i in range(1, count):
        c = o.copy()
        c.data = o.data.copy()
        bpy.context.scene.collection.objects.link(c)
        c.location = o.location + Vector(offset) * i
        out.append(c)
    return out


def dup(o, loc=None, name=None, mirror_x_axis=False, rot_z=None):
    c = o.copy()
    c.data = o.data.copy()
    bpy.context.scene.collection.objects.link(c)
    if name:
        c.name = name
    if mirror_x_axis:
        c.data.transform(Matrix.Scale(-1, 4, (1, 0, 0)))
        c.data.flip_normals()
        c.location.x = -o.location.x
    if rot_z is not None:
        c.data.transform(Matrix.Rotation(rot_z, 4, "Z"))
    if loc is not None:
        c.location = loc
    return c


# --------------------------------------------------------------------------------------------
# Finishing: normals, AO bake, export

def meshes(root):
    out = []
    stack = [root]
    while stack:
        o = stack.pop()
        if o.type == "MESH":
            out.append(o)
        stack.extend(o.children)
    return out


def finish(root, sharp_angle=35, ao_distance=0.18, ao_samples=48):
    """Apply modifiers, triangulate-safe cleanup, auto-smooth by angle, bake AO + grime into Col."""
    ms = meshes(root)
    for o in ms:
        apply_modifiers(o)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
        bm.to_mesh(o.data)
        bm.free()
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(sharp_angle))
        if "Col" not in o.data.color_attributes:
            o.data.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
        o.data.attributes.active_color = o.data.color_attributes["Col"]
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = ao_samples
    if sc.world is None:
        sc.world = bpy.data.worlds.new("w")
    sc.world.light_settings.distance = ao_distance
    bpy.ops.object.select_all(action="DESELECT")
    for o in ms:
        o.select_set(True)
    bpy.context.view_layer.objects.active = ms[0]
    sc.render.bake.target = "VERTEX_COLORS"
    bpy.ops.object.bake(type="AO", target="VERTEX_COLORS")
    # AO -> soft occlusion + slight sand grime near the ground.
    for o in ms:
        me = o.data
        col = me.color_attributes["Col"]
        mw = o.matrix_world
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                z = (mw @ me.vertices[vi].co).z
                ao = col.data[li].color[0]
                k = 0.22 + 0.78 * ao
                g = min(1.0, max(0.0, z / 0.12))
                dust = (0.93 + 0.07 * g, 0.88 + 0.12 * g, 0.80 + 0.20 * g)
                col.data[li].color = (k * dust[0], k * dust[1], k * dust[2], 1.0)


def tri_count(root):
    n = 0
    dg = bpy.context.evaluated_depsgraph_get()
    for o in meshes(root):
        me = o.evaluated_get(dg).to_mesh()
        me.calc_loop_triangles()
        n += len(me.loop_triangles)
        o.evaluated_get(dg).to_mesh_clear()
    return n


def export_glb(root, path):
    # Plain Principled values for the exporter: baseColorFactor = the palette colour (the AO
    # multiply node is a preview-only construct; COLOR_0 carries the AO).
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        b = m.node_tree.nodes.get("Principled BSDF")
        if not b:
            continue
        for l in list(b.inputs["Base Color"].links):
            m.node_tree.links.remove(l)
        key = m.name.split(".")[0]
        if key in PALETTE:
            b.inputs["Base Color"].default_value = (*srgb_to_linear(PALETTE[key][0]), 1)
    bpy.ops.object.select_all(action="DESELECT")
    for o in [root] + list(root.children_recursive):
        o.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=True,
        export_normals=True, export_vertex_color="ACTIVE", export_all_vertex_colors=False,
        export_attributes=False, export_materials="EXPORT", export_extras=False, export_tangents=False,
        export_image_format="NONE")


# --------------------------------------------------------------------------------------------
# Preview renders (turntable)

def relink_preview():
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        b = m.node_tree.nodes.get("Principled BSDF")
        mix = next((n for n in m.node_tree.nodes if n.bl_idname == "ShaderNodeMix"), None)
        if b and mix and not b.inputs["Base Color"].links:
            m.node_tree.links.new(mix.outputs[2], b.inputs["Base Color"])


def preview(root, out_prefix, dist=2.2, target_z=0.2, size=480, angles=8, elev=38, ground=6.0):
    """Render `angles` views around the model (EEVEE), sun from the north-west like the game."""
    sc = bpy.context.scene
    relink_preview()
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = sc.render.resolution_y = size
    sc.render.film_transparent = False
    sc.view_settings.view_transform = "Standard"
    # House preview tint.
    for m in bpy.data.materials:
        if m.name.startswith("HOUSE"):
            b = m.node_tree.nodes.get("Principled BSDF")
            base = PALETTE[m.name.split(".")[0]][0]
            l = 0.299 * base[0] + 0.587 * base[1] + 0.114 * base[2]
            tint = srgb_to_linear(tuple(min(1, c * l * 1.8) for c in PREVIEW_HOUSE))
            for n in m.node_tree.nodes:
                if n.bl_idname == "ShaderNodeMix":
                    n.inputs[6].default_value = (*tint, 1)
    # Ground, world, sun.
    gm = bpy.data.materials.new("ground_prev")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb_to_linear((0.78, 0.60, 0.40)), 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1
    bpy.ops.mesh.primitive_plane_add(size=ground * 2, location=(0, 0, -0.001))
    gp = bpy.context.active_object
    gp.data.materials.append(gm)
    w = sc.world or bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.33, 0.32, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.9
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = math.radians(3)
    so = bpy.data.objects.new("sun", sun)
    sc.collection.objects.link(so)
    # direction towards the sun: north-west (-x, +y), 40 deg up; a sun light points down its -Z
    to_sun = Vector((-math.cos(math.radians(40)) * 0.7071, math.cos(math.radians(40)) * 0.7071, math.sin(math.radians(40))))
    so.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()
    cam = bpy.data.cameras.new("cam")
    cam.lens = 50
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    tgt = Vector((0, 0, target_z))
    files = []
    for i in range(angles):
        az = math.radians(-90 - 360 * i / angles)  # first view from the south (game default)
        e = math.radians(elev)
        pos = tgt + Vector((math.cos(az) * math.cos(e), math.sin(az) * math.cos(e), math.sin(e))) * dist
        co.location = pos
        co.rotation_euler = (tgt - pos).to_track_quat("-Z", "Y").to_euler()
        f = f"{out_prefix}_{i}.png"
        sc.render.filepath = f
        bpy.ops.render.render(write_still=True)
        files.append(f)
    return files


def hull_pts(name, pts, material="hull", parent=None, bevel=0.0):
    """Convex hull of a point cloud: angular armour pieces (turrets, wedges)."""
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1), verts=bm.verts, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = mesh_obj(name, bm, material, parent)
    if bevel:
        add_bevel(o, bevel)
    return o


def sym(pts):
    """Mirror (x, y, z) points across x = 0 (adds the -x copies)."""
    out = list(pts)
    for (x, y, z) in pts:
        if abs(x) > 1e-9:
            out.append((-x, y, z))
    return out


def node(o, name, origin, parent):
    """Give a mesh its own node: origin at `origin` (world), parented to `parent`."""
    apply_modifiers(o)
    o.name = name
    o.data.name = name
    set_origin(o, origin)
    reparent(o, parent)
    return o
