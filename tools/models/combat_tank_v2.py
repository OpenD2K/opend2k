"""Atreides combat tank, hand-crafted (v2) — a modern game asset in the original's forms.

    blender -b --factory-startup --python tools/models/combat_tank_v2.py -- \
        [--out DIR]            write DIR/models/unit/ATREIDES_COMBAT_TANK.glb (+ _turret.glb: the turret alone)
                               (default artpacks/models-fable; --model-pack / render_pack --models take DIR)
        [--turret-scale 1.0]   turret size about its pivot (1.0 = the sprite's 0.33 width; 0.9 = smaller)
        [--no-x-offset]        do not shift the model +0.4 px east (see P["x_offset"])
        [--no-bake]            plain colours instead of the baked texture atlas (fast iteration)
        [--bake-size 1024]     atlas size (one base-colour + one metallic/roughness PNG in the .glb)
        [--iou FIT.npz]        silhouette IoU against the original frames (tools/models3d/make_refs.py
                               fit.npz: body/turret masks + anchors, 32 facings), game camera
        [--elev 45[,36]]       camera elevation(s) for --iou (the pipeline uses 45; the original
                               sprites measure ~36, see the notes below)
        [--iou-sheet DIR]      write overlay sheets (render mask vs original) for the --iou run
        [--turntable DIR]      8 Cycles views around the model (house colour previewed blue)
        [--samples 64]

Conventions (docs/3d-models.md "New models"): Blender units = tiles, +X east, +Y north = the
tank's front, +Z up, origin on the ground at the unit centre; nodes "body", "turret" (origin at the
turret ring = the pivot) and "barrel" (child of the turret, origin at the trunnion). Materials:
everything but the house areas is baked into ONE atlas material "body" (base colour, metallic /
roughness), so both the 2D render pipeline (tools/render_sprites) and the 3D loader (one texture
per model) read it; "HOUSE" is the house-colour mask (plain material, named as render_pack and
model_gltf expect).

The original (ATREIDES COMBAT TANK, DATA.R16, measured from the 32 facings): an "H" plan, two
tall armoured side pods 0.22 tiles wide with a 0.19 hull gap; pods 0.87 long (rear 0.47 / front
0.40 from the centre) with chamfered (wedge) noses; the owner's reading, which this model follows:
the pods are WW1 Mark IV-like armoured boxes (squared, not the pointed rhombus) with the TRACK
BELT running round the whole pod perimeter (dark rim round the pale riveted side plate, exposed on
the slanted noses and over the pod ends), no road wheels; a pale roof cover over the top run
(~0.49 long) with the house-colour caps at both ends; a dark recessed centre section between the
pods carrying the turret ring; a low cream hull with a rounded rear deck and a bright glacis; a
turret 0.34 wide tapering to the front with a dark lower band, a house-colour stripe along the
roof centre, and a thick ribbed gun 0.33 long. Vertical layout: the two blue bands of the side
view sit 8 px apart for a 0.42-tile lateral distance -> sin(elev) ~ 0.59, i.e. the sprites were
rendered from ~36 deg; with that the pod tops come out at z ~0.23, the turret roof at ~0.35, the
gun axis at 0.30 (tiles). Rendered at the pipeline's 45 deg the silhouettes still match at
IoU ~0.80 (body 0.81, turret 0.80).

Pack rendering that matched the original's tones (measured on facing 8 against the sprite's
luminance distribution): render_pack.py --exposure 0.4 --look None --house-grey 0.08 --sharpen 0.7.

Nothing here is derived from game data: proportions were read off the sprites by eye / pixel
counts, all geometry is procedural.
"""
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

# ------------------------------------------------------------------------------------------------
# Proportions (tiles). Measured against the original frames (see the module docstring).

P = dict(
    # track pods: rhomboid outline (nose tips at rear / front, height zn), belt top, belt band
    rear=-0.445, front=0.39,         # a little shorter than measured (owner: compact, tall)
    pod_in=0.094, pod_out=0.305,
    belt_top=0.222, pod_taper=0.13,
    nose_z=(0.045, 0.14),           # the vertical part of the nose faces (z range)
    chamfer_bot=(0.06, 0.05), chamfer_top=(0.12, 0.09),     # nose chamfer lengths (rear, front)
    belt_x=(0.098, 0.305), belt_thick=0.02,                 # the belt wraps the whole pod width (noses open, as round 2), thin band
    plate_inset=0.012, plate_recess=0.016, plate_lean=0.02,  # side plate inside the belt rim
    roof_y=(-0.245, 0.235), roof_top=0.252, roof_chamfer_x=0.275,
    band=0.09, cap_height=0.018,    # house-colour caps at both ends of the roof (length, rise)
    # hull between the pods
    hull_w=0.094, hull_rear=-0.25, hull_front=0.405, hull_top=0.19, hull_bot=0.065,   # hull ends under the turret: open gap between the pods behind it (sprite rows +8.. show ground)
    glacis_y=0.10, hull_nose_z=0.075,
    # turret
    pivot=(0.0, 0.0),               # turret centred on the body (owner)
    x_offset=0.012,                 # whole model +0.4 px east: the original frames rotate about their anchor PIXEL CENTRE
    tur_scale=1.0,                  # turret plan / height scale about the pivot (1.0 = the sprite's 0.33 width; 0.9 = smaller variant)
    ring_r=0.10, ring_top=0.27,
    tur_rear=-0.23, tur_front=0.19, tur_wr=0.33, tur_wf=0.26,
    tur_bot=0.27, tur_top=0.39, tur_side_slope=0.9, tur_dark_band=0.3, tur_roof_scale=0.78, tur_base_flare=1.08, tur_dark_height=0.55,
    roof_groove_w=0.024,            # the dark diagonal seams of the roof (an "A" converging on the gun)
    hump=(-0.13, 0.10, 0.046, 0.026),   # y0, y1, half width, height (house stripe on top; original: 3 px wide, ~0.22 long)
    gun_z=0.35, gun_r=0.042, gun_muzzle=0.54,    # the sprite's gun: ~3 px thick, muzzle ~8-9 px past the turret body
    link_step=0.024, link_len=0.012, link_h=0.006,  # fine rounded tread links
)

# sRGB base colours
COL = dict(
    cream=(0.97, 0.90, 0.72),       # armour paint (pods, turret): the original's warm white
    cream_hull=(0.98, 0.90, 0.68),  # the hull, a shade warmer
    turret_cream=(0.70, 0.68, 0.60),  # the turret's upper plates: grey-cream, darker than the pods
    turret_mid=(0.46, 0.46, 0.47),    # turret base plates / lower turret / mantlet / outer roof: mid grey (dark from the base up, not black)
    wear=(0.42, 0.42, 0.41),        # bare metal on worn edges
    rubber=(0.11, 0.105, 0.095),
    steel_dark=(0.30, 0.30, 0.30),
    steel=(0.50, 0.50, 0.49),
    gun=(0.42, 0.42, 0.41),
    glass=(0.08, 0.14, 0.18),
    grille=(0.14, 0.13, 0.12),
    HOUSE=(0.80, 0.80, 0.80),
)
PREVIEW_HOUSE = (0.20, 0.38, 0.85)   # Atreides blue, previews only


def srgb_to_linear(c):
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def log(*a):
    print(*a, flush=True)


# ------------------------------------------------------------------------------------------------
# Mesh helpers

_mats = {}


def mat(name):
    if name in _mats and _mats[name].name in bpy.data.materials:
        return _mats[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*srgb_to_linear(COL[name]), 1)
    spec = dict(cream=(0.08, 0.55), cream_hull=(0.08, 0.55), turret_cream=(0.08, 0.6), turret_mid=(0.5, 0.5), wear=(0.9, 0.38), rubber=(0.0, 0.9),
                steel_dark=(0.85, 0.55), steel=(0.9, 0.4), gun=(0.9, 0.36), glass=(0.0, 0.1),
                grille=(0.5, 0.8), HOUSE=(0.15, 0.5))[name]
    b.inputs["Metallic"].default_value = spec[0]
    b.inputs["Roughness"].default_value = spec[1]
    _mats[name] = m
    return m


