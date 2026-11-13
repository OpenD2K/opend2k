#pragma once
// glTF 2.0 (.glb / .gltf) -> formats::Model3D, for "3D models: New (art pack)" (docs/3d-models.md).
// Parsed with cgltf (third_party/cgltf, MIT, Johannes Kuhlmann); embedded PNG textures decoded
// with SDL_LoadPNG_IO (JPEG / KTX2 / Draco are not supported: the model is used untextured).
//
// Convention (glTF standard axes): +Y up, +X = map east, +Z = map south, 1 unit = 1 map tile
// (32 px), origin on the ground at the unit centre / the building footprint centre; vehicles face
// north (-Z), buildings show their front towards +Z. A node named "turret" (any case) becomes the
// turret part (pivot = its origin). Attributes: POSITION, NORMAL (else flat normals), TEXCOORD_0,
// COLOR_0, custom _HOUSE (house-colour coverage 0..1). One baseColor texture per model (the first
// material that has one); baseColorFactor is multiplied into the vertex colours, both linear as
// glTF defines them (converted to the renderer's sRGB values). Materials: roughness / metallic
// factors give a sun highlight, an emissive factor draws the part unshaded (lights), a material
// named HOUSE* is house-coloured (as _HOUSE = 1). Nodes named fx_* (empties or not) are anchor
// points (Model3D::anchors; their world position, size = scale). docs/3d-models.md "New models".

#include "formats/model3d.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::render3d {

struct GltfAdjust {
    float scale = 1, yawDeg = 0, y = 0;  // models.ini: scale, yaw (degrees, clockwise from above), y (tiles)
    // Buildings (applied by render3d/models.cpp after loading): how the model is fitted onto its
    // template's occupied cells (docs/3d-models.md "Placement"): Contain (default) = the smallest
    // move that brings the model's ground plan inside the cells (centred on an axis where it is
    // larger), Centre = its ground plan centred on them, Off = the origin as authored. x / z: a
    // further offset (tiles, east / south).
    enum class Fit : std::uint8_t { Contain, Centre, Off };
    Fit fit = Fit::Contain;
    float x = 0, z = 0;
    // Contain / Centre also scale a model down (uniformly, about its plan centre) until it stays
    // within its cells' box and out of the box's unoccupied cells (the free top corners of the 3 x 4
    // buildings), so it never reaches into a neighbouring building. models.ini "shrink = 0" keeps
    // the authored size (an intentional overhang).
    bool shrink = true;
    // models.ini "anchor.NAME = x y z [size]": anchor points in the glb's own axes and units
    // (tiles; x east, y up, z south), added to (or replacing) the glb's fx_* nodes.
    struct Anchor {
        std::string name;
        float pos[3] = {};
        float size = 1;
    };
    std::vector<Anchor> anchors;
    // Loader flag (not models.ini): geometry only, no texture decode (pose variants that only
    // swap the body mesh, e.g. variants/HARVESTER_unloadNN.glb).
    bool geometryOnly = false;
};

// Also fills Model3D::skeleton (nodes, rest TRS, animations; models with at most 255 nodes) and
// the vertices' joints / weights: skinned primitives (JOINTS_0 / WEIGHTS_0 of the node's skin)
// are stored in their bind pose with the skin's joints, other meshes rigidly on their own node.
bool loadGltfModel(const std::string& path, const GltfAdjust& adj, formats::Model3D& out, std::string& err);

// A pose variant of a model (variants/KEY_poseNN.glb: the same file with some nodes moved):
// every node's local TRS in that file (translation 3, rotation 4 = x y z w, scale 3 = 10 floats
// per node). False when it cannot be parsed or its node names differ from `names`.
bool loadGltfNodeTrs(const std::string& path, const std::vector<std::string>& names, std::vector<float>& trs,
                     std::string& err);
// The node names of a glb (to match variants), in file order.
bool loadGltfNodeNames(const std::string& path, std::vector<std::string>& names);

// An infantry frame map (models/unit/NAME.anim.json, docs/art-pipeline.md "Animated models"):
// for every frame of the unit's original art (key = offset from the art's first frame =
// animFrame * dirs + direction column), the glTF animation, its time in frames at 24 fps, the turn
// (degrees clockwise), the shift (original px east / north), scale and alpha of that frame.
struct FrameMapEntry {
    std::string anim;
    float frame = 0, yaw = 0, shiftE = 0, shiftN = 0, scale = 1, alpha = 1;
};
struct FrameMap {
    int dirs = 8;
    std::vector<FrameMapEntry> frames;  // by key; anim empty = no entry
};
bool loadFrameMap(const std::string& path, FrameMap& out, std::string& err);

// A pose spec (the building generators' variants/spec.json, tools/models/atreides_kit.py): which
// pose shows for each overlay frame of the building animation ("anim_frames"), each silo fill
// level ("levels") or each animation row of a unit's art ("rows"), -1 = the rest pose. False when
// the file is missing or names another key.
bool loadPoseSpec(const std::string& path, const std::string& key, std::vector<int>& animFrames, std::vector<int>& levels,
                  std::vector<int>& rows);

}  // namespace d2k::render3d
