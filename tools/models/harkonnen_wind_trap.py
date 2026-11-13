"""Harkonnen wind trap - the original's design: the Atreides wind trap's layout (tools/models/atreides_wind_trap.py:
two pointed scoops one behind the other, mouths to the south-east with cyan louvres, copper machinery to the east)
in the Harkonnen dress (tools/models/harkonnen_kit.py).

    blender -b --factory-startup --python tools/models/harkonnen_wind_trap.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_WIND_TRAP .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3338 main, 3339 damaged, no base; animation 11 = 10 overlay frames: the louvres'
glow running; build-up 10 kept): the two houses' pictures overlap 0.90. Differences read from the Harkonnen frame:
  * the shells are dark brown-grey gun-metal with a pale sheen along the crest, not cream; the mouths' lips are
    grey steel ramps;
  * house colour: three armour patches along the west flank of each scoop (not one crest band) and a patch at
    the apex behind the rim; the rim itself is brass with a pale highlight.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_wind_trap as A  # noqa: E402

KEY = "HARKONNEN_WIND_TRAP"
P = A.P
P.update(deep=34.5)


def flank_patches(main, tag, m):
    """Three house-colour armour patches along the west flank of a scoop (bands over the shell, short in arc)."""
    fa = P["face_ang"]
    bx, by = A.unit(fa + 90.0)
    R, sq, D = P["half"], P["tall"], P["deep"]

    def rad(t):
        return R * max(0.0, 1.0 - t * t) ** 0.31
    a = (m[0], m[1], 0.0)
    b = (m[0] + bx * D, m[1] + by * D, 0.0)
    for i, (t0, t1) in enumerate(((0.20, 0.34), (0.44, 0.58), (0.68, 0.80))):
        ts = [t0, (t0 + t1) / 2, t1]
        main.append(K.house(K.sweep(f"{tag}_patch{i}", a, b, [(t, rad(t) + 0.35) for t in ts], "HOUSE", 32, 0.0, sq, (134.0, 166.0), A.PT), 0.36))
    main.append(K.house(K.sweep(f"{tag}_apex", a, b, [(t, rad(t) + 0.35) for t in (0.05, 0.10, 0.16)], "HOUSE", 32, 0.0, sq, (62.0, 100.0), A.PT), 0.42))


def build(damaged=False):
    built = A.build(damaged)
    K.drop(built, "s_crest", "n_crest", "s_stripe", "n_stripe", "s_dark", "n_dark")
    main = built["nodes"]["main"]
    K.remat(main, {"s_shell": "roof", "n_shell": "roof", "s_rib": "hk_shell", "n_rib": "hk_shell", "s_rim": "hk_brass", "n_rim": "hk_brass",
                   "s_lip": "steel", "n_lip": "steel", "s_lip_in": "wall", "n_lip_in": "wall", "s_foot": "hk_black", "n_foot": "hk_black"})
    flank_patches(main, "s", P["mouth_s"])
    flank_patches(main, "n", P["mouth_n"])
    # the mouths: a brown louvre wall (not a black cave), grey lips
    K.remat(main, {"s_slat": "fin", "n_slat": "fin"})
    K.move(main, ("pump", "loop", "lamp0"), (1.0, 3.0, 0.0))
    R, H = P["half"], P["half"] * P["tall"]
    for tag, m in (("s", P["mouth_s"]), ("n", P["mouth_n"])):       # the louvre wall: a brown panel leaning back into the scoop
        sx, sy = A.unit(P["face_ang"])
        bx, by = A.unit(P["face_ang"] + 90.0)
        lo = [(m[0] - sx * (R - 2.6) - bx * 0.4, m[1] - sy * (R - 2.6) - by * 0.4), (m[0] + sx * (R - 2.6) - bx * 0.4, m[1] + sy * (R - 2.6) - by * 0.4)]
        hi = [(m[0] + sx * R * 0.2 + bx * 9.0, m[1] + sy * R * 0.2 + by * 9.0), (m[0] - sx * R * 0.2 + bx * 9.0, m[1] - sy * R * 0.2 + by * 9.0)]
        main.append(K.ramp(f"{tag}_panel", lo, hi, 0.0, 1.0, H * 0.84, "skirt", 0.0))
    return K.dress(built, 0.22)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(anim="glow", glow=[A.glow(P["mouth_s"]), dict(A.glow(P["mouth_n"]), phase=0.35)],
                                flags=dict(shadow_azimuth=30.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
