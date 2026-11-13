#include "render/soft_shroud.hpp"

#include "game/world.hpp"
#include "render/map_renderer.hpp"
#include "render/sprites.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace d2k::render {

namespace {

constexpr int K = SoftShroud::kTexelsPerCell;
// Gaussian sigma in texels (2 texels = 1 cell) and the kernel radius (3 sigma).
constexpr float kSigma = 2.0f;
constexpr int kRadius = 6;
// Blurred-mask values mapped to black .. full light (see rebuild step 4).
constexpr float kDark = 0.35f, kLight = 0.95f;

struct Kernel {
    float w[2 * kRadius + 1];
    Kernel() {
        float sum = 0;
        for (int i = -kRadius; i <= kRadius; ++i) sum += w[i + kRadius] = std::exp(-0.5f * float(i * i) / (kSigma * kSigma));
        for (float& v : w) v /= sum;
    }
};
const Kernel kKernel;

}  // namespace

SoftShroud::SoftShroud(SDL_Renderer* r, SpriteCache& sprites) : r_(r), sprites_(sprites) {}

SoftShroud::~SoftShroud() {
    if (tex_) SDL_DestroyTexture(tex_);
}

// Brightness (1 clear .. 0 black) of each 16x16 quadrant of the original edge piece for every
// shroud byte: the 8bpp pixel value 0..31 is the darkness subtracted from each 5-bit channel.
void SoftShroud::initQuadrants() {
    quadInit_ = true;
    for (int b = 0; b < 256; ++b) {
        const int low = b & 0xF;
        for (int q = 0; q < 4; ++q) quad_[b][q] = low == 0 ? 1.0f : low == 1 ? 0.0f : 0.5f;
        if (low <= 1) continue;
        const formats::R16Frame* f = sprites_.frame(game::World::kShroudFirstFrame + b);
        if (!f || f->empty() || f->bpp != 8 || f->width < 2 || f->height < 2) continue;
        for (int q = 0; q < 4; ++q) {
            const int qx = q & 1, qy = q >> 1;
            const int x0 = qx * f->width / 2, x1 = (qx + 1) * f->width / 2;
            const int y0 = qy * f->height / 2, y1 = (qy + 1) * f->height / 2;
            double sum = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) sum += std::min<int>(f->pixels[std::size_t(y) * f->width + x], 31);
            quad_[b][q] = float(1.0 - sum / (31.0 * (x1 - x0) * (y1 - y0)));
        }
    }
}

void SoftShroud::sync(const game::World& w) {
    if (!quadInit_) initQuadrants();
    const int cw = w.width(), ch = w.height();
    if (&w != world_ || cw != cw_ || ch != ch_ || !tex_) {
        world_ = &w;
        cw_ = cw;
        ch_ = ch;
        tw_ = cw * K;
        th_ = ch * K;
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = nullptr;
        if (tw_ <= 0 || th_ <= 0) return;
        tex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, tw_, th_);
        if (!tex_) return;
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_LINEAR);
        const std::size_t n = std::size_t(tw_) * th_;
        last_.assign(std::size_t(cw) * ch, 0);
        base_.assign(n, 0);
        hidden_.assign(n, 1);
        tmp_.assign(n, 0);
        blur_.assign(n, 0);
        open_.assign(n, 0);
        px_.assign(n, 0xFF000000u);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) last_[std::size_t(y) * cw + x] = w.cell(x, y).shroud;
        rebuild(w, 0, 0, cw, ch);
        return;
    }
    // Incremental: per 16x16-cell block, the bounding box of the cells whose shroud byte
    // changed (units revealing at opposite ends of the map do not rebuild everything between).
    constexpr int B = 16;
    const int bw = (cw + B - 1) / B, bh = (ch + B - 1) / B;
    struct Box { int x0, y0, x1, y1; };
    std::vector<Box> boxes(std::size_t(bw) * bh, Box{cw, ch, -1, -1});
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            std::uint8_t& l = last_[std::size_t(y) * cw + x];
            const std::uint8_t s = w.cell(x, y).shroud;
            if (s == l) continue;
            l = s;
            Box& b = boxes[std::size_t(y / B) * bw + x / B];
            b.x0 = std::min(b.x0, x);
            b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x);
            b.y1 = std::max(b.y1, y);
        }
    // Blocks are rebuilt one after another, each from the bytes of all changed cells (last_ is
    // complete), so the result equals a full rebuild.
    for (const Box& b : boxes)
        if (b.x1 >= 0) rebuild(w, b.x0, b.y0, b.x1 + 1, b.y1 + 1);
}

