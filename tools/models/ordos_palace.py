"""Ordos palace - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_palace.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_PALACE .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 53: main 3877, damaged 3878, no base, no animation; build-up 56 kept):
  * a tall fortress turned about 40 degrees: a high dark-brown fluted base with house-colour slits on its
    south-east and north-east faces, orange rubble at its feet;
  * along the base's south-west edge a brightly lit pale-gold gallery with a row of dark bays between two rails,
    ending in a tall pale pylon at the south corner;
  * the keep on the north-east part: a tall brass block with pilasters, house-colour window bands and teal-black
    glass orbs with cyan glints on its corners, two roof pavilions with house-colour roofs crossed by bronze
    diagonals, masts.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402

KEY = "ORDOS_PALACE"
P = dict(
    ang=-40.0, c=(-3.0, -9.0), hl=37.0, hw=24.0, base_h=31.0,
         keep=(-7.0, 14.0), keep_hl=24.0, keep_hw=12.0, keep_h=22.0, pav_h=6.0, tier_hl=12.5, tier_hw=8.0, tier_h=9.0,
         gal=(1.0, -23.0), gal_hl=34.0, gal_hw=7.0,
)


def at(t, s=0.0, c=None):
    a = math.radians(P["ang"])
    c = P["c"] if c is None else c
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def build(damaged=False):
    main = []
    A, hl, hw, bh = P["ang"], P["hl"], P["hw"], P["base_h"]
    main.extend(K.skirt("rock", [at(-hl, -hw), at(hl, -hw), at(hl, hw), at(-hl, hw)], 5.0, 3, 3.0, material="stone_dk", dark="block_dk", lumps=18))
    main.append(K.obox("base", P["c"], A, hl, hw, 0.0, bh, "block", 0.8, 0.93))
    main.append(K.house(K.obox("band_se", at(hl * 0.94 + 0.3, 2.0), A + 90.0, hw * 0.8, 0.5, bh - 4.6, bh - 2.2, "HOUSE"), 0.3))
    main.append(K.house(K.obox("band_sw", at(-2.0, -hw * 0.94 + 9.5), A, hl * 0.72, 1.3, bh + 0.4, bh + 1.0, "HOUSE"), 0.26))
    main.append(K.obox("terrace_pale", at(hl * 0.62, -4.0), A, hl * 0.16, hw * 0.3, bh + 0.5, bh + 0.9, "stone", 0.1))
    main.append(K.obox("base_top", P["c"], A, hl * 0.93 - 0.6, hw * 0.93 - 0.6, bh - 0.2, bh + 0.6, "block_dk", 0.2))
    main.append(K.obox("base_rim", P["c"], A, hl * 0.93 + 0.4, hw * 0.93 + 0.4, bh - 1.6, bh + 0.1, "wall", 0.2))
    for i in range(12):                                   # flutes on the two front faces
        t = -hl * 0.9 + hl * 1.8 * i / 11
        p0, p1 = at(t, -hw - 0.2), at(t * 0.93, -hw * 0.93 - 0.2)
        main.append(K.beam(f"flute_sw{i}", (*p0, 0.8), (*p1, bh - 1.8), 1.7, 1.0, "stone_dk" if i % 2 else "block_dk"))
    for i in range(10):
        s = -hw * 0.9 + hw * 1.8 * i / 9
        p0, p1 = at(hl + 0.2, s), at(hl * 0.93 + 0.2, s * 0.93)
        main.append(K.beam(f"flute_se{i}", (*p0, 0.8), (*p1, bh - 1.8), 1.7, 1.0, "block_dk" if i % 2 else "stone_dk"))
        if i % 2 == 0:
            q0, q1 = at(hl + 0.5, s + 1.6), at(hl * 0.95 + 0.5, (s + 1.6) * 0.95)
            main.append(K.house(K.beam(f"slit_se{i}", (*q0, bh * 0.42), (*q1, bh * 0.8), 1.3, 0.6, "HOUSE"), 0.3))
    for i in range(5):
        t = -hl * 0.5 + hl * 1.3 * i / 4
        q0, q1 = at(t, -hw - 0.5), at(t * 0.95, -hw * 0.95 - 0.5)
        main.append(K.house(K.beam(f"slit_sw{i}", (*q0, bh * 0.3), (*q1, bh * 0.62), 1.3, 0.6, "HOUSE"), 0.36))
    for i, (t, s) in enumerate(((-hl, -hw), (hl, -hw), (hl, hw))):            # round bastions on the west, south and east corners
        p = at(t * 0.97, s * 0.97)
        main.append(K.cyl(f"bastion{i}", p, 6.0, 0.0, bh + 0.5, "stone_dk", 20, 5.4))
        main.append(K.cyl(f"bastion{i}_top", p, 5.8, bh + 0.3, bh + 1.4, "wall", 20))
        main.append(K.house(K.arc(f"bastion{i}_band", p, 5.3, 5.9, 200.0, 340.0, bh * 0.55, bh * 0.7, "HOUSE", 8), 0.32))
    # ---- the gallery along the south-west edge, its pylon at the south corner
    gt, gs = P["gal"]
    gl, gw = P["gal_hl"], P["gal_hw"]
    main.append(K.obox("gallery", at(gt, gs), A, gl, gw, bh - 0.5, bh + 3.0, "roof", 0.4))
    main.append(K.obox("gallery_bed", at(gt, gs), A, gl - 2.0, gw - 2.6, bh + 2.8, bh + 3.3, "block_dk"))
    for s in (-1, 1):
        main.append(K.obox(f"gallery_rail{s}", at(gt, gs + s * (gw - 1.3)), A, gl, 1.3, bh + 2.8, bh + 4.6, "white", 0.3))
    for i in range(9):
        t = gt - gl + 4.0 + (2 * gl - 8.0) * i / 8
        main.append(K.obox(f"gallery_bar{i}", at(t, gs), A, 1.3, gw - 2.6, bh + 3.2, bh + 4.2, "white", 0.2))
    main.append(K.obox("gallery_return", at(gt - gl + 2.0, gs + gw + 7.0), A + 90.0, 9.0, 3.2, bh + 0.2, bh + 4.4, "white", 0.4))
    main.append(K.obox("gallery_return_bed", at(gt - gl + 2.0, gs + gw + 7.0), A + 90.0, 7.0, 1.4, bh + 4.3, bh + 4.7, "block_dk"))
    py = at(gt + gl + 1.0, gs - 1.5)
    main.append(K.obox("pylon", py, A + 45.0, 4.2, 4.2, 0.0, bh + 17.0, "white", 0.4, 0.4))
    main.append(K.house(K.obox("pylon_band", py, A + 45.0, 3.3, 3.3, bh * 0.35, bh * 0.35 + 2.0, "HOUSE"), 0.35))
    main.append(K.obox("pylon_foot", py, A + 45.0, 4.6, 4.6, 0.0, 5.0, "wall", 0.4))
    # ---- the keep
    kt, ks = P["keep"]
    kl, kw = P["keep_hl"], P["keep_hw"]
    k0, k1 = bh, bh + P["keep_h"]
    kc = at(kt, ks)
    main.append(K.obox("keep", kc, A, kl, kw, k0, k1, "wall", 0.5, 0.97))
    main.append(K.obox("keep_cornice", kc, A, kl + 0.6, kw + 0.6, k1 - 2.0, k1 + 0.4, "roof2", 0.3))
    main.append(K.obox("keep_roof", kc, A, kl - 1.0, kw - 1.0, k1 + 0.2, k1 + 1.0, "block_dk", 0.2))
    main.append(K.obox("keep_plinth", kc, A, kl + 1.2, kw + 1.2, k0, k0 + 3.0, "block", 0.3))
    for i in range(7):                                    # pilasters and house window bands, south-west and south-east faces
        t = -kl + 3.0 + (2 * kl - 6.0) * i / 6
        main.append(K.obox(f"keep_pil{i}", at(t, -kw - 0.3, kc), A, 1.0, 0.6, k0 + 3.0, k1 - 2.0, "roof2"))
        if i < 6:
            for j, (z0, z1) in enumerate(((k0 + 6.0, k0 + 11.0), (k0 + 15.0, k0 + 21.0))):
                main.append(K.house(K.obox(f"keep_win{i}_{j}", at(t + (kl - 3.0) / 6, -kw - 0.15, kc), A, 1.5, 0.35, z0, z1, "HOUSE"), 0.3 if (i + j) % 2 else 0.4))
    for i in range(4):
        s = -kw + 3.0 + (2 * kw - 6.0) * i / 3
        main.append(K.obox(f"keep_pil_se{i}", at(kl + 0.3, s, kc), A + 90.0, 1.0, 0.6, k0 + 3.0, k1 - 2.0, "rib"))
        if i < 3:
            main.append(K.house(K.obox(f"keep_win_se{i}", at(kl + 0.15, s + (kw - 3.0) / 3, kc), A + 90.0, 1.5, 0.35, k0 + 12.0, k0 + 20.0, "HOUSE"), 0.3))
    for i, (t, s) in enumerate(((-kl, -kw), (kl, -kw), (kl, kw), (-kl, kw), (0.0, -kw - 1.0))):
        p = at(t, s, kc)
        zo = k1 - 4.0 if i < 4 else k0 + 12.0
        main.append(K.cyl(f"keep_orb{i}_arm", p, 2.6, zo - 4.0, zo - 1.6, "bronze_dk", 10))
        main.extend(K.orb(f"keep_orb{i}", (p[0], p[1], zo), 3.2))
    # the upper tier: a smaller block set back on the keep, bright diagonal brass beams on its south-west face,
    # two small roof pavilions with dark house roofs crossed by bronze diagonals, masts
    tl, tw = P["tier_hl"], P["tier_hw"]
    tc = at(-3.0, 1.5, kc)
    t1 = k1 + P["tier_h"]
    main.append(K.obox("tier", tc, A, tl, tw, k1, t1, "wall", 0.4))
    main.append(K.obox("tier_cornice", tc, A, tl + 0.5, tw + 0.5, t1 - 1.6, t1 + 0.3, "roof2", 0.25))
    main.append(K.obox("tier_roof", tc, A, tl - 0.8, tw - 0.8, t1 + 0.1, t1 + 0.8, "block_dk", 0.2))
    for i, t in enumerate((-7.0, 1.0, 9.0)):
        p0, p1 = at(t - 3.0, -tw - 0.4, tc), at(t + 3.0, -tw - 0.4, tc)
        main.append(K.beam(f"tier_diag{i}", (*p0, t1 - 2.0), (*p1, k1 + 1.0), 1.5, 0.6, "roof"))
        main.append(K.house(K.obox(f"tier_win{i}", at(t - 4.0, -tw - 0.15, tc), A, 1.0, 0.35, k1 + 2.0, t1 - 3.0, "HOUSE"), 0.36))
    for i, t in enumerate((-6.0, 6.0)):
        pc = at(t, 0.0, tc)
        ph = t1 + P["pav_h"] + (3.0 if i == 0 else 0.0)
        pl, pw = 5.2, tw - 2.0
        main.append(K.obox(f"pav{i}", pc, A, pl, pw, t1, ph, "wall", 0.4))
        main.append(K.obox(f"pav{i}_cornice", pc, A, pl + 0.5, pw + 0.5, ph - 1.2, ph + 0.3, "roof", 0.2))
        main.append(K.obox(f"pav{i}_deck", pc, A, pl - 0.4, pw - 0.4, ph + 0.1, ph + 0.5, "bronze_dk", 0.1))
        main.append(K.house(K.obox(f"pav{i}_roof", pc, A, pl - 1.3, pw - 1.3, ph + 0.3, ph + 0.9, "HOUSE", 0.15), 0.26))
        for s in (-1, 1):
            p0, p1 = at(-pl + 1.4, -s * (pw - 1.4), pc), at(pl - 1.4, s * (pw - 1.4), pc)
            main.append(K.beam(f"pav{i}_x{s}", (*p0, ph + 1.0), (*p1, ph + 1.0), 1.0, 0.5, "roof2" if s < 0 else "bronze"))
        for j, tt in enumerate((-2.6, 2.6)):
            main.append(K.house(K.obox(f"pav{i}_win{j}", at(tt, -pw - 0.15, pc), A, 1.0, 0.35, t1 + 1.6, ph - 2.0, "HOUSE"), 0.4))
    for i, (t, s, h) in enumerate(((-tl + 1.0, tw - 2.0, 20.0), (-tl + 6.0, tw - 0.5, 13.0), (-tl - 2.5, -1.0, 12.0))):
        p = at(t, s, tc)
        main.append(K.obox(f"mast{i}", p, A, 1.4, 1.4, k1, t1 + h, "bronze_dk", 0.2, 0.6))
        main.append(K.obox(f"mast{i}_cap", p, A, 1.6, 1.6, t1 + h - 1.0, t1 + h + 0.6, "soot"))
    # the lower terrace on the north-east / south-east part: a low brass parapet, rubble at the feet
    main.append(K.obox("parapet", at(hl * 0.93 - 2.0, 4.0), A + 90.0, hw * 0.75, 1.2, bh, bh + 2.2, "roof2", 0.3))
    main.extend(K.rubble("rub_sw", at(-hl * 0.75, -hw - 6.0), 7.0, 3, 6))
    main.extend(K.rubble("rub_se", at(hl + 4.0, -hw * 0.3), 8.0, 5, 7))
    main.extend(K.rubble("rub_s", at(hl * 0.55, -hw - 7.0), 6.0, 7, 5))
    if damaged:
        K.hole(main, [], at(4.0, -2.0, kc), 4.5, k1 + 0.3, 3, depth=3.0, panel=("wall", "roof2"))
        K.hole(main, [], at(gt + 8.0, gs), 5.0, bh + 3.3, 5, depth=3.0)
        K.wall_gash(main, at(4.0, -kw - 0.5, kc), A, 7.0, k0 + 6.0, k0 + 20.0, 7, 0.9)
        K.wall_gash(main, at(kl + 0.5, 0.0, kc), A + 90.0, 5.0, k0 + 8.0, k0 + 22.0, 8, 0.9)
        K.wall_gash(main, at(hl + 0.6, -8.0), A + 90.0, 7.0, 5.0, 18.0, 9, 1.0)
        K.scorch(main, at(hl * 0.6, 6.0), 7.0, bh + 0.7, 11, 5)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(flags=K.flags(-0.08, shadow_azimuth=30.0)), P=P)
