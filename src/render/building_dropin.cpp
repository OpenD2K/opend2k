// Drop-in build-up poses (render/building_dropin.hpp, docs/3d-mode.md "Drop-in build-up").
#include "render/building_dropin.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

namespace {
float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float smooth(float e0, float e1, float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}
// Ease-out with a small overshoot (the parts snap into place).
float easeOutBack(float t, float s = 1.3f) {
    const float u = t - 1;
    return 1 + (s + 1) * u * u * u + s * u * u;
}
}  // namespace

DropInPose dropInPose(float p, bool fromGround, float height) {
    DropInPose o;
    p = clamp01(p);
    const float land = dropInImpactAt(fromGround);
    if (!fromGround && p < land) {  // falling: gravity (accelerating), folded, a little narrower
        const float s = p / land;
        o.lift = (110.0f + 1.0f * std::max(height, 0.0f)) * (1 - s * s);
        o.shadow = 0.25f + 0.75f * s;
    } else if (!fromGround) {
        o.shadow = 1 - smooth(land, land + 0.05f, p);
    }
    // The landing: the folded box squashes a little and bounces back.
    const float q = (p - land) / 0.07f;
    if (q > 0 && q < 1) {
        const float k = std::sin(3.14159265f * q) * (1 - 0.4f * q);
        o.squash = 1 - (fromGround ? 0.05f : 0.13f) * k;
        o.spread = 1 + (fromGround ? 0.02f : 0.05f) * k;
    }
    const float t = clamp01((p - kDropUnfold0) / (kDropUnfold1 - kDropUnfold0));
    o.unfold = t;
    // The footprint widens to full size in the second half of the unfold (arms, ramps, aprons).
    const float w = clamp01((t - 0.35f) / 0.6f);
    o.spread *= 0.9f + 0.1f * (w > 0 ? easeOutBack(w, 1.1f) : 0.0f);
    o.light = smooth(kDropLights0, 1.0f, p);
    return o;
}

}  // namespace d2k::render
