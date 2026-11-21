"""Ground clutter kit (docs/3d-mode.md "Ground clutter"): the small meshes the Reimagined look scatters over the
terrain - pebbles, gravel patches, flat slabs, tiny dry tufts (the only things allowed on cells units use: all
under ~3 px), boulders, talus rubble piles, dry shrubs, dead wood and rare sun-bleached scrap (impassable cells
only). Writes the pack the game loads (render3d/ground_clutter.*):

    <out>/clutter.ini           manifest: texture layers and pieces (class, layer, height / width)
    <out>/<piece>.glb           LOD 0 (positions, normals, UV 0; 1 unit = the piece's width, ground at y = 0)
    <out>/<piece>_lod1.glb      LOD 1 (about a quarter of the triangles)
    <out>/tex/<layer>.png       512 x 512 albedo per texture layer (scan diffuse x its ambient occlusion)
    <out>/LICENSES.md           every source with its URL and licence

Sources: Poly Haven CC0 scans (glTF 1k, downloaded by the caller into --src, one folder dl_<asset id> each: see
tools/models/clutter_fetch.py) - decimated, normalised, some combined into gravel patches / rubble piles - and
procedural pieces generated here (shrubs, tufts, scrap) with procedural textures. Nothing is game data.

    blender -b --factory-startup -P tools/models/clutter_kit.py -- --src ~/OpenD2K-assets/clutter-src \
        --out artpacks/ground-clutter/clutter

Blender 5.x, CPU only (no rendering). Deterministic (fixed seeds).
"""
import argparse
import glob
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

TEX = 512

# Poly Haven sources (all CC0 1.0; https://polyhaven.com/license).
SOURCES = {
    "namaqualand_stones_01": "Namaqualand Stones 01",
    "namaqualand_rocks_01": "Namaqualand Rocks 01",
    "namaqualand_boulders_01": "Namaqualand Boulders 01",
    "rock_07": "Rock 07",
    "rock_09": "Rock 09",
    "stone_01": "Stone 01",
    "dead_quiver_branch_01": "Dead Quiver Branch 01",
    "dead_quiver_branch_02": "Dead Quiver Branch 02",
    "dry_branches_medium_01": "Dry Branches Medium 01",
}

# Texture layers: (file stem, material kind, source asset or None = procedural).
LAYERS = [
    ("stones", "rock", "namaqualand_stones_01"),
    ("rocks", "rock", "namaqualand_rocks_01"),
    ("boulders", "rock", "namaqualand_boulders_01"),
    ("rock07", "rock", "rock_07"),
    ("rock09", "rock", "rock_09"),
    ("stone01", "rock", "stone_01"),
    ("deadwood_a", "wood", "dead_quiver_branch_01"),
    ("deadwood_b", "wood", "dead_quiver_branch_02"),
    ("drybranch", "wood", "dry_branches_medium_01"),
    ("plant", "plant", None),  # procedural: left half bark, right half dry foliage / straw
    ("rust", "metal", None),   # procedural: rust, flaking sun-bleached paint
]
LAYER = {name: i for i, (name, _, _) in enumerate(LAYERS)}


def log(*a):
    print("[clutter]", *a, flush=True)


# ------------------------------------------------------------------------------------------------ scene helpers
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_gltf(src, asset):
    files = glob.glob(os.path.join(src, f"dl_{asset}", "*.gltf"))
    if not files:
        raise SystemExit(f"missing {asset} in {src} (run tools/models/clutter_fetch.py)")
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=files[0])
    objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    objs.sort(key=lambda o: o.name)
    for o in objs:
        o.parent = None
    return objs


def bake_transform(o):
    o.data.transform(o.matrix_world)
    o.matrix_world = Matrix.Identity(4)


def weld(o):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    size = max(o.dimensions) if max(o.dimensions) > 0 else 1.0
    # glTF splits vertices along UV seams: weld positions (UVs live on the loops, they are kept).
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=size * 1e-5)
    bm.to_mesh(o.data)
    bm.free()


