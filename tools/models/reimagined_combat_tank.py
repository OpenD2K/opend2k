"""Atreides combat tank, Reimagined (docs/3d-models.md "Reimagined models"): the approved v3 design
(combat_tank_v3.py: silhouette, open track loops, white pods, blue caps, turret - unchanged) at Reimagined
fidelity: one atlas with the house parts weathered too (house colour = a mask), PBR maps (ORM + normal map with
rounded edges, panel lines, rivets, dents, edge wear), a commander's hatch / periscopes / headlights as the only
new geometry, idler and sprocket wheels as their own nodes (they spin with Smooth driving), and a simplified
version for small views.

    blender -b --factory-startup --python tools/models/reimagined_combat_tank.py -- \
        [--out DIR]            DIR/models/unit/ATREIDES_COMBAT_TANK.glb + _lod1.glb (default artpacks/models-reimagined)
        [--bake-size 2048] [--lod 0.36] [--turntable DIR] [--samples 48] [--work DIR (keeps the PNG maps)]

Runs on the CPU (V3.DEVICE); about 10 minutes for the bakes at 2048.
"""
import math
import os
import sys
import time

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402
import reimagined_kit as RK  # noqa: E402

P = V3.P
KEY = "ATREIDES_COMBAT_TANK"
DET = dict(panel=0.17, groove_w=0.0075, groove_depth=0.0065, rivet_pitch=0.03, rivet_r=0.0055, bevel_r=0.011,
           house_uv=0.75, ao_distance=0.14)


def extras_turret():
    """A commander's hatch (right of the house stripe, behind the middle of the roof), two periscope blocks
    on the left front of the roof. Low: the silhouette stays the v3 turret."""
    zt = P["tur_top"] + 0.12
    out = []
    v, u = 2.75, -1.6
    out.append(_zcyl("hatch_ring", 1.05, v, u, zt - 0.05, zt + 0.2, "mantlet", 0.06))
    out.append(_zcyl("hatch_lid", 0.82, v, u, zt + 0.18, zt + 0.36, "tur_roof", 0.08))
    out.append(V3.box("hatch_handle", v - 0.5, v + 0.5, u + 0.05, u + 0.25, zt + 0.36, zt + 0.5, "gun", bev=0.04, segs=1))
    out.append(V3.box("hatch_hinge", v - 0.35, v + 0.35, u - 1.05, u - 0.78, zt + 0.12, zt + 0.34, "mantlet", bev=0.04, segs=1))
    for k, uu in enumerate((0.35, -0.75)):
        out.append(V3.box(f"periscope{k}", -3.1, -2.2, uu - 0.35, uu + 0.35, zt - 0.02, zt + 0.42, "tur_core", bev=0.06, segs=1))
        out.append(V3.box(f"periscope_glass{k}", -3.05, -2.25, uu + 0.33, uu + 0.37, zt + 0.15, zt + 0.36, "gun", segs=1))
    return out


def _zcyl(name, r, v, u, z0, z1, material, bev):
    """Upright cylinder at (v, u) in px."""
    import bmesh
    bm = bmesh.new()
    n = 24
    rings = []
    for z in (z0, z1):
        rings.append([(v + r * math.cos(2 * math.pi * i / n), u + r * math.sin(2 * math.pi * i / n), z) for i in range(n)])
    V3.rings_solid(bm, rings)
    return V3.new_obj(name, bm, material, bev=bev, segs=2)


def extras_body():
    """Two headlights on the glacis front (small cylinders along u with a light lens)."""
    out = []
    gl = P["hull_glacis"]
    for s in (-1, 1):
        v = s * 1.75
        out.append(V3.cyl("headlamp", 0.42, gl[0] - 0.35, gl[0] + 0.25, v, 4.55, "gun", segs=14, bev=0.05))
        out.append(V3.cyl("headlamp_lens", 0.3, gl[0] + 0.2, gl[0] + 0.32, v, 4.55, "cream", segs=14))
    return out


