#!/usr/bin/env python3
"""Render the redesigned WALL (tools/models/wall.py) into a 2D art pack: the 16 wall-link frames and the 16 damaged
frames of the three wall arts (ATREIDES / HARKONNEN / ORDOS WALL share one design, as in the original).

  .venv/bin/python -I tools/models/wall_pack.py --pack artpacks/opend2k-redesign --models artpacks/models-wall
      [--samples 48] [--keep-raw]

--models = wall.py's output (2d/WALL[_damaged]_mMM.glb: one model per link mask MM = W 1 | N 2 | E 4 | S 8, the
north / south arms stretched for the 2D camera). Mask MM goes to wall frame kWallFrameMapping[MM] (MapRenderer::
drawBuilding), i.e. DATA frame first + 1 + frame (damaged: first + 1 + 16 + frame). The passes and post-processing
are tools/render_sprites' (render_pack.process_view, the camera / sun of tools/models/atreides_check/pack.py), the
frames get their own hotspots (DATA/hotspots.txt); pack.ini is left alone. Back the pack's DATA up first.
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "render_sprites"))
import render_pack as RP  # noqa: E402
import upscale_art as U  # noqa: E402
import make_refs as M  # noqa: E402

WALL_FRAME_MAPPING = [0, 2, 3, 12, 1, 6, 11, 7, 4, 13, 5, 9, 15, 10, 8, 14]
FLAGS = dict(view_transform="Standard", look="High Contrast", exposure=0.0, house_grey=0.55, sky_strength=0.13,
             shadow_azimuth=20.0, sun_elevation=58.0, sun_strength=4.8, fill=(0.5, 90.0, 35.0),
             contour=0.0, contour_width=0.75, contour_azimuth=-60.0, sharpen=0.7, shadow_strength=1.0)
# contour 0: a frame's silhouette rim would darken the middle of a wall run where two frames meet


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pack", required=True)
    ap.add_argument("--models", required=True)
    ap.add_argument("--data", default=os.path.join(REPO, "game", "Dune 2000", "data"))
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--samples", type=int, default=48)
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--blender", default="")
    ap.add_argument("--keep-raw", action="store_true")
    args = ap.parse_args()
    t0 = time.time()
    sc, F = args.scale, FLAGS
    frames = U.parse_r16(os.path.join(args.data, "DATA.R16"))
    idx = M.image_indexes(args.data, frames)
    _units, blds = M.read_templates(args.data)
    firsts, anchor = [], None
    for b in blds:
        if b["name"] and b["group"] == "WALL":
            firsts.append(idx["building"][b["art"]][0])
            cx, cy = M.footprint(b["occupied"])["centre_px"]
            anchor = [0 - cx, b["art_height"] - cy]
    firsts = sorted(set(firsts))
    if not firsts:
        raise SystemExit("no WALL templates")
    sun_e = math.radians(F["sun_elevation"])
    sky_lum = 0.2126 * 0.55 + 0.7152 * 0.62 + 0.0722 * 0.75
    e_sun = F["sun_strength"] * math.sin(sun_e)
    e_sky = math.pi * F["sky_strength"] * sky_lum + F["fill"][0] * math.sin(math.radians(F["fill"][2]))
    st = dict(elev=RP.ELEVATION, scale=sc, samples=args.samples, device=args.device,
              sun_strength=F["sun_strength"], sun_elevation=F["sun_elevation"], shadow_azimuth=F["shadow_azimuth"],
              sky_strength=F["sky_strength"], view_transform=F["view_transform"], look=F["look"], exposure=F["exposure"],
              house_grey=F["house_grey"], filter_width=1.2, mask_samples=16,
              fill_strength=F["fill"][0], fill_azimuth=F["fill"][1], fill_elevation=F["fill"][2],
              shadow_core=e_sun / (e_sun + e_sky), shadow_strength=F["shadow_strength"],
              sharpen=F["sharpen"], sharpen_radius=0.6 * sc / 4 + 0.4,
              contour=[F["contour"], F["contour"]], contour_width=max(1, int(round(F["contour_width"] * sc))),
              contour_azimuth=F["contour_azimuth"])
    raw = os.path.join(args.pack, "raw_wall")
    shutil.rmtree(raw, ignore_errors=True)
    os.makedirs(raw, exist_ok=True)
    tasks, jobs = [], []
    for damaged in (False, True):
        for m in range(16):
            name = f"WALL{'_damaged' if damaged else ''}_m{m:02d}"
            path = os.path.join(args.models, "2d", name + ".glb")
            if not os.path.exists(path):
                raise SystemExit("missing " + path)
            out = os.path.abspath(os.path.join(raw, name))
            tasks.append(dict(model=os.path.abspath(path), adjust={}, part="all", angles=[0.0], clips=None, centre=[0.0, 0.0, 0.0],
                              shadow=True, base_names=[], exclude_turret=False, id=name, out=out))
            jobs.append((damaged, m, out))
    job_path = os.path.join(raw, "job.json")
    json.dump(dict(settings=dict(st, denoise=True), tasks=tasks), open(job_path, "w"), indent=1)
    cmd = [RP.find_blender(args.blender), "-b", "--factory-startup", "--python",
           os.path.join(REPO, "tools", "render_sprites", "blender_render.py"), "--", "--job", job_path]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in proc.stdout:
        if line.startswith(("[", "done:", "Error", "Traceback")) or "Error" in line:
            print("  blender|", line.rstrip(), flush=True)
    if proc.wait() != 0:
        raise SystemExit("Blender failed")
    pw = RP.PackWriter(args.pack, sc)
    n = 0
    for damaged, m, out in jobs:
        size = json.load(open(os.path.join(out, "meta.json")))["size"]
        base, house, shadow, _ = RP.process_view(os.path.join(out, "0.color.png"), os.path.join(out, "0.mask.png"), st, F["contour"])
        ax, ay = int(round(size // 2 + anchor[0] * sc)), int(round(size // 2 + anchor[1] * sc))
        if shadow is not None:
            # no shadow across a linked cell edge: the neighbour's wall continues there, and whichever frame is drawn
            # later would lay this one's shadow (the arm's rounded end) over it as a dark half disc. (The south
            # neighbour is drawn after this frame and covers it.)
            shadow = shadow.copy()
            cell = 32 * sc
            if m & 4:
                shadow[:, ax + cell:] = 0
            if m & 1:
                shadow[:, :ax] = 0
            if m & 2:
                shadow[:max(0, ay - cell), :] = 0
            if not shadow.any():
                shadow = None
        box = RP.crop_box(base, shadow, sc)
        x0, y0, _x1, _y1 = box
        for first in firsts:
            fr = first + 1 + (16 if damaged else 0) + WALL_FRAME_MAPPING[m]
            pw.write(fr, RP.cut(base, box), None if house is None else RP.cut(house, box),
                     None if shadow is None else RP.cut(shadow, box), (ax - x0, ay - y0))
            n += 1
    with open(os.path.join(pw.data, "hotspots.txt"), "w") as f:      # PackWriter.finish would rewrite pack.ini
        f.write("# <frame> <x> <y>: the texel corner of DATA/<frame>.png that lands on the frame's anchor\n")
        for k in sorted(pw.hot):
            f.write(f"{k} {pw.hot[k][0]} {pw.hot[k][1]}\n")
    if not args.keep_raw:
        shutil.rmtree(raw, ignore_errors=True)
    print(f"{n} wall frames for arts starting at {firsts} in {time.time() - t0:.0f} s", flush=True)


if __name__ == "__main__":
    main()
