"""Harkonnen barracks - built from scratch with tools/models/harkonnen_kit.py (the one Harkonnen building whose
original does not follow the Atreides layout: outline overlap 0.5).

    blender -b --factory-startup --python tools/models/harkonnen_barracks.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_BARRACKS .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3341 main, 3342 damaged, no base, no animation; build-up 14 kept): a brutal,
armoured bunker shaped like a crouching beast -
  * at the back a dark helmet dome with a house-colour crest plate on its crown, a pale brass brow band across its
    west front and a thin chimney behind it;
  * under the helmet a pale steel "visor" plate and jaw block (the brightest spot of the picture);
  * the west flank: a big lying armour barrel (the troop hall) with house-colour trim lines along its ends and
    seams, a copper dome and orange rubble at its south-west foot;
  * the east flank: a blackened barrel hall, a low east wing with a house-colour lamp panel, a house-colour door
    and brown struts at the south front.
Free choices: seams, vents, lamps, the exact section of the halls (the sprite is a 60 px lump).

The door (tools/models/barracks_door.py, redesign 2026-11-13): a trained soldier appears inside the footprint at px
(7, 2) from its centre and walks straight south. Graphics: 3D shows this model 6.8 px south of where it is authored
(its picture reaches north of the cells), so there he appears at model px (7, 8.8) - in the south end of the
blackened east hall. That hall is shorter now and ends in an armoured GATE TOWER on the spot (an open, lit doorway
20 px high - a soldier stands 22 px tall in 3D -, door plane y = 5, the hall behind it hollow); the plinth is cut
open for a paved path between two low walls, where the picture's gate block with the house-colour door stood; the
two brown struts stand left and right of it.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import barracks_door as BD  # noqa: E402

KEY = "HARKONNEN_BARRACKS"
P = dict(
    head=(-7.0, 38.0), head_r=14.0, head_z=17.0, head_h=13.0,
    west_a=(-19.5, 24.0, 8.5), west_b=(-18.5, -5.0, 8.5), west_r=10.8,
    east_a=(5.0, 28.0, 9.0), east_b=(10.0, 17.0, 9.0), east_r=10.0,          # (ended at (15, 6) before the gate tower)
    door=(7.0, 5.0, -9.5, 6.0, 11.0, 20.0),       # the gate tower: centre line x, door plane y, the path's south end, depth, width, height
    wing=(21.0, 25.0), stack=(1.5, 52.0, 27.0),
)


def build(damaged=False):
    main = []
    hc, hr, hz, hh = P["head"], P["head_r"], P["head_z"], P["head_h"]
    # ---- the plinth everything grows from
    main.append(K.prism("plinth", [(-28.0, -8.0), (-20.0, -16.0), (-6.0, -14.0), (0.6, -10.7), (0.6, 5.0), (13.4, 5.0), (13.4, -3.4), (19.0, 0.0), (26.0, 14.0), (28.0, 29.0), (20.0, 43.0),
                                    (6.0, 54.0), (-10.0, 55.0), (-24.0, 46.0), (-29.0, 20.0)], 0.0, 3.0, "stone_dk", 0.8, 2.0))
    # ---- the helmet: a drum and dome, brow band, crest plate
    main.append(K.cyl("head_drum", hc, hr, 0.0, hz + 0.5, "stone_dk", 32, hr - 0.6))
    head = K.dome("head", hc, hr, hz, hh, "hk_shell", 32, 8)
    main.append(head)
    main.append(K.arc("brow", hc, hr - 0.4, hr + 1.4, 150.0, 262.0, hz - 2.2, hz + 1.2, "hk_brass", 16))
    main.append(K.arc("brow_hi", hc, hr + 1.0, hr + 2.2, 170.0, 255.0, hz - 0.8, hz + 1.0, "white", 12))

    def zh(x, y):
        return hz + hh * math.sqrt(max(0.03, 1 - ((x - hc[0]) ** 2 + (y - hc[1]) ** 2) / (hr * hr)))
    import bmesh
    pts_o = [K.polar(hc, hr * 0.78, 40.0 + 100.0 * j / 8) for j in range(9)]
    pts_i = [K.polar(hc, hr * 0.30, 140.0 - 100.0 * j / 8) for j in range(9)]
    bm = bmesh.new()
    ring = pts_o + pts_i
    K.rings_solid(bm, [[(x, y, zh(x, y) - 0.4) for (x, y) in ring], [(x, y, zh(x, y) + 0.45) for (x, y) in ring]])
    main.append(K.house(K.new_obj("crest", bm, "HOUSE"), 0.14))
    for i in range(5):
        a = 150.0 + 28.0 * i
        pts = [K.polar(hc, hr * t, a) for t in (0.25, 0.5, 0.75, 0.95)]
        for j in range(3):
            main.append(K.seam(f"head_seam{i}_{j}", (*pts[j], zh(*pts[j]) + 0.05), (*pts[j + 1], zh(*pts[j + 1]) + 0.05), 0.3, 0.25))
    # eye sockets under the brow, the pale visor and jaw
    for i, a in enumerate((205.0, 245.0)):
        q = K.polar(hc, hr - 0.2, a)
        main.append(K.cyl(f"eye{i}", q, 2.4, hz - 8.0, hz - 3.4, "recess", 10))
        main.extend(K.lamp(f"eye_lamp{i}", (*K.polar(hc, hr + 1.2, a), hz - 6.0), 0.7))
    main.append(K.ramp("visor", [(-10.0, 19.0), (5.0, 24.0)], [(7.0, 35.0), (-7.0, 31.0)], 6.0, 12.0, 21.0, "white", 0.4))
    main.append(K.obox("jaw", (-2.0, 17.0), 20.0, 6.5, 4.0, 0.0, 12.5, "hk_plate", 0.6, 0.85))
    main.append(K.obox("jaw_step", (-4.6, 8.0), 20.0, 3.4, 3.0, 0.0, 7.5, "white", 0.5, 0.85))
    # ---- the west hall: a lying armour barrel with house trim
    a, b, r = P["west_a"], P["west_b"], P["west_r"]
    west = K.tank("west", a, b, r, "wall", 24, 0.5)
    main.append(west)
    for i, (t0, t1) in enumerate(((0.05, 0.085), (0.90, 0.935))):
        main.append(K.house(K.hoop(f"west_trim{i}", a, b, t0, t1, r + 0.25, "HOUSE", 24, (10.0, 175.0)), 0.18))
    main.append(K.house(K.hoop("west_line", a, b, 0.085, 0.90, r + 0.2, "HOUSE", 24, (168.0, 176.0)), 0.18))
    for i, t in enumerate((0.3, 0.5, 0.7)):
        main.append(K.hoop(f"west_rib{i}", a, b, t - 0.012, t + 0.012, r + 0.2, "hk_shell", 24))
    main.append(K.obox("west_plate", (-27.0, 8.0), 92.0, 9.0, 1.2, 0.0, 9.0, "stone", 0.4))
    # ---- the east hall: blackened, the house door and struts at its south end, the east wing
    a2, b2, r2 = P["east_a"], P["east_b"], P["east_r"]
    east = K.tank("east", a2, b2, r2, "wall", 24, 0.5)
    main.append(east)
    for i, t in enumerate((0.25, 0.55, 0.8)):
        main.append(K.hoop(f"east_rib{i}", a2, b2, t - 0.012, t + 0.012, r2 + 0.2, "stone", 24))
    main.append(K.house(K.hoop("east_trim", a2, b2, 0.3, 0.34, r2 + 0.25, "HOUSE", 24, (0.0, 80.0)), 0.15))
    # ---- the gate tower on the soldiers' spawn point, the path through the plinth, the struts beside it
    dx, dy, dpath, ddep, dwid, dhgt = P["door"]
    dmain, glow, danch = BD.door(K, "tower", dx, dy, dpath, width=dwid, depth=ddep, height=dhgt, wall="stone_dk", trim="hk_brass", dark="hk_black",
                                 flank=3.6, damaged=damaged)
    main.extend(dmain)
    main.append(K.house(K.box("tower_plate", dx - 3.2, dx + 3.2, dy - 0.8, dy - 0.4, dhgt + 0.5, dhgt + 2.0, "HOUSE", 0.1), 0.3))
    for i, (p, q) in enumerate((((-1.5, 9.0, 13.0), (-2.0, -3.0, 0.3)), ((15.5, 9.0, 12.0), (17.0, 0.0, 0.3)))):
        main.append(K.beam(f"strut{i}", p, q, 2.0, 1.6, "copper", 0.2))
    wc = P["wing"]
    main.append(K.obox("wing", wc, -20.0, 9.0, 6.5, 0.0, 8.0, "skirt", 0.6, 0.9))
    main.append(K.obox("wing_roof", wc, -20.0, 7.6, 5.0, 7.8, 9.0, "hk_black", 0.3))
    main.append(K.house(K.obox("wing_panel", (wc[0] + 4.0, wc[1] - 2.0), -20.0, 1.8, 2.6, 8.8, 9.7, "HOUSE", 0.15), 0.5))
    main.append(K.house(K.obox("wing_bar", (wc[0] + 0.5, wc[1] - 6.8), -20.0, 3.0, 0.5, 2.0, 6.0, "HOUSE"), 0.25))
    main.extend(K.vent("wing_vent", K.polar(wc, 9.05, -20.0), 70.0, 2.4, 2.4, 5.6, 0.5, 2, "stone"))
    # ---- chimney, the copper dome and rubble at the south-west foot, pipes
    sx, sy, sh = P["stack"]
    st, top = K.stack("stack", (sx, sy), 1.9, 0.0, sh, "copper", 2)
    main.extend(st)
    main.append(K.cyl("pot_base", (-18.0, -15.0), 6.4, 0.0, 3.0, "copper_dk", 18))
    main.append(K.dome("pot", (-18.0, -15.0), 5.6, 2.6, 5.4, "copper", 18))
    main.extend(K.lamp("pot_lamp", (-16.0, -13.0, 8.2), 1.1))
    main.extend(K.rocks("rub_w", (-27.0, -9.0), 4.6, 11, 5, 0.7))
    main.extend(K.rocks("rub_s", (-8.0, -17.0), 3.0, 13, 3, 0.6))
    main.extend(K.pipe("pipe_w", [(-8.0, -4.0, 6.0), (-8.0, -10.0, 6.0), (-11.0, -12.0, 3.0)], 1.3, "copper"))
    main.extend(K.pipe("pipe_e", [(20.0, 12.0, 5.0), (26.0, 14.0, 5.0), (27.0, 18.0, 7.0)], 1.2, "orange"))
    if damaged:
        K.hole(main, [head], (hc[0] + 3.0, hc[1] - 3.0), 5.5, zh, 3, depth=6.0, panel=("roof", "hk_shell"))

        def zw(x, y):
            return a[2] + math.sqrt(max(4.0, r * r - (x - a[0]) ** 2))
        K.hole(main, [west], (-19.0, 8.0), 4.6, zw, 5, depth=6.0, panel=("roof", "roof2"))
        K.scorch(main, (10.0, 16.0), 6.0, lambda x, y: 18.5, 7, 5)
        K.wall_gash(main, (-1.0, 12.8), 20.0, 5.0, 3.0, 10.0, 9)
        K.scorch(main, wc, 5.0, 9.1, 10, 4)
    return dict(nodes=dict(main=main, glow=glow), anchors=[("fx_flue_0", top, 0.5)] + danch)


if __name__ == "__main__":
    BD.install(K, path=(0.740, 0.720, 0.680))
    K.run(KEY, build, spec=dict(flags=dict(shadow_azimuth=40.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)
