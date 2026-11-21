"""Reimagined models kit (docs/3d-models.md "Reimagined models"): takes a model built by one of the approved
generators (combat_tank_v3.py, the Atreides building kit) - the SAME design - and gives it the Reimagined
fidelity for Graphics: 3D with the Reimagined look:

  * one atlas for everything, house-colour parts included (weathered like the rest; the house colour is a MASK
    the engine tints, so dirt and scratches lie over the paint),
  * PBR maps: base colour, an ORM map (R ambient occlusion, G roughness, B metallic, A house-colour mask - the
    alpha is our convention) and a tangent-space normal map,
  * surface detail computed per texel from baked position / normal / tangent maps (numpy, no extra geometry):
    rounded edges that catch the light (Cycles bevel normal), panel lines on painted plates (a lattice of
    planes broken into panels, dark grime in the seams), rivet rows beside some seams, a few dents,
    edge wear (lighter, smoother), roughness and occlusion that follow the detail,
  * a simplified version KEY_lod1.glb (Decimate, same nodes / UVs / textures) for small views.

Used by reimagined_combat_tank.py and reimagined_buildings.py; Blender 5.2, CPU Cycles (see the tank v3 README:
the Metal kernel compile crashed). Nothing here is game data: everything is generated.

The glb is written by the generators' own exporter (V3.export_glb) and then patched here (glb_patch): the base
colour image is replaced by the detailed albedo, the metallicRoughness image by the ORM map (factors 1, the
same texture as occlusionTexture), and a normalTexture is added. Emission (atreides_kit glows) stays as exported.
"""
import json
import math
import os
import struct
import sys
import time
import zlib

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402

log = V2.log

# Detail parameters per model family (tiles; 1 px = 1/32 tile). Overridden by the generators.
DETAIL = dict(
    panel=0.19,          # panel lattice spacing
    panel_offset=(0.37, 0.11, 0.23),
    panel_keep=0.55,     # share of panel-line segments kept
    panel_aniso=(1.0, 1.35, 0.8),  # spacing per axis (x, y, z): oblong panels, not a square grid
    groove_w=0.0055,     # seam half width
    groove_depth=0.0028,
    rivet_pitch=0.032,   # along a seam
    rivet_r=0.0055,
    rivet_keep=0.4,      # share of seams with a rivet row
    bevel_r=0.012,       # rounded edges (Cycles bevel radius)
    dent=0.35,           # strength of the dents / hammered texture
    wear=0.55,           # edge wear (lighter paint on exposed edges)
    ao_distance=0.16,
    house_uv=0.7,        # atlas share of the house-colour parts (V3.UV_SCALE["HOUSE"])
)
# Materials that get panel lines / rivets (painted plates); the rest only the bevels and wear.
PANEL_KINDS = ("paint",)


# ------------------------------------------------------------------------------------------------
# PNG / glb


def write_png(path, arr):
    """arr: uint8 H x W x C (C 3 or 4), row 0 = top."""
    import numpy as np
    arr = np.ascontiguousarray(arr, dtype=np.uint8)
    h, w, c = arr.shape
    raw = np.zeros((h, 1 + w * c), np.uint8)
    raw[:, 1:] = arr.reshape(h, w * c)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if c == 4 else 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw.tobytes(), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return png


def read_glb(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + n])
    o = 20 + n
    bl = struct.unpack("<I", d[o:o + 4])[0]
    return j, d[o + 8:o + 8 + bl]


def write_glb(path, j, views):
    """views: list of bytes, one per bufferView (j["bufferViews"] rewritten to pack them)."""
    bin_ = bytearray()
    for i, b in enumerate(views):
        while len(bin_) % 4:
            bin_ += b"\0"
        j["bufferViews"][i]["byteOffset"] = len(bin_)
        j["bufferViews"][i]["byteLength"] = len(b)
        bin_ += b
    while len(bin_) % 4:
        bin_ += b"\0"
    j["buffers"] = [dict(byteLength=len(bin_))]
    js = json.dumps(j, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_), 0x004E4942) + bytes(bin_)
    with open(path, "wb") as f:
        f.write(out)


