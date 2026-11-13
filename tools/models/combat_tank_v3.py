"""Atreides combat tank, v3 — the ORIGINAL design rebuilt as a modern hard-surface model.

    blender -b --factory-startup --python tools/models/combat_tank_v3.py -- \
        [--out DIR]            write DIR/models/unit/ATREIDES_COMBAT_TANK.glb (+ _turret.glb: turret + barrel alone)
                               (default artpacks/models-fable-v3; --model-pack / render_pack --models take DIR)
        [--no-bake]            plain colours instead of the baked atlas (fast iteration)
        [--bake-size 2048]     atlas size (base colour + metallic/roughness PNG inside the .glb)
        [--set key=value ...]  override a proportion in P (px, see below), e.g. --set gun_tip=16.5
        [--iou ORIG.npz]       silhouette IoU against the original frames (32 facings, game camera); the npz
                               holds body_opaque / turret_opaque [32, N, N] masks and origin = the hotspot pixel
        [--turntable DIR]      review renders: 8 views around the model + close-ups (house colour = Atreides blue)
        [--samples 64]
        [--device cpu|gpu]     Cycles device for the bake and the review renders (default cpu: the Metal
                               kernel compile of Blender 5.2 aborted once in a background thread, and the
                               CPU is as fast at these sizes)

    then the 2D pack (docs/art-pipeline.md "Rendered from 3D"; RENDER_FLAGS below):
    python tools/render_sprites/render_pack.py --models artpacks/models-fable-v3 --out artpacks/rendered-fable-v3 \
        --types ATREIDES_COMBAT_TANK --subpixel --device cpu --view-transform Standard --look "High Contrast" \
        --exposure 0 --house-grey 0.27 --sky-strength 0.05 --shadow-azimuth 30 --sun-elevation 58 \
        --contour 0.65,0.85 --contour-azimuth -45 --sharpen 0.6

Conventions (docs/3d-models.md "New models"): Blender units = tiles, +X east, +Y north = the tank's
front, +Z up, origin on the ground at the unit centre; nodes "body", "turret" (origin on the turret
axis at its base = the pivot) and "barrel" (child of the turret, origin at the trunnion). Materials:
everything but the house areas is baked into ONE atlas material "body" (base colour, metallic /
roughness); "HOUSE" is the house-colour mask (plain, named as render_pack and model_gltf expect), its
COLOR_0 carries the shade of each house part (the turret stripe is darker than the caps, every part
darkens towards its lower edge).

Everything is authored in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; u forward, v right, z up from
the ground) and scaled at the end, so every number below can be checked against the sprite.

How the original was read (DATA.R16 frames 1966-1997 body / 2094-2125 turret; nothing is copied: the
proportions come from pixel counts and from a visual hull carved out of the 32 silhouettes with the
game camera - orthographic, 45 deg, the frames turn about a pivot 5.7 px above the ground whose
projection is the CENTRE of the hotspot pixel):

  * Two track pods 5 px wide (|v| 3.1..8.1), 27.5 px long (u -15..12.4). Side elevation = a flat
    hexagon: the top run is level (6.8 px) only between u -11 and +10, then drops 45 deg to a low
    rear tip (2.4 px) / a blunter nose (3.7 px, bottom raised). The tread (3 px, fine rounded
    links) runs BETWEEN two side frames 1 px thick. A frame is a trapezoid: a dark-grey panel whose
    sloped end edges are light beams, from under the roof cover's ends down to the pod's bottom
    corners (40 deg rear, 59 deg front) - the "slanted light members" of the sprite's flank, and
    the light edges of the rear legs seen from behind; beyond them the loop is open (idler /
    sprocket, the tread's inside: near-black). A near-black sill closes the bottom (the sprite's
    1-px black run).
  * A cream roof cover on each pod (9.5 x 3.45 px, 0.8 above the track) with a 2-px house-colour
    cap wrapping each end (a dark slot between them). Outside it, over a dark box 8 px long, a
    shoulder plate slopes 45 deg down and outwards (v 8.2 -> 9.75, z 7.35 -> 5.8): its upper strip
    cream (the sprite's "light rim strip": exactly one pixel row in the side view), its lower strip
    dusty grey, a dark groove between it and the cover.
  * A hull block between the pods under the turret only (deck 7 px high, u -5.2..5.3, sloped rear
    and glacis, dark below): the pods are two separate legs behind and ahead of it.
  * Turret (centred on the pivot): base 14.5 x 10.8 px at 7.8 px with small rear chamfers and a
    tapered front, dark flat roof 6.7 x 8 px at 12.3 px with the 2.8 x 6 px house stripe as a ridge,
    then a long white front glacis around a dark gun housing 3.4 px wide. The walls are separate
    armour panels over a black core: the gaps are the sprite's dark vertical seams (two on each
    side, one either side of the 3-px rear tail block). Plain 2.9-px gun to 16.2 px from the pivot,
    black base flange, dark square socket under the turret.
  * The body sits 0.5 px right of the rotation axis in the original frames (kept: P["v0"]).

Look (owner's notes on the checkpoints): near-black seams / gaps / sill; procedural weathering baked
into the atlas (mottle, a patchy dust film, dirt patches, cavity dirt at two radii, grime along lower
edges and towards the bottom of every part, run-off streaks, a few chipped edges, exhaust soot over
the rear, dusty grey-brown tracks); medium saturated house blue (--house-grey 0.27). The pack uses
Blender's Standard view transform with its High Contrast look and little sky fill - albedo colours
as authored and hard contrast, like the original's pre-rendered sprites (AgX rolls the highlights
off and desaturates them: cream turns white and no dirt shows on a light plate) - plus a dark
contour along the lower-right rim of every frame (--contour).
"""
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402  generic helpers (bevel, join, uv unwrap, bake packing, export, camera)

RENDER_FLAGS = ('--subpixel --device cpu --view-transform Standard --look "High Contrast" --exposure 0 --house-grey 0.27 '
                '--sky-strength 0.05 --shadow-azimuth 30 --sun-elevation 58 --contour 0.65,0.85 --contour-azimuth -45 --sharpen 0.6')

PX = 1.0 / 32.0

# ------------------------------------------------------------------------------------------------
# Proportions, in original sprite pixels (u forward, v right, z above the ground).

