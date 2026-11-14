#!/usr/bin/env python3
"""Barrel tips of a model pack's vehicles -> models.ini muzzle anchors (docs/3d-models.md "Anchors").

  python -I tools/models/muzzle_anchors.py PACK [--write] [--types A,B] [--front DEVASTATOR_TANK]

For every vehicle model PACK/models/unit/NAME.glb (infantry with a frame map are skipped) it finds the
muzzle points the Modern effects start shots at:
  - the glb's own fx_muzzle_N nodes (empties at the barrel tips; under the `turret` node when it turns),
  - else the tip of the node named `barrel` (the centre of its front-most vertices, facing -Z = north),
and writes them to PACK/models.ini as
  anchor.fx_muzzle_N = x y z 1 turret      (glb axes and units: tiles, x east, y up, z south; "turret" = turns
                                            with the turret about its pivot)
  turret_pivot = x y z                      (the `turret` node's origin: the 2D pack's turret frames turn about it)
The 3D renderer reads the anchors (the same names as the glb nodes replace them); the 2D pack's PackAnchors
projects them the way render_pack.py renders the frames (render/weld_fx.cpp). Without --write it prints the
lines only. models.ini is backed up as models.ini.bak-muzzle before it is changed.

Python 3.10+ with numpy (the project venv).
"""
import argparse
import json
import os
import re
import shutil
import struct
import sys

import numpy as np

COMP = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    d = open(path, "rb").read()
    jl = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + jl])
    off = 20 + jl
    bin_chunk = b""
    if off + 8 <= len(d):
        bl = struct.unpack("<I", d[off:off + 4])[0]
        bin_chunk = d[off + 8:off + 8 + bl]
    return j, bin_chunk