def glb_patch(path, color_png, orm_png, normal_png):
    """The PBR maps into the exported glb (see the module docstring)."""
    j, b = read_glb(path)
    views = [b[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]] for v in j["bufferViews"]]
    mats = j.get("materials", [])
    tex = None
    for m in mats:
        pbr = m.get("pbrMetallicRoughness", {})
        if "baseColorTexture" in pbr:
            tex = m
            break
    if tex is None:
        raise SystemExit("glb_patch: no textured material in " + path)
    pbr = tex["pbrMetallicRoughness"]

    def image_of(ti):
        return j["textures"][ti]["source"]

    def replace(img_index, data):
        views[j["images"][img_index]["bufferView"]] = data
        j["images"][img_index]["mimeType"] = "image/png"

    def add_image(name, data):
        views.append(data)
        j["bufferViews"].append(dict(buffer=0, byteLength=len(data)))
        j["images"].append(dict(name=name, mimeType="image/png", bufferView=len(j["bufferViews"]) - 1))
        samp = j["textures"][pbr["baseColorTexture"]["index"]].get("sampler")
        t = dict(source=len(j["images"]) - 1)
        if samp is not None:
            t["sampler"] = samp
        j["textures"].append(t)
        return len(j["textures"]) - 1

    replace(image_of(pbr["baseColorTexture"]["index"]), color_png)
    if "metallicRoughnessTexture" in pbr:
        mr = pbr["metallicRoughnessTexture"]["index"]
        replace(image_of(mr), orm_png)
    else:
        mr = add_image("rm_orm", orm_png)
        pbr["metallicRoughnessTexture"] = dict(index=mr)
    pbr["metallicFactor"] = 1.0
    pbr["roughnessFactor"] = 1.0
    tex["occlusionTexture"] = dict(index=mr)
    tex["normalTexture"] = dict(index=add_image("rm_normal", normal_png))
    tex.setdefault("extras", {})["d2k_orm_alpha"] = "house-colour mask"
    write_glb(path, j, views)


# ------------------------------------------------------------------------------------------------
# Bakes


