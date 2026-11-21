#include "render/order_feedback.hpp"

#include <algorithm>
#include <cmath>

#include "game/world.hpp"

namespace d2k::render {

namespace {
constexpr double kDrawnFrame = 1.0 / 60.0;  // the original's drawn frame (cnc-ddraw maxfps 60)
constexpr float kMoveLife = 0.6f, kAttackLife = 0.75f, kOutsideLife = 0.8f, kHoverLife = 0.45f;
}  // namespace

// AddCursorPuffAnimationToQueue 0x456d50: the first finished entry (frame >= frame count) takes
// the puff; when all ten are running, the one with the highest frame (the oldest) is replaced.
// The original then clears the delay of entry 10 (one past the array, an index slip) instead of
// the chosen one, so a replaced entry keeps its delay counter - reproduced.
void OrderFeedback::addPuff(int wx, int wy) {
    if (puffFirst_ < 0 || puffFrames_ <= 0) return;
    for (Puff& p : puffs_)
        if (p.frame >= puffFrames_) {
            p = {wx, wy, 0, 0};
            return;
        }
    std::size_t oldest = 0;
    int best = 0;
    for (std::size_t i = 0; i < puffs_.size(); ++i)
        if (best < puffs_[i].frame) {
            best = puffs_[i].frame;
            oldest = i;
        }
    puffs_[oldest].x = wx;
    puffs_[oldest].y = wy;
    puffs_[oldest].frame = 0;
}

void OrderFeedback::addMove(float wx, float wy, bool attackGround) {
    Marker m;
    m.kind = attackGround ? Marker::AttackGround : Marker::Move;
    m.x = wx;
    m.y = wy;
    markers_.push_back(m);
}

void OrderFeedback::addAttackMove(float wx, float wy) {
    Marker m;
    m.kind = Marker::AttackMove;
    m.x = wx;
    m.y = wy;
    markers_.push_back(m);
}

void OrderFeedback::addAttackUnit(int side, int index) {
    std::erase_if(markers_, [&](const Marker& m) { return m.kind == Marker::AttackUnit && m.side == side && m.index == index; });
    Marker m;
    m.kind = Marker::AttackUnit;
    m.side = side;
    m.index = index;
    markers_.push_back(m);
}

void OrderFeedback::addAttackBuilding(int side, int index) {
    std::erase_if(markers_,
                  [&](const Marker& m) { return m.kind == Marker::AttackBuilding && m.side == side && m.index == index; });
    Marker m;
    m.kind = Marker::AttackBuilding;
    m.side = side;
    m.index = index;
    markers_.push_back(m);
}

void OrderFeedback::addOutside(float wx, float wy) {
    std::erase_if(markers_, [](const Marker& m) { return m.kind == Marker::Outside; });  // one at a time
    Marker m;
    m.kind = Marker::Outside;
    m.x = wx;
    m.y = wy;
    markers_.push_back(m);
}

void OrderFeedback::hoverOutside(float wx, float wy) {
    hoverX_ = wx;
    hoverY_ = wy;
    hoverLeft_ = kHoverLife;
}

// Display__DrawCursorPuffs 0x428220, after the blit: delay + 1; past 2 -> delay 0, frame + 1.
void OrderFeedback::stepPuffs() {
    for (Puff& p : puffs_) {
        if (p.frame >= puffFrames_) continue;
        if (++p.delay > 2) {
            p.delay = 0;
            ++p.frame;
        }
    }
}

void OrderFeedback::update(double dt) {
    if (dt <= 0) return;
    puffClock_ = std::min(puffClock_ + dt, 8 * kDrawnFrame);
    while (puffClock_ >= kDrawnFrame) {
        puffClock_ -= kDrawnFrame;
        stepPuffs();
    }
    for (Marker& m : markers_) m.age += float(dt);
    hoverLeft_ = std::max(0.0f, hoverLeft_ - float(dt));
    std::erase_if(markers_, [](const Marker& m) {
        return m.age >= (m.kind == Marker::Outside ? kOutsideLife
                         : m.kind == Marker::Move || m.kind == Marker::AttackGround || m.kind == Marker::AttackMove ? kMoveLife
                                                                                                                 : kAttackLife);
    });
}

void OrderFeedback::clear() {
    puffs_ = {};
    puffClock_ = 0;
    markers_.clear();
    hoverLeft_ = 0;
}

namespace {

// A ground ring (world radii r0..r1 around cx, cy) through the projection, as one triangle list.
void ring(SDL_Renderer* r, const std::function<bool(float, float, float&, float&)>& project, float cx, float cy,
          float r0, float r1, SDL_FColor c, float squash = 1.0f) {
    constexpr int kSeg = 32;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve((kSeg + 1) * 2);
    for (int i = 0; i <= kSeg; ++i) {
        const float a = float(i) * 6.2831853f / kSeg, ca = std::cos(a), sa = std::sin(a) * squash;
        SDL_Vertex in{}, out{};
        if (!project(cx + ca * r0, cy + sa * r0, in.position.x, in.position.y) ||
            !project(cx + ca * r1, cy + sa * r1, out.position.x, out.position.y))
            return;
        in.color = out.color = c;
        v.push_back(in);
        v.push_back(out);
    }
    for (int i = 0; i < kSeg; ++i) {
        const int a = i * 2;
        idx.insert(idx.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

float easeOut(float t) { return 1 - (1 - t) * (1 - t); }

// A cross lying on the ground (two bars of half length len, half width wid, world px) through the projection.
void cross(SDL_Renderer* r, const std::function<bool(float, float, float&, float&)>& project, float cx, float cy, float len,
           float wid, SDL_FColor c) {
    SDL_Vertex v[8];
    constexpr float k = 0.70710678f;
    for (int b = 0; b < 2; ++b) {
        const float dx = k, dy = b ? -k : k;  // the bar's direction; its normal is (-dy, dx)
        const float ex[4] = {-len, len, len, -len}, ew[4] = {-wid, -wid, wid, wid};
        for (int i = 0; i < 4; ++i) {
            SDL_Vertex& q = v[b * 4 + i];
            q = SDL_Vertex{};
            if (!project(cx + dx * ex[i] - dy * ew[i], cy + dy * ex[i] + dx * ew[i], q.position.x, q.position.y)) return;
            q.color = c;
        }
    }
    static constexpr int idx[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    SDL_RenderGeometry(r, nullptr, v, 8, idx, 12);
}

}  // namespace

void OrderFeedback::drawModern(SDL_Renderer* r, const game::World& w,
                               const std::function<bool(float, float, float&, float&)>& project) const {
    if (markers_.empty() && hoverLeft_ <= 0) return;
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (hoverLeft_ > 0) {  // the pointer over the land beyond the map: a faint red cross
        const float a = 0.5f * std::min(1.0f, hoverLeft_ / (kHoverLife * 0.6f));
        cross(r, project, hoverX_ + 0.8f, hoverY_ + 0.8f, 7.6f, 2.0f, SDL_FColor{0, 0, 0, 0.5f * a});
        cross(r, project, hoverX_, hoverY_, 7.0f, 1.3f, SDL_FColor{0.95f, 0.2f, 0.16f, a});
    }
    for (const Marker& m : markers_) {
        if (m.kind == Marker::Outside) {  // a click beyond the map: the cross pops, then fades
            const float t = std::clamp(m.age / kOutsideLife, 0.0f, 1.0f);
            const float a = t < 0.5f ? 1.0f : 1.0f - (t - 0.5f) / 0.5f;
            const float len = 7.0f + 4.0f * easeOut(std::min(1.0f, t * 4.0f)) - 1.5f * t;
            cross(r, project, m.x + 1, m.y + 1, len + 0.8f, 2.6f, SDL_FColor{0, 0, 0, 0.55f * a});
            cross(r, project, m.x, m.y, len, 1.8f, SDL_FColor{1.0f, 0.16f, 0.12f, 0.95f * a});
            continue;
        }
        if (m.kind == Marker::Move || m.kind == Marker::AttackGround || m.kind == Marker::AttackMove) {
            // A ring closing in on the point (16 -> 5 px), fading over its last 40 %, plus a second,
            // later ring for a "landing" pulse; a small dot at the centre.
            const float t = std::clamp(m.age / kMoveLife, 0.0f, 1.0f);
            const float a = t < 0.6f ? 1.0f : 1.0f - (t - 0.6f) / 0.4f;
            // Green = move, red = attack the ground, orange = attack-move (not original).
            const SDL_FColor col = m.kind == Marker::AttackGround ? SDL_FColor{1.0f, 0.32f, 0.22f, 0.95f * a}
                                   : m.kind == Marker::AttackMove ? SDL_FColor{1.0f, 0.58f, 0.12f, 0.95f * a}
                                                                  : SDL_FColor{0.45f, 1.0f, 0.45f, 0.95f * a};
            const SDL_FColor shadow{0, 0, 0, 0.45f * a};
            const float rad = 16.0f - 11.0f * easeOut(t);
            ring(r, project, m.x + 1, m.y + 1, rad - 1.5f, rad + 1.0f, shadow, 0.75f);
            ring(r, project, m.x, m.y, rad - 1.2f, rad + 0.6f, col, 0.75f);
            if (t > 0.25f) {
                const float t2 = (t - 0.25f) / 0.75f, r2 = 14.0f - 10.0f * easeOut(t2);
                SDL_FColor c2 = col;
                c2.a *= 0.55f;
                ring(r, project, m.x, m.y, r2 - 0.8f, r2 + 0.4f, c2, 0.75f);
            }
            ring(r, project, m.x, m.y, 0, 1.6f, col, 0.75f);
            continue;
        }
        // Attacked unit / building: a red ring pulsing twice around it (follows it while alive).
        if (m.side < 0 || m.side >= int(w.sides().size())) continue;
        const auto& s = w.sides()[std::size_t(m.side)];
        float cx = 0, cy = 0, rad = 14;
        if (m.kind == Marker::AttackUnit) {
            if (m.index < 0 || m.index >= int(s.units.size()) || !s.units[std::size_t(m.index)].alive) continue;
            const game::Unit& u = s.units[std::size_t(m.index)];
            cx = float(game::fixToPixel(u.posX));
            cy = float(game::fixToPixel(u.posY));
            rad = w.unitTemplate(u).isInfantry ? 9.0f : 15.0f;
        } else {
            if (m.index < 0 || m.index >= int(s.buildings.size()) || !s.buildings[std::size_t(m.index)].alive) continue;
            const game::Building& b = s.buildings[std::size_t(m.index)];
            const auto& t = w.rules().templates->bin.buildings[b.type];
            cx = float(game::fixToPixel(b.posX)) + t.artWidth / 2.0f;
            cy = float(game::fixToPixel(b.posY)) - t.artHeight / 2.0f;
            rad = std::max(t.artWidth, t.artHeight) * 0.42f + 2.0f;
        }
        const float t = std::clamp(m.age / kAttackLife, 0.0f, 1.0f);
        const float pulse = std::fmod(t * 2.0f, 1.0f);  // two pulses
        const float a = (1.0f - pulse) * (t < 0.5f ? 1.0f : 0.8f);
        const float rr = rad * (1.25f - 0.25f * easeOut(pulse));
        ring(r, project, cx + 1, cy + 1, rr - 2.0f, rr + 1.0f, SDL_FColor{0, 0, 0, 0.4f * a});
        ring(r, project, cx, cy, rr - 1.6f, rr + 0.6f, SDL_FColor{1.0f, 0.25f, 0.2f, 0.95f * a});
    }
    SDL_SetRenderDrawBlendMode(r, prev);
}

}  // namespace d2k::render
