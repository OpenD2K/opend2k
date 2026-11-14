// Building fires for the Modern effects (render/effects.hpp, Effects: Modern; docs/enhanced-graphics.md
// "Building fires"). Timed only by the original's own events: a building below half health whose smoke
// timer runs spawns EX_SMOKE3 / EX_FLAME at its BUILEXP.BIN points (UpdateBuilding, game/buildings.cpp;
// each point with its own period, the points dropping out at random over the timer's last 200 ticks).
// Every such event feeds a record of that building and point (feedBurn) instead of a one-off puff; the
// record burns while the events keep coming and dies down after the last one, so the fire flares up
// after hits and goes out on its own exactly when the original's smoke does. How big it burns follows
// the building's damage: light smoke just below half health, small fires lower, large fires with a
// thick black column near destruction.
//
// The look, per point: 2-4 flame tongues (coloured tongue sprites - white-yellow core, orange, dark red
// tips - rising, thinning, bent by the wind, flickering) on the building's roof / side, a hot core glow,
// embers drifting up, a dark smoke column whose foot is lit orange by the fire, a flickering warm point
// light on the building and the ground (the light list), soot on the ground around the building. Flames
// use Blend::Glow (over, not darkened by the time of day): orange stays orange at night. Particle rates
// scale with setDetail (Quality). Render-only: the World is only read.

#include "render/effects.hpp"

#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

using namespace fx;

namespace {
constexpr float kHold = 1.3f;    // s a point keeps full heat after its last event (> two event periods)
constexpr float kFade = 2.6f;    // s it then takes to die down
constexpr float kPi2 = 2 * kPi;

// The damaged building whose art box holds the drawn point (x, y) (its smoke timer runs).
bool burningAt(const game::World& w, const formats::Templates& t, float x, float y, int& side, int& index) {
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& sd = w.sides()[std::size_t(s)];
        for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
            const auto& b = sd.buildings[std::size_t(i)];
            if (!b.alive || b.smokeTimer == 0 || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
            const auto& tp = t.bin.buildings[b.type];
            const float x0 = float(b.tileX * 32) - 8, x1 = float(b.tileX * 32 + tp.artWidth) + 8;
            const float y0 = float(b.tileY * 32) - 48, y1 = float(b.tileY * 32 + tp.artHeight) + 8;
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            side = s;
            index = i;
            return true;
        }
    }
    return false;
}

// The occupied cells' box of a building type (world px from its top-left tile corner).
void footprintBox(const formats::BuildingTemplate& tp, float& x0, float& y0, float& x1, float& y1) {
    std::uint32_t mask = tp.tilesOccupiedSolid ? tp.tilesOccupiedSolid : tp.tilesOccupiedAll;
    int minX = 3, minY = 3, maxX = 0, maxY = 0;
    bool any = false;
    for (int c = 0; c < 16; ++c)
        if ((mask >> c) & 1) {
            any = true;
            minX = std::min(minX, c % 4);
            maxX = std::max(maxX, c % 4);
            minY = std::min(minY, c / 4);
            maxY = std::max(maxY, c / 4);
        }
    if (!any) {
        minX = minY = 0;
        maxX = std::max(1, int(tp.artWidth) / 32) - 1;
        maxY = std::max(1, int(tp.artHeight) / 32) - 1;
    }
    x0 = float(minX * 32);
    y0 = float(minY * 32);
    x1 = float((maxX + 1) * 32);
    y1 = float((maxY + 1) * 32);
}

// 0 at half health .. 1 at a tenth (and below).
float severity(float hp) { return std::clamp((0.5f - hp) / 0.4f, 0.0f, 1.0f); }
}  // namespace

