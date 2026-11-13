"""Build the OpenD2K procedural 3D models (glTF .glb) with Blender, headless.

    blender -b --factory-startup --python tools/models/build_models.py -- \
        [--out assets/models] [--only NAME[,NAME]] [--preview DIR] [--size 480]

Writes <out>/unit/*.glb and <out>/building/*.glb (file names = Templates.bin unit / building
names or group names with spaces as underscores, which is how render3d/models.cpp finds them),
prints the triangle count per model, and with --preview renders an 8-angle turntable per model
to DIR/<name>_<i>.png (montage: tools/models/montage.py). Every model is our own procedural
geometry (docs/3d-models.md "New models": style guide + conventions), so the output is tracked
in the repository and shipped.
"""

import importlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import d2klib as L  # noqa: E402

MODULES = ["combat_tank", "trike", "harvester", "wind_trap", "refinery"]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = os.path.join(os.path.dirname(os.path.dirname(HERE)), "assets", "models")
    only, prev, size = None, None, 480
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            out = argv[i + 1]; i += 1
        elif a == "--only":
            only = set(argv[i + 1].split(",")); i += 1
        elif a == "--preview":
            prev = argv[i + 1]; i += 1
        elif a == "--size":
            size = int(argv[i + 1]); i += 1
        i += 1
    models = {}
    for mn in MODULES:
        try:
            mod = importlib.import_module(mn)
        except ModuleNotFoundError as e:
            if e.name == mn:
                continue
            raise
        models.update(mod.MODELS)
    for key, spec in models.items():
        name = key.split("/")[-1]
        if only and name not in only and key not in only:
            continue
        t0 = time.time()
        L.reset()
        root = spec["build"]()
        L.finish(root)
        tris = L.tri_count(root)
        path = os.path.join(out, key + ".glb")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        L.export_glb(root, path)
        t1 = time.time()
        print(f"MODEL {key}: {tris} triangles, {os.path.getsize(path) // 1024} KB, built in {t1 - t0:.1f} s", flush=True)
        for alias in spec.get("aliases", []):
            ap = os.path.join(out, alias + ".glb")
            os.makedirs(os.path.dirname(ap), exist_ok=True)
            with open(path, "rb") as f, open(ap, "wb") as g:
                g.write(f.read())
        if prev:
            os.makedirs(prev, exist_ok=True)
            L.preview(root, os.path.join(prev, name), size=size, **spec.get("view", {}))
            print(f"  preview {time.time() - t1:.1f} s", flush=True)


main()
