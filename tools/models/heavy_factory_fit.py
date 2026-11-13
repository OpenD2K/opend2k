#!/usr/bin/env python3
"""Fit the shared heavy factory layout (tools/models/heavy_factory_kit.py) to an original's silhouette, without Blender.

  .venv/bin/python -I tools/models/heavy_factory_fit.py REF/orig.npz [--house atreides|harkonnen|ordos|mercenary]
      [--set key=value ...] [--rounds 6] [--fix key,key]

REF/orig.npz: the building as the game composes it (tools/models/<house>_check/ref.py; local scratch, never commit).
A height-field proxy of the massing (podium with its slot and free-cell notches, hall, roof domes / gable, apron) is
projected with the game camera (a column at (x, y) of height z covers v = y .. y + z) and compared with idle_opaque;
coordinate descent on c, ang, hl, hw, round, ph, hall, hall_h under the footprint's limits (x within +-48.4 px of the
cells' centre line, north edge inside the box). Prints the IoU and the P.update(...) line for the generator.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import heavy_factory_kit as HF  # noqa: E402

OX, OY, W, H = 96.0, 145.6, 192, 240          # canvas of *_check/cfg.py for the 3 x 4 heavy factory
BOX = (-48.0, 48.0, -54.4, 73.6)              # the cells' box in model px (x0, x1, y0, y1)
STEP = 0.5

# roof furniture per house: (t, s, r, h) domes relative to the hall (fractions of its size), as the generators place them
DOMES = dict(
    atreides=lambda h0, h1, s0, s1: [(h0 + 8.8, s1 - 8.4, 7.4, 8.2), (h0 + 9.2, s0 + 8.8, 6.3, 7.5)],
    harkonnen=lambda h0, h1, s0, s1: [(h0 + 9.6, s1 - 9.0, 8.8, 9.4), (h0 + 10.0, s0 + 9.6, 8.4, 9.0)],
    ordos=lambda h0, h1, s0, s1: [(h0 + 10.5, s1 - 10.0, 9.6, 10.6), (h0 + 10.5, s0 + 10.0, 9.2, 10.2)],
    mercenary=lambda h0, h1, s0, s1: [],
)
GABLE = dict(mercenary=7.0)


def proxy(P_, house):
    P_ = HF.derive(P_)
    cx = HF.Ctx(None, P_, False, {})
    xs = np.arange(-60.0, 60.0, STEP) + STEP / 2
    ys = np.arange(-70.0, 90.0, STEP) + STEP / 2
    X, Y = np.meshgrid(xs, ys)
    dx, dy = X - P_["c"][0], Y - P_["c"][1]
    T, S = dx * cx.ca + dy * cx.sa, -dx * cx.sa + dy * cx.ca
    hl, hw, r, ph, d = P_["hl"], P_["hw"], P_["round"], P_["ph"], P_["inset"]
    # distance inside the rounded rectangle (> 0 inside)
    qx, qy = np.abs(T) - (hl - r), np.abs(S) - (hw - r)
    dist = r - (np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0))
    inside = dist > 0
    (in_w, _aw), (in_e, _ae) = HF.free_wedges(cx)
    wt, ws = cx.ts(*P_["free_w"])
    et, es = cx.ts(*P_["free_e"])
    free = (((T - wt) * -cx.ca + (S - ws) * cx.sa > 0) & ((T - wt) * cx.sa + (S - ws) * cx.ca > 0)) | \
           (((T - et) * cx.ca - (S - es) * cx.sa > 0) & ((T - et) * cx.sa + (S - es) * cx.ca > 0))
    b0, b1, bs = P_["bay"]
    h0, h1, hs0, hs1 = P_["hall"]
    slot = (T > b0) & (T < b1) & (S < bs)
    Z = np.where(inside, np.clip(dist / d, 0, 1) * ph, 0.0)
    Z = np.where(slot, 0.0, Z)
    Z = np.where(free & inside, np.minimum(Z, 1.8), Z)
    hall = (T > h0) & (T < h1) & (S > hs0) & (S < hs1) & ~free
    top = P_["hall_h"] + 1.0
    if house in GABLE:
        sm, hwd = (hs0 + hs1) / 2, (hs1 - hs0) / 2 + 0.8
        roof = P_["hall_h"] + 0.2 + GABLE[house] * (1 - np.minimum(1.0, np.abs(S - sm) / hwd))
        Z = np.where(hall, np.maximum(Z, roof), Z)
    else:
        Z = np.where(hall, np.maximum(Z, top), Z)
    for (t, s, rr, hh) in DOMES[house](h0, h1, hs0, hs1):
        q = 1 - ((T - t) ** 2 + (S - s) ** 2) / rr ** 2
        Z = np.where((q > 0) & ~free, np.maximum(Z, top + hh * np.sqrt(np.maximum(q, 0))), Z)
    ae, tp = P_["apron_end"], P_["apron_taper"]
    k = np.clip((-hw - S) / max(1e-6, -hw - ae), 0, 1) * tp
    apron = (S < hs0 + 1) & (S > ae) & (T > b0 + k) & (T < b1 - k)
    Z = np.where(apron | inside, np.maximum(Z, 0.5), Z)
    solid = Z > 0
    # project: column (x, y, z) covers v = y .. y + z at screen x
    mask = np.zeros((H, W), bool)
    rows = (np.arange(H) + 0.5)
    V = (OY - rows) / 0.70711                    # v of each screen row
    cols = np.round(xs + OX - 0.5).astype(int)
    lo = np.where(solid, Y, np.inf)
    hi = np.where(solid, Y + Z, -np.inf)
    for j in np.unique(cols):
        if j < 0 or j >= W:
            continue
        sel = cols == j
        l, h = lo[:, sel].ravel(), hi[:, sel].ravel()
        ok = np.isfinite(l)
        if not ok.any():
            continue
        l, h = l[ok], h[ok]
        mask[:, j] = ((V[:, None] >= l[None, :] - 0.36) & (V[:, None] <= h[None, :] + 0.36)).any(1)
    bounds = (X[solid].min(), X[solid].max(), Y[solid].min(), Y[solid].max())
    return mask, bounds, P_


def score(P_, house, ref):
    try:
        m, b, _ = proxy(P_, house)
    except Exception:
        return -1.0
    if b[0] < BOX[0] - 0.4 or b[1] > BOX[1] + 0.4 or b[3] > BOX[3] + 0.4 or b[2] < BOX[2]:
        return -1.0 + 0.001 * min(0, BOX[1] + 0.4 - b[1], b[0] - BOX[0] + 0.4)
    return (m & ref).sum() / max(1, (m | ref).sum())


def main():
    a = sys.argv[1:]
    ref = np.load(a[0])["idle_opaque"]
    house = a[a.index("--house") + 1] if "--house" in a else "atreides"
    rounds = int(a[a.index("--rounds") + 1]) if "--rounds" in a else 6
    fixed = set(a[a.index("--fix") + 1].split(",")) if "--fix" in a else set()
    P_ = dict(HF.P)
    for i, v in enumerate(a):
        if v == "--set":
            k, val = a[i + 1].split("=", 1)
            P_[k] = eval(val)        # noqa: S307 (own CLI)
    keys = [("c", 0), ("c", 1), ("ang", None), ("hl", None), ("hw", None), ("round", None), ("ph", None), ("hall", 0), ("hall", 1), ("hall", 2), ("hall", 3),
            ("hall_h", None), ("apron_end", None)]
    keys = [k for k in keys if k[0] not in fixed]

    def get(P__, k):
        return P__[k[0]] if k[1] is None else P__[k[0]][k[1]]

    def put(P__, k, v):
        Q = dict(P__)
        if k[1] is None:
            Q[k[0]] = v
        else:
            t = list(Q[k[0]])
            t[k[1]] = v
            Q[k[0]] = tuple(t)
        return Q
    best = score(P_, house, ref)
    print(f"start IoU {best:.4f}")
    step = 2.0
    for rnd in range(rounds):
        for k in keys:
            for sgn in (1, -1):
                while True:
                    Q = put(P_, k, round(get(P_, k) + sgn * step, 2))
                    if Q["round"] < 4 or Q["round"] > min(Q["hl"], Q["hw"]) - 2 or Q["ph"] < 6 or Q["apron_end"] > -Q["hw"] - 3.0:
                        break
                    sc = score(Q, house, ref)
                    if sc > best + 1e-4:
                        best, P_ = sc, Q
                    else:
                        break
        print(f"round {rnd} step {step}: IoU {best:.4f}")
        step = max(0.5, step / 2)
    D = HF.derive(P_)
    _m, b, _ = proxy(P_, house)
    print("bounds x %.1f..%.1f y %.1f..%.1f" % b)
    print("P.update(" + ", ".join(f"{k}={P_[k]!r}" for k in ("c", "ang", "hl", "hw", "round", "ph", "hall", "hall_h", "apron_end")) + ")")
    print("derived bay", tuple(round(v, 1) for v in D["bay"]), "hall", tuple(round(v, 1) for v in D["hall"]))


if __name__ == "__main__":
    main()