void SoftShroud::rebuild(const game::World&, int cx0, int cy0, int cx1, int cy1) {
    const int tw = tw_, th = th_;
    auto cellHidden = [&](int tx, int ty) { return (last_[std::size_t(ty / K) * cw_ + tx / K] & 0xF) == 1; };
    // 1. Base brightness of the changed cells' texels.
    for (int cy = cy0; cy < cy1; ++cy)
        for (int cx = cx0; cx < cx1; ++cx) {
            const float* q = quad_[last_[std::size_t(cy) * cw_ + cx]];
            for (int sy = 0; sy < K; ++sy)
                for (int sx = 0; sx < K; ++sx)
                    base_[std::size_t(cy * K + sy) * tw + cx * K + sx] = q[(sy * 2 / K) * 2 + (sx * 2 / K)];
        }
    const int ux0 = cx0 * K, uy0 = cy0 * K, ux1 = cx1 * K, uy1 = cy1 * K;
    // 2. Fully shrouded cells dilated by one texel: bilinear filtering of the stretched texture
    // then sees only opaque texels anywhere inside such a cell.
    for (int ty = std::max(0, uy0 - 1); ty < std::min(th, uy1 + 1); ++ty)
        for (int tx = std::max(0, ux0 - 1); tx < std::min(tw, ux1 + 1); ++tx) {
            bool h = false;
            for (int dy = -1; dy <= 1 && !h; ++dy)
                for (int dx = -1; dx <= 1 && !h; ++dx) {
                    const int x = tx + dx, y = ty + dy;
                    h = x >= 0 && y >= 0 && x < tw && y < th && cellHidden(x, y);
                }
            hidden_[std::size_t(ty) * tw + tx] = h;
        }
    // 3. Separable gaussian of both masks over the affected region (clamp to edge at the map
    // border): the shape (base brightness) and the distance from fully shrouded cells.
    const int ox0 = std::max(0, ux0 - kRadius - 1), ox1 = std::min(tw, ux1 + kRadius + 1);
    const int oy0 = std::max(0, uy0 - kRadius - 1), oy1 = std::min(th, uy1 + kRadius + 1);
    const int ry0 = std::max(0, oy0 - kRadius), ry1 = std::min(th, oy1 + kRadius);
    auto blur = [&](auto&& in, std::vector<float>& out) {
        for (int y = ry0; y < ry1; ++y)
            for (int x = ox0; x < ox1; ++x) {
                float s = 0;
                for (int i = -kRadius; i <= kRadius; ++i)
                    s += kKernel.w[i + kRadius] * in(std::size_t(y) * tw + std::size_t(std::clamp(x + i, 0, tw - 1)));
                tmp_[std::size_t(y) * tw + x] = s;
            }
        for (int y = oy0; y < oy1; ++y)
            for (int x = ox0; x < ox1; ++x) {
                float s = 0;
                for (int i = -kRadius; i <= kRadius; ++i)
                    s += kKernel.w[i + kRadius] * tmp_[std::size_t(std::clamp(y + i, 0, th - 1)) * tw + x];
                out[std::size_t(y) * tw + x] = s;
            }
    };
    blur([&](std::size_t i) { return base_[i]; }, blur_);
    blur([&](std::size_t i) { return hidden_[i] ? 0.0f : 1.0f; }, open_);
    // 4. Bias into the revealed side. Both blurred masks are about 0.5 on their boundary. The
    // shape ramps from kDark to kLight; the distance cap ramps from 0.5 (the dilated shrouded
    // boundary, black) to kLight, so the gradient never needs a hard cut at a shrouded cell.
    // Texels in or next to fully shrouded cells are black regardless.
    auto ramp = [](float b, float lo, float hi) {
        const float v = std::clamp((b - lo) / (hi - lo), 0.0f, 1.0f);
        return v * v * (3 - 2 * v);
    };
    for (int y = oy0; y < oy1; ++y)
        for (int x = ox0; x < ox1; ++x) {
            const std::size_t i = std::size_t(y) * tw + x;
            const float v = hidden_[i] ? 0.0f : ramp(blur_[i], kDark, kLight) * ramp(open_[i], 0.5f, kLight);
            const auto a = std::uint32_t(std::lround((1.0f - v) * 255.0f));
            px_[i] = a << 24;  // black, alpha = darkness
        }
    const SDL_Rect rect{ox0, oy0, ox1 - ox0, oy1 - oy0};
    if (rect.w > 0 && rect.h > 0) SDL_UpdateTexture(tex_, &rect, &px_[std::size_t(oy0) * tw + ox0], tw * 4);
}

void SoftShroud::draw(const game::World& w, const Camera& cam, int, int) {
    sync(w);
    if (!tex_) return;
    // The same origin snapping as the terrain (MapRenderer::drawTerrain), so the mask cannot
    // drift against the tiles at any zoom or fractional camera position.
    const double z = cam.zoom;
    const double ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    const SDL_FRect dst{float(-ox), float(-oy), float(std::round(cw_ * 32 * z)), float(std::round(ch_ * 32 * z))};
    SDL_RenderTexture(r_, tex_, nullptr, &dst);
}

}  // namespace d2k::render
