#pragma once
// Working flashes of redesigned buildings (docs/3d-models.md "Anchors", "fx_weld"): the original
// draws a factory's "working" animation overlay (additive welding flashes, BlitVehicles 0x428e30)
// at positions painted for the original art. A redesigned model / rendered pack moves the works
// (the heavy factories' vehicle bay), so a building whose model has "fx_weld*" anchors (models.ini
// `anchor.fx_weld_N = x y z [size]`) drops the original overlay and gets procedural blue-white
// arc flashes at those anchors instead, timed by the same animation counter (2D Enhanced with the
// rendered pack: MapRenderer; Graphics: 3D: Renderer3D), plus a flickering light there
// (SceneLights) and, with Modern effects, sparks (Effects). Original graphics and buildings
// without the anchors keep the original overlay. Render only (reads the World).

#include "render/effect_anchors.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::formats {
struct Templates;
}
namespace d2k::game {
class World;
struct Building;
}

namespace d2k::render {

class ArtPack;

// The flash of a working building this tick.
struct WeldFlash {
    int anchor = 0;     // which of the building's fx_weld anchors (0..n-1)
    float a = 0;        // intensity 0..1 (flickers tick by tick)
    float size = 1;     // size factor of this flash (on top of the anchor's own)
};

// Whether building `b`'s animation overlay would show this tick (the conditions of
// MapRenderer::drawBuilding: permanent / once + HAS_ANIMATION, not building up / selling, at
// least half health, not dying) and the flash it makes over `anchors` (> 0) anchor points.
// False in the overlay's rest frame (0) and between arcs.
bool weldFlash(const game::World& w, const game::Building& b, const formats::Templates& t, int anchors, WeldFlash& out);

// Working lamps (`fx_work*` anchors, docs/3d-models.md "Working lamps"): signal lamps of a redesigned building
// that run while the simulation marks it working - the animation flags permanent / once (0x30) that production
// (barracks training, palace charging its special weapon), the repair pad's repair steps, the conyard after a
// placement and the frigate set, also on buildings whose template has no overlay animation (the barracks).
// A chase: the lit lamp steps every 4 game ticks, the one before it fades; off while building up / selling,
// below half health or dying. Fills `a` (one intensity per anchor, 0..1); false = not working.
bool workLamps(const game::World& w, const game::Building& b, const formats::Templates& t, int anchors, float* a);
constexpr float kWorkLamp[3] = {1.0f, 0.62f, 0.16f};  // amber
constexpr int kMaxWorkLamps = 16;

// A launch flame (`fx_launch*` anchors): the Harkonnen palace's Death Hand launch - the original's once-overlay
// (the missile rising out of its silo on fire, 21 frames) as a flame at the silo mouth that flares up and dies
// down over the overlay's frames (k / frames), flickering tick by tick. False = no launch running.
bool launchFlame(const game::World& w, const game::Building& b, const formats::Templates& t, float& a, float& size);
constexpr float kLaunchHalo[3] = {1.0f, 0.45f, 0.10f};
constexpr float kLaunchCore[3] = {1.0f, 0.88f, 0.55f};

// A soft round glow for the flashes: white, premultiplied alpha (draw with SDL_BLENDMODE_ADD and a
// colour mod); the caller owns it.
SDL_Texture* makeWeldGlowTexture(SDL_Renderer* r);
// The flash colours: halo (blue-white) and core (near white), 0..1.
constexpr float kWeldHalo[3] = {0.55f, 0.75f, 1.0f};
constexpr float kWeldCore[3] = {0.92f, 0.96f, 1.0f};

// 2D: the anchors of a rendered art pack (pack.ini kind=rendered; the pack's <root>/models.ini),
// projected the way tools/render_sprites/render_pack.py renders the models (orthographic, looking
// north, 45 degrees down; the glb origin = the occupied cells' centre; models.ini scale / yaw / y
// applied), for the buildings whose main frame the pack replaces. Serves the buildings' fx_weld
// anchors and the vehicles' fx_light* lamps (SceneLights; the 2D placement of every other effect
// stays as it is). A vehicle's anchors turn with its facing about the model origin, which the pack
// draws at the unit's position plus its vertical centroid alignment (render_pack.py moves each unit's
// frames so their centroid matches the original sprite's): models.ini `anchor2d_dy = px` (the origin
// drawn that far below the unit position; the redesign's harvester 6.5, MCV 5).
class Effects;
class PackAnchors final : public EffectAnchors {
public:
    // Reads the pack's models.ini (nullptr / no file: no anchors).
    void load(const ArtPack* pack, const formats::Templates& t);
    bool empty() const { return types_.empty() && unitTypes_.empty() && overlayOff_.empty() && glowBlink_.empty() && overlayKeep_.empty(); }
    // The Modern effects' eased vehicle facing (what the 2D frames are drawn at); nullptr: the model facing.
    void setEffects(const Effects* fx) { fx_ = fx; }
    bool buildingAnchors(const game::World& w, int side, int building, std::string_view prefix,
                         std::vector<EffectAnchor>& out) const override;
    bool unitAnchors(const game::World& w, int side, int unit, std::string_view prefix,
                     std::vector<EffectAnchor>& out) const override;
    // models.ini `overlay = off` / `glow = blink` of the buildings whose main frame the pack replaces.
    bool buildingOverlayOff(int type) const override {
        return type >= 0 && std::size_t(type) < overlayOff_.size() && overlayOff_[std::size_t(type)];
    }
    int buildingGlowBlink(int type) const override {
        return type >= 0 && std::size_t(type) < glowBlink_.size() ? glowBlink_[std::size_t(type)] : 0;
    }
    // models.ini `overlay = keep`: the pack's own overlay frames stay drawn next to the fx_weld flashes.
    bool buildingOverlayKeep(int type) const override {
        return type >= 0 && std::size_t(type) < overlayKeep_.size() && overlayKeep_[std::size_t(type)];
    }

private:
    struct Point {
        std::string name;
        float dx = 0, dy = 0, z = 0, size = 1;  // from the building's top-left (world px); height
    };
    struct UnitPoint {
        std::string name;
        float x = 0, y = 0, z = 0, size = 1;  // glb axes after the models.ini adjust (tiles; x east, y up, z south)
        float dy2d = 0;                        // models.ini anchor2d_dy (world px)
        // fx_muzzle* "turret": turns with the turret facing about the turret pivot (glb axes after the
        // adjust), which the pack's turret frames keep where it is at body facing 0 (render_pack.py).
        bool turret = false;
        float pivotX = 0, pivotZ = 0;
    };
    std::vector<std::vector<Point>> types_;  // per building type (empty: none)
    std::vector<std::vector<UnitPoint>> unitTypes_;  // per unit type (empty: none)
    std::vector<std::uint8_t> overlayOff_, glowBlink_, overlayKeep_;  // per building type (empty: none); glow: 1 blink, 2 pulse
    const Effects* fx_ = nullptr;
};

}  // namespace d2k::render