def _out_node(nt):
    return next(n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial")


def bake_pass(objs, procs, socket_of, name, size, samples=1, float_buffer=True, margin=8):
    """EMIT bake of socket_of(proc) of every material into a new image; returns H x W x 4 float (row 0 = v 0)."""
    import numpy as np
    img = bpy.data.images.new(name, size, size, alpha=False, float_buffer=float_buffer)
    img.colorspace_settings.name = "Non-Color"
    img.generated_color = (0, 0, 0, 1)
    for pr in procs.values():
        nt = pr["mat"].node_tree
        out = _out_node(nt)
        em = pr["emit"]
        for l in list(out.inputs["Surface"].links):
            nt.links.remove(l)
        for l in list(em.inputs["Color"].links):
            nt.links.remove(l)
        nt.links.new(socket_of(pr), em.inputs["Color"])
        nt.links.new(em.outputs[0], out.inputs["Surface"])
        t = nt.nodes.get("BAKE_TARGET") or nt.nodes.new("ShaderNodeTexImage")
        t.name = "BAKE_TARGET"
        t.image = img
        nt.nodes.active = t
    scene = bpy.context.scene
    scene.cycles.samples = samples
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    t0 = time.time()
    bpy.ops.object.bake(type="EMIT", margin=margin, use_clear=True)
    a = np.array(img.pixels[:], np.float32).reshape(size, size, 4)
    bpy.data.images.remove(img)
    log(f"  baked {name} in {time.time() - t0:.1f} s")
    return a


def bake_ao(objs, procs, size, distance, samples=48):
    import numpy as np
    scene = bpy.context.scene
    if scene.world is None:
        scene.world = bpy.data.worlds.new("w")
    scene.world.light_settings.distance = distance
    img = bpy.data.images.new("rm_ao", size, size, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    for pr in procs.values():
        nt = pr["mat"].node_tree
        out = _out_node(nt)
        b = nt.nodes["Principled BSDF"]
        for l in list(out.inputs["Surface"].links):
            nt.links.remove(l)
        nt.links.new(b.outputs[0], out.inputs["Surface"])
        t = nt.nodes.get("BAKE_TARGET") or nt.nodes.new("ShaderNodeTexImage")
        t.name = "BAKE_TARGET"
        t.image = img
        nt.nodes.active = t
    scene.cycles.samples = samples
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    t0 = time.time()
    bpy.ops.object.bake(type="AO", margin=8, use_clear=True)
    a = np.array(img.pixels[:], np.float32).reshape(size, size, 4)[..., 0].copy()
    bpy.data.images.remove(img)
    log(f"  baked ao in {time.time() - t0:.1f} s")
    return a


def aux_sockets(pr, mat_id, bevel_r):
    """Extra outputs per material for the detail bakes: position, normal, tangent, bevel normal (all encoded
    to 0..1), material id, the "Col" shade attribute."""
    nt = pr["mat"].node_tree
    nodes, links = nt.nodes, nt.links
    geo = nodes.new("ShaderNodeNewGeometry")

    def enc(sock, k=0.5, o=0.5):
        m = nodes.new("ShaderNodeVectorMath")
        m.operation = "MULTIPLY_ADD"
        links.new(sock, m.inputs[0])
        m.inputs[1].default_value = (k, k, k)
        m.inputs[2].default_value = (o, o, o)
        return m.outputs[0]

    tan = nodes.new("ShaderNodeTangent")
    tan.direction_type = "UV_MAP"
    bev = nodes.new("ShaderNodeBevel")
    bev.samples = 16
    bev.inputs["Radius"].default_value = bevel_r
    rgb = nodes.new("ShaderNodeRGB")
    rgb.outputs[0].default_value = (mat_id / 255.0, 0, 0, 1)
    col = nodes.new("ShaderNodeAttribute")
    col.attribute_name = "Col"
    pr["aux"] = dict(pos=enc(geo.outputs["Position"], 0.125, 0.5), nrm=enc(geo.outputs["Normal"]),
                     tan=enc(tan.outputs["Tangent"]), bev=enc(bev.outputs["Normal"]), id=rgb.outputs[0],
                     shade=col.outputs["Color"])


# ------------------------------------------------------------------------------------------------
# Detail (numpy)


def _hash(*ints):
    import numpy as np
    h = np.uint32(2166136261) * np.ones_like(ints[0], dtype=np.uint32)
    for k in ints:
        h = (h ^ (k.astype(np.int64) & 0xFFFFFFFF).astype(np.uint32)) * np.uint32(16777619)
        h ^= h >> np.uint32(13)
        h = h * np.uint32(0x5bd1e995)
    h ^= h >> np.uint32(15)
    return (h & np.uint32(0xFFFFFF)).astype(np.float32) / float(0xFFFFFF)


def _norm(v):
    import numpy as np
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-8)


def compose(maps, base, kinds, house_ids, house_col, det):
    """maps: pos/nrm/tan/bev (H x W x 3 decoded), id (H x W int), shade (H x W), ao (H x W);
    base: color (sRGB 0..1), rough, metal (H x W); kinds: id -> surface kind; house_ids: ids of HOUSE materials.
    Returns albedo (H x W x 3 0..1 sRGB), orm (H x W x 4 0..1), normal (H x W x 3 tangent space -1..1)."""
    import numpy as np
    P, N, T, BV = maps["pos"], _norm(maps["nrm"]), maps["tan"], _norm(maps["bev"])
    mid = maps["id"]
    H, W = mid.shape
    col = base["color"].copy()
    rough = base["rough"].copy()
    metal = base["metal"].copy()
    ao = np.clip(maps["ao"], 0, 1)
    panel_ok = np.isin(mid, [i for i, k in kinds.items() if k in PANEL_KINDS]).astype(np.float32)
    # panel lines / rivets only on flat plates: curvature |dN| / |dP| from the baked maps (a lattice of planes
    # drawn over a lying tank or a dome reads as a wireframe); radius below ~0.6 tiles = curved
    def curv(axis):
        dn = np.zeros((H, W), np.float32)
        dp = np.full((H, W), 1e9, np.float32)
        sl0 = [slice(None)] * 2
        a0, a1 = list(sl0), list(sl0)
        a0[axis], a1[axis] = slice(2, None), slice(None, -2)
        mid_ = list(sl0)
        mid_[axis] = slice(1, -1)
        dn[tuple(mid_)] = np.linalg.norm(N[tuple(a0)] - N[tuple(a1)], axis=-1)
        dp[tuple(mid_)] = np.maximum(np.linalg.norm(P[tuple(a0)] - P[tuple(a1)], axis=-1), 1e-6)
        same = np.zeros((H, W), bool)
        same[tuple(mid_)] = mid[tuple(a0)] == mid[tuple(a1)]
        return np.where(same & (dp < 0.05), dn / dp, 0.0)
    k_curv = np.maximum(curv(0), curv(1))
    flat = np.clip((det.get("flat_curv", 1.6) - k_curv) / 0.8, 0, 1)
    panel_ok = panel_ok * flat
    house = np.isin(mid, list(house_ids)).astype(np.float32)
    n = BV.copy()                                  # rounded edges: the bevel normal is the base
    s, w, dep = det["panel"], det["groove_w"], det["groove_depth"]
    seam = np.zeros((H, W), np.float32)
    rivet = np.zeros((H, W), np.float32)
    off = det["panel_offset"]
    for a in range(3):
        b, c = (a + 1) % 3, (a + 2) % 3
        sa = s * det["panel_aniso"][a]
        q = P[..., a] / sa + off[a]
        k = np.floor(q)
        sd = (q - k - 0.5) * sa                    # signed distance to the plane through the cell middle
        # panels: the line on a face exists where the face crosses the plane (not on faces parallel to it),
        # broken into segments 2 cells long, some dropped
        face = np.clip((1.0 - np.abs(N[..., a]) - 0.35) / 0.3, 0, 1)
        cb, cc = np.floor(P[..., b] / (2 * s) + 0.31 * a), np.floor(P[..., c] / (2 * s) + 0.57 * a)
        keep = (_hash(k.astype(np.int64), cb.astype(np.int64), cc.astype(np.int64), np.full_like(k, a, np.int64)) < det["panel_keep"]).astype(np.float32)
        wgt = face * keep * panel_ok
        g = np.exp(-(sd / w) ** 2)
        dH = dep * 2.0 * sd / (w * w) * g          # height H = -dep exp(-(sd/w)^2)
        e = np.zeros_like(N)
        e[..., a] = 1.0
        e = _norm(e - N * N[..., a:a + 1])
        n = n - (dH * wgt)[..., None] * e
        seam = np.maximum(seam, g * wgt)
        # rivet rows beside some seams (one side), pitch along the seam
        rk = (_hash(k.astype(np.int64) + 7, cb.astype(np.int64), cc.astype(np.int64), np.full_like(k, a + 11, np.int64)) < det["rivet_keep"]).astype(np.float32)
        t = _norm(np.cross(N, e))
        along = np.einsum("ijk,ijk->ij", P, t)
        pa = det["rivet_pitch"]
        da = along - (np.floor(along / pa) + 0.5) * pa
        dx = sd - 3.2 * w
        r2 = (dx * dx + da * da) / (det["rivet_r"] ** 2)
        dome = np.clip(1.0 - r2, 0, 1) * wgt * rk
        n = n + ((dx / det["rivet_r"]) * dome * 0.9)[..., None] * e + ((da / det["rivet_r"]) * dome * 0.9)[..., None] * t
        rivet = np.maximum(rivet, (r2 < 1.0) * wgt * rk)
    # dents / hammered plate: a soft value noise on the painted parts
    if det["dent"] > 0:
        cell = 0.09
        q = P / cell
        i0 = np.floor(q).astype(np.int64)
        fr = q - i0
        def h3(dx, dy, dz, salt):
            return _hash(i0[..., 0] + dx, i0[..., 1] + dy, i0[..., 2] + dz, np.full(i0.shape[:2], salt, np.int64)) - 0.5
        u = fr * fr * (3 - 2 * fr)
        grad = np.zeros_like(P)
        for axis in range(3):
            v = np.zeros((H, W), np.float32)
            for dx in (0, 1):
                for dy in (0, 1):
                    for dz in (0, 1):
                        wt = (u[..., 0] if dx else 1 - u[..., 0]) * (u[..., 1] if dy else 1 - u[..., 1]) * (u[..., 2] if dz else 1 - u[..., 2])
                        v += wt * h3(dx, dy, dz, 31 + axis)
            grad[..., axis] = v
        grad = grad - N * np.einsum("ijk,ijk->ij", grad, N)[..., None]
        n = n + grad * (det["dent"] * 0.35) * panel_ok[..., None]
    n = _norm(n)
    # edges: wear (exposed edges lighter / smoother), the bevel's light
    edge = np.clip((1.0 - np.einsum("ijk,ijk->ij", BV, N)) * 12.0, 0, 1)
    up = np.clip(BV[..., 2] * 1.5 + 0.2, 0, 1)
    wear = edge * up * det["wear"] * (0.5 + 0.5 * _hash((P[..., 0] * 160).astype(np.int64), (P[..., 1] * 160).astype(np.int64), (P[..., 2] * 160).astype(np.int64), np.zeros((H, W), np.int64)))
    lum = col @ np.array([0.299, 0.587, 0.114], np.float32)
    worn = np.where(lum[..., None] > 0.45, col * 1.08 + 0.03, col * 1.35 + 0.08)
    col = col + (np.clip(worn, 0, 1) - col) * wear[..., None]
    rough = rough - 0.18 * wear
    # seams: grime in the groove; rivet heads slightly lighter and smoother
    grime = np.array([0.18, 0.15, 0.12], np.float32)
    col = col + (grime - col) * (seam * 0.52)[..., None]
    col = col + (np.clip(col * 1.12 + 0.02, 0, 1) - col) * (rivet * 0.6)[..., None]
    rough = np.clip(rough + 0.22 * seam - 0.1 * rivet, 0.05, 1.0)
    ao = ao * (1.0 - 0.45 * seam)
    # house-colour mask: the clean paint shows the house colour, dirt / grime / wear over it lowers it
    shade = maps["shade"]
    clean = house_col * np.maximum(shade, 0.05)
    hl = (col @ np.array([0.299, 0.587, 0.114], np.float32)) / np.maximum(clean, 1e-3)
    mask = np.clip((hl - 0.3) / 0.35, 0, 1) * (1.0 - 0.6 * wear) * house
    # tangent space (Blender: tangent from the UV map, bitangent = +v; its sign from the position map)
    Tn = _norm(T - N * np.einsum("ijk,ijk->ij", T, N)[..., None])
    Bn = np.cross(N, Tn)
    dPv = np.zeros_like(P)
    dPv[1:-1] = P[2:] - P[:-2]
    sgn = np.sign(np.einsum("ijk,ijk->ij", Bn, dPv))
    sgn[sgn == 0] = 1.0
    Bn = Bn * sgn[..., None]
    tn = np.stack([np.einsum("ijk,ijk->ij", n, Tn), np.einsum("ijk,ijk->ij", n, Bn), np.einsum("ijk,ijk->ij", n, N)], -1)
    tn = _norm(tn)
    tn[..., 2] = np.maximum(tn[..., 2], 0.05)
    orm = np.stack([ao, rough, metal, mask], -1)
    stats = dict(seam=float(seam.mean()), rivets=float(rivet.mean()), wear=float(wear.mean()), house=float(mask.mean()))
    return np.clip(col, 0, 1), np.clip(orm, 0, 1), _norm(tn), stats


def srgb_to_lin(c):
    import numpy as np
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    import numpy as np
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


# ------------------------------------------------------------------------------------------------
# The whole bake


def bake_reimagined(objs, size, surface, out_png_dir, det=None, samples=24):
    """Bakes every material of objs (HOUSE included) into one atlas + the PBR maps; returns (body material,
    png bytes dict color / orm / normal). surface(nt, name) -> (colour, roughness, metallic) sockets."""
    import numpy as np
    det = dict(DETAIL, **(det or {}))
    V3.UV_SCALE["HOUSE"] = det["house_uv"]
    # house paint weathers like the other paint (V3's tree knows paint / steel / rubber / dark)
    V3.SPEC["HOUSE"] = (V3.SPEC.get("HOUSE", (0.0, 0.32, ""))[0], V3.SPEC.get("HOUSE", (0.0, 0.32, ""))[1], "paint")
    procs, kinds, house_ids = {}, {}, set()
    mats = {}
    for o in objs:
        for s in o.material_slots:
            if s.material:
                mats[s.material.name] = s.material
    for i, (name, m) in enumerate(sorted(mats.items())):
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        key = name.split(".")[0]
        if key.startswith("HOUSE"):
            c, r, mt = V3.surface_tree(nt, "HOUSE")
            house_ids.add(i + 1)
            kinds[i + 1] = "house"
            # the part's shade (COLOR_0 "Col": the turret stripe darker, lower edges darker) into the paint
            at = nt.nodes.new("ShaderNodeAttribute")
            at.attribute_name = "Col"
            mx = nt.nodes.new("ShaderNodeMix")
            mx.data_type = "RGBA"
            mx.blend_type = "MULTIPLY"
            mx.inputs["Factor"].default_value = 1.0
            nt.links.new(c, mx.inputs[6])
            nt.links.new(at.outputs["Color"], mx.inputs[7])
            c = mx.outputs[2]
        else:
            c, r, mt = surface(nt, key)
            kinds[i + 1] = V3.SPEC.get(key, (0, 0, "paint"))[2]
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        procs[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
        aux_sockets(procs[name], i + 1, det["bevel_r"])
    V3.uv_unwrap(objs, margin=0.003)
    t0 = time.time()
    images = V2.bake_atlas(objs, procs, size, samples)   # color (sRGB), rough, metal (+ emit)
    def px(img, ch=None):
        a = np.array(img.pixels[:], np.float32).reshape(size, size, 4)
        return a[..., :3].copy() if ch is None else a[..., ch].copy()
    # (a byte image's pixels are its stored values: the colour atlas is sRGB-encoded already)
    col = px(images["color"])
    base = dict(color=col if not images["color"].is_float else lin_to_srgb(col), rough=px(images["rough"], 0),
                metal=px(images["metal"], 0))
    log(f"  colour atlas mean {col.reshape(-1, 3).mean(0).round(3).tolist()} (float {images['color'].is_float})")
    maps = {}
    for key in ("pos", "nrm", "tan", "bev"):
        a = bake_pass(objs, procs, lambda pr, k=key: pr["aux"][k], "rm_" + key, size, samples=16 if key == "bev" else 1)
        maps[key] = (a[..., :3] - 0.5) / (0.125 if key == "pos" else 0.5)
    a = bake_pass(objs, procs, lambda pr: pr["aux"]["id"], "rm_id", size, samples=1, margin=8)
    maps["id"] = np.rint(a[..., 0] * 255).astype(np.int32)
    maps["shade"] = bake_pass(objs, procs, lambda pr: pr["aux"]["shade"], "rm_shade", size, samples=1)[..., 0]
    maps["ao"] = bake_ao(objs, procs, size, det["ao_distance"])
    log(f"  all bakes {time.time() - t0:.1f} s")
    house_col = float(np.dot(np.array(V3.COL["HOUSE"], np.float32), [0.299, 0.587, 0.114]))
    albedo, orm, nrm, stats = compose(maps, base, kinds, house_ids, house_col, det)
    if os.environ.get("D2K_RK_DEBUG"):   # the inputs of compose, for looking into a map
        os.makedirs(out_png_dir, exist_ok=True)
        np.savez_compressed(os.path.join(out_png_dir, "debug.npz"), pos=maps["pos"], id=maps["id"], shade=maps["shade"],
                            color=base["color"], albedo=albedo, orm=orm, house=np.array(sorted(house_ids)))
    log("  detail: " + ", ".join(f"{k} {v:.3f}" for k, v in stats.items()))
    os.makedirs(out_png_dir, exist_ok=True)
    flip = lambda a: a[::-1]                    # Blender pixels: row 0 = bottom
    pngs = dict(
        color=write_png(os.path.join(out_png_dir, "rm_color.png"), flip(np.rint(albedo * 255))),
        orm=write_png(os.path.join(out_png_dir, "rm_orm.png"), flip(np.rint(orm * 255))),
        normal=write_png(os.path.join(out_png_dir, "rm_normal.png"), flip(np.rint((nrm * 0.5 + 0.5) * 255))))
    # the export material: base colour + emission as V3 (the PNGs replace the images afterwards, glb_patch);
    # also the preview material of the review renders (PBR maps through Blender's own nodes)
    mr = V3.pack_mr(images, size)
    body = V3.export_material(images, mr, out_png_dir)
    for o in objs:
        me = o.data
        old = [p.material_index for p in me.polygons]
        me.materials.clear()
        me.materials.append(body)
        for p in me.polygons:
            p.material_index = 0
        del old
    return body, pngs


def preview_material(png_dir):
    """A Principled material with the final maps (review renders in Blender)."""
    m = bpy.data.materials.new("rm_preview")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    def tex(fn, colour):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(os.path.join(png_dir, fn))
        t.image.colorspace_settings.name = "sRGB" if colour else "Non-Color"
        t.image.alpha_mode = "CHANNEL_PACKED"
        return t
    tc, to, tn = tex("rm_color.png", True), tex("rm_orm.png", False), tex("rm_normal.png", False)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(to.outputs["Color"], sep.inputs[0])
    # house colour preview: Atreides blue over the mask
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    nt.links.new(to.outputs["Alpha"], mix.inputs["Factor"])
    nt.links.new(tc.outputs["Color"], mix.inputs[6])
    mix.inputs[7].default_value = (*V3.srgb_to_linear((0.30, 0.42, 0.66)), 1)
    ao = nt.nodes.new("ShaderNodeMix")
    ao.data_type = "RGBA"
    ao.blend_type = "MULTIPLY"
    ao.inputs["Factor"].default_value = 0.5
    nt.links.new(mix.outputs[2], ao.inputs[6])
    nt.links.new(sep.outputs["Red"], ao.inputs[7])
    nt.links.new(ao.outputs[2], b.inputs["Base Color"])
    nt.links.new(sep.outputs["Green"], b.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], b.inputs["Metallic"])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], b.inputs["Normal"])
    return m