def new_obj(name, bm, material, parent=None):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(mat(material))
    if parent:
        o.parent = parent
    return o


def bevel(o, width, segments=2, angle=35, clamp=True):
    m = o.modifiers.new("bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.use_clamp_overlap = clamp
    m.harden_normals = True
    return o


def box(name, x0, x1, y0, y1, z0, z1, material, bev=0.0, segs=2, top_scale=None, parent=None):
    """Axis-aligned box; top_scale=(sx, sy) tapers the top face about the box centre."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    for v in bm.verts:
        sx = sy = 1.0
        if top_scale and v.co.z > 0:
            sx, sy = top_scale
        v.co.x = cx + v.co.x * (x1 - x0) * sx
        v.co.y = cy + v.co.y * (y1 - y0) * sy
        v.co.z = z0 + (v.co.z + 0.5) * (z1 - z0)
    o = new_obj(name, bm, material, parent)
    if bev:
        bevel(o, bev, segs)
    return o


def prism(name, pts_yz, x0, x1, material, bev=0.0, segs=2, parent=None):
    """Side profile (y, z) extruded along x from x0 to x1."""
    bm = bmesh.new()
    vs = [bm.verts.new((x0, y, z)) for y, z in pts_yz]
    f = bm.faces.new(vs)
    r = bmesh.ops.extrude_face_region(bm, geom=[f])
    moved = [e for e in r["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=moved, vec=(x1 - x0, 0, 0))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = new_obj(name, bm, material, parent)
    if bev:
        bevel(o, bev, segs)
    return o


def plan_prism(name, pts_xy, z0, z1, material, bev=0.0, segs=2, bottom_scale=1.0, parent=None):
    """Plan polygon (x, y) extruded from z0 to z1; bottom face scaled about its centre."""
    bm = bmesh.new()
    cx = sum(p[0] for p in pts_xy) / len(pts_xy)
    cy = sum(p[1] for p in pts_xy) / len(pts_xy)
    vs = [bm.verts.new((cx + (x - cx) * bottom_scale, cy + (y - cy) * bottom_scale, z0)) for x, y in pts_xy]
    f = bm.faces.new(vs)
    r = bmesh.ops.extrude_face_region(bm, geom=[f])
    moved = [e for e in r["geom"] if isinstance(e, bmesh.types.BMVert)]
    for v in moved:
        v.co.x = cx + (v.co.x - cx) / bottom_scale
        v.co.y = cy + (v.co.y - cy) / bottom_scale
        v.co.z = z1
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = new_obj(name, bm, material, parent)
    if bev:
        bevel(o, bev, segs)
    return o


def cyl(name, r, length, centre, axis, material, segs=16, r2=None, bev=0.0, bsegs=1, parent=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segs, radius1=r,
                          radius2=r if r2 is None else r2, depth=length)
    rot = {"Z": Matrix(), "X": Matrix.Rotation(math.pi / 2, 4, "Y"), "Y": Matrix.Rotation(-math.pi / 2, 4, "X")}[axis]
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(centre)) @ rot, verts=bm.verts)
    o = new_obj(name, bm, material, parent)
    if bev:
        bevel(o, bev, bsegs, angle=50)
    return o


def hull_pts(name, pts, material, bev=0.0, segs=2, parent=None):
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1), verts=bm.verts, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = new_obj(name, bm, material, parent)
    if bev:
        bevel(o, bev, segs)
    return o


def belt_loop(name, profile, x0, x1, thick, material, parent=None, inner=None):
    """A closed band around a (y, z) outline, extruded from x0 to x1: outer = profile, inner = the
    outline inset by `thick` (or an explicit polygon with the same vertex count: a frame plate)."""
    bm = bmesh.new()
    n = len(profile)
    if inner is None:
        inner = []
        for i in range(n):
            p0, p1, p2 = (Vector(profile[(i - 1) % n]), Vector(profile[i]), Vector(profile[(i + 1) % n]))
            d1 = (p1 - p0).normalized()
            d2 = (p2 - p1).normalized()
            n1 = Vector((d1.y, -d1.x))
            n2 = Vector((d2.y, -d2.x))
            nn = (n1 + n2).normalized()
            k = thick / max(0.3, nn.dot(n1))
            inner.append(p1 - nn * k)
    # polygon winding: make sure "inner" is inside (outline is counter-clockwise in (y, z))
    outer_v = [[bm.verts.new((x, y, z)) for (y, z) in profile] for x in (x0, x1)]
    inner_v = [[bm.verts.new((x, y, z)) for (y, z) in inner] for x in (x0, x1)]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([outer_v[0][i], outer_v[0][j], outer_v[1][j], outer_v[1][i]])   # outer surface
        bm.faces.new([inner_v[0][j], inner_v[0][i], inner_v[1][i], inner_v[1][j]])   # inner surface
        bm.faces.new([outer_v[0][j], outer_v[0][i], inner_v[0][i], inner_v[0][j]])   # side x0
        bm.faces.new([outer_v[1][i], outer_v[1][j], inner_v[1][j], inner_v[1][i]])   # side x1
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return new_obj(name, bm, material, parent)


def along_profile(profile, step):
    """Points (y, z, tangent) spaced `step` along a closed (y, z) outline."""
    out = []
    n = len(profile)
    carry = 0.0
    for i in range(n):
        a, b = Vector(profile[i]), Vector(profile[(i + 1) % n])
        seg = b - a
        ln = seg.length
        t = seg / ln
        d = carry
        while d < ln:
            p = a + t * d
            out.append((p.x, p.y, t))
            d += step
        carry = d - ln
    return out


def apply_all(o):
    if o.type != "MESH" or not o.modifiers:
        return
    bpy.context.view_layer.objects.active = o
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def join(objs, name, parent=None):
    objs = [o for o in objs if o]
    for o in objs:
        apply_all(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    j = bpy.context.view_layer.objects.active
    j.name = name
    j.data.name = name
    if parent:
        j.parent = parent
    return j


def mirror_copy(o):
    c = o.copy()
    c.data = o.data.copy()
    c.name = o.name + "_L"
    bpy.context.scene.collection.objects.link(c)
    c.parent = o.parent
    c.data.transform(Matrix.Scale(-1, 4, (1, 0, 0)))
    c.data.flip_normals()
    return c


def set_origin(o, point):
    bpy.context.view_layer.update()
    mw = o.matrix_world.copy()
    local = mw.inverted() @ Vector(point)
    o.data.transform(Matrix.Translation(-local))
    o.matrix_world = mw @ Matrix.Translation(local)


def smooth_by_angle(o, deg=35):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(deg))


# ------------------------------------------------------------------------------------------------
# The tank


def inset_poly(poly, d):
    """Inset a convex (y, z) polygon by d (vertex normals of a counter-clockwise outline)."""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = (Vector(poly[(i - 1) % n]), Vector(poly[i]), Vector(poly[(i + 1) % n]))
        d1 = (p1 - p0).normalized()
        d2 = (p2 - p1).normalized()
        n1 = Vector((d1.y, -d1.x))
        n2 = Vector((d2.y, -d2.x))
        nn = (n1 + n2).normalized()
        out.append(tuple(p1 - nn * (d / max(0.3, nn.dot(n1)))))
    return out


def track_pod(parts):
    """Right pod (x > 0), round 4 = round 2's OPEN track (the owner's pick) with round 3's thinner
    band: a tall squared armoured pod with chamfered noses; the track belt runs round the whole pod
    perimeter on the outer part of the pod (a dark rim with fine rounded links round the pale
    riveted side plate, exposed on the noses, below and on the pod tops beyond the roof cover); a dark
    pod core inboard of the belt (the inner walls seen between the pod legs); a pale roof cover with
    the house-colour caps at both ends. No road wheels."""
    r, f = P["rear"], P["front"]
    xi, xo = P["pod_in"], P["pod_out"]
    zt, tb = P["belt_top"], P["belt_thick"]
    zn0, zn1 = P["nose_z"]
    cb_r, cb_f = P["chamfer_bot"]
    ct_r, ct_f = P["chamfer_top"]
    # the pod outline (y, z), counter-clockwise seen from +x: a squared block with chamfered noses
    prof = [(r + cb_r, 0.0), (f - cb_f, 0.0), (f, zn0), (f, zn1), (f - ct_f, zt), (r + ct_r, zt), (r, zn1), (r, zn0)]
    bx0, bx1 = P["belt_x"]
    belt = belt_loop("belt", prof, bx0, bx1, tb, "rubber")
    parts.append(belt)
    # fine rounded tread links all round (skipped under the roof cover)
    ry0, ry1 = P["roof_y"]
    for (y, z, t) in along_profile(prof, P["link_step"]):
        nrm = Vector((t.y, -t.x))
        if nrm.y > 0.9 and ry0 + 0.02 < y < ry1 - 0.02:
            continue
        ang = math.atan2(nrm.y, nrm.x)
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co.x *= (bx1 - bx0) * 0.9
            v.co.y *= P["link_len"]
            v.co.z *= P["link_h"]
        rot = Matrix.Rotation(ang - math.pi / 2, 4, "X")
        bmesh.ops.transform(bm, matrix=Matrix.Translation(((bx0 + bx1) / 2, y + nrm.x * 0.003, z + nrm.y * 0.003)) @ rot,
                            verts=bm.verts)
        parts.append(bevel(new_obj("link", bm, "steel_dark"), P["link_h"] * 0.4, 1, angle=50))
    # dark pod core inboard of the belt (its inner face is the wall seen between the pod legs) and
    # the core inside the belt loop
    parts.append(prism("podcore_in", inset_poly(prof, 0.004), xi, bx0 + 0.002, "steel_dark"))
    parts.append(prism("podcore", inset_poly(prof, tb + 0.001), bx0, bx1 - 0.03, "steel_dark"))
    # the pale side plate set inside the belt rim, leaning out a little going up
    plate = inset_poly(prof, tb + P["plate_inset"])
    px0, px1 = xo - 0.05, xo - P["plate_recess"]
    lean = P["plate_lean"]
    pts = [(px0, y, z) for (y, z) in plate] + [(px1 - lean * (1 - z / zt), y, z) for (y, z) in plate]
    parts.append(hull_pts("sideplate", pts, "turret_mid", bev=0.007, segs=2))

    def face_x(z):
        return px1 - lean * (1 - z / zt)
    rim = inset_poly(prof, tb + P["plate_inset"] + 0.022)
    for (y, z, t) in along_profile(rim, 0.04):
        parts.append(cyl("rivet", 0.006, 0.007, (face_x(z) + 0.002, y, z), "X", "steel", segs=8))
    for y in (-0.13, 0.085):
        parts.append(hull_pts("seam", [(face_x(z) + dx, y + dy, z) for z in (0.055, zt - tb - 0.03) for dy in (-0.003, 0.003)
                                       for dx in (-0.003, 0.0008)], "grille"))
    parts.append(cyl("porthole", 0.028, 0.006, (face_x(0.11) + 0.001, -0.02, 0.11), "X", "steel", segs=16, bev=0.002))
    parts.append(cyl("porthole2", 0.019, 0.004, (face_x(0.11) + 0.006, -0.02, 0.11), "X", "turret_mid", segs=14))
    # roof cover over the top run: a pale plate with a chamfered outer edge and the house caps
    zr0, zr1 = zt - 0.004, P["roof_top"]
    xc = P["roof_chamfer_x"]
    roof = hull_pts("roof", [
        (xi, ry0, zr0), (xo - 0.004, ry0, zr0), (xi, ry1, zr0), (xo - 0.004, ry1, zr0),
        (xi, ry0, zr1), (xc, ry0, zr1), (xi, ry1, zr1), (xc, ry1, zr1),
        (xo - 0.004, ry0, zr1 - 0.014), (xo - 0.004, ry1, zr1 - 0.014)],
        "cream", bev=0.008, segs=2)
    parts.append(roof)
    parts.append(box("roofgroove", xc - 0.009, xc - 0.002, ry0 + 0.09, ry1 - 0.09, zr1 - 0.008, zr1 + 0.0005, "grille"))
    band, ch = P["band"], P["cap_height"]
    for (ya, yb, sgn) in ((ry1 - band, ry1, 1), (ry0, ry0 + band, -1)):
        ye = yb if sgn > 0 else ya            # the roof end: the cap's end face slants down over it
        pts = []
        for x in (xi + 0.003, xc - 0.004):
            pts += [(x, ya, zr0 + 0.008), (x, yb, zr0 + 0.008), (x, ya, zr1 + ch), (x, yb - sgn * 0.02 if sgn > 0 else yb, zr1 + ch),
                    (x, ye + sgn * 0.035, zt - 0.012), (x, ye + sgn * 0.035, zt + 0.006)]
            if sgn < 0:
                pts[-4] = (x, ya + 0.02, zr1 + ch)
        parts.append(hull_pts("cap", pts, "HOUSE", bev=0.004, segs=1))
    # roof details: bolts along the inner edge, a stowage box, a lifting eye, a tow hook on the nose
    for i, y in enumerate((-0.12, -0.02, 0.08)):
        parts.append(cyl(f"bolt{i}", 0.009, 0.006, (xi + 0.02, y, zr1 + 0.003), "Z", "steel", segs=8))
    parts.append(box("stow", xi + 0.03, xc - 0.03, ry0 + band + 0.015, ry0 + band + 0.105, zr1, zr1 + 0.022,
                     "steel_dark", bev=0.005, segs=1))
    parts.append(box("strap", xi + 0.03, xc - 0.03, ry0 + band + 0.052, ry0 + band + 0.068, zr1 + 0.022, zr1 + 0.026, "rubber"))
    parts.append(box("eye", xc - 0.03, xc - 0.012, 0.0, 0.024, zr1, zr1 + 0.013, "steel", bev=0.003, segs=1))
    parts.append(cyl("hook", 0.012, 0.03, ((xi + bx0) / 2, f - 0.02, (zn0 + zn1) / 2), "Y", "steel", segs=10))


def hull(parts):
    w = P["hull_w"]
    yr, yf, zt, zb = P["hull_rear"], P["hull_front"], P["hull_top"], P["hull_bot"]
    gy, nz = P["glacis_y"], P["hull_nose_z"]
    # main body: rear box with a rounded rear-top edge, the glacis as a separate wedge
    body = prism("hullbody", [(yr, zb), (gy + 0.02, zb), (gy + 0.02, zt), (yr + 0.06, zt), (yr, zt - 0.05)],
                 -w, w, "steel_dark", bev=0.02, segs=3)
    parts.append(body)
    glacis = prism("glacis", [(gy - 0.02, zb), (yf - 0.03, zb), (yf, nz), (yf - 0.005, nz + 0.02), (gy, zt)],
                   -w + 0.001, w - 0.001, "steel_dark", bev=0.012, segs=2)
    parts.append(glacis)
    # the dark recessed centre section the turret sits on, and the turret ring collar
    px, py = P["pivot"]
    parts.append(box("socket", -w + 0.004, w - 0.004, py - 0.10, py + 0.10, zt - 0.03, zt - 0.004, "rubber"))   # black square recess in the deck
    parts.append(cyl("ring", P["ring_r"], P["ring_top"] - zt + 0.002, (px, py, (P["ring_top"] + zt) / 2), "Z",
                     "steel_dark", segs=28))
    # rear deck: engine grille (recessed), two exhausts, a hatch on the glacis, headlights
    parts.append(box("grille", -w + 0.02, w - 0.02, yr + 0.07, yr + 0.17, zt - 0.004, zt + 0.004, "grille"))
    for i in range(5):
        y = yr + 0.085 + i * 0.019
        parts.append(box(f"slat{i}", -w + 0.022, w - 0.022, y, y + 0.008, zt + 0.004, zt + 0.011, "steel"))
    for sx in (-1, 1):
        parts.append(cyl("exhaust", 0.014, 0.05, (sx * (w - 0.03), yr - 0.012, zt - 0.045), "Y", "steel_dark", segs=10))
        parts.append(cyl("lamp", 0.012, 0.012, (sx * (w - 0.03), yf - 0.035, nz + 0.07), "Y", "glass", segs=10))
    parts.append(cyl("dhatch", 0.03, 0.01, (0.0, gy + 0.085, zt - 0.04 + 0.03), "Z", "turret_mid", segs=14, bev=0.003))
    parts.append(box("periscope", -0.016, 0.016, gy + 0.03, gy + 0.05, zt - 0.015, zt + 0.014, "glass", bev=0.003, segs=1))


def turret(root):
    px, py = P["pivot"]
    k = P["tur_scale"]
    yr, yf = P["tur_rear"] * k, P["tur_front"] * k
    hw_r, hw_f = P["tur_wr"] / 2 * k, P["tur_wf"] / 2 * k
    zb = P["tur_bot"]
    zt = zb + (P["tur_top"] - zb) * k
    plan = [(-hw_r + 0.04 * k, yr), (hw_r - 0.04 * k, yr), (hw_r, yr + 0.05 * k), (hw_r, 0.04 * k),
            (hw_f, yf), (-hw_f, yf), (-hw_r, 0.04 * k), (-hw_r, yr + 0.05 * k)]
    tp = []
    zs = zb + (zt - zb) * P["tur_dark_band"]
    ks = P["tur_side_slope"]
    kk = ks + (1 - ks) * P["tur_dark_band"]      # taper at the split height
    cx_ = sum(x for x, _ in plan) / len(plan)
    cy_ = sum(y for _, y in plan) / len(plan)
    # the base: a flared ring of angled plates (the sprite's diagonal facet edges), over a dark
    # ring; above it the armour slopes in to a roof narrower than the base
    rs, fl = P["tur_roof_scale"], P["tur_base_flare"]
    def scaled(k_):
        return [(cx_ + (x - cx_) * k_, cy_ + (y - cy_) * k_) for (x, y) in plan]
    # base plates (flared facets) and the lower turret in mid grey, the roof plates pale; the roof
    # carries two dark diagonal seams running from the rear corners to the mantlet (the "A" of the
    # original turret frames) with a raised central plate between them
    base = plan_prism("turretbase", scaled(0.94), zb, zs + 0.004, "turret_mid", bev=0.006, segs=1, bottom_scale=fl / 0.94)
    zm = zs + (zt - zs) * P["tur_dark_height"]
    km = rs + (0.94 - rs) * (1 - P["tur_dark_height"])     # plan scale at the mid / pale split
    lower = plan_prism("turretlower", scaled(km), zs, zm + 0.002, "turret_mid", bev=0.008, segs=1, bottom_scale=0.94 / km)
    body = plan_prism("turretbody", scaled(rs), zm, zt, "turret_mid", bev=0.01, segs=2, bottom_scale=km / rs)
    tp += [base, lower, body]
    gw = P["roof_groove_w"]
    rr_y, rf_y = yr * rs + 0.02, yf * rs - 0.012          # roof rear / front
    rw_r = hw_r * rs - 0.016                              # roof half width at the rear
    apex = 0.055 * k                                      # half width of the plate at the mantlet
    # central pale plate (raised), its edges are the diagonals
    tp.append(hull_pts("roofplate", [(-rw_r + gw, rr_y, zt - 0.002), (rw_r - gw, rr_y, zt - 0.002),
                                     (-apex, rf_y, zt - 0.002), (apex, rf_y, zt - 0.002),
                                     (-rw_r + gw + 0.004, rr_y + 0.004, zt + 0.014), (rw_r - gw - 0.004, rr_y + 0.004, zt + 0.014),
                                     (-apex + 0.004, rf_y - 0.004, zt + 0.014), (apex - 0.004, rf_y - 0.004, zt + 0.014)],
                       "turret_cream", bev=0.004, segs=1))
    # dark seams along both diagonals, slightly proud so they read from above
    for sx in (-1, 1):
        a_ = Vector((sx * (rw_r - gw / 2), rr_y))
        b_ = Vector((sx * (apex + gw / 2), rf_y))
        d_ = (b_ - a_).normalized()
        n_ = Vector((-d_.y, d_.x)) * (gw / 2)
        pts = []
        for q in (a_ - d_ * 0.004, b_ + d_ * 0.004):
            for sgn in (-1, 1):
                pts += [(q.x + sgn * n_.x, q.y + sgn * n_.y, zt - 0.002), (q.x + sgn * n_.x, q.y + sgn * n_.y, zt + 0.017)]
        tp.append(hull_pts("roofseam", pts, "rubber"))
    # outer roof plates slope down to the sides (pale, slightly darker than the centre plate)
    for sx in (-1, 1):
        tp.append(hull_pts("roofside", [(sx * (rw_r + 0.012), rr_y - 0.004, zt - 0.006), (sx * (rw_r - gw / 2), rr_y - 0.004, zt + 0.012),
                                        (sx * (apex + gw / 2), rf_y + 0.004, zt + 0.012), (sx * (apex + 0.03), rf_y + 0.004, zt - 0.006),
                                        (sx * (rw_r + 0.012), rr_y - 0.004, zt - 0.012), (sx * (apex + 0.03), rf_y + 0.004, zt - 0.012)],
                           "turret_mid", bev=0.003, segs=1))
    zt = zt + 0.014   # details sit on the raised centre plate
    # roof hump carrying the house stripe (top + rear face), flanked by the body colour
    y0, y1, hwid, hh = P["hump"]
    y0, y1 = y0 * k * rs, y1 * k * rs
    tp.append(hull_pts("hump", [(-hwid, y0, zt - 0.004), (hwid, y0, zt - 0.004), (-hwid, y1, zt - 0.004), (hwid, y1, zt - 0.004),
                                (-hwid + 0.01, y0 + 0.01, zt + hh), (hwid - 0.01, y0 + 0.01, zt + hh),
                                (-hwid + 0.01, y1 - 0.006, zt + hh), (hwid - 0.01, y1 - 0.006, zt + hh)],
                       "HOUSE", bev=0.005, segs=1))
    # commander's hatch (left rear), periscope block, antenna stub, smoke dischargers (right), bustle rack
    tp.append(cyl("chatch", 0.038 * k, 0.01, (-0.085 * k, -0.11 * k, zt + 0.003), "Z", "turret_cream", segs=16, bev=0.003))
    tp.append(cyl("chatch2", 0.026 * k, 0.007, (-0.085 * k, -0.11 * k, zt + 0.012), "Z", "turret_cream", segs=12))
    tp.append(box("vision", -0.11 * k, -0.07 * k, -0.03 * k, 0.0, zt - 0.004, zt + 0.018, "turret_cream", bev=0.003, segs=1))
    tp.append(box("visionglass", -0.104 * k, -0.076 * k, -0.002, 0.004, zt + 0.003, zt + 0.013, "glass"))
    tp.append(cyl("antbase", 0.01, 0.026, (0.115 * k, -0.19 * k, zt + 0.008), "Z", "steel_dark", segs=8))
    tp.append(cyl("antenna", 0.003, 0.06, (0.115 * k, -0.19 * k, zt + 0.05), "Z", "steel_dark", segs=6))
    for i in range(3):
        tp.append(cyl(f"smoke{i}", 0.009, 0.034, ((0.105 + i * 0.011) * k, (0.09 - i * 0.028) * k, zt - 0.018), "Y", "steel_dark", segs=8))
    tp.append(box("bustle", -0.12 * k, 0.12 * k, yr - 0.04 * k, yr + 0.01, zb + 0.022, zt - 0.014, "steel_dark", bev=0.005, segs=1))
    for i in range(4):
        x = (-0.095 + i * 0.062) * k
        tp.append(box(f"rack{i}", x, x + 0.028 * k, yr - 0.045 * k, yr - 0.028 * k, zb + 0.027, zt - 0.019, "steel"))
    # mantlet (compact rounded armour around the gun base)
    gz = P["gun_z"]
    mw, md = 0.065 * k, 0.035 * k
    tp.append(hull_pts("mantlet", [(-mw, yf - 0.02, gz - 0.042), (mw, yf - 0.02, gz - 0.042), (-mw, yf - 0.02, gz + 0.04),
                                   (mw, yf - 0.02, gz + 0.04), (-mw + 0.012, yf + md, gz - 0.034), (mw - 0.012, yf + md, gz - 0.034),
                                   (-mw + 0.012, yf + md, gz + 0.032), (mw - 0.012, yf + md, gz + 0.032)],
                       "turret_mid", bev=0.01, segs=2))
    tur = join(tp, "turret")
    set_origin(tur, (px, py, zb))
    tur.parent = root
    # the gun: long slim barrel with a thermal sleeve, bore evacuator, two collars and a muzzle brake
    gr = P["gun_r"]
    g0 = yf + md - 0.01
    gl = P["gun_muzzle"] - g0
    gp = []
    gp.append(cyl("barrel", gr, gl, (0, g0 + gl / 2, gz), "Y", "gun", segs=14))
    gp.append(cyl("sleeve", gr + 0.008, gl * 0.30, (0, g0 + gl * 0.17, gz), "Y", "turret_cream", segs=14, bev=0.003))
    gp.append(cyl("evac", gr + 0.006, gl * 0.12, (0, g0 + gl * 0.50, gz), "Y", "gun", segs=14, bev=0.002))
    for i, yq in enumerate((0.66, 0.75)):
        gp.append(cyl(f"collar{i}", gr + 0.004, 0.012, (0, g0 + gl * yq, gz), "Y", "steel_dark", segs=14))
    gp.append(cyl("brake", gr + 0.008, 0.05, (0, g0 + gl - 0.027, gz), "Y", "gun", segs=10, bev=0.003))
    gp.append(cyl("bore", gr * 0.6, 0.006, (0, g0 + gl + 0.001, gz), "Y", "rubber", segs=12))
    for sx in (-1, 1):
        gp.append(box("slot", sx * (gr + 0.003), sx * (gr + 0.016), g0 + gl - 0.046, g0 + gl - 0.012, gz - 0.009, gz + 0.009, "rubber"))
    gun = join(gp, "barrel")
    set_origin(gun, (0, g0, gz))
    gun.parent = tur
    bpy.context.view_layer.update()
    gun.matrix_parent_inverse = tur.matrix_world.inverted()
    return tur


def build():
    root = bpy.data.objects.new("ATREIDES_COMBAT_TANK", None)
    bpy.context.scene.collection.objects.link(root)
    parts = []
    track_pod(parts)
    # slightly slanted pod sides: narrow the pod towards the ground about its inner edge
    xi, k, zt = P["pod_in"], P["pod_taper"], P["belt_top"]
    for o in parts:
        for v in o.data.vertices:
            f = 1.0 - k * max(0.0, 1.0 - v.co.z / zt)
            v.co.x = xi + (v.co.x - xi) * f
    right = list(parts)
    for o in right:
        parts.append(mirror_copy(o))
    hull(parts)
    body = join(parts, "body")
    body.parent = root
    tur = turret(root)
    for o in (body, tur, tur.children[0]):
        smooth_by_angle(o, 35)
    root.location = (P["x_offset"], 0.0, 0.0)
    bpy.context.view_layer.update()
    return root


def meshes_under(root):
    out = []
    st = [root]
    while st:
        o = st.pop()
        if o.type == "MESH":
            out.append(o)
        st.extend(o.children)
    return out


def tri_count(objs):
    n = 0
    for o in objs:
        o.data.calc_loop_triangles()
        n += len(o.data.loop_triangles)
    return n


# ------------------------------------------------------------------------------------------------
# Procedural surface -> baked atlas


def surface_tree(nt, base_srgb, metal, rough, kind):
    """Nodes giving (Color, Roughness, Metallic) sockets for a part kind: paint with worn edges and
    dust, rubber, or steel. Returns the three output sockets."""
    nodes, links = nt.nodes, nt.links
    base = nodes.new("ShaderNodeRGB")
    base.outputs[0].default_value = (*srgb_to_linear(base_srgb), 1)
    geo = nodes.new("ShaderNodeNewGeometry")
    texco = nodes.new("ShaderNodeTexCoord")
    # fine surface variation
    n1 = nodes.new("ShaderNodeTexNoise")
    n1.inputs["Scale"].default_value = 60.0
    n1.inputs["Detail"].default_value = 4.0
    n1.inputs["Roughness"].default_value = 0.6
    links.new(texco.outputs["Object"], n1.inputs["Vector"])
    v1 = nodes.new("ShaderNodeMapRange")
    v1.inputs["From Min"].default_value = 0.35
    v1.inputs["From Max"].default_value = 0.65
    v1.inputs["To Min"].default_value = 0.955 if kind == "paint" else 0.85
    v1.inputs["To Max"].default_value = 1.035 if kind == "paint" else 1.12
    links.new(n1.outputs["Fac"], v1.inputs["Value"])
    col = nodes.new("ShaderNodeMix")
    col.data_type = "RGBA"
    col.blend_type = "MULTIPLY"
    col.inputs["Factor"].default_value = 1.0
    links.new(base.outputs[0], col.inputs[6])
    links.new(v1.outputs[0], col.inputs[7])
    cur = col.outputs[2]
    # cavity grime: ambient occlusion darkens crevices
    ao = nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.05
    ao.samples = 8
    ao.only_local = True
    aom = nodes.new("ShaderNodeMapRange")
    aom.inputs["From Min"].default_value = 0.3
    aom.inputs["From Max"].default_value = 1.0
    aom.inputs["To Min"].default_value = 0.84
    aom.inputs["To Max"].default_value = 1.0
    links.new(ao.outputs["AO"], aom.inputs["Value"])
    g = nodes.new("ShaderNodeMix")
    g.data_type = "RGBA"
    g.blend_type = "MULTIPLY"
    g.inputs["Factor"].default_value = 1.0
    links.new(cur, g.inputs[6])
    links.new(aom.outputs[0], g.inputs[7])
    cur = g.outputs[2]
    rough_out = nodes.new("ShaderNodeValue")
    rough_out.outputs[0].default_value = rough
    metal_out = nodes.new("ShaderNodeValue")
    metal_out.outputs[0].default_value = metal
    rough_sock, metal_sock = rough_out.outputs[0], metal_out.outputs[0]
    if kind == "paint":
        # worn edges: bevel normal vs true normal, patchy by noise
        bev = nodes.new("ShaderNodeBevel")
        bev.samples = 8
        bev.inputs["Radius"].default_value = 0.02
        dot = nodes.new("ShaderNodeVectorMath")
        dot.operation = "DOT_PRODUCT"
        links.new(bev.outputs["Normal"], dot.inputs[0])
        links.new(geo.outputs["Normal"], dot.inputs[1])
        edge = nodes.new("ShaderNodeMapRange")
        edge.inputs["From Min"].default_value = 0.99
        edge.inputs["From Max"].default_value = 0.78
        edge.inputs["To Min"].default_value = 0.0
        edge.inputs["To Max"].default_value = 1.0
        links.new(dot.outputs["Value"], edge.inputs["Value"])
        n2 = nodes.new("ShaderNodeTexNoise")
        n2.inputs["Scale"].default_value = 35.0
        n2.inputs["Detail"].default_value = 3.0
        links.new(texco.outputs["Object"], n2.inputs["Vector"])
        patch = nodes.new("ShaderNodeMapRange")
        patch.inputs["From Min"].default_value = 0.36
        patch.inputs["From Max"].default_value = 0.60
        links.new(n2.outputs["Fac"], patch.inputs["Value"])
        wearf = nodes.new("ShaderNodeMath")
        wearf.operation = "MULTIPLY"
        links.new(edge.outputs[0], wearf.inputs[0])
        links.new(patch.outputs[0], wearf.inputs[1])
        wearc = nodes.new("ShaderNodeRGB")
        wearc.outputs[0].default_value = (*srgb_to_linear(COL["wear"]), 1)
        mixw = nodes.new("ShaderNodeMix")
        mixw.data_type = "RGBA"
        links.new(wearf.outputs[0], mixw.inputs["Factor"])
        links.new(cur, mixw.inputs[6])
        links.new(wearc.outputs[0], mixw.inputs[7])
        cur = mixw.outputs[2]
        # dust towards the ground (object z), warm sand tint
        sep = nodes.new("ShaderNodeSeparateXYZ")
        links.new(texco.outputs["Object"], sep.inputs[0])
        dustf = nodes.new("ShaderNodeMapRange")
        dustf.inputs["From Min"].default_value = 0.02
        dustf.inputs["From Max"].default_value = 0.22
        dustf.inputs["To Min"].default_value = 0.28
        dustf.inputs["To Max"].default_value = 0.0
        links.new(sep.outputs["Z"], dustf.inputs["Value"])
        n3 = nodes.new("ShaderNodeTexNoise")
        n3.inputs["Scale"].default_value = 14.0
        links.new(texco.outputs["Object"], n3.inputs["Vector"])
        dmul = nodes.new("ShaderNodeMath")
        dmul.operation = "MULTIPLY"
        links.new(dustf.outputs[0], dmul.inputs[0])
        links.new(n3.outputs["Fac"], dmul.inputs[1])
        dustc = nodes.new("ShaderNodeRGB")
        dustc.outputs[0].default_value = (*srgb_to_linear((0.62, 0.50, 0.36)), 1)
        mixd = nodes.new("ShaderNodeMix")
        mixd.data_type = "RGBA"
        links.new(dmul.outputs[0], mixd.inputs["Factor"])
        links.new(cur, mixd.inputs[6])
        links.new(dustc.outputs[0], mixd.inputs[7])
        cur = mixd.outputs[2]
        # roughness: rough paint, smoother worn metal, rougher dust
        r1 = nodes.new("ShaderNodeMix")
        r1.data_type = "FLOAT"
        links.new(wearf.outputs[0], r1.inputs["Factor"])
        r1.inputs[2].default_value = rough
        r1.inputs[3].default_value = 0.38
        r2 = nodes.new("ShaderNodeMix")
        r2.data_type = "FLOAT"
        links.new(dmul.outputs[0], r2.inputs["Factor"])
        links.new(r1.outputs[0], r2.inputs[2])
        r2.inputs[3].default_value = 0.85
        rough_sock = r2.outputs[0]
        m1 = nodes.new("ShaderNodeMix")
        m1.data_type = "FLOAT"
        links.new(wearf.outputs[0], m1.inputs["Factor"])
        m1.inputs[2].default_value = metal
        m1.inputs[3].default_value = 0.9
        metal_sock = m1.outputs[0]
    elif kind == "steel":
        # brushed look: streaks along y
        n4 = nodes.new("ShaderNodeTexNoise")
        n4.inputs["Scale"].default_value = 30.0
        n4.inputs["Distortion"].default_value = 0.4
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = (10.0, 0.5, 10.0)
        links.new(texco.outputs["Object"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n4.inputs["Vector"])
        rr = nodes.new("ShaderNodeMapRange")
        rr.inputs["From Min"].default_value = 0.3
        rr.inputs["From Max"].default_value = 0.7
        rr.inputs["To Min"].default_value = rough - 0.12
        rr.inputs["To Max"].default_value = rough + 0.12
        links.new(n4.outputs["Fac"], rr.inputs["Value"])
        rough_sock = rr.outputs[0]
    return cur, rough_sock, metal_sock


KIND = dict(cream="paint", cream_hull="paint", turret_cream="paint", turret_mid="steel", wear="steel", rubber="rubber", steel_dark="steel", steel="steel",
            gun="steel", glass="rubber", grille="rubber")


def procedural_materials():
    """Replace the plain materials (except HOUSE) by procedural ones with an Emission bake switch:
    material["d2k_sockets"] = (colour, roughness, metallic) sockets, output -> Principled."""
    out = {}
    for name in list(_mats):
        if name == "HOUSE":
            continue
        m = _mats[name]
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        metal, rough = b.inputs["Metallic"].default_value, b.inputs["Roughness"].default_value
        c, r, mt = surface_tree(nt, COL[name], metal, rough, KIND[name])
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        out[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
    return out


def uv_unwrap(objs, margin=0.004):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=margin, scale_to_bounds=False)
    try:
        bpy.ops.uv.pack_islands(margin=margin, rotate=True, scale=True)
    except TypeError:
        bpy.ops.uv.pack_islands(margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


# Emissive materials (docs/3d-models.md "Emission"): material name -> linear RGB emission (0..1; the brightest
# channel = how strongly it glows). The kits add theirs (atreides_kit: lamps, cyan louvres, the refinery's arched
# windows...). With any of them in a model, bake_atlas also bakes an "emit" image and export_material links it
# as the glTF emissiveTexture: the game draws those texels glowing at night (3D) and render_pack / the check
# pack drivers render them into the 2D frames' .glow.png. The other images are baked first and stay identical.
EMIT = {}


def bake_emit(objs, procs, size, samples):
    """The emission atlas (only when a baked material is in EMIT; None otherwise)."""
    if not any(name in EMIT for name in procs):
        return None
    img = bpy.data.images.new("tank_emit", size, size, alpha=False, float_buffer=False)
    img.colorspace_settings.name = "sRGB"
    img.generated_color = (0, 0, 0, 1)
    for name, pr in procs.items():
        nt = pr["mat"].node_tree
        out = next(n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        em = pr["emit"]
        for l in list(out.inputs["Surface"].links):
            nt.links.remove(l)
        for l in list(em.inputs["Color"].links):
            nt.links.remove(l)
        em.inputs["Color"].default_value = (*EMIT.get(name, (0.0, 0.0, 0.0)), 1.0)
        nt.links.new(em.outputs[0], out.inputs["Surface"])
        tex = nt.nodes.get("BAKE_TARGET")
        tex.image = img
        nt.nodes.active = tex
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    t0 = time.time()
    bpy.ops.object.bake(type="EMIT", margin=6, use_clear=True)
    log(f"  baked emit {size}x{size} in {time.time() - t0:.1f} s")
    return img


def link_emission(nt, b, images, scratch):
    """Export material: the baked emission atlas -> Principled Emission (glTF emissiveTexture, factor 1)."""
    img = images.get("emit")
    if img is None:
        return
    img.filepath_raw = os.path.join(scratch, "tank_emit.png")
    img.file_format = "PNG"
    img.save()
    te = nt.nodes.new("ShaderNodeTexImage")
    te.image = img
    nt.links.new(te.outputs["Color"], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = 1.0


def bake_atlas(objs, procs, size, samples):
    """Bake colour / roughness / metallic of every non-HOUSE material into images (EMIT passes)
    (+ "emit", the emission atlas, when a material is in EMIT)."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    setup_gpu(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 6
    scene.render.bake.use_clear = True
    scene.render.bake.target = "IMAGE_TEXTURES"
    images = {}
    for pname, idx in (("color", 0), ("rough", 1), ("metal", 2)):
        img = bpy.data.images.new(f"tank_{pname}", size, size, alpha=False, float_buffer=False)
        img.colorspace_settings.name = "sRGB" if pname == "color" else "Non-Color"
        img.generated_color = (0, 0, 0, 1)
        images[pname] = img
        for name, pr in procs.items():
            nt = pr["mat"].node_tree
            out = next(n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
            em = pr["emit"]
            for l in list(out.inputs["Surface"].links):
                nt.links.remove(l)
            for l in list(em.inputs["Color"].links):
                nt.links.remove(l)
            nt.links.new(pr["sockets"][idx], em.inputs["Color"])
            nt.links.new(em.outputs[0], out.inputs["Surface"])
            tex = nt.nodes.get("BAKE_TARGET")
            if tex is None:
                tex = nt.nodes.new("ShaderNodeTexImage")
                tex.name = "BAKE_TARGET"
            tex.image = img
            nt.nodes.active = tex
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        t0 = time.time()
        bpy.ops.object.bake(type="EMIT", margin=6, use_clear=True)
        log(f"  baked {pname} {size}x{size} in {time.time() - t0:.1f} s")
    emit = bake_emit(objs, procs, size, samples)
    if emit is not None:
        images["emit"] = emit
    # restore the Principled output (for previews)
    for name, pr in procs.items():
        nt = pr["mat"].node_tree
        out = next(n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        b = nt.nodes["Principled BSDF"]
        for l in list(out.inputs["Surface"].links):
            nt.links.remove(l)
        nt.links.new(b.outputs[0], out.inputs["Surface"])
    return images


def pack_mr(images, size):
    """glTF metallicRoughness: G = roughness, B = metallic (R = 1, no occlusion)."""
    import numpy as np
    r = np.array(images["rough"].pixels[:], np.float32).reshape(size, size, 4)
    m = np.array(images["metal"].pixels[:], np.float32).reshape(size, size, 4)
    mr = bpy.data.images.new("tank_mr", size, size, alpha=False, float_buffer=False)
    mr.colorspace_settings.name = "Non-Color"
    px = np.ones((size, size, 4), np.float32)
    px[..., 1] = r[..., 0]
    px[..., 2] = m[..., 0]
    mr.pixels.foreach_set(px.ravel())
    mr.update()
    return mr


def export_material(images, mr, scratch):
    """The single textured material of the export (base colour + packed metallic/roughness)."""
    m = bpy.data.materials.new("body")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    os.makedirs(scratch, exist_ok=True)
    for img, fn in ((images["color"], "tank_color.png"), (mr, "tank_mr.png")):
        img.filepath_raw = os.path.join(scratch, fn)
        img.file_format = "PNG"
        img.save()
    tc = nt.nodes.new("ShaderNodeTexImage")
    tc.image = images["color"]
    nt.links.new(tc.outputs["Color"], b.inputs["Base Color"])
    tm = nt.nodes.new("ShaderNodeTexImage")
    tm.image = mr
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(tm.outputs["Color"], sep.inputs[0])
    nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], b.inputs["Metallic"])
    link_emission(nt, b, images, scratch)
    return m


