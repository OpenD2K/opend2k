#include "render/lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string_view>

namespace d2k::render {

namespace {

void set3(float* d, float a, float b, float c) {
    d[0] = a;
    d[1] = b;
    d[2] = c;
}

// The fixed times. Colours are multipliers of the old light (ambient 0.62 + diffuse 0.55 N.L in
// 3D; the 2D bake has its own constants and divides by the grade, render3d/terrain_bake.cpp).
SceneLight fixedTime(TimeOfDay t, LightingStyle s) {
    SceneLight l;
    const bool modern = s == LightingStyle::Modern;
    switch (t) {
    case TimeOfDay::Day:
    case TimeOfDay::Cycle:
        if (!modern) return l;  // identity
        // Warm, directional sun against a cooler sky: more contrast between lit and shaded sides,
        // a warm cast overall.
        set3(l.sun, 1.20f, 1.05f, 0.84f);
        set3(l.amb, 0.86f, 0.90f, 1.00f);
        set3(l.fog, 1.04f, 0.99f, 0.90f);
        l.lamps = 0.12f;
        l.sat = 1.06f;
        break;
    case TimeOfDay::Dawn:
        // Low pink-orange sun from the south, a pale lilac sky.
        l.dAz = -40;
        l.elK = 0.62f;
        set3(l.sun, 1.06f, 0.92f, 0.90f);
        set3(l.amb, 0.76f, 0.76f, 0.94f);
        set3(l.fog, 0.95f, 0.85f, 0.92f);
        l.lamps = 0.3f;
        l.sat = 0.72f;
        break;
    case TimeOfDay::Dusk:
        // Low red-gold sun from the west, a violet-blue sky: long shadows.
        l.dAz = 28;
        l.elK = 0.6f;
        set3(l.sun, 1.04f, 0.78f, 0.50f);
        set3(l.amb, 0.48f, 0.48f, 0.64f);
        set3(l.fog, 0.85f, 0.60f, 0.50f);
        l.lamps = 0.6f;
        l.sat = 0.78f;
        break;
    case TimeOfDay::Night:
        // Cool blue moonlight, still readable: the moon high, a dark blue sky.
        l.dAz = 12;
        l.elK = 1.1f;
        set3(l.sun, 0.28f, 0.37f, 0.58f);
        set3(l.amb, 0.25f, 0.31f, 0.48f);
        set3(l.fog, 0.22f, 0.28f, 0.42f);
        l.lamps = 1.0f;
        l.sat = 0.55f;
        break;
    }
    l.identity = false;
    l.modern = modern;
    if (modern) {
        l.bloom = t == TimeOfDay::Night ? 0.75f : t == TimeOfDay::Day || t == TimeOfDay::Cycle ? 0.3f : 0.5f;
        l.bloomThreshold = t == TimeOfDay::Night ? 0.6f : t == TimeOfDay::Day || t == TimeOfDay::Cycle ? 0.92f : 0.78f;
        l.spec = t == TimeOfDay::Night ? 0.8f : 1.6f;
    }
    return l;
}

SceneLight mix(const SceneLight& a, const SceneLight& b, float t) {
    SceneLight o;
    auto m = [t](float x, float y) { return x + (y - x) * t; };
    o.dAz = m(a.dAz, b.dAz);
    o.elK = m(a.elK, b.elK);
    for (int i = 0; i < 3; ++i) {
        o.sun[i] = m(a.sun[i], b.sun[i]);
        o.amb[i] = m(a.amb[i], b.amb[i]);
        o.fog[i] = m(a.fog[i], b.fog[i]);
    }
    o.lamps = m(a.lamps, b.lamps);
    o.bloom = m(a.bloom, b.bloom);
    o.bloomThreshold = m(a.bloomThreshold, b.bloomThreshold);
    o.spec = m(a.spec, b.spec);
    o.sat = m(a.sat, b.sat);
    o.identity = false;
    o.modern = a.modern || b.modern;
    return o;
}

}  // namespace

void SceneLight::sunDirection(const float base[3], float out[3]) const {
    if (dAz == 0 && elK == 1) {
        std::memcpy(out, base, sizeof(float) * 3);
        return;
    }
    // Compass azimuth (0 north = -z, 90 east = +x) and elevation of the base sun.
    const float h = std::hypot(base[0], base[2]);
    const float el0 = std::atan2(base[1], h);
    const float az0 = std::atan2(base[0], -base[2]);
    // Never lower than 20 degrees: the 2D bake's shadow margins and the 3D shadow map fit stay valid.
    const float el = std::clamp(el0 * elK, 0.349f, 1.40f);
    const float az = az0 + dAz * 3.14159265f / 180.0f;
    out[0] = std::sin(az) * std::cos(el);
    out[1] = std::sin(el);
    out[2] = -std::cos(az) * std::cos(el);
}

SpriteShadowWarp spriteShadowWarp(const SceneLight& l) {
    SpriteShadowWarp w;
    if (!l.modern || l.identity || (std::fabs(l.dAz) < 1e-3f && std::fabs(l.elK - 1) < 1e-4f)) return w;
    constexpr float kPi = 3.14159265f, kEl0 = 40.0f * kPi / 180.0f;  // the art's sun: north-west, 40 degrees
    const float el = std::clamp(kEl0 * l.elK, 0.349f, 1.40f);        // never below 20 degrees (sunDirection)
    w.k = std::clamp(std::tan(kEl0) / std::tan(el), 0.35f, 2.2f);
    constexpr float kElPack = 58.0f * kPi / 180.0f;
    w.kPack = std::clamp(std::tan(kElPack) / std::tan(std::clamp(kElPack * l.elK, 0.349f, 1.40f)), 0.35f, 2.4f);
    // Shadow bearing (compass, clockwise from north = screen up): the sun's azimuth + 180.
    const float b0 = 135.0f * kPi / 180.0f, b = b0 + l.dAz * kPi / 180.0f;
    w.dx0 = std::sin(b0);
    w.dy0 = -std::cos(b0);
    w.dx = std::sin(b);
    w.dy = -std::cos(b);
    // Darkness like the 2D terrain bake's shadows (its ambient 0.46 / diffuse 0.78): the share of the
    // light a shaded spot loses, against the Day of the same style (= 1 at Day, so the Cycle is smooth).
    auto loss = [](const SceneLight& s, float sunEl) {
        const float lum = [](const float* c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }(s.amb);
        const float sunL = 0.3f * s.sun[0] + 0.59f * s.sun[1] + 0.11f * s.sun[2];
        const float shade = 0.46f * lum, lit = shade + 0.78f * std::sin(sunEl) * sunL;
        return lit > 1e-4f ? 1 - shade / lit : 0.0f;
    };
    const SceneLight day = sceneLight(TimeOfDay::Day, LightingStyle::Modern);
    const float ratio = loss(l, el) / std::max(1e-3f, loss(day, kEl0));
    // Long shadows fade (up to a quarter at twice the length) - the softer penumbra of a low sun.
    const float fade = 1 - 0.25f * std::clamp(w.k - 1, 0.0f, 1.0f);  // (the pack frames: the same fade)
    w.strength = std::clamp(ratio, 0.55f, 1.0f) * fade;
    w.active = true;
    return w;
}

double cycleLength() {
    static const double len = [] {
        const char* e = std::getenv("D2K_CYCLE_SECONDS");
        const double v = e ? std::atof(e) : 0.0;
        return v > 1.0 ? v : 5400.0;  // game seconds (ticks / 20): 30 min at GameSpeed 100 (60 ticks/s, owner 2026-11-13), 90 min at the default speed
    }();
    return len;
}

SceneLight sceneLight(TimeOfDay t, LightingStyle s, double cycleSeconds, bool lite) {
    SceneLight l;
    if (t != TimeOfDay::Cycle) {
        l = fixedTime(t, s);
    } else {
        // One day: Day (long), sunset into Dusk, Night, Dawn, back to Day. Keys at fractions of
        // the cycle; smooth (smoothstep) between neighbours.
        struct Key { float at; TimeOfDay t; };
        static constexpr Key kKeys[] = {{0.00f, TimeOfDay::Day},  {0.40f, TimeOfDay::Day},   {0.50f, TimeOfDay::Dusk},
                                        {0.58f, TimeOfDay::Night}, {0.84f, TimeOfDay::Night}, {0.92f, TimeOfDay::Dawn},
                                        {1.00f, TimeOfDay::Day}};
        const double len = cycleLength();
        double f = std::fmod(std::max(0.0, cycleSeconds), len) / len;
        int k = 0;
        while (k + 2 < int(std::size(kKeys)) && f >= kKeys[k + 1].at) ++k;
        const Key& a = kKeys[k];
        const Key& b = kKeys[k + 1];
        float u = float((f - a.at) / std::max(1e-6f, b.at - a.at));
        u = std::clamp(u, 0.0f, 1.0f);
        u = u * u * (3 - 2 * u);
        const SceneLight la = fixedTime(a.t, s), lb = fixedTime(b.t, s);
        l = a.t == b.t ? la : mix(la, lb, u);
        if (a.t == b.t && la.identity) return la;
        l.identity = false;
    }
    if (lite) {  // the iPad lite profile: the colours only
        l.bloom = 0;
        l.lamps = 0;
    }
    return l;
}

const char* timeOfDayName(TimeOfDay t) {
    switch (t) {
    case TimeOfDay::Dawn: return "dawn";
    case TimeOfDay::Dusk: return "dusk";
    case TimeOfDay::Night: return "night";
    case TimeOfDay::Cycle: return "cycle";
    default: return "day";
    }
}

TimeOfDay timeOfDayFrom(const char* v) {
    const std::string_view s = v ? v : "";
    if (s == "dawn") return TimeOfDay::Dawn;
    if (s == "dusk") return TimeOfDay::Dusk;
    if (s == "night") return TimeOfDay::Night;
    if (s == "cycle") return TimeOfDay::Cycle;
    return TimeOfDay::Day;
}

const char* lightingStyleName(LightingStyle s) { return s == LightingStyle::Modern ? "modern" : "classic"; }

LightingStyle lightingStyleFrom(const char* v) {
    return v && std::string_view(v) == "modern" ? LightingStyle::Modern : LightingStyle::Classic;
}

}  // namespace d2k::render