def tri_count(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def decimate(o, target):
    t = tri_count(o)
    if t <= target:
        return
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("dec", "DECIMATE")
    m.decimate_type = "COLLAPSE"
    m.ratio = max(0.001, target / t)
    m.use_collapse_triangulate = True
    with bpy.context.temp_override(object=o, active_object=o):
        bpy.ops.object.modifier_apply(modifier=m.name)


def copy_obj(o, name):
    c = o.copy()
    c.data = o.data.copy()
    c.name = name
    bpy.context.scene.collection.objects.link(c)
    return c


def join(objs, name):
    ctx = bpy.context
    for o in ctx.scene.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    ctx.view_layer.objects.active = objs[0]
    with ctx.temp_override(active_object=objs[0], selected_editable_objects=objs):
        bpy.ops.object.join()
    o = objs[0]
    o.name = name
    return o


def bounds(o):
    vs = [v.co for v in o.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return lo, hi


def normalise(o, sink=0.0, ground=None):
    """Centre on X / Y, scale the larger horizontal extent to 1. ground None: the lowest point goes to
    z = -sink * height (sunk into the ground); else the given z is the ground (composites built on z = 0)."""
    lo, hi = bounds(o)
    size = max(hi.x - lo.x, hi.y - lo.y)
    base = lo.z + sink * (hi.z - lo.z) if ground is None else ground
    c = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, base))
    for v in o.data.vertices:
        v.co = (v.co - c) / size
    o.data.update()
    lo, hi = bounds(o)
    return hi.z  # height above the ground / width


def flatten(o, f):
    for v in o.data.vertices:
        v.co.z *= f
    o.data.update()


def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True


def export(o, path):
    ctx = bpy.context
    for x in ctx.scene.objects:
        x.select_set(x == o)
    ctx.view_layer.objects.active = o
    o.data.materials.clear()
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_materials="NONE",
                              export_texcoords=True, export_normals=True, export_yup=True, export_apply=True)


# ------------------------------------------------------------------------------------------------ textures
def load_px(path, size=TEX):
    img = bpy.data.images.load(path)
    img.scale(size, size)
    a = np.array(img.pixels[:], dtype=np.float32).reshape(size, size, 4)
    bpy.data.images.remove(img)
    return a