P = dict(
    v0=0.5,                         # the body sits 0.5 px right of the rotation axis (as the original frames)
    v0_turret=0.0,                  # the turret is centred on it
    # track loop: side profile (u, z), counter-clockwise from the rear bottom; lateral extent of the tread
    track_profile=[(-13.3, 0.0), (9.3, 0.0), (12.0, 2.6), (12.4, 3.7), (11.7, 5.0), (10.0, 6.8),
                   (-11.2, 6.8), (-14.4, 3.7), (-15.0, 2.4), (-14.5, 1.0)],
    track_round=0.7,
    tread_v=(4.1, 7.1), belt_t=0.45,
    link_pitch=0.74, link_len=0.52, link_h=0.12,
    # side frames: the outer one's face at pod_out, the inner one's at pod_in; thickness frame_t
    pod_in=3.1, pod_out=8.1, frame_t=1.0,
    frame_top=6.45, frame_a=-7.7, frame_b=7.3,          # top edge (u of its rear / front end)
    frame_foot_r=(-13.75, 1.15), frame_foot_f=(10.45, 1.15),
    member_w=(1.3, 1.1), member_proud=0.15,             # horizontal width of the rear / front beam
    sill=(0.25, 1.2, 0.15),                             # z0, z1, how far it stands proud of the frame face
    # roof cover + house caps
    cover_v=(3.85, 7.3), cover_u=(-5.5, 4.5), cover_z=(6.7, 7.6),
    cap_len=1.95, cap_over=0.15, cap_slope=0.35,
    # shoulder: dark box out to sp_v, 45 deg plate from z1 at the cover side; sp_light = width of its cream strip
    sp_v=9.75, sp_u=(-4.6, 3.6), sp_z=(2.9, 7.35), sp_end=0.7, sp_light=0.8,
    # hull block between the pods
    hull_w=2.75, hull_deck=7.0, hull_deck_u=(-5.2, 5.3), hull_rear=(-7.3, 6.1), hull_rear_low=(-7.5, 2.9),
    hull_glacis=(7.3, 5.3), hull_front_low=(7.35, 3.0), hull_belly=2.3, hull_split=5.6,
    # turret (turret-local u, v; z above the ground)
    tur_base=7.8, tur_mid=9.7, tur_top=12.3,
    tur_plan=[(2.0, -6.0), (3.6, -6.0), (5.4, -4.4), (5.4, -2.5), (5.4, 2.2), (2.7, 8.5)],    # right half (v, u), rear -> front
    tur_roof=[(1.7, -4.6), (2.9, -4.6), (4.0, -3.6), (4.0, -2.0), (4.0, 1.7), (2.4, 2.1)],    # the flat roof ends at u 2.1: a long front glacis
    tur_seam=[True, False, False, True, True, False],   # a panel gap (dark seam) at these vertices: beside the tail, twice on each side
    panel_gap=1.1, panel_t=0.36, flange=0.6,
    stripe=(-4.2, 1.9, 1.4, 0.7),                      # u0, u1, half width, height above the roof (a ridge: it shades the roof)
    stripe_shade=0.51, house_low=0.4,                   # COLOR_0 of the stripe; of the lower edge of any house part
    tail=(-8.1, -5.7, 1.45, 8.2, 12.0, 9.8),           # u0, u1, half width, z0, z1 (front), z1 (rear)
    mantlet=(3.0, 8.6, 1.7, 8.2, 12.2, 11.85),         # u0, u1, half width, z0, z1 (rear), z1 (front)
    gun_z=10.35, gun_r=1.45, gun_tip=16.2,
    socket=(-4.2, 5.0, 3.0, 7.05),                     # u0, u1, half width, z0 (up to the turret base)
)

# sRGB base colours
# The pack is rendered with the Standard view transform at exposure 0 (RENDER_FLAGS), so these are
# very nearly the colours on screen (a sunlit top face shows its albedo a few % brighter, a wall in
# shade at a third) - and Graphics: 3D, which shows the texture as it is, looks the same.
COL = dict(
    cream=(0.918, 0.864, 0.781),     # roof covers, sponson tops, hull deck: the original's warm cream, clean
    frame=(0.968, 0.896, 0.781),     # the slanted beams of the side frames
    panel=(0.429, 0.407, 0.352),     # flank panels, sponson sides, lower hull: dark olive grey
    shoulder=(0.66, 0.621, 0.533),   # lower strip of the shoulder plates: dusty grey-beige
    tread=(0.094, 0.088, 0.077),     # belt
    link=(0.138, 0.127, 0.11),       # link pads
    sill=(0.083, 0.077, 0.068),
    wheel=(0.176, 0.165, 0.143),
    core=(0.077, 0.072, 0.064),      # inside of the track loop
    tur_white=(0.97, 0.96, 0.93),    # turret wall panels: neutral white, clean
    tur_roof=(0.341, 0.325, 0.292),  # turret roof: neutral grey
    tur_core=(0.055, 0.055, 0.05),   # under the panels (the seams) and the socket
    mantlet=(0.281, 0.264, 0.242),
    gun=(0.517, 0.495, 0.451),
    HOUSE=(0.80, 0.80, 0.80),
)
# (metallic, roughness, surface kind)
SPEC = dict(
    cream=(0.0, 0.34, "paint"), frame=(0.0, 0.45, "paint"), panel=(0.0, 0.6, "paint"), shoulder=(0.0, 0.55, "paint"),
    tread=(0.0, 0.75, "rubber"), link=(0.3, 0.55, "steel"), sill=(0.2, 0.7, "dark"),
    wheel=(0.4, 0.5, "steel"), core=(0.0, 0.85, "dark"),
    tur_white=(0.0, 0.34, "paint"), tur_roof=(0.0, 0.5, "paint"), tur_core=(0.0, 0.9, "dark"),
    mantlet=(0.0, 0.55, "paint"), gun=(0.2, 0.36, "steel"), HOUSE=(0.0, 0.32, "house"),
)
PREVIEW_HOUSE = (0.33, 0.45, 0.65)   # Atreides blue (about what the game's ramp gives the caps), previews only

srgb_to_linear = V2.srgb_to_linear
log = V2.log

# ------------------------------------------------------------------------------------------------
# Mesh helpers (all in px: x = v, y = u, z = height)

_mats = {}


def mat(name):
    m = _mats.get(name)
    if m is not None and m.name in bpy.data.materials:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*srgb_to_linear(COL[name]), 1)
    b.inputs["Metallic"].default_value = SPEC[name][0]
    b.inputs["Roughness"].default_value = SPEC[name][1]
    if name == "HOUSE":
        # base colour x COLOR_0: lets one house area be darker than another (the turret stripe);
        # the glTF exporter writes COLOR_0 for materials that use a colour attribute
        nt = m.node_tree
        vc = nt.nodes.new("ShaderNodeVertexColor")
        vc.layer_name = "Col"
        mul = nt.nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs["Factor"].default_value = 1.0
        mul.inputs[6].default_value = b.inputs["Base Color"].default_value
        mul.name = "HOUSE_TINT"
        nt.links.new(vc.outputs["Color"], mul.inputs[7])
        nt.links.new(mul.outputs[2], b.inputs["Base Color"])
    _mats[name] = m
    return m


def shade(o, k):
    """COLOR_0 of a part (linear grey k); parts without a call are white."""
    o["d2k_shade"] = k
    return o


def colour_attribute(o):
    """COLOR_0 "Col": white, except on house parts - their shade, falling off towards the part's
    lower edge (grime where a cap meets the track / the stripe meets the roof; the house material
    itself is a plain mask, so this is its only weathering)."""
    k = o.get("d2k_shade", 1.0)
    V2.apply_all(o)
    me = o.data
    ca = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    zs = [v.co.z for v in me.vertices]
    z0, z1 = min(zs), max(zs)
    # "grime_h": the height within the part (0 bottom .. 1 top), read by the weathering shaders
    gh = me.attributes.new("grime_h", "FLOAT", "POINT")
    for v, d in zip(me.vertices, gh.data):
        d.value = (v.co.z - z0) / max(1e-6, z1 - z0)
    house = any(m is not None and m.name.startswith("HOUSE") for m in me.materials)
    if not house:
        for d in ca.data:
            d.color = (k, k, k, 1.0)
        return
    lo = P["house_low"]
    for loop, d in zip(me.loops, ca.data):
        t = (me.vertices[loop.vertex_index].co.z - z0) / max(1e-6, z1 - z0)
        t = min(1.0, t / 0.8)
        f = k * (lo + (1.0 - lo) * t * t * (3 - 2 * t))
        d.color = (f, f, f, 1.0)


def new_obj(name, bm, material, bev=0.0, segs=2, angle=30):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(mat(material))
    if bev > 0:
        m = o.modifiers.new("bevel", "BEVEL")
        m.width = bev
        m.segments = segs
        m.limit_method = "ANGLE"
        m.angle_limit = math.radians(angle)
        m.use_clamp_overlap = True
        m.harden_normals = False
    return o


