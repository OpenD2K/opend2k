"""Large gun (rocket) turret (art 12 + barrel art 13, shared by the three houses) - rebuilt with atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_large_gun_turret.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_LARGE_GUN_TURRET .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read: base art frames 3222..3237 = the pedestal with the 16 wall-link variants (as the medium
gun's), barrel art frames 3255..3286 = the launcher head at 32 facings (frame 0 = north, clockwise); no damaged
frame, no animation; build-up 38 kept.
  * head (node "turret"): a tan body with a house-colour dome at its front and a green lamp at its rear, two tan
    launcher boxes on short arms left and right - red rocket tips at their fronts, grey nozzles at their rears.
Poses: the wall stubs per link frame.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_LARGE_GUN_TURRET"
P = dict(body=5.2, pod_x=10.8, pod_w=3.4, pod_back=-7.5, pod_front=8.0, pod_h=5.2)


def build(damaged=False):
    main, links, piv = K.turret_pedestal(damaged)
    x, y, z = piv
    t = [K.cyl("head_mount", (x, y), 4.6, z, z + 3.2, "skirt", 16)]
    z += 3.0
    b = P["body"]
    t.append(K.cyl("head_ring", (x, y), 6.6, z, z + 1.4, "roof", 24))
    t.append(K.box("head_body", x - b, x + b, y - b, y + b, z + 1.2, z + 6.4, "tan", 0.6, 0.85))
    t.append(K.house(K.dome("head_dome", (x, y + 2.2), 3.3, z + 6.0, 3.0, "HOUSE", 16), 0.4))
    t.append(K.cyl("head_dome_ring", (x, y + 2.2), 3.8, z + 5.9, z + 6.5, "steel_dk", 16))
    t.append(K.dome("head_lamp", (x, y - 3.4), 1.5, z + 6.2, 1.4, "green", 10))
    anchors = []
    for i, sx in enumerate((-1, 1)):
        px = x + sx * P["pod_x"]
        t.append(K.beam(f"arm{i}", (x + sx * (b - 0.5), y, z + 4.0), (px, y, z + 4.0), 3.4, 2.6, "skirt", 0.2))
        t.append(K.box(f"pod{i}", px - P["pod_w"], px + P["pod_w"], y + P["pod_back"], y + P["pod_front"], z + 1.6, z + 1.6 + P["pod_h"], "tan", 0.5))
        t.append(K.box(f"pod{i}_band", px - P["pod_w"] - 0.2, px + P["pod_w"] + 0.2, y - 1.0, y + 1.0, z + 1.5, z + 1.8 + P["pod_h"], "skirt", 0.1))
        for j, (dx, dz) in enumerate(((-1.5, 1.4), (1.5, 1.4), (-1.5, 3.7), (1.5, 3.7))):
            t.append(K.tube(f"pod{i}_tip{j}", (px + dx, y + P["pod_front"] - 0.5, z + 1.6 + dz), (px + dx, y + P["pod_front"] + 0.9, z + 1.6 + dz), 1.0, "red", 8))
            t.append(K.tube(f"pod{i}_noz{j}", (px + dx, y + P["pod_back"] - 1.3, z + 1.6 + dz), (px + dx, y + P["pod_back"] + 0.5, z + 1.6 + dz), 1.0, "gun", 8))
        anchors.append((f"fx_muzzle_{i}", (px, y + P["pod_front"] + 0.9, z + 4.2), 1.0))
    nodes = dict(main=main, turret=t)
    nodes.update(links)
    return dict(nodes=nodes, origins=dict(turret=piv), anchors=anchors, anchor_parent={a[0]: "turret" for a in anchors})


if __name__ == "__main__":
    K.run(KEY, build, poses=[K.link_pose(c) for c in K.LINK_POSES], rest=K.link_pose(""),
          spec=dict(glb_names=['LARGE_GUN_TURRET'], levels=[K.LINK_POSES.index(c) if c else -1 for c in K.LINK_FRAMES], damaged=False,
                    turret_shadow=False,
                    flags=dict(shadow_azimuth=25.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1, shadow_strength=0.6)), P=P)
