#include "render/minimap.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace d2k::render {

Minimap::Minimap(SDL_Renderer* r, const formats::R16File& tiles, const formats::HouseColours& colours)
    : r_(r), colours_(colours) {
    tileColour_.resize(tiles.frames.size(), 0xFF000000u);
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
        tileColour_[i] = 0xFF000000u | std::uint32_t(b / n) << 16 | std::uint32_t(g / n) << 8 | std::uint32_t(r / n);
    }
}

Minimap::~Minimap() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void Minimap::rebuild(const game::World& w, int side) {
    if (!tex_ || w_ != w.width() || h_ != w.height()) {
        if (tex_) SDL_DestroyTexture(tex_);
        w_ = w.width();
        h_ = w.height();
        tex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w_, h_);
        SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_NEAREST);
    }
    std::vector<formats::Rgba> px(static_cast<std::size_t>(w_) * h_);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const int t = w.cell(x, y).tile & 0x0FFF;
            px[static_cast<std::size_t>(y) * w_ + x] =
                (w.cell(x, y).shroud & 0xF) == 1 ? 0xFF000000u
                : t < static_cast<int>(tileColour_.size()) ? tileColour_[t] : 0xFF000000u;
        }
    auto sideColour = [&](int s) {
        int house = s >= 0 && s < int(w.sides().size()) ? w.sides()[s].house : 0;
        if (house < 0 || house >= formats::kHouseCount) house = 0;
        return colours_[house][8];  // FUN_00461fb0: COLOURS.BIN shade 8
    };
    // Buildings: every unshrouded cell the building's flags mark (the original's cell owner).
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const auto& c = w.cell(x, y);
            if ((c.shroud & 0xF) == 1 || !(c.flags & game::cell_flags::kBuilding)) continue;
            px[static_cast<std::size_t>(y) * w_ + x] = sideColour(int(c.flags & game::cell_flags::kOwnerMask));
        }
    // Units: on fully revealed cells; enemy cloaked units hidden; flyers only ornithopters.
    const formats::Rgba worm = 0xFF000000u | (0xa5u << 16) | (0xb5u << 8) | 0xc6u;
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& sd = w.sides()[s];
        const formats::Rgba col = sideColour(s);
        for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) {
            const auto& u = sd.units[i];
            const int x = u.tileX(), y = u.tileY();
            if (!u.alive || x < 0 || y < 0 || x >= w_ || y >= h_ || (w.cell(x, y).shroud & 0xF) != 0) continue;
            if (s != side && (u.flags & game::unit_flags::kCloaked)) continue;
            const int beh = w.unitTemplate(u).specialBehavior;
            if (beh == game::behaviour::kSandworm) px[static_cast<std::size_t>(y) * w_ + x] = worm;
            else if (!(u.flags & game::unit_flags::kFlying) || beh == game::behaviour::kOrnithopter)
                px[static_cast<std::size_t>(y) * w_ + x] = col;
        }
    }
    SDL_UpdateTexture(tex_, nullptr, px.data(), w_ * 4);
}

SDL_FRect Minimap::placement(const SDL_FRect& area) const {
    if (w_ <= 0 || h_ <= 0) return area;
    // Whole texels per tile when at least 1:1 fits (crisp), else the exact fit.
    float s = std::min(area.w / w_, area.h / h_);
    if (s >= 1.0f) s = std::floor(s);
    const float mw = w_ * s, mh = h_ * s;
    return SDL_FRect{std::round(area.x + (area.w - mw) / 2), std::round(area.y + (area.h - mh) / 2), mw, mh};
}

void Minimap::draw(const SDL_FRect& area, const Camera& cam, int viewW, int viewH, float alpha, bool background,
                   const std::vector<SDL_FPoint>* footprint) {
    if (!tex_) return;
    const SDL_FRect dst = placement(area);
    scale_ = dst.w / w_;
    const float mw = dst.w, mh = dst.h;
    dx_ = dst.x;
    dy_ = dst.y;

    if (background) {
        SDL_SetRenderDrawColor(r_, 0, 0, 0, Uint8(200 * alpha));
        SDL_RenderFillRect(r_, &area);
    }
    SDL_SetTextureScaleMode(tex_, scale_ == std::floor(scale_) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_PIXELART);
    SDL_SetTextureAlphaModFloat(tex_, alpha);
    SDL_RenderTexture(r_, tex_, nullptr, &dst);
    SDL_SetTextureAlphaModFloat(tex_, 1.0f);

    // Camera rectangle (world pixels -> minimap pixels: /32 * scale).
    const float k = scale_ / 32.0f;
    SDL_SetRenderDrawColor(r_, 255, 255, 255, Uint8(230 * std::min(1.0f, alpha * 1.3f)));
    if (footprint) {  // 3D: the frustum's ground polygon (kept inside the map)
        if (footprint->size() < 2) return;
        std::vector<SDL_FPoint> pts;
        pts.reserve(footprint->size() + 1);
        for (const SDL_FPoint& p : *footprint)
            pts.push_back({std::clamp(std::round(dx_ + p.x * k), dx_, dx_ + mw - 1),
                           std::clamp(std::round(dy_ + p.y * k), dy_, dy_ + mh - 1)});
        pts.push_back(pts.front());
        SDL_RenderLines(r_, pts.data(), int(pts.size()));
        return;
    }
    SDL_FRect view{std::round(dx_ + cam.x * k), std::round(dy_ + cam.y * k), std::round(viewW / cam.zoom * k),
                   std::round(viewH / cam.zoom * k)};
    view.w = std::min(view.w, mw);
    view.h = std::min(view.h, mh);
    SDL_RenderRect(r_, &view);
}

bool Minimap::screenToWorld(float sx, float sy, float& wx, float& wy) const {
    const float mx = (sx - dx_) / scale_, my = (sy - dy_) / scale_;
    if (mx < 0 || my < 0 || mx >= w_ || my >= h_) return false;
    wx = mx * 32.0f;
    wy = my * 32.0f;
    return true;
}

}  // namespace d2k::render