bool Effects::feedBurn(const game::World& w, float x, float y, bool flame) {
    int side = -1, index = -1;
    if (!burningAt(w, t_, x, y, side, index)) return false;
    const auto& b = w.sides()[std::size_t(side)].buildings[std::size_t(index)];
    BurnSite* s = nullptr;
    for (BurnSite& o : burns_)
        if (o.side == side && o.index == index && o.type == b.type && o.tileX == b.tileX && o.tileY == b.tileY) s = &o;
    if (!s) {
        burns_.push_back(BurnSite{});
        s = &burns_.back();
        s->side = side;
        s->index = index;
        s->type = b.type;
        s->tileX = b.tileX;
        s->tileY = b.tileY;
        s->seed = hash32(std::uint32_t(side * 1000 + index) * 2654435761u ^ std::uint32_t(b.tileX * 131 + b.tileY));
    }
    const float ox = x - float(b.tileX * 32), oy = y - float(b.tileY * 32);
    BurnPoint* p = nullptr;
    for (int i = 0; i < s->points; ++i)
        if (std::fabs(s->pt[i].ox - ox) < 4 && std::fabs(s->pt[i].oy - oy) < 4) p = &s->pt[i];
    if (!p) {
        if (s->points >= 8) return true;
        p = &s->pt[s->points++];
        *p = BurnPoint{};
        p->ox = ox;
        p->oy = oy;
        p->seed = hash32(s->seed ^ std::uint32_t(s->points) * 0x9E3779B1u);
        // The tongues: spread along the roof edge / side around the point, the first one on it.
        p->tongues = 4;
        for (int k = 0; k < 4; ++k) {
            const float a = hashf(p->seed, std::uint32_t(k) * 2 + 1), c = hashf(p->seed, std::uint32_t(k) * 2 + 2);
            p->tx[k] = k == 0 ? 0.0f : (k % 2 ? -1.0f : 1.0f) * (3.0f + 6.0f * a) * (1 + 0.3f * float(k / 2));
            p->ty[k] = k == 0 ? 0.0f : (c - 0.5f) * 5.0f;
        }
    }
    p->flame = p->flame || flame;
    p->since = 0;
    p->heat = 1;
    return true;
}

void Effects::burnPlace(const game::World& w, const BurnSite& s, int i, float& gx, float& gy, float& z) const {
    // 3D with a model that names its fire points: those.
    if (anchors_ && anchors_->buildingAnchors(w, s.side, s.index, "fx_fire", anchorTmp_) && !anchorTmp_.empty()) {
        const EffectAnchor& a = anchorTmp_[std::size_t(i) % anchorTmp_.size()];
        gx = a.x;
        gy = a.y;
        z = a.z;
        return;
    }
    const auto& tp = t_.bin.buildings[s.type];
    float fx0, fy0, fx1, fy1;
    footprintBox(tp, fx0, fy0, fx1, fy1);
    const float bx = float(s.tileX * 32), by = float(s.tileY * 32);
    fx0 += bx, fx1 += bx, fy0 += by, fy1 += by;
    const float south = by + float(tp.artHeight);
    const float ex = bx + s.pt[i].ox, ey = by + s.pt[i].oy;
    const float d = std::max(0.0f, south - ey);  // drawn px above the art's south edge
    gx = std::clamp(ex, fx0 + 5, fx1 - 5);
    if (cardTilt_ >= 0) {
        // 3D: start from the same split as 2D, then onto the model's roof near there.
        gy = std::clamp(south - 0.55f * d, fy0 + 5, fy1 - 4);
        if (anchors_ && anchors_->buildingSurface(w, s.side, s.index, gx, gy, z)) return;
        // Sprite cards stand upright on the south edge facing the camera: d px up the art is
        // d cos(tilt) north of the edge and d sin(tilt) high (the refinery vents' convention).
        gy = std::max(fy0, south - d * std::cos(cardTilt_));
        z = d * std::sin(cardTilt_);
        return;
    }
    // 2D: keep the original's drawn point (gy - z = ey) but split it into a ground point inside the
    // footprint and a height, so the fire stands on the building (its roof or its front), never
    // beside it. The drawn point stays inside the solid cells' box (the redesigned art covers it with
    // its roof and front; the original art's tall top rows - chimneys, sky - may be empty there), so
    // very high points come down onto the roof's north edge.
    gy = std::clamp(south - 0.55f * d, fy0 + 5, fy1 - 4);
    z = std::clamp(gy - ey, 0.0f, std::max(0.0f, gy - (fy0 + 2)));
}

