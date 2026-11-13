#pragma once
// Small overview image of a .MAP for the skirmish lobby: average tileset colour per cell,
// spice fields and the multiplayer start locations (special 0x17).

#include "formats/map.hpp"
#include "formats/r16.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_FRect;

namespace d2k::render {

class MapPreview {
public:
    explicit MapPreview(SDL_Renderer* r) : r_(r) {}
    ~MapPreview();
    MapPreview(const MapPreview&) = delete;
    MapPreview& operator=(const MapPreview&) = delete;

    // Builds the preview of `map` drawn with the tileset at `tilesetPath` (cached per tileset).
    void build(const formats::MapFile& map, const std::string& tilesetPath);
    // Draws the preview fitted into `area`, start locations as numbered dots.
    void draw(const SDL_FRect& area) const;
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<std::array<int, 2>>& spawns() const { return spawns_; }

private:
    SDL_Renderer* r_;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<std::array<int, 2>> spawns_;
    std::map<std::string, std::vector<formats::Rgba>> tileColours_;
};

}  // namespace d2k::render
