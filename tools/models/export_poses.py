#!/usr/bin/env python3
"""Copy a model's pose variants into a model pack for Graphics: 3D (docs/3d-models.md "Animation").

    python3 -I tools/models/export_poses.py MODELS_DIR PACK_DIR KEY [--full] [--force] [--no-spec] [--dry-run]

MODELS_DIR is a generator's output:
  * tools/models/atreides_kit.py (buildings): MODELS_DIR/variants/KEY_poseNN.glb + spec.json,
  * tools/models/harvester_v1.py: MODELS_DIR/variants/KEY_unloadNN.glb,
  * tools/models/atreides_vehicle_kit.py (units with animation rows in their art, the ornithopter's wing beat):
    MODELS_DIR/variants/KEY_animNN.glb = art row NN -> written as KEY_rowNN.glb,
and MODELS_DIR/models/{unit,building}/KEY.glb, the main model of that run. PACK_DIR is the combined pack (artpacks/NAME,
the one with models/unit + models/building). Writes PACK_DIR/variants/ (stale KEY_{pose,unload,row}NN.glb of KEY are
removed first):

  * a variant whose NODES moved (buildings: crane, dish, silo fill) -> node poses only: the variant's glTF JSON without
    meshes, materials, images or buffers (a few KB instead of megabytes). The game turns the poses into the model's
    "pose" animation (pose NN = key NN, step).
  * a variant whose nodes are at rest but whose MESHES differ (the harvester's merged body, the ornithopter's wings
    bent in its one body mesh) -> the geometry: the glb with its images replaced by a 1x1 placeholder (the game draws
    a body mesh variant with the main model's texture; --full copies the whole file).
  * a variant equal to the main model (e.g. the ornithopter's row 1, wings level = the main model) is not written: a
    missing pose is the rest pose.
  * KEY.spec.json - the generator's spec.json (which pose each overlay frame / silo level / art row shows; "key" =
    KEY). An existing KEY.spec.json whose pose fields (poses, anim_frames, levels, rows) agree is kept as it is (a
    --no-damaged run writes "damaged": false); --no-spec never writes it.

The pose variants must come from the same build of the model as PACK_DIR/models/.../KEY.glb: the script compares the
node names and rest transforms of MODELS_DIR's main glb with the pack's, and for mesh variants its whole buffer, and
refuses a mismatch (--force writes anyway).
A --no-bake run of the generator is enough for node poses (blender -b --factory-startup --python
tools/models/atreides_construction_yard.py -- --out /tmp/yard --no-bake --no-damaged); mesh variants need the baked
run (their UVs must match the pack's texture).
"""
import json
import os
import re
import shutil
import struct
import sys
import zlib

TAGS = {"pose": "pose", "unload": "unload", "row": "row", "anim": "row"}  # generator tag -> pack tag
EPS = 1e-5


def read_glb(path):
    b = open(path, "rb").read()
    if b[:4] != b"glTF":
        raise SystemExit(f"{path}: not a .glb")
    n = struct.unpack_from("<I", b, 12)[0]
    j = json.loads(b[20:20 + n])
    rest = 20 + n
    bin_ = b""
    if rest + 8 <= len(b):
        bl, bt = struct.unpack_from("<II", b, rest)
        if bt == 0x004E4942:
            bin_ = b[rest + 8:rest + 8 + bl]
    return j, bin_


def write_glb(path, j, bin_=b""):
    data = json.dumps(j, separators=(",", ":")).encode()
    data += b" " * (-len(data) % 4)
    bin_ = bin_ + b"\0" * (-len(bin_) % 4)
    total = 12 + 8 + len(data) + (8 + len(bin_) if bin_ else 0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(data), 0x4E4F534A))
        f.write(data)
        if bin_:
            f.write(struct.pack("<II", len(bin_), 0x004E4942))
            f.write(bin_)


