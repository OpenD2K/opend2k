// Track marks in 3D: vehicle bands and the relief texture (render3d/track_marks.hpp, docs/3d-mode.md
// "Track marks").
#include "render3d/track_marks.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace d2k::render3d {

namespace {

constexpr float kTau = 6.2831853f;
constexpr int kColumn = 40, kPad = 4, kTexH = 64;

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// A soft periodic pulse: 1 in the middle of [0, w) of each unit step of x, easing out over `soft`.
float pulse(float x, float w, float soft) {
    const float f = x - std::floor(x);
    return smooth(0, soft, f) * (1 - smooth(w - soft, w, f));
}

// The imprint's height (units of the groove depth: 0 = untouched ground, -1 = the floor) at s across
// (band half-widths, 0 = the centre line) and t along (0..1 = one period, periodic).
float heightAt(TrackPattern p, float s, float t) {
    // Ragged edges: the edge and the berm wander a little along the trail (periodic in t).
    const float wob = 0.05f * std::sin(kTau * 2 * t + 1.3f * s) + 0.035f * std::sin(kTau * 5 * t + 2.1f + 4.0f * s) +
                      0.02f * std::sin(kTau * 9 * t + 0.4f);
    const float a = std::abs(s) + wob;
    const float wall = 1 - smooth(0.74f, 1.04f, a);  // 1 on the floor
    float berm = std::exp(-((a - 1.17f) * (a - 1.17f)) / (0.17f * 0.17f));
    float h = 0;
    switch (p) {
    case kTrackTread: {
        // Grousers: 4 bars across per period dig slots; the sand between them stands as ridges.
        const float g = pulse(t * 4 + 0.03f * std::abs(s), 0.55f, 0.25f);
        h = -wall * (0.85f - 0.3f * g) + 0.36f * berm;
        break;
    }
    case kTrackTyre: {
        // Chevron lugs either side of a central rib, staggered half a lug between the sides.
        const float side = s < 0 ? 0.5f : 0.0f;
        const float lug = pulse(t * 5 + 0.35f * std::abs(s) + side, 0.45f, 0.12f) * smooth(0.12f, 0.25f, std::abs(s));
        h = -0.75f * wall * (1 - 0.5f * lug) + 0.3f * berm;
        break;
    }
    case kTrackBalloon: {
        // A rounded floor (a soft tyre), broad diagonal lugs.
        const float bowl = 1 - std::min(1.0f, (s / 0.95f) * (s / 0.95f));
        const float lug = pulse(t * 3 + 0.4f * s, 0.5f, 0.15f);
        h = -wall * (0.45f + 0.55f * bowl) * (1 - 0.35f * lug) + 0.32f * berm;
        break;
    }
    default: {
        // Scuffed by turning on the spot: shallower, raked lengthwise, more sand shoved to the sides.
        berm = std::exp(-((a - 1.2f) * (a - 1.2f)) / (0.24f * 0.24f));
        const float rake = 0.5f + 0.5f * std::sin(17 * s + 2 * std::sin(5 * s));
        h = -0.9f * wall * (1 - 0.35f * rake) + 0.7f * berm;
        break;
    }
    }
    return h;
}

float coverageAt(float s, float t) {
    const float wob = 0.06f * std::sin(kTau * 3 * t + 2.0f * s) + 0.04f * std::sin(kTau * 7 * t + 1.0f);
    return 1 - smooth(1.28f, kTrackSpan - 0.04f, std::abs(s) + wob);
}

std::uint32_t pack(float r, float g, float b, float a) {
    auto q = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return q(r) | q(g) << 8 | q(b) << 16 | q(a) << 24;
}

}  // namespace

bool trackContactFromGear(const std::vector<DriveContact>& contacts, TrackContact& out) {
    if (contacts.empty()) return false;
    std::vector<DriveContact> c = contacts;
    std::sort(c.begin(), c.end(), [](const DriveContact& a, const DriveContact& b) { return a.x0 < b.x0; });
    struct Merged {
        float x0, x1;
        bool track;
    };
    std::vector<Merged> m;
    for (const DriveContact& d : c) {
        if (!m.empty() && d.x0 <= m.back().x1 + 0.3f) {
            m.back().x1 = std::max(m.back().x1, d.x1);
            m.back().track = m.back().track || d.track;
        } else {
            m.push_back({d.x0, d.x1, d.track});
        }
    }
    out = TrackContact{};
    bool track = false;
    float widest = 0;
    for (const Merged& b : m) {
        const float hw = 0.5f * (b.x1 - b.x0);
        if (hw < 0.4f) continue;
        out.bands.push_back({0.5f * (b.x0 + b.x1), hw});
        track = track || b.track;
        widest = std::max(widest, hw);
    }
    if (out.bands.empty() || out.bands.size() > 6) return false;
    // Mirror pairs (a model's two sides are rarely found exactly alike): the same offset and width.
    for (std::size_t i = 0; i < out.bands.size(); ++i)
        for (std::size_t j = i + 1; j < out.bands.size(); ++j) {
            auto& a = out.bands[i];
            auto& b = out.bands[j];
            const float m = 0.5f * (std::abs(a.lateral) + std::abs(b.lateral));
            if (a.lateral * b.lateral >= 0 || std::abs(a.lateral + b.lateral) > 0.3f * m + 0.5f) continue;
            const float hw = std::max(a.halfWidth, b.halfWidth);
            a.lateral = a.lateral < 0 ? -m : m, b.lateral = -a.lateral;
            a.halfWidth = b.halfWidth = hw;
        }
    if (track) {
        // The links found are the pads' middle pieces: the belt (and its print) is wider.
        for (auto& b : out.bands) b.halfWidth *= 1.5f;
        widest *= 1.5f;
        out.pattern = kTrackTread;
        out.period = std::clamp(widest * 5.0f, 7.0f, 12.0f);
    } else if (widest >= 2.2f) {
        out.pattern = kTrackBalloon;
        out.period = std::clamp(widest * 4.0f, 8.0f, 16.0f);
    } else {
        out.pattern = kTrackTyre;
        out.period = std::clamp(widest * 6.0f, 5.0f, 10.0f);
    }
    out.depth = track ? 1.0f : out.pattern == kTrackBalloon ? 0.95f : 0.7f;
    return true;
}

