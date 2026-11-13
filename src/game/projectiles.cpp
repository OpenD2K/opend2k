// Bullets and explosions (re/notes/combat.md §3-4): ModelAddBullet 0x456a30, UpdateBullet
// 0x49be60, ModelAddExplosion 0x456df0, UpdateExplosion 0x49dd80, sin/cos tables 0x44b090.
// Also MoveUnitSimple 0x495560 (units walking out of a building: survivors, factories).

#include "game/geometry.hpp"
#include "game/tables.hpp"
#include "game/world.hpp"

#include <array>
#include <cmath>
#include <cstdlib>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;
using namespace weapon_flags;

namespace {
inline int px16(std::int32_t v) { return v / 65536; }
inline int div32(int v) { return v / 32; }

struct SinCos {
    std::array<std::int32_t, kSinCosSize> s{}, c{};
};
const SinCos& sinCos() {
    static const SinCos t = [] {
        SinCos r;
        for (int m = 0; m < kSinCosMags; ++m)
            for (int d = 0; d < kSinCosDirs; ++d) {
                const double a = static_cast<double>(d) * 6.283185308 * 0.03125;  // constants @0x4c93c0
                const double mag = static_cast<double>(m * 0x800);
                r.s[static_cast<std::size_t>(d * kSinCosMags + m)] = static_cast<std::int32_t>(std::sin(a) * mag);
                r.c[static_cast<std::size_t>(d * kSinCosMags + m)] = static_cast<std::int32_t>(std::cos(a) * mag);
            }
        return r;
    }();
    return t;
}
}  // namespace

std::int32_t sinTableFlat(int i) { return i >= 0 && i < kSinCosSize ? sinCos().s[static_cast<std::size_t>(i)] : 0; }
std::int32_t cosTableFlat(int i) { return i >= 0 && i < kSinCosSize ? sinCos().c[static_cast<std::size_t>(i)] : 0; }

// ---------------------------------------------------------------------------------------
// Template tables

std::size_t World::explosionCount() const {
    const auto& T = *rules_.templates;
    return T.explosions.empty() ? 0 : T.explosions.size();
}
const formats::ExplosionTemplate& World::explosionTemplate(int type) const {
    return rules_.templates->explosions[static_cast<std::size_t>(type)];
}
int World::animationFrames(int type) const {
    const auto& v = rules_.templates->animationArtFrames;
    return static_cast<std::size_t>(type) < v.size() ? v[static_cast<std::size_t>(type)] : 0;
}
std::uint32_t World::animationFlags(int type) const {
    const auto& v = rules_.templates->animationArtFlags;
    return static_cast<std::size_t>(type) < v.size() ? v[static_cast<std::size_t>(type)] : 0;
}
// Indexed by the weapon (bullet type), not by its projectile art (faithful).
int World::projectileDirections(int w) const { return rules_.templates->bin.projectileArtDirections[w]; }

// ---------------------------------------------------------------------------------------
// Explosions

// ModelAddExplosion 0x456df0. Flags = animationArtFlags[type] XOR `flags`; with 0x20 in
// `flags` the explosion starts with `delay` (and stays hidden until its first frame step);
// with kDrift the explosion moves once by (driftX, driftY).
int World::addExplosion(int side, int type, int x, int y, std::int32_t z, std::uint32_t flags, int delay,
                        std::int32_t driftX, std::int32_t driftY) {
    type &= 0xff;
    if (type > 0x3f || static_cast<std::size_t>(type) >= explosionCount()) return -1;  // "explosionType > kMax"
    Side& s = sides_[side & 7];
    const int idx = s.allocObject();
    if (idx < 0) return -1;
    Explosion& e = s.explosions[idx];
    e = Explosion{};
    e.alive = true;
    e.index = idx;
    e.side = side & 7;
    e.type = type;
    e.posX = x << 16;
    e.posY = y << 16;
    e.posZ = z;
    e.frame = 0;
    e.unitIndex = kNoIndex;
    e.delay = (flags & explosion_flags::kHidden) ? static_cast<std::uint8_t>(delay) : 0;
    e.flags = animationFlags(type) ^ flags;
    if (e.flags & explosion_flags::kDrift) {
        e.driftX = driftX;
        e.driftY = driftY;
    }
    s.linkObject(idx, SlotKind::Explosion);
    return idx;
}

