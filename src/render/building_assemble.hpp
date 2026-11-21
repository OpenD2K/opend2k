#pragma once
// Assemble build-up (Graphics: 3D with models, Settings "Building placement: Assemble"; not original;
// docs/3d-mode.md "Assemble build-up"): a placed building model is built up from the ground part by
// part - a glowing survey outline and dust on the footprint first, then the model's parts (its
// connected pieces, grouped: render3d/model_assembly.*) in bottom-up order: the foundation and the big
// blocks extrude out of the ground behind a glowing cut edge, roof units are lowered into place,
// side modules slide out of the core, masts swing up on a hinge, small units telescope out of the
// roof; each part seats with a weld flash, sparks and a puff of dust, and the lamps come on at the
// end. Driven by the SAME build-up counter as the original's build-up frames (buildupProgress: the
// duration is exact, the pose a pure function of the counter; selling plays it backwards).
// Render-only: the World is read, never changed.
//
// Buildings that stand at mission start or come from a save never assemble: the model renderer
// settles every building it sees on its first frame of a world (render3d::ModelRenderer::settle),
// they are drawn finished; only buildings placed during play assemble.
//
// Timeline over the build-up progress p (0..1):
//   0 .. 0.06            survey outline + dust on the footprint (Effects), nothing stands yet
//   kAssembleParts0 .. 1 the parts, each in its own window [t0, t0 + dur] (sorted bottom-up)
//   kAssembleLights0 .. 1 the lamps (fx_light anchors, emissive surfaces) fade in

#include "formats/templates.hpp"
#include "game/world.hpp"

#include <cstdint>
#include <vector>

namespace d2k::render {

inline constexpr float kAssembleParts0 = 0.05f;  // the first part starts ...
inline constexpr float kAssembleParts1 = 0.90f;  // ... the last one has seated
inline constexpr float kAssembleLights0 = 0.84f; // the lamps start to come on

// How a part arrives (model_vs "assemble", render3d/models_msl.hpp; encoded per vertex).
enum class AssembleMotion : std::uint8_t {
    Extrude = 0,    // grows out of its base behind a glowing cut (the foundation, big blocks)
    Lower = 1,      // lowered into place from above with a small bounce (roof units)
    Slide = 2,      // slides out of the core towards its place (side modules, annexes)
    Hinge = 3,      // swings up from lying outward (masts, chimneys, antennas)
    Telescope = 4,  // rises out of the surface it stands on (small roof units, towers)
};

// One part of an assembling building in the world (Effects): its foot = the centre of the bottom of
// its box (ground x / y in world px, height above the building's ground), its box size, its window.
struct AssemblyPartWorld {
    float x = 0, y = 0, z = 0;  // the foot
    float w = 0, d = 0, h = 0;  // box size: x, y (ground), height (px)
    float t0 = 0, t1 = 0;       // progress window
    AssembleMotion motion = AssembleMotion::Extrude;
};

inline float assembleLight(float p) {
    const float t = (p - kAssembleLights0) / (1.0f - kAssembleLights0);
    const float c = t < 0 ? 0.0f : t > 1 ? 1.0f : t;
    return c * c * (3 - 2 * c);
}

// Who knows which buildings are drawn assembling this frame (render3d::ModelRenderer: 3D models, the
// setting on, placed during play). The Modern effects (outline, dust, sparks) ask it.
class AssemblySource {
public:
    virtual ~AssemblySource() = default;
    // The building's build-up progress (0..1, smoothed between ticks) when it is drawn assembling this
    // frame, else < 0. parts (optional): its parts in the world, in assembly order.
    virtual float buildingAssembly(const game::World& w, int side, int building,
                                   std::vector<AssemblyPartWorld>* parts) const = 0;
};

}  // namespace d2k::render