def trs(nd):
    """A node's local transform as 10 floats (t, r xyzw, s); a matrix node as its 16 floats."""
    if "matrix" in nd:
        return list(nd["matrix"])
    return list(nd.get("translation", [0, 0, 0])) + list(nd.get("rotation", [0, 0, 0, 1])) + list(nd.get("scale", [1, 1, 1]))


def same_trs(a, b):
    return len(a) == len(b) and all(abs(x - y) <= EPS for x, y in zip(a, b))


def nodes_only(j):
    nodes = []
    for nd in j.get("nodes", []):
        nd = {k: v for k, v in nd.items() if k in ("name", "children", "translation", "rotation", "scale", "matrix")}
        nodes.append(nd)
    out = {"asset": j.get("asset", {"version": "2.0"}), "nodes": nodes}
    for k in ("scene", "scenes"):
        if k in j:
            out[k] = j[k]
    out["asset"] = dict(out["asset"], extras={"d2k": "pose variant: node transforms only (tools/models/export_poses.py)"})
    return out


def png_1x1():
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"\0\x80\x80\x80")) + chunk(b"IEND", b""))


def geometry_only(j, bin_):
    """The glb with every embedded image replaced by a 1x1 PNG (the buffer repacked)."""
    j = json.loads(json.dumps(j))
    image_views = {im["bufferView"] for im in j.get("images", []) if "bufferView" in im}
    stub = png_1x1()
    out = bytearray()
    for i, bv in enumerate(j.get("bufferViews", [])):
        if bv.get("buffer", 0) != 0:
            raise SystemExit("only single-buffer .glb files are supported")
        data = stub if i in image_views else bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        out += b"\0" * (-len(out) % 4)
        bv["byteOffset"] = len(out)
        bv["byteLength"] = len(data)
        out += data
    for im in j.get("images", []):
        if "bufferView" in im:
            im["mimeType"] = "image/png"
    if j.get("buffers"):
        j["buffers"][0]["byteLength"] = len(out)
    j["asset"] = dict(j.get("asset", {"version": "2.0"}),
                      extras={"d2k": "pose variant: geometry only, images are placeholders (tools/models/export_poses.py)"})
    return j, bytes(out)


def main_glb(base, key):
    for sub in ("models/unit", "models/building", "unit", "building"):
        p = os.path.join(base, sub, key + ".glb")
        if os.path.exists(p):
            return p
    return None


