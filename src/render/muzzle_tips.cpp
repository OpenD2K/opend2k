#include "render/muzzle_tips.hpp"

#include "render/sprites.hpp"

#include <cmath>

namespace d2k::render {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kSin45 = 0.70710678f;
}  // namespace

bool MuzzleTips::turreted(int type) const {
    if (type < 0 || type >= formats::kMaxUnitTypes) return false;
    const auto& ut = t_.bin.units[type];
    return ut.barrelArt >= 0 && ut.barrelArt < formats::kMaxUnitArt;
}

// The frame's pixels relative to its anchor (MapRenderer::blitUnit: pixel (px, py) lands at
// anchor + (px - x, py - y)); the screen direction of the facing (the original's camera looks
// north 45 degrees down: ground north-south distances shrink by sin 45).
MuzzleTips::Tip MuzzleTips::measure(int frame, int facing32) const {
    Tip t;
    const formats::R16Frame* f = sprites_.frame(frame);
    if (!f || f->empty()) return t;
    const float a = float(facing32 & 31) * (2 * kPi / 32);
    float dx = std::sin(a), dy = -std::cos(a) * kSin45;
    const float l = std::hypot(dx, dy);
    dx /= l;
    dy /= l;
    const float nx = -dy, ny = dx;
    for (const float band : {2.0f, 3.5f, 6.0f}) {
        float best = -1e9f, perpSum = 0;
        int n = 0;
        for (int py = 0; py < f->height; ++py)
            for (int px = 0; px < f->width; ++px) {
                const std::size_t i = std::size_t(py) * std::size_t(f->width) + std::size_t(px);
                bool solid;
                if (f->bpp == 8) solid = i < f->pixels.size() && f->pixels[i] > 1;  // 0 clear, 1 shadow
                else solid = 2 * i + 1 < f->pixels.size() && (f->pixels[2 * i] | f->pixels[2 * i + 1]) != 0;
                if (!solid) continue;
                const float rx = float(px - f->x) + 0.5f, ry = float(py - f->y) + 0.5f;
                const float perp = rx * nx + ry * ny;
                if (std::fabs(perp) > band) continue;
                const float along = rx * dx + ry * dy;
                if (along > best + 0.5f) {
                    best = along;
                    perpSum = perp;
                    n = 1;
                } else if (along > best - 0.5f) {
                    perpSum += perp;
                    ++n;
                }
            }
        if (n == 0 || best < 1) continue;
        const float along = best - 0.5f, perp = perpSum / float(n);
        t.sx = dx * along + nx * perp;
        t.sy = dy * along + ny * perp;
        t.ok = true;
        break;
    }
    return t;
}

bool MuzzleTips::tip(const game::Unit& u, float facing, float& dx, float& dy, float& z) const {
    if (u.type < 0 || u.type >= formats::kMaxUnitTypes) return false;
    const auto& ut = t_.bin.units[u.type];
    const bool tur = turreted(u.type);
    const int art = tur ? ut.barrelArt : ut.unitArt;
    if (art < 0 || art >= formats::kMaxUnitArt || std::size_t(art) >= t_.unitArtFirstFrame.size()) return false;
    const float r = std::round(facing);
    const int f32 = int(r) & 31;
    const int frame = t_.unitArtFirstFrame[std::size_t(art)] + ut.directionFrames[f32];
    const long long key = (long long)frame * 64 + f32;
    auto it = cache_.find(key);
    if (it == cache_.end()) it = cache_.emplace(key, measure(frame, f32)).first;
    const Tip& t = it->second;
    if (!t.ok) return false;
    // The sprite is turned by the eased facing's residual about the anchor (blitUnit): the tip too.
    float sx = t.sx, sy = t.sy;
    if (t_.bin.unitArtDirectionFrames[art] == 32) {
        const float rad = (facing - r) * (2 * kPi / 32), c = std::cos(rad), s = std::sin(rad);
        sx = t.sx * c - t.sy * s;
        sy = t.sx * s + t.sy * c;
    }
    // The gun's height above the ground (the screen point stays the measured one: y - z).
    z = tur ? 7.0f : 5.0f;
    dx = sx;
    dy = sy + z;
    return true;
}

}  // namespace d2k::render
