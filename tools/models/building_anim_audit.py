#!/usr/bin/env python3
"""Audit the buildings' working animations: original vs a redesign art pack (2D Enhanced) vs 3D models.

  python3 -I tools/models/building_anim_audit.py --pack artpacks/opend2k-redesign [--data DIR] [--md OUT.md]

Per building template (Templates.bin): the animation overlay the original draws (HAS_ANIMATION 0x40, the
overlay template index, frame count, speed, additive 0x800000, permanent 0x10 from the template), what
drives it (re/notes/combat.md §9: production sets 0x10 on factories, the conyard animates once after every
placement, the repair pad once per repair step, the star port when the frigate arrives, the palace on a
Death Hand launch), and what the pack does: main frame replaced? overlay frames of its own (non-blank)?
blank? the original overlay drawn over the new art? models.ini overlay=off / glow=blink / fx_* anchors;
3D: glb present, pose variants (variants/KEY_poseNN.glb + spec.json anim_frames).
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import upscale_art as U  # noqa: E402

BLDS_OFF, BLD_SIZE = 60 * 256, 268


def cstr(b):
    return b.split(b"\0")[0].decode("latin1").strip()


def read(data):
    d = open(os.path.join(data, "bin", "Templates.bin"), "rb").read()
    t = U.templates_indexes(data)
    bld_count = d[46195]
    bnames = 46196
    o = bnames + 100 * 450 + 64 * 50 + 64 * 50 + 60 * 450 + 60 * 50
    bgroups = o
    bld_anim = list(d[132938:133038])
    s = t["buildup"][-1][0] + t["buildup"][-1][1] if t["buildup"] else 0
    anim_first = []
    for i in range(bld_count):
        anim_first.append((s, bld_anim[i]))
        s += bld_anim[i]
    out = []
    for i in range(bld_count):
        b = BLDS_OFF + i * BLD_SIZE
        art, barrel = struct.unpack_from("<ii", d, b + 20)
        flags = struct.unpack_from("<I", d, b + 88)[0]
        g = d[b + 136]
        out.append(dict(index=i, name=cstr(d[bnames + i * 450:bnames + (i + 1) * 450]),
                        group=cstr(d[bgroups + g * 50:bgroups + (g + 1) * 50]), art=art, barrel=barrel,
                        flags=flags, special=d[b + 92], speed=d[b + 133], anim=d[b + 139]))
    for e in out:
        a = e["anim"]
        e["anim_first"], e["anim_frames"] = anim_first[a] if a < bld_count else (0, 0)
        e["anim_speed"] = out[a]["speed"] if a < bld_count else 0
        e["first"] = t["building"][e["art"]][0] if 0 <= e["art"] < len(t["building"]) else -1
    return out


def read_ini(path):
    secs, cur = {}, None
    if not os.path.exists(path):
        return secs
    for line in open(path, encoding="utf-8", errors="replace"):
        line = re.split(r"[;#]", line, maxsplit=1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            cur = line[1:line.index("]")].strip()
            secs.setdefault(cur, {})
        elif "=" in line and cur:
            k, v = line.split("=", 1)
            secs[cur][k.strip().lower()] = v.strip()
    return secs


TRIGGER = {  # specialBehavior / group -> what sets the overlay's permanent / once flag in the simulation
    "CONSTRUCTION_YARD": "once after every building placed (spawn.cpp addBuilding)",
    "BARRACKS": "permanent while producing infantry (production.cpp)",
    "LIGHT_FACTORY": "permanent while producing (production.cpp)",
    "HEAVY_FACTORY": "permanent while producing (production.cpp)",
    "HIGH_TECH_FACTORY": "permanent while producing (production.cpp)",
    "STAR_PORT": "once per frigate arrival with cargo (world.cpp FlyUnit)",
    "REPAIR_PAD": "once per repair step of a unit on the pad (world.cpp repair)",
    "PALACE": "once on a Death Hand launch (world.cpp); production of the special weapon sets permanent",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--data", default=os.path.join(REPO, "game", "Dune 2000", "data"))
    ap.add_argument("--md", default="")
    a = ap.parse_args()
    blds = read(a.data)
    data_dir = os.path.join(a.pack, "DATA")
    ini = read_ini(os.path.join(a.pack, "models.ini"))
    vdir = os.path.join(a.pack, "variants")
    variants = os.listdir(vdir) if os.path.isdir(vdir) else []

    def has(f):
        return os.path.exists(os.path.join(data_dir, f"{f}.png"))

    def blank(f):
        p = os.path.join(data_dir, f"{f}.png")
        if not os.path.exists(p):
            return False
        from PIL import Image
        im = Image.open(p)
        return im.getbbox() is None or (im.convert("RGBA").getchannel("A").getbbox() is None)

    def glb(key):
        for sub in ("models/building", "building", "models/unit"):
            p = os.path.join(a.pack, sub, key + ".glb")
            if os.path.exists(p):
                return True
        return False

    rows = []
    for e in blds:
        key = e["name"].replace(" ", "_").upper()
        gkey = e["group"].replace(" ", "_").upper()
        mkey = key if glb(key) else (gkey if glb(gkey) else "")
        main = e["first"] >= 0 and (has(e["first"] + 1) or has(e["first"]))
        has_anim = bool(e["flags"] & 0x40)
        frames = list(range(e["anim_first"], e["anim_first"] + e["anim_frames"])) if has_anim else []
        own = [f for f in frames if has(f) and not blank(f)]
        blk = [f for f in frames if has(f) and blank(f)]
        sec = ini.get(key) or ini.get(gkey, {})
        anchors = sorted({re.sub(r"_\d+$", "", k[7:]) for k in sec if k.startswith("anchor.fx_")})
        poses = sorted(v for v in variants if v.startswith((mkey or key) + "_pose"))
        spec = os.path.exists(os.path.join(vdir, (mkey or key) + ".spec.json"))
        if not has_anim:
            d2 = "fx_work lamps (no original overlay)" if "fx_work" in anchors else "-"
        elif not main:
            d2 = "original art (pack has no main frame)"
        elif "fx_weld" in anchors:
            d2 = "fx_weld flashes " + ("+ pack overlay frames (keep)" if sec.get("overlay") == "keep" else "(overlay dropped)")
        elif "fx_launch" in anchors:
            d2 = "fx_launch flame + smoke" + (" (pack frames blank)" if blk else "")
        elif sec.get("overlay", "") in ("off", "0"):
            d2 = "overlay=off" + (" + glow=blink" if sec.get("glow") == "blink" else "")
        elif own:
            d2 = f"pack overlay frames ({len(own)}/{len(frames)})"
        elif blk and len(blk) == len(frames):
            d2 = "blank pack frames (nothing)"
        else:
            d2 = "ORIGINAL 1998 OVERLAY over new art"
        if sec.get("glow") and "glow=" not in d2:
            d2 += " + glow=" + sec["glow"]
        rows.append(dict(e, key=key, mkey=mkey, main=main, own=len(own), blank=len(blk), d2=d2, anchors=anchors,
                         poses=len(poses), spec=spec, ini=sec))
    lines = ["| # | building | group | anim (frames x speed, add, perm) | 2D Enhanced (pack) | 3D model / poses / anchors |",
             "|---|---|---|---|---|---|"]
    for r in rows:
        an = "-" if not r["flags"] & 0x40 else (
            f"#{r['anim']} {r['anim_frames']}x{r['anim_speed']}{' add' if r['flags'] & 0x800000 else ''}"
            f"{' PERM' if r['flags'] & 0x10 else ''}")
        m3 = (r["mkey"] or "no glb") + (f", {r['poses']} poses" if r["poses"] else "") + \
             (", " + "/".join(r["anchors"]) if r["anchors"] else "")
        lines.append(f"| {r['index']} | {r['name']} | {r['group']} | {an} | {r['d2']} | {m3} |")
    txt = "\n".join(lines)
    print(txt)
    if a.md:
        open(a.md, "w").write(txt + "\n")


if __name__ == "__main__":
    main()
