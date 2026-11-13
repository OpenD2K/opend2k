#pragma once
// Minimap (Enhanced mode): one texel per map tile (average tile colour) with the objects the
// original radar shows (Radar_Rebuild 0x462140 rules, as in ClassicSidebar::drawRadar: shrouded
// cells black, buildings by the cell's owner, units only on fully revealed cells, enemy cloaked
// units hidden, flyers only when ornithopters, worms in their own colour; COLOURS.BIN shade 8).
// Like the original it is only on with an outpost and power >= 100 % (World::radarActive);
// otherwise the session shows the house logo. Drawn crisp (nearest, integer factor when it fits).

#include "formats/bintables.hpp"
#include "formats/r16.hpp"
#include "game/world.hpp"
#include "render/map_renderer.hpp"

#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
#include <SDL3/SDL_rect.h>

namespace d2k::render {

class Minimap {
public:
    Minimap(SDL_Renderer* r, const formats::R16File& tiles, const formats::HouseColours& colours);
    ~Minimap();
    Minimap(const Minimap&) = delete;
    Minimap& operator=(const Minimap&) = delete;

    // Rebuild the texture from the world for the player `side` (call when terrain/objects change).
    void rebuild(const game::World& w, int side = 0);
    // Draws into `area` (screen pixels), preserving aspect ratio, with the camera rectangle.
    // alpha < 1: the phone layout's see-through radar (background, map and view box faded).
    // footprint (Graphics: 3D): the camera's ground footprint, a polygon in world pixels already
    // clipped to the map, drawn instead of the 2D rectangle (empty: no frame at all).
    void draw(const SDL_FRect& area, const Camera& cam, int viewW, int viewH, float alpha = 1.0f, bool background = true,
              const std::vector<SDL_FPoint>* footprint = nullptr);
    // The placement draw() would use for `area` (map rectangle in screen pixels).
    SDL_FRect placement(const SDL_FRect& area) const;
    // Converts a screen point inside the last drawn area to world pixels; false if outside.
    bool screenToWorld(float sx, float sy, float& wx, float& wy) const;

private:
    SDL_Renderer* r_;
    std::vector<formats::Rgba> tileColour_;
    const formats::HouseColours& colours_;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    float dx_ = 0, dy_ = 0, scale_ = 1;  // last drawn placement
};

}  // namespace d2k::render