TrackContact fallbackTrackContact(std::string_view name) {
    const std::string n(name);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    TrackContact c;
    if (has("TRIKE") || has("RAIDER")) {
        c.pattern = kTrackTyre;
        c.bands = {{-4.5f, 1.2f}, {0.0f, 1.2f}, {4.5f, 1.2f}};
        c.period = 6;
        c.depth = 0.6f;
    } else if (has("QUAD")) {
        c.pattern = kTrackTyre;
        c.bands = {{-5.5f, 1.5f}, {5.5f, 1.5f}};
        c.period = 7;
        c.depth = 0.7f;
    } else if (has("HARVESTER") || has("MCV")) {
        c.pattern = kTrackBalloon;
        c.bands = {{-9.0f, 3.2f}, {9.0f, 3.2f}};
        c.period = 12;
        c.depth = 0.95f;
    } else if (has("DEVASTATOR")) {
        c.bands = {{-10.0f, 3.5f}, {10.0f, 3.5f}};
        c.period = 10;
    } else {  // tanks (combat, missile, siege, sonic, deviator)
        c.bands = {{-7.5f, 2.6f}, {7.5f, 2.6f}};
        c.period = 8;
    }
    return c;
}

TrackTexture buildTrackTexture() {
    TrackTexture tex;
    tex.w = kColumn * kTrackPatterns;
    tex.h = kTexH;
    // Slopes: one unit of s = a reference half-width of 2.6 px, one period = 8 px, the groove 0.8 px deep.
    const float ds = 2 * kTrackSpan / float(kColumn - 2 * kPad), dt = 1.0f / float(kTexH);
    const float kxs = 0.8f / 2.6f, kyt = 0.8f / 8.0f;
    std::vector<std::uint32_t> base(std::size_t(tex.w) * std::size_t(tex.h), pack(0.5f, 0.5f, 0.5f, 0));
    for (int p = 0; p < kTrackPatterns; ++p) {
        const auto pat = TrackPattern(p);
        for (int y = 0; y < kTexH; ++y)
            for (int i = kPad; i < kColumn - kPad; ++i) {
                const float s = -kTrackSpan + (float(i - kPad) + 0.5f) * ds, t = (float(y) + 0.5f) * dt;
                const float e = 0.25f;  // finite differences over a quarter texel
                const float dhs = (heightAt(pat, s + e * ds, t) - heightAt(pat, s - e * ds, t)) / (2 * e * ds);
                const float dht = (heightAt(pat, s, t + e * dt) - heightAt(pat, s, t - e * dt)) / (2 * e * dt);
                float nx = -dhs * kxs, ny = -dht * kyt, nz = 1;
                const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= l, ny /= l;
                const float h = heightAt(pat, s, t);
                // Pressed sand a little darker, loose sand on the berms a little lighter.
                const float tone = 0.5f + (h < 0 ? 0.07f * h : 0.03f * h);
                const float a = coverageAt(s, t);
                base[std::size_t(y) * std::size_t(tex.w) + std::size_t(p * kColumn + i)] =
                    pack(0.5f + 0.5f * nx, 0.5f + 0.5f * ny, tone, a);
            }
    }
    tex.levels.push_back(std::move(base));
    // Mips (box filter; periodic in v): the pads keep the columns apart down to level 2.
    int w = tex.w, h = tex.h;
    while (tex.levels.size() < 4 && w >= 2 && h >= 2) {
        const auto& src = tex.levels.back();
        const int w2 = w / 2, h2 = h / 2;
        std::vector<std::uint32_t> dst(std::size_t(w2) * std::size_t(h2));
        for (int y = 0; y < h2; ++y)
            for (int x = 0; x < w2; ++x) {
                std::uint32_t sum[4] = {};
                for (int k = 0; k < 4; ++k) {
                    const std::uint32_t v = src[std::size_t(2 * y + (k >> 1)) * std::size_t(w) + std::size_t(2 * x + (k & 1))];
                    for (int c = 0; c < 4; ++c) sum[c] += (v >> (8 * c)) & 0xFF;
                }
                std::uint32_t o = 0;
                for (int c = 0; c < 4; ++c) o |= ((sum[c] + 2) / 4) << (8 * c);
                dst[std::size_t(y) * std::size_t(w2) + std::size_t(x)] = o;
            }
        tex.levels.push_back(std::move(dst));
        w = w2, h = h2;
    }
    return tex;
}

void trackColumnU(TrackPattern p, int texW, float& u0, float& u1) {
    u0 = float(int(p) * kColumn + kPad) / float(texW);
    u1 = float(int(p) * kColumn + kColumn - kPad) / float(texW);
}

}  // namespace d2k::render3d