// UpdateExplosion 0x49dd80: frames advance every 3 ticks (delay 2,1,0); the explosion ends
// after animationArtFrames[type] frames. Blinking muzzle flashes live 8 ticks. Returns false
// when the slot is to be freed.
bool World::updateExplosion(Explosion& e) {
    if (e.unitIndex != kNoIndex) {
        const Side& s = sides_[e.side];
        if (s.links[e.unitIndex].kind == SlotKind::Unit) {
            if (s.units[e.unitIndex].state == UnitState::Dead) return false;
        } else {
            e.unitIndex = kNoIndex;
        }
    }
    std::uint32_t f = e.flags;
    if (f & explosion_flags::kDrift) {
        e.posX += e.driftX;
        e.driftX = 0;
        e.posY += e.driftY;
        e.driftY = 0;
    }
    const std::uint8_t d = e.delay;
    if (f & explosion_flags::kBlink) {
        const int pattern = explosionTemplate(e.type).muzzleFlashPattern;
        if (pattern & (1 << (d & 0x1f))) e.flags = f & ~explosion_flags::kBlinkOff;
        else e.flags = f | explosion_flags::kBlinkOff;
        e.delay = static_cast<std::uint8_t>(d + 1);
        return e.delay != 8;
    }
    if (d == 0) {
        e.flags = f & ~explosion_flags::kHidden;
        e.delay = 2;
        e.frame = static_cast<std::uint8_t>(e.frame + 1);
        if (animationFrames(e.type) <= e.frame) {
            e.frame = 0;
            return false;
        }
    } else {
        e.delay = static_cast<std::uint8_t>(d - 1);
    }
    if (e.flags & explosion_flags::kRises) e.posZ += 0x20000;
    return true;
}

// ---------------------------------------------------------------------------------------
// Bullets

