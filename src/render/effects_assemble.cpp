// Modern effects of the Assemble build-up (render/building_assemble.hpp, docs/3d-mode.md "Assemble
// build-up"): a glowing survey outline on the footprint and site dust when the work starts, light dust
// drifting round the base while it goes on, sparks running along the glowing cut of a part that grows
// out of the ground (and round the ring a part telescopes out of), and a weld flash with a burst of
// sparks (and dust on the ground) where each part seats. Selling plays it backwards. Render-only, own
// RNG; a part's moments are seen when the building's build-up progress (the original's counter) passes
// them between two updates, so a paused game spawns nothing.
#include "render/building_assemble.hpp"
#include "render/effects.hpp"
#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

using namespace fx;

namespace {
constexpr float kOutlineEnd = 0.24f;  // the survey outline has faded by this progress
}

void Effects::updateAssemblies(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                               int viewH, bool showShroud) {
    for (auto& a : assemblies_) a.seen = false;
    if (assembly_) {
        const float vx0 = cam.x - 96, vy0 = cam.y - 96;
        const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 160;
        for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
            const auto& side = w.sides()[std::size_t(s)];
            for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.buildings[std::size_t(i)];
                if (!b.alive || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
                const float p = assembly_->buildingAssembly(w, s, i, &assemblyParts_);
                if (p < 0) continue;
                AssemblySeen* a = nullptr;
                for (auto& o : assemblies_)
                    if (o.side == s && o.index == i && o.type == b.type && o.tileX == b.tileX && o.tileY == b.tileY) a = &o;
                if (!a) {
                    AssemblySeen n;
                    n.side = s;
                    n.index = i;
                    n.type = b.type;
                    n.tileX = b.tileX;
                    n.tileY = b.tileY;
                    const auto& t = t_.bin.buildings[b.type];
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
                    n.sand = terrainColour(w, atlas, (n.x0 + n.x1) * 0.5f, n.y1 + 6);
                    assemblies_.push_back(n);
                    a = &assemblies_.back();
                }
                a->seen = true;
                a->p = p;
                const float cx = (a->x0 + a->x1) * 0.5f, cy = (a->y0 + a->y1) * 0.5f;
                const int tx = int(cx) >> 5, ty = int(cy) >> 5;
                const bool shrouded = showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
                a->visible = !shrouded && cx >= vx0 && cx <= vx1 && cy >= vy0 && cy <= vy1;
                const float last = a->lastP;
                a->lastP = p;
                if (dt <= 0 || !a->visible) continue;
                const bool building = (b.flags & game::building_flags::kBuildup) != 0;
                const float W = a->x1 - a->x0, H = a->y1 - a->y0;
                const float k = std::clamp((W + H) / 192.0f, 0.5f, 1.4f);  // 3 x 3 cells = 1
                const std::uint32_t dust = lerpColour(a->sand, rgba(246, 222, 184, 255), 0.35f);
                auto edgePoint = [&](float u, float& x, float& y, float& nx, float& ny) {
                    const float per = 2 * (W + H), q = u * per;
                    if (q < W) x = a->x0 + q, y = a->y0, nx = 0, ny = -1;
                    else if (q < W + H) x = a->x1, y = a->y0 + (q - W), nx = 1, ny = 0;
                    else if (q < 2 * W + H) x = a->x1 - (q - W - H), y = a->y1, nx = 0, ny = 1;
                    else x = a->x0, y = a->y1 - (q - 2 * W - H), nx = -1, ny = 0;
                };
                auto puff = [&](float x, float y, float z, float vx, float vy, float size, float alpha, float life) {
                    Particle q{};
                    q.x = x;
                    q.y = y;
                    q.z = z;
                    q.vx = vx + kWindX * 0.6f;
                    q.vy = vy + kWindY * 0.6f;
                    q.vz = rnd(4, 14);
                    q.drag = 1.6f;
                    q.life = life;
                    q.size0 = size * 0.35f;
                    q.size1 = size;
                    q.c0 = withAlpha(scaleRgb(dust, rnd(0.93f, 1.05f)), int(alpha));
                    q.c1 = withAlpha(dust, 0);
                    q.tex = kSmoke;
                    q.blend = Blend::Alpha;
                    q.rot = rnd(0, 2 * kPi);
                    q.vrot = rnd(-0.6f, 0.6f);
                    add(q);
                };
                auto spark = [&](float x, float y, float z, float speed, float up) {
                    Particle q{};
                    const float an = rnd(0, 2 * kPi), sp = rnd(0.4f, 1.0f) * speed;
                    q.x = x;
                    q.y = y;
                    q.z = z;
                    q.vx = std::cos(an) * sp;
                    q.vy = std::sin(an) * sp * 0.7f;
                    q.vz = rnd(0.3f, 1.0f) * up;
                    q.gravity = -280;
                    q.drag = 1.4f;
                    q.life = rnd(0.25f, 0.55f);
                    q.size0 = rnd(1.8f, 2.8f);
                    q.size1 = 0.8f;
                    q.c0 = rgba(255, 244, 215, 255);
                    q.c1 = rgba(255, 150, 50, 0);
                    q.tex = kSpark;
                    q.blend = Blend::Add;
                    q.streak = true;
                    add(q);
                };
                // The site is marked out: dust rising along the footprint as the work starts.
                if (building && last < 0 && p < 0.08f) {
                    const int n = int(std::clamp((W + H) / 4.0f, 24.0f, 70.0f) * std::max(detail_, 0.5f));
                    for (int j = 0; j < n; ++j) {
                        float x, y, nx, ny;
                        edgePoint((float(j) + rnd(0, 0.8f)) / float(n), x, y, nx, ny);
                        const float sp = rnd(18, 46) * k;
                        puff(x, y, rnd(0.5f, 3), nx * sp, ny * sp, rnd(36, 58) * k, rnd(150, 200), rnd(2.0f, 3.2f));
                    }
                }
                // Light dust drifting round the base while the work goes on.
                if (p > 0.03f && p < 0.92f) {
                    a->dustAcc += dt * 12.0f * k * load() * std::max(detail_, 0.5f);
                    while (a->dustAcc >= 1) {
                        a->dustAcc -= 1;
                        float x, y, nx, ny;
                        edgePoint(rnd(), x, y, nx, ny);
                        puff(x, y, rnd(0.5f, 4), nx * rnd(8, 22), ny * rnd(8, 22), rnd(30, 48) * k, rnd(90, 130),
                             rnd(1.8f, 2.8f));
                    }
                }
                auto crossed = [&](float t) { return (last >= 0 && last < t && p >= t) || (last > t && p <= t); };
                for (const AssemblyPartWorld& pt : assemblyParts_) {
                    const bool onGround = pt.z < 2.5f;
                    const float span = std::max(pt.t1 - pt.t0, 1e-3f);
                    const float sw = std::max(pt.w, 2.0f), sd = std::max(pt.d, 2.0f);
                    auto rim = [&](float& x, float& y) {  // a point on the part's box outline (ground plane)
                        const float u = rnd() * 2 * (sw + sd);
                        if (u < sw) x = pt.x - sw * 0.5f + u, y = pt.y - sd * 0.5f;
                        else if (u < sw + sd) x = pt.x + sw * 0.5f, y = pt.y - sd * 0.5f + (u - sw);
                        else if (u < 2 * sw + sd) x = pt.x + sw * 0.5f - (u - sw - sd), y = pt.y + sd * 0.5f;
                        else x = pt.x - sw * 0.5f, y = pt.y + sd * 0.5f - (u - 2 * sw - sd);
                    };
                    // A part that grows out of the ground: dust round its foot as it starts.
                    if (pt.motion == AssembleMotion::Extrude && onGround && building && crossed(pt.t0)) {
                        const int n = int(std::clamp((sw + sd) / 6.0f, 4.0f, 14.0f));
                        for (int j = 0; j < n; ++j) {
                            float x, y;
                            rim(x, y);
                            const float ox = x - pt.x, oy = y - pt.y, l = std::sqrt(ox * ox + oy * oy) + 1e-3f;
                            puff(x, y, 1, ox / l * rnd(18, 40), oy / l * rnd(18, 40), rnd(26, 42) * k, rnd(130, 175),
                                 rnd(1.4f, 2.4f));
                        }
                    }
                    // Sparks along the glowing cut of a growing part / round the ring a part telescopes out of.
                    if ((pt.motion == AssembleMotion::Extrude || pt.motion == AssembleMotion::Telescope) && p > pt.t0 &&
                        p < pt.t1) {
                        const float s01 = (p - pt.t0) / span;
                        const float z = pt.motion == AssembleMotion::Extrude
                                            ? pt.z + pt.h * (1 - (1 - s01) * (1 - s01))
                                            : pt.z;
                        const float want = dt * std::clamp((sw + sd) * 0.5f, 10.0f, 34.0f) * load();
                        int n = int(want);
                        if (rnd() < want - float(n)) ++n;
                        for (int j = 0; j < n; ++j) {
                            float x, y;
                            rim(x, y);
                            spark(x, y, z, 38, 30);
                        }
                    }
                    // Seated: a weld flash, a burst of sparks round its foot, dust if it stands on the ground.
                    if (crossed(pt.t1)) {
                        const float big = std::clamp((sw + sd + pt.h) / 48.0f, 0.4f, 1.5f);
                        const int n = int(std::round(10 + 16 * big));
                        for (int j = 0; j < n; ++j) {
                            float x, y;
                            rim(x, y);
                            spark(x, y, pt.z + rnd(0, 1.5f), 60 * big + 20, 45);
                        }
                        addFlash(pt.x, pt.y, pt.z + 2, 26 * big + 10, rgba(255, 214, 160, 255), 0.22f);
                        if (onGround && pt.motion != AssembleMotion::Extrude) {
                            for (int j = 0; j < 3; ++j) {
                                float x, y;
                                rim(x, y);
                                puff(x, y, 1, rnd(-12, 12), rnd(-12, 12), rnd(16, 26) * k, rnd(80, 115), rnd(0.9f, 1.5f));
                            }
                        }
                    }
                }
            }
        }
    }
    std::erase_if(assemblies_, [](const AssemblySeen& a) { return !a.seen; });
}

