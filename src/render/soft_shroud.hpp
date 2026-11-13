#pragma once
// Enhanced "Soft" shroud (docs/enhanced-graphics.md): instead of the original's 32x32 edge pieces,
// a low-resolution darkness mask (2x2 texels per cell) built from World shroud bytes, blurred on
// the CPU with a separable gaussian and drawn over the map stretched with linear filtering.
// Render only: reads Cell::shroud, never writes it.
//
// Nothing hidden is revealed: fully shrouded cells (low nibble 1) and every texel next to one stay
// opaque, so bilinear filtering cannot lighten any part of such a cell; the gradient is biased
// into the revealed side (about two tiles wide). Edge cells (low nibble 2..15) start from the mean
// darkness of the original edge piece's quadrants, so the shape follows the original's edges.
// Updated incrementally: only the region around cells whose shroud byte changed is recomputed.

#include <cstdint>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::game {
class World;
}

namespace d2k::render {

class SpriteCache;
struct Camera;

class SoftShroud {
public:
    SoftShroud(SDL_Renderer* r, SpriteCache& sprites);
    ~SoftShroud();
    SoftShroud(const SoftShroud&) = delete;
    SoftShroud& operator=(const SoftShroud&) = delete;

    // Draws the mask over the map view (same origin rounding as the terrain).
    void draw(const game::World& w, const Camera& cam, int viewW, int viewH);
    // The mask itself, brought up to date (3D mode: sampled by the terrain / sprite shaders;
    // kTexelsPerCell texels per cell, alpha = darkness). nullptr if it could not be created.
    SDL_Texture* mask(const game::World& w) {
        sync(w);
        return tex_;
    }

    static constexpr int kTexelsPerCell = 2;

private:
    void sync(const game::World& w);
    void rebuild(const game::World& w, int cx0, int cy0, int cx1, int cy1);  // cell rect, exclusive
    void initQuadrants();

    SDL_Renderer* r_;
    SpriteCache& sprites_;
    SDL_Texture* tex_ = nullptr;
    const game::World* world_ = nullptr;
    int cw_ = 0, ch_ = 0;            // map size in cells
    int tw_ = 0, th_ = 0;            // texture size in texels
    std::vector<std::uint8_t> last_;  // shroud bytes the texture was built from
    std::vector<float> base_;         // per texel: 1 clear .. 0 fully shrouded
    std::vector<std::uint8_t> hidden_;  // per texel: inside or next to a fully shrouded cell
    std::vector<float> tmp_, blur_, open_;  // open_: blurred "not near a fully shrouded cell"
    std::vector<std::uint32_t> px_;
    float quad_[256][4] = {};  // per shroud byte: brightness of the edge piece's 2x2 quadrants
    bool quadInit_ = false;
};

}  // namespace d2k::render