def save_png(path, rgb):
    h, w = rgb.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=True)
    a = np.ones((h, w, 4), dtype=np.float32)
    a[..., :3] = np.clip(rgb, 0, 1)
    img.pixels.foreach_set(a.ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def scan_texture(src, asset, out):
    d = os.path.join(src, f"dl_{asset}", "textures")
    diff = load_px(glob.glob(os.path.join(d, "*_diff_1k.jpg"))[0])
    arm = glob.glob(os.path.join(d, "*_arm_1k.jpg"))
    rgb = diff[..., :3]
    if arm:
        ao = load_px(arm[0])[..., 0:1]
        rgb = rgb * (0.35 + 0.65 * ao)  # the cavities of the scan, darkened into the colour
    save_png(out, rgb)


def fbm(size, octaves, seed, base=4):
    rng = np.random.default_rng(seed)
    acc = np.zeros((size, size), np.float32)
    amp, tot = 1.0, 0.0
    for k in range(octaves):
        n = base * (2 ** k)
        g = rng.random((n + 1, n + 1)).astype(np.float32)
        g[-1, :] = g[0, :]
        g[:, -1] = g[:, 0]  # tileable
        t = np.linspace(0, n, size, endpoint=False)
        i = t.astype(int)
        f = t - i
        f = f * f * (3 - 2 * f)
        a = g[i][:, i] * (1 - f)[None, :] + g[i][:, i + 1] * f[None, :]
        b = g[i + 1][:, i] * (1 - f)[None, :] + g[i + 1][:, i + 1] * f[None, :]
        acc += amp * (a * (1 - f)[:, None] + b * f[:, None])
        tot += amp
        amp *= 0.5
    return acc / tot


def plant_texture(out):
    s = TEX
    rgb = np.zeros((s, s, 3), np.float32)
    # Left half: weathered grey-brown bark, streaks along v (the branch).
    streak = fbm(s, 4, 11, base=3)
    streak = np.repeat(streak[:, :1], s, axis=1) * 0.5 + fbm(s, 5, 12, base=8) * 0.5  # vertical grain
    grain = np.clip(0.55 + (np.roll(fbm(s, 6, 13, base=16), 0, 0) - 0.5) * 0.9 + (streak - 0.5) * 0.8, 0, 1)
    bark = np.stack([0.30 * grain + 0.07, 0.26 * grain + 0.06, 0.22 * grain + 0.05], -1)
    # Right half: dry, dead grey-brown foliage / straw (no green), speckled.
    sp = fbm(s, 6, 21, base=12)
    fol = np.stack([0.42 + 0.16 * (sp - 0.5), 0.37 + 0.14 * (sp - 0.5), 0.29 + 0.11 * (sp - 0.5)], -1)
    dark = (fbm(s, 4, 22, base=24) > 0.62)[..., None]
    fol = np.where(dark, fol * 0.62, fol)
    half = s // 2
    rgb[:, :half] = bark[:, :half]
    rgb[:, half:] = fol[:, half:]
    save_png(out, rgb)


def rust_texture(out):
    s = TEX
    n1, n2, n3 = fbm(s, 5, 31, base=4), fbm(s, 6, 32, base=10), fbm(s, 4, 33, base=20)
    rust = np.stack([0.46 + 0.14 * (n2 - 0.5), 0.25 + 0.08 * (n2 - 0.5), 0.13 + 0.05 * (n2 - 0.5)], -1)
    darkrust = np.stack([0.22, 0.12, 0.07]) * (0.8 + 0.4 * n3[..., None])
    paint = np.stack([0.62, 0.59, 0.53]) * (0.85 + 0.25 * n3[..., None])  # sun-bleached old paint
    rgb = np.where((n1 > 0.58)[..., None], darkrust, rust)
    rgb = np.where((n1 < 0.33)[..., None] & (n2 > 0.45)[..., None], paint, rgb)
    save_png(out, rgb)


# ------------------------------------------------------------------------------------------------ procedural meshes
def new_obj(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def perp(d):
    a = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
    u = d.cross(a).normalized()
    return u, d.cross(u).normalized()


def tube(bm, uvl, pts, radii, sides, u0, u1, vscale):
    """A tapered tube through pts (open ends). UVs: u across [u0, u1] around, v along (scaled)."""
    rings = []
    for k, p in enumerate(pts):
        d = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]).normalized()
        a, b = perp(d)
        ring = []
        for s in range(sides):
            ang = 2 * math.pi * s / sides
            ring.append(bm.verts.new(p + (a * math.cos(ang) + b * math.sin(ang)) * radii[k]))
        rings.append(ring)
    vacc = 0.0
    vs = [0.0]
    for k in range(1, len(pts)):
        vacc += (pts[k] - pts[k - 1]).length * vscale
        vs.append(vacc)
    for k in range(len(pts) - 1):
        for s in range(sides):
            s2 = (s + 1) % sides
            f = bm.faces.new((rings[k][s], rings[k][s2], rings[k + 1][s2], rings[k + 1][s]))
            us = [u0 + (u1 - u0) * s / sides, u0 + (u1 - u0) * (s + 1) / sides]
            for loop, (uu, vv) in zip(f.loops, ((us[0], vs[k]), (us[1], vs[k]), (us[1], vs[k + 1]), (us[0], vs[k + 1]))):
                loop[uvl].uv = (uu, vv)
    return rings


def blob(bm, uvl, c, r, rng, squash=0.7, u0=0.5, u1=1.0):
    """A small irregular clump (icosahedron, 20 triangles) - dry foliage."""
    res = bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0, matrix=Matrix.Identity(4), calc_uvs=False)
    vs = res["verts"]
    for v in vs:
        k = 0.7 + 0.5 * rng.random()
        v.co = Vector((v.co.x * r * k, v.co.y * r * k, v.co.z * r * k * squash)) + c
    faces = {f for v in vs for f in v.link_faces}
    for f in faces:
        for loop in f.loops:
            d = (loop.vert.co - c)
            ang = math.atan2(d.y, d.x) / (2 * math.pi) + 0.5
            loop[uvl].uv = (u0 + (u1 - u0) * ang, 0.5 + 0.5 * d.z / max(r, 1e-6))


