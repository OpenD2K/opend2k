#pragma once
// Modern build-up of redesigned buildings (docs/enhanced-graphics.md "Build-up"): a building whose
// art is redesigned (an art-pack main frame without pack build-up frames, or a 3D model) is not
// shown through the original's build-up frames (1998 art) but as its finished art revealed from
// the ground up, driven by the same counter (so the duration matches; selling plays it backwards).
// Render-only.
//
// 2D (Enhanced): in the spirit of the 1998 frames (a dark stain and cracks, a heap of rocks over the
// footprint, the structure breaking out of it while the rubble falls away): a heap of sand-coloured
// rocks grows on the footprint, the art rises behind a broken, rubble-shaped cut line, the rocks sink
// and clear as it completes; dust puffs (Modern effects). 3D: model_fs (cyan scan lines, warm band).

#include "formats/templates.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::render {

// How much of the building stands: 0 = nothing (placement start / end of a sale), 1 = complete;
// < 0 = no build-up / sale running. `tickFrac` (0..1, the time into the next tick) smooths it
// between ticks: the build-up counter grows by one per tick, the sale counter shrinks by one.
inline float buildupProgress(const formats::Templates& T, const game::Building& b, float tickFrac = 0) {
    if (!(b.flags & (game::building_flags::kBuildup | game::building_flags::kSelling)) || b.type < 0 ||
        b.type >= formats::kMaxBuildingTypes)
        return -1;
    const auto& t = T.bin.buildings[b.type];
    const int frames = t.buildupArt < formats::kMaxBuildingTypes ? int(T.bin.buildupArtFrames[t.buildupArt]) : 0;
    if (frames <= 0) return -1;
    const float dir = (b.flags & game::building_flags::kBuildup) ? 1.0f : -1.0f;
    return std::clamp((float(b.buildupCounter) + dir * std::clamp(tickFrac, 0.0f, 1.0f)) / float(3 * frames), 0.0f, 1.0f);
}

// The 3D construction band colours (warm molten orange) and the ghost (cyan scan lines).
inline constexpr float kBuildupEdge[3] = {1.0f, 0.55f, 0.2f};
inline constexpr float kBuildupGhost[3] = {0.05f, 0.45f, 1.0f};

// ---- 2D rubble build-up -------------------------------------------------------------------------

// How far the art has risen (0 = nothing above the base line, 1 = complete) at progress p: it starts
// once the first rocks lie and is complete just before the last ones clear.
inline float buildupReveal(float p) { return std::clamp((p - 0.06f) / 0.86f, 0.0f, 1.0f); }

// The broken cut line: its offset (world px, + = lower) at world x, for a building seeded `seed`;
// zero once the art stands. Two octaves of value noise, sharpened so it reads as broken stone.
float buildupJag(std::uint32_t seed, float worldX, float reveal);

// The footprint (world px) of a building: the bounding box of its solid cells.
struct BuildupFootprint {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
BuildupFootprint buildupFootprint(const formats::BuildingTemplate& t, int tileX, int tileY);

// One rock of the heap at progress p, in world px: (x, y) = the bottom centre where it is drawn
// (lifted by the heap), `size` its width, `alpha` 0..1, `shape` a cell of the rock atlas,
// `shade` its brightness (x the terrain colour). Sorted back to front.
struct BuildupRock {
    float x = 0, y = 0, size = 0, alpha = 0, shade = 1;
    int shape = 0;
};
inline constexpr int kBuildupRockShapes = 8;  // atlas: 4 x 2 cells of 32 x 32 texels
// The heap's rocks for a building (`seed` = its identity) over its footprint; `artHeight` limits
// the heap's height; `cutY` = the cut line (world y): rocks it has passed slide off, except at the
// foot of the walls. Deterministic in (seed, p, cutY): selling plays it backwards.
void buildupRocks(std::uint32_t seed, const BuildupFootprint& f, float artHeight, float p, float cutY,
                  std::vector<BuildupRock>& out);
// The heap's full height (world px) above the footprint's ground.
float buildupHeapHeight(const BuildupFootprint& f, float artHeight);
// How much of the ground stain (the dark disturbed sand under the heap) shows at p.
float buildupStain(float p);

// Textures (created once per renderer): the rock atlas (grey shaded rocks with alpha, to be
// colour-modulated with the terrain colour) and a soft round puff (white, alpha; blend mode BLEND).
SDL_Texture* makeBuildupRockTexture(SDL_Renderer* r);
SDL_Texture* makeBuildupPuffTexture(SDL_Renderer* r);

}  // namespace d2k::render