def check_rest(gen_main, pack_main, force):
    (g, _), (p, _) = read_glb(gen_main), read_glb(pack_main)
    gn, pn = g.get("nodes", []), p.get("nodes", [])
    names_g, names_p = [n.get("name", "") for n in gn], [n.get("name", "") for n in pn]
    problems = []
    if names_g != names_p:
        problems.append(f"node names differ: {names_g} vs the pack's {names_p}")
    else:
        for a, b in zip(gn, pn):
            if not same_trs(trs(a), trs(b)):
                problems.append(f"node {a.get('name')!r}: rest transform {trs(a)} vs the pack's {trs(b)}")
    if problems:
        msg = f"{gen_main} is not the build of {pack_main}:\n  " + "\n  ".join(problems)
        if not force:
            raise SystemExit(msg + "\n(--force writes the poses anyway)")
        print("WARNING: " + msg)
    else:
        print(f"rest check: {len(gn)} nodes, names and rest transforms equal to {pack_main}")
    return names_p, [trs(n) for n in pn]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    unknown = flags - {"--full", "--force", "--no-spec", "--dry-run"}
    if len(args) != 3 or unknown:
        raise SystemExit(__doc__)
    full, force, dry = "--full" in flags, "--force" in flags, "--dry-run" in flags
    src, pack, key = args
    vsrc, vdst = os.path.join(src, "variants"), os.path.join(pack, "variants")
    if not os.path.isdir(vsrc):
        raise SystemExit(f"no {vsrc}")
    pack_main = main_glb(pack, key)
    if pack_main is None:
        raise SystemExit(f"no {key}.glb under {pack}/models/{{unit,building}}")
    gen_main = main_glb(src, key)
    if gen_main is None:
        if not force:
            raise SystemExit(f"no {key}.glb under {src}/models/{{unit,building}} to check the rest pose against (--force)")
        pj, _ = read_glb(pack_main)
        names, rest = [n.get("name", "") for n in pj.get("nodes", [])], [trs(n) for n in pj.get("nodes", [])]
    else:
        names, rest = check_rest(gen_main, pack_main, force)
    _, main_bin = read_glb(gen_main or pack_main)
    _, pack_bin = read_glb(pack_main)

    pat = re.compile(re.escape(key) + r"_(pose|unload|row|anim)(\d\d)\.glb$")
    found = []
    for f in sorted(os.listdir(vsrc)):
        m = pat.match(f)
        if m:
            found.append((f, TAGS[m.group(1)], int(m.group(2))))
    if not found:
        raise SystemExit(f"{key}: no pose variants in {vsrc}")
    if len({t for _, t, _ in found}) > 1:
        raise SystemExit(f"{key}: mixed variant kinds {sorted({t for _, t, _ in found})}: one kind per model")

    if not dry:
        os.makedirs(vdst, exist_ok=True)
        stale = re.compile(re.escape(key) + r"_(pose|unload|row)\d\d\.glb$")
        for f in os.listdir(vdst):
            if stale.match(f):
                os.remove(os.path.join(vdst, f))
    counts = dict(nodes=0, mesh=0, rest=0)
    total = 0
    for f, tag, k in found:
        a = os.path.join(vsrc, f)
        b = os.path.join(vdst, f"{key}_{tag}{k:02d}.glb")
        j, bin_ = read_glb(a)
        vn = j.get("nodes", [])
        if [n.get("name", "") for n in vn] != names:
            raise SystemExit(f"{a}: its node names differ from the pack's {key}.glb")
        moved = [n.get("name") for n, r in zip(vn, rest) if not same_trs(trs(n), r)]
        if moved:
            kind, out = "nodes " + ",".join(moved), (nodes_only(j), b"")
            counts["nodes"] += 1
        elif bin_ == main_bin:
            print(f"  {f}: equal to the main model (rest pose), not written")
            counts["rest"] += 1
            continue
        else:
            if main_bin != pack_bin and not force:   # a body mesh variant replaces the pack's body: same build only
                raise SystemExit(f"{a}: a mesh variant, but {gen_main} is not the same build as {pack_main} "
                                 "(its buffer differs: run the generator with baking, as the pack was made; --force)")
            kind = "mesh"
            out = None if full else geometry_only(j, bin_)
            counts["mesh"] += 1
        if not dry:
            if out is None:
                shutil.copyfile(a, b)
            else:
                write_glb(b, *out)
            total += os.path.getsize(b)
        print(f"  {f} -> {os.path.basename(b)} ({kind})")

    spec = os.path.join(vsrc, "spec.json")
    if os.path.exists(spec) and "--no-spec" not in flags:
        s = json.load(open(spec))
        if s.get("key", key) == key:
            dst = os.path.join(vdst, key + ".spec.json")
            keep = False
            if os.path.exists(dst):
                old = json.load(open(dst))
                keep = all(old.get(f) == s.get(f) for f in ("poses", "anim_frames", "levels", "rows"))
            if keep:
                print(f"  {os.path.basename(dst)}: kept (its pose fields agree)")
            elif not dry:
                json.dump(s, open(dst, "w"), indent=1)
                print(f"  {os.path.basename(dst)}: written")
    print(f"{key}: {counts['nodes']} node poses, {counts['mesh']} mesh poses, {counts['rest']} at rest -> {vdst}"
          f" ({total // 1024} KB){' (dry run)' if dry else ''}")


if __name__ == "__main__":
    main()
