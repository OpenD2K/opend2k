"""The Emperor's palace (IMPERIAL_PALACE, art 57) - built from scratch with tools/models/harkonnen_kit.py.

    blender -b --factory-startup --python tools/models/harkonnen_imperial_palace.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=IMPERIAL_PALACE .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3889 main, 3890 damaged, no base, no animation; build-up 56 kept): a sandstone
ziggurat turned about 11 degrees -
  * three battered tiers; the west face in full sun (pale yellow), the south face mid-brown with a row of framed
    dark window panels on every tier, a house-colour door at the foot of the south face;
  * on top a broad overhanging lid of banded golden-brown beams (wider than the top tier), a house-colour banner
    row on its crown and a row of house-colour squares under its south eave;
  * sandstone boulders at the south-west and south-east corners.
Free choices: the panel frames, the lid's banding.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402

KEY = "IMPERIAL_PALACE"
P = dict(ang=11.0, c=(-1.5, -18.5), half=38.0, depth=39.0, h=37.0, top=0.34, lid_l=38.0, lid_w=19.0, lid_h=8.0, lid_c=(-5.5, -13.5))


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): the framed windows of the south face
# (the golden ones bright, the copper ones dim), the dark doorway.
GLOW = dict(gold=K.AK.lit("hk_gold_lit", "hk_gold", (0.95, 0.66, 0.24), 0.5), dim=K.AK.lit("copper_dk_lit", "copper_dk", (0.48, 0.26, 0.09), 0.5),
            door=K.AK.lit("recess_lit", "recess", (0.90, 0.58, 0.25), 0.08))


def rot(c, t, s):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def build(damaged=False):
    main = []
    A, c, hl, hw, h = P["ang"], P["c"], P["half"], P["depth"], P["h"]
    n = 3
    main.append(K.obox("foot", c, A, hl + 1.5, hw + 1.5, 0.0, 1.6, "hk_sand_dk", 0.3))
    for i in range(n):
        k0 = 1.0 - (1.0 - P["top"]) * i / n
        k1 = 1.0 - (1.0 - P["top"]) * (i + 1) / n
        z0, z1 = h * i / n, h * (i + 1) / n
        main.append(K.obox(f"tier{i}", c, A, hl * k0, hw * k0, z0, z1, "hk_sand", 0.4, (k1 + 0.035) / k0))
        main.append(K.obox(f"ledge{i}", c, A, hl * k1 + 1.4, hw * k1 + 1.4, z1 - 1.2, z1 + 0.3, "hk_sand_dk", 0.2))
        # framed window panels along the south face (and plainer ribs on the west face)
        cnt = 9 - 2 * i
        for j in range(cnt):
            f = (j + 0.5) / cnt * 2 - 1
            for (zz, kk, nm, mt, w, d) in ((z0 + 1.2, k0, "pil", "hk_sand_dk", 0.0, 0.0),):
                pass
            t0, t1 = f * hl * k0 * 0.94, f * hl * (k1 + 0.035) * 0.94
            p0, p1 = rot(c, t0, -hw * k0 - 0.2), rot(c, t1, -hw * (k1 + 0.035) - 0.2)
            wdt = hl * k0 * 1.5 / cnt
            main.append(K.beam(f"panel{i}_{j}", (*p0, z0 + 1.6), (*p1, z1 - 1.8), wdt * 0.5, 0.5, "hk_sand_dk", 0.1))
            q0 = (p0[0] + (p1[0] - p0[0]) * 0.3, p0[1] + (p1[1] - p0[1]) * 0.3, z0 + 1.6 + (z1 - z0 - 3.4) * 0.3)
            q1 = (p0[0] + (p1[0] - p0[0]) * 0.75, p0[1] + (p1[1] - p0[1]) * 0.75, z0 + 1.6 + (z1 - z0 - 3.4) * 0.75)
            main.append(K.beam(f"window{i}_{j}", q0, q1, wdt * 0.2, 0.9, GLOW["gold"] if (i + j) % 3 == 0 else GLOW["dim"], 0.1))
        for j in range(cnt - 1):
            f = (j + 1.0) / cnt * 2 - 1
            p0, p1 = rot(c, -hl * k0 - 0.2, f * hw * k0 * 0.9), rot(c, -hl * (k1 + 0.035) - 0.2, f * hw * (k1 + 0.035) * 0.9)
            main.append(K.beam(f"wrib{i}_{j}", (*p0, z0 + 1.0), (*p1, z1 - 1.4), 0.8, 0.5, "hk_sand_dk"))
    # the house door at the foot of the south face
    dq = rot(c, 5.0, -hw - 0.6)
    main.append(K.house(K.obox("door", dq, A, 4.2, 0.9, 0.5, 9.0, "HOUSE", 0.1), 0.45))
    main.append(K.obox("door_dark", rot(c, 5.0, -hw - 1.0), A, 2.6, 0.9, 0.5, 5.0, GLOW["door"]))
    # ---- the lid
    lc, ll, lw, lh = P["lid_c"], P["lid_l"], P["lid_w"], P["lid_h"]
    main.append(K.obox("lid_neck", rot(c, 0.0, 0.0), A, hl * P["top"] + 3.0, hw * P["top"] + 3.0, h, h + 2.0, "copper_dk", 0.2))
    main.append(K.obox("lid", lc, A, ll, lw, h + 1.6, h + 1.6 + lh * 0.55, "rim", 0.5, 0.97))
    main.append(K.obox("lid_up", (lc[0] + 1.0, lc[1] + 3.0), A, ll * 0.8, lw * 0.72, h + 1.2 + lh * 0.55, h + 1.6 + lh, "fin", 0.5, 0.95))
    for j in range(7):
        s = -lw + 2.0 + j * (2 * lw - 4.0) / 6
        p0, p1 = rot(lc, -ll + 0.5, s), rot(lc, ll - 0.5, s)
        main.append(K.beam(f"lid_band{j}", (*p0, h + 1.7 + lh * 0.55), (*p1, h + 1.7 + lh * 0.55), 1.6, 0.6, "hk_gold" if j % 2 else "copper", 0.1))
    for j in range(3):
        q = rot(lc, -3.0 + 0.0 * j, -6.0 + 7.5 * j)
        main.append(K.plate(f"banner{j}", q, A, 4.6, 1.6, h + 2.4 + lh, 0.5))
    main.append(K.obox("lid_block", rot(lc, -2.0, 4.0), A, 7.0, 10.0, h + 1.6 + lh * 0.5, h + 2.0 + lh, "copper", 0.4, 0.9))
    for j in range(9):
        q = rot(lc, -30.0 + 6.4 * j, -lw * 0.74)
        main.append(K.house(K.obox(f"eave_sq{j}", q, A, 1.3, 0.8, h - 3.2, h + 0.2, "HOUSE"), 0.4))
    for j in range(3):
        main.append(K.plate(f"west_mark{j}", rot(lc, -ll + 2.0, -8.0 + 7.0 * j), A, 1.6, 1.2, h + 2.0 + lh * 0.55, 0.5))
        main.append(K.plate(f"east_mark{j}", rot(lc, ll - 3.0, 2.0 + 6.0 * j), A, 1.6, 1.2, h + 2.0 + lh * 0.55, 0.5))
    main.extend(K.rocks("rock_sw", rot(c, -hl - 2.0, -hw + 1.0), 6.0, 3, 6, 0.8))
    main.extend(K.rocks("rock_se", rot(c, hl + 1.0, -hw - 1.0), 5.5, 5, 6, 0.8))
    if damaged:
        K.hole(main, [], rot(lc, 12.0, -2.0), 6.0, h + 1.8 + lh * 0.55, 3, depth=4.0, panel=("rim", "fin"), rib="copper")
        K.hole(main, [], rot(lc, -20.0, 3.0), 4.5, h + 1.8 + lh * 0.55, 4, depth=4.0, panel=("rim", "fin"), rib="copper")
        K.wall_gash(main, rot(c, -10.0, -hw * 0.82), A, 8.0, 8.0, 17.0, 5, 1.5)
        K.wall_gash(main, rot(c, 14.0, -hw * 0.6), A, 6.0, 20.0, 28.0, 6, 1.5)
    return dict(nodes=dict(main=main))


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(flags=dict(shadow_azimuth=8.0, sun_elevation=42.0, sun_strength=5.4, fill=(0.3, 90.0, 35.0), sky_strength=0.1)), P=P, short="imperial_palace")
