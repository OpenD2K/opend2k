"""Harkonnen high tech factory - the original's design: the Atreides high tech factory's layout
(tools/models/atreides_high_tech_factory.py: the tall hangar block in the west, the lattice launch tower with its
round deck and tall mast in the east, a big hoop round the legs, the pump station) in the Harkonnen dress.

    blender -b --factory-startup --python tools/models/harkonnen_high_tech_factory.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_HIGH_TECH_FACTORY .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3383 main, 3384 damaged, no base; animation 33 = 10 ADDITIVE overlay frames, kept
original; build-up 32 kept): the two houses' pictures overlap 0.79. Differences read from the Harkonnen frame:
  * the hangar is dark plating under a VAULTED armour roof (a pale sheen along its crown) with house-colour
    stripes across it, two pale horns at its north-west end; no roundel, no doors;
  * the tower's deck is dark with a low dark dome, a house-colour collar round the mast's foot; the mast is a
    heavy brown chimney; house bars low on the south-east legs; the pump station's tanks are dark balls.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_high_tech_factory as A  # noqa: E402

KEY = "HARKONNEN_HIGH_TECH_FACTORY"
P = A.P
P.update(mast=41.0, vault_h=5.5, hall_h=36.0, hall=(-6.4, 26.1), hall_hl=23.3, hall_hw=23.5, hoop=(25.0, 1.5, 11.5), hoop_r=20.5, hoop_tilt=9.0, foot=(26.0, 3.0), foot_r=17.0,
         deck=(15.0, 21.0, 41.0), neck_r=8.0)


def build(damaged=False):
    built = A.build(damaged)
    K.drop(built, "roundel", "door", "lamp", "pump", "aerial", "hoop")
    main = built["nodes"]["main"]
    K.remat(main, {"hall": "wall", "hall_up": "stone", "hall_rib": "hk_plate", "hall_cornice": "hk_brass", "hall_roof": "hk_black", "hall_pil": "hk_plate",
                   "deck": "stone_dk", "deck_rim": "hk_brass", "deck_plate": "hk_black", "deck_dome": "roof", "deck_skirt": "hk_black",
                   "hoop": "hk_black", "mast_collar": "copper_dk", "pump_ball": "roof", "pump_strut": "stone", "pump_pipe": "copper_dk", "leg": "stone_dk", "ring": "hk_black", "xa": "rust", "xb": "stone_dk", "core": "hk_black", "mast": "copper"})
    K.dress(built, 0.4)
    hc, hl, hw, hh = P["hall"], P["hall_hl"], P["hall_hw"], P["hall_h"]
    a2, b2 = A.rot(hc, 0.0, -hw * 0.86), A.rot(hc, 0.0, hw * 0.7)
    a, b = (a2[0], a2[1], hh + 0.6), (b2[0], b2[1], hh + 0.6)
    r, h = hl * 0.84, P["vault_h"]
    roof = K.vault("hall_vault", a2, b2, r, hh + 0.6, "hk_plate", 16, h, 0.0, 6.0)
    main.append(roof)
    for i, (t0, t1) in enumerate(((0.16, 0.26), (0.40, 0.50), (0.66, 0.76))):
        main.append(K.house(K.hoop(f"vault_band{i}", a, b, t0, t1, r + 0.3, "HOUSE", 16, (38.0, 150.0), h / r), 0.2))
    main.append(K.hoop("vault_dark", a, b, 0.80, 0.985, r + 0.2, "hk_shell", 16, (0.0, 180.0), h / r))      # the dark north-east end
    main.append(K.hoop("vault_dark2", a, b, 0.02, 0.985, r + 0.15, "hk_shell", 16, (118.0, 180.0), h / r))     # the shaded north-west flank
    for i, t in enumerate((0.33, 0.58, 0.84)):
        main.append(K.hoop(f"vault_rib{i}", a, b, t - 0.012, t + 0.012, r + 0.25, "stone_dk", 16, (0.0, 180.0), h / r))
    for i, t in enumerate((-21.0, -11.0)):                                        # the horns
        p = A.rot(hc, t, hw * 0.78)
        main.append(K.beam(f"horn{i}", (p[0], p[1], hh + 2.0), (p[0] - 2.0, p[1] + 1.5, hh + 9.0), 2.4, 1.6, "hk_plate", 0.3))
    dx, dy, dz = P["deck"]
    K.move(main, ("mast",), (1.0, 0.0, 0.0))
    mx, my = dx, dy + 4.0
    main.append(K.house(K.lathe_loop("mast_house", (mx, my), [(6.2, dz + 2.2), (6.2, dz + 4.6), (3.2, dz + 4.6), (3.2, dz + 2.2)], "HOUSE", 20), 0.3))
    main.extend(K.stack("mast2", (mx + 5.5, my - 1.0), 1.9, dz, dz + P["mast"] * 0.62, "copper_dk", 1)[0])
    # the solid machinery trunk inside the legs (the original's tower is a dark mass with a few heavy members,
    # not an open lattice)
    fc = P["foot"]
    main.append(K.tube("trunk", (fc[0] - 2.0, fc[1] + 6.0, 0.0), (dx, dy, dz - 3.0), 6.0, "hk_black", 14, 6.0))
    main.append(K.beam("big_leg", (dx + 1.0, dy - 6.0, dz - 4.0), (20.0, -27.0, 0.0), 3.4, 3.4, "rust", 0.4))
    main.append(K.cyl("big_leg_foot", (20.0, -27.0), 3.2, 0.0, 2.0, "hk_black", 10))
    # the hoop: a heavy dark ring round the legs, tilted (low in front, high behind) and open at the front-left
    import math
    hx_, hy_, hz_ = P["hoop"]
    hr_ = P["hoop_r"]
    def hp(a_):
        return (hx_ + hr_ * math.cos(math.radians(a_)), hy_ + hr_ * math.sin(math.radians(a_)), hz_ + P["hoop_tilt"] * math.sin(math.radians(a_)))
    n_ = 26
    for k in range(n_):
        a0_, a1_ = -105.0 + 225.0 * k / n_, -105.0 + 225.0 * (k + 1) / n_
        main.append(K.beam(f"ring_seg{k}", hp(a0_), hp(a1_ + 2.0), 3.2, 2.8, "hk_black", 0.3))
    for k, a_ in enumerate((-95.0, -50.0, 0.0, 60.0)):
        q_ = hp(a_)
        main.append(K.tube(f"ring_post{k}", (q_[0], q_[1], 0.0), q_, 1.0, "copper_dk", 6))
        main.append(K.beam(f"ring_tie{k}", q_, (dx + (q_[0] - dx) * 0.25, dy + (q_[1] - dy) * 0.25, dz - 6.0), 1.6, 1.6, "rust" if k % 2 else "stone_dk"))
    # the pump station (south-west): a pale ball tank piped to a dark block, an A-frame gantry with a golden lamp
    main.append(K.cyl("pball_foot", (-40.0, -1.0), 2.2, 0.0, 2.0, "stone_dk", 10))
    main.append(K.ball("pball", (-40.0, -1.0, 3.8), 4.0, "roof", 12))
    main.extend(K.pipe("ppipe", [(-38.0, -2.0, 3.5), (-32.0, -7.0, 3.5), (-26.0, -11.0, 4.0)], 1.5, "stone"))
    main.append(K.obox("pblock", (-24.0, -12.0), -30.0, 5.5, 4.0, 0.0, 7.5, "hk_black", 0.5))
    main.append(K.obox("pblock2", (-5.0, -15.0), -30.0, 4.0, 2.5, 0.0, 7.0, "stone_dk", 0.5))
    for i_, (p_, q_) in enumerate((((-14.0, -5.0, 9.0), (-16.0, -13.0, 0.3)), ((-14.0, -5.0, 9.0), (-8.0, -19.0, 0.3)), ((-3.0, -11.0, 9.0), (-4.0, -20.0, 0.3)),
                                   ((-14.0, -5.0, 9.0), (-3.0, -11.0, 9.0)))):
        main.append(K.beam(f"pframe{i_}", p_, q_, 1.5, 1.5, "stone_dk" if i_ != 2 else "copper", 0.2))
    main.append(K.beam("plamp", (-1.0, -10.0, 9.0), (-2.0, -15.0, 3.0), 1.6, 1.2, "hk_gold", 0.2))
    hx, hy, hz = P["hoop"]
    for i in range(3):
        main.append(K.house(K.box(f"leg_bar{i}", hx + 3.0 + 3.2 * i, hx + 4.4 + 3.2 * i, hy - 9.0, hy - 8.2, 5.0, 13.0, "HOUSE"), 0.22))
    if damaged:
        import math
        K.hole(main, [roof], A.rot(hc, 2.0, 4.0), 5.0, lambda x, y: hh + h * 0.8, 33, depth=5.0, panel=("roof", "roof2"))
    return built


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="keep", flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09)), P=P)