def build_body():
    """V3.build_body with the idler / sprocket wheels (+ hubs) as their own nodes (origin on the axle)."""
    parts = []
    V3.track_parts(parts)
    V3.frame_parts(parts, P["pod_out"], +1)
    V3.frame_parts(parts, P["pod_in"], -1)
    V3.cover_parts(parts)
    V3.sponson_parts(parts)
    for o in list(parts):
        V2.apply_all(o)
        parts.append(V3.mirror_copy(o))
    V3.hull_parts(parts)
    parts += extras_body()
    vc = (P["tread_v"][0] + P["tread_v"][1]) / 2
    centres = dict(idler=(-12.75, 2.45), sprocket=(10.2, 3.65))
    wheels = []
    for kind, (cu, cz) in centres.items():
        for side, sign in (("R", 1), ("L", -1)):
            objs = [o for o in parts if o.name.split(".")[0].startswith(kind) and (o.name.endswith("_L") == (side == "L"))]
            for o in objs:
                parts.remove(o)
            wheels.append(V3.finish_node(objs, f"{kind}_{side}", (sign * vc + P["v0"], cu, cz), (P["v0"], 0, 0)))
    body = V3.finish_node(parts, "body", (0, 0, 0), (P["v0"], 0, 0))
    return body, wheels


def build():
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    body, wheels = build_body()
    body.parent = root
    for w in wheels:
        w.parent = root
    orig = V3.finish_node

    def finish(objs, name, origin_px, offset_px=(0.0, 0.0, 0.0)):
        if name == "turret":
            objs = list(objs) + extras_turret()
        return orig(objs, name, origin_px, offset_px)

    V3.finish_node = finish
    try:
        tur = V3.build_turret()
    finally:
        V3.finish_node = orig
    tur.parent = root
    bpy.context.view_layer.update()
    return root


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-reimagined"), bake_size=2048,
               lod=0.36, turntable=None, samples=48, work=None)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--turntable", "--work"):
            opt[a[2:]] = argv[i + 1]; i += 1
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--lod":
            opt["lod"] = float(argv[i + 1]); i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root = build()
    objs = V2.meshes_under(root)
    tris = V2.tri_count(objs)
    RK.log(f"built: {len(objs)} meshes, {tris} triangles in {time.time() - t0:.1f} s")
    png_dir = opt["work"] or os.path.join(bpy.app.tempdir, "d2k_reimagined_tank")  # --work DIR: keep the maps
    t1 = time.time()
    _, pngs = RK.bake_reimagined(objs, opt["bake_size"], V3.surface_tree, png_dir, DET)
    RK.log(f"baked in {time.time() - t1:.1f} s")
    path = os.path.join(opt["out"], "models", "unit", KEY + ".glb")
    V3.export_glb(root, path)
    RK.glb_patch(path, pngs["color"], pngs["orm"], pngs["normal"])
    RK.log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    lod = os.path.join(opt["out"], "models", "unit", KEY + "_lod1.glb")
    RK.export_lod(root, lod, opt["lod"])
    RK.strip_images(lod)
    if opt["turntable"]:
        RK.review(root, opt["turntable"], png_dir, "tank", samples=opt["samples"], dist=2.3, target=(0, 0, 0.2),
                  closeups=[("closeup_front", 55, 22, 2.0, 70, (0, 0, 0.2)), ("closeup_rear", -125, 26, 2.2, 70, (0, 0, 0.2)),
                            ("closeup_side", 0, 14, 2.1, 80, (0, 0, 0.17)), ("closeup_track", -20, 12, 1.15, 85, (0.25, -0.3, 0.1)),
                            ("closeup_turret", 70, 40, 1.1, 85, (0, 0, 0.36))])
    RK.log(f"total {time.time() - t0:.1f} s")


if __name__ == "__main__":
    main()
