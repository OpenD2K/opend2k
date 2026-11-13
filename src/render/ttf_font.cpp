#include "render/ttf_font.hpp"

#include "formats/language.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#include "../../third_party/stb/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace d2k::render {

namespace {
constexpr int kAtlasSize = 1024;

}  // namespace

struct TtfFont::Info {
    stbtt_fontinfo f;
};

TtfFont::TtfFont(SDL_Renderer* r, const unsigned char* data, std::size_t size) : r_(r), info_(std::make_unique<Info>()) {
    ok_ = data && size > 0 && stbtt_InitFont(&info_->f, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
}

TtfFont::~TtfFont() {
    for (auto& [px, a] : atlases_)
        if (a.tex) SDL_DestroyTexture(a.tex);
}

std::vector<std::uint32_t> TtfFont::decode(std::string_view s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    // Text that is not valid UTF-8 as a whole is a game string in its code page.
    bool utf8 = true;
    for (std::size_t i = 0; i < s.size() && utf8;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        const int n = (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + std::size_t(n) >= s.size()) { utf8 = false; break; }
        for (int k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + std::size_t(k)]) & 0xC0) != 0x80) utf8 = false;
        i += std::size_t(n) + 1;
    }
    // Game strings are code page bytes (CP1251 for the Russian text, else CP1252).
    std::string conv;
    if (!utf8) {
        conv = formats::lang::toUtf8(std::string(s), formats::lang::activeCodepage());
        s = conv;
    }
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out.push_back(c); ++i; continue; }
        const int n = (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : 3;
        std::uint32_t cp = c & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
        for (int k = 1; k <= n; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += std::size_t(n) + 1;
    }
    return out;
}

TtfFont::Atlas& TtfFont::atlas(int px) {
    auto it = atlases_.find(px);
    if (it != atlases_.end()) return it->second;
    Atlas a;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info_->f, &asc, &desc, &gap);
    a.scale = float(px) / float(asc - desc);
    a.ascent = std::round(asc * a.scale);
    a.tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, kAtlasSize, kAtlasSize);
    if (a.tex) {
        std::vector<std::uint32_t> clear(std::size_t(kAtlasSize) * kAtlasSize, 0x00FFFFFFu);
        SDL_UpdateTexture(a.tex, nullptr, clear.data(), kAtlasSize * 4);
        SDL_SetTextureBlendMode(a.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(a.tex, SDL_SCALEMODE_NEAREST);
    }
    return atlases_.emplace(px, std::move(a)).first->second;
}

const TtfFont::Glyph* TtfFont::glyph(Atlas& a, int px, std::uint32_t cp) {
    (void)px;
    if (auto it = a.glyphs.find(cp); it != a.glyphs.end()) return &it->second;
    Glyph g;
    int idx = stbtt_FindGlyphIndex(&info_->f, int(cp));
    if (idx == 0 && cp != ' ') idx = stbtt_FindGlyphIndex(&info_->f, '?');
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&info_->f, idx, &adv, &lsb);
    g.adv = adv * a.scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&info_->f, idx, a.scale, a.scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0 && a.tex) {
        if (a.penX + w + 1 > kAtlasSize) {
            a.penX = 1;
            a.penY += a.rowH + 1;
            a.rowH = 0;
        }
        if (a.penY + h + 1 <= kAtlasSize) {
            std::vector<unsigned char> cov(std::size_t(w) * h);
            stbtt_MakeGlyphBitmap(&info_->f, cov.data(), w, h, w, a.scale, a.scale, idx);
            std::vector<std::uint32_t> rgba(cov.size());
            for (std::size_t i = 0; i < cov.size(); ++i) rgba[i] = std::uint32_t(cov[i]) << 24 | 0x00FFFFFFu;
            const SDL_Rect rc{a.penX, a.penY, w, h};
            SDL_UpdateTexture(a.tex, &rc, rgba.data(), w * 4);
            g.x = a.penX;
            g.y = a.penY;
            g.w = w;
            g.h = h;
            g.xoff = float(x0);
            g.yoff = float(y0);
            a.penX += w + 1;
            a.rowH = std::max(a.rowH, h);
        }
    }
    return &a.glyphs.emplace(cp, g).first->second;
}

float TtfFont::width(std::string_view s, float pxf) {
    if (!ok_) return 0;
    const int px = std::max(4, int(pxf + 0.5f));
    Atlas& a = atlas(px);
    float w = 0;
    std::uint32_t prev = 0;
    for (std::uint32_t cp : decode(s)) {
        if (prev) w += stbtt_GetCodepointKernAdvance(&info_->f, int(prev), int(cp)) * a.scale;
        w += glyph(a, px, cp)->adv;
        prev = cp;
    }
    return std::ceil(w);
}

float TtfFont::draw(std::string_view s, float x, float y, float pxf, Colour c) {
    if (!ok_) return 0;
    const int px = std::max(4, int(pxf + 0.5f));
    Atlas& a = atlas(px);
    if (!a.tex) return 0;
    SDL_SetTextureColorMod(a.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(a.tex, c.a);
    float pen = std::round(x);
    const float base = std::round(y) + a.ascent;
    std::uint32_t prev = 0;
    for (std::uint32_t cp : decode(s)) {
        if (prev) pen += stbtt_GetCodepointKernAdvance(&info_->f, int(prev), int(cp)) * a.scale;
        const Glyph* g = glyph(a, px, cp);
        if (g->w > 0) {
            const SDL_FRect src{float(g->x), float(g->y), float(g->w), float(g->h)};
            const SDL_FRect dst{std::round(pen + g->xoff), base + g->yoff, float(g->w), float(g->h)};
            SDL_RenderTexture(r_, a.tex, &src, &dst);
        }
        pen += g->adv;
        prev = cp;
    }
    SDL_SetTextureColorMod(a.tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(a.tex, 255);
    return pen - std::round(x);
}

}  // namespace d2k::render
