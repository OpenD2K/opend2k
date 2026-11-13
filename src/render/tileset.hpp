#pragma once
// GPU texture atlas built from a BLOX*.R16 tileset (32x32 tiles).
// Each tile is stored with a 1px extruded border so filtering/rounding can never
// sample a neighbouring tile (prevents seams between tiles).

#include "formats/r16.hpp"

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_FRect;

namespace d2k::render {

class ArtPack;

class TileAtlas {
public:
    static constexpr int kTileSize = 32;
    static constexpr int kPad = 1;
    static constexpr int kStride = kTileSize + 2 * kPad;
    static constexpr int kColumns = 40;

    // hq: tiles upscaled 4x with Scale2x and sampled linearly (HQ mode). pack (Enhanced): tiles
    // from <pack>/<tileset>/<tile>.png replace the originals; the atlas is built at the pack's
    // scale and the other tiles are upscaled to match.
    TileAtlas(SDL_Renderer* renderer, const formats::R16File& tiles, bool hq = false, const ArtPack* pack = nullptr,
              const std::string& tileset = {});
    ~TileAtlas();
    TileAtlas(const TileAtlas&) = delete;
    TileAtlas& operator=(const TileAtlas&) = delete;

    SDL_Texture* texture() const { return texture_; }
    int tileCount() const { return count_; }
    bool hq() const { return scale_ > 1; }
    int scale() const { return scale_; }
    int packTiles() const { return packTiles_; }
    // Source rectangle (inside the padding) of a tile in the atlas texture.
    SDL_FRect sourceRect(int tile) const;
    // Mip-aware variants: the half-resolution atlas when the full one would be minified > 2x.
    SDL_Texture* texture(float zoom) const { return useHalf(zoom) ? half_ : texture_; }
    SDL_FRect sourceRect(int tile, float zoom) const;
    // Original-resolution tile pixel (RGBA8 decoded from RGB555), for effects that sample the
    // terrain colour (sandworm particles, FUN_0045d650). Out of range -> opaque black.
    formats::Rgba pixel(int tile, int x, int y) const;

private:
    std::vector<formats::Rgba> pixels_;  // count_ x 32 x 32
    SDL_Texture* texture_ = nullptr;
    SDL_Texture* half_ = nullptr;  // 2x box downsample (scale >= 4)
    int count_ = 0;
    int scale_ = 1;
    int packTiles_ = 0;
    bool useHalf(float zoom) const { return half_ && float(scale_) > 2.0f * zoom + 1e-3f; }
    SDL_FRect rectAt(int tile, float scale) const;
};

}  // namespace d2k::render
