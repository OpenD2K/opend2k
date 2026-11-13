#!/usr/bin/env python3
"""Downloads the CC0 textures of the "Designed" terrains (docs/3d-mode.md "Terrain v2" / "Terrain v3").

Everything fetched here is CC0 (public domain): ambientCG (https://ambientcg.com, "All assets are
provided under the Creative Commons CC0 1.0 Universal License") and Poly Haven
(https://polyhaven.com/license, CC0). The files are NOT checked in (assets/terrain/.gitignore):
this script puts them where the game looks for them:

    assets/terrain/materials/<role>/albedo.jpg   colour (ambientCG *_Color)
                                    normal.jpg   OpenGL normal map (*_NormalGL)
                                    height.jpg   displacement (height blending between materials)
                                    ao.jpg       ambient occlusion (multiplied into the colour)
    assets/terrain/rocks/<name>.jpg              Poly Haven rock scan diffuse (1k)

The rock GEOMETRY (assets/terrain/rocks/<name>.glb, decimated scans, small) is checked in; it is
regenerated with  --rocks  (downloads the Poly Haven glTF and runs tools/terrain_rocks_blender.py
in Blender, which must be on the PATH).

Standard library only.  Usage:
    python3 tools/fetch_terrain_assets.py            # textures (about 30 MB), skips files present
    python3 tools/fetch_terrain_assets.py --force    # download again
    python3 tools/fetch_terrain_assets.py --rocks    # also rebuild the rock meshes (needs Blender)
"""

import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "terrain")
UA = {"User-Agent": "Mozilla/5.0 (OpenD2K terrain asset fetch)"}

# role -> ambientCG asset id (1K-JPG). Roles are what render3d/terrain_v2.cpp loads.
MATERIALS = {
    "sand": "Ground080",      # fine wind-blown sand
    "dunes": "Ground097",     # wind ripples (dune cells, terrain type 2)
    "rock": "Rock029",        # flaky desert rock: buildable plateau tops
    "cliff": "Rock061",       # layered brown rock: cliff faces / impassable rock
    "rough": "Rock049",       # rough brown rock: rough / infantry-only rock (terrain 3, 5)
    "spice": "Gravel025",     # orange-red grit: spice fields
    "concrete": "Concrete040",  # rough beige concrete: concrete slabs
    # "Designed (faithful)" only (docs/3d-mode.md "Terrain v3"): jagged, blocky cliff rock for the
    # crags (drawn in the original tiles' own tones). Optional: without it the cliff material is used.
    "crag": "Rock026",
}
MAPS = {"albedo": "_Color.jpg", "normal": "_NormalGL.jpg", "height": "_Displacement.jpg", "ao": "_AmbientOcclusion.jpg"}

# Poly Haven rock scans (models): name -> asset id. Diffuse 1k jpg; geometry with --rocks.
ROCKS = {
    "boulder_a": "namaqualand_boulder_02",
    "boulder_b": "namaqualand_boulder_03",
    "boulder_c": "namaqualand_boulder_04",
    "boulder_d": "namaqualand_boulder_05",
    "boulder_e": "namaqualand_boulder_06",
    "cliff_a": "namaqualand_cliff_01",
}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        return r.read()


def fetch_materials(force):
    for role, asset in MATERIALS.items():
        d = os.path.join(OUT, "materials", role)
        if not force and all(os.path.isfile(os.path.join(d, m + ".jpg")) for m in MAPS):
            print(f"  {role}: {asset} (present)")
            continue
        url = f"https://ambientcg.com/get?file={asset}_1K-JPG.zip"
        print(f"  {role}: {asset} <- {url}")
        z = zipfile.ZipFile(io.BytesIO(get(url)))
        os.makedirs(d, exist_ok=True)
        for m, suffix in MAPS.items():
            name = f"{asset}_1K-JPG{suffix}"
            if name not in z.namelist():
                print(f"    missing {name}", file=sys.stderr)
                continue
            with z.open(name) as src, open(os.path.join(d, m + ".jpg"), "wb") as dst:
                shutil.copyfileobj(src, dst)


def polyhaven_files(asset):
    return json.loads(get(f"https://api.polyhaven.com/files/{asset}"))


def fetch_rock_textures(force):
    d = os.path.join(OUT, "rocks")
    os.makedirs(d, exist_ok=True)
    for name, asset in ROCKS.items():
        out = os.path.join(d, name + ".jpg")
        if not force and os.path.isfile(out):
            print(f"  rock {name}: {asset} (present)")
            continue
        url = polyhaven_files(asset)["Diffuse"]["1k"]["jpg"]["url"]
        print(f"  rock {name}: {asset} <- {url}")
        with open(out, "wb") as f:
            f.write(get(url))


def build_rocks():
    blender = shutil.which("blender")
    if not blender:
        sys.exit("--rocks needs Blender on the PATH")
    script = os.path.join(ROOT, "tools", "terrain_rocks_blender.py")
    for name, asset in ROCKS.items():
        files = polyhaven_files(asset)["gltf"]["1k"]["gltf"]
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "src")
            os.makedirs(src)
            gltf = os.path.join(src, os.path.basename(files["url"]))
            with open(gltf, "wb") as f:
                f.write(get(files["url"]))
            for rel, inc in files["include"].items():
                if rel.endswith(".bin"):  # geometry only; the diffuse comes from fetch_rock_textures
                    p = os.path.join(src, rel)
                    os.makedirs(os.path.dirname(p), exist_ok=True)
                    with open(p, "wb") as f:
                        f.write(get(inc["url"]))
            out = os.path.join(OUT, "rocks", name + ".glb")
            print(f"  rock {name}: decimating {asset} -> {out}")
            r = subprocess.run([blender, "-b", "--factory-startup", "--python-exit-code", "1", "-P", script, "--", gltf,
                                out], check=True, stdout=subprocess.PIPE, text=True)
            print("".join("    " + l + "\n" for l in r.stdout.splitlines() if l.startswith("rock ")), end="")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="download again even if present")
    ap.add_argument("--rocks", action="store_true", help="rebuild the rock meshes with Blender")
    a = ap.parse_args()
    print("Terrain materials (ambientCG, CC0):")
    fetch_materials(a.force)
    print("Rock textures (Poly Haven, CC0):")
    fetch_rock_textures(a.force)
    if a.rocks:
        build_rocks()
    print("done:", OUT)


if __name__ == "__main__":
    main()
