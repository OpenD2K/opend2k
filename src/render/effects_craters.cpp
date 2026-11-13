// Modern craters (Effects: Modern + Designed terrain, 2D and 3D): the terrain damage the
// simulation keeps per cell (cell +9, DamageTiles; the original draws crater sprite
// (damage >> 4) + 16 * ((x + y) & 1) of the rock / sand set once it is above 0x10, see
// MapRenderer::drawDecals) drawn as a soft, lit dent instead of the pixel-art sprite: same cells,
// same moment, size growing with the damage level. Two procedural layers per variant:
//   row 0 "ground": grey shading x the cell's material colour (dent walls lit by the sun from
//         the north-west like the baked terrain, a darker disturbed floor), alpha blended;
//   row 1 "overlay": pale dust (the blown-out rim, ejecta rays, light grains) under dark soot
//         (the charred centre, soot rays, dark debris specks).
// Variants 0-3 are sand craters (wide soft ejecta), 4-7 rock craters (smaller rim, rough
// chipped walls, more chunks). Purely presentational.

#include "render/effects.hpp"

#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"
#include "render/tileset.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace d2k::render {

using namespace fx;

namespace {

constexpr int kCraterCell = 128;
constexpr int kCraterVariants = 8;

float vnoise(float x, float y, std::uint32_t seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - float(xi), fy = y - float(yi);
    auto h = [&](int a, int b) {
        return float(hash32(std::uint32_t(a) * 73856093u ^ std::uint32_t(b) * 19349663u ^ seed) & 0xFFFF) / 65535.0f;
    };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

float fbm(float x, float y, std::uint32_t seed) {
    return 0.55f * vnoise(x, y, seed) + 0.3f * vnoise(x * 2.1f, y * 2.1f, seed + 17) +
           0.15f * vnoise(x * 4.3f, y * 4.3f, seed + 41);
}

}  // namespace

void Effects::buildCraterTexture() {
    constexpr int N = kCraterCell, W = N * kCraterVariants, H = N * 2;
    std::vector<std::uint32_t> px(std::size_t(W) * H, 0);
    std::vector<float> hgt(std::size_t(N) * N), dd(std::size_t(N) * N), ang(std::size_t(N) * N);
    const float Lx = -0.52f, Ly = -0.52f, Lz = 0.68f;  // sun from the north-west (image y down)
    for (int v = 0; v < kCraterVariants; ++v) {
        const bool rock = v >= 4;
        const std::uint32_t seed = 0xC0A7E5u + std::uint32_t(v) * 7919u;
        const float ph = hashf(seed, 1) * 6.28f;
        // Height field: bowl, rim, ejecta, roughness.
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = (float(x) + 0.5f) / N * 2 - 1, w = (float(y) + 0.5f) / N * 2 - 1;
                // Irregular outline by domain warping (no angular terms: those make fans in the bowl).
                const float wa = rock ? 0.2f : 0.13f;
                const float wu = u + wa * (fbm(u * 1.8f + 3, w * 1.8f + 7, seed) - 0.5f) * 2 + 0.025f * std::sin(ph);
                const float ww = w + wa * (fbm(u * 1.8f + 13, w * 1.8f + 1, seed + 1) - 0.5f) * 2 + 0.025f * std::cos(ph);
                const float a = std::atan2(w, u);
                const float R = rock ? 0.36f : 0.42f;
                const float d = std::sqrt(wu * wu + ww * ww) / R;
                float h = 0;
                const float depth = rock ? 0.5f : 0.62f;
                if (d < 1) h -= depth * (1 - d * d) * (1 - d * d);
                const float rw = rock ? 0.26f : 0.34f;
                h += (rock ? 0.1f : 0.14f) * std::exp(-((d - 0.95f) / rw) * ((d - 0.95f) / rw));
                if (d > 1) h += (rock ? 0.03f : 0.06f) * std::exp(-(d - 1) * 2.2f) * fbm(std::cos(a) * 3 + d, std::sin(a) * 3, seed + 3);
                h += (rock ? 0.07f : 0.025f) * (fbm(u * 9, w * 9, seed + 7) - 0.5f) * std::clamp(1.6f - d * 0.5f, 0.0f, 1.0f);
                hgt[std::size_t(y) * N + x] = h;
                dd[std::size_t(y) * N + x] = d;
                ang[std::size_t(y) * N + x] = a;
            }
        // Debris specks (light grains in the ground layer, dark chunks in the soot layer).
        struct Speck { float x, y, r; bool dark; };
        std::vector<Speck> specks;
        const int nSpecks = rock ? 16 : 10;
        for (int k = 0; k < nSpecks; ++k) {
            const float a = hashf(seed, 100 + k) * 6.28f;
            const float rr = (rock ? 0.4f : 0.5f) + hashf(seed, 200 + k) * (rock ? 0.42f : 0.38f);
            specks.push_back({(std::cos(a) * rr + 1) * 0.5f * N, (std::sin(a) * rr + 1) * 0.5f * N,
                              0.9f + hashf(seed, 300 + k) * (rock ? 1.6f : 1.0f), hashf(seed, 400 + k) < (rock ? 0.6f : 0.45f)});
        }
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const std::size_t i = std::size_t(y) * N + x;
                auto H_ = [&](int xx, int yy) {
                    return hgt[std::size_t(std::clamp(yy, 0, N - 1)) * N + std::size_t(std::clamp(xx, 0, N - 1))];
                };
                const float k = 15.0f;  // slope scale
                float nx = -(H_(x + 1, y) - H_(x - 1, y)) * k, ny = -(H_(x, y + 1) - H_(x, y - 1)) * k, nz = 1;
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= nl;
                ny /= nl;
                nz /= nl;
                const float shade = std::max(0.0f, nx * Lx + ny * Ly + nz * Lz) / Lz;
                const float d = dd[i], a = ang[i];
                const float u = (float(x) + 0.5f) / N * 2 - 1, w = (float(y) + 0.5f) / N * 2 - 1;
                const float r = std::sqrt(u * u + w * w);
                // Albedo: disturbed darker floor, blown-out lighter rim / ejecta.
                const float rf = rock ? 2.4f : 3.6f;  // rays: noise around the circle (no seam), stretched radially
                const float rays = fbm(std::cos(a) * rf + 11 + d * 0.25f, std::sin(a) * rf + 4, seed + 9);
                // Ground layer: the dent's shading on the material colour (1 = undisturbed
                // ground lit like the terrain; shadowed walls and the disturbed floor darker).
                float alb = d < 1 ? 0.84f + 0.16f * d * d : 1.0f;
                alb *= 0.93f + 0.1f * fbm(u * 14, w * 14, seed + 13);
                float grey = std::min(1.0f, alb * (0.42f + 0.58f * shade));
                float ga = d < 1.0f ? 1.0f : smooth01(1.35f, 1.0f, d);
                ga *= smooth01(1.0f, 0.86f, r);
                // Dust: the blown-out lighter rim and ejecta rays (pale, fading out in rays).
                float da = (rock ? 0.3f : 0.42f) * smooth01(0.9f, 1.05f, d) * smooth01(rock ? 1.9f : 2.3f, 1.05f, d) *
                           (0.3f + 0.7f * rays);
                da = std::max(da, 0.25f * std::max(0.0f, shade - 1.05f) * (d < 1.05f ? 1.0f : 0.0f));  // lit inner wall
                // Soot: charred, blotchy centre, soot rays.
                const float blot = fbm(u * 5 + 3, w * 5 + 1, seed + 21);
                float sa = 0.78f * (1 - smooth01(0.1f, 0.6f + 0.55f * blot, d));
                sa = std::max(sa, (rock ? 0.24f : 0.2f) * smooth01(2.0f, 1.25f, d) * smooth01(1.05f, 1.3f, d) * smooth01(0.5f, 0.85f, rays));
                sa *= 0.7f + 0.45f * blot;
                for (const Speck& s : specks) {
                    const float q = std::hypot(float(x) + 0.5f - s.x, float(y) + 0.5f - s.y);
                    if (q > s.r + 1) continue;
                    const float m = std::clamp(s.r + 0.5f - q, 0.0f, 1.0f);
                    if (s.dark) sa = std::max(sa, 0.6f * m);
                    else da = std::max(da, 0.8f * m);  // a light grain
                }
                const float edge = smooth01(1.0f, 0.86f, r);
                sa *= edge;
                da *= edge;
                if (x == 0 || y == 0 || x == N - 1 || y == N - 1) ga = sa = da = 0;
                const int G = std::clamp(int(grey * 255 + 0.5f), 0, 255);
                px[std::size_t(y) * W + std::size_t(v) * N + x] = rgba(G, G, G, std::clamp(int(ga * 255 + 0.5f), 0, 255));
                // Overlay layer: soot over dust (dust first, then soot on top, as one texel).
                const float oa = sa + da * (1 - sa);
                float cr = 0, cg = 0, cb = 0;
                if (oa > 1e-4f) {
                    const float wd = da * (1 - sa) / oa, ws = sa / oa;
                    cr = 246 * wd + 46 * ws;
                    cg = 232 * wd + 32 * ws;
                    cb = 204 * wd + 22 * ws;
                }
                px[std::size_t(y + N) * W + std::size_t(v) * N + x] =
                    rgba(int(cr + 0.5f), int(cg + 0.5f), int(cb + 0.5f), std::clamp(int(oa * 255 + 0.5f), 0, 255));
            }
    }
    if (const char* dump = std::getenv("D2K_CRATER_DUMP")) {  // debugging: the atlas as a BMP
        if (SDL_Surface* sf = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_ABGR8888, px.data(), W * 4)) {
            SDL_SaveBMP(sf, dump);
            SDL_DestroySurface(sf);
        }
    }
    craterTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, W, H);
    if (craterTex_) {
        SDL_UpdateTexture(craterTex_, nullptr, px.data(), W * 4);
        SDL_SetTextureScaleMode(craterTex_, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(craterTex_, SDL_BLENDMODE_BLEND);
    }
}