def rings_solid(bm, rings, cap=True):
    """Closed solid lofted through polygon rings (same vertex count); returns the verts."""
    vs = [[bm.verts.new(p) for p in ring] for ring in rings]
    n = len(rings[0])
    for a, b in zip(vs[:-1], vs[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new([a[i], a[j], b[j], b[i]])
    if cap:
        bm.faces.new(vs[0][::-1])
        bm.faces.new(vs[-1])
    return vs


def prism_uz(name, pts_uz, v0, v1, material, bev=0.0, segs=2):
    """Side profile (u, z) extruded across the vehicle from v0 to v1."""
    bm = bmesh.new()
    rings_solid(bm, [[(v, u, z) for (u, z) in pts_uz] for v in (v0, v1)])
    return new_obj(name, bm, material, bev, segs)


def box(name, v0, v1, u0, u1, z0, z1, material, bev=0.0, segs=2):
    return prism_uz(name, [(u0, z0), (u1, z0), (u1, z1), (u0, z1)], v0, v1, material, bev, segs)


def hull_pts(name, pts, material, bev=0.0, segs=2):
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1), verts=bm.verts, edges=bm.edges)
    return new_obj(name, bm, material, bev, segs)


def loft(name, rings, material, bev=0.0, segs=2):
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, bev, segs)


def cyl(name, r, u0, u1, v, z, material, segs=20, bev=0.0, r1=None, axis="u"):
    """Cylinder along u (default) or v, from u0 to u1 (or v0..v1 with (u, z) centre when axis='v')."""
    bm = bmesh.new()
    ring0, ring1 = [], []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        c, s = math.cos(a), math.sin(a)
        ra, rb = r, (r if r1 is None else r1)
        if axis == "u":
            ring0.append((v + ra * c, u0, z + ra * s))
            ring1.append((v + rb * c, u1, z + rb * s))
        else:           # along v: u0, u1 are the v range, v is the u centre
            ring0.append((u0, v + ra * c, z + ra * s))
            ring1.append((u1, v + rb * c, z + rb * s))
    rings_solid(bm, [ring0, ring1])
    return new_obj(name, bm, material, bev, 2, angle=50)


def round_poly(pts, r, n=3):
    """Round every corner of a closed (u, z) polygon with an arc of radius ~r (n segments)."""
    out = []
    m = len(pts)
    for i in range(m):
        p0, p1, p2 = Vector(pts[(i - 1) % m]), Vector(pts[i]), Vector(pts[(i + 1) % m])
        d1, d2 = (p0 - p1), (p2 - p1)
        l1, l2 = d1.length, d2.length
        d1.normalize()
        d2.normalize()
        ang = d1.angle(d2)
        t = min(r / math.tan(ang / 2), 0.45 * l1, 0.45 * l2)     # tangent length along both edges
        a, b = p1 + d1 * t, p1 + d2 * t
        for k in range(n + 1):
            s = k / n
            q = (1 - s) ** 2 * a + 2 * (1 - s) * s * p1 + s ** 2 * b    # quadratic Bezier ~ arc
            out.append((q.x, q.y))
    return out


