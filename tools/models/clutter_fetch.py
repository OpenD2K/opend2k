#!/usr/bin/env python3
"""Downloads the Poly Haven (CC0) scans of the ground clutter pack (tools/models/clutter_kit.py; docs/3d-mode.md
"Ground clutter") as glTF 1k: one new folder dl_<asset id> per asset under DEST (default
~/OpenD2K-assets/clutter-src - outside the repo; downloads are data, never run). Standard library only.

    python3 -I tools/models/clutter_fetch.py [DEST]
"""
import json
import os
import sys
import urllib.request

ASSETS = ["namaqualand_stones_01", "namaqualand_rocks_01", "namaqualand_boulders_01", "rock_07", "rock_09", "stone_01",
          "dead_quiver_branch_01", "dead_quiver_branch_02", "dry_branches_medium_01"]
UA = {"User-Agent": "Mozilla/5.0 (OpenD2K asset fetch)"}


def get(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        return
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        data = r.read()
    with open(path, "wb") as f:
        f.write(data)


def main():
    root = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/OpenD2K-assets/clutter-src")
    for a in ASSETS:
        fj = os.path.join(root, f"files_{a}.json")
        get(f"https://api.polyhaven.com/files/{a}", fj)
        with open(fj) as f:
            g = json.load(f)["gltf"]["1k"]["gltf"]
        out = os.path.join(root, f"dl_{a}")
        get(g["url"], os.path.join(out, os.path.basename(g["url"])))
        for rel, v in g["include"].items():
            get(v["url"], os.path.join(out, rel))
        print(a, "ok")


if __name__ == "__main__":
    main()
