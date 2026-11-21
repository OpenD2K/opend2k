#include "render/motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace d2k::render {

namespace {

// Longest per-tick move that is still drawn as motion (16.16 px). Units move a few pixels per
// tick; carried / unloaded / teleported units jump further. Projectiles fly faster.
constexpr std::int64_t kMaxUnitJump = std::int64_t(48) << 16;
constexpr std::int64_t kMaxBulletJump = std::int64_t(128) << 16;

std::int32_t lerp(std::int32_t a, std::int32_t b, float t) {
    return a + static_cast<std::int32_t>(static_cast<double>(std::int64_t(b) - a) * t);
}

bool near(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by, std::int64_t lim) {
    return std::llabs(std::int64_t(ax) - bx) <= lim && std::llabs(std::int64_t(ay) - by) <= lim;
}

}  // namespace

void MotionInterpolator::reset() {
    havePrev_ = false;
    saved_.clear();
    for (auto& d : drawn_) d.clear();
}

void MotionInterpolator::beforeTick(const game::World& w) {
    const auto& sides = w.sides();
    for (int s = 0; s < game::kMaxSides; ++s) {
        const game::Side& side = sides[s];
        auto& pu = units_[s];
        auto& pb = bullets_[s];
        auto& pe = explosions_[s];
        pu.assign(side.units.size(), Prev{});
        pb.assign(side.bullets.size(), Prev{});
        pe.assign(side.explosions.size(), Prev{});
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const game::Unit& u = side.units[i];
            pu[i] = Prev{u.posX, u.posY, u.posZ, u.type, 0, std::int16_t(u.facing), std::int16_t(u.facingTurret), w.unitHeading(u)};
        }
        for (int i = side.firstBullet; i != -1; i = side.nextObject(i)) {
            const game::Bullet& b = side.bullets[i];
            pb[i] = Prev{b.posX, b.posY, b.posZ, b.type, b.moveSteps};
        }
        for (int i = side.firstExplosion; i != -1; i = side.nextObject(i)) {
            const game::Explosion& e = side.explosions[i];
            pe[i] = Prev{e.posX, e.posY, e.posZ, e.type, 0};
        }
    }
    havePrev_ = true;
}

void MotionInterpolator::apply(game::World& w, float alpha, float easeDt, SmoothDrive* drive) {
    saved_.clear();
    applied_ = true;
    ++frame_;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const bool interp = havePrev_ && alpha < 1.0f;
    const bool ease = easeDt >= 0;
    const double k = easeDt > 0 ? 1.0 - std::exp(-double(easeDt) / kEaseTau) : 0.0;
    constexpr double kSnap = double(kMaxUnitJump);  // farther behind than this: drawn where it is
    auto& sides = w.sides();
    for (int s = 0; s < game::kMaxSides; ++s) {
        game::Side& side = sides[s];
        const auto& pu = units_[s];
        const auto& pb = bullets_[s];
        const auto& pe = explosions_[s];
        auto& dr = drawn_[s];
        if (dr.size() != side.units.size()) dr.assign(side.units.size(), Drawn{});  // (once per game)
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            game::Unit& u = side.units[i];
            std::int32_t x = u.posX, y = u.posY, z = u.posZ;
            if (interp && i < int(pu.size()) && pu[i].key == u.type) {
                const Prev& p = pu[i];
                if (!(p.x == u.posX && p.y == u.posY && p.z == u.posZ) && near(p.x, p.y, u.posX, u.posY, kMaxUnitJump)) {
                    x = lerp(p.x, u.posX, alpha);
                    y = lerp(p.y, u.posY, alpha);
                    z = lerp(p.z, u.posZ, alpha);
                }
            }
            const bool samePrev = havePrev_ && i < int(pu.size()) && pu[i].key == u.type;
            if (drive && u.alive && !(u.flags & 0x200140)) {
                // Smooth driving: ground vehicles (not infantry / worms / a harvester in its docking frames).
                const auto& tp = w.unitTemplate(u);
                if (!tp.isInfantry && tp.specialBehavior != game::behaviour::kSandworm &&
                    !(tp.specialBehavior == 1 && u.animFrame != 0)) {
                    float fx = float(x) / 65536.0f, fy = float(y) / 65536.0f;
                    drive->unit(w, u, samePrev ? pu[i].facing : u.facing, samePrev ? pu[i].turret : u.facingTurret,
                                samePrev ? pu[i].heading : w.unitHeading(u), fx, fy);
                    x = static_cast<std::int32_t>(std::lround(double(fx) * 65536.0));
                    y = static_cast<std::int32_t>(std::lround(double(fy) * 65536.0));
                }
            }
            Drawn& d = dr[std::size_t(i)];
            // A unit that just appeared in this slot (or one not drawn last frame) starts where it is.
            const bool fresh = (havePrev_ && (i >= int(pu.size()) || pu[i].key != u.type)) || d.frame + 1 != frame_;
            d.frame = frame_;
            // Smooth camera: ground units (not aircraft / carried units / worms) ease their drawn
            // position towards the interpolated one.
            const bool ground = ease && u.alive && !(u.flags & 0x200140) &&
                                w.unitTemplate(u).specialBehavior != game::behaviour::kSandworm;
            if (ground && !fresh && d.eased && d.key == u.type && std::abs(d.ex - x) <= kSnap && std::abs(d.ey - y) <= kSnap) {
                d.ex += (x - d.ex) * k;
                d.ey += (y - d.ey) * k;
                if (std::abs(d.ex - x) < 64 && std::abs(d.ey - y) < 64) d.ex = x, d.ey = y;  // within 1/1000 px
                x = static_cast<std::int32_t>(std::lround(d.ex));
                y = static_cast<std::int32_t>(std::lround(d.ey));
            } else {
                d.ex = x;
                d.ey = y;
                d.eased = ground;
            }
            d.key = u.type;
            d.x = x;
            d.y = y;
            d.z = z;
            if (x == u.posX && y == u.posY && z == u.posZ) continue;
            saved_.push_back({1, std::uint8_t(s), std::uint16_t(i), u.posX, u.posY, u.posZ});
            u.posX = x;
            u.posY = y;
            u.posZ = z;
        }
        if (!interp) continue;
        for (int i = side.firstBullet; i != -1; i = side.nextObject(i)) {
            game::Bullet& b = side.bullets[i];
            if (i >= int(pb.size()) || pb[i].key != b.type || b.moveSteps > pb[i].steps) continue;
            const Prev& p = pb[i];
            if ((p.x == b.posX && p.y == b.posY && p.z == b.posZ) || !near(p.x, p.y, b.posX, b.posY, kMaxBulletJump))
                continue;
            saved_.push_back({2, std::uint8_t(s), std::uint16_t(i), b.posX, b.posY, b.posZ});
            b.posX = lerp(p.x, b.posX, alpha);
            b.posY = lerp(p.y, b.posY, alpha);
            b.posZ = lerp(p.z, b.posZ, alpha);
        }
        for (int i = side.firstExplosion; i != -1; i = side.nextObject(i)) {
            game::Explosion& e = side.explosions[i];
            if (i >= int(pe.size()) || pe[i].key != e.type) continue;
            const Prev& p = pe[i];
            if ((p.x == e.posX && p.y == e.posY && p.z == e.posZ) || !near(p.x, p.y, e.posX, e.posY, kMaxUnitJump)) continue;
            saved_.push_back({3, std::uint8_t(s), std::uint16_t(i), e.posX, e.posY, e.posZ});
            e.posX = lerp(p.x, e.posX, alpha);
            e.posY = lerp(p.y, e.posY, alpha);
            e.posZ = lerp(p.z, e.posZ, alpha);
        }
    }
}