void Effects::craterDecals(const game::World& w, bool showShroud, int x0, int y0, int x1, int y1,
                           std::vector<CraterDecal>& out) const {
    out.clear();
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w.width(), x1);
    y1 = std::min(w.height(), y1);
    const float cu = float(kCraterCell) / float(kCraterCell * kCraterVariants), cv = 0.5f;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto& c = w.cell(x, y);
            if (c.damage <= 0x10) continue;                     // as the original: crater from 0x11
            if (showShroud && (c.shroud & 0xF) == 1) continue;  // the original skips shrouded cells
            bool rock;
            if (c.flags & 0x8000) rock = true;          // rock crater set (BuildOn)
            else if (c.flags & 0x10000) rock = false;   // sand crater set
            else continue;                              // other terrain: none (as the original)
            const std::uint32_t hsh = hash32(std::uint32_t(x) * 92837111u ^ std::uint32_t(y) * 689287499u);
            const int level = c.damage >> 4;  // 1..15
            const float t = std::sqrt(float(level - 1) / 14.0f);
            CraterDecal d;
            d.size = (rock ? 24.0f : 26.0f) + (rock ? 26.0f : 30.0f) * t;
            d.x = float(x * 32 + 16) + (float(hsh & 7) - 3.5f);
            d.y = float(y * 32 + 16) + (float((hsh >> 3) & 7) - 3.5f);
            const int variant = (rock ? 4 : 0) + int((hsh >> 6) & 3);
            d.u0 = float(variant) * cu + 0.5f / float(kCraterCell * kCraterVariants);
            d.u1 = float(variant + 1) * cu - 0.5f / float(kCraterCell * kCraterVariants);
            d.v0 = 0.5f / (2 * kCraterCell);
            d.v1 = cv - 0.5f / (2 * kCraterCell);
            d.sv0 = cv + 0.5f / (2 * kCraterCell);
            d.sv1 = 1 - 0.5f / (2 * kCraterCell);
            // Material colour of the cell (the designed terrain is colour-locked to the tile means).
            std::uint32_t m = rgba(206, 150, 96, 255);
            if (craterAtlas_) {
                int r = 0, g = 0, b = 0;
                for (int k = 0; k < 9; ++k) {
                    const std::uint32_t p = craterAtlas_->pixel(c.tile, 5 + (k % 3) * 11, 5 + (k / 3) * 11);
                    r += int(p & 0xFF);
                    g += int((p >> 8) & 0xFF);
                    b += int((p >> 16) & 0xFF);
                }
                m = rgba(r / 9, g / 9, b / 9, 255);
            }
            {
                // The designed terrain is darker, warmer and more saturated than the tile means
                // (DesignedTerrain::computeTints: sand x1.25 + an orange bias, rock x1.15).
                const float r = float(m & 0xFF), g = float((m >> 8) & 0xFF), b = float((m >> 16) & 0xFF);
                const float l = 0.3f * r + 0.59f * g + 0.11f * b, k = rock ? 1.2f : 1.35f, f = rock ? 0.92f : 0.9f;
                d.tint = rgba(std::clamp(int((l + (r - l) * k) * f), 0, 255), std::clamp(int((l + (g - l) * k) * f), 0, 255),
                              std::clamp(int((l + (b - l) * k) * f), 0, 255), 255);
            }
            d.soot = 0.55f + 0.45f * std::min(1.0f, float(level) / 8.0f);
            out.push_back(d);
        }
}

