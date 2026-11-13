#include "render/tileset.hpp"

#include "render/art_pack.hpp"
#include "render/upscale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2k::render {

TileAtlas::TileAtlas(SDL_Renderer* renderer, const formats::R16File& tiles, bool hq, const ArtPack* pack,
                     const std::string& tileset)
    : count_(static_cast<int>(tiles.frames.size())), scale_(hq ? 4 : 1) {
    const bool usePack = pack && !tileset.empty() && pack->tileCount(tileset) > 0;
    if (usePack) scale_ = std::clamp(pack->scale(), 1, 8);
    const int rows = (count_ + kColumns - 1) / kColumns;
    const int w = kColumns * kStride, h = rows * kStride;
    std::vector<formats::Rgba> atlas(static_cast<std::size_t>(w) * h, 0xFF000000u);
    pixels_.assign(static_cast<std::size_t>(count_) * kTileSize * kTileSize, 0xFF000000u);

    for (int i = 0; i < count_; ++i) {
        const auto& f = tiles.frames[i];
        // Tiles are opaque terrain: keep colour 0 as black.
        const auto px = formats::decodeFrame(tiles, f, /*zeroIsTransparent=*/false);
        const int ox = (i % kColumns) * kStride + kPad, oy = (i / kColumns) * kStride + kPad;
        for (int y = 0; y < std::min(f.height, kTileSize); ++y)
            for (int x = 0; x < std::min(f.width, kTileSize); ++x)
                pixels_[(static_cast<std::size_t>(i) * kTileSize + y) * kTileSize + x] =
                    px[static_cast<std::size_t>(y) * f.width + x];
        // Write the tile plus a border that repeats its edge pixels (clamped lookup).
        for (int y = -kPad; y < kTileSize + kPad; ++y) {
            const int sy = std::clamp(y, 0, std::min(f.height, kTileSize) - 1);
            for (int x = -kPad; x < kTileSize + kPad; ++x) {
                const int sx = std::clamp(x, 0, std::min(f.width, kTileSize) - 1);
                atlas[static_cast<std::size_t>(oy + y) * w + ox + x] =
                    px[static_cast<std::size_t>(sy) * f.width + sx];
            }
        }
    }

    int tw = w, th = h;
    if (scale_ > 1) {
        // Upscale each padded tile block separately so tiles never bleed into each other.
        const int bs = kStride * scale_;
        std::vector<formats::Rgba> big(static_cast<std::size_t>(w) * h * scale_ * scale_);
        std::vector<formats::Rgba> block(static_cast<std::size_t>(kStride) * kStride);
        for (int i = 0; i < count_; ++i) {
            const int bx = (i % kColumns) * kStride, by = (i / kColumns) * kStride;
            std::vector<formats::Rgba> up;
            ArtImage img;
            if (usePack && pack->loadTile(tileset, i, img)) {
                // Replacement: fit to 32*scale, then extrude the edge texels into the padding.
                const int ts = kTileSize * scale_, pad = kPad * scale_;
                std::vector<formats::Rgba> t = (img.w == ts && img.h == ts) ? img.px : resample(img.px, img.w, img.h, ts, ts);
                up.assign(static_cast<std::size_t>(bs) * bs, 0);
                for (int y = 0; y < bs; ++y)
                    for (int x = 0; x < bs; ++x) {
                        const int sx = std::clamp(x - pad, 0, ts - 1), sy = std::clamp(y - pad, 0, ts - 1);
                        up[static_cast<std::size_t>(y) * bs + x] = t[static_cast<std::size_t>(sy) * ts + sx] | 0xFF000000u;
                    }
                ++packTiles_;
            } else {
                for (int y = 0; y < kStride; ++y)
                    for (int x = 0; x < kStride; ++x) block[y * kStride + x] = atlas[static_cast<std::size_t>(by + y) * w + bx + x];
                if (scale_ == 2) up = scale2x(block, kStride, kStride);
                else if (scale_ == 4) up = scale4x(block, kStride, kStride);
                else if (scale_ == 8) up = scale2x(scale4x(block, kStride, kStride), kStride * 4, kStride * 4);
                else up = resample(scale4x(block, kStride, kStride), kStride * 4, kStride * 4, bs, bs);
            }
            for (int y = 0; y < bs; ++y)
                for (int x = 0; x < bs; ++x)
                    big[static_cast<std::size_t>(by * scale_ + y) * w * scale_ + bx * scale_ + x] = up[static_cast<std::size_t>(y) * bs + x];
        }
        atlas.swap(big);
        tw = w * scale_;
        th = h * scale_;
    }
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, tw, th);
    if (!texture_) throw std::runtime_error(std::string("atlas texture: ") + SDL_GetError());
    SDL_UpdateTexture(texture_, nullptr, atlas.data(), tw * 4);
    SDL_SetTextureScaleMode(texture_, scale_ > 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    if (scale_ >= 4) {
        // One mip level: zoomed-out views sample a 2x box-filtered copy instead of skipping texels.
        int hw = 0, hh = 0;
        const auto small = downsample2(atlas, tw, th, hw, hh);
        half_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, hw, hh);
        if (half_) {
            SDL_UpdateTexture(half_, nullptr, small.data(), hw * 4);
            SDL_SetTextureScaleMode(half_, SDL_SCALEMODE_LINEAR);
        }
    }
    if (packTiles_) SDL_Log("Art pack: %d %s tiles replaced (atlas scale %d)", packTiles_, tileset.c_str(), scale_);
}

TileAtlas::~TileAtlas() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (half_) SDL_DestroyTexture(half_);
}

formats::Rgba TileAtlas::pixel(int tile, int x, int y) const {
    if (tile < 0 || tile >= count_ || x < 0 || y < 0 || x >= kTileSize || y >= kTileSize) return 0xFF000000u;
    return pixels_[(static_cast<std::size_t>(tile) * kTileSize + y) * kTileSize + x];
}

SDL_FRect TileAtlas::rectAt(int tile, float scale) const {
    // Smoothed atlases: inset half a source texel so linear filtering stays inside the tile.
    const float inset = scale_ > 1 ? 0.5f : 0.0f;
    return SDL_FRect{float((tile % kColumns) * kStride + kPad) * scale + inset,
                     float((tile / kColumns) * kStride + kPad) * scale + inset, float(kTileSize) * scale - 2 * inset,
                     float(kTileSize) * scale - 2 * inset};
}

SDL_FRect TileAtlas::sourceRect(int tile) const { return rectAt(tile, float(scale_)); }

SDL_FRect TileAtlas::sourceRect(int tile, float zoom) const {
    return rectAt(tile, useHalf(zoom) ? float(scale_) * 0.5f : float(scale_));
}

}  // namespace d2k::render
