#pragma once
// A small, renderer-neutral 3D model: what the 3D mode's model renderer (render3d/models.*) draws
// in place of a unit / building billboard. Filled by the PS1 disc loader (formats/ps1_models.*)
// or the glTF art-pack loader (render3d/model_gltf.*). docs/3d-models.md.
//
// Units: world pixels (one map tile = 32), axes as the 3D mode's world: +x east, +y up, +z south.
// Unit models: origin = the unit's position on the ground; front towards -z (north) = facing 0.
// Building models: see Anchor.

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::formats {

struct MeshVertex {
    float pos[3] = {};
    float nrm[3] = {0, 1, 0};
    float uv[2] = {};
    float rgba[4] = {1, 1, 1, 1};
    // House colour: 0 none, 0..1 = tint the texel's luminance with the house colour by this much
    // (art-pack masks), 2 = solid house colour (the PS1 house markers).
    float house = 0;
    float lit = 1;       // 1 = shade with the normal; 0 = the colour is pre-lit (PS1 buildings)
    float textured = 1;  // 0 = vertex colour only (untextured polygons)
    float oneSided = 0;  // 1 = drawn only from its front (counter-clockwise) side (PS1 back curtains)
    // Sun highlight (glTF material: gloss = 1 - roughness, metal = metallic); 0 = matte (PS1).
    float gloss = 0, metal = 0;
    // Skinning (glTF skins and animated / posed nodes, docs/3d-models.md "Animation"): up to four
    // palette entries (= Model3D::skeleton node indices) and their weights (0..255, summing to 255).
    // All weights 0 = rigid (not affected by a pose): PS1 models, static glTF models.
    std::uint8_t joint[4] = {};
    std::uint8_t weight[4] = {};
};

struct MeshPart {
    std::vector<MeshVertex> tris;  // triangle list
    bool empty() const { return tris.empty(); }
};

struct Model3D {
    enum class Anchor : std::uint8_t {
        Centre,           // units; buildings: the centre of the footprint (occupied cells), on the ground
        ArtTopLeft,       // buildings: the top-left corner of the template's art box (tileX, tileY)
        ArtBottomLeft,    // buildings: the bottom-left corner of the art box (tileX, tileY + art rows); z <= 0
    };
    std::string name;
    Anchor anchor = Anchor::Centre;
    // One RGBA8 texture (R in the lowest byte); empty = untextured (vertex colours only).
    int texW = 0, texH = 0;
    std::vector<std::uint32_t> texture;
    bool pointSample = false;  // nearest-neighbour texels (the PS1 look)
    // The texture's alpha also carries glTF emission (render3d/model_gltf.cpp foldEmission): opaque
    // texels 128..255, 255 - 127 x emission; drawn glowing by model_fs (docs/3d-models.md "Emission").
    bool emissiveInAlpha = false;
    MeshPart body;             // fixed to the object, turned by its facing
    MeshPart turret;           // turned by the turret facing about turretPivot (empty = none)
    MeshPart shadow;           // flat polygons at ground level, drawn as a darkening of the ground
    MeshPart house;            // house-colour marker polygons (also turned with the body)
    float turretPivot[3] = {};
    bool houseOnTurret = false;  // the house marker turns with the turret (PS1 combat tank)
    // Named anchor points (docs/3d-models.md "Anchors": glTF nodes / models.ini entries named
    // fx_*, e.g. fx_flue_0 = a chimney top, fx_muzzle_0 = a barrel tip), in model space like the
    // vertices; onTurret = turns with the turret part (about turretPivot). size = the node's scale.
    struct AnchorPoint {
        std::string name;
        float pos[3] = {};
        float size = 1;
        bool onTurret = false;
    };
    std::vector<AnchorPoint> anchors;  // sorted by name
    // Animation (glTF; docs/3d-models.md "Animation"): the node hierarchy with its rest transforms,
    // the glTF animations over it, and `place` = glTF space (tiles, the glb's axes) -> model space
    // (px, after models.ini scale / yaw / y and the footprint fit). The body / house vertices carry
    // joint indices into `nodes` and are stored in the rest (bind) pose; a pose's palette entry of
    // node n is place * world_n(pose) * inverse(place * world_n(rest)) (render3d/model_anim.*).
    struct Skeleton {
        struct Node {
            int parent = -1;
            float t[3] = {}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};  // rest TRS (glTF: rotation x y z w)
        };
        struct Channel {
            int node = -1;
            std::uint8_t path = 0;  // 0 translation, 1 rotation, 2 scale
            bool step = false;      // STEP interpolation (else LINEAR)
            std::vector<float> times, values;  // values: 3 or 4 floats per key
        };
        struct Animation {
            std::string name;
            float duration = 0;  // s
            std::vector<Channel> channels;
        };
        std::vector<Node> nodes;
        std::vector<Animation> animations;
        float place[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
        bool skinned = false;  // has skinned vertices (else only rigid node parts)
        bool empty() const { return nodes.empty(); }
        int find(const std::string& animName) const {
            for (std::size_t i = 0; i < animations.size(); ++i)
                if (animations[i].name == animName) return int(i);
            return -1;
        }
    };
    Skeleton skeleton;
    // Body geometry per pose (pose variants whose meshes differ, e.g. the harvester's unload poses
    // variants/HARVESTER_unloadNN.glb): bodyPoses[k] replaces `body` in pose k (empty = none).
    std::vector<MeshPart> bodyPoses;
    bool empty() const { return body.empty() && turret.empty(); }
};

}  // namespace d2k::formats
