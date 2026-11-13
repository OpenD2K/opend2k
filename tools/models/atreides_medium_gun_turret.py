"""Medium gun turret (art 10 + barrel art 11, shared by the three houses) - rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_medium_gun_turret.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_MEDIUM_GUN_TURRET .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read: base art frames 3124..3139 = the pedestal with the 16 wall-link variants (the game picks
one by the neighbouring walls; no damaged frame), barrel art frames 3157..3188 = the gun head at 32 facings
(frame 0 = north, clockwise), drawn over the pedestal shifted by exitPoint1 (-8, 0); no animation; build-up 38 kept.
  * pedestal: atreides_kit.turret_pedestal (rust drum, pale house deck, steps, rubble);
  * head (node "turret", pivot on the deck): a cream ring, a squat grey-cream body tapering to the front, a house
    stripe across its rear, a green lamp dome, one long pale barrel with a dark muzzle.
Poses: the wall stubs (nodes link_e / link_w / link_s) shown per link frame.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_MEDIUM_GUN_TURRET"
P = dict(body_w=7.4, body_back=-8.5, body_front=7.0, body_h=5.6, barrel=17.0, barrel_r=1.05)


def build(damaged=False):
    main, links, piv = K.turret_pedestal(damaged)
    x, y, z = piv
    t = []
    t.append(K.cyl("head_ring", (x, y), 8.2, z, z + 1.4, "roof", 24))
    w, yb, yf, h = P["body_w"], P["body_back"], P["body_front"], P["body_h"]
    t.append(K.prism("head_body", [(x - w, y + yb), (x + w, y + yb), (x + w, y + 1.0), (x + w * 0.55, y + yf), (x - w * 0.55, y + yf), (x - w, y + 1.0)],
                     z + 1.2, z + 1.2 + h, "stone", 0.5, 1.2))
    t.append(K.house(K.box("head_stripe", x - w - 0.2, x + w + 0.2, y + yb + 1.0, y + yb + 3.6, z + 1.6, z + 1.5 + h, "HOUSE", 0.2), 0.55))
    for i, sx in enumerate((-1, 1)):                     # ... and along both sides: a U open to the front
        t.append(K.house(K.box(f"head_stripe_s{i}", x + sx * (w + 0.2) - 0.9, x + sx * (w + 0.2) + 0.9, y + yb + 1.0, y + 2.0, z + 1.6, z + 1.5 + h, "HOUSE", 0.2), 0.55))
    t.append(K.box("head_mantlet", x - 2.4, x + 2.4, y + yf - 1.5, y + yf + 1.2, z + 2.0, z + 1.0 + h, "stone_dk", 0.3))
    t.append(K.dome("head_lamp", (x, y - 0.5), 2.5, z + 1.0 + h, 2.2, "green", 14))
    t.append(K.cyl("head_lamp_ring", (x, y - 0.5), 3.0, z + 0.9 + h, z + 1.5 + h, "steel_dk", 14))
    zb = z + 1.2 + h * 0.55
    t.append(K.tube("barrel", (x, y + yf, zb), (x, y + yf + P["barrel"], zb), P["barrel_r"], "cream_pipe", 10))
    t.append(K.tube("barrel_sleeve", (x, y + yf, zb), (x, y + yf + 4.5, zb), P["barrel_r"] + 0.6, "steel", 10))
    t.append(K.tube("barrel_muzzle", (x, y + yf + P["barrel"] - 2.2, zb), (x, y + yf + P["barrel"] + 0.2, zb), P["barrel_r"] + 0.45, "gun", 10))
    for i, sx in enumerate((-1, 1)):
        t.append(K.box(f"head_box{i}", x + sx * (w - 1.6) - 1.2, x + sx * (w - 1.6) + 1.2, y - 3.0, y + 0.5, z + 1.0 + h, z + 2.0 + h, "steel", 0.2))
    nodes = dict(main=main, turret=t)
    nodes.update(links)
    return dict(nodes=nodes, origins=dict(turret=piv), anchors=[("fx_muzzle_0", (x, y + yf + P["barrel"], zb), 1.0)],
                anchor_parent=dict(fx_muzzle_0="turret"))


if __name__ == "__main__":
    K.run(KEY, build, poses=[K.link_pose(c) for c in K.LINK_POSES], rest=K.link_pose(""),
          spec=dict(glb_names=['MEDIUM_GUN_TURRET'], levels=[K.LINK_POSES.index(c) if c else -1 for c in K.LINK_FRAMES], damaged=False,
                    turret_shadow=False,
                    flags=dict(shadow_azimuth=25.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1, shadow_strength=0.6)), P=P)