// ModelAddBullet 0x456a30. Straight flight: (SpeedX, SpeedY) = speed * delta / dist with
// dist = trunc(sqrt) in pixels, MoveSteps = (dist << 16) / speed (range-limited weapons:
// (range << 16) / speed). Arcs start with FallSpeed = -steps * 0x1000; falling bullets start
// at the firer's height; homing doubles MoveSteps. PosZ starts at 6 px.
int World::addBullet(int side, int w, int delay, int firer, int srcX, int srcY, int dstX, int dstY, int homingIndex,
                     int homingSide) {
    if (w < 0 || w > 0x3f) return -1;  // DebugFatal "bulletType > kMaxAllowableBullet"
    Side& s = sides_[side & 7];
    const int idx = s.allocObject();
    if (idx < 0) return -1;
    const auto& wt = weapon(w);
    Bullet& b = s.bullets[idx];
    b = Bullet{};
    b.index = idx;
    b.side = side & 7;
    b.firerIndex = firer & 0xffff;
    b.posX = (srcX & 0xffff) << 16;
    b.posY = (srcY & 0xffff) << 16;
    b.targetX = static_cast<std::uint16_t>(dstX);
    b.targetY = static_cast<std::uint16_t>(dstY);
    b.type = w;
    b.delay = delay;
    b.posZ = 0x60000;
    b.flags = wt.flags;
    b.byte1B = wt._unknown19;
    b.homingIndex = homingIndex & 0xffff;
    b.homingSide = static_cast<std::int8_t>(homingSide);
    const int dx = b.targetX - px16(b.posX), dy = b.targetY - px16(b.posY);
    const int dist = static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    const std::uint32_t speed = wt.projectileSpeed ? wt.projectileSpeed : 1;  // (division by zero otherwise)
    std::int32_t steps = static_cast<std::int32_t>(dist << 16) / static_cast<std::int32_t>(speed);
    if (wt.flags & kRangeLimited) steps = static_cast<std::int32_t>((wt.range << 16) / speed);
    b.speed = speed;
    b.moveSteps = steps;
    if (dist == 0) {
        // Quirk: a non-falling bullet with zero distance is never linked; the slot leaks.
        if (!(wt.flags & kFalling)) return -1;
        b.speedX = b.speedY = 0;
    } else {
        b.speedX = static_cast<std::int32_t>(speed * static_cast<std::uint32_t>(dx)) / dist;
        b.speedY = static_cast<std::int32_t>(speed * static_cast<std::uint32_t>(dy)) / dist;
    }
    if (wt.flags & kArc) {
        b.fallSpeed = steps * -0x2000 / 2;
        b.flags = wt.flags | kShadow;
    }
    if (b.flags & kFalling) {
        b.flags |= kShadow;
        // The firer's PosZ (the slot read as a unit; debris passes a building index).
        const int fi = firer & 0xffff;
        b.fallSpeed = 0;
        b.posZ = (fi < kObjectsPerSide && s.links[fi].kind == SlotKind::Unit) ? s.units[fi].posZ : 0;
    }
    if (b.flags & kHoming) b.moveSteps = steps * 2;
    const int dirs = projectileDirections(w);
    if (dirs < 2 || (b.flags & kAnimated) || dirs > 32) {
        b.animFrame = b.dir = 0;
    } else {
        const int f = getFacing(srcX & 0xffff, srcY & 0xffff, dstX & 0xffff, dstY & 0xffff);
        b.animFrame = b.dir = f / (32 / dirs);
    }
    b.prevDir = b.dir;
    b.alive = true;
    s.linkObject(idx, SlotKind::Bullet);
    shots_.push_back({gameTicks_, b.side, w, delay, b.firerIndex, srcX & 0xffff, srcY & 0xffff, b.targetX, b.targetY});
    return idx;
}