def shrub(name, seed, depth, clumps=True):
    """A dry desert shrub: a low, wide tangle of grey-brown twigs from one root, dead foliage clumps at the
    tips. Bark on the left half of the plant layer, foliage on the right half. Ground at z = 0."""
    rng = random.Random(seed)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    tips = []

    def grow(p, d, length, radius, level):
        n = 3
        pts = [p]
        dd = d.copy()
        for k in range(n):
            dd = (dd + Vector((rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), rng.uniform(-0.1, 0.15)))).normalized()
            pts.append(pts[-1] + dd * (length / n))
        radii = [radius * (1 - 0.45 * k / n) for k in range(n + 1)]
        uo = rng.random() * 0.3
        tube(bm, uvl, pts, radii, 4 if level >= 2 else 3, uo, uo + 0.18, 3.0)
        end = pts[-1]
        if level <= 0:
            tips.append(end)
            return
        kids = rng.choice((2, 2, 3))
        for _ in range(kids):
            a, b = perp(dd)
            ang = rng.uniform(0, 2 * math.pi)
            tilt = rng.uniform(0.35, 0.8)
            nd = (dd * math.cos(tilt) + (a * math.cos(ang) + b * math.sin(ang)) * math.sin(tilt)).normalized()
            if nd.z < 0.05:
                nd.z = 0.05 + rng.random() * 0.2
                nd.normalize()
            grow(end, nd, length * rng.uniform(0.6, 0.78), radius * 0.62, level - 1)

    stems = rng.choice((4, 5, 6))
    for s in range(stems):
        ang = 2 * math.pi * (s + rng.random() * 0.6) / stems
        up = rng.uniform(0.55, 1.0)
        d = Vector((math.cos(ang), math.sin(ang), up)).normalized()
        grow(Vector((rng.uniform(-0.02, 0.02), rng.uniform(-0.02, 0.02), -0.05)), d, rng.uniform(0.22, 0.3), 0.022, depth)
    if clumps:
        for t in tips:
            if rng.random() < 0.75:
                blob(bm, uvl, t + Vector((0, 0, rng.uniform(-0.01, 0.02))), rng.uniform(0.05, 0.09), rng)
    o = new_obj(name, bm)
    return o


def tuft(name, seed):
    """A tiny dry grass tuft: thin curved straw blades from one point (foliage half of the plant layer)."""
    rng = random.Random(seed)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    blades = rng.choice((7, 8, 9, 10))
    for b in range(blades):
        ang = 2 * math.pi * (b + rng.random() * 0.8) / blades
        lean = rng.uniform(0.35, 1.0)
        L = rng.uniform(0.45, 0.7)
        w = rng.uniform(0.035, 0.05)
        d = Vector((math.cos(ang) * lean, math.sin(ang) * lean, 1.0)).normalized()
        side = Vector((-math.sin(ang), math.cos(ang), 0)) * w
        root = Vector((math.cos(ang), math.sin(ang), 0)) * 0.04
        mid = root + d * L * 0.5
        tip = root + d * L + Vector((math.cos(ang), math.sin(ang), -0.25)) * L * 0.35 * lean
        v = [bm.verts.new(root - side), bm.verts.new(root + side), bm.verts.new(mid + side * 0.6),
             bm.verts.new(mid - side * 0.6), bm.verts.new(tip)]
        u0 = 0.55 + 0.4 * rng.random()
        f1 = bm.faces.new((v[0], v[1], v[2], v[3]))
        for loop, uv in zip(f1.loops, ((u0, 0.1), (u0 + 0.03, 0.1), (u0 + 0.03, 0.5), (u0, 0.5))):
            loop[uvl].uv = uv
        f2 = bm.faces.new((v[3], v[2], v[4]))
        for loop, uv in zip(f2.loops, ((u0, 0.5), (u0 + 0.03, 0.5), (u0 + 0.015, 0.9))):
            loop[uvl].uv = uv
    return new_obj(name, bm)


