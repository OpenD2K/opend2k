#!/usr/bin/env python3
"""Check the refineries' docks against the harvester model, without starting the game (docs/3d-models.md
"Docking"; the numbers are tools/models/refinery_dock.py's).

  .venv/bin/python -I tools/models/refinery_dock_check.py PACK [--refinery FILE.glb ...] [--harvester PACK2] [-v]

For every *REFINERY.glb of PACK/models/building (or the given files) and the harvester of the pack
(models/unit/HARVESTER.glb + variants/HARVESTER_unloadNN.glb):
  * the plan bounds against the 3 x 3 cells (no fit move, no shrink expected: inside +-48 px);
  * the deck the harvester stands on at the dock cell's centre (as render3d/models.cpp deckAt: the highest
    surface <= 8 px within 5 px) and what the pad has above it under the harvester;
  * the slide: plan distance from the pipe's mouth of the harvester on the dock centre (facing 12, last unloading
    pose) to the anchor fx_inlet_0, and their height difference;
  * intersections: refinery surface points inside the harvester's body (a solid: every column of its plan filled
    between its lowest and highest point) - turning on the dock centre through all 32 facings in the driving
    pose, and docked at facing 12 in every unloading pose (with the engine's slide). 0 points = clear.
Exit code 1 if anything intersects, the model leaves its cells by more than 0.5 px or the slide is over 3 px.
"""
import glob
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refinery_dock as RD  # noqa: E402


def _quat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def read_glb(path):
    """-> (triangles (n, 3, 3) px in the glb axes: x east, y up, z south, {anchor: (x, y, z px, size)})"""
    d = open(path, "rb").read()
    jl = struct.unpack_from("<I", d, 12)[0]
    J = json.loads(d[20:20 + jl])
    bin0 = 20 + jl + 8
    tris, anchors = [], {}

    def walk(i, M):
        n = J["nodes"][i]
        L = np.eye(4)
        if "matrix" in n:
            L = np.array(n["matrix"]).reshape(4, 4).T
        else:
            L[:3, :3] = _quat(n.get("rotation", [0, 0, 0, 1])) @ np.diag(n.get("scale", [1, 1, 1]))
            L[:3, 3] = n.get("translation", [0, 0, 0])
        M2 = M @ L
        if n.get("name", "").startswith("fx_"):
            anchors[n["name"]] = np.append(M2[:3, 3] * 32, np.linalg.norm(M2[:3, 0]))   # x, y, z px, size
        if "mesh" in n:
            for pr in J["meshes"][n["mesh"]]["primitives"]:
                a = J["accessors"][pr["attributes"]["POSITION"]]
                bv = J["bufferViews"][a["bufferView"]]
                o = bin0 + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
                st = bv.get("byteStride", 12)
                p = np.frombuffer(d, dtype="<f4", count=a["count"] * 3, offset=o).reshape(-1, 3) if st == 12 else \
                    np.array([struct.unpack_from("<3f", d, o + k * st) for k in range(a["count"])])
                if "indices" in pr:
                    ia = J["accessors"][pr["indices"]]
                    ibv = J["bufferViews"][ia["bufferView"]]
                    dt = {5121: "u1", 5123: "<u2", 5125: "<u4"}[ia["componentType"]]
                    p = p[np.frombuffer(d, dtype=dt, count=ia["count"], offset=bin0 + ibv.get("byteOffset", 0) + ia.get("byteOffset", 0)).astype(int)]
                tris.append(((p @ M2[:3, :3].T + M2[:3, 3]) * 32).reshape(-1, 3, 3))
        for c in n.get("children", []):
            walk(c, M2)
    for r in J["scenes"][0]["nodes"]:
        walk(r, np.eye(4))
    return np.concatenate(tris), anchors


def sample(tris, step=0.5):
    """Points on the triangles, about `step` px apart."""
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    n = np.ceil(np.maximum(np.linalg.norm(b - a, axis=1), np.maximum(np.linalg.norm(c - a, axis=1), np.linalg.norm(c - b, axis=1))) / step).astype(int)
    n = np.clip(n, 1, 400)
    out = []
    for k in np.unique(n):
        m = n == k
        i, j = np.meshgrid(np.arange(k + 1), np.arange(k + 1), indexing="ij")
        ok = i + j <= k
        u, v = (i[ok] / k)[None, :, None], (j[ok] / k)[None, :, None]
        out.append((a[m][:, None] * (1 - u - v) + b[m][:, None] * u + c[m][:, None] * v).reshape(-1, 3))
    return np.concatenate(out)


