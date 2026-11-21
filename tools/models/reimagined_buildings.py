"""Atreides buildings, Reimagined (docs/3d-models.md "Reimagined models"): the approved designs of the
construction yard (atreides_construction_yard.py), the refinery (refinery_pilot_fable.py, its 3D layout) and
the wind trap (atreides_wind_trap.py) - same geometry, nodes and anchors, so the main pack's models.ini
sections and pose variants still fit - at Reimagined fidelity (reimagined_kit.py: one atlas incl. the house
parts as a mask, PBR maps with rounded edges / panel lines / rivets / wear, 3D emission strengths as the pack's
glbs (D2K_GLOW_3D), a simplified version).

    blender -b --factory-startup --python tools/models/reimagined_buildings.py -- --only yard|refinery|windtrap \
        [--out DIR] [--bake-size 2048] [--lod 0.4] [--turntable DIR] [--samples 48] [--work DIR]

One building per run (one Blender process at a time; the kits set module-wide palettes).
"""
import os
import sys
import time

os.environ["D2K_GLOW_3D"] = "1"   # the 3D emission strengths of the lit twins (atreides_kit.lit)

import bpy  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402
import atreides_kit as AK  # noqa: E402  (imports refinery_pilot_fable: the building palette)
import refinery_pilot_fable as RF  # noqa: E402
import reimagined_kit as RK  # noqa: E402

DET = dict(panel=0.25, groove_w=0.0105, groove_depth=0.009, rivet_pitch=0.045, rivet_r=0.0075, rivet_keep=0.35,
           bevel_r=0.018, house_uv=0.6, ao_distance=0.3, dent=0.3, wear=0.5)


def build(which):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    V3._mats.clear()
    rest = None
    if which == "refinery":
        V3.P["house_low"] = RF.P["house_low"]
        root, nodes = RF.build(False)
        key = RF.KEY
    else:
        import importlib
        mod = importlib.import_module({"yard": "atreides_construction_yard", "windtrap": "atreides_wind_trap"}[which])
        root, nodes = AK.assemble(mod.KEY, mod.build(False))
        key = mod.KEY
        rest = getattr(mod, "rest", None)
    for name in list(V3._mats):
        RF.mat(name)
    objs = list(nodes.values()) if isinstance(nodes, dict) else list(nodes)
    return key, root, nodes, objs, rest


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-reimagined"), bake_size=2048,
               lod=0.4, turntable=None, samples=48, work=None, only=None)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--turntable", "--work", "--only"):
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
    if opt["only"] not in ("yard", "refinery", "windtrap"):
        raise SystemExit("--only yard|refinery|windtrap")
    V2.setup_gpu = V3.setup_device
    t0 = time.time()
    key, root, nodes, objs, rest = build(opt["only"])
    tris = V2.tri_count(objs)
    RK.log(f"built {key}: {len(objs)} nodes, {tris} triangles")
    png_dir = opt["work"] or os.path.join(bpy.app.tempdir, "d2k_reimagined_" + opt["only"])
    _, pngs = RK.bake_reimagined(objs, opt["bake_size"], RF.surface, png_dir, DET)
    if rest is not None:   # the idle pose (the yard's load / glow hidden), as the main pack's glb
        rest(nodes)
        bpy.context.view_layer.update()
    path = os.path.join(opt["out"], "models", "building", key + ".glb")
    V3.export_glb(root, path)
    RK.glb_patch(path, pngs["color"], pngs["orm"], pngs["normal"])
    RK.log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    lod = os.path.join(opt["out"], "models", "building", key + "_lod1.glb")
    RK.export_lod(root, lod, opt["lod"])
    RK.strip_images(lod)
    if opt["turntable"]:
        RK.review(root, opt["turntable"], png_dir, opt["only"], samples=opt["samples"], dist=6.0, target=(0, 0, 0.45),
                  closeups=[("closeup_a", -60, 30, 3.2, 70, (0.2, -0.4, 0.4)), ("closeup_b", 30, 24, 3.0, 70, (0.3, 0.2, 0.4))])
    RK.log(f"total {time.time() - t0:.1f} s")


if __name__ == "__main__":
    main()