def scrap_plate(name, seed):
    """A bent, torn metal sheet lying half-buried (rust layer). Ground at z = 0."""
    rng = random.Random(seed)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    nx, ny = 7, 5
    grid = []
    bend = rng.uniform(0.6, 1.4)
    for j in range(ny):
        row = []
        for i in range(nx):
            x = i / (nx - 1) - 0.5
            y = (j / (ny - 1) - 0.5) * 0.6
            if i in (0, nx - 1) or j in (0, ny - 1):  # torn edges
                x += rng.uniform(-0.05, 0.05)
                y += rng.uniform(-0.04, 0.04)
            z = bend * x * x * 0.35 + rng.uniform(-0.01, 0.01)
            row.append(bm.verts.new((x, y, z)))
        grid.append(row)
    for j in range(ny - 1):
        for i in range(nx - 1):
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            for loop in f.loops:
                loop[uvl].uv = (loop.vert.co.x + 0.5, loop.vert.co.y + 0.5)
    o = new_obj(name, bm)
    sol = o.modifiers.new("sol", "SOLIDIFY")
    sol.thickness = 0.025
    with bpy.context.temp_override(object=o, active_object=o):
        bpy.ops.object.modifier_apply(modifier=sol.name)
    # tilted into the sand
    o.data.transform(Matrix.Rotation(rng.uniform(0.35, 0.6), 4, "Y") @ Matrix.Rotation(rng.uniform(-0.2, 0.2), 4, "X"))
    o.data.transform(Matrix.Translation((0, 0, 0.02)))
    return o


def scrap_ribs(name, seed):
    """The rib cage of a long-dead machine half-sunk into the sand: a bent spine with arched ribs (rust)."""
    rng = random.Random(seed)
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    spine = [Vector((x, 0.03 * math.sin(x * 5), 0.06 + 0.02 * rng.random())) for x in np.linspace(-0.5, 0.5, 7)]
    tube(bm, uvl, spine, [0.035] * len(spine), 5, 0.0, 0.5, 2.0)
    ribs = 6
    for r in range(ribs):
        x = -0.42 + 0.84 * r / (ribs - 1) + rng.uniform(-0.02, 0.02)
        broken = rng.random() < 0.35
        span = math.pi * (0.55 if broken else 1.0)
        rad = rng.uniform(0.17, 0.24) * (1.0 - 0.25 * abs(x))
        pts = []
        for k in range(8):
            a = -math.pi / 2 + span * k / 7
            pts.append(Vector((x + 0.03 * math.sin(a), math.sin(a) * rad, 0.06 + math.cos(a) * rad * 0.9)))
        tube(bm, uvl, pts, [0.022] * len(pts), 4, 0.5, 1.0, 2.0)
    o = new_obj(name, bm)
    o.data.transform(Matrix.Rotation(rng.uniform(-0.15, 0.15), 4, "X") @ Matrix.Translation((0, 0, -0.08)))
    return o