class Harvester:
    """A pose as a solid: per plan cell (0.5 px) the lowest and highest point."""
    RES, HALF = 0.5, 32.0

    def __init__(self, tris):
        p = sample(tris, 0.35)
        n = int(2 * self.HALF / self.RES)
        self.lo = np.full((n, n), 1e9, np.float32)
        self.hi = np.full((n, n), -1e9, np.float32)
        ix = ((p[:, 0] + self.HALF) / self.RES).astype(int)
        iz = ((p[:, 2] + self.HALF) / self.RES).astype(int)
        np.minimum.at(self.lo, (iz, ix), p[:, 1])
        np.maximum.at(self.hi, (iz, ix), p[:, 1])
        self.pts = p
        rear = p[p[:, 2] > p[:, 2].max() - 0.5]
        self.tip = (rear[:, 0].mean(), rear[:, 1].mean(), p[:, 2].max())   # as models.cpp dockTip_: x, y, z

    def inside(self, q, margin=0.0):
        """q: points in the harvester's own axes -> mask of those inside the solid."""
        ix = np.floor((q[:, 0] + self.HALF) / self.RES).astype(int)
        iz = np.floor((q[:, 2] + self.HALF) / self.RES).astype(int)
        n = self.lo.shape[0]
        ok = (ix >= 0) & (iz >= 0) & (ix < n) & (iz < n)
        out = np.zeros(len(q), bool)
        lo, hi = self.lo[iz[ok], ix[ok]], self.hi[iz[ok], ix[ok]]
        out[ok] = (q[ok, 1] >= lo - margin) & (q[ok, 1] <= hi + margin)
        return out


def to_local(pts, cx, cz, deck, facing):
    """Refinery points (glb px) -> the axes of a harvester standing at (cx, cz) on `deck` at `facing` (0..31)."""
    a = facing * 2 * math.pi / 32
    c, s = math.cos(a), math.sin(a)
    dx, dz = pts[:, 0] - cx, pts[:, 2] - cz
    return np.stack([dx * c + dz * s, pts[:, 1] - deck, -dx * s + dz * c], 1)


def deck_at(tris, x, z):
    """render3d/models.cpp buildDeck / deckAt: 2 px grid of the highest surface <= 8 px, max over the nine
    samples at 0 / +-5 px."""
    lowest = tris[:, :, 1].min(1)
    t = tris[lowest <= 8.0]
    nrm = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    t = t[np.abs(nrm[:, 1]) > 1e-6]
    p = sample(t, 0.4)
    p = p[p[:, 1] <= 8.0]
    best = -1.0
    for o in (0.0, -5.0, 5.0):
        for q in (0.0, -5.0, 5.0):
            cx, cz = math.floor((x + o) / 2) * 2 + 1.0, math.floor((z + q) / 2) * 2 + 1.0
            m = (np.abs(p[:, 0] - cx) < 0.45) & (np.abs(p[:, 2] - cz) < 0.45)
            if m.any():
                best = max(best, float(p[m, 1].max()))
    return best


