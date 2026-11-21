"""Reimagined models, round 2 (docs/3d-models.md "Reimagined models"): one entry point for every model of the
Reimagined pack. Each model is built by its APPROVED generator (same design, node names, origins, anchors, pose
nodes), gets modelled detail from reimagined_detail.py (armour plates with real joints and chamfers, bolts,
ledges, vents, railings, hatches, pipes, antennas; chunkier tracks on the tank), then the Reimagined bake of
reimagined_kit.py (one atlas incl. the house parts as a mask; PBR maps: colour, ORM, normal with rounded edges /
dents / wear - no procedural panel lattice any more: the joints are geometry) and a simplified version.

    blender -b --factory-startup --python tools/models/reimagined_build.py -- --only KEY[,KEY...] \
        [--out DIR] [--bake-size 2048] [--turntable DIR] [--samples 40] [--work DIR] [--no-bake] [--preview DIR]
        [--no-detail]

KEY: an engine key (ATREIDES_COMBAT_TANK, ATREIDES_CONSTRUCTION_YARD, REPAIR_PAD, TRIKE, ...; see MODELS) or "atreides"
(every Atreides model, one Blender process per model is better: the kits set module-wide palettes - run_all.sh).
--preview DIR: plain material colours, a few quick Cycles views (no bake, no export) for checking the geometry.
"""
import importlib
import json
import math
import os
import sys
import time

os.environ.setdefault("D2K_GLOW_3D", "1")   # the 3D emission strengths of the building kit's lit twins

import bpy  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402

# engine key -> (generator module, kind, model key in the generator)
MODELS = {
    "ATREIDES_COMBAT_TANK": ("reimagined_combat_tank", "tank", None),
    "ATREIDES_CONSTRUCTION_YARD": ("atreides_construction_yard", "building", None),
    "ATREIDES_REFINERY": ("refinery_pilot_fable", "refinery", None),
    "ATREIDES_WIND_TRAP": ("atreides_wind_trap", "building", None),
    "ATREIDES_BARRACKS": ("atreides_barracks", "building", None),
    "ATREIDES_LIGHT_FACTORY": ("atreides_light_factory", "building", None),
    "ATREIDES_HEAVY_FACTORY": ("atreides_heavy_factory", "building", None),
    "ATREIDES_HIGH_TECH_FACTORY": ("atreides_high_tech_factory", "building", None),
    "ATREIDES_OUTPOST": ("atreides_outpost", "building", None),
    "ATREIDES_PALACE": ("atreides_palace", "building", None),
    "ATREIDES_RESEARCH_CENTRE": ("atreides_research_centre", "building", None),
    "ATREIDES_SILO": ("atreides_silo", "building", None),
    "ATREIDES_STAR_PORT": ("atreides_star_port", "building", None),
    "REPAIR_PAD": ("atreides_repair_pad", "building", None),
    "LARGE_GUN_TURRET": ("atreides_large_gun_turret", "building", None),
    "MEDIUM_GUN_TURRET": ("atreides_medium_gun_turret", "building", None),
    "TRIKE": ("atreides_trike", "vehicle", None),
    "QUAD": ("atreides_quad", "vehicle", None),
    "MCV": ("atreides_mcv", "vehicle", None),
    "MISSILE_TANK": ("atreides_missile_tank", "vehicle", None),
    "SIEGE_TANK": ("atreides_siege_tank", "vehicle", None),
    "SONIC_TANK": ("atreides_sonic_tank", "vehicle", None),
    "CARRYALL": ("atreides_carryall", "vehicle", None),
    "ORNITHOPTER": ("atreides_ornithopter", "vehicle", None),
}
ATREIDES = list(MODELS)
# big-shape redesigns: a generator of their own with its own run() (not part of "atreides": they also write pose
# variants and a models.ini section; install with harvester_check/reimagined_install.sh)
MODELS["HARVESTER"] = ("reimagined_harvester", "own", None)
# triangle budgets (full model) and LOD1 targets
BUDGET = dict(building=34000, refinery=34000, vehicle=19500, tank=19500)
LOD_TARGET = dict(building=0.36, refinery=0.36, vehicle=6500, tank=6500)
# bake detail (reimagined_kit.DETAIL): the panel lattice / rivet rows are off - joints are geometry now
DET_BUILDING = dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.016, house_uv=0.6, ao_distance=0.3, dent=0.22, wear=0.5)
DET_VEHICLE = dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.010, house_uv=0.75, ao_distance=0.14, dent=0.25, wear=0.55)
# per-model detail overrides (reimagined_detail profile keys)
OVER = {
    "ATREIDES_REFINERY": dict(rails=True),
    "REPAIR_PAD": dict(rails=False, antenna=False, pipes=0),
    "LARGE_GUN_TURRET": dict(rails=False, antenna=False, pipes=0, hatches=0),
    "MEDIUM_GUN_TURRET": dict(rails=False, antenna=False, pipes=0, hatches=0),
    "ATREIDES_SILO": dict(antenna=False, pipes=1),
}
SKIP_NODES = {
    "ATREIDES_CONSTRUCTION_YARD": ("load", "glow", "hook"),
    "ATREIDES_SILO": ("fill",),
}


