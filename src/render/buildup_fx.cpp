// 2D rubble build-up of redesigned buildings (render/buildup_fx.hpp, docs/enhanced-graphics.md "Build-up").
#include "render/buildup_fx.hpp"

#include <SDL3/SDL.h>

#include <cmath>

namespace d2k::render {

namespace {

std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
float unit(std::uint32_t h) { return float(h & 0xFFFFFF) / float(0xFFFFFF); }  // 0..1
float hash01(std::uint32_t seed, std::uint32_t i, std::uint32_t k) { return unit(mix(seed * 0x9E3779B1u ^ mix(i * 0x85EBCA77u + k))); }
float smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// Value noise in -1..1 along x, lattice spacing `cell`.
float noise1(std::uint32_t seed, float x, float cell) {
    const float fx = x / cell, fl = std::floor(fx), t = fx - fl;
    const auto i = static_cast<std::uint32_t>(static_cast<std::int32_t>(fl));
    const float a = hash01(seed, i, 7) * 2 - 1, b = hash01(seed, i + 1, 7) * 2 - 1;
    return a + (b - a) * (t * t * (3 - 2 * t));
}

}  // namespace

float buildupJag(std::uint32_t seed, float worldX, float reveal) {
    const float env = std::clamp((1.0f - reveal) * 6.0f, 0.0f, 1.0f);
    if (env <= 0) return 0;
    float n = 0.65f * noise1(seed, worldX, 9.0f) + 0.35f * noise1(seed ^ 0x5bd1e995u, worldX, 3.5f);
    n = std::copysign(std::pow(std::fabs(n), 0.7f), n);  // sharper breaks
    return n * 5.0f * env;
}

BuildupFootprint buildupFootprint(const formats::BuildingTemplate& t, int tileX, int tileY) {
    // The cells the structure stands on (not the walkable apron in front of it).
    const std::uint32_t m = t.tilesOccupiedSolid ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
    int c0 = 4, c1 = -1, r0 = 4, r1 = -1;
    for (int q = 0; q < 16; ++q)
        if ((m >> q) & 1) {
            c0 = std::min(c0, q % 4);
            c1 = std::max(c1, q % 4);
            r0 = std::min(r0, q / 4);
            r1 = std::max(r1, q / 4);
        }
    if (c1 < 0) c0 = r0 = c1 = r1 = 0;
    return {float((tileX + c0) * 32), float((tileY + r0) * 32), float((tileX + c1 + 1) * 32), float((tileY + r1 + 1) * 32)};
}

float buildupHeapHeight(const BuildupFootprint& f, float artHeight) {
    const float W = f.x1 - f.x0, H = f.y1 - f.y0;
    return std::clamp(std::min(artHeight * 0.35f, 6.0f + 0.2f * std::min(W, H)), 6.0f, 26.0f);
}

float buildupStain(float p) { return smooth(0.0f, 0.08f, p) * (1.0f - smooth(0.80f, 1.0f, p)); }

void buildupRocks(std::uint32_t seed, const BuildupFootprint& f, float artHeight, float p, float cutY,
                  std::vector<BuildupRock>& out) {
    out.clear();
    const float W = f.x1 - f.x0, H = f.y1 - f.y0;
    if (W <= 0 || H <= 0) return;
    // A packed heap: about 30 stones per cell, spread evenly (a Halton sequence, jittered).
    const int n = std::clamp(int(W * H / (32.0f * 32.0f) * 30.0f), 30, 300);
    // The heap's height: up to about 45% of the art (a low mound under small buildings).
    const float hMax = buildupHeapHeight(f, artHeight);
    constexpr float kPop = 0.05f, kClear = 0.06f;
    struct Key {
        float gy;
        BuildupRock r;
    };
    static thread_local std::vector<Key> keys;
    keys.clear();
    auto halton = [](std::uint32_t i, std::uint32_t base) {
        float r = 0, fct = 1.0f / float(base);
        for (; i > 0; i /= base, fct /= float(base)) r += fct * float(i % base);
        return r;
    };
    for (int j = 0; j < n; ++j) {
        const auto J = std::uint32_t(j);
        const float a = hash01(seed, J, 3), b = hash01(seed, J, 4), c = hash01(seed, J, 5), d = hash01(seed, J, 6);
        const float u = std::clamp(halton(J + 1 + (seed & 63), 2) + (hash01(seed, J, 1) - 0.5f) * 0.06f, 0.0f, 1.0f);
        const float v = std::clamp(halton(J + 1 + (seed & 63), 3) + (hash01(seed, J, 2) - 0.5f) * 0.06f, 0.0f, 1.0f);
        const float du = 2 * u - 1, dv = 2 * v - 1;
        const float m = std::max(0.0f, 1.0f - (du * du + dv * dv) * 0.75f);  // the mound, low at the corners
        const float zRel = std::pow(m, 0.8f) * (0.82f + 0.18f * c);
        float z = hMax * zRel;
        // Boulders in the middle, smaller stones and gravel at the foot.
        float size = (6.0f + 7.0f * a * a) * (0.7f + 0.55f * m) + (a > 0.93f ? 4.0f : 0.0f);
        // Piling up: the low rocks first, the top of the heap last.
        const float tA = 0.015f + 0.30f * std::clamp(0.7f * zRel + 0.3f * b, 0.0f, 1.0f);
        const float tp = (p - tA) / kPop;
        if (tp <= 0) continue;
        const float e = std::min(1.0f, tp);
        float alpha = std::min(1.0f, tp * 2.5f);
        float y = -(1 - e) * (1 - e) * 7.0f;  // dropping in
        // Clearing: the top of the heap tumbles down first, the low rocks at the foot last (they sink).
        const float tC = 0.66f + 0.28f * std::clamp(0.6f * (1 - zRel) + 0.4f * d, 0.0f, 1.0f);
        const float k = std::clamp((p - tC) / kClear, 0.0f, 1.0f);
        if (k >= 1) continue;
        z *= 1 - k;
        y += k * size * 0.55f;
        alpha *= 1 - k * k;
        size *= 1 - 0.3f * k;
        const float gx = f.x0 + 2 + u * (W - 4), gy = f.y0 + 6 + v * (H - 3);
        // The rising structure pushes the rubble off: a rock the cut line has passed slides down and
        // away, except at the foot of the walls (the front band and the sides), where it lies until the end.
        const bool foot = v > 0.8f || u < 0.08f || u > 0.92f;
        const float top = gy - z - size * 0.8f;
        const float s = std::clamp((top - cutY) / 10.0f, 0.0f, 1.0f);
        if (!foot) {
            if (s >= 1) continue;
            y += s * (8.0f + 6.0f * d);
            z *= 1 - 0.5f * s;
            alpha *= 1 - s;
        } else {  // the foot's rocks settle into a low ridge
            z *= 1 - 0.6f * s;
            size *= 1 - 0.2f * s;
        }
        BuildupRock r;
        r.x = gx;
        r.y = gy - z + y;
        r.size = size;
        r.alpha = alpha;
        r.shape = int(mix(seed + J * 31u) % kBuildupRockShapes);
        r.shade = 0.5f + 0.28f * c + 0.16f * zRel;  // higher rocks catch more light
        keys.push_back({gy, r});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& l, const Key& r) { return l.gy < r.gy; });
    for (const Key& k : keys) out.push_back(k.r);
}

