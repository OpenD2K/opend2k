#!/usr/bin/env python3
"""Graft the emission atlas of a regenerated building glb onto the pack's glb (docs/3d-models.md "Emission").

  .venv/bin/python -I tools/models/graft_emission.py NEW.glb PACK.glb [--out OUT.glb] [--force]

NEW = the same generator re-run with emission (combat_tank_v2.EMIT -> emissiveTexture). The pack glbs were baked on
other machines / devices / bake sizes, so their colour atlases differ from a fresh bake by Cycles noise; the meshes
and UVs do not (the generators are deterministic). Instead of replacing the owner's approved texture, this copies
NEW's emissiveTexture into PACK (resampled to PACK's base colour size) after checking that both have the same
POSITION and TEXCOORD_0 data per primitive (refuses otherwise unless --force). Writes PACK in place unless --out.
"""
import argparse
import io
import json
import struct
import sys

from PIL import Image


def read_glb(p):
    d = open(p, "rb").read()
    n = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + n])
    o = 20 + n
    bn = struct.unpack("<I", d[o:o + 4])[0]
    return j, bytearray(d[o + 8:o + 8 + bn])


def write_glb(p, j, b):
    while len(b) % 4:
        b.append(0)
    j["buffers"][0]["byteLength"] = len(b)
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(b)
    with open(p, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(b), 0x004E4942) + bytes(b))


def view_bytes(j, b, bv_index):
    bv = j["bufferViews"][bv_index]
    o = bv.get("byteOffset", 0)
    return bytes(b[o:o + bv["byteLength"]])


def accessor_bytes(j, b, ai):
    a = j["accessors"][ai]
    bv = j["bufferViews"][a["bufferView"]]
    comp = {5126: 4, 5123: 2, 5125: 4, 5121: 1}[a["componentType"]]
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    o = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    return bytes(b[o:o + a["count"] * comp * ncomp])


def geometry(j, b):
    out = []
    for m in j["meshes"]:
        for pr in m["primitives"]:
            at = pr["attributes"]
            out.append((accessor_bytes(j, b, at["POSITION"]), accessor_bytes(j, b, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else b""))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("new")
    ap.add_argument("pack")
    ap.add_argument("--out", default="")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    jn, bn = read_glb(a.new)
    jp, bp = read_glb(a.pack)
    src = next((m for m in jn.get("materials", []) if "emissiveTexture" in m), None)
    if src is None:
        sys.exit(f"{a.new}: no emissiveTexture (nothing glows in this model)")
    if geometry(jn, bn) != geometry(jp, bp) and not a.force:
        sys.exit(f"{a.new} / {a.pack}: the meshes or UVs differ - not the same model build (--force to graft anyway)")
    dst = next((m for m in jp["materials"] if m.get("name") == src.get("name")), None)
    if dst is None or "baseColorTexture" not in dst.get("pbrMetallicRoughness", {}):
        sys.exit(f"{a.pack}: no textured material {src.get('name')!r}")
    tex_n = jn["textures"][src["emissiveTexture"]["index"]]
    em = Image.open(io.BytesIO(view_bytes(jn, bn, jn["images"][tex_n["source"]]["bufferView"]))).convert("RGB")
    tex_p = jp["textures"][dst["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
    base = Image.open(io.BytesIO(view_bytes(jp, bp, jp["images"][tex_p["source"]]["bufferView"])))
    if em.size != base.size:
        em = em.resize(base.size, Image.BILINEAR)
    png = io.BytesIO()
    em.save(png, "PNG", optimize=True)
    data = png.getvalue()
    for k in ("emissiveTexture", "emissiveFactor"):     # a previous graft
        dst.pop(k, None)
    while len(bp) % 4:
        bp.append(0)
    off = len(bp)
    bp += data
    jp["bufferViews"].append(dict(buffer=0, byteOffset=off, byteLength=len(data)))
    jp["images"].append(dict(bufferView=len(jp["bufferViews"]) - 1, mimeType="image/png", name="tank_emit"))
    t = dict(source=len(jp["images"]) - 1)
    if "sampler" in tex_p:
        t["sampler"] = tex_p["sampler"]
    jp["textures"].append(t)
    dst["emissiveTexture"] = dict(index=len(jp["textures"]) - 1)
    dst["emissiveFactor"] = src.get("emissiveFactor", [1.0, 1.0, 1.0])
    write_glb(a.out or a.pack, jp, bp)
    print(f"grafted {em.size[0]}x{em.size[1]} emission ({len(data) // 1024} KB) -> {a.out or a.pack}")


if __name__ == "__main__":
    main()