// One flame tongue: a coloured tongue sprite (texture: white-yellow core, orange, dark red tips) that
// leaves (x, y, z), rises with the heat, thins out, leans with the wind and flickers as it cools.
void Effects::flameLick(float x, float y, float z, float s, float lean) {
    Particle p{};
    p.x = x + rnd(-2.0f, 2.0f) * s;
    p.y = y + rnd(-1, 1);
    p.z = z + rnd(-1, 1) * s;
    p.vx = kWindX * 0.7f + rnd(-3, 3) * s;
    p.vy = kWindY * 0.3f + rnd(-1.5f, 1.5f);
    p.vz = rnd(10, 18) * std::sqrt(s);
    p.drag = 0.8f;
    p.life = rnd(0.42f, 0.7f) * (0.85f + 0.2f * s);
    p.size0 = rnd(12, 16) * s;  // width
    p.size1 = rnd(5, 7) * s;
    p.len = rnd(17, 26) * s;    // height (texture u: base -> tip)
    p.rot = -kPi / 2 + lean * rnd(0.6f, 1.1f) + rnd(-0.12f, 0.12f);
    p.vrot = rnd(-0.35f, 0.35f);
    p.c0 = rgba(255, 255, 255, int(rnd(200, 235)));
    p.c1 = rgba(255, 110, 60, 0);
    p.fx = kFxHot;
    p.tex = rnd() < 0.5f ? kTongue : kTongue2;
    p.blend = Blend::Glow;
    // The strip is drawn centred on its point: start part of a tongue up so the base sits on (x, y, z).
    p.z += p.len * 0.36f;
    add(p);
    // The body of the fire: a billowing orange mass at the tongues' feet that darkens to red as it
    // rises (gives the flames volume between the tongues).
    if (rnd() < 0.55f) {
        Particle b{};
        b.x = x + rnd(-3, 3) * s;
        b.y = y + rnd(-1, 1);
        b.z = z + rnd(1, 4) * s;
        b.vx = kWindX * 0.6f + rnd(-3, 3);
        b.vy = rnd(-1, 1);
        b.vz = rnd(12, 20) * std::sqrt(s);
        b.drag = 0.9f;
        b.life = rnd(0.35f, 0.55f);
        b.size0 = rnd(9, 12) * s;
        b.size1 = rnd(5, 8) * s;
        b.c0 = rgba(255, 168, 60, int(rnd(170, 210)));
        b.c1 = rgba(150, 34, 8, 0);
        b.fx = kFxHot;
        b.tex = kFire;
        b.blend = Blend::Glow;
        b.rot = rnd(0, 2 * kPi);
        b.vrot = rnd(-2, 2);
        add(b);
    }
}