void MotionInterpolator::applyDrawn(game::World& w) {
    if (drawnDepth_++ > 0) return;
    savedDrawn_.clear();
    if (applied_) return;
    auto& sides = w.sides();
    for (int s = 0; s < game::kMaxSides; ++s) {
        game::Side& side = sides[s];
        const auto& dr = drawn_[s];
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            game::Unit& u = side.units[i];
            if (i >= int(dr.size()) || dr[std::size_t(i)].key != u.type) continue;
            const Drawn& d = dr[std::size_t(i)];
            if ((d.x == u.posX && d.y == u.posY && d.z == u.posZ) || !near(d.x, d.y, u.posX, u.posY, kMaxUnitJump)) continue;
            savedDrawn_.push_back({1, std::uint8_t(s), std::uint16_t(i), u.posX, u.posY, u.posZ});
            u.posX = d.x;
            u.posY = d.y;
            u.posZ = d.z;
        }
    }
}

void MotionInterpolator::toSim(const game::World& w, float& wx, float& wy) const {
    // The drawn unit nearest the point (within ~2/3 of a tile) decides the offset.
    if (applied_ || drawnDepth_ > 0) return;
    constexpr float kRadius = 20.0f;
    float best = kRadius * kRadius, ox = 0, oy = 0;
    const auto& sides = w.sides();
    for (int s = 0; s < game::kMaxSides; ++s) {
        const game::Side& side = sides[s];
        const auto& dr = drawn_[s];
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const game::Unit& u = side.units[i];
            if (i >= int(dr.size()) || dr[std::size_t(i)].key != u.type) continue;
            const Drawn& d = dr[std::size_t(i)];
            if ((d.x == u.posX && d.y == u.posY) || !near(d.x, d.y, u.posX, u.posY, kMaxUnitJump)) continue;
            const float dx = float(d.x) / 65536.0f - wx, dy = float(d.y) / 65536.0f - wy;
            if (dx * dx + dy * dy >= best) continue;
            best = dx * dx + dy * dy;
            ox = float(u.posX - d.x) / 65536.0f;
            oy = float(u.posY - d.y) / 65536.0f;
        }
    }
    wx += ox;
    wy += oy;
}

void MotionInterpolator::put(game::World& w, const std::vector<Saved>& v) {
    auto& sides = w.sides();
    for (const Saved& sv : v) {
        game::Side& side = sides[sv.side];
        std::int32_t *x, *y, *z;
        if (sv.kind == 1) { auto& o = side.units[sv.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        else if (sv.kind == 2) { auto& o = side.bullets[sv.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        else { auto& o = side.explosions[sv.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        *x = sv.x;
        *y = sv.y;
        *z = sv.z;
    }
}

void MotionInterpolator::restore(game::World& w) {
    put(w, saved_);
    saved_.clear();
    applied_ = false;
}

void MotionInterpolator::restoreDrawn(game::World& w) {
    if (drawnDepth_ == 0 || --drawnDepth_ > 0) return;
    put(w, savedDrawn_);
    savedDrawn_.clear();
}

}  // namespace d2k::render
