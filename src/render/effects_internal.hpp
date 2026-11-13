#pragma once
// Small helpers shared by the Modern effects sources (render/effects.cpp, render/effects_weapons.cpp).
// Internal: not part of the Effects interface.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace d2k::render::fx {

constexpr float kPi = 3.14159265f;
constexpr int kTexCell = 128;      // procedural particle texture cell (px)
constexpr std::size_t kMaxParticles = 9000;
constexpr float kWindX = 7.0f, kWindY = -2.0f;  // smoke drift (px / s)
// Devastator: its one bullet is a pair of plasma bolts 8 px apart across the flight (DEVBULLET / MUZ_DEVAS art).
constexpr float kPlasmaTwinHalf = 4.0f;

constexpr std::uint32_t rgba(int r, int g, int b, int a) {
    return std::uint32_t(r) | std::uint32_t(g) << 8 | std::uint32_t(b) << 16 | std::uint32_t(a) << 24;
}
inline std::uint32_t withAlpha(std::uint32_t c, int a) { return (c & 0xFFFFFFu) | std::uint32_t(std::clamp(a, 0, 255)) << 24; }
inline std::uint32_t lerpColour(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const float x = float((a >> s) & 0xFF), y = float((b >> s) & 0xFF);
        out |= std::uint32_t(std::clamp(int(x + (y - x) * t + 0.5f), 0, 255)) << s;
    }
    return out;
}
inline std::uint32_t scaleRgb(std::uint32_t c, float k) {
    auto ch = [&](int s) { return std::uint32_t(std::clamp(int(float((c >> s) & 0xFF) * k), 0, 255)) << s; };
    return (c & 0xFF000000u) | ch(0) | ch(8) | ch(16);
}
// Light-blend colour: rgb scaled by alpha * k (the blend ignores alpha), alpha 255.
inline std::uint32_t premultiplied(std::uint32_t c, float k) {
    const float a = float(c >> 24) / 255.0f * k;
    auto ch = [&](int s) { return std::uint32_t(std::clamp(int(float((c >> s) & 0xFF) * a), 0, 255)) << s; };
    return 0xFF000000u | ch(0) | ch(8) | ch(16);
}
inline std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float hashf(std::uint32_t seed, std::uint32_t n) { return float(hash32(seed ^ (n * 0x9E3779B1u)) & 0xFFFF) / 65535.0f; }
inline float smooth01(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

}  // namespace d2k::render::fx