# ------------------------------------------------------------------------------------------------ main
def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    src, out = os.path.expanduser(a.src), os.path.expanduser(a.out)
    os.makedirs(os.path.join(out, "tex"), exist_ok=True)

    reset()
    for name, kind, asset in LAYERS:
        p = os.path.join(out, "tex", name + ".png")
        if asset:
            scan_texture(src, asset, p)
        elif name == "plant":
            plant_texture(p)
        else:
            rust_texture(p)
        log("texture", name)

    pieces = []  # (name, layer, class, height/width, tris lod0, tris lod1)

    def emit(o, name, layer, cls, lod0, lod1, sink=0.0, ground=None, flat=None, lay=False):
        bake_transform(o)
        if lay:  # a branch scanned standing up: lay it on the ground (long axis horizontal)
            lo, hi = bounds(o)
            if hi.z - lo.z > max(hi.x - lo.x, hi.y - lo.y):
                o.data.transform(Matrix.Rotation(math.pi / 2, 4, "X"))
        if flat:
            flatten(o, flat)
        decimate(o, lod0)
        h = normalise(o, sink=sink, ground=ground)
        smooth(o)
        l1 = copy_obj(o, name + "_lod1")
        decimate(l1, lod1)
        export(o, os.path.join(out, name + ".glb"))
        export(l1, os.path.join(out, name + "_lod1.glb"))
        pieces.append((name, layer, cls, h, tri_count(o), tri_count(l1)))
        log(name, cls, "h/w %.3f" % h, "tris", tri_count(o), tri_count(l1))
        bpy.data.objects.remove(l1)
        bpy.data.objects.remove(o)

    def scan_parts(asset):
        reset()
        objs = import_gltf(src, asset)
        for o in objs:
            bake_transform(o)
            weld(o)
        return objs

    # Pebbles (single stones) + gravel patches (several small stones on z = 0, partly sunk).
    stones = scan_parts("namaqualand_stones_01")
    small = []
    for k, o in enumerate(stones):
        s = copy_obj(o, f"pebble_src_{k}")
        decimate(s, 24)
        small.append(s)
    for k, o in enumerate(stones):
        emit(o, f"pebble_{'abcde'[k]}", LAYER["stones"], "pebble", 80, 16, sink=0.2)
    for g, (n, seed) in enumerate(((16, 1), (11, 2))):
        rng = random.Random(seed)
        parts = []
        for i in range(n):
            s = copy_obj(small[rng.randrange(len(small))], f"g{g}_{i}")
            lo, hi = bounds(s)
            sc = rng.uniform(0.12, 0.26) / max(hi.x - lo.x, hi.y - lo.y)
            r = 0.42 * math.sqrt(rng.random())
            ang = rng.uniform(0, 2 * math.pi)
            m = (Matrix.Translation((r * math.cos(ang), r * math.sin(ang), 0)) @ Matrix.Rotation(rng.uniform(0, 6.3), 4, "Z") @
                 Matrix.Diagonal((sc, sc, sc * 0.7, 1.0)) @ Matrix.Translation((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z)))
            s.data.transform(m)
            lo2, hi2 = bounds(s)
            s.data.transform(Matrix.Translation((0, 0, -(hi2.z - lo2.z) * rng.uniform(0.3, 0.5))))
            parts.append(s)
        o = join(parts, f"gravel_{'ab'[g]}")
        emit(o, f"gravel_{'ab'[g]}", LAYER["stones"], "gravel", 400, 70, ground=0.0)
    for o in small:
        bpy.data.objects.remove(o)

    # Rocks: boulders (impassable), flat slabs (passable: flattened), rubble piles (impassable).
    rocks = scan_parts("namaqualand_rocks_01")
    pile_src = []
    for k, o in enumerate(rocks):
        s = copy_obj(o, f"pile_src_{k}")
        decimate(s, 70)
        pile_src.append(s)
    for k in (1, 2):
        s = copy_obj(rocks[k], f"slab_src_{k}")
        emit(s, f"slab_{'ab'[k - 1]}", LAYER["rocks"], "slab", 100, 24, sink=0.15, flat=0.32)
    for k, o in enumerate(rocks):
        emit(o, f"boulder_{'abcd'[k]}", LAYER["rocks"], "boulder", 500, 64, sink=0.12)
    for g, (n, seed) in enumerate(((11, 5), (8, 6))):
        rng = random.Random(seed)
        parts = []
        for i in range(n):
            s = copy_obj(pile_src[rng.randrange(len(pile_src))], f"r{g}_{i}")
            lo, hi = bounds(s)
            r = 0.4 * math.sqrt(rng.random())
            sc = (0.42 - 0.45 * r + rng.uniform(-0.05, 0.05)) / max(hi.x - lo.x, hi.y - lo.y)  # bigger in the middle
            ang = rng.uniform(0, 2 * math.pi)
            m = (Matrix.Translation((r * math.cos(ang), r * math.sin(ang), 0)) @
                 Matrix.Rotation(rng.uniform(-0.5, 0.5), 4, "X") @ Matrix.Rotation(rng.uniform(0, 6.3), 4, "Z") @
                 Matrix.Diagonal((sc, sc, sc, 1.0)) @ Matrix.Translation((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z)))
            s.data.transform(m)
            lo2, hi2 = bounds(s)
            s.data.transform(Matrix.Translation((0, 0, -(hi2.z - lo2.z) * rng.uniform(0.15, 0.35))))
            parts.append(s)
        o = join(parts, f"rubble_{'ab'[g]}")
        emit(o, f"rubble_{'ab'[g]}", LAYER["rocks"], "rubble", 700, 110, ground=0.0)
    for o in pile_src:
        bpy.data.objects.remove(o)

    for asset, layer, names in (("namaqualand_boulders_01", "boulders", ("boulder_e", "boulder_f")),
                                ("rock_07", "rock07", ("boulder_g",)),
                                ("stone_01", "stone01", ("boulder_h",))):
        objs = scan_parts(asset)
        for o, n in zip(objs, names):
            emit(o, n, LAYER[layer], "boulder", 500, 64, sink=0.12)
    objs = scan_parts("rock_09")
    emit(objs[0], "slab_c", LAYER["rock09"], "slab", 100, 24, sink=0.15, flat=0.4)

    # Dead wood (impassable cells): the scanned dead branches lying on the ground.
    for asset, layer, name in (("dead_quiver_branch_01", "deadwood_a", "deadwood_a"),
                               ("dead_quiver_branch_02", "deadwood_b", "deadwood_b")):
        objs = scan_parts(asset)
        o = objs[0] if len(objs) == 1 else join(objs, name)
        emit(o, name, LAYER[layer], "deadwood", 400, 70, sink=0.1, lay=True)
    objs = scan_parts("dry_branches_medium_01")
    emit(objs[0], "deadwood_c", LAYER["drybranch"], "deadwood", 350, 60, sink=0.1)

    # Procedural: dry shrubs, straw tufts, scrap.
    reset()
    for k, seed in enumerate((101, 102, 103)):
        o = shrub(f"shrub_{'abc'[k]}", seed, 2)
        emit(o, f"shrub_{'abc'[k]}", LAYER["plant"], "shrub", 700, 130, ground=0.0)
    for k, seed in enumerate((201, 202)):
        o = tuft(f"tuft_{'ab'[k]}", seed)
        emit(o, f"tuft_{'ab'[k]}", LAYER["plant"], "tuft", 120, 40, ground=0.0)
    o = scrap_plate("scrap_a", 301)
    emit(o, "scrap_a", LAYER["rust"], "scrap", 250, 50, ground=0.0)
    o = scrap_ribs("scrap_b", 302)
    emit(o, "scrap_b", LAYER["rust"], "scrap", 500, 90, ground=0.0)

    with open(os.path.join(out, "clutter.ini"), "w") as f:
        f.write("# OpenD2K ground clutter pack (tools/models/clutter_kit.py; render3d/ground_clutter.cpp)\n")
        f.write("# layer <index> <tex/file.png> <material: rock|wood|plant|metal>\n")
        f.write("# piece <name> <layer> <class> <height / width> <triangles lod0> <triangles lod1>\n")
        for i, (name, kind, _) in enumerate(LAYERS):
            f.write(f"layer {i} tex/{name}.png {kind}\n")
        for p in pieces:
            f.write("piece %s %d %s %.4f %d %d\n" % p)
    with open(os.path.join(out, "LICENSES.md"), "w") as f:
        f.write("# Ground clutter pack: sources and licences\n\n")
        f.write("Built by tools/models/clutter_kit.py (OpenD2K). The pack as a whole is part of the OpenD2K art and "
                "follows its licence (CC BY-SA 4.0); every external source below is CC0 1.0 (public domain "
                "dedication, https://polyhaven.com/license) - no attribution required, listed anyway.\n\n")
        f.write("| Source (Poly Haven, CC0) | URL | Used for |\n|---|---|---|\n")
        use = {"namaqualand_stones_01": "pebble_a-e, gravel_a/b (texture layer stones)",
               "namaqualand_rocks_01": "boulder_a-d, slab_a/b, rubble_a/b (layer rocks)",
               "namaqualand_boulders_01": "boulder_e/f (layer boulders)",
               "rock_07": "boulder_g (layer rock07)", "rock_09": "slab_c (layer rock09)",
               "stone_01": "boulder_h (layer stone01)", "dead_quiver_branch_01": "deadwood_a",
               "dead_quiver_branch_02": "deadwood_b", "dry_branches_medium_01": "deadwood_c (one of the three branches)"}
        for asset, title in SOURCES.items():
            f.write(f"| {title} | https://polyhaven.com/a/{asset} | {use[asset]} |\n")
        f.write("\nProcedural (generated by the kit, no external source): shrub_a-c, tuft_a/b, scrap_a/b and the "
                "texture layers plant.png and rust.png.\n\nScans: glTF 1k downloads, decimated, normalised; the "
                "albedo is the 1k diffuse x its ambient occlusion, resized to 512 px.\n")
    log("done:", len(pieces), "pieces")


main()