void Effects::updateBurns(const game::World& w, float dt, const Camera& cam, int viewW, int viewH, bool showShroud) {
    const float vx0 = cam.x - 128, vy0 = cam.y - 128;
    const float vx1 = cam.x + viewW / cam.zoom + 128, vy1 = cam.y + viewH / cam.zoom + 196;
    auto shrouded = [&](float x, float y) {
        const int tx = int(x) >> 5, ty = int(y) >> 5;
        return showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
    };
    const float ld = load() * detail_;
    for (std::size_t si = 0; si < burns_.size();) {
        BurnSite& s = burns_[si];
        // The building: still standing (a dying one hands over to the wreck's own fire).
        s.alive = false;
        if (s.side < int(w.sides().size())) {
            const auto& sd = w.sides()[std::size_t(s.side)];
            if (s.index >= 0 && s.index < int(sd.buildings.size())) {
                const auto& b = sd.buildings[std::size_t(s.index)];
                if (b.alive && b.type == s.type && b.tileX == s.tileX && b.tileY == s.tileY && b.state != 17) {
                    s.alive = true;
                    const auto& tp = t_.bin.buildings[s.type];
                    s.hp = tp.hitPoints ? std::clamp(float(b.health) / float(tp.hitPoints), 0.0f, 1.0f) : 0.3f;
                }
            }
        }
        float maxHeat = 0;
        for (int i = 0; i < s.points; ++i) {
            BurnPoint& p = s.pt[i];
            if (!s.alive) p.heat = 0;
            if (dt > 0) {
                p.since += dt;
                if (p.since > kHold) p.heat = std::max(0.0f, 1 - (p.since - kHold) / kFade);
            }
            maxHeat = std::max(maxHeat, p.heat);
        }
        const float sev = severity(s.hp);
        if (dt > 0) {
            if (maxHeat > 0) s.scorch = std::min(1.0f, s.scorch + dt * maxHeat * (0.05f + 0.12f * sev));
            else s.scorch = std::max(0.0f, s.scorch - dt * (s.alive ? 0.01f : 0.04f));
        }
        if (maxHeat <= 0 && s.scorch <= 0) {
            burns_[si] = burns_.back();
            burns_.pop_back();
            continue;
        }
        ++si;
        if (maxHeat <= 0) continue;
        const auto& tp = t_.bin.buildings[s.type];
        const float cxs = float(s.tileX * 32) + float(tp.artWidth) * 0.5f, cys = float(s.tileY * 32) + float(tp.artHeight) * 0.5f;
        if (cxs < vx0 || cxs > vx1 || cys < vy0 || cys > vy1 || shrouded(cxs, cys)) continue;
        // The light: one flickering warm point per building at its fires' heat-weighted centre.
        float lx = 0, ly = 0, lz = 0, lw = 0, total = 0;
        for (int i = 0; i < s.points; ++i) {
            BurnPoint& p = s.pt[i];
            if (p.heat <= 0) continue;
            float gx, gy, gz;
            burnPlace(w, s, i, gx, gy, gz);
            s.gx[i] = gx;
            s.gy[i] = gy;
            s.gz[i] = gz;
            // How big this point burns: flame points by the damage; smoke points smoke, and catch
            // fire below ~30% health.
            const float fire = p.flame ? (0.55f + 0.85f * sev) : (sev > 0.5f ? 0.35f + 0.7f * (sev - 0.5f) : 0.0f);
            const float h = p.heat;
            const float flick = 0.82f + 0.1f * std::sin(time_ * 11.3f + float(p.seed & 63)) +
                                0.08f * std::sin(time_ * 23.7f + float((p.seed >> 6) & 63));
            if (fire > 0 && dt > 0) {
                const float s1 = fire * (0.6f + 0.4f * h);
                const int nt = std::clamp(int(1 + ld * (1.2f + 1.6f * fire)), 1, 4);
                const float lean = 0.2f + 0.08f * std::sin(time_ * 0.9f + float(p.seed & 15));  // a gentle wind
                for (int k = 0; k < nt; ++k) {
                    const float sk = s1 * (k == 0 ? 1.0f : 0.72f - 0.08f * float(k));
                    p.lickAcc[k] += dt * (16 + 8 * fire) * ld * h * flick;
                    while (p.lickAcc[k] >= 1) {
                        p.lickAcc[k] -= 1;
                        flameLick(gx + p.tx[k] * s1, gy + p.ty[k] * 0.6f, gz, sk, lean);
                    }
                }
                // The hot core: a small yellow-white glow at the base (additive: yellow + blue night
                // = pale yellow, never pink) and a wider orange halo over (Glow).
                Particle g{};
                g.x = gx;
                g.y = gy;
                g.z = gz + 3 * s1;
                g.life = 1;
                g.tex = kGlow;
                g.blend = Blend::Add;
                g.size0 = g.size1 = 11 * s1 * flick;
                g.c0 = g.c1 = rgba(255, 214, 120, int(150 * h * flick));
                beams_.push_back(g);
                g.blend = Blend::Glow;
                g.z = gz + 5 * s1;
                g.size0 = g.size1 = 26 * s1 * (0.9f + 0.1f * flick);
                g.c0 = g.c1 = rgba(255, 128, 36, int(60 * h * flick));
                beams_.push_back(g);
                // Embers drifting up and away.
                p.emberAcc += dt * (1.5f + 4 * fire) * ld * h;
                while (p.emberAcc >= 1) {
                    p.emberAcc -= 1;
                    Particle e{};
                    e.x = gx + rnd(-5, 5) * s1;
                    e.y = gy + rnd(-2, 2);
                    e.z = gz + rnd(4, 12) * s1;
                    e.vx = kWindX * 1.6f + rnd(-8, 8);
                    e.vy = kWindY + rnd(-4, 4);
                    e.vz = rnd(28, 56) * std::sqrt(s1);
                    e.gravity = -14;
                    e.drag = 0.7f;
                    e.life = rnd(0.9f, 1.8f);
                    e.size0 = rnd(1.6f, 2.4f);
                    e.size1 = 0.8f;
                    e.c0 = rgba(255, 214, 130, 255);
                    e.c1 = rgba(255, 70, 15, 0);
                    e.tex = kSpark;
                    e.blend = Blend::Add;
                    e.vrot = rnd(-6, 6);
                    add(e);
                }
                // The smoke's foot, lit orange from below by the flames (Glow: the fire's own light).
                p.litAcc += dt * 3.0f * ld * h;
                while (p.litAcc >= 1) {
                    p.litAcc -= 1;
                    Particle q{};
                    q.x = gx + rnd(-3, 3) * s1;
                    q.y = gy + rnd(-1.5f, 1.5f);
                    q.z = gz + 14 * s1;
                    q.vx = kWindX * 1.2f + rnd(-3, 3);
                    q.vy = kWindY;
                    q.vz = rnd(16, 24);
                    q.drag = 0.3f;
                    q.life = rnd(0.8f, 1.1f);
                    q.size0 = 9 * s1;
                    q.size1 = 22 * s1;
                    q.c0 = rgba(122, 58, 22, int(150 * h));
                    q.c1 = rgba(52, 40, 34, 0);
                    q.tex = kSmoke;
                    q.blend = Blend::Glow;
                    q.rot = rnd(0, kPi2);
                    q.vrot = rnd(-0.6f, 0.6f);
                    add(q);
                }
                lx += gx * fire * h;
                ly += (gy - gz) * fire * h;
                lz += gz * fire * h;
                lw += fire * h;
                total += fire * h;
            }
            // The column: dark smoke rising from every point, thicker, blacker and higher with the
            // damage (just below half health: a light grey wisp).
            const float smoke = (p.flame ? 0.75f : 0.5f) + 0.9f * sev;
            if (dt > 0) p.smokeAcc += dt * (2.2f + 3.2f * smoke) * ld * h;
            while (p.smokeAcc >= 1) {
                p.smokeAcc -= 1;
                Particle q{};
                const float base = fire > 0 ? 16 * fire : 2.0f;
                q.x = gx + rnd(-3, 3);
                q.y = gy + rnd(-1.5f, 1.5f);
                q.z = gz + base + rnd(0, 3);
                q.vx = kWindX * 1.7f + rnd(-3, 3);
                q.vy = kWindY + rnd(-2, 2);
                q.vz = rnd(18, 28) * (0.8f + 0.35f * sev);
                q.drag = 0.25f;
                q.life = rnd(2.4f, 3.4f) * (0.75f + 0.45f * sev);
                q.size0 = rnd(9, 12) * (0.8f + 0.3f * fire);
                q.size1 = rnd(36, 50) * (0.7f + 0.5f * sev);
                const int g = int(rnd(26, 40) + 62 * (1 - sev));
                q.c0 = rgba(g + 4, g + 1, g - 2, int(rnd(150, 185) + 50 * sev));
                q.c1 = rgba(std::min(255, g + 66), std::min(255, g + 63), std::min(255, g + 60), 0);
                q.tex = kSmoke;
                q.blend = Blend::Alpha;
                q.rot = rnd(0, kPi2);
                q.vrot = rnd(-0.5f, 0.5f);
                add(q);
            }
        }
        if (lw > 0) {
            const float flick = 0.78f + 0.12f * std::sin(time_ * 9.7f + float(s.seed & 63)) +
                                0.1f * std::sin(time_ * 21.3f + float((s.seed >> 8) & 63));
            const float big = std::min(total, 2.4f);
            // Stronger in the dark (2D: the time of day's grade; 3D lights add to the dark scene anyway).
            const float lum = graded_ ? 0.3f * grade_[0] + 0.5f * grade_[1] + 0.2f * grade_[2] : 1.0f;
            const float night = std::clamp(1 + 2.4f * (0.85f - lum), 1.0f, 2.4f);
            lights_.push_back({lx / lw, ly / lw - 6, (56 + 40 * big) * (0.94f + 0.06f * flick) * (0.8f + 0.2f * night),
                               rgba(255, 140, 50, int(std::min(255.0f, (36 + 32 * big) * flick * night))), lz / lw + 6});
        }
    }
}

