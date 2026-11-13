#!/usr/bin/env python3
"""The shipped Graphics previews (assets/previews, written by `d2k --render-previews`): stamp / check.

  check_previews.py [--previews assets/previews] [--pack DIR ...] [--pack-root artpacks]
      Checks that assets/previews/index.json still matches this engine and the art pack(s) it was
      rendered with: the engine version (CMakeLists.txt project VERSION), and for every pack in the
      index its fingerprint (file names + sizes + pack.ini / models.ini text; the game computes the
      same, GraphicsPreviews::packFingerprint) and its content sha256 (every file of DATA/, models/,
      variants/ + the two .ini files). Exit 0 = up to date, 1 = stale (a big warning with the command
      that regenerates the set), 2 = no index.

  check_previews.py --stamp [--previews ...] [--pack-root ...] [--no-optimize]
      After a render: shrinks the PNGs (256-colour palette with dithering - the cards are small; the
      originals stay below ~15 MB this way), adds each pack's content sha256 and the total size to
      index.json. Needs Pillow (the project venv).

tools/release/render_previews.sh runs the render and the stamp; tools/release/package_*.sh run the check.
"""
import argparse
import hashlib
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SUBDIRS = ("DATA", "models", "variants")
INIS = ("pack.ini", "models.ini")
REGENERATE = "tools/release/render_previews.sh   (needs the game data, the art pack and a build; then commit assets/previews)"


def pack_files(pack):
    out = []
    for sub in SUBDIRS:
        root = os.path.join(pack, sub)
        if not os.path.isdir(root):
            continue
        for dp, dn, fn in os.walk(root, followlinks=True):
            for f in fn:
                if f == ".DS_Store":
                    continue
                p = os.path.join(dp, f)
                if os.path.isfile(p):
                    out.append((os.path.relpath(p, pack).replace(os.sep, "/").encode("utf-8"), p))
    out.sort()
    return out


def fnv1a64(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def fingerprint(pack):
    """The same bytes as GraphicsPreviews::packFingerprint (src/platform/preset_previews.cpp)."""
    parts = []
    for ini in INIS:
        p = os.path.join(pack, ini)
        text = open(p, "rb").read() if os.path.isfile(p) else b""
        parts.append(ini.encode() + b"\n" + text + b"\n")
    for rel, p in pack_files(pack):
        parts.append(rel + b"\t" + str(os.path.getsize(p)).encode() + b"\n")
    return fnv1a64(b"".join(parts))


def content_sha256(pack):
    h = hashlib.sha256()
    files = [(ini.encode(), os.path.join(pack, ini)) for ini in INIS if os.path.isfile(os.path.join(pack, ini))]
    for rel, p in files + pack_files(pack):
        h.update(rel + b"\0")
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        h.update(b"\0")
    return h.hexdigest()


def engine_version():
    t = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"^project\(d2k VERSION ([0-9.]+)", t, re.M)
    return m.group(1) if m else ""


def find_pack(name, explicit, pack_root):
    for p in explicit:
        if os.path.basename(os.path.normpath(p)) == name:
            return p
    p = os.path.join(pack_root, name)
    return p if os.path.isfile(os.path.join(p, "pack.ini")) else None


def stamp(args, idx, index_path):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("--stamp needs Pillow (use the project venv: .venv/bin/python -I tools/release/check_previews.py --stamp)")
    total = 0
    for pic in idx["pictures"]:
        path = os.path.join(args.previews, pic["file"])
        if not args.no_optimize:
            im = Image.open(path).convert("RGB")
            q = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
            buf = io.BytesIO()
            q.save(buf, "PNG", optimize=True)
            if buf.tell() < os.path.getsize(path):
                open(path, "wb").write(buf.getvalue())
        total += os.path.getsize(path)
    for name, info in idx.get("packs", {}).items():
        pack = find_pack(name, args.pack, args.pack_root)
        if not pack:
            print(f"warning: pack {name} not found under {args.pack_root}: no content sha256 in the index")
            continue
        fp = fingerprint(pack)
        if fp != info.get("fingerprint"):
            sys.exit(f"error: {name}: the fingerprint here ({fp}) differs from the one the game computed "
                     f"({info.get('fingerprint')}): the previews were rendered with another copy of the pack")
        info["content_sha256"] = content_sha256(pack)
    idx["total_bytes"] = total
    with open(index_path, "w", encoding="utf-8") as f:
        json.dump(idx, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"assets/previews: {len(idx['pictures'])} pictures, {total / 1048576:.1f} MB "
          f"({sum(1 for p in idx['pictures'] if p.get('watermark'))} with the watermark)")


def check(args, idx):
    problems = []
    ev = engine_version()
    if idx.get("engine_version") != ev:
        problems.append(f"rendered by engine {idx.get('engine_version')}, this is {ev}")
    for name, info in idx.get("packs", {}).items():
        pack = find_pack(name, args.pack, args.pack_root)
        if not pack:
            print(f"note: pack {name} is not here ({args.pack_root}/{name}): its previews cannot be verified")
            continue
        fp = fingerprint(pack)
        if fp != info.get("fingerprint"):
            problems.append(f"art pack {name} changed since the render (fingerprint {info.get('fingerprint')} -> {fp}: "
                            "files added / removed / resized, or pack.ini / models.ini edited)")
            continue
        want = info.get("content_sha256")
        if not want:
            problems.append(f"art pack {name}: no content sha256 in the index (the render was not stamped)")
        elif content_sha256(pack) != want:
            problems.append(f"art pack {name}: file contents changed since the render (models / frames re-exported)")
    if problems:
        bar = "!" * 100
        print(f"\n{bar}\n!!  STALE GRAPHICS PREVIEWS (assets/previews)")
        for p in problems:
            print(f"!!   - {p}")
        print(f"!!  Regenerate them and commit:\n!!     {REGENERATE}\n{bar}\n")
        return 1
    print(f"assets/previews: up to date ({len(idx['pictures'])} pictures, engine {ev}, "
          f"packs: {', '.join(idx.get('packs', {})) or 'none'})")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--previews", default=os.path.join(REPO, "assets", "previews"))
    ap.add_argument("--pack", action="append", default=[], help="a pack folder to compare (default: <pack-root>/<name>)")
    ap.add_argument("--pack-root", default=os.path.join(REPO, "artpacks"))
    ap.add_argument("--stamp", action="store_true")
    ap.add_argument("--no-optimize", action="store_true")
    args = ap.parse_args()
    index_path = os.path.join(args.previews, "index.json")
    if not os.path.isfile(index_path):
        print(f"no {index_path}: render the previews first:\n   {REGENERATE}")
        return 2
    idx = json.load(open(index_path, encoding="utf-8"))
    if args.stamp:
        stamp(args, idx, index_path)
        return 0
    return check(args, idx)


if __name__ == "__main__":
    sys.exit(main())