// UpdateBullet 0x49be60. Returns false when the bullet is to be freed (it exploded or left
// the map). Order per tick: firer check, start delay, map bounds, wall stop, range-limited
// damage, trail, then homing flight or straight/arc/falling flight, impact.
bool World::updateBullet(Bullet& b) {
    Side& s = sides_[b.side];
    if (b.firerIndex != kNoIndex) {
        const auto kind = s.links[b.firerIndex].kind;
        const bool alive = (kind == SlotKind::Unit && s.units[b.firerIndex].state != UnitState::Dead) ||
                           (kind == SlotKind::Building && s.buildings[b.firerIndex].state != 17);
        if (!alive) b.firerIndex = kNoIndex;
    }
    if (b.delay != 0) {
        --b.delay;
        return true;
    }
    auto onMap = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && div32(px16(x)) < width_ && y >= 0 && div32(px16(y)) < height_;
    };
    if (!onMap(b.posX, b.posY)) return false;
    const auto& wt = weapon(b.type);
    const int firer = b.firerIndex;
    const bool deviate = (b.flags & kDeviator) != 0;
    auto impact = [&](int x, int y, std::int32_t z) {
        const int hit = wt.hitExplosion;
        if (hit < 0) return;
        if (static_cast<std::size_t>(hit) < explosionCount()) playSound(explosionTemplate(hit).sound, x >> 5, y >> 5);
        addExplosion(b.side, hit, x, y, z);
    };
    {
        const int bx = px16(b.posX), by = px16(b.posY);
        if ((b.flags & kStopsAtWalls) && (cell(div32(bx), div32(by)).flags & kWall)) {
            damageTiles(bx, by, b.posZ, b.type, b.side, firer, deviate);
            impact(bx, by, b.posZ);
            return false;
        }
        if (b.flags & kRangeLimited) {
            // Second half of the flight: damage every tick along the way (flamethrower).
            const std::uint32_t total = wt.projectileSpeed ? (wt.range << 16) / wt.projectileSpeed : 0;
            if (static_cast<std::uint32_t>(b.moveSteps) << 1 < total)
                damageTiles(bx, by, b.posZ, b.type, b.side, firer, false);
        }
        if (b.flags & kTrail) addExplosion(b.side, static_cast<std::uint8_t>(wt.trailExplosion), bx, px16(b.posY), b.posZ);
    }

    if (b.flags & kHoming) {
        std::int32_t targetZ = 0;
        if (b.homingIndex != kNoIndex && b.homingSide != -1) {
            // The original reads the slot as a unit whatever it holds now.
            const Unit& t = sides_[b.homingSide & 7].units[b.homingIndex];
            b.targetX = static_cast<std::uint16_t>(px16(t.posX));
            b.targetY = static_cast<std::uint16_t>(px16(t.posY));
            targetZ = t.posZ;
            if (t.state == UnitState::Dead) {
                b.homingIndex = kNoIndex;
                b.homingSide = -1;
            }
        }
        const std::int32_t tyFix = static_cast<std::int32_t>(static_cast<std::uint32_t>(b.targetY) << 16);
        const std::int32_t txFix = static_cast<std::int32_t>(static_cast<std::uint32_t>(b.targetX) << 16);
        const int dty = div32(px16(tyFix)) - div32(px16(b.posY));
        const int dtx = div32(px16(txFix)) - div32(px16(b.posX));
        const int d2 = dty * dty + dtx * dtx;
        // Turn rate (direction steps per tick) grows as the target gets closer (tiles^2).
        const int turns = d2 >= 17 ? 1 : d2 >= 15 ? 2 : d2 >= 13 ? 3 : d2 >= 11 ? 4 : d2 >= 9 ? 6 : d2 >= 7 ? 8 : 32;
        b.speed = wt.projectileSpeed;
        const int n = static_cast<std::uint8_t>(projectileDirections(b.type));
        if (n == 0 || n > 32) return false;  // division by zero in the original
        const int ang = atan4096(px16(b.posX - txFix), px16(tyFix - b.posY));
        const int want = ang / (0x1000 / n);
        int cur = static_cast<std::uint8_t>(b.dir);
        for (int k = 0; k < turns; ++k) {
            if (cur == want) continue;
            const std::uint8_t cw = static_cast<std::uint8_t>((want + n - cur) % n);
            const std::uint8_t ccw = static_cast<std::uint8_t>((cur - want + n) % n);
            cur = static_cast<std::uint8_t>(((cw < ccw) ? cur + n + 1 : cur + n - 1) % n);
        }
        if (std::abs(b.prevDir - cur) == 16 && b.moveSteps != 0) b.moveSteps = 1;  // overshot: last step
        b.dir = b.prevDir = cur;
        if (b.flags & kAnimated) b.animFrame = (b.animFrame + 1) % projectileDirections(b.type);
        else b.animFrame = cur;
        const int idx = (32 / n) * cur * kSinCosMags + px16(static_cast<std::int32_t>(b.speed << 5));
        b.posX += sinTableFlat(idx);
        b.posY -= cosTableFlat(idx);
        if (!onMap(b.posX, b.posY)) return false;
        if (targetZ != 0) {
            std::int32_t dz = (targetZ - b.posZ) / 2;
            if (dz >= 1) {
                if (dz > 0x20000) dz = 0x20000;
            } else if (dz != 0 && dz < -0x20000) {
                dz = -0x20000;
            }
            b.posZ += dz;
        }
        const std::int32_t left = b.moveSteps--;
        if (left != 0) {
            // Explodes when within the same 8x8 px block as the target (or out of steps).
            auto block = [](std::int32_t v) { return (static_cast<std::int32_t>(static_cast<std::uint32_t>(v) + 4) / 8) / 65536; };
            if (block(txFix) != block(b.posX)) return true;
            if (block(tyFix) != block(b.posY)) return true;
        }
        damageTiles(px16(b.posX), px16(b.posY), b.posZ, b.type, b.side, firer, deviate);
        impact(px16(b.posX), px16(b.posY), b.posZ);
        return false;
    }

    if (b.flags & kAnimated) b.animFrame = (b.animFrame + 1) % projectileDirections(b.type);
    bool landed = false;
    if (!(b.flags & kFalling)) {
        if (b.moveSteps == 0) landed = true;
        else --b.moveSteps;
    } else {
        const std::int32_t fs = b.fallSpeed + 0x2000;
        b.fallSpeed = fs;
        b.targetX = static_cast<std::uint16_t>(px16(b.posX));
        b.targetY = static_cast<std::uint16_t>(px16(b.posY));
        b.posZ -= fs;
        if (b.posZ < 1) {
            if (!(b.flags & kBounce) || (rng_.next() & 3) == 1 || static_cast<double>(b.fallSpeed) <= 6553.6) {
                b.posZ = 0;
                landed = true;
            } else {
                b.fallSpeed = -(b.fallSpeed / 2);  // bounce
            }
        }
    }
    if (!landed) {
        if (b.flags & kArc) {
            const std::int32_t v = b.fallSpeed;
            b.fallSpeed = v + 0x2000;
            b.posZ -= v + 0x2000;
        }
        b.posX += b.speedX;
        b.posY += b.speedY;
        return true;
    }
    if (!onMap(b.posX, b.posY)) return false;
    const int tx = b.targetX, ty = b.targetY;
    if (b.type == rules_.templates->bin.groupIds[50]) {
        // Spice bloom shell: puts spice on the (sandy) target cell.
        const int cx = tx >> 5, cy = ty >> 5;
        if (inBounds(cx, cy)) {
            auto spiceAt = [&](int x, int y) { return inBounds(x, y) && (cell(x, y).flags & kSpiceMask); };
            bool thick = spiceAt(cx, cy);
            for (int yy = cy - 1; yy <= cy + 1 && thick; ++yy)
                for (int xx = cx - 1; xx <= cx + 1 && thick; ++xx) thick = spiceAt(xx, yy);
            Cell& c = cell(cx, cy);
            c.tile = 0x31a;
            c.damage = 0;
            if (!thick) c.flags = (c.flags & 0xffafffffu) | 0x200000u;  // spice level 2
            else c.flags = (c.flags & 0xffcfffffu) | 0x400000u;         // spice level >= 4
            updateSpiceInRegion(cx - 1, cy - 1, cx + 2, cy + 2);
        }
    }
    if ((tx >> 5) < width_ && (ty >> 5) < height_) damageTiles(tx, ty, b.posZ, b.type, b.side, firer, deviate);
    // Quirk: the impact explosion's height argument is PosZ >> 16 stored as 16.16 (~0 px).
    impact(tx, ty, px16(b.posZ));
    return false;
}

