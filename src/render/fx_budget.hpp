#pragma once
// Busy-screen budget of the Modern effects and the Reimagined cinematic particles (docs/enhanced-graphics.md
// "Effects intensity"; Settings::effectsIntensity). Render-only.
//
// "Effects intensity: Clean / Balanced / Cinematic":
//   Cinematic - the full look: no budget (every event spawns everything it always did), only a light
//               see-through of smoke over units;
//   Balanced  - (default) a single explosion keeps the full look, but the effects scale down as the view gets
//               busy: transient smoke (shell / rocket / artillery hits, small arms, a blast's own column) clears
//               within ~1-3 s, puffs get smaller, thinner and fewer with the number of recent events in view
//               (smoothed with hysteresis: rises in ~0.6 s, settles back over ~4 s, so nothing pops); burning
//               wrecks / buildings keep their columns, but nearby ones merge into one and only a few burn
//               at once per screen area; smoke goes see-through over units;
//   Clean     - short effects, minimal smoke: as Balanced at a high busy level all the time, shorter columns.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace d2k::render {

// D2K_HIDE_UNITS=1 (measurements only): ground units are not drawn (2D, 3D sprites and models) - the unit mask
// of the readability numbers is the difference between a frame with and one without them.
inline bool fxHideUnits() {
    static const bool hide = [] {
        const char* e = std::getenv("D2K_HIDE_UNITS");
        return e && *e && *e != '0';
    }();
    return hide;
}

enum class FxIntensity : std::uint8_t { Clean, Balanced, Cinematic };

inline const char* fxIntensityName(FxIntensity i) {
    return i == FxIntensity::Clean ? "clean" : i == FxIntensity::Cinematic ? "cinematic" : "balanced";
}

struct FxBudget {
    FxIntensity intensity = FxIntensity::Balanced;
    float load = 0;  // recent event weight in view (decays with ~2.5 s), x the view's area factor
    float busy = 0;  // 0 calm .. 1 very busy, smoothed (hysteresis)

    void reset() { load = busy = 0; }
    // An event in view: weight ~1 for a tank-shell / rocket hit, more for big blasts.
    void addEvent(float w) { load += w; }
    // areaK: reference view area / this view's area (zoomed in = the same events fill more of the screen).
    void step(float dt, float areaK) {
        if (dt <= 0) return;
        load *= std::exp(-dt / 2.5f);
        const float x = load * std::clamp(areaK, 0.35f, 3.0f);
        float target = std::clamp((x - 5.0f) / 20.0f, 0.0f, 1.0f);
        target = target * target * (3 - 2 * target);
        if (intensity == FxIntensity::Cinematic) target = 0;
        const float tau = target > busy ? 0.6f : 4.0f;
        busy += (target - busy) * (1 - std::exp(-dt / tau));
        if (busy < 1e-4f) busy = 0;
    }
    bool on() const { return intensity != FxIntensity::Cinematic; }
    // The busy level the knobs below use (Clean: never below 0.7).
    float level() const {
        if (intensity == FxIntensity::Cinematic) return 0;
        return intensity == FxIntensity::Clean ? std::max(0.7f, busy) : busy;
    }
    // Transient smoke / dust (hits, a blast's own column): seconds it may last (0 = no cap).
    float smokeCap() const {
        if (intensity == FxIntensity::Cinematic) return 0;
        if (intensity == FxIntensity::Clean) return 1.0f;
        return 3.0f - 1.8f * busy;  // 3 s calm .. 1.2 s busy
    }
    // Size / opacity / count multipliers of transient puffs.
    float sizeMul() const { return 1 - 0.3f * level(); }
    float alphaMul() const { return 1 - 0.4f * level(); }
    float countMul() const { return 1 - 0.55f * level(); }
    // Lasting columns (burning wrecks / buildings): emission rate and puff life multipliers.
    float columnRate() const { return intensity == FxIntensity::Clean ? 0.45f : 1 - 0.35f * busy; }
    float columnLife() const { return intensity == FxIntensity::Clean ? 0.45f : 1 - 0.35f * busy; }
    // Long-lived columns allowed at once per reference view area (Cinematic: no cap).
    int columnCap(float areaK) const {
        if (intensity == FxIntensity::Cinematic) return 1 << 20;
        const float base = intensity == FxIntensity::Clean ? 3.0f : 7.0f;
        return std::max(2, int(std::lround(base / std::clamp(areaK, 0.35f, 3.0f))));
    }
    // Columns closer than this (world px, ~1.5 tiles) share one (Cinematic: the old 20 px).
    float mergeRadius() const { return intensity == FxIntensity::Cinematic ? 20.0f : 48.0f; }
    // Smoke over units: the lowest opacity a puff keeps where it covers a unit (own selected units: `sel`).
    float seeThrough(bool sel) const {
        switch (intensity) {
        case FxIntensity::Cinematic: return sel ? 0.55f : 0.75f;
        case FxIntensity::Clean: return sel ? 0.12f : 0.25f;
        default: return sel ? 0.2f : 0.38f;
        }
    }
    // The fade-out at the end of a capped puff (s).
    static float capFade(float cap) { return std::min(1.0f, 0.45f * cap); }
    // Alpha multiplier of a puff of age `age` with cap `cut` (0 = none).
    static float capAlpha(float age, float cut) {
        if (cut <= 0) return 1;
        const float f = capFade(cut);
        const float t = std::clamp((cut - age) / f, 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }
    // Area factor of a view of w x h world px (reference: 1600 x 900 world px, a 2560 x 1440 window at ~1.6x).
    static float areaK(float w, float h) { return (1600.0f * 900.0f) / std::max(1.0f, w * h); }
    // A cheap deterministic 0..1 hash (thinning must not disturb the effects' own RNG streams).
    static float hash01(std::uint32_t n) {
        n ^= n >> 16;
        n *= 0x7feb352du;
        n ^= n >> 15;
        n *= 0x846ca68bu;
        n ^= n >> 16;
        return float(n & 0xFFFFFF) / float(0x1000000);
    }
};

}  // namespace d2k::render
