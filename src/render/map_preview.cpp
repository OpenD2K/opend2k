#include "render/map_preview.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace d2k::render {

MapPreview::~MapPreview() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void MapPreview::build(const formats::MapFile& map, const std::string& tilesetPath) {
    auto it = tileColours_.find(tilesetPath);
    if (it == tileColours_.end()) {
        std::vector<formats::Rgba> colours;
        try {
            const auto tiles = formats::loadR16(tilesetPath);
            colours.resize(tiles.frames.size(), 0xFF000000u);
            for (std::size_t i = 0; i < tiles.frames.size(); ++i) {
                const auto px = formats::decodeFrame(tiles, tiles.frames[i], false);
                if (px.empty()) continue;
                std::uint64_t r = 0, g = 0, b = 0;
                for (auto c : px) {
                    r += c & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += (c >> 16) & 0xFF;
                }
                const auto n = px.size();
                colours[i] = 0xFF000000u | std::uint32_t(b / n) << 16 | std::uint32_t(g / n) << 8 | std::uint32_t(r / n);
            }
        } catch (const std::exception& e) {
            SDL_Log("Map preview: %s", e.what());
        }
        it = tileColours_.emplace(tilesetPath, std::move(colours)).first;
    }
    const auto& colours = it->second;
    if (!tex_ || w_ != map.width || h_ != map.height) {
        if (tex_) SDL_DestroyTexture(tex_);
        w_ = map.width;
        h_ = map.height;
        tex_ = w_ > 0 && h_ > 0 ? SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w_, h_) : nullptr;
        if (tex_) SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_NEAREST);
    }
    spawns_.clear();
    std::vector<formats::Rgba> px(static_cast<std::size_t>(w_) * h_, 0xFF000000u);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const auto& c = map.at(x, y);
            const int t = c.tile & 0x0FFF;
            formats::Rgba v = t < static_cast<int>(colours.size()) ? colours[t] : 0xFF000000u;
            if (c.special == 1) v = 0xFF2F7FD8u;       // thin spice (orange)
            else if (c.special == 2) v = 0xFF1F5FB0u;  // thick spice
            else if (c.special == 0x17) spawns_.push_back({x, y});
            px[static_cast<std::size_t>(y) * w_ + x] = v;
        }
    if (tex_) SDL_UpdateTexture(tex_, nullptr, px.data(), w_ * 4);
}

void MapPreview::draw(const SDL_FRect& area) const {
    if (!tex_) return;
    const float s = std::min(area.w / w_, area.h / h_);
    const float mw = w_ * s, mh = h_ * s;
    const SDL_FRect dst{std::round(area.x + (area.w - mw) / 2), std::round(area.y + (area.h - mh) / 2), mw, mh};
    SDL_RenderTexture(r_, tex_, nullptr, &dst);
    const float d = std::max(4.0f, s * 3);
    for (const auto& sp : spawns_) {
        const float cx = dst.x + (sp[0] + 0.5f) * s, cy = dst.y + (sp[1] + 0.5f) * s;
        const SDL_FRect o{cx - d / 2 - 1, cy - d / 2 - 1, d + 2, d + 2};
        SDL_SetRenderDrawColor(r_, 0, 0, 0, 220);
        SDL_RenderFillRect(r_, &o);
        const SDL_FRect in{cx - d / 2, cy - d / 2, d, d};
        SDL_SetRenderDrawColor(r_, 255, 230, 120, 255);
        SDL_RenderFillRect(r_, &in);
    }
}

}  // namespace d2k::render