def setup_gpu(scene):
    cy = scene.cycles
    cy.device = "CPU"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for kind in ("METAL", "OPTIX", "CUDA", "HIP"):
            try:
                prefs.compute_device_type = kind
            except TypeError:
                continue
            prefs.get_devices()
            if any(d.type == kind for d in prefs.devices):
                for d in prefs.devices:
                    d.use = d.type == kind
                cy.device = "GPU"
                break
    except Exception as e:  # noqa: BLE001
        log("no GPU:", e)


# ------------------------------------------------------------------------------------------------
# Export


def export_glb(root, path, only=None):
    """Export the model (or, with `only`, that node and its children — the standalone turret)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    top = only if only is not None else root
    for o in [top] + list(top.children_recursive):
        o.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=True,
        export_normals=True, export_materials="EXPORT", export_image_format="AUTO", export_extras=False,
        export_tangents=False, export_texcoords=True, export_attributes=False)


# ------------------------------------------------------------------------------------------------
# Review: game-camera silhouettes (IoU vs the original) and a turntable


def game_camera(scene, elev, ppt, size):
    e = math.radians(elev)
    cam_data = bpy.data.cameras.new("CAM")
    cam_data.type = "ORTHO"
    cam_data.clip_start = 0.01
    cam_data.clip_end = 200
    cam_data.ortho_scale = size / ppt
    cam = bpy.data.objects.new("CAM", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.location = 50 * Vector((0, -math.cos(e), math.sin(e)))
    cam.rotation_euler = (math.pi / 2 - e, 0, 0)
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    return cam


def iou_report(root, npz_path, elevs, sheet_dir, scale=4):
    import numpy as np
    d = np.load(npz_path)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (1, 1, 1)
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    scene.display_settings.display_device = "sRGB"
    ppt = 32 * scale
    size = 48 * scale
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    root.parent = yaw
    tur = next(o for o in root.children if o.name == "turret")
    tm0 = tur.matrix_basis.copy()
    meshes = meshes_under(root)
    tur_meshes = set(meshes_under(tur))
    tmp = os.path.join(bpy.app.tempdir, "d2k_iou.png")
    results = {}
    for elev in elevs:
        cam = game_camera(scene, elev, ppt, size)
        masks = {}
        for part in ("body", "turret"):
            for o in meshes:
                o.hide_render = (o in tur_meshes) != (part == "turret")
            ms = []
            for f in range(32):
                a = -math.radians(f * 11.25)
                if part == "turret":
                    yaw.rotation_euler = (0, 0, 0)
                    tur.matrix_basis = Matrix.Translation(tm0.translation) @ Matrix.Rotation(a, 4, "Z") @ tm0.to_3x3().to_4x4()
                else:
                    yaw.rotation_euler = (0, 0, a)
                    tur.matrix_basis = tm0
                scene.render.filepath = tmp
                bpy.ops.render.render(write_still=True)
                img = bpy.data.images.load(tmp)
                px = np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1, :, 3]
                bpy.data.images.remove(img)
                lo = px.reshape(size // scale, scale, size // scale, scale).mean((1, 3)) > 0.5
                ms.append(lo)
            masks[part] = np.array(ms)
        tur.matrix_basis = tm0
        yaw.rotation_euler = (0, 0, 0)
        bpy.data.objects.remove(cam)
        # anchor auto: shift (whole px, vertical) from the body centroid difference, same for the turret
        n = size // scale
        c = n // 2
        ds = []
        for f in range(32):
            om = d["body_mask"][f]
            ax, ay = d["body_anchor"]
            ys, xs = np.nonzero(om)
            co = np.array([xs.mean() + 0.5 - ax, ys.mean() + 0.5 - ay])
            ys, xs = np.nonzero(masks["body"][f])
            cr = np.array([xs.mean() + 0.5 - c, ys.mean() + 0.5 - c])
            ds.append(cr - co)
        shift = int(round(np.mean(ds, 0)[1]))
        rep = {}
        for part in ("body", "turret"):
            ious = []
            ax, ay = d[part + "_anchor"]
            sheet = []
            for f in range(32):
                om = d[part + "_mask"][f]
                can = np.zeros((n + 128, n + 128), bool)
                can[64:64 + n, 64:64 + n] = masks[part][f]
                org = np.zeros_like(can)
                # the engine draws the render `shift` px higher (hotspot + shift); equivalently
                # the original sits `shift` px lower here
                ox, oy = int(64 + c - ax), int(64 + c + shift - ay)
                org[oy:oy + om.shape[0], ox:ox + om.shape[1]] = om
                ious.append((can & org).sum() / max(1, (can | org).sum()))
                if sheet_dir and f % 4 == 0:
                    cell = np.zeros((n, n, 3), np.uint8)
                    r_ = can[64:64 + n, 64:64 + n]
                    o_ = org[64:64 + n, 64:64 + n]
                    cell[r_ & o_] = (235, 235, 235)
                    cell[r_ & ~o_] = (220, 60, 60)    # render only
                    cell[~r_ & o_] = (60, 120, 230)   # original only
                    sheet.append(cell)
            rep[part] = ious
            if sheet_dir and sheet:
                os.makedirs(sheet_dir, exist_ok=True)
                row = np.concatenate(sheet, 1)
                row = np.repeat(np.repeat(row, 4, 0), 4, 1)
                img = bpy.data.images.new("sheet", row.shape[1], row.shape[0], alpha=False)
                px = np.ones((row.shape[0], row.shape[1], 4), np.float32)
                px[..., :3] = row[::-1].astype(np.float32) / 255.0
                img.pixels.foreach_set(px.ravel())
                img.filepath_raw = os.path.join(sheet_dir, f"iou_{part}_elev{int(elev)}.png")
                img.file_format = "PNG"
                img.save()
                bpy.data.images.remove(img)
        results[elev] = rep
        log(f"IOU elev {elev}: shift {shift} px  body mean {np.mean(rep['body']):.3f}  turret mean {np.mean(rep['turret']):.3f}  "
            f"combined {np.mean(rep['body'] + rep['turret']):.3f}")
        log("   body   per facing: " + " ".join(f"{v:.2f}" for v in rep["body"][::2]))
        log("   turret per facing: " + " ".join(f"{v:.2f}" for v in rep["turret"][::2]))
    for o in meshes:
        o.hide_render = False
    root.parent = None
    bpy.data.objects.remove(yaw)
    return results


def turntable(root, out_dir, samples, size=560, views=8, house=PREVIEW_HOUSE):
    import numpy as np  # noqa: F401
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    setup_gpu(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "AgX"
    try:
        scene.view_settings.look = "AgX - Punchy"
    except TypeError:
        pass
    for m in bpy.data.materials:
        if m.name.startswith("HOUSE") and m.use_nodes:
            b = m.node_tree.nodes.get("Principled BSDF")
            if b:
                b.inputs["Base Color"].default_value = (*srgb_to_linear(house), 1)
    # ground, sun, sky
    bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, -0.001))
    gp = bpy.context.active_object
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*srgb_to_linear((0.76, 0.62, 0.43)), 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1
    gp.data.materials.append(gm)
    w = bpy.data.worlds.new("w")
    scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.62, 0.75, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 4.0
    sun.angle = math.radians(3)
    so = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(so)
    az, el = math.radians(155), math.radians(58)
    to_sun = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
    so.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()
    cam = bpy.data.cameras.new("cam")
    cam.lens = 60
    co = bpy.data.objects.new("cam", cam)
    scene.collection.objects.link(co)
    scene.camera = co
    tgt = Vector((0, 0, 0.16))
    for i in range(views):
        a = math.radians(-90 - 360 * i / views)
        e = math.radians(32 if i % 2 == 0 else 24)
        pos = tgt + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * 2.4
        co.location = pos
        co.rotation_euler = (tgt - pos).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, f"turn_{i}.png")
        bpy.ops.render.render(write_still=True)
    # a close detail view
    pos = tgt + Vector((0.9, -1.3, 0.9))
    co.location = pos
    co.rotation_euler = (tgt - pos).to_track_quat("-Z", "Y").to_euler()
    cam.lens = 85
    scene.render.filepath = os.path.join(out_dir, "detail.png")
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(gp)
    bpy.data.objects.remove(so)
    bpy.data.objects.remove(co)


# ------------------------------------------------------------------------------------------------


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=1024, iou=None, elev=[45.0], iou_sheet=None, turntable=None, samples=64)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--iou":
            opt["iou"] = argv[i + 1]; i += 1
        elif a == "--elev":
            opt["elev"] = [float(x) for x in argv[i + 1].split(",")]; i += 1
        elif a == "--iou-sheet":
            opt["iou_sheet"] = argv[i + 1]; i += 1
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--turret-scale":
            P["tur_scale"] = float(argv[i + 1]); i += 1
        elif a == "--no-x-offset":
            P["x_offset"] = 0.0
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None and opt["iou"] is None and opt["turntable"] is None:
        here = os.path.dirname(os.path.abspath(__file__))
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(here)), "artpacks", "models-fable")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root = build()
    objs = meshes_under(root)
    log(f"built: {len(objs)} meshes, {tri_count(objs)} triangles in {time.time() - t0:.1f} s")
    if opt["iou"]:
        iou_report(root, opt["iou"], opt["elev"], opt["iou_sheet"])
    if opt["bake"] and (opt["out"] or opt["turntable"]):
        t1 = time.time()
        procs = procedural_materials()
        uv_unwrap(objs)
        scratch = os.path.join(bpy.app.tempdir, "d2k_tank_bake")
        images = bake_atlas(objs, procs, opt["bake_size"], 24)
        mr = pack_mr(images, opt["bake_size"])
        body_mat = export_material(images, mr, scratch)
        house = _mats["HOUSE"]
        for o in objs:
            names = [s.material.name if s.material else "" for s in o.material_slots]
            for s in o.material_slots:
                if s.material and not s.material.name.startswith("HOUSE"):
                    s.material = body_mat
            # merge duplicate slots: keep "body" + "HOUSE" only
            if len(o.material_slots) > 2 or names:
                me = o.data
                idx_map = {}
                keep = []
                for si, s in enumerate(me.materials):
                    key = s.name if s else ""
                    if key not in idx_map:
                        idx_map[key] = len(keep)
                        keep.append(s)
                new_index = {si: idx_map[(s.name if s else "")] for si, s in enumerate(me.materials)}
                old_idx = [p.material_index for p in me.polygons]
                me.materials.clear()          # resets the polygons' indices: remap afterwards
                for s in keep:
                    me.materials.append(s)
                for p, oi in zip(me.polygons, old_idx):
                    p.material_index = new_index[oi]
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']}px atlas)")
        _ = house
    if opt["out"]:
        unit_dir = os.path.join(opt["out"], "models", "unit")    # what --model-pack / render_pack --models expect
        path = os.path.join(unit_dir, "ATREIDES_COMBAT_TANK.glb")
        export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        # the turret alone (node "turret" at its body position: pivot (0, -0.03, tur_bot), x offset removed),
        # for merging onto another body
        tur = next(o for o in root.children if o.name == "turret")
        root.location = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()
        tpath = os.path.join(unit_dir, "ATREIDES_COMBAT_TANK_turret.glb")
        export_glb(root, tpath, only=tur)
        root.location = (P["x_offset"], 0.0, 0.0)
        log(f"exported {tpath} ({os.path.getsize(tpath) // 1024} KB)  (turret + barrel only, pivot at {tuple(round(v, 3) for v in tur.matrix_world.translation)})")
    if opt["turntable"]:
        t2 = time.time()
        turntable(root, opt["turntable"], opt["samples"])
        log(f"turntable in {time.time() - t2:.1f} s -> {opt['turntable']}")


if __name__ == "__main__":
    main()