// Soot on the ground around a burning building: dark blotches under its fires along the footprint's
// edges (ground marks: drawn under the buildings, so only the part outside the walls shows).
void Effects::burnMarks() {
    for (const BurnSite& s : burns_) {
        if (s.scorch <= 0.01f || s.type < 0 || s.type >= formats::kMaxBuildingTypes) continue;
        const auto& tp = t_.bin.buildings[s.type];
        float fx0, fy0, fx1, fy1;
        footprintBox(tp, fx0, fy0, fx1, fy1);
        const float bx = float(s.tileX * 32), by = float(s.tileY * 32);
        fx0 += bx, fx1 += bx, fy0 += by, fy1 += by;
        for (int i = 0; i < s.points; ++i) {
            const BurnPoint& p = s.pt[i];
            const float ex = std::clamp(bx + p.ox, fx0 + 6, fx1 - 6);
            const float a = s.scorch * (p.flame ? 1.0f : 0.6f);
            const float r = (14 + 10 * hashf(p.seed, 7)) * (0.6f + 0.4f * s.scorch);
            const float rot = hashf(p.seed, 9) * kPi2;
            // under the fire at the south wall, and a second patch towards the nearer side wall
            marks_.push_back({ex, fy1 - 2, r * 1.3f, r * 0.8f, rot, kScorch, Blend::Alpha, rgba(24, 18, 14, int(150 * a))});
            const float side = (ex - fx0) < (fx1 - ex) ? fx0 + 2 : fx1 - 2;
            marks_.push_back({side, std::clamp(by + p.oy + 16, fy0 + 8, fy1 - 8), r * 0.7f, r, rot + 1.3f, kScorch, Blend::Alpha,
                              rgba(28, 22, 16, int(110 * a))});
        }
    }
}

}  // namespace d2k::render
