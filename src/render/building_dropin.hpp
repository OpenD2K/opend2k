#pragma once
// Drop-in build-up (Graphics: 3D with models, Settings "Building placement: Drop-in"; not original;
// docs/3d-mode.md "Drop-in build-up"): instead of rising out of the rock (render/buildup_fx.hpp), a
// placed building model comes down folded from the sky, lands with a thud (dust ring, pebbles, a
// dark mark on the sand), unfolds bottom-up with a little overshoot and finally switches its lamps
// on. Driven by the SAME build-up counter as the original's build-up frames (buildupProgress: the
// duration is exact, the look deterministic in the counter; selling plays it backwards: the lamps
// go out, it folds and lifts off). Render-only: the World is read, never changed.
//
// Timeline over the build-up progress p (0..1):
//   0 .. kDropLand        fall: folded, accelerating down from above, a soft shadow grows under it
//   kDropLand             impact: dust burst (Effects), a short squash of the folded box
//   kDropUnfold0 .. 1     unfold: four height bands extend in turn, bottom first (model_vs "unfold",
//                         render3d/models_msl.hpp), the footprint spreads from 90 % to full size
//   kDropLights0 .. 1     the lamps (fx_light anchors, emissive surfaces) fade in
// The construction yard (an MCV deploying where it stands) does not fall: it unfolds from the ground.

#include "formats/templates.hpp"
#include "game/world.hpp"

namespace d2k::render {

inline constexpr float kDropLand = 0.24f;     // the landing
inline constexpr float kDropUnfold0 = 0.31f;  // unfolding starts ...
inline constexpr float kDropUnfold1 = 0.92f;  // ... and ends
inline constexpr float kDropLights0 = 0.84f;  // the lamps start to come on
inline constexpr float kDropFold = 0.28f;     // a folded band keeps this share of its height (model_vs)

struct DropInPose {
    float lift = 0;    // world px above its foundation
    float unfold = 1;  // 0 folded .. 1 unfolded (> 1 briefly: overshoot is in the shader's bands)
    float squash = 1;  // vertical scale of the whole model (the landing)
    float spread = 1;  // horizontal scale about the footprint centre
    float light = 1;   // lamps / emission 0..1
    float shadow = 0;  // the soft blob shadow under a falling building 0..1 (0 = none)
};

// The pose at build-up progress p (buildupProgress, 0..1) of a model `height` px tall; fromGround =
// it unfolds where it stands (the construction yard: an MCV deploying).
DropInPose dropInPose(float p, bool fromGround, float height);

// Where the impact (dust burst, thud) happens on the progress scale.
inline float dropInImpactAt(bool fromGround) { return fromGround ? 0.02f : kDropLand; }

// Whether a building type drops in at all: not walls / concrete slabs (they keep their build-up).
inline bool dropInType(const formats::BuildingTemplate& t) {
    return t.specialBehavior != game::building_behaviour::kWall && t.specialBehavior != game::building_behaviour::kConcrete;
}
inline bool dropInFromGround(const formats::BuildingTemplate& t) {
    return t.specialBehavior == game::building_behaviour::kConstructionYard;
}

// Who knows which buildings are drawn dropping in this frame (render3d::ModelRenderer: 3D models,
// the setting on). The Modern effects (the dust) and the scene lights (lamps fading in) ask it.
class DropInSource {
public:
    virtual ~DropInSource() = default;
    // The building's build-up progress (0..1, smoothed between ticks) when it is drawn dropping in
    // / unfolding this frame, else < 0 (complete, no model, Rise from rock, walls ...).
    virtual float buildingDropIn(const game::World& w, int side, int building) const = 0;
};

}  // namespace d2k::render
