// Effects intensity (render/fx_budget.hpp, docs/enhanced-graphics.md "Effects intensity"): units stay readable
// under smoke, and lasting smoke columns merge / are capped per screen area. Render-only.
#include "render/effects.hpp"

#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace d2k::render {

using namespace fx;

float Effects::budgetAlpha(const Particle& p) { return FxBudget::capAlpha(p.age, p.cut) * (1 - p.clear); }

namespace {
struct Pin {
    float x, y, r, keep;  // drawn centre (world px), radius, the lowest opacity a puff keeps over it
};
}  // namespace

// Every alpha-blended puff (smoke, dust, soot, gas) eases its `clear` towards how much it covers a unit: the
// closer the unit is to the puff's middle, the more see-through it gets, down to FxBudget::seeThrough (lower over
// the player's selected units). Cinematic keeps a light version. Additive / light particles are left alone.
void Effects::updateSeeThrough(const game::World& w, float dt) {
    if (dt <= 0) return;
    static thread_local std::vector<Pin> pins;
    static thread_local std::vector<int> head, next;
    pins.clear();
    const int me = w.mySide();
    for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
        const auto& side = w.sides()[s];
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            if (u.flags & 0x200000) continue;  // inside something (carried / in a building)
            const auto& tp = t_.bin.units[u.type];
            const bool sel = u.selected && s == me;
            const float r = tp.isInfantry ? 8.0f : 17.0f;
            pins.push_back({float(u.posX) / 65536.0f, float(u.posY) / 65536.0f - (tp.isInfantry ? 4.0f : 6.0f), r,
                            budget_.seeThrough(sel)});
        }
        // The player's selected buildings (their health bar / frame must stay readable too).
        if (s == me)
            for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.buildings[i];
                if (!b.alive || !b.selected || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
                const auto& tp = t_.bin.buildings[b.type];
                const float bw = std::max(32.0f, float(tp.artWidth)), bh = std::max(32.0f, float(tp.artHeight));
                pins.push_back({float(b.tileX * 32) + bw * 0.5f, float(b.tileY * 32) + bh * 0.5f, 0.45f * std::max(bw, bh),
                                0.5f * (1 + budget_.seeThrough(true))});
            }
    }
    const float ease = 1 - std::exp(-dt * 10.0f);
    if (pins.empty()) {
        for (Particle& p : particles_)
            if (p.clear > 0) p.clear -= p.clear * ease;
        return;
    }
    // A 64 px grid over the pins' bounds.
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Pin& p : pins) {
        x0 = std::min(x0, p.x - p.r);
        y0 = std::min(y0, p.y - p.r);
        x1 = std::max(x1, p.x + p.r);
        y1 = std::max(y1, p.y + p.r);
    }
    constexpr float kCell = 64;
    const int gw = std::clamp(int((x1 - x0) / kCell) + 1, 1, 512), gh = std::clamp(int((y1 - y0) / kCell) + 1, 1, 512);
    head.assign(std::size_t(gw * gh), -1);
    next.assign(pins.size(), -1);
    auto cellOf = [&](float x, float y, int& cx, int& cy) {
        cx = std::clamp(int((x - x0) / kCell), 0, gw - 1);
        cy = std::clamp(int((y - y0) / kCell), 0, gh - 1);
    };
    for (int i = 0; i < int(pins.size()); ++i) {
        int cx, cy;
        cellOf(pins[std::size_t(i)].x, pins[std::size_t(i)].y, cx, cy);
        next[std::size_t(i)] = head[std::size_t(cy * gw + cx)];
        head[std::size_t(cy * gw + cx)] = i;
    }
    for (Particle& p : particles_) {
        if (p.blend != Blend::Alpha || !(p.tex == kSmoke || p.tex == kSoft || p.tex == kFire || p.tex == kGas) || p.age < 0) {
            continue;
        }
        const float t = p.age / p.life, e = 1 - (1 - t) * (1 - t);
        const float r = 0.5f * (p.size0 + (p.size1 - p.size0) * e);
        const float px = p.x, py = p.y - p.z;
        float target = 0;
        if (px + r + 40 >= x0 && px - r - 40 <= x1 && py + r + 40 >= y0 && py - r - 40 <= y1) {
            int ca, cb, cc, cd;
            cellOf(px - r - 40, py - r - 40, ca, cb);
            cellOf(px + r + 40, py + r + 40, cc, cd);
            for (int cy = cb; cy <= cd; ++cy)
                for (int cx = ca; cx <= cc; ++cx)
                    for (int i = head[std::size_t(cy * gw + cx)]; i >= 0; i = next[std::size_t(i)]) {
                        const Pin& u = pins[std::size_t(i)];
                        const float reach = 0.85f * r + u.r;
                        const float d = std::hypot(u.x - px, u.y - py);
                        if (d >= reach) continue;
                        float cover = 1 - d / reach;
                        cover = std::min(1.0f, cover * 1.6f);
                        target = std::max(target, cover * (1 - u.keep));
                    }
        }
        p.clear += (target - p.clear) * ease;
        if (p.clear < 1e-3f) p.clear = 0;
    }
}

// Burning vehicle wrecks (fires_ with a smoke column): only so many columns smoke at once per screen area; past
// the cap the oldest ones die down gently (their smoke share eases away, the flames burn on).
void Effects::capColumns(float dt, const Camera& cam, int viewW, int viewH) {
    if (!budget_.on() || dt <= 0) return;
    const float vx0 = cam.x - 64, vy0 = cam.y - 64;
    const float vx1 = cam.x + float(viewW) / cam.zoom + 64, vy1 = cam.y + float(viewH) / cam.zoom + 96;
    static thread_local std::vector<int> cols;
    cols.clear();
    for (int i = 0; i < int(fires_.size()); ++i) {
        const Fire& f = fires_[std::size_t(i)];
        if (f.smoke > 0.3f && f.x >= vx0 && f.x <= vx1 && f.y >= vy0 && f.y <= vy1) cols.push_back(i);
    }
    const int cap = budget_.columnCap(areaK_);
    if (int(cols.size()) <= cap) return;
    // The youngest keep their columns.
    std::sort(cols.begin(), cols.end(), [&](int a, int b) { return fires_[std::size_t(a)].age < fires_[std::size_t(b)].age; });
    for (std::size_t k = std::size_t(cap); k < cols.size(); ++k) {
        Fire& f = fires_[std::size_t(cols[k])];
        f.smoke = std::max(0.31f, f.smoke * std::exp(-dt * 0.8f));  // still a "column" (keeps its look), thinner
        f.life = std::min(f.life, f.age + 6.0f);
    }
}

}  // namespace d2k::render