# ------------------------------------------------------------------------------------------------
# Simplified version


def export_lod(root, path, ratio):
    """KEY_lod1.glb: every mesh under root decimated (collapse, ratio), same nodes / UVs / material."""
    objs = V2.meshes_under(root)
    mods = []
    for o in objs:
        m = o.modifiers.new("lod", "DECIMATE")
        m.decimate_type = "COLLAPSE"
        m.ratio = ratio
        m.use_collapse_triangulate = True
        mods.append((o, m))
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    tris = sum(sum(len(p.vertices) - 2 for p in o.evaluated_get(deps).data.polygons) for o in objs)
    V3.export_glb(root, path)
    for o, m in mods:
        o.modifiers.remove(m)
    log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    return tris


def strip_images(path):
    """The simplified version needs no textures (the loader reads its geometry only): drop the images."""
    j, b = read_glb(path)
    views = [b[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]] for v in j["bufferViews"]]
    for img in j.get("images", []):
        if "bufferView" in img:
            views[img["bufferView"]] = b""
    write_glb(path, j, views)


# ------------------------------------------------------------------------------------------------
# Review renders (turntable + close-ups; house colour previewed Atreides blue)


def review(root, out_dir, png_dir, prefix, samples=48, dist=2.4, target=(0, 0, 0.2), size=900, closeups=()):
    os.makedirs(out_dir, exist_ok=True)
    pm = preview_material(png_dir)
    saved = {}
    for o in V2.meshes_under(root):
        saved[o.name] = list(o.data.materials)
        o.data.materials.clear()
        o.data.materials.append(pm)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Punchy"
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.0005))
    gp = bpy.context.active_object
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*V3.srgb_to_linear((0.74, 0.60, 0.42)), 1)
    gb.inputs["Roughness"].default_value = 1
    gp.data.materials.append(gm)
    w = scene.world or bpy.data.worlds.new("w")
    scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.5, 0.6, 0.78, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.7
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 4.5
    sun.angle = math.radians(2)
    so = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(so)
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    scene.collection.objects.link(co)
    scene.camera = co
    from mathutils import Vector
    tgt = Vector(target)

    def shot(name, az_deg, el_deg, d, lens, t=tgt, sun_az=150.0, sun_el=40.0, res=(size, size)):
        a, e = math.radians(az_deg), math.radians(el_deg)
        pos = t + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * d
        co.location = pos
        co.rotation_euler = (t - pos).to_track_quat("-Z", "Y").to_euler()
        cam.lens = lens
        sa, se = math.radians(sun_az), math.radians(sun_el)
        so.rotation_euler = Vector((math.cos(sa) * math.cos(se), math.sin(sa) * math.cos(se), math.sin(se))).to_track_quat("Z", "Y").to_euler()
        scene.render.resolution_x, scene.render.resolution_y = res
        scene.render.filepath = os.path.join(out_dir, f"{prefix}_{name}.png")
        bpy.ops.render.render(write_still=True)

    for i in range(8):
        az = -90 - 45 * i
        shot(f"turn_{i}", az, 32, dist, 70, sun_az=az - 70)
    for (name, az, el, d, lens, t) in closeups:
        shot(name, az, el, d, lens, Vector(t), sun_az=az - 75, res=(1400, 900))
    bpy.data.objects.remove(gp)
    bpy.data.objects.remove(so)
    bpy.data.objects.remove(co)
    for o in V2.meshes_under(root):
        o.data.materials.clear()
        for m in saved[o.name]:
            o.data.materials.append(m)
