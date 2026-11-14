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
    // The top surface of the model drawn for building `building` near ground point (gx, gy) (world
    // px): (gx, gy) moves to the nearest point within a few px where the model stands tall (its roof,
    // a tank, a chimney - not the low apron), z = that surface's height above the terrain there.
    // Effects that sit ON a building (fires) use it; false = no model (2D, sprite cards).
    virtual bool buildingSurface(const game::World& /*w*/, int /*side*/, int /*building*/, float& /*gx*/, float& /*gy*/,
                                 float& /*z*/) const {
        return false;
    }
    // models.ini switches of a redesigned building type (docs/3d-models.md "models.ini"):
    // `overlay = off` - the original animation overlay (painted for the 1998 art) is not drawn over
    // the redesigned art; `glow = blink` - its emissive surfaces blink in the rhythm of that overlay
    // (render::buildingGlow). False for a type drawn with the original art.
    virtual bool buildingOverlayOff(int /*buildingType*/) const { return false; }
    // (an int: 0 steady, 1 blink, 2 `glow = pulse` - a smooth swell once per overlay cycle)
    virtual int buildingGlowBlink(int /*buildingType*/) const { return 0; }
    // `overlay = keep`: the pack's own overlay frames stay drawn next to the fx_weld flashes (the repair pad).
    virtual bool buildingOverlayKeep(int /*buildingType*/) const { return false; }
};

}  // namespace d2k::render