def opts():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    o = dict(out=os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-reimagined"), bake_size=2048,
             turntable=None, samples=40, work=None, only=None, bake=True, preview=None, detail=True)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--turntable", "--work", "--only", "--preview"):
            o[a[2:]] = argv[i + 1]; i += 1
        elif a == "--bake-size":
            o["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--samples":
            o["samples"] = int(argv[i + 1]); i += 1
        elif a == "--no-bake":
            o["bake"] = False
        elif a == "--no-detail":
            o["detail"] = False
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if not o["only"]:
        raise SystemExit("--only KEY[,KEY...] (or atreides)")
    return o


def build(key, detail=True):
    """-> dict(root, nodes (list), kind, rest(nodes) or None, surface(nt, name), finish())"""
    import reimagined_detail as RD
    modname, kind, _ = MODELS[key]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    V3._mats.clear()
    if kind in ("building", "refinery"):
        import atreides_kit as AK
        import refinery_pilot_fable as RF
        if detail:
            RD.tag_helpers(AK)
            RD.install("building", BUDGET[kind], skip_nodes=("glow",) + tuple(SKIP_NODES.get(key, ())), **OVER.get(key, {}))
        rest = None
        if kind == "refinery":
            V3.P["house_low"] = RF.P["house_low"]
            root, nodes = RF.build(False)
            nodes = list(nodes)
        else:
            mod = importlib.import_module(modname)
            if key == "ATREIDES_BARRACKS":          # what its __main__ sets up before the run
                mod.BD.install(AK, path=(0.900, 0.870, 0.790))
            root, nd = AK.assemble(mod.KEY, mod.build(False))
            nodes = list(nd.values())
            if key == "ATREIDES_SILO":
                rest = mod.level(0.001)
            elif key.endswith("GUN_TURRET"):
                rest = AK.link_pose("")
            else:
                rest = getattr(mod, "rest", None)
            rest_nodes = nd
        RD.uninstall()
        for name in list(V3._mats):
            RF.mat(name)
        out = dict(root=root, nodes=nodes, kind=kind, surface=RF.surface, rest=None)
        if rest is not None:
            out["rest"] = lambda: rest(rest_nodes)
        return out
    if kind == "tank":
        import reimagined_combat_tank as RT
        tank_round2()
        if detail:
            RD.install("vehicle", BUDGET[kind], extras=tank_extras)
        root = RT.build()
        RD.uninstall()
        return dict(root=root, nodes=V2.meshes_under(root), kind=kind, surface=V3.surface_tree, rest=None)
    # vehicles of the Atreides vehicle kit
    import atreides_vehicle_kit as K
    if detail:
        vehicle_kit_round2(K)
    mod = importlib.import_module(modname)
    if detail:
        RD.install("vehicle", BUDGET[kind], extras=VEHICLE_EXTRAS.get(key))
    root = mod.build()
    RD.uninstall()
    K.finish_materials()

    def surface(nt, name):
        c, r, mt = V3.surface_tree(nt, name)
        if K.SURFACE is not None:
            c, r, mt = K.SURFACE(nt, name, (c, r, mt))
        return c, r, mt
    return dict(root=root, nodes=V2.meshes_under(root), kind=kind, surface=surface, rest=None, mod=mod)


# ------------------------------------------------------------------------------------------------
# The combat tank (the owner's favourite: v3 stays recognisably the same)


def tank_round2():
    """Chunkier track links (taller pads with a centre guide horn read as links at normal zoom), the rest as v3."""
    V3.P.update(link_pitch=0.92, link_len=0.66, link_h=0.26)


def tank_extras(parts, node):
    """Hand-placed detail of the tank (px: x = v right, y = u forward, z up)."""
    import reimagined_detail as RD  # noqa: F401
    P = V3.P
    out = []
    if node == "body":
        # guide horns on the links: a small tooth in the middle of every 2nd link, all round the loop
        import bmesh
        from mathutils import Vector
        prof = V3.round_poly(P["track_profile"], P["track_round"], 3)
        tv0, tv1 = P["tread_v"]
        vm = (tv0 + tv1) / 2
        for side in (1, -1):
            bm = bmesh.new()
            for k, (p, tg) in enumerate(V3.along(prof, P["link_pitch"])):
                nrm = Vector((tg.y, -tg.x))
                for (vc, w) in ((tv0 + 0.42, 0.3), (tv1 - 0.42, 0.3)):   # grouser ends: taller blocks at both edges
                    ring = [p + tg * s + nrm * h for (s, h) in ((-0.22, 0.0), (0.22, 0.0), (0.17, P["link_h"] + 0.16), (-0.17, P["link_h"] + 0.16))]
                    V3.rings_solid(bm, [[(side * (vc + dv), q.x, q.y) for q in ring] for dv in (-w, w)])
            o = V3.new_obj("track_grousers", bm, "link")
            out.append(o)
        # sprocket teeth (the sprocket is its own node: teeth are added in the wheel builder, see build_body)
        # rear: two stowage boxes and a tow hook pair on the hull rear, an exhaust grille on the rear deck
        hr = P["hull_rear"]
        out.append(V3.box("stow_l", -2.55, -0.45, hr[0] + 0.35, hr[0] + 1.9, 6.0, P["hull_deck"] + 1.05, "panel", bev=0.12))
        out.append(V3.box("stow_r", 0.45, 2.55, hr[0] + 0.35, hr[0] + 1.9, 6.0, P["hull_deck"] + 1.05, "panel", bev=0.12))
        for s in (-1, 1):
            out.append(V3.box("tow", s * 1.9 - 0.35 + 0.25, s * 1.9 + 0.35 + 0.25, P["hull_rear_low"][0] - 0.5, P["hull_rear_low"][0] + 0.2,
                              3.4, 4.3, "gun", bev=0.08, segs=1))
    if node == "turret":
        # smoke dischargers: a cluster of 3 short tubes on both front cheeks, an antenna base + whip at the rear
        zt = P["tur_top"]
        for s in (-1, 1):
            for k in range(3):
                v = s * (4.75 + 0.0)
                u = 0.6 - k * 0.75
                out.append(V3.cyl("smoke", 0.3, u - 0.1, u + 0.55, v, zt - 1.4 + 0.15 * k, "gun", segs=8))
        out.append(V3.cyl("ant_base", 0.35, -4.4, -4.0, -2.6, zt + 0.3, "gun", segs=8))
        bm = __import__("bmesh").new()
        V3.rings_solid(bm, [[(-2.6 + 0.09 * math.cos(a), -4.2 + 0.09 * math.sin(a), zt + 0.3) for a in [i * math.pi / 2 for i in range(4)]],
                            [(-2.6 + 0.05 * math.cos(a), -4.2 + 0.05 * math.sin(a), zt + 7.5) for a in [i * math.pi / 2 for i in range(4)]]])
        out.append(V3.new_obj("antenna", bm, "gun"))
    return out


VEHICLE_EXTRAS = {}


def grousers(profile, rnd, v0, v1, pitch, link_h, name="track_grousers"):
    """Raised grouser blocks at both edges of every track link (they read as links at normal zoom)."""
    import bmesh
    from mathutils import Vector
    prof = V3.round_poly(profile, rnd, 3)
    bm = bmesh.new()
    for (p, tg) in V3.along(prof, pitch):
        nrm = Vector((tg.y, -tg.x))
        ring = [p + tg * s + nrm * h for (s, h) in ((-0.22, 0.0), (0.22, 0.0), (0.17, link_h + 0.16), (-0.17, link_h + 0.16))]
        for vc in (v0 + 0.42, v1 - 0.42):
            V3.rings_solid(bm, [[(vc + dv, q.x, q.y) for q in ring] for dv in (-0.3, 0.3)])
    return V3.new_obj(name, bm, "link")


def vehicle_kit_round2(K):
    """Chunkier tracks (taller links + grousers) and wheels with more lugs for every vehicle of the kit."""
    if getattr(K, "_rd_round2", False):
        return
    K._rd_round2 = True
    track0, wheel0 = K.track, K.wheel

    def track(parts, profile, v0, v1, belt_t=0.45, pitch=0.8, link_len=0.55, link_h=0.14, rnd=0.7, core=None, wheels=(), name="track"):
        pitch, link_len, link_h = max(pitch, 0.92), max(link_len, 0.66), max(link_h, 0.26)
        track0(parts, profile, v0, v1, belt_t, pitch, link_len, link_h, rnd, core, wheels, name)
        parts.append(grousers(profile, rnd, v0, v1, pitch, link_h, name + "_grousers"))

    def wheel(parts, u, v, r, w, tyre="tyre", hub="hub", lugs=10, z=None, hub_r=0.45, name="wheel", squash=1.0):
        return wheel0(parts, u, v, r, w, tyre, hub, max(lugs, 14) if lugs else lugs, z, hub_r, name, squash)
    K.track, K.wheel = track, wheel


# ------------------------------------------------------------------------------------------------
# Preview (plain colours) and the full run


def preview(root, out_dir, key, dist):
    os.makedirs(out_dir, exist_ok=True)
    import reimagined_kit as RK  # noqa: F401
    for o in V2.meshes_under(root):
        for m in o.data.materials:
            if m is None:
                continue
            b = m.node_tree.nodes.get("Principled BSDF")
            nm = m.name.split(".")[0]
            if b is not None and nm in V3.COL:
                c = (0.33, 0.45, 0.65) if nm.startswith("HOUSE") else V3.COL[nm]
                b.inputs["Base Color"].default_value = (*V2.srgb_to_linear(c), 1)
                for l in list(b.inputs["Base Color"].links):
                    m.node_tree.links.remove(l)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = 16
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1100, 800
    scene.view_settings.view_transform = "AgX"
    w = scene.world or bpy.data.worlds.new("w")
    scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.5, 0.6, 0.78, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.0005))
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 4.0
    so = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(so)
    so.rotation_euler = (math.radians(40), 0, math.radians(30))
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    scene.collection.objects.link(co)
    scene.camera = co
    from mathutils import Vector
    for i, (az, el, d, lens) in enumerate(((-90, 45, dist, 50), (-35, 30, dist, 50), (150, 35, dist, 50), (-60, 25, dist * 0.55, 60))):
        a, e = math.radians(az), math.radians(el)
        t = Vector((0, 0, 0.2 if dist < 4 else 0.4))
        pos = t + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * d
        co.location = pos
        co.rotation_euler = (t - pos).to_track_quat("-Z", "Y").to_euler()
        cam.lens = lens
        scene.render.filepath = os.path.join(out_dir, f"{key}_p{i}.png")
        bpy.ops.render.render(write_still=True)


