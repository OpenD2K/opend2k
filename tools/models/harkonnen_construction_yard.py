"""Harkonnen construction yard - the original's design: the Atreides yard's layout
(tools/models/atreides_construction_yard.py: inclined mixer drum on a machine block, the round walled yard with its
berm, crane house and boom, the east wing with two house panels and the conveyor, dish, gate) in the Harkonnen dress
(tools/models/harkonnen_kit.py). The Emperor's yard (IMPERIAL_CONSTRUCTION_YARD, art 58) is the same picture with
its own frames: tools/models/harkonnen_imperial_construction_yard.py runs this build under that key.

    blender -b --factory-startup --python tools/models/harkonnen_construction_yard.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_CONSTRUCTION_YARD .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3377 main, 3378 damaged, no base; animation 1 = 14 crane overlay frames; build-up
0 kept): the two houses' pictures overlap 0.92. Differences read from the Harkonnen frame:
  * the drum is blackened gun-metal with pale glints on its ribs, a red collar; behind it (north-east) stand TWO
    tall brown chimneys with dark collars;
  * the berm of the yard stays pale concrete, the crane boom is grey steel;
  * the east wing is a black, rounded armour roof with two bright red house panels; the dish is dark.
Animation: the Atreides crane poses (boom swings out to the west, hook down, load up, back over the yard, assembly
glow) - checked against the Harkonnen overlay strip.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_construction_yard as A  # noqa: E402

KEY = "HARKONNEN_CONSTRUCTION_YARD"
P = A.P
P.update(stack0=(4.5, 50.5, 44.0, 2.6), stack1=(9.8, 49.5, 38.0, 2.3), dish=(38.0, 44.0, 7.0), wing_c=(20.3, 20.1), wing_hl=25.5, wing_hw=13.0, block_c=(-4.0, 36.5))


def build(damaged=False):
    built = A.build(damaged)
    main = built["nodes"]["main"]
    K.remat(main, {
        "drum": "hk_shell", "drum_rib": "stone", "drum_cap": "hk_black", "drum_mouth": "recess",
        "block": "stone_dk", "block_top": "hk_black", "cradle": "rust",
        "pit_berm": "concrete", "pit_crest": "hk_plate", "pit_rib": "stone", "pit_floor": "skirt",
        "machine": "hk_plate", "machine_top": "stone", "cabin": "white", "crane_house": "fin", "crane_house2": "hk_plate", "west_box": "fin", "cabin_roof": "hk_black",
        "wing_block": "hk_black", "wing_ledge": "concrete", "wing_roof": "hk_black", "wing_rim": "stone", "wing_frame": "hk_plate",
        "conveyor": "hk_shell", "conveyor_belt": "hk_black", "conveyor_head": "stone_dk", "conveyor_turn": "white", "conveyor_rib": "hk_plate",
        "dish": "hk_shell", "gate_lintel": "hk_brass", "front_block": "stone_dk", "front_top": "hk_black",
    })
    K.remat(built["nodes"]["crane"], {"boom": "steel", "boom_rail": "hk_brass", "boom_back": "hk_black", "boom_head": "copper"})
    K.move(main, ("west_box", "crane_house2", "crane_dome"), (3.0, 0.0, 0.0))
    K.dress(built, 0.3)
    K.remat(main, {"wing_panel0": ("HOUSE", 0.2), "wing_panel1": ("HOUSE", 0.34), "drum_collar": ("HOUSE", 0.05)})
    a_, b_, r_ = P["drum_a"], P["drum_b"], P["drum_r"]          # round 2: the drum's sunlit upper flank is pale between the ribs
    for i, (t0, t1) in enumerate(((0.11, 0.235), (0.26, 0.395), (0.42, 0.56), (0.585, 0.72))):
        main.append(K.hoop(f"drum_pale{i}", a_, b_, t0, t1, r_ + 0.12, "hk_plate", 24, (62.0, 128.0)))
    # the assembly glow is a fixed blue arc in the Harkonnen (and the Emperor's) overlay frames, not house colour
    K.remat(built["nodes"]["glow"], {"glow_arc": "glow", "glow_arc2": "glow_dk"})
    tops = []
    for i, key in enumerate(("stack0", "stack1")):
        x, y, h, r = P[key]
        st, top = K.stack(f"stack{i}", (x, y), r, 0.0, h, "copper", 2)
        main.extend(st)
        tops.append(top)
    main.append(K.box("stack_base", 0.0, 14.0, 45.0, 55.0, 0.0, 9.0, "stone_dk", 0.5))
    main.extend(K.rocks("rub_sw", (-40.0, -15.0), 5.0, 61, 6, 0.7))
    main.extend(K.rocks("rub_s", (1.0, -17.0), 3.0, 62, 3, 0.7))
    if damaged:
        K.wall_gash(main, (6.0, 44.8), 0.0, 4.0, 2.0, 7.5, 31)
    built["anchors"] = list(built.get("anchors", [])) + [(f"fx_flue_{i}", t, 0.6) for i, t in enumerate(tops)]
    return built


SPEC = dict(anim_frames=A.ANIM, flags=dict(shadow_azimuth=55.0, fill=(0.3, 90.0, 35.0), sky_strength=0.09))

if __name__ == "__main__":
    K.run(KEY, build, poses=A.POSES, rest=A.rest, spec=SPEC, P=P)