void Effects::drawCraters(const game::World& w, const TileAtlas& atlas, const Camera& cam, int x0, int y0, int x1,
                          int y1, bool showShroud) {
    if (!craterTex_) return;
    craterAtlas_ = &atlas;
    craterDecals(w, showShroud, x0 - 1, y0 - 1, x1 + 1, y1 + 1, craterScratch_);
    if (craterScratch_.empty()) return;
    const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    std::vector<SDL_Vertex> v;
    v.reserve(craterScratch_.size() * 12);
    auto put = [&](const CraterDecal& d, std::uint32_t c, float a, float u0, float v0, float u1, float v1) {
        const float sx = d.x * z - ox, sy = d.y * z - oy, h = d.size * 0.5f * z;
        const SDL_FColor col{float(c & 0xFF) / 255.0f, float((c >> 8) & 0xFF) / 255.0f, float((c >> 16) & 0xFF) / 255.0f, a};
        const SDL_Vertex p0{{sx - h, sy - h}, col, {u0, v0}}, p1{{sx + h, sy - h}, col, {u1, v0}};
        const SDL_Vertex p2{{sx + h, sy + h}, col, {u1, v1}}, p3{{sx - h, sy + h}, col, {u0, v1}};
        v.insert(v.end(), {p0, p1, p2, p0, p2, p3});
    };
    for (const auto& d : craterScratch_) put(d, d.tint, 1.0f, d.u0, d.v0, d.u1, d.v1);
    for (const auto& d : craterScratch_) put(d, 0xFFFFFFFFu, d.soot, d.u0, d.sv0, d.u1, d.sv1);
    SDL_RenderGeometry(r_, craterTex_, v.data(), int(v.size()), nullptr, 0);
}

}  // namespace d2k::render
