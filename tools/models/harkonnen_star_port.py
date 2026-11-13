"""Harkonnen star port - the original's design: the Atreides star port's layout (tools/models/atreides_star_port.py:
the round landing pit inside a crescent wall, domes on the wall, the gate house with two copper domes at the south,
fence arcs) in the Harkonnen dress.

    blender -b --factory-startup --python tools/models/harkonnen_star_port.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_STAR_PORT .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3596 = the one picture, a base frame drawn under units; 3598 damaged; animation 47 =
8 overlay frames of landing lights; build-up 46 kept): the two houses' pictures overlap 0.96. Differences read from
the Harkonnen frame: everything is dark gun-metal; the wall's lip is brass; the domes are dark steel with house
patches on their crowns (the big central one carries a ring of house plates), no house crescent or collars; the
gate house is dark with its two orange-copper domes under house caps; the fence arcs stay pale concrete.
Animation: guide lights in the pit, as the Atreides pack (drawn by the pack driver).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_star_port as A  # noqa: E402

KEY = "HARKONNEN_STAR_PORT"
P = A.P
P.update(ecc=3.5, fence0=(2.0, -0.5), fence1=(2.0, 2.0), domew=(-0.5, -0.5), domen=(1.5, -3.0), domene=(1.5, -3.0), domec=(7.5, -1.5), neb=(-2.0, 0.0), lug=(41.0, 16.0), gate_w=1.12)


# Night glow (docs/3d-models.md "Emission"; emissive twins, no new geometry): the brass dome tips and the gate
# domes' tips are beacons, the brass teeth under the lip glow into the pit, the gate house's louvre is lit.
GLOW = dict(brass=K.AK.lit("hk_brass_lit", "hk_brass", (0.95, 0.62, 0.22), 0.3), tooth=K.AK.lit("hk_brass_dim", "hk_brass", (0.50, 0.30, 0.10), 0.3),
            tip=K.AK.lit("orange_lit", "orange", (1.00, 0.60, 0.22)), slat=K.AK.lit("stone_lit", "stone", (0.90, 0.50, 0.20), 0.1),
            dark=K.AK.lit("stone_dim", "stone", (0.60, 0.30, 0.10), 0.1))     # (the re-dress made the whole louvre "stone")


def build(damaged=False):
    built = A.build(damaged)
    K.drop(built, "dome_w_crescent", "dome_n_collar", "dome_ne_collar", "west_band")
    main = built["nodes"]["main"]
    K.remat(main, {"wall": "stone_dk", "wall_lip": "hk_brass", "wall_rib": "stone", "dome_w_drum": "stone_dk", "dome_w": "roof", "dome_n": "roof",
                   "dome_ne": "roof", "dome_n_tip": "hk_brass", "dome_ne_tip": "hk_brass", "dome_c_drum": "stone_dk", "dome_c": "roof",
                   "east_block": "stone", "ne_block": "hk_black", "gate_round": "stone", "gate": "stone_dk", "gate_step": "stone_dk", "gate_dome": "orange", "gate_vent": "stone",
                   "fence": "concrete", "girder": "stone_dk", "pit_arc_w": "stone"})
    K.remat(main, {"fence0_1": "white", "fence1_1": "white"})
    # round 2: fitted to the Harkonnen outline (fences, west dome, gate house, north-east block)
    K.move(main, ("fence0",), (*P["fence0"], 0.0))
    K.move(main, ("fence1",), (*P["fence1"], 0.0))
    K.move(main, ("dome_w",), (*P["domew"], 0.0), 1.08, (-34.0, 28.0, 0.0))
    K.move(main, ("dome_n$", "dome_n_tip"), (*P["domen"], 0.0))
    K.move(main, ("dome_ne",), (*P["domene"], 0.0))
    K.move(main, ("dome_c",), (*P["domec"], 0.0))
    K.move(main, ("ne_block",), (*P["neb"], 0.0))
    K.move(main, ("gate",), scale=(P["gate_w"], 1.0, 1.0), about=(P["gate"][0], P["gate"][1], 0.0))
    main.append(K.obox("east_lug", P["lug"], 100.0, 6.0, 3.5, 0.0, 6.0, "stone_dk", 0.6, 0.9))
    K.dress(built, 0.45)
    wh = P["wall_h"]
    main.append(K.house(K.dome("dome_w_cap", (-34.0 + P["domew"][0], 28.0 + P["domew"][1]), 4.6, (wh + 0.8 + 9.0 * 0.9) * 1.08 + 0.2, 1.7, "HOUSE", 14), 0.3))
    for k in range(7):
        q = K.polar((4.0 + P["domec"][0], 45.0 + P["domec"][1]), 5.6, 20.0 + 360.0 * k / 7)
        main.append(K.house(K.dome(f"dome_c_plate{k}", q, 2.0, wh + 1.0 + 6.6 * 0.7, 1.5, "HOUSE", 8), 0.3))
    main.append(K.house(K.dome("dome_ne_cap", (28.0 + P["domene"][0], 43.0 + P["domene"][1]), 2.2, wh + 2.7 + 9.2 * 0.93, 1.0, "HOUSE", 10), 0.3))
    main.append(K.arc("lip_brass", P["c"], P["r"] - P["wall"] - 1.2, P["r"] - P["wall"] + 2.6, 5.0, 175.0, wh - 0.4, wh + 1.3, "hk_brass", 40))
    for i in range(7):                                    # brass teeth hanging from the lip into the pit, north-east
        a = 20.0 + 11.0 * i
        ri = P["r"] - P["wall"]
        main.append(K.beam(f"tooth{i}", K.polar(P["c"], ri - 0.3, a, wh - 0.2), K.polar(P["c"], ri - 0.6, a, wh - 5.0 - (i % 2) * 2.0), 1.4, 0.8, "hk_brass"))
    K.AK.relight(main, {"dome_n_tip": GLOW["brass"], "dome_ne_tip": GLOW["brass"], "gate_dome0_tip": GLOW["tip"], "gate_dome1_tip": GLOW["tip"],
                        "tooth": GLOW["tooth"], "gate_vent_s": GLOW["slat"], "gate_vent_d": GLOW["dark"]})
    return built


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(anim="dots", dots=[(x, y, P["pit"] + 0.6) for (x, y) in A.LIGHTS], dot_frames=A.dot_frames(), dot_radius=0.7,
                                flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09, sun_strength=4.0)), P=P)
