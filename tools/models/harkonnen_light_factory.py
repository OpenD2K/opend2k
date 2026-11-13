"""Harkonnen light factory - the original's design: the Atreides light factory's layout
(tools/models/atreides_light_factory.py: three long arched canopies fanning out from the east, rising from the west,
a dark hood behind, machinery at the north-west, the exit ramp and boulders in the base frame) in the Harkonnen dress.

    blender -b --factory-startup --python tools/models/harkonnen_light_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_LIGHT_FACTORY .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3599 base, 3600 main, 3601 damaged; animation 36 = 10 ADDITIVE welding flashes,
kept original; build-up 35 kept): the two houses' pictures overlap 0.82. Differences read from the Harkonnen frame:
  * the canopies are dark ribbed gun-metal with brass edges (no house trim, no spine); house-colour diamonds sit
    on their crowns; the southern canopy's west end is a pale striped ramp;
  * a tall arched hood stands across the middle canopy (a pale brass arch with a house plate on its crown);
  * north-west: three dark ball tanks instead of the two white flues; at the south a drum under a house cap.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_light_factory as A  # noqa: E402

KEY = "HARKONNEN_LIGHT_FACTORY"
P = A.P
P.update(va=((27.0, 30.0), (-45.0, 27.0)), vb=((28.0, 17.0), (-40.0, 6.0)), vc=((1.0, 8.0), (-27.0, -16.0)), vr=10.0, hood_c=(-4.0, 45.0), hood_r=15.0, hood_h=5.0, east=8.0)


def build(damaged=False):
    built = A.build(damaged)
    K.drop(built, "va_trim", "vb_trim", "vc_trim", "spine", "va_cap", "vb_cap", "vc_cap", "flue", "pump_band", "ramp_plate", "rock_ne", "rock_sw", "strut", "tower", "lamp_arm")
    base, main = built["nodes"]["base"], built["nodes"]["main"]
    K.remat(main, {"va_roof": "wall", "vb_roof": "wall", "vc_roof": "hk_plate", "va_roof_e": "hk_shell", "vb_roof_e": "hk_shell", "vc_roof_e": "hk_shell",
                   "va_rib": "hk_shell", "vb_rib": "hk_shell", "vc_rib": "hk_shell", "va_edge": "hk_brass", "vb_edge": "hk_brass", "vc_edge": "hk_gold", "vc_rib": "tan",
                   "va_east": "hk_black", "vb_east": "hk_black", "vc_east": "hk_black", "hood": "hk_shell", "hood_wall": "stone_dk", "tower": "rust",
                   "pump": "stone_dk"})
    K.dress(built, 0.4)
    # round 2: the Harkonnen canopies are densely ribbed - extra thin ribs between the Atreides generator's
    for tag, mt in (("va", "hk_shell"), ("vb", "hk_shell"), ("vc", "tan")):
        a_, b_ = P[tag]
        L_ = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        n_ = (-(b_[1] - a_[1]) / L_, (b_[0] - a_[0]) / L_)
        nrib = max(3, int(round(L_ / 10.0)))
        for i in range(nrib * 2):
            t = (i + 0.5) / (nrib * 2)
            if t < 0.3:
                continue
            d = 0.3 / L_
            rt = P["vr"] * (0.9 + P["leaf"] * math.sin(math.pi * t ** 0.8))
            zt = lambda u: P["z_w"] + (P["z_e"] - P["z_w"]) * math.sqrt(max(0.0, 1.0 - u ** 2.2))   # noqa: E731
            main.append(A.loft(f"{tag}_xrib{i}", [(A.lerp2(a_, b_, t - d), n_, rt, P["vh"], zt(t - d)), (A.lerp2(a_, b_, t + d), n_, rt, P["vh"], zt(t + d))], mt, dr=0.22))

    def zc(t):
        return P["z_w"] + (P["z_e"] - P["z_w"]) * math.sqrt(1.0 - t ** 2.2) + P["vh"]
    for i, (v, t) in enumerate((("va", 0.22), ("va", 0.62), ("vb", 0.3), ("vb", 0.72), ("vc", 0.3))):
        q = A.lerp2(*P[v], t)
        main.append(K.plate(f"diamond{i}", q, 45.0, 2.0, 2.0, zc(t) + 0.5, 0.22))
    # the arched hood across the middle canopy
    m = A.lerp2(*P["vb"], 0.42)
    a, b = (m[0] - 8.0, m[1] + 17.0), (m[0] + 3.0, m[1] - 9.0)
    main.append(K.vault("arch", a, b, 11.0, 2.0, "hk_shell", 14, 24.0))
    main.append(K.vault("arch_in", (a[0] + 0.3, a[1] - 0.8), (b[0] - 0.3, b[1] + 0.8), 9.0, 2.0, "hk_black", 14, 21.4))
    a3, b3 = (a[0], a[1], 2.0), (b[0], b[1], 2.0)
    main.append(K.house(K.hoop("arch_house", a3, b3, 0.38, 0.62, 11.3, "HOUSE", 14, (72.0, 108.0), 24.0 / 11.0), 0.22))
    for i, t in enumerate((0.07, 0.93)):
        main.append(K.hoop(f"arch_rim{i}", a3, b3, t - 0.07, t + 0.07, 11.4, "white", 14, (0.0, 180.0), 24.0 / 11.0))
    # ball tanks at the north-west, the drum with a house cap at the south, the pale ramp at the south-west
    for i, (p, r) in enumerate((((-35.0, 47.0), 4.6), ((-30.0, 38.0), 4.4), ((-40.0, 31.0), 4.2))):
        main.append(K.cyl(f"ball{i}_foot", p, 2.6, 0.0, 3.0, "stone_dk", 10))
        main.append(K.ball(f"ball{i}", (p[0], p[1], 3.0 + r * 0.8), r, "roof", 14))
    main.append(K.cyl("south_drum", (-4.0, -14.0), 8.0, 0.0, 6.0, "stone", 20))
    main.append(K.house(K.dome("south_cap", (-4.0, -14.0), 4.6, 5.8, 2.0, "HOUSE", 16), 0.22))
    main.append(K.lathe_loop("south_ring", (-4.0, -14.0), [(8.3, 4.6), (8.3, 6.2), (7.2, 6.2), (7.2, 4.6)], "hk_brass", 20))
    base.append(K.obox("ramp_plate", (-24.0, -16.0), 32.0, 9.0, 5.0, 0.0, 1.6, "white", 0.2))
    for i in range(5):
        base.append(K.obox(f"ramp_stripe{i}", K.polar((-24.0, -16.0), -7.0 + 3.5 * i, 32.0), 122.0, 4.6, 0.5, 1.5, 1.9, "hk_gold" if i % 2 else "stone"))
    return built


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="keep", flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0),
                                                  sky_strength=0.09, sun_strength=4.1)), P=P)