def inset(poly, d):
    """Inset a counter-clockwise (x, y) polygon by d."""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = Vector(poly[(i - 1) % n]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        d1 = (p1 - p0).normalized()
        d2 = (p2 - p1).normalized()
        n1 = Vector((-d1.y, d1.x))
        n2 = Vector((-d2.y, d2.x))
        nn = (n1 + n2).normalized()
        k = d / max(0.35, nn.dot(n1))
        q = p1 + nn * k
        out.append((q.x, q.y))
    return out


def along(profile, step):
    """(point, unit tangent) every `step` along a closed polygon."""
    out = []
    n = len(profile)
    total = sum((Vector(profile[(i + 1) % n]) - Vector(profile[i])).length for i in range(n))
    count = max(1, int(round(total / step)))
    step = total / count
    d_next, acc = 0.0, 0.0
    for i in range(n):
        a, b = Vector(profile[i]), Vector(profile[(i + 1) % n])
        seg = b - a
        ln = seg.length
        if ln < 1e-9:
            continue
        t = seg / ln
        while d_next < acc + ln - 1e-9 and len(out) < count:
            out.append((a + t * (d_next - acc), t))
            d_next += step
        acc += ln
    return out


def mirror_copy(o):
    c = o.copy()
    c.data = o.data.copy()
    c.name = o.name + "_L"
    bpy.context.scene.collection.objects.link(c)
    c.data.transform(Matrix.Scale(-1, 4, (1, 0, 0)))
    c.data.flip_normals()
    return c


def finish_node(objs, name, origin_px, offset_px=(0.0, 0.0, 0.0)):
    """Join parts (px), move them by offset_px, scale to tiles, put the node origin at origin_px."""
    for o in objs:
        colour_attribute(o)
    j = V2.join(objs, name)
    j.data.transform(Matrix.Scale(PX, 4) @ Matrix.Translation(Vector(offset_px)))
    V2.set_origin(j, Vector(origin_px) * PX)
    V2.smooth_by_angle(j, 32)
    m = j.modifiers.new("wn", "WEIGHTED_NORMAL")
    m.keep_sharp = True
    m.weight = 60
    bpy.context.view_layer.objects.active = j
    bpy.ops.object.modifier_apply(modifier=m.name)
    return j


# ------------------------------------------------------------------------------------------------
# The body


def track_parts(parts):
    """Right pod (v > 0): tread loop with fine rounded links between the two side frames, dark core,
    idler and sprocket visible through the open ends."""
    prof = round_poly(P["track_profile"], P["track_round"], 3)
    tv0, tv1 = P["tread_v"]
    t = P["belt_t"]
    inner = inset(prof, t)
    n = len(prof)
    bm = bmesh.new()
    ro = [[bm.verts.new((v, u, z)) for (u, z) in prof] for v in (tv0, tv1)]
    ri = [[bm.verts.new((v, u, z)) for (u, z) in inner] for v in (tv0, tv1)]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([ro[0][i], ro[0][j], ro[1][j], ro[1][i]])
        bm.faces.new([ri[0][j], ri[0][i], ri[1][i], ri[1][j]])
        bm.faces.new([ro[0][j], ro[0][i], ri[0][i], ri[0][j]])
        bm.faces.new([ro[1][i], ro[1][j], ri[1][j], ri[1][i]])
    parts.append(new_obj("belt", bm, "tread"))
    # links: low rounded pads across the tread, all round the loop
    bm = bmesh.new()
    L, h = P["link_len"], P["link_h"]
    c = L * 0.28
    sect = [(-L / 2, -0.03), (-L / 2, h * 0.5), (-L / 2 + c, h), (L / 2 - c, h), (L / 2, h * 0.5), (L / 2, -0.03)]
    for (p, tg) in along(prof, P["link_pitch"]):
        nrm = Vector((tg.y, -tg.x))                      # outward for a counter-clockwise loop
        ring = [p + tg * s + nrm * hh for (s, hh) in sect]
        rings_solid(bm, [[(v, q.x, q.y) for q in ring] for v in (tv0 + 0.06, tv1 - 0.06)])
    parts.append(new_obj("links", bm, "link"))
    # dark core between the frames, the wheels at both ends
    parts.append(box("core", tv0 + 0.02, tv1 - 0.02, -10.0, 8.4, 0.75, 6.25, "core"))
    for (name, cu, cz, r) in (("idler", -12.75, 2.45, 1.6), ("sprocket", 10.2, 3.65, 1.55)):
        parts.append(cyl(name, r, tv0 + 0.1, tv1 - 0.1, cu, cz, "wheel", segs=20, bev=0.12, axis="v"))
        parts.append(cyl(name + "_hub", r * 0.42, tv0 - 0.08, tv1 + 0.08, cu, cz, "link", segs=12, bev=0.06, axis="v"))


def frame_parts(parts, v_face, sign, sill=True):
    """One side frame: dark panel, light sloped end beams + top beam, near-black sill.
    v_face = the frame's outer face, sign = +1 when that face looks towards +v."""
    th = P["frame_t"]
    a, b, ztop = P["frame_a"], P["frame_b"], P["frame_top"]
    fr, ff = P["frame_foot_r"], P["frame_foot_f"]
    pr = P["member_proud"]
    v_in = v_face - sign * th
    v_panel = v_face - sign * pr
    lo, hi = sorted((v_in, v_panel))
    panel = [fr, ff, (b, ztop), (a, ztop)]
    parts.append(prism_uz("panel", panel, lo, hi, "panel", bev=0.05, segs=1))
    wr, wf = P["member_w"]
    lo, hi = sorted((v_in + sign * 0.05, v_face))
    parts.append(prism_uz("beam_r", [fr, (fr[0] + wr, fr[1]), (a + wr, ztop), (a, ztop)], lo, hi, "frame", bev=0.13))
    parts.append(prism_uz("beam_f", [(ff[0] - wf, ff[1]), ff, (b, ztop), (b - wf, ztop)], lo, hi, "frame", bev=0.13))
    parts.append(prism_uz("beam_t", [(a + 0.3, ztop - 0.75), (b - 0.3, ztop - 0.75), (b, ztop), (a, ztop)], lo, hi, "panel", bev=0.1))
    if sill:
        z0, z1, sp = P["sill"]
        lo, hi = sorted((v_in, v_face + sign * sp))
        parts.append(prism_uz("sill", [(fr[0] + 0.55, z0), (ff[0] - 1.0, z0), (ff[0], z1), (fr[0], z1)], lo, hi, "sill",
                              bev=0.08, segs=1))
    # bolt heads at the beam joints (read only close up)
    for (u, z) in ((a + 0.6, ztop - 0.4), (b - 0.55, ztop - 0.4), (fr[0] + 0.9, fr[1] + 0.45), (ff[0] - 0.8, ff[1] + 0.5)):
        lo, hi = sorted((v_face - sign * 0.02, v_face + sign * 0.07))
        parts.append(cyl("bolt", 0.14, lo, hi, u, z, "frame", segs=8, axis="v"))


def cover_parts(parts):
    """Cream roof cover over the pod's top run, the house caps wrapping both ends."""
    v0, v1 = P["cover_v"]
    u0, u1 = P["cover_u"]
    z0, z1 = P["cover_z"]
    parts.append(box("cover", v0, v1, u0 + 0.24, u1 - 0.24, z0, z1, "cream", bev=0.2, segs=3))    # a dark slot before each cap
    # a recessed panel line down the middle and two flush fasteners (subtle; reads as a plate, not a block)
    parts.append(box("cover_seam", v0 + 0.25, v1 - 0.25, -0.62, -0.38, z1 - 0.1, z1 + 0.004, "core"))
    L, ov, sl = P["cap_len"], P["cap_over"], P["cap_slope"]
    for sgn, ue in ((1, u1), (-1, u0)):
        ua, ub = ue, ue + sgn * L                        # the cap's inner / outer end
        pts = []
        for v in (v0 - ov, v1 + ov):
            pts += [(v, ua, z0 + 0.03), (v, ua, z1 + ov), (v, ub, z1 + ov), (v, ub + sgn * sl, z0 + 0.03)]
        parts.append(hull_pts("cap", pts, "HOUSE", bev=0.2, segs=3))


def sponson_parts(parts):
    """The shoulder on the pod's outer flank: a dark box under a plate that slopes 45 deg down and
    outwards beside the roof cover - its upper strip cream (the sprite's "light rim strip", one
    pixel row in the side view), its lower strip dusty grey (the lighter top row of the flank)."""
    v0 = P["pod_out"] + 0.1
    v1 = P["sp_v"]
    u0, u1 = P["sp_u"]
    z0, z1 = P["sp_z"]
    e = P["sp_end"]
    zs = z1 - (v1 - v0)                                 # where the 45 deg slope meets the outer face
    vm = v0 + P["sp_light"]                             # cream above, grey below
    zm = z1 - P["sp_light"]
    body = []
    for (u_top, u_bot) in ((u0, u0 - e), (u1, u1 + e)):
        body += [(v0 - 0.3, u_top, z1 - 0.3), (v0, u_top, z1 - 0.3), (v1 - 0.05, u_top + (u_bot - u_top) * 0.25, zs - 0.25),
                 (v1 - 0.05, u_bot, z0 + 0.35), (v1 - 0.45, u_bot, z0), (v0 - 0.3, u_bot, z0)]
    parts.append(hull_pts("sponson", body, "panel", bev=0.14))
    # the sloped plate (0.3 thick): two strips, a dark groove between it and the cover
    t = 0.3
    hi, lo = [], []
    for u in (u0 - 0.1, u1 + 0.1):
        hi += [(v0, u, z1 - t - 0.05), (v0, u, z1), (v0 + 0.15, u, z1 + 0.06), (vm + 0.04, u, zm + 0.1), (vm - 0.02, u, zm - t)]
        lo += [(vm + 0.06, u, zm + 0.08), (v1 + 0.1, u, zs + 0.05), (v1 + 0.1, u, zs - t - 0.05), (v1 - 0.2, u, zs - t - 0.05), (vm, u, zm - t)]
    parts.append(hull_pts("shoulder_light", hi, "cream", bev=0.08))
    parts.append(hull_pts("shoulder_grey", lo, "shoulder", bev=0.08))
    # a recessed grille slot in the outer face (detail for close views)
    parts.append(box("sponson_slot", v1 - 0.1, v1 - 0.02, u0 + 1.2, u1 - 1.2, z0 + 0.7, z0 + 1.3, "core"))


def hull_parts(parts):
    w = P["hull_w"]
    zd = P["hull_deck"]
    ur, uf = P["hull_deck_u"]
    rr, rl = P["hull_rear"], P["hull_rear_low"]
    gl, fl = P["hull_glacis"], P["hull_front_low"]
    zb, zs = P["hull_belly"], P["hull_split"]
    lower = [(rl[0] + 0.3, zb), (fl[0] - 0.25, zb), fl, (gl[0], zs), (rl[0], zs), rl]
    parts.append(prism_uz("hull_lower", lower, -w + 0.05, w - 0.05, "panel", bev=0.2))
    upper = [(rl[0], zs - 0.05), (gl[0], zs - 0.05), gl, (uf, zd), (ur, zd), rr]
    parts.append(prism_uz("hull_upper", upper, -w, w, "cream", bev=0.22, segs=3))
    # low seat plate under the turret (hidden by it), dark exhaust slots in the rear face
    s = P["socket"]
    parts.append(box("seat", -s[2] + 0.3, s[2] - 0.3, s[0] + 0.25, s[1] - 0.45, zd - 0.05, zd + 0.14, "cream", bev=0.07, segs=1))
    for k in (-1, 1):
        parts.append(box("exhaust", k * 1.45 - 0.75, k * 1.45 + 0.75, rl[0] - 0.06, rl[0] + 0.3, 3.55, 4.75, "core"))


def build_body():
    parts = []
    track_parts(parts)
    frame_parts(parts, P["pod_out"], +1)
    frame_parts(parts, P["pod_in"], -1)
    cover_parts(parts)
    sponson_parts(parts)
    for o in list(parts):
        V2.apply_all(o)
        parts.append(mirror_copy(o))
    hull_parts(parts)
    return finish_node(parts, "body", (0, 0, 0), (P["v0"], 0, 0))


# ------------------------------------------------------------------------------------------------
# The turret


def full_plan(half):
    """Right-half (v, u) polygon rear -> front  ->  closed counter-clockwise (x, y) polygon."""
    return list(half) + [(-v, u) for (v, u) in reversed(half)]


def build_turret():
    zb, zm, zt = P["tur_base"], P["tur_mid"], P["tur_top"]
    base = full_plan(P["tur_plan"])
    roof = full_plan(P["tur_roof"])
    n = len(base)
    th, gap = P["panel_t"], P["panel_gap"]
    tp = []
    # dark core under the panels: what the seams show
    core_b, core_r = inset(base, th + 0.02), inset(roof, th + 0.02)
    tp.append(loft("tcore", [[(x, y, zb + 0.06) for (x, y) in core_b], [(x, y, zm) for (x, y) in core_b],
                             [(x, y, zt - 0.1) for (x, y) in core_r]], "tur_core"))
    # dark base flange under the panels
    fl = P["flange"]
    tp.append(loft("tflange", [[(x, y, zb) for (x, y) in inset(base, -0.14)], [(x, y, zb + fl) for (x, y) in inset(base, -0.14)]],
                   "tur_core", bev=0.06))
    # one armour panel per wall facet (a bent plate: vertical band + sloped upper part); the panels
    # stand apart only where the original shows a dark vertical seam
    seam = list(P["tur_seam"]) + list(reversed(P["tur_seam"]))
    for i in range(n):
        j = (i + 1) % n
        b0, b1 = Vector(base[i]), Vector(base[j])
        r0, r1 = Vector(roof[i]), Vector(roof[j])
        eb = (b1 - b0).normalized()
        er = (r1 - r0).normalized()
        nrm = Vector((eb.y, -eb.x))                       # outward (counter-clockwise polygon)
        g0, g1 = (gap / 2 if seam[i] else 0.0), (gap / 2 if seam[j] else 0.0)
        pts = []
        for inward in (0.0, th):
            off = -nrm * inward
            pts += [(*(b0 + eb * g0 + off), zb + fl), (*(b1 - eb * g1 + off), zb + fl),
                    (*(b0 + eb * g0 + off), zm), (*(b1 - eb * g1 + off), zm),
                    (*(r0 + er * g0 * 0.8 + off), zt - 0.08), (*(r1 - er * g1 * 0.8 + off), zt - 0.08)]
        tp.append(hull_pts(f"panel{i}", pts, "tur_white", bev=0.13))
    # roof plate (dark grey), the house stripe along the gun axis
    rp = inset(roof, 0.1)
    tp.append(loft("troof", [[(x, y, zt - 0.2) for (x, y) in inset(roof, -0.03)], [(x, y, zt + 0.12) for (x, y) in rp]], "tur_roof", bev=0.08))
    su0, su1, sw, sh = P["stripe"]
    tp.append(shade(hull_pts("stripe", [(sx * (sw - dz * 0.22), u + (0.25 * dz if u == su0 else -0.5 * dz), zt + 0.05 + dz)
                                        for sx in (-1, 1) for u in (su0, su1) for dz in (0.0, sh)],
                             "HOUSE", bev=0.16, segs=3), P["stripe_shade"]))
    # rear tail block
    u0, u1, hw, z0, z1f, z1r = P["tail"]
    tp.append(hull_pts("tail", [(sx * (hw if u == u1 else hw - 0.35), u, z) for sx in (-1, 1)
                                for (u, z) in ((u0, z0 + 0.15), (u1, z0), (u1, z1f), (u0, z1r))], "tur_white", bev=0.16))
    # mantlet block: dark grey, as wide as the stripe, from the roof's front edge down to the gun
    u0, u1, hw, z0, z1r, z1f = P["mantlet"]
    tp.append(hull_pts("mantlet", [(sx * hw, u, z) for sx in (-1, 1) for (u, z) in ((u0, z0), (u1, z0 + 0.25), (u1, z1f), (u0, z1r))],
                       "mantlet", bev=0.2, segs=3))
    # dark square socket under the turret
    s = P["socket"]
    tp.append(box("socket", -s[2], s[2], s[0], s[1], s[3], zb + 0.1, "tur_core"))
    tur = finish_node(tp, "turret", (0, 0, zb), (P["v0_turret"], 0, 0))
    # the gun: a plain tube, a short muzzle step and the bore
    gz, gr, tip = P["gun_z"], P["gun_r"], P["gun_tip"]
    g0 = P["mantlet"][1] - 0.3
    gp = [cyl("barrel", gr, g0, tip - 0.55, 0, gz, "gun", segs=24),
          cyl("muzzle", gr + 0.06, tip - 0.6, tip, 0, gz, "gun", segs=24, bev=0.1),
          cyl("bore", gr * 0.55, tip - 0.3, tip + 0.012, 0, gz, "tur_core", segs=14),
          cyl("collar", gr + 0.16, g0, g0 + 0.7, 0, gz, "mantlet", segs=24, bev=0.08)]
    gun = finish_node(gp, "barrel", (0, g0, gz), (P["v0_turret"], 0, 0))
    gun.parent = tur
    bpy.context.view_layer.update()
    gun.matrix_parent_inverse = tur.matrix_world.inverted()
    return tur


def build():
    root = bpy.data.objects.new("ATREIDES_COMBAT_TANK", None)
    bpy.context.scene.collection.objects.link(root)
    body = build_body()
    body.parent = root
    tur = build_turret()
    tur.parent = root
    bpy.context.view_layer.update()
    return root


# ------------------------------------------------------------------------------------------------
# Procedural surfaces -> baked atlas


# Weathering (sRGB colours). The original sprite is a worn, sand-blown vehicle: its light plates are
# a dusty cream / grey-beige with dirtier patches, not a clean white; the track legs read as a dusty
# grey-brown; every seam and recess is near-black. The albedo itself carries that look (Graphics: 3D
# shows the texture as it is).
DIRT = dict(
    cavity=(0.077, 0.068, 0.057),    # packed dirt / shadow in seams and inner corners: near-black
    soft=(0.462, 0.407, 0.33),       # grey-brown film spreading out of the recesses
    blotch=(0.605, 0.539, 0.44),     # dirt patches on the plates
    grime=(0.396, 0.352, 0.292),     # grime along lower edges and towards the bottom of every part
    streak=(0.396, 0.352, 0.292),    # run-off streaks on upright faces
    chip=(0.253, 0.231, 0.209),      # chipped paint: primer / dark metal
    chip_light=(0.77, 0.737, 0.671), # chips on dark paint: bare metal
    sand=(0.638, 0.561, 0.44),       # sand dust lying low and on the tracks
    soot=(0.165, 0.154, 0.138),      # exhaust soot over the rear of the hull deck and the turret
)
# the dust film that dulls most of a surface: its colour per material
FILM = dict(
    cream=(0.759, 0.704, 0.616), frame=(0.781, 0.715, 0.611), tur_white=(0.77, 0.743, 0.693),
    panel=(0.506, 0.473, 0.407), shoulder=(0.572, 0.533, 0.451), tur_roof=(0.424, 0.396, 0.347), mantlet=(0.352, 0.33, 0.292),
    tread=(0.209, 0.193, 0.165), link=(0.231, 0.215, 0.182), wheel=(0.242, 0.22, 0.187), gun=(0.55, 0.517, 0.462),
)
SOOT = dict(tur_white=0.85, cream=0.75, tur_roof=0.5)     # how much soot a material takes (towards the rear)
GRIME_SCALE = dict(frame=0.15)                            # the thin frame beams stay light: they are the sprite's light lines
#            film  blotch cavity soft  grime streak chip  low dust
WEATHER = dict(
    paint=(0.85, 0.75, 0.92, 0.60, 0.75, 0.70, 0.50, 0.50),
    steel=(0.75, 0.25, 0.80, 0.40, 0.35, 0.25, 0.00, 0.20),
    rubber=(0.80, 0.0, 0.55, 0.25, 0.0, 0.00, 0.00, 0.18),
    dark=(0.0, 0.0, 0.50, 0.10, 0.0, 0.00, 0.00, 0.10),
)


def surface_tree(nt, name):
    """(colour, roughness, metallic) sockets for a material: the base paint / steel / rubber under
    layers of weathering - pixel-scale mottle, a patchy dust film, dirt patches at two sizes, cavity
    dirt at two radii, grime along the lower edges and towards the bottom of every part (the "grime_h"
    attribute = height within the part), run-off streaks, a few chipped edges, exhaust soot at the
    rear, sand lying low. Upper edges stay clean: they carry the highlights."""
    base_srgb = COL[name]
    metal, rough, kind = SPEC[name]
    w_film, w_blotch, w_cav, w_soft, w_grime, w_streak, w_chip, w_dust = WEATHER[kind]
    nodes, links = nt.nodes, nt.links

    def value(x):
        v = nodes.new("ShaderNodeValue")
        v.outputs[0].default_value = x
        return v.outputs[0]

    def rgb(c):
        n = nodes.new("ShaderNodeRGB")
        n.outputs[0].default_value = (*srgb_to_linear(c), 1)
        return n.outputs[0]

    def noise(scale, detail=3.0, rough_=0.55, mapping=None, offset=0.0):
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough_
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = mapping or (1.0, 1.0, 1.0)
        mp.inputs["Location"].default_value = (offset, offset * 0.7, offset * 1.3)
        links.new(geo.outputs["Position"], mp.inputs["Vector"])      # world space: continuous across the nodes
        links.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]

    def remap(sock, a_, b_, c_, d_):
        m = nodes.new("ShaderNodeMapRange")
        m.inputs["From Min"].default_value = a_
        m.inputs["From Max"].default_value = b_
        m.inputs["To Min"].default_value = c_
        m.inputs["To Max"].default_value = d_
        links.new(sock, m.inputs["Value"])
        return m.outputs[0]

    def mix(fac, a_, b_, blend="MIX"):
        m = nodes.new("ShaderNodeMix")
        m.data_type = "RGBA"
        m.blend_type = blend
        if isinstance(fac, float):
            m.inputs["Factor"].default_value = fac
        else:
            links.new(fac, m.inputs["Factor"])
        links.new(a_, m.inputs[6])
        links.new(b_, m.inputs[7])
        return m.outputs[2]

    def mixf(fac, a_, b_):
        m = nodes.new("ShaderNodeMix")
        m.data_type = "FLOAT"
        links.new(fac, m.inputs["Factor"])
        for sock, x in ((m.inputs[2], a_), (m.inputs[3], b_)):
            if isinstance(x, float):
                sock.default_value = x
            else:
                links.new(x, sock)
        return m.outputs[0]

    def math(op, a_, b_=None):
        m = nodes.new("ShaderNodeMath")
        m.operation = op
        m.use_clamp = op in ("MULTIPLY", "ADD", "SUBTRACT")
        for sock, x in ((m.inputs[0], a_), (m.inputs[1], b_)):
            if x is None:
                continue
            if isinstance(x, float):
                sock.default_value = x
            else:
                links.new(x, sock)
        return m.outputs[0]

    def mul(a_, b_):
        return math("MULTIPLY", a_, b_)

    geo = nodes.new("ShaderNodeNewGeometry")
    nrm = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Normal"], nrm.inputs[0])
    pos = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], pos.inputs[0])
    upright = remap(nrm.outputs["Z"], 0.65, 0.15, 0.0, 1.0)          # walls
    low = remap(pos.outputs["Z"], 0.015, 0.14, 1.0, 0.0)             # close to the ground
    part_h = nodes.new("ShaderNodeAttribute")                        # 0 at the bottom of the part, 1 at its top
    part_h.attribute_name = "grime_h"
    bottom = remap(part_h.outputs["Fac"], 0.55, 0.0, 0.0, 1.0)
    cur = rgb(base_srgb)
    rough_s, metal_s = value(rough), value(metal)
    # ambient occlusion at two radii (same object only): recesses and inner corners
    ao_w = nodes.new("ShaderNodeAmbientOcclusion")
    ao_w.inputs["Distance"].default_value = 0.065
    ao_w.samples = 12
    ao_w.only_local = True
    near = remap(ao_w.outputs["AO"], 0.85, 0.35, 0.0, 1.0)
    ao_t = nodes.new("ShaderNodeAmbientOcclusion")
    ao_t.inputs["Distance"].default_value = 0.035
    ao_t.samples = 12
    ao_t.only_local = True
    tight = remap(ao_t.outputs["AO"], 0.82, 0.3, 0.0, 1.0)
    # edges (bevel normal against the face normal): upper ones stay clean, the others collect grime
    bev = nodes.new("ShaderNodeBevel")
    bev.samples = 8
    bev.inputs["Radius"].default_value = 0.022
    dot = nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    links.new(bev.outputs["Normal"], dot.inputs[0])
    links.new(geo.outputs["Normal"], dot.inputs[1])
    edge = remap(dot.outputs["Value"], 0.995, 0.86, 0.0, 1.0)
    bz = nodes.new("ShaderNodeSeparateXYZ")
    links.new(bev.outputs["Normal"], bz.inputs[0])
    edge_up = mul(edge, remap(bz.outputs["Z"], 0.25, 0.6, 0.0, 1.0))
    edge_low = mul(edge, remap(bz.outputs["Z"], 0.45, 0.1, 0.0, 1.0))
    # 1. mottle of about one sprite pixel + a broad drift
    amp = dict(paint=0.15, steel=0.12, rubber=0.12, dark=0.08)[kind]
    cur = mix(1.0, cur, remap(noise(30.0, 3.0, 0.6), 0.36, 0.64, 1 - amp * 1.2, 1 + amp * 0.5), "MULTIPLY")
    cur = mix(1.0, cur, remap(noise(7.0, 2.0, 0.5, offset=3.1), 0.3, 0.7, 1 - amp * 0.8, 1 + amp * 0.3), "MULTIPLY")
    # 2. the dust film: patchy, thicker near recesses, towards the bottom of the part and the ground
    if w_film and name in FILM:
        patch = remap(noise(5.0, 4.0, 0.62, offset=11.0), 0.36, 0.62, 0.3, 1.0)
        patch = mul(patch, remap(noise(21.0, 3.0, 0.6, offset=5.0), 0.32, 0.68, 0.5, 1.0))
        film = math("MAXIMUM", patch, mul(near, 0.9))
        film = math("MAXIMUM", film, mul(bottom, 0.85))
        film = math("MAXIMUM", film, mul(low, 0.85))
        film = mul(film, remap(edge_up, 0.0, 1.0, 1.0, 0.1))
        film = mul(film, w_film * (0.6 if name in GRIME_SCALE else 1.0))
        cur = mix(film, cur, rgb(FILM[name]))
        rough_s = mixf(film, rough_s, 0.9)
    # 3. dirt patches: a few large ones, many of about a sprite pixel
    if w_blotch:
        big = remap(noise(8.0, 3.0, 0.62, offset=17.0), 0.56, 0.68, 0.0, 1.0)
        small = remap(noise(24.0, 3.0, 0.65, offset=23.0), 0.58, 0.70, 0.0, 0.75)
        bl = mul(mul(math("MAXIMUM", big, small), remap(edge_up, 0.0, 1.0, 1.0, 0.0)), w_blotch)
        cur = mix(bl, cur, rgb(DIRT["blotch"]))
    # 4. cavity dirt: a grey-brown film out of the recesses, a near-black core in the seams
    cur = mix(mul(mul(near, remap(noise(26.0, 3.0, 0.6, offset=7.0), 0.25, 0.75, 0.5, 1.0)), w_soft * (0.4 if name in GRIME_SCALE else 1.0)),
              cur, rgb(DIRT["soft"]))
    cur = mix(mul(tight, w_cav), cur, rgb(DIRT["cavity"]))
    # 5. grime: along the lower / side edges and at the bottom of the part
    if w_grime:
        gr = math("MAXIMUM", mul(edge_low, 0.9), mul(remap(part_h.outputs["Fac"], 0.3, 0.0, 0.0, 1.0), 0.8))
        gr = mul(mul(gr, remap(noise(18.0, 3.0, 0.6, offset=13.0), 0.25, 0.7, 0.35, 1.0)), w_grime * GRIME_SCALE.get(name, 1.0))
        cur = mix(gr, cur, rgb(DIRT["grime"]))
    # 6. run-off streaks on upright faces
    if w_streak:
        sk = remap(noise(34.0, 2.0, 0.5, mapping=(1.0, 1.0, 0.05), offset=2.0), 0.52, 0.74, 0.0, 1.0)
        cur = mix(mul(mul(sk, upright), w_streak), cur, rgb(DIRT["streak"]))
    # 7. a few chipped edges
    if w_chip:
        wear = mul(mul(edge, remap(noise(48.0, 4.0, 0.7, offset=9.0), 0.52, 0.62, 0.0, 1.0)), w_chip)
        lum = 0.2126 * base_srgb[0] + 0.7152 * base_srgb[1] + 0.0722 * base_srgb[2]
        cur = mix(wear, cur, rgb(DIRT["chip"] if lum > 0.42 else DIRT["chip_light"]))
        rough_s = mixf(wear, rough_s, 0.45)
        metal_s = mixf(wear, metal_s, 0.8)
    # 8. exhaust soot: the rear of the vehicle (behind u = -4 px), patchy
    if name in SOOT:
        rear = remap(pos.outputs["Y"], -0.125, -0.21, 0.0, 1.0)
        so = mul(mul(rear, remap(noise(13.0, 3.0, 0.6, offset=6.0), 0.2, 0.7, 0.55, 1.0)), SOOT[name])
        cur = mix(so, cur, rgb(DIRT["soot"]))
        rough_s = mixf(so, rough_s, 0.85)
    # 9. sand lying low (on the tracks: all round the loop, it travels with the belt)
    if kind == "rubber" or name in ("link", "wheel"):
        dust = mul(remap(noise(15.0, 3.0, 0.6, offset=4.0), 0.25, 0.7, 0.45, 1.0), w_dust)
    else:
        dust = mul(mul(low, remap(noise(15.0, 3.0, 0.6, offset=4.0), 0.25, 0.7, 0.4, 1.0)), w_dust)
    cur = mix(dust, cur, rgb(DIRT["sand"]))
    rough_s = mixf(dust, rough_s, 0.93)
    if kind == "steel":
        rough_s = mixf(remap(noise(40.0, 2.0, 0.5, mapping=(8.0, 0.4, 8.0)), 0.3, 0.7, 0.0, 1.0), rough_s, rough + 0.14)
    return cur, rough_s, metal_s


