#include "render/motion.hpp"

#include <algorithm>
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
            pu[i] = Prev{u.posX, u.posY, u.posZ, u.type, 0};
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

void MotionInterpolator::apply(game::World& w, float alpha) {
    saved_.clear();
    if (!havePrev_) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha >= 1.0f) return;
    auto& sides = w.sides();
    for (int s = 0; s < game::kMaxSides; ++s) {
        game::Side& side = sides[s];
        const auto& pu = units_[s];
        const auto& pb = bullets_[s];
        const auto& pe = explosions_[s];
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            game::Unit& u = side.units[i];
            if (i >= int(pu.size()) || pu[i].key != u.type) continue;
            const Prev& p = pu[i];
            if ((p.x == u.posX && p.y == u.posY && p.z == u.posZ) || !near(p.x, p.y, u.posX, u.posY, kMaxUnitJump)) continue;
            saved_.push_back({1, std::uint8_t(s), std::uint16_t(i), u.posX, u.posY, u.posZ});
            u.posX = lerp(p.x, u.posX, alpha);
            u.posY = lerp(p.y, u.posY, alpha);
            u.posZ = lerp(p.z, u.posZ, alpha);
        }
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

void MotionInterpolator::restore(game::World& w) {
    auto& sides = w.sides();
    for (const Saved& v : saved_) {
        game::Side& side = sides[v.side];
        std::int32_t *x, *y, *z;
        if (v.kind == 1) { auto& o = side.units[v.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        else if (v.kind == 2) { auto& o = side.bullets[v.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        else { auto& o = side.explosions[v.index]; x = &o.posX; y = &o.posY; z = &o.posZ; }
        *x = v.x;
        *y = v.y;
        *z = v.z;
    }
    saved_.clear();
}

}  // namespace d2k::render
