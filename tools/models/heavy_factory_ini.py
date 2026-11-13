#!/usr/bin/env python3
"""Write the models.ini sections of the redesigned heavy factories from the anchors their glb files carry.

  .venv/bin/python -I tools/models/heavy_factory_ini.py artpacks/opend2k-redesign [--check]

For every *HEAVY_FACTORY.glb in <pack>/models/building: the fx_* nodes (fx_flue_N stack tops, fx_light_N the bay's
ceiling light strips - points ON the geometry) become "anchor.NAME = x y z size" lines (glb axes, tiles) in the
section [KEY] of <pack>/models.ini (the section is replaced; the rest of the file is kept). The engine reads the
same nodes from the glb, the lines make them visible and editable. --check also prints each model's plan bounds and
how deep parts above 2 px stand in the two free cells of the footprint (docs/3d-models.md "Placement": at most 1 px).
"""
import glob
import json
import os
import re
import struct
import sys

HEAD = "; redesign 2 (tools/models/heavy_factory_kit.py): open garage on the unit's exit path, inside the cells (no shrink)."


def read(path):
    d = open(path, "rb").read()
    jl = struct.unpack_from("<I", d, 12)[0]
    J = json.loads(d[20:20 + jl])
    bin0 = 20 + jl + 8
    anchors, pts = [], []

    def walk(i, off, sc):
        n = J["nodes"][i]
        if "rotation" in n and [round(v, 5) for v in n["rotation"]] != [0, 0, 0, 1] or "matrix" in n:
            raise SystemExit(f"{path}: node {n.get('name')} is rotated - not handled")
        s = n.get("scale", [1, 1, 1])
        t = n.get("translation", [0, 0, 0])
        off2 = [off[k] + sc[k] * t[k] for k in range(3)]
        sc2 = [sc[k] * s[k] for k in range(3)]
        if n.get("name", "").startswith("fx_"):
            anchors.append((n["name"], off2, s[0]))
        if "mesh" in n:
            for pr in J["meshes"][n["mesh"]]["primitives"]:
                a = J["accessors"][pr["attributes"]["POSITION"]]
                bv = J["bufferViews"][a["bufferView"]]
                o = bin0 + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
                st = bv.get("byteStride", 12)
                for k in range(a["count"]):
                    p = struct.unpack_from("<3f", d, o + k * st)
                    pts.append([off2[j] + sc2[j] * p[j] for j in range(3)])
        for c in n.get("children", []):
            walk(c, off2, sc2)
    for r in J["scenes"][0]["nodes"]:
        walk(r, [0, 0, 0], [1, 1, 1])
    em = [(m["name"], m.get("emissiveFactor"), m.get("extensions", {}).get("KHR_materials_emissive_strength", {}).get("emissiveStrength", 1.0))
          for m in J["materials"] if m.get("emissiveFactor")]
    return anchors, pts, em


def main():
    pack = sys.argv[1]
    ini = os.path.join(pack, "models.ini")
    text = open(ini).read() if os.path.exists(ini) else ""
    for path in sorted(glob.glob(os.path.join(pack, "models", "building", "*HEAVY_FACTORY.glb"))):
        key = os.path.basename(path)[:-4]
        anchors, pts, em = read(path)
        if "--check" in sys.argv:
            xs, zs = [p[0] * 32 for p in pts], [p[2] * 32 for p in pts]
            depth = 0.0
            for p in pts:
                x, y, z = p[0] * 32 + 48, p[1] * 32, p[2] * 32 + 73.6          # px in the 96 x 128 art box
                if y > 2 and z < 32 and (x < 32 or x > 64):
                    depth = max(depth, min(32 - x if x < 32 else x - 64, 32 - z))
            print(f"{key}: plan x {min(xs):.1f}..{max(xs):.1f} (cells -48..48), z {min(zs):.1f}..{max(zs):.1f} (cells -73.6..54.4), "
                  f"free-cell depth {depth:.2f} px, emissive {[(n, round(s, 2)) for n, _c, s in em]}")
        lines = [f"[{key}]", HEAD] + [f"anchor.{n} = {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {s:g}" for n, p, s in sorted(anchors)]
        sec = re.compile(r"(?ms)^\[" + re.escape(key) + r"\]\n.*?(?=^\[|\Z)")
        block = "\n".join(lines) + "\n\n"
        text = sec.sub(block, text) if sec.search(text) else text.rstrip("\n") + "\n\n" + block
    tmp = ini + f".tmp{os.getpid()}"
    open(tmp, "w").write(text.rstrip("\n") + "\n")
    os.replace(tmp, ini)
    print("wrote", ini)


if __name__ == "__main__":
    main()
