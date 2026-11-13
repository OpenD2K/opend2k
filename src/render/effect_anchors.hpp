#pragma once
// Named anchor points of 3D models for the Modern effects (docs/3d-models.md "Anchors"): in
// Graphics: 3D with models, effects that leave a part of an object (the refinery's flue tops,
// later barrel tips) take the model's anchor positions instead of the offsets measured on the 2D
// art. Implemented by render3d::Renderer3D (the models' placement on the terrain); without it
// (2D, sprites in 3D) the effects keep their 2D placement. Render-only.

#include <string_view>
#include <vector>

namespace d2k::game {
class World;
}

namespace d2k::render {

struct EffectAnchor {
    float x = 0, y = 0;  // ground point (world px, the particles' x / y)
    float z = 0;         // height above the terrain at (x, y) (the particles' z)
    float size = 1;      // the anchor's size factor (node scale / models.ini), 1 = normal
};

class EffectAnchors {
public:
    virtual ~EffectAnchors() = default;
    // The anchors whose name starts with `prefix` (in name order) of the model drawn for building
    // `building` / unit `unit` of `side`; false when it has no model or no such anchor.
    virtual bool buildingAnchors(const game::World& w, int side, int building, std::string_view prefix,
                                 std::vector<EffectAnchor>& out) const = 0;
    virtual bool unitAnchors(const game::World& w, int side, int unit, std::string_view prefix,
                             std::vector<EffectAnchor>& out) const = 0;
};

}  // namespace d2k::render