// ---------------------------------------------------------------------------------------
// MoveUnitSimple 0x495560: straight-line movement to Target, one tile at a time, without
// path finding (units walking out of a building). releaseFrom/markTo: update the cell flags
// of the tile left / entered.

bool World::moveUnitSimple(Unit& u, bool releaseFrom, bool markTo) {
    const auto& t = unitTemplate(u);
    const bool inf = t.isInfantry != 0;
    if (u.posSteps == 0) {
        if (u.blockFromX == u.targetX && u.blockFromY == u.targetY) return false;
        if (u.state != UnitState::Infiltrating && !canUnitUseSquare(u.targetX, u.targetY, u, u.side, 0)) return true;
        int nx = u.blockFromX, ny = u.blockFromY;
        stepInDirection(nx, ny, getDir8(u.blockFromX, u.blockFromY, u.targetX, u.targetY));
        u.blockToX = nx;
        u.blockToY = ny;
        if (!inBounds(nx, ny)) return true;  // DebugFatal "Invalid next"
        std::uint32_t& to = cell(nx, ny).flags;
        if (!inf) {
            u.stepTargetX = nx * 0x200000 + 0x100000;
            u.stepTargetY = ny * 0x200000 + 0x100000;
            const int ddx = std::abs(u.posX - u.stepTargetX), ddy = std::abs(u.posY - u.stepTargetY);
            const std::uint32_t dist = static_cast<std::uint32_t>(ddy < ddx ? ddx : ddy);
            const std::uint8_t steps = static_cast<std::uint8_t>(dist / (t.speed ? t.speed : 1));
            u.posSteps = steps;
            u.posStepsMax = static_cast<std::uint8_t>(steps >> 1);
            if (markTo) to = (to & ~kOwnerMask) | static_cast<std::uint32_t>(u.side) | kVehicle;
        } else {
            static constexpr int kSpot[5][2] = {{16, 21}, {23, 14}, {23, 26}, {7, 26}, {7, 14}};  // 0x4d7028
            int spot = u.cellspotFrom;
            if (to & kOccupied & (0x20u << spot)) {
                std::uint32_t bit = 0x200;
                spot = 4;
                while (bit >= 0x20 && (bit & to & kOccupied)) {
                    bit >>= 1;
                    --spot;
                }
                if (bit < 0x20) spot = 0;  // DebugFatal "Can't find a free slot"
            }
            u.stepTargetX = (kSpot[spot][0] + nx * 32) * 0x10000;
            u.stepTargetY = (kSpot[spot][1] + ny * 32) * 0x10000;
            u.cellspotTo = spot;
            const int ddx = std::abs(u.posX - u.stepTargetX), ddy = std::abs(u.posY - u.stepTargetY);
            const std::uint32_t dist = static_cast<std::uint32_t>(ddy < ddx ? ddx : ddy);
            const std::uint8_t steps = static_cast<std::uint8_t>(dist / (u.speed ? u.speed : 1));
            u.posSteps = steps;
            u.posStepsMax = static_cast<std::uint8_t>(steps >> 1);
            if (markTo) {
                to = (to & ~kOwnerMask) | (0x20u << spot) | static_cast<std::uint32_t>(u.side);
                to = (to & 0xF1FFFFFFu) | (static_cast<std::uint32_t>(u.side) << kInfantryOwnerShift);
            }
        }
        if (markTo) u.flags |= kBlockToMarked;
    }
    const int dir = (u.blockFromX == u.blockToX && u.blockFromY == u.blockToY)
                        ? u.facing
                        : getDir8(u.blockFromX, u.blockFromY, u.blockToX, u.blockToY);
    if (u.facing != dir) {
        rotateBody(u, dir);
        if (t.primaryWeaponUseBarrel) rotateTurret(u, dir);
        return true;
    }
    if (inf) setInfantryAnim(u, 1);
    const std::int32_t sp = static_cast<std::int32_t>(t.speed), half = sp / 2;
    const int x = px16(u.posX);
    if (x < px16(u.stepTargetX - half)) u.posX += sp;
    else if (px16(u.stepTargetX + half) < x) u.posX -= sp;
    const int y = px16(u.posY);
    if (y < px16(u.stepTargetY - half)) u.posY += sp;
    else if (px16(u.stepTargetY + half) < y) u.posY -= sp;
    if (t.primaryWeaponUseBarrel) rotateTurret(u, dir);
    u.posSteps = static_cast<std::uint8_t>(u.posSteps - 1);
    if (u.posSteps < u.posStepsMax) {
        u.posStepsMax = 0;
        if (releaseFrom) {
            std::uint32_t& from = cell(u.blockFromX, u.blockFromY).flags;
            if (!inf) {
                from &= ~kVehicle;
                if (from & kInfantryMask) from = (from & ~kOwnerMask) | ((from >> kInfantryOwnerShift) & 7);
            } else {
                from &= ~(0x20u << u.cellspotFrom);
                u.cellspotFrom = u.cellspotTo;
            }
        }
        u.blockFromX = u.blockToX;
        u.blockFromY = u.blockToY;
        u.flags &= ~kBlockToMarked;
    }
    return true;
}

}  // namespace d2k::game
