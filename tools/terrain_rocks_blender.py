# Blender script (tools/fetch_terrain_assets.py --rocks): one Poly Haven rock scan (glTF) -> a small
# game mesh for the 3D mode's designed terrain (render3d/terrain_v2.cpp).
#   blender -b --factory-startup -P tools/terrain_rocks_blender.py -- in.gltf out.glb [triangles]
# Joins the meshes, applies the transforms, decimates (collapse) to ~`triangles` (default 1400),
# then normalises: centred on X/Z, lowest point at y = 0, largest horizontal extent = 1 unit.
# Exported with positions, normals and UV 0 only (no materials: the game samples the rock's 1k
# diffuse, fetched separately, and tints it to the map's palette).
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
target = int(argv[2]) if len(argv) > 2 else 1400

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
if tris > target:
    m = obj.modifiers.new("dec", "DECIMATE")
    m.decimate_type = "COLLAPSE"
    m.ratio = target / tris
    m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)

# Normalise (Blender is Z-up; glTF export turns it into Y-up).
vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
size = max(hi.x - lo.x, hi.y - lo.y)
centre = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
for v in obj.data.vertices:
    v.co = (v.co - centre) / size
obj.data.materials.clear()
for p in obj.data.polygons:
    p.use_smooth = True
print("rock", dst, "triangles", sum(len(p.vertices) - 2 for p in obj.data.polygons),
      "height/width", (hi.z - lo.z) / size)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=False, export_materials="NONE",
                          export_texcoords=True, export_normals=True, export_yup=True)