def accessor(j, b, i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    dt = np.dtype(COMP[a["componentType"]])
    n = NCOMP[a["type"]]
    stride = bv.get("byteStride", 0) or dt.itemsize * n
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    raw = np.frombuffer(b, dtype=np.uint8, count=stride * (a["count"] - 1) + dt.itemsize * n, offset=start)
    out = np.empty((a["count"], n), dt)
    for k in range(a["count"]):
        out[k] = np.frombuffer(raw[k * stride:k * stride + dt.itemsize * n].tobytes(), dt)
    return out.astype(np.float64)


def quat_m(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def local_m(n):
    if "matrix" in n:
        return np.array(n["matrix"], np.float64).reshape(4, 4).T
    m = np.eye(4)
    s = np.array(n.get("scale", [1, 1, 1]), np.float64)
    m[:3, :3] = quat_m(n.get("rotation", [0, 0, 0, 1])) * s
    m[:3, 3] = n.get("translation", [0, 0, 0])
    return m


def world_mats(j):
    nodes = j.get("nodes", [])
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    cache = {}

    def wm(i):
        if i not in cache:
            m = local_m(nodes[i])
            cache[i] = wm(parent[i]) @ m if i in parent else m
        return cache[i]

    return [wm(i) for i in range(len(nodes))], parent


def under(parent, nodes, i, name):
    while i is not None:
        if (nodes[i].get("name") or "").split(".")[0].lower() == name:
            return True
        i = parent.get(i)
    return False


def front_tips(j, b, W, i):
    """The front-most vertices of node i's mesh (within 0.05 tiles of the nose, above 0.15 tiles): one
    tip at their centre, or two when they form a left and a right cluster (twin guns)."""
    pts = []
    for p in j["meshes"][j["nodes"][i]["mesh"]]["primitives"]:
        v = accessor(j, b, p["attributes"]["POSITION"])
        pts.append((W[i] @ np.c_[v, np.ones(len(v))].T).T[:, :3])
    pts = np.concatenate(pts)
    pts = pts[pts[:, 1] > 0.15]
    zmin = pts[:, 2].min()
    f = pts[pts[:, 2] < zmin + 0.05]
    left, right, mid = f[f[:, 0] < -0.03], f[f[:, 0] > 0.03], f[np.abs(f[:, 0]) <= 0.03]
    groups = [left, right] if len(left) and len(right) and not len(mid) else [f]
    return [np.array([g[:, 0].mean(), g[:, 1].mean(), zmin]) for g in groups]


def muzzles(path, front=False):
    j, b = read_glb(path)
    nodes = j.get("nodes", [])
    W, parent = world_mats(j)
    pivot = None
    for i, n in enumerate(nodes):
        if (n.get("name") or "").split(".")[0].lower() == "turret":
            pivot = W[i][:3, 3].copy()
    out = []
    for i, n in enumerate(nodes):
        nm = (n.get("name") or "").lower()
        if nm.startswith("fx_muzzle"):
            out.append((nm, W[i][:3, 3].copy(), under(parent, nodes, i, "turret")))
    src = "glb fx_muzzle nodes"
    if not out:
        for i, n in enumerate(nodes):
            if (n.get("name") or "").split(".")[0].lower() != "barrel" or "mesh" not in n:
                continue
            pts = []
            for p in j["meshes"][n["mesh"]]["primitives"]:
                v = accessor(j, b, p["attributes"]["POSITION"])
                pts.append((W[i] @ np.c_[v, np.ones(len(v))].T).T[:, :3])
            pts = np.concatenate(pts)
            zmin = pts[:, 2].min()
            front = pts[pts[:, 2] < zmin + 0.01]
            tip = np.array([front[:, 0].mean(), front[:, 1].mean(), zmin])
            out.append(("fx_muzzle_0", tip, under(parent, nodes, i, "turret")))
            src = "barrel mesh tip"
    if not out and front:
        for i, n in enumerate(nodes):
            if (n.get("name") or "").split(".")[0].lower() == "body" and "mesh" in n:
                out = [(f"fx_muzzle_{k}", t, False) for k, t in enumerate(front_tips(j, b, W, i))]
                src = "front-most body vertices (--front)"
    return sorted(out, key=lambda t: t[0]), pivot, src


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--types", default="")
    ap.add_argument("--front", default="", help="types without fx_muzzle nodes or a barrel whose guns are the "
                    "front-most parts of the body (the devastator's twin cannons)")
    args = ap.parse_args()
    unit_dir = os.path.join(args.pack, "models", "unit")
    only = {t.strip().upper() for t in args.types.split(",") if t.strip()}
    front = {t.strip().upper() for t in args.front.split(",") if t.strip()}
    lines = {}
    for fn in sorted(os.listdir(unit_dir)):
        if not fn.endswith(".glb"):
            continue
        key = fn[:-4]
        if key.endswith("_turret") or os.path.exists(os.path.join(unit_dir, key + ".anim.json")):
            continue
        if only and key not in only:
            continue
        ms, pivot, src = muzzles(os.path.join(unit_dir, fn), key in front)
        if not ms:
            continue
        ls = []
        if pivot is not None and any(t for _, _, t in ms):
            ls.append("turret_pivot = %.4f %.4f %.4f" % tuple(pivot))
        for nm, p, t in ms:
            ls.append("anchor.%s = %.4f %.4f %.4f 1%s" % (nm, p[0], p[1], p[2], " turret" if t else ""))
        lines[key] = ls
        print(f"[{key}]  ; {src}")
        for l in ls:
            print("  " + l)
    if not args.write:
        return
    ini = os.path.join(args.pack, "models.ini")
    text = open(ini, encoding="utf-8").read() if os.path.exists(ini) else ""
    if text:
        shutil.copyfile(ini, ini + ".bak-muzzle")
    out, sec, done = [], None, set()

    def flush(s):
        if s in lines and s not in done:
            out.extend(lines[s])
            done.add(s)

    for raw in text.splitlines():
        m = re.match(r"\s*\[([^\]]+)\]", raw)
        if m:
            flush(sec)
            sec = m.group(1).strip()
            out.append(raw)
            continue
        k = raw.split("=", 1)[0].strip().lower()
        if sec in lines and (k.startswith("anchor.fx_muzzle") or k == "turret_pivot"):
            continue  # replaced
        out.append(raw)
    flush(sec)
    for s, ls in lines.items():
        if s not in done:
            out.append(f"[{s}]")
            out.extend(ls)
    open(ini, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"wrote {ini} (backup: models.ini.bak-muzzle)")


if __name__ == "__main__":
    sys.exit(main())
