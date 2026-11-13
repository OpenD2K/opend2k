"""The redesigned WALL (one neutral wall shared by the three houses, as the original: ATREIDES / HARKONNEN / ORDOS
WALL use three identical arts 4 / 22 / 40) - built with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/wall.py -- [--out DIR] [--bake-size 512] [--device cpu]
    .venv/bin/python -I tools/models/wall_pack.py --pack artpacks/opend2k-redesign --models DIR     (the 2D frames)

How the original was read (DATA.R16 frames 3069.. / 3343.. / 3617..: no base frame, 16 link frames + 16 damaged,
32 x 32 art, one cell): a low grey concrete barrier with a rounded (half-round) top, made of drum-like segments with
dark joints, a darker footing; the 16 frames are the connections to the walls on the four neighbouring cells - the
game picks frame kWallFrameMapping[W | N << 1 | E << 2 | S << 3] (MapRenderer::drawBuilding; only walls and wall-linking
turrets link). Single wall (no neighbour) = a short block; damaged frames = the same shapes broken, with rubble.
  * nodes: "main" = the centre post (always), "arm_e" / "arm_w" / "arm_n" / "arm_s" = a half-cell wall from the post
    to that cell edge (origins at the cell centre, so a pose scales an absent arm into the post);
  * the profile is identical on every arm and ends exactly on the cell edge, so neighbouring walls join into one run.

Writes (DIR default artpacks/models-wall):
  models/building/WALL.glb       3D: the rest pose = the post alone (frame 0), arms hidden; anchors none
  variants/WALL_poseNN.glb       pose NN = link mask NN + 1 (1..15: W 1, N 2, E 4, S 8) - the arms shown
  variants/spec.json             "levels": wall frame (0..15) -> pose (models.cpp buildingPose, as the turrets' links)
  2d/WALL[_damaged]_mMM.glb      the 2D renders' models, one per mask MM and state, with the north / south arms
                                 stretched to 32 / cos 45 px: the 2D camera foreshortens north-south distances by
                                 cos 45 (tools/render_sprites "Rendered from 3D"), the game draws every cell 32 px tall
                                 - so a straight north-south run of frames joins without gaps.
Emission: none (concrete).
"""
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

V3, V2, RF = K.V3, K.V3.V2, K.RF
bpy = K.bpy

KEY = "WALL"
# px: half cell, wall thickness, straight wall height (below the rounded top), top radius, post
P = dict(half=16.0, t=10.4, h0=4.8, cap=1.0, post=12.0, post_h=11.8, seg=8.0)
# wall frame (0..15) -> link mask (W 1, N 2, E 4, S 8): the inverse of kWallFrameMapping
WALL_FRAME_MAPPING = [0, 2, 3, 12, 1, 6, 11, 7, 4, 13, 5, 9, 15, 10, 8, 14]
MASK_OF_FRAME = [WALL_FRAME_MAPPING.index(f) for f in range(16)]
ARMS = (("arm_w", (-1, 0), 1), ("arm_n", (0, 1), 2), ("arm_e", (1, 0), 4), ("arm_s", (0, -1), 8))


def arm(name, d, L, damaged, seed):
    """A half-cell wall from the post (length post / 2) to the cell edge at distance L along d."""
    t, h0, r = P["t"], P["h0"], P["t"] / 2
    s0 = P["post"] / 2 - 0.6
    out = []
    rnd = random.Random(seed)

    def pt(s, o=0.0):     # along the arm s, across o (to the right of d)
        return (d[0] * s + d[1] * o, d[1] * s - d[0] * o)

    def seg_box(nm, a, b, w, z0, z1, mat, bev=0.25):
        (x0, y0), (x1, y1) = pt(a, -w), pt(b, w)
        return K.box(nm, min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1), z0, z1, mat, bev)

    # the footing (a little wider, dark), the straight body, the rounded top: drums between dark joints
    out.append(seg_box(f"{name}_foot", s0, L, t / 2 + 0.9, 0.0, 1.4, "concrete_dk", 0.3))
    cuts = [s0, L - P["seg"] * 1.0, L] if L - s0 > P["seg"] * 1.4 else [s0, L]
    for i, (a, b) in enumerate(zip(cuts[:-1], cuts[1:])):
        a2, b2 = a + 0.35, b - 0.35
        top_k = 1.0
        if damaged and rnd.random() < 0.75:      # a broken drum: shorter, chipped top
            top_k = rnd.uniform(0.45, 0.75)
        out.append(seg_box(f"{name}_body{i}", a2, b2, r, 1.2, h0 + (1 - top_k) * -2.0, "stone", 0.35))
        if top_k > 0.99:
            out.append(K.vault(f"{name}_top{i}", pt(a2), pt(b2), r, h0, "concrete", 12, h=r * 1.05))
            out.append(K.hoop(f"{name}_band{i}", (*pt(a2 + 0.5), h0), (*pt(b2 - 0.5), h0), 0.42, 0.58, r + 0.12, "concrete_dk", 12,
                              arc=(0.0, 180.0), squash=1.05))
        else:
            k = top_k
            out.append(K.vault(f"{name}_top{i}", pt(a2), pt(a2 + (b2 - a2) * k), r, h0 - 2.0, "concrete", 10, h=r * 0.8))
            out.append(seg_box(f"{name}_break{i}", a2 + (b2 - a2) * k - 0.4, b2, r - 0.6, h0 - 2.4, h0 - 1.2, "burnt", 0.2))
        # the joint: a near-black seam all round the section at the start of the drum
        # (a dark core set 0.45 px into the 0.7 px gap between two drums: a groove, not a protruding ring)
        out.append(K.vault(f"{name}_joint{i}", pt(a - 0.05), pt(a + 0.75), r - 0.45, h0, "recess", 12, h=(r - 0.45) * 1.05))
        out.append(seg_box(f"{name}_jointw{i}", a - 0.05, a + 0.75, r - 0.45, 1.2, h0 + 0.05, "recess", 0.0))
    if damaged:                                  # rubble at its foot, soot on the top
        for k in range(3):
            s = rnd.uniform(s0 + 1, L - 1)
            o = rnd.choice((-1, 1)) * (r + rnd.uniform(0.8, 2.6))
            c = pt(s, o)
            out.append(K.box(f"{name}_rubble{k}", c[0] - 1.1, c[0] + 1.1, c[1] - 0.9, c[1] + 0.9, 0.0, rnd.uniform(0.8, 1.8), "rubble", 0.3))
        K.scorch(out, pt((s0 + L) / 2), 3.0, h0 + r * 0.9, seed + 7, 3)
    return out