def run(key, o):
    import reimagined_kit as RK
    if MODELS[key][1] == "own":
        importlib.import_module(MODELS[key][0]).run(dict(o, bake_size=o["bake_size"]))
        return
    V2.setup_gpu = V3.setup_device
    t0 = time.time()
    b = build(key, o["detail"])
    root, nodes, kind = b["root"], b["nodes"], b["kind"]
    tris = V2.tri_count(V2.meshes_under(root))
    RK.log(f"built {key}: {len(nodes)} nodes, {tris} triangles in {time.time() - t0:.1f} s")
    vehicle = kind in ("vehicle", "tank")
    if o["preview"]:
        preview(root, o["preview"], key, 2.6 if vehicle else 6.5)
        return
    if not o["bake"]:
        return
    png_dir = o["work"] or os.path.join(bpy.app.tempdir, "d2k_reimagined_" + key)
    det = DET_VEHICLE if vehicle else DET_BUILDING
    # a "glow" node of emissive GLOW_* materials (heavy_factory_kit.install: bay / window lights) keeps its own
    # materials, as in the main pack: it is not baked into the atlas
    bake_objs = [m for m in V2.meshes_under(root)
                 if not (m.name.startswith("glow") and any(x is not None and x.name.startswith("GLOW_") for x in m.data.materials))]
    _, pngs = RK.bake_reimagined(bake_objs, o["bake_size"], b["surface"], png_dir, det)
    if b["rest"] is not None:
        b["rest"]()
        bpy.context.view_layer.update()
    sub = "unit" if vehicle else "building"
    path = os.path.join(o["out"], "models", sub, key + ".glb")
    V3.export_glb(root, path)
    RK.glb_patch(path, pngs["color"], pngs["orm"], pngs["normal"])
    RK.log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    lod = os.path.join(o["out"], "models", sub, key + "_lod1.glb")
    target = LOD_TARGET[kind]
    ratio = target if target < 1 else min(1.0, target / max(tris, 1))
    lt = RK.export_lod(root, lod, ratio)
    RK.strip_images(lod)
    stats = dict(key=key, triangles=tris, lod1=lt, glb_kb=os.path.getsize(path) // 1024, bake=o["bake_size"])
    os.makedirs(os.path.join(o["out"], "stats"), exist_ok=True)
    json.dump(stats, open(os.path.join(o["out"], "stats", key + ".json"), "w"))
    if o["turntable"]:
        if vehicle:
            RK.review(root, o["turntable"], png_dir, key, samples=o["samples"], dist=2.3, target=(0, 0, 0.2),
                      closeups=[("closeup_a", 55, 22, 1.6, 70, (0, 0, 0.18)), ("closeup_b", -125, 26, 1.6, 70, (0, 0, 0.18))])
        else:
            RK.review(root, o["turntable"], png_dir, key, samples=o["samples"], dist=6.0, target=(0, 0, 0.45),
                      closeups=[("closeup_a", -60, 30, 3.2, 70, (0.2, -0.4, 0.4)), ("closeup_b", 30, 24, 3.0, 70, (0.3, 0.2, 0.4))])
    RK.log(f"total {key} {time.time() - t0:.1f} s")


def main():
    o = opts()
    keys = ATREIDES if o["only"] == "atreides" else o["only"].split(",")
    for k in keys:
        if k not in MODELS:
            raise SystemExit("unknown key " + k)
        run(k, o)


if __name__ == "__main__":
    main()
