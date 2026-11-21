// Modern effects of the drop-in build-up (render/building_dropin.hpp, docs/3d-mode.md "Drop-in
// build-up"): a soft shadow growing under a building model that comes down, and its landing - a
// ring of dust rolling out from the footprint, a few pebbles thrown up, a darker patch of disturbed
// sand fading over a few seconds. Render-only, own RNG; the landing is seen when the building's
// build-up progress (the original's counter) passes the impact point between two updates.
//
// Hook for the Reimagined cinematic particles: setDropInDust(false) leaves the burst to them (they
// read dropInImpacts(), filled every update); the ground marks stay here.
#include "render/building_dropin.hpp"
#include "render/effects.hpp"
#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

using namespace fx;

namespace {
constexpr float kMarkLife = 3.2f;  // s: the disturbed sand of a landing fades over this
}

void Effects::updateDropIns(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                            int viewH, bool showShroud) {
    dropInImpacts_.clear();
    for (auto& d : dropIns_) d.seen = false;
    if (dropIn_) {
        const float vx0 = cam.x - 96, vy0 = cam.y - 96;
        const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 160;
        for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
            const auto& side = w.sides()[std::size_t(s)];
            for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.buildings[std::size_t(i)];
                if (!b.alive || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
                const float p = dropIn_->buildingDropIn(w, s, i);
                if (p < 0) continue;
                DropInSeen* d = nullptr;
                for (auto& o : dropIns_)
                    if (o.side == s && o.index == i && o.type == b.type && o.tileX == b.tileX && o.tileY == b.tileY) d = &o;
                const auto& t = t_.bin.buildings[b.type];
                if (!d) {
                    DropInSeen n;
                    n.side = s;
                    n.index = i;
                    n.type = b.type;
                    n.tileX = b.tileX;
                    n.tileY = b.tileY;
                    n.lastP = p;
                    // The solid cells (not the walkable apron in front).
                    const std::uint32_t m = t.tilesOccupiedSolid ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
                    int c0 = 4, c1 = -1, r0 = 4, r1 = -1;
                    for (int q = 0; q < 16; ++q)
                        if ((m >> q) & 1) {
                            c0 = std::min(c0, q % 4), c1 = std::max(c1, q % 4);
                            r0 = std::min(r0, q / 4), r1 = std::max(r1, q / 4);
                        }
                    if (c1 < 0) c0 = r0 = c1 = r1 = 0;
                    n.x0 = float((b.tileX + c0) * 32);
                    n.y0 = float((b.tileY + r0) * 32);
                    n.x1 = float((b.tileX + c1 + 1) * 32);
                    n.y1 = float((b.tileY + r1 + 1) * 32);
                    n.fromGround = dropInFromGround(t);
                    n.sand = terrainColour(w, atlas, (n.x0 + n.x1) * 0.5f, n.y1 + 6);
                    dropIns_.push_back(n);
                    d = &dropIns_.back();
                }
                d->seen = true;
                const float cx = (d->x0 + d->x1) * 0.5f, cy = (d->y0 + d->y1) * 0.5f;
                const int tx = int(cx) >> 5, ty = int(cy) >> 5;
                const bool shrouded = showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
                d->visible = !shrouded && cx >= vx0 && cx <= vx1 && cy >= vy0 && cy <= vy1;
                const DropInPose pose = dropInPose(p, d->fromGround, 0);
                d->shadow = d->visible ? pose.shadow : 0.0f;
                // The landing: the progress passed the impact point since the last update while
                // building up (not selling; not a jump from a load / a first frame far past it).
                const float at = dropInImpactAt(d->fromGround);
                if (dt > 0 && (b.flags & game::building_flags::kBuildup) && d->lastP < at && p >= at && p - at < 0.2f) {
                    d->impactT = time_;
                    if (d->visible) {
                        dropInImpacts_.push_back({d->x0, d->y0, d->x1, d->y1, b.tileX, b.tileY, d->sand, d->fromGround});
                        if (dropInDust_) spawnDropInDust(*d);
                    }
                }
                d->lastP = p;
            }
        }
    }
    // Records whose building is complete / gone keep their mark until it has faded.
    for (std::size_t k = 0; k < dropIns_.size();) {
        DropInSeen& d = dropIns_[k];
        if (!d.seen) {
            d.shadow = 0;
            if (time_ - d.impactT > kMarkLife) {
                dropIns_.erase(dropIns_.begin() + std::ptrdiff_t(k));
                continue;
            }
        }
        ++k;
    }
}