def post(damaged):
    w, hp = P["post"] / 2, P["post_h"]
    out = [K.box("post_foot", -w - 1.0, w + 1.0, -w - 1.0, w + 1.0, 0.0, 1.6, "concrete_dk", 0.3),
           K.box("post_body", -w, w, -w, w, 1.2, hp - 2.2, "stone", 0.45, 0.94),
           K.box("post_cap", -w - 0.5, w + 0.5, -w - 0.5, w + 0.5, hp - 2.4, hp - 1.0, "concrete", 0.4),
           K.box("post_top", -w + 1.4, w - 1.4, -w + 1.4, w - 1.4, hp - 1.1, hp, "concrete_dk", 0.35, 0.85)]
    for i, (x0, x1, y0, y1) in enumerate(((-w - 0.05, w + 0.05, -w - 0.05, -w + 0.6), (-w - 0.05, -w + 0.6, -w - 0.05, w + 0.05),
                                          (w - 0.6, w + 0.05, -w - 0.05, w + 0.05), (-w - 0.05, w + 0.05, w - 0.6, w + 0.05))):
        out.append(K.box(f"post_band{i}", x0, x1, y0, y1, 5.0, 6.2, "steel_dk", 0.1))       # a steel band round the post
    out.append(K.box("post_seam", -w - 0.06, w + 0.06, -0.25, 0.25, 6.2, hp - 2.4, "recess", 0.0))
    if damaged:
        rnd = random.Random(5)
        out.append(K.box("post_chip", w - 3.2, w + 0.2, -w - 0.2, -w + 3.0, hp - 4.5, hp + 0.1, "burnt", 0.2))
        for k in range(2):
            c = (rnd.uniform(-w, w), -w - rnd.uniform(1.0, 2.5))
            out.append(K.box(f"post_rubble{k}", c[0] - 1.2, c[0] + 1.2, c[1] - 0.8, c[1] + 0.8, 0.0, rnd.uniform(0.9, 1.6), "rubble", 0.3))
        K.scorch(out, (0.0, 0.0), 3.0, hp, 41, 3)
    return out


def build(damaged=False, ns=16.0):
    nodes = dict(main=post(damaged))
    for i, (name, d, _bit) in enumerate(ARMS):
        L = ns if d[1] != 0 else P["half"]
        nodes[name] = arm(name, d, L, damaged, 100 + 17 * i + (50 if damaged else 0))
    return dict(nodes=nodes, origins={n: (0.0, 0.0, 0.0) for n in nodes})


def show(nodes, mask):
    for name, _d, bit in ARMS:
        s = 1.0 if mask & bit else 0.001
        nodes[name].scale = (s, s, s)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=os.path.join(os.path.dirname(os.path.dirname(K.HERE)), "artpacks", "models-wall"), bake_size=512, no2d=False)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--no-2d":
            opt["no2d"] = True
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    V2.setup_gpu = V3.setup_device
    out = opt["out"]
    vdir, d2 = os.path.join(out, "variants"), os.path.join(out, "2d")
    os.makedirs(vdir, exist_ok=True)
    os.makedirs(d2, exist_ok=True)
    runs = [(False, P["half"], "3d")]
    if not opt["no2d"]:
        runs += [(False, P["half"] / math.cos(math.radians(45.0)), "2d"), (True, P["half"] / math.cos(math.radians(45.0)), "2d")]
    for damaged, ns, kind in runs:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        V3._mats.clear()
        t0 = time.time()
        root, nodes = K.assemble(KEY, build(damaged, ns))
        for name in list(V3._mats):
            RF.mat(name)
        K.log(f"built {kind}{' damaged' if damaged else ''}: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s")
        RF.bake_and_merge(list(nodes.values()), opt["bake_size"])
        if kind == "3d":
            show(nodes, 0)
            bpy.context.view_layer.update()
            V3.export_glb(root, os.path.join(out, "models", "building", KEY + ".glb"))
            for m in range(1, 16):
                show(nodes, m)
                bpy.context.view_layer.update()
                V3.export_glb(root, os.path.join(vdir, f"{KEY}_pose{m - 1:02d}.glb"))
            spec = dict(key=KEY, levels=[MASK_OF_FRAME[f] - 1 for f in range(16)], damaged=False, poses=15,
                        note="wall frame -> pose (pose NN = link mask NN + 1; W 1, N 2, E 4, S 8)")
            json.dump(spec, open(os.path.join(vdir, "spec.json"), "w"), indent=1)
        else:
            for m in range(16):
                show(nodes, m)
                bpy.context.view_layer.update()
                V3.export_glb(root, os.path.join(d2, f"{KEY}{'_damaged' if damaged else ''}_m{m:02d}.glb"))
        K.log(f"exported {kind}{' damaged' if damaged else ''} in {time.time() - t0:.1f} s")


if __name__ == "__main__":
    main()