def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    args = [a for a in args if a != "-v"]
    pack = args[0]
    files, hpack = [], pack
    i = 1
    while i < len(args):
        if args[i] == "--refinery":
            files.append(args[i + 1])
        elif args[i] == "--harvester":
            hpack = args[i + 1]
        i += 2
    if not files:
        files = sorted(glob.glob(os.path.join(pack, "models", "building", "*REFINERY.glb")))
    drive = Harvester(read_glb(os.path.join(hpack, "models", "unit", "HARVESTER.glb"))[0])
    poses = [Harvester(read_glb(f)[0]) for f in sorted(glob.glob(os.path.join(hpack, "variants", "HARVESTER_unload*.glb")))]
    last = poses[-1] if poses else drive
    r = np.hypot(drive.pts[:, 0], drive.pts[:, 2])
    print(f"harvester: turning radius {r[drive.pts[:, 1] < 8].max():.1f} px below 8 px, {r[drive.pts[:, 1] >= 8].max():.1f} above; "
          f"top {max(h.pts[:, 1].max() for h in poses + [drive]):.1f}; pipe mouth {last.tip[2]:.2f} px behind the centre, {last.tip[1]:.2f} px up")
    bad = False
    cx, cz = RD.DOCK[0], -RD.DOCK[1]
    for f in files:
        tris, anchors = read_glb(f)
        key = os.path.basename(f)[:-4]
        x0, x1, z0, z1 = tris[:, :, 0].min(), tris[:, :, 0].max(), tris[:, :, 2].min(), tris[:, :, 2].max()
        over = max(-RD.BOX - x0, x1 - RD.BOX, -RD.BOX - z0, z1 - RD.BOX)
        deck = deck_at(tris, cx, cz)
        pts = sample(tris, 0.5)
        near = pts[np.hypot(pts[:, 0] - cx, pts[:, 2] - cz) < 34]
        print(f"{key}: plan x {x0:.1f}..{x1:.1f}, north {-z1:.1f}..{-z0:.1f} (cells +-48; over by {max(over, 0):.2f}); deck at the dock centre {deck:.2f} px")
        if over > 0.5:
            bad = True
        inlet = anchors.get("fx_inlet_0")
        sx = sz = 0.0
        if inlet is None:
            print("   no fx_inlet_0 anchor")
            bad = True
        else:
            a = 12 * 2 * math.pi / 32
            c, s = math.cos(a), math.sin(a)
            nx, nz = cx + c * last.tip[0] - s * last.tip[2], cz + s * last.tip[0] + c * last.tip[2]
            sx, sz = inlet[0] - nx, inlet[2] - nz
            slide = math.hypot(sx, sz)
            dy = inlet[1] - (deck + last.tip[1])
            print(f"   inlet anchor ({inlet[0]:.2f}, {inlet[1]:.2f}, {inlet[2]:.2f}); pipe mouth of the centred harvester ({nx:.2f}, {deck + last.tip[1]:.2f}, {nz:.2f}): "
                  f"slide {slide:.2f} px, socket {dy:+.2f} px above the pipe")
            if slide > 3.0 or abs(dy) > 1.0:
                bad = True
            if slide > 32.0:
                sx = sz = 0.0
        total = 0
        worst = []
        for fc in range(32):                              # turning on the dock centre (driving pose, no slide)
            m = drive.inside(to_local(near, cx, cz, deck, fc))
            m &= near[:, 1] > deck + 0.05
            if m.any():
                total += int(m.sum())
                worst.append((f"turning, facing {fc}", near[m]))
        for k, h in enumerate(poses):                     # docked: pose k = animFrame k, slide eased in over 7 frames
            e = min(1.0, k / 7.0)
            m = h.inside(to_local(near, cx + sx * e, cz + sz * e, deck, 12))
            m &= near[:, 1] > deck + 0.05
            if inlet is not None:                         # the pipe ends inside the socket: its bore and lip do not count
                m &= np.linalg.norm(near - inlet[:3], axis=1) > 4.5
            if m.any():
                total += int(m.sum())
                worst.append((f"docked, pose {k}", near[m]))
        if total:
            bad = True
            print(f"   INTERSECTS: {total} surface points inside the harvester")
            for name, q in worst if verbose else worst[:6]:
                print(f"      {name}: {len(q)} points, x {q[:, 0].min():.1f}..{q[:, 0].max():.1f} north {-q[:, 2].max():.1f}..{-q[:, 2].min():.1f} "
                      f"height {q[:, 1].min():.1f}..{q[:, 1].max():.1f}")
        else:
            print("   clear: nothing inside the harvester (32 facings turning, every unloading pose)")
        # the smallest gap to the keep-out cylinder, for the record
        d = np.hypot(near[:, 0] - cx, near[:, 2] - cz)
        lo = near[(near[:, 1] > deck + 0.05) & (near[:, 1] < RD.Z_LOW)]
        hi = near[(near[:, 1] >= RD.Z_LOW) & (near[:, 1] < RD.Z_HIGH)]
        dl = np.hypot(lo[:, 0] - cx, lo[:, 2] - cz).min() if len(lo) else 99
        dh = np.hypot(hi[:, 0] - cx, hi[:, 2] - cz).min() if len(hi) else 99
        print(f"   nearest part above the deck: {dl:.1f} px from the dock centre below {RD.Z_LOW} px (keep-out {RD.K_LOW}), {dh:.1f} above (keep-out {RD.K_HIGH})")
        del d
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
