"""Ordos spice silo - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_silo.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_SILO .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 44: frames 3660..3663 = the four fill levels, 3664 = damaged, no base, no animation;
build-up 38 kept):
  * a squat drum on a flared bright-gold foot ring (lit on the south-west), a near-black belt with bronze piers and
    house-colour panels in its front bays, a bronze eave;
  * the lid: a low dark-bronze dome with a brass boss and radial ribs;
  * on the west wall a window where the spice shows (dark when empty, an orange fill rising in three steps);
  * a pale pipe at the south-east foot.
Poses: the node "fill" (the spice behind the window) scaled up in three steps -> frames 3661..3663.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_SILO"
P = dict(
    c=(0.0, -6.0), r=13.0, ring_h=9.2, h=3.0, win0=158.0, win1=222.0, win_z0=3.6, win_z1=9.2,
)


def build(damaged=False):
    main, fill = [], []
    c, r = P["c"], P["r"]
    rh = P["ring_h"]
    main.append(K.lathe("ring", c, [(r + 3.2, 0.0), (r + 2.6, 1.6), (r + 0.9, 3.6), (r - 0.5, 4.0)], "white", 40))
    main.append(K.cyl("foot", c, r + 3.6, 0.0, 0.7, "foot", 40))
    main.append(K.cyl("belt", c, r - 0.4, 3.4, rh, "tank", 40))
    main.append(K.lathe_loop("eave", c, [(r - 0.6, rh - 1.0), (r + 0.7, rh - 1.0), (r + 0.9, rh + 0.3), (r - 0.6, rh + 0.5)], "bronze", 40))
    for k in range(14):
        a = 12.0 + 360.0 / 14 * k
        if P["win0"] < a < P["win1"]:
            continue
        main.append(K.beam(f"pier{k}", K.polar(c, r + 0.15, a, 3.5), K.polar(c, r + 0.15, a, rh - 0.8), 1.3, 0.9, "rib" if k % 2 else "bronze"))
    for i, (a0, a1) in enumerate(((232.0, 251.0), (257.0, 276.0), (282.0, 301.0), (307.0, 326.0), (332.0, 348.0))):      # house panels in the front bays
        main.append(K.house(K.arc(f"panel{i}", c, r - 0.8, r + 0.1, a0, a1, 3.9, rh - 1.0, "HOUSE", 4), 0.3))
    lid = K.dome("lid", c, r - 0.3, rh + 0.2, P["h"], "bronze_dk", 40, 6)
    main.append(lid)

    def zf(x, y):
        return rh + 0.2 + P["h"] * math.sqrt(max(0.03, 1 - ((x - c[0]) ** 2 + (y - c[1]) ** 2) / ((r - 0.3) ** 2)))
    for i in range(8):                                  # radial ribs on the lid
        a = 22.5 + 45.0 * i
        pts = [K.polar(c, r * t, a) for t in (0.3, 0.55, 0.78, 0.95)]
        for j in range(3):
            main.append(K.beam(f"lidrib{i}_{j}", (*pts[j], zf(*pts[j]) + 0.1), (*pts[j + 1], zf(*pts[j + 1]) + 0.1), 0.9, 0.5, "bronze" if i % 2 else "rib"))
    main.append(K.cyl("boss", c, 4.2, rh + P["h"] - 0.5, rh + P["h"] + 0.9, "bronze", 16, 3.6))
    main.append(K.dome("boss_cap", (c[0] - 0.3, c[1] + 0.3), 3.0, rh + P["h"] + 0.8, 2.0, "gold", 14))
    main.append(K.lathe_loop("lid_hoop", c, [(r * 0.62, zf(c[0] + r * 0.62, c[1]) - 0.1), (r * 0.68, zf(c[0] + r * 0.68, c[1]) - 0.1),
                                             (r * 0.68, zf(c[0] + r * 0.68, c[1]) + 0.5), (r * 0.62, zf(c[0] + r * 0.62, c[1]) + 0.6)], "bronze", 32))
    # the spice window on the west wall: frame, the spice behind it (node "fill": scaled in z by the poses)
    main.append(K.arc("win_sill", c, r - 1.0, r + 1.0, P["win0"] - 3.0, P["win1"] + 3.0, P["win_z0"] - 0.6, P["win_z0"] + 0.3, "roof2", 12))
    for i, a in enumerate((P["win0"] - 2.0, P["win1"] + 2.0)):
        main.append(K.beam(f"win_post{i}", K.polar(c, r + 0.3, a, 3.5), K.polar(c, r + 0.3, a, rh - 0.6), 1.5, 1.1, "roof2"))
    fill.append(K.arc("spice", c, r - 2.4, r + 0.2, P["win0"] + 1.0, P["win1"] - 1.0, P["win_z0"] + 0.3, P["win_z1"], "orange", 12))
    main.extend(K.pipe("valve", [(*K.polar(c, r - 0.5, 318.0), 6.0), (*K.polar(c, r + 3.4, 318.0), 6.0), (*K.polar(c, r + 3.4, 318.0), 0.3)], 1.15, "white"))
    main.extend(K.orb("lamp", (*K.polar(c, r - 3.0, 60.0), rh + 2.6), 1.3))
    if damaged:
        K.hole(main, [lid], (c[0] + 3.0, c[1] + 4.5), 5.0, zf, 3, depth=4.0, panel=("bronze", "roof2"))
        K.hole(main, [lid], (c[0] - 6.5, c[1] - 3.0), 3.2, zf, 5, depth=3.5, panel=("bronze", "roof2"))
        K.scorch(main, (c[0] + 5.0, c[1] - 6.0), 4.0, zf, 7, 4)
        K.wall_gash(main, K.polar(c, r + 0.3, 285.0), 15.0, 4.0, 3.6, 9.0, 9, 0.8)
    return dict(nodes=dict(main=main, fill=fill), origins=dict(fill=(c[0], c[1], P["win_z0"] + 0.3)))


def level(k):
    def pose(nodes):
        nodes["fill"].scale = (1.0, 1.0, k)
    return pose


if __name__ == "__main__":
    K.run(KEY, build, poses=[level(0.36), level(0.68), level(1.0)], rest=level(0.001),
          spec=dict(levels=[-1, 0, 1, 2], flags=K.flags(-0.2, shadow_azimuth=25.0)), P=P)