SDL_Texture* makeBuildupRockTexture(SDL_Renderer* r) {
    constexpr int C = 32, Cols = 4, Rows = 2, Wt = C * Cols, Ht = C * Rows;
    std::vector<std::uint32_t> px(std::size_t(Wt) * Ht, 0);
    const float L[3] = {-0.48f, -0.62f, 0.62f};  // light from the top left, above (like the art's sun)
    for (int s = 0; s < Cols * Rows; ++s) {
        const auto S = std::uint32_t(s);
        // Outline: 7..9 corners at random radii (a broken stone), flattened by the oblique view.
        const int nv = 4 + int(hash01(77, S, 0) * 3);
        float rad[10], ang[10];
        for (int i = 0; i < nv; ++i) {
            ang[i] = (float(i) + 0.35f * (hash01(77, S, 10 + i) - 0.5f)) / float(nv) * 6.2831853f;
            rad[i] = 0.6f + 0.4f * hash01(77, S, 30 + i);
        }
        const float rx = 13.5f, ry = 10.5f + 2.0f * hash01(77, S, 50), cx = 16, cy = 31 - ry - 0.5f;
        const float tiltX = (hash01(77, S, 51) - 0.5f) * 0.5f, tiltY = (hash01(77, S, 52) - 0.5f) * 0.4f;
        for (int y = 0; y < C; ++y)
            for (int x = 0; x < C; ++x) {
                const float dx = (float(x) + 0.5f - cx) / rx, dy = (float(y) + 0.5f - cy) / ry;
                const float d = std::sqrt(dx * dx + dy * dy);
                float th = std::atan2(dy, dx);
                if (th < 0) th += 6.2831853f;
                // The outline radius at this angle (linear between the corners) and the facet.
                int i0 = nv - 1;
                for (int i = 0; i < nv; ++i)
                    if (ang[i] <= th) i0 = i;
                const int i1 = (i0 + 1) % nv;
                float a0 = ang[i0], a1 = ang[i1];
                if (a1 <= a0) a1 += 6.2831853f;
                float tt = th;
                if (tt < a0) tt += 6.2831853f;
                const float f = std::clamp((tt - a0) / (a1 - a0), 0.0f, 1.0f);
                const float R = rad[i0] + (rad[i1] - rad[i0]) * f;
                const float cov = std::clamp((R - d) * rx * 0.9f, 0.0f, 1.0f);
                if (cov <= 0) continue;
                // A faceted dome: the facet's own direction (its corner pair) for the side slope, a flat top.
                const float q = std::clamp(d / R, 0.0f, 1.0f);
                const float fa = (a0 + a1) * 0.5f;
                const float slope = q < 0.4f ? 0.1f : 0.1f + (q - 0.4f) * 2.6f;  // a flat top, steep facets
                float nx = std::cos(fa) * slope + tiltX, ny = std::sin(fa) * slope + tiltY, nz = 1.0f;
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= nl;
                ny /= nl;
                nz /= nl;
                const float lam = std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]);
                float v = 0.38f + 0.72f * lam;
                v += (hash01(91, S * 4096 + std::uint32_t(y * C + x), 0) - 0.5f) * 0.10f;  // grain
                if (dy > 0.35f) v *= 1.0f - 0.35f * std::clamp((dy - 0.35f) / 0.65f, 0.0f, 1.0f) * q;  // contact shade
                if (q > 0.86f) v *= 0.82f;  // dark rim
                const auto g = std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                const auto A = std::uint32_t(cov * 255.0f + 0.5f);
                const int X = (s % Cols) * C + x, Y = (s / Cols) * C + y;
                px[std::size_t(Y) * Wt + std::size_t(X)] = (A << 24) | (g << 16) | (g << 8) | g;
            }
    }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, Wt, Ht);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), Wt * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

SDL_Texture* makeBuildupPuffTexture(SDL_Renderer* r) {
    constexpr int N = 48;
    std::vector<std::uint32_t> px(std::size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (float(x) + 0.5f) / (N * 0.5f) - 1, dy = (float(y) + 0.5f) / (N * 0.5f) - 1;
            const float d2 = dx * dx + dy * dy;
            float a = std::exp(-d2 / 0.2f) * std::clamp((1.0f - std::sqrt(d2)) * 5.0f, 0.0f, 1.0f);
            const auto A = std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[std::size_t(y) * N + std::size_t(x)] = (A << 24) | 0xFFFFFFu;
        }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, N, N);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), N * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

}  // namespace d2k::render