void Effects::spawnDropInDust(const DropInSeen& d) {
    const float W = d.x1 - d.x0, H = d.y1 - d.y0;
    const float cx = (d.x0 + d.x1) * 0.5f, cy = (d.y0 + d.y1) * 0.5f;
    const float big = d.fromGround ? 0.55f : 1.0f;
    const float k = std::clamp((W + H) / 192.0f, 0.5f, 1.4f);  // 3 x 3 cells = 1
    // Dust in the air reads paler than the ground it comes from.
    const std::uint32_t dust = lerpColour(d.sand, rgba(246, 222, 184, 255), 0.35f);
    // A point on the footprint's edge and its outward direction.
    auto edge = [&](float u, float& x, float& y, float& nx, float& ny) {
        const float per = 2 * (W + H), s = u * per;
        if (s < W) x = d.x0 + s, y = d.y0, nx = 0, ny = -1;
        else if (s < W + H) x = d.x1, y = d.y0 + (s - W), nx = 1, ny = 0;
        else if (s < 2 * W + H) x = d.x1 - (s - W - H), y = d.y1, nx = 0, ny = 1;
        else x = d.x0, y = d.y1 - (s - 2 * W - H), nx = -1, ny = 0;
        // Corners and the middle of a side blow out diagonally a little.
        const float ox = x - cx, oy = y - cy, l = std::sqrt(ox * ox + oy * oy) + 1e-3f;
        nx = 0.55f * nx + 0.45f * ox / l;
        ny = 0.55f * ny + 0.45f * oy / l;
        const float nl = std::sqrt(nx * nx + ny * ny) + 1e-3f;
        nx /= nl, ny /= nl;
    };
    // The ring: low, fast, rolling out and slowing down.
    const int ring = int(std::clamp((W + H) / 5.0f, 22.0f, 60.0f) * big * std::max(detail_, 0.5f));
    for (int j = 0; j < ring; ++j) {
        float x, y, nx, ny;
        edge((float(j) + rnd(0, 0.8f)) / float(ring), x, y, nx, ny);
        Particle p{};
        const float sp = rnd(70, 135) * (0.7f + 0.3f * big) * k;
        p.x = x + nx * rnd(0, 4);
        p.y = y + ny * rnd(0, 4);
        p.z = rnd(0.5f, 3);
        p.vx = nx * sp + kWindX * 0.5f;
        p.vy = ny * sp + kWindY * 0.5f;
        p.vz = rnd(3, 12);
        p.drag = 2.6f;
        p.life = rnd(1.5f, 2.5f);
        p.size0 = rnd(9, 14) * k;
        p.size1 = rnd(46, 66) * k * (0.7f + 0.3f * big);
        p.c0 = withAlpha(scaleRgb(dust, rnd(0.93f, 1.05f)), int(rnd(190, 235) * (0.6f + 0.4f * big)));
        p.c1 = withAlpha(dust, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.8f, 0.8f);
        add(p);
    }
    // A few billows rising higher at the corners and sides (the cloud the reference shows).
    const int billows = int(12 * big);
    for (int j = 0; j < billows; ++j) {
        float x, y, nx, ny;
        edge(rnd(), x, y, nx, ny);
        Particle p{};
        p.x = x;
        p.y = y;
        p.z = rnd(2, 6);
        p.vx = nx * rnd(18, 40) + kWindX;
        p.vy = ny * rnd(18, 40) + kWindY;
        p.vz = rnd(16, 32);
        p.drag = 1.4f;
        p.life = rnd(2.2f, 3.2f);
        p.size0 = rnd(14, 20) * k;
        p.size1 = rnd(56, 78) * k;
        p.c0 = withAlpha(scaleRgb(dust, 1.04f), int(rnd(95, 135) * big));
        p.c1 = withAlpha(dust, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.5f, 0.5f);
        add(p);
    }
    // Pebbles thrown up from under the edges; each kicks a tiny puff where it lands.
    const int pebbles = int(14 * big * k);
    const std::uint32_t rock = scaleRgb(d.sand, 0.55f);
    for (int j = 0; j < pebbles; ++j) {
        float x, y, nx, ny;
        edge(rnd(), x, y, nx, ny);
        Particle g{};
        const float sp = rnd(35, 95);
        g.x = x;
        g.y = y;
        g.z = 2;
        g.vx = nx * sp + rnd(-12, 12);
        g.vy = ny * sp + rnd(-12, 12);
        g.vz = rnd(45, 105);
        g.gravity = -320;
        g.drag = 0.4f;
        g.life = rnd(0.7f, 1.2f);
        g.size0 = g.size1 = rnd(2.0f, 3.6f);
        g.c0 = withAlpha(scaleRgb(rock, rnd(0.8f, 1.2f)), 245);
        g.c1 = withAlpha(rock, 180);
        g.tex = kDebris;
        g.blend = Blend::Alpha;
        g.rot = rnd(0, 2 * kPi);
        g.vrot = rnd(-12, 12);
        g.fx = kFxLateFade | kFxLandDust;
        add(g);
    }
}

void Effects::dropInMarks() {
    for (const DropInSeen& d : dropIns_) {
        const float cx = (d.x0 + d.x1) * 0.5f, cy = (d.y0 + d.y1) * 0.5f;
        const float hw = (d.x1 - d.x0) * 0.5f, hh = (d.y1 - d.y0) * 0.5f;
        if (d.shadow > 0) {  // the soft shadow of a building coming down: grows and darkens as it nears
            const float s = d.shadow;
            marks_.push_back({cx + 2, cy + 3, hw * (0.55f + 0.6f * s), hh * (0.5f + 0.6f * s), 0, kGlow, Blend::Alpha,
                              rgba(18, 12, 6, int(40 + 150 * s * s))});
        }
        const float e = time_ - d.impactT;
        if (e >= 0 && e < kMarkLife) {
            const float u = e / kMarkLife, f = (1 - u) * (1 - u);
            const float big = d.fromGround ? 0.5f : 1.0f;
            // The disturbed sand: a darker patch round the footprint ...
            marks_.push_back({cx, cy + 2, hw * 1.45f, hh * 1.4f, 0, kGlow, Blend::Alpha,
                              withAlpha(scaleRgb(d.sand, 0.5f), int(120 * f * big))});
            // ... and the shock of the landing: a ring of pushed sand running out (the first half second).
            if (e < 0.6f) {
                const float r = e / 0.6f, g = 1 - r;
                const float R = std::max(hw, hh) * (1.1f + 1.1f * (1 - g * g));
                marks_.push_back({cx, cy, R, R * 0.9f, 0, kShock, Blend::Alpha, withAlpha(scaleRgb(d.sand, 0.62f), int(110 * g * big))});
            }
        }
    }
}

}  // namespace d2k::render