DEVICE = "cpu"


def setup_device(scene):
    """Cycles on the CPU unless --device gpu (V2.bake_atlas calls V2.setup_gpu: patched in main())."""
    if DEVICE == "gpu":
        _V2_SETUP_GPU(scene)
    else:
        scene.cycles.device = "CPU"


_V2_SETUP_GPU = V2.setup_gpu


def procedural_materials():
    out = {}
    for name, m in list(_mats.items()):
        if name == "HOUSE":
            continue
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        c, r, mt = surface_tree(nt, name)
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        out[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
    return out


# share of the atlas per material: the plates the player looks at get the texels, the links / hidden
# cores / the untextured house parts little
UV_SCALE = dict(link=0.42, tread=0.45, core=0.3, tur_core=0.35, wheel=0.5, sill=0.5, HOUSE=0.06)


def uv_unwrap(objs, margin=0.003):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=margin, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in objs:
        me = o.data
        uv = me.uv_layers.active.data
        for poly in me.polygons:
            k = UV_SCALE.get(me.materials[poly.material_index].name, 1.0)
            if k != 1.0:
                for li in poly.loop_indices:
                    uv[li].uv = uv[li].uv * k
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(margin=margin, rotate=True, scale=True)
    bpy.ops.object.mode_set(mode="OBJECT")


# glTF factors of the atlas material: the textures hold metallic / roughness divided by these. The 3D
# renderer reads only the factors (model_gltf.cpp: gloss = 1 - roughnessFactor, metal = metallicFactor;
# a factor of 1, the exporter's default with a texture, would mean "bare metal, no highlight").
MR_FACTOR = dict(metal=0.3, rough=0.8)


def pack_mr(images, size):
    """glTF metallicRoughness texture: G = roughness / factor, B = metallic / factor."""
    import numpy as np
    r = np.array(images["rough"].pixels[:], np.float32).reshape(size, size, 4)
    m = np.array(images["metal"].pixels[:], np.float32).reshape(size, size, 4)
    mr = bpy.data.images.new("tank_mr", size, size, alpha=False, float_buffer=False)
    mr.colorspace_settings.name = "Non-Color"
    px = np.ones((size, size, 4), np.float32)
    px[..., 1] = np.clip(r[..., 0] / MR_FACTOR["rough"], 0, 1)
    px[..., 2] = np.clip(m[..., 0] / MR_FACTOR["metal"], 0, 1)
    mr.pixels.foreach_set(px.ravel())
    mr.update()
    return mr


def export_material(images, mr, scratch):
    """The single textured material of the export: base colour + packed metallic / roughness x factor."""
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
    for chan, sock, k in (("Green", "Roughness", MR_FACTOR["rough"]), ("Blue", "Metallic", MR_FACTOR["metal"])):
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = k
        nt.links.new(sep.outputs[chan], mul.inputs[0])
        nt.links.new(mul.outputs[0], b.inputs[sock])
    V2.link_emission(nt, b, images, scratch)
    return m


def bake_and_merge(objs, size, samples=24):
    """Bake all non-HOUSE materials into one atlas material "body" and remap the slots."""
    procs = procedural_materials()
    uv_unwrap(objs, margin=0.003)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = pack_mr(images, size)
    body_mat = export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_tank_v3_bake"))
    for o in objs:
        me = o.data
        for s in o.material_slots:
            if s.material and not s.material.name.startswith("HOUSE"):
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


def export_glb(root, path, only=None):
    """Export the model (or, with `only`, that node and its children: the standalone turret).
    COLOR_0 is written only where a material uses it (the HOUSE areas' shade)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    top = only if only is not None else root
    for o in [top] + list(top.children_recursive):
        o.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=True,
        export_normals=True, export_materials="EXPORT", export_image_format="AUTO", export_extras=False,
        export_tangents=False, export_texcoords=True, export_attributes=False,
        export_vertex_color="MATERIAL", export_all_vertex_colors=False)


# ------------------------------------------------------------------------------------------------
# Review: silhouettes with the game camera (IoU against the original), turntable and close-ups


def iou_report(root, npz_path, scale=4, elev=45.0):
    import numpy as np
    d = np.load(npz_path)
    ox, oy = (int(v) for v in d["origin"])
    n = d["body_opaque"].shape[1]
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (1, 1, 1)
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    size = n * scale
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    root.parent = yaw
    tur = next(o for o in root.children if o.name == "turret")
    tm0 = tur.matrix_basis.copy()
    meshes = V2.meshes_under(root)
    tur_meshes = set(V2.meshes_under(tur))
    tmp = os.path.join(bpy.app.tempdir, "d2k_iou_v3.png")
    cam = V2.game_camera(scene, elev, 32 * scale, size)
    alpha = {}
    for part in ("body", "turret"):
        for o in meshes:
            o.hide_render = (o in tur_meshes) != (part == "turret")
        frames = []
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
            frames.append(np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1, :, 3].copy())
            bpy.data.images.remove(img)
        alpha[part] = np.array(frames)
    tur.matrix_basis = tm0
    yaw.rotation_euler = (0, 0, 0)
    bpy.data.objects.remove(cam)
    for o in meshes:
        o.hide_render = False
    root.parent = None
    bpy.data.objects.remove(yaw)

    def down(a):
        return a.reshape(n, scale, n, scale).mean((1, 3)) > 0.5

    def placed(a, dx_tex, dy_px):
        """Render alpha (canvas centre = model origin) -> 1x mask on the original's canvas (anchor at ox, oy)."""
        s = np.roll(np.roll(a, dx_tex, 1), dy_px * scale + (oy - n // 2) * scale, 0)
        s = np.roll(s, (ox - n // 2) * scale, 1)
        return down(s)

    # vertical shift as render_pack: whole px from the mean body centroid difference
    ds = []
    for f in range(32):
        om = d["body_opaque"][f]
        ys, xs = np.nonzero(om)
        co = np.array([xs.mean() + 0.5 - ox, ys.mean() + 0.5 - oy])
        a = alpha["body"][f]
        w = a.sum()
        yy, xx = np.mgrid[0:size, 0:size]
        cr = np.array([((xx + 0.5) * a).sum() / w - size / 2, ((yy + 0.5) * a).sum() / w - size / 2]) / scale
        ds.append(cr - co)
    dmean = np.mean(ds, 0)
    shift = int(round(-dmean[1]))
    log(f"IOU: render centroid - original centroid = ({dmean[0]:+.2f}, {dmean[1]:+.2f}) px -> frames moved down {shift} px")
    res = {}
    for dx in (0, scale // 2):
        for part in ("body", "turret"):
            ious = []
            for f in range(32):
                r = placed(alpha[part][f], dx, shift)
                o = d[part + "_opaque"][f]
                ious.append((r & o).sum() / max(1, (r | o).sum()))
            res[(part, dx)] = ious
            log(f"IOU {part:6s} x+{dx / scale:.2f}px: mean {np.mean(ious):.3f} min {np.min(ious):.2f} | " + " ".join(f"{v:.2f}" for v in ious[::2]))
    return res


def review_renders(root, out_dir, samples, size=720):
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    for m in bpy.data.materials:
        if m.name.startswith("HOUSE") and m.use_nodes:
            tint = m.node_tree.nodes.get("HOUSE_TINT")
            if tint:
                tint.inputs[6].default_value = (*srgb_to_linear(PREVIEW_HOUSE), 1)
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -0.0005))
    gp = bpy.context.active_object
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*srgb_to_linear((0.77, 0.63, 0.43)), 1)
    gb.inputs["Roughness"].default_value = 1
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
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    scene.collection.objects.link(co)
    scene.camera = co
    tgt = Vector((0, 0, 0.2))

    def shot(name, az_deg, el_deg, dist, lens, target=tgt, sun_az=150.0, sun_el=52.0):
        a, e = math.radians(az_deg), math.radians(el_deg)
        pos = target + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * dist
        co.location = pos
        co.rotation_euler = (target - pos).to_track_quat("-Z", "Y").to_euler()
        cam.lens = lens
        sa, se = math.radians(sun_az), math.radians(sun_el)
        to_sun = Vector((math.cos(sa) * math.cos(se), math.sin(sa) * math.cos(se), math.sin(se)))
        so.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, name + ".png")
        bpy.ops.render.render(write_still=True)

    for i in range(8):
        az = -90 - 45 * i                                  # 0: from behind (the game's facing 0), then clockwise seen from above
        shot(f"turn_{i}", az, 30 if i % 2 == 0 else 22, 2.3, 70, sun_az=az - 60)
    scene.render.resolution_x, scene.render.resolution_y = 1400, 900
    shot("closeup_front", 55, 22, 2.35, 70, sun_az=120)
    shot("closeup_rear", -125, 26, 2.5, 70, sun_az=-170)
    shot("closeup_side", 0, 14, 2.4, 80, target=Vector((0, 0, 0.17)), sun_az=-55)
    shot("closeup_track", -20, 12, 1.25, 85, target=Vector((0.25, -0.3, 0.1)), sun_az=-70)
    shot("closeup_top", -90, 62, 2.6, 80, sun_az=-150, sun_el=58)
    bpy.data.objects.remove(gp)
    bpy.data.objects.remove(so)
    bpy.data.objects.remove(co)


# ------------------------------------------------------------------------------------------------


def main():
    global DEVICE
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=2048, iou=None, turntable=None, samples=64)
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
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--set":
            k, v = argv[i + 1].split("=", 1)
            if k not in P:
                raise SystemExit("unknown proportion " + k)
            P[k] = type(P[k])(eval(v)) if not isinstance(P[k], float) else float(v)   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None and opt["iou"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-fable-v3")

    V2.setup_gpu = setup_device           # the bake helpers of v2 pick the device through this
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root = build()
    objs = V2.meshes_under(root)
    log(f"built: {len(objs)} meshes, {V2.tri_count(objs)} triangles in {time.time() - t0:.1f} s")
    if opt["iou"]:
        iou_report(root, opt["iou"])
    if opt["bake"] and (opt["out"] or opt["turntable"]):
        t1 = time.time()
        bake_and_merge(objs, opt["bake_size"])
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
    if opt["out"]:
        unit_dir = os.path.join(opt["out"], "models", "unit")
        path = os.path.join(unit_dir, "ATREIDES_COMBAT_TANK.glb")
        export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        tur = next(o for o in root.children if o.name == "turret")
        tpath = os.path.join(unit_dir, "ATREIDES_COMBAT_TANK_turret.glb")
        export_glb(root, tpath, only=tur)
        log(f"exported {tpath} ({os.path.getsize(tpath) // 1024} KB)  (turret + barrel, pivot "
            f"{tuple(round(v, 4) for v in tur.matrix_world.translation)})")
        log("2D pack: tools/render_sprites/render_pack.py --models " + opt["out"] + " --out artpacks/rendered-fable-v3 "
            "--types ATREIDES_COMBAT_TANK " + RENDER_FLAGS)
    if opt["turntable"]:
        t2 = time.time()
        review_renders(root, opt["turntable"], opt["samples"])
        log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")


if __name__ == "__main__":
    main()