void Effects::assemblyMarks() {
    for (const AssemblySeen& a : assemblies_) {
        if (!a.visible) continue;
        const float cx = (a.x0 + a.x1) * 0.5f, cy = (a.y0 + a.y1) * 0.5f;
        const float hw = (a.x1 - a.x0) * 0.5f, hh = (a.y1 - a.y0) * 0.5f;
        // Worked ground: a darker patch round the footprint while the work goes on.
        const float work = smooth01(0.0f, 0.06f, a.p) * (1 - smooth01(0.85f, 1.0f, a.p));
        if (work > 0)
            marks_.push_back({cx, cy + 2, hw * 1.35f, hh * 1.3f, 0, kGlow, Blend::Alpha,
                              withAlpha(scaleRgb(a.sand, 0.55f), int(85 * work))});
        // The survey outline: thin glowing lines along the footprint and bright corners, gone once the
        // first blocks stand.
        const float o = smooth01(0.0f, 0.025f, a.p) * (1 - smooth01(0.1f, kOutlineEnd, a.p));
        if (o <= 0) continue;
        const std::uint32_t line = rgba(255, 196, 92, int(210 * o));
        const float in = 1.0f, t = 1.1f;
        marks_.push_back({cx, a.y0 + in, hw, t, 0, kLine, Blend::Add, line});
        marks_.push_back({cx, a.y1 - in, hw, t, 0, kLine, Blend::Add, line});
        marks_.push_back({a.x0 + in, cy, hh, t, 1.5707963f, kLine, Blend::Add, line});
        marks_.push_back({a.x1 - in, cy, hh, t, 1.5707963f, kLine, Blend::Add, line});
        const std::uint32_t corner = rgba(255, 228, 170, int(230 * o));
        for (const float x : {a.x0 + in, a.x1 - in})
            for (const float y : {a.y0 + in, a.y1 - in}) marks_.push_back({x, y, 5, 5, 0, kGlow, Blend::Add, corner});
    }
}

}  // namespace d2k::render
