#include "render/text.hpp"

#include "formats/language.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2k::render {

TextRenderer::TextRenderer(SDL_Renderer* r, const formats::Font& font) : r_(r), font_(font) {
    // Simple shelf packing into a 512-wide atlas.
    constexpr int kAtlasW = 512;
    int x = 0, y = 0, rowH = 0;
    rects_.resize(font_.glyphs.size());
    for (std::size_t i = 0; i < font_.glyphs.size(); ++i) {
        const auto& g = font_.glyphs[i];
        if (x + g.width + 1 > kAtlasW) { x = 0; y += rowH + 1; rowH = 0; }
        rects_[i] = {x, y, g.width, g.height};
        x += g.width + 1;
        rowH = std::max(rowH, g.height);
    }
    const int atlasH = std::max(1, y + rowH);
    std::vector<std::uint32_t> px(static_cast<std::size_t>(kAtlasW) * atlasH, 0);
    for (std::size_t i = 0; i < font_.glyphs.size(); ++i) {
        const auto& g = font_.glyphs[i];
        const auto& rc = rects_[i];
        for (int gy = 0; gy < g.height; ++gy)
            for (int gx = 0; gx < g.width; ++gx)
                if (g.pixels[static_cast<std::size_t>(gy) * g.width + gx])
                    px[static_cast<std::size_t>(rc.y + gy) * kAtlasW + rc.x + gx] = 0xFFFFFFFFu;
    }
    atlas_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, kAtlasW, atlasH);
    if (atlas_) {
        SDL_UpdateTexture(atlas_, nullptr, px.data(), kAtlasW * 4);
        SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_NEAREST);
    }
}

TextRenderer::~TextRenderer() {
    if (atlas_) SDL_DestroyTexture(atlas_);
}

float TextRenderer::drawRun(const std::string& text, float x, float y, Colour c, float scale) {
    if (!atlas_) return 0;
    // Whole scales: nearest; fractional ones (Enhanced UI scale): pixel-art filtering, so the
    // bitmap glyphs keep even, sharp pixels.
    SDL_SetTextureScaleMode(atlas_, scale == std::floor(scale) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_PIXELART);
    SDL_SetTextureColorMod(atlas_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(atlas_, c.a);
    const float x0 = x;
    for (unsigned char ch : text) {
        const formats::Glyph* g = font_.glyphFor(ch);
        if (!g) { x += font_.spaceWidth * scale; continue; }
        const auto& rc = rects_[static_cast<std::size_t>(g - font_.glyphs.data())];
        const SDL_FRect src{float(rc.x), float(rc.y), float(rc.w), float(rc.h)};
        const SDL_FRect dst{std::round(x), std::round(y), rc.w * scale, rc.h * scale};
        SDL_RenderTexture(r_, atlas_, &src, &dst);
        x += (g->width + font_.spacing) * scale;
    }
    return x - x0;
}

float TextRenderer::width(const std::string& text, float scale) const {
    return font_.textWidth(formats::lang::forBitmapFont(text, formats::lang::activeCodepage())) * scale;
}

float TextRenderer::draw(const std::string& in, float x, float y, Colour c, float scale, bool shadow) {
    const std::string text = formats::lang::forBitmapFont(in, formats::lang::activeCodepage());
    if (shadow) drawRun(text, x + scale, y + scale, Colour{0, 0, 0, static_cast<unsigned char>(c.a * 3 / 4)}, scale);
    return drawRun(text, x, y, c, scale);
}

}  // namespace d2k::render
