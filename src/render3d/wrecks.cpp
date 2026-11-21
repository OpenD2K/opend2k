#include "render3d/wrecks.hpp"

#include "game/world.hpp"
#include "render/effects.hpp"
#include "render3d/flight.hpp"
#include "render3d/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace d2k::render3d {

namespace {

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kTwoPi = 6.28318530718f;
constexpr float kHopG = 260.0f;     // px/s^2: the hull's hop
constexpr float kTurretG = 300.0f;  // px/s^2: the turret's flight
constexpr float kFade = 1.8f;       // s: sinking and crumbling away at the end of a hulk's life
constexpr float kCoverIn = 0.22f;   // s: a hulk a live unit drives into is gone this fast
constexpr float kEdge = 20.0f;      // px: a crashing flyer comes down at least this far inside the map

// The class of a vehicle type: what it carries / how heavy it is, not its name (mods rename units).
Wrecks::Size sizeOf(const formats::UnitTemplate& t) {
    using namespace game::behaviour;
    if (t.specialBehavior == kHarvester || t.specialBehavior == kMcv || t.specialBehavior == kDevastator) return Wrecks::kLarge;
    // Wheeled scouts (trike, quad, raider): light, and they cannot crush infantry.
    if (!t.canCrushInfantry) return Wrecks::kSmall;
    return Wrecks::kMedium;
}

}  // namespace

float Wrecks::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

void Wrecks::reset() {
    for (Hulk& h : hulks_) h.used = false;
    count_ = 0;
    stall_ = 0;
}

Wrecks::Hulk* Wrecks::owner(int side, int index) {
    if (count_ == 0) return nullptr;
    for (Hulk& h : hulks_)
        if (h.used && h.side == side && h.index == index) return &h;
    return nullptr;
}

// A free slot, else the oldest hulk's (the one nearest its end).
Wrecks::Hulk& Wrecks::take() {
    Hulk* best = nullptr;
    float bestLeft = 1e9f;
    for (Hulk& h : hulks_) {
        if (!h.used) {
            ++count_;
            h = Hulk{};
            return h;
        }
        if (const float left = h.life - h.age; left < bestLeft) bestLeft = left, best = &h;
    }
    *best = Hulk{};
    return *best;
}

Wrecks::Pose Wrecks::pose(const Hulk& h) {
    Pose p;
    const float t = std::max(0.0f, h.age);
    const int sz = int(h.size);
    // The blast scorches it at once (the fireball hides the change), the rest of the paint burns off under it.
    p.charK = 0.5f + 0.5f * smooth01(t / 0.5f);
    const float end = std::clamp((t - (h.life - kFade)) / kFade, 0.0f, 1.0f);
    p.fire = (1.0f - 0.8f * smooth01(t / (h.life * 0.8f))) * (1.0f - smooth01(end * 1.6f));
    const float fl = 0.82f + 0.18f * std::sin(t * 9.0f + h.seed[0] * 40.0f) * std::sin(t * 5.3f + h.seed[1] * 23.0f);
    p.ember = (0.3f + 0.7f * (1.0f - smooth01(t / (h.life * 0.65f)))) * (1.0f - smooth01(end * 2.0f)) * fl;
    p.dissolve = std::max(smooth01((end - 0.15f) / 0.85f), h.cover);
    p.turretDissolve = std::max(p.dissolve, h.turretCover);
    if (h.crash) {
        // A shot-down flyer: it keeps some of its way on and arcs down (slower forward, faster down), out of
        // control; on the ground it settles askew, part buried, and burns out like any hulk.
        const float T = std::max(0.05f, h.fallT), tt = std::min(t, T), u = tt / T;
        const float r = std::max(0.0f, t - T), settle = smooth01(r / 0.35f);
        p.air = t < T;
        const float hk = u * (1.4f - 0.4f * u), vk = u * (0.12f + 0.88f * u);
        p.pos = Vec3{h.from.x + (h.to.x - h.from.x) * hk, h.from.y + (h.to.y - h.from.y) * vk, h.from.z + (h.to.z - h.from.z) * hk};
        const float s0 = h.seed[0] < 0.5f ? -1.0f : 1.0f, s1 = h.seed[2] < 0.5f ? -1.0f : 1.0f;
        float sink = 0.2f;
        if (h.cargo) {  // what it carried: let go, tumbling a little
            p.spin = s0 * 0.9f * u;
            p.roll = s1 * (0.7f * u * (1.0f - settle) + (0.05f + 0.1f * h.seed[1]) * settle);
            p.pitch = (0.5f * h.seed[3] + 0.2f) * u * (1.0f - settle) + 0.05f * settle;
            sink = 0.12f;
        } else if (h.heavy) {  // a carryall: nose down, rolling slowly
            p.spin = s0 * 0.5f * u * u;
            p.roll = s1 * (0.42f * u * (1.0f - settle) + 0.2f * settle);
            p.pitch = 0.55f * smooth01(u) * (1.0f - settle) + 0.17f * settle;
            sink = 0.16f;
        } else {  // an ornithopter: a flat spin that winds up, banking over
            p.spin = s0 * (2.2f * tt + 2.4f * tt * tt);
            p.roll = s1 * ((1.0f + 0.4f * h.seed[1]) * smooth01(u * 1.15f) * (1.0f - settle) + 0.45f * settle);
            p.pitch = 0.45f * u * (1.0f - settle) + 0.14f * settle;
        }
        p.centre = 0.5f * h.top * (1.0f - u * u * u);
        p.lift = -sink * h.top * smooth01(r / 0.3f) - 0.42f * h.top * smooth01(end);
        // Burning from the hit on; the impact chars what is left.
        p.charK = p.air ? 0.35f + 0.45f * u : 0.8f + 0.2f * smooth01(r / 0.4f);
        if (p.air) {
            p.fire = 1.0f;
            p.ember = 0.75f * fl;
            p.dissolve = p.turretDissolve = 0;
        } else if (!h.rests) {
            p.dissolve = p.turretDissolve = 1;
        }
        return p;
    }
    // The hull hops with the blast (the lighter the higher) and comes down askew, then sinks at the end.
    const float hopV = sz == kSmall ? 60.0f : sz == kMedium ? 36.0f : 22.0f;
    const float air = 2.0f * hopV / kHopG;
    const float hop = t < air ? hopV * t - 0.5f * kHopG * t * t : 0.0f;
    const float lean = sz == kSmall ? 0.2f : sz == kMedium ? 0.07f : 0.035f;
    const float settle = smooth01(t / air);
    p.roll = (h.seed[0] < 0.5f ? -1.0f : 1.0f) * lean * (0.6f + 0.8f * h.seed[1]) * settle;
    p.pitch = (h.seed[2] < 0.5f ? -1.0f : 1.0f) * lean * 0.5f * (0.4f + 0.8f * h.seed[3]) * settle;
    p.lift = hop - 0.42f * h.top * smooth01(end);
    // The turret: blown up off its ring, tumbling, to lie upside down against the hull.
    if (h.turret) {
        const float a = h.seed[2] * kTwoPi;
        const Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
        // Upside down on the ground its pivot ends this far above where it started (see docs/3d-mode.md).
        const float rest = h.top - 2.0f * h.pivotY + 0.5f;
        float v0 = 104.0f + 30.0f * h.seed[3];
        v0 = std::max(v0, std::sqrt(2.0f * kTurretG * (std::max(0.0f, rest) + 8.0f)));
        const float land = (v0 + std::sqrt(std::max(0.0f, v0 * v0 - 2.0f * kTurretG * rest))) / kTurretG;
        const float u = std::clamp(t - kTurretDelay, 0.0f, land);
        const float reach = 12.0f + 8.0f * h.seed[1] + 3.0f * float(sz);
        const float k = u / land;
        p.turretAir = t < land + kTurretDelay;
        p.turretOff = dir * (reach * k) + Vec3{0.0f, v0 * u - 0.5f * kTurretG * u * u, 0.0f};
        if (!p.turretAir) p.turretOff.y = rest;
        // It tumbles about the axis across its flight (top over), a little more or less than half a turn.
        p.turretAxis = Vec3{-dir.z, 0.0f, dir.x};
        p.turretAngle = (kPi + (h.seed[0] - 0.5f) * 0.5f) * k;
        p.turretSpin = (h.seed[3] - 0.5f) * 2.4f * k;
        p.turretOff.y -= 0.42f * h.top * smooth01(end);  // sinks with the hull
    }
    return p;
}

// A crash (docs/3d-mode.md "Aircraft crashes"): where the flyer u (or, with a carrier, what it carried: u is the
// carried unit, or the carrier itself for a delivery's cargo) falls from and comes down.
void Wrecks::crashOf(Hulk& h, const game::World& w, const Terrain& terr, const game::Unit& u, const Hulk* carrier) {
    h.crash = true;
    const float a = float(u.facing & 31) * (kTwoPi / 32.0f);
    const float dx = std::sin(a), dz = -std::cos(a);  // (facing 0 = north)
    const float maxX = float(w.width() * 32) - kEdge, maxZ = float(w.height() * 32) - kEdge;
    auto ground = [&](float x, float z, Vec3& out) {
        out.x = std::clamp(x, kEdge, std::max(kEdge, maxX));
        out.z = std::clamp(z, kEdge, std::max(kEdge, maxZ));
        out.y = terr.height(out.x, out.z);
        const int cx = int(out.x) >> 5, cz = int(out.z) >> 5;
        return w.inBounds(cx, cz) && !(w.cell(cx, cz).flags & game::cell_flags::kBuilding);
    };
    if (carrier) {
        // Hanging 10 px under the carrier; it comes down a little short of it and to one side.
        h.cargo = true;
        h.from = carrier->from + Vec3{0.0f, -10.0f, 0.0f};
        const float side = h.seed[0] < 0.5f ? -1.0f : 1.0f, off = 15.0f + 3.0f * float(h.size);
        h.rests = ground(carrier->to.x - dx * 9.0f - dz * side * off, carrier->to.z - dz * 9.0f + dx * side * off, h.to);
        if (!h.rests) h.rests = ground(carrier->to.x - dx * 9.0f + dz * side * off, carrier->to.z - dz * 9.0f - dx * side * off, h.to);
        h.fallT = std::max(0.5f, carrier->fallT - 0.07f);
        h.life = h.fallT + 5.0f + rnd() * 0.8f;
    } else {
        const float alt = flyerAltitude(u);
        h.heavy = w.unitTemplate(u).specialBehavior == game::behaviour::kCarryall ||
                  w.unitTemplate(u).specialBehavior == game::behaviour::kFrigate;
        h.size = h.heavy ? kMedium : kSmall;
        h.from = Vec3{h.x, flyerGround(terr, u) + alt, h.y};
        // 1-2 tiles on along its heading (less from low down: a carryall landing / lifting off hardly moves),
        // on open ground: the nearest such point along the heading, else where it is. Never above where it was.
        const float reach = (38.0f + 22.0f * h.seed[1]) * std::clamp((alt - 10.0f) / 40.0f, 0.15f, 1.0f);
        h.rests = false;
        for (const float d : {0.0f, 16.0f, -16.0f, 32.0f, -32.0f, 48.0f}) {
            const float r = std::max(0.0f, reach + d);
            if (ground(h.from.x + dx * r, h.from.z + dz * r, h.to) && h.to.y < h.from.y - 4.0f) {
                h.rests = true;
                break;
            }
        }
        if (!h.rests) ground(h.from.x, h.from.z, h.to);
        h.fallT = std::clamp(0.55f + (h.from.y - h.to.y) / 57.0f, 0.7f, 1.6f);
        h.life = h.fallT + 4.5f + rnd();
    }
    h.to.y = std::min(h.to.y, h.from.y);
    if (!h.rests) h.life = h.fallT + 0.3f;  // on a building: the impact only
    const int cx = int(h.to.x) >> 5, cz = int(h.to.z) >> 5;
    h.sand = w.inBounds(cx, cz) && (w.cell(cx, cz).flags & game::cell_flags::kSand) != 0;
    h.wy = h.from.y;
    h.agl = h.from.y - terr.height(h.from.x, h.from.z);
}

void Wrecks::update(const render::Effects& fx, const game::World& w, const Terrain& terr, float frameDt, const Hidden& hidden,
                    const Modelled& modelled) {
    if (w.gameTicks() < lastTick_) reset();  // a new world (restart, load)
    // The hulks' time follows the game, as the particles' does: no new tick for a while = paused.
    float dt = std::clamp(frameDt, 0.0f, 0.1f);
    if (w.gameTicks() == lastTick_) {
        stall_ += dt;
        if (stall_ > 0.2f) dt = 0;
    } else {
        stall_ = 0;
        lastTick_ = w.gameTicks();
    }
    // Deaths of this frame (see the header: the death flash on the pixel of a ground vehicle in state Dead, or
    // of a flyer in state Dead - the flash of a flyer's death is started on the ground under it).
    using K = render::Effects::FxEvent;
    const auto& units = w.rules().templates->bin.units;
    auto make = [&](const game::Unit& u, int type) -> Hulk& {
        Hulk& h = take();
        h.used = true;
        h.fresh = true;
        h.serial = ++serial_;
        h.side = u.side;
        h.index = u.index;
        h.type = type;
        h.size = sizeOf(units[type]);
        h.x = h.sx = float(u.posX) / 65536.0f;
        h.y = h.sy = float(u.posY) / 65536.0f;
        h.facing = u.facing & 31;
        h.turretFacing = u.facingTurret & 31;
        for (float& s : h.seed) s = rnd();
        h.life = (h.size == kSmall ? 7.0f : h.size == kMedium ? 8.5f : 10.0f) + rnd() * 0.8f;
        return h;
    };
    for (const K& e : fx.events()) {
        if (e.kind != K::VehicleDeath || hidden(e.x, e.y)) continue;
        const game::Unit* dead = nullptr;
        bool flyer = false;
        for (int pass = 0; pass < 2 && !dead; ++pass)  // a ground vehicle first, then a flyer
            for (const game::Side& side : w.sides()) {
                for (int i = side.firstUnit; i >= 0 && !dead; i = side.nextObject(i)) {
                    const game::Unit& u = side.units[std::size_t(i)];
                    if (!u.alive || u.state != game::UnitState::Dead || u.type < 0 || (u.flags & game::unit_flags::kNotDrawn)) continue;
                    if (pass == 0) {
                        if ((u.flags & game::unit_flags::kExcluded) || u.posZ != 0) continue;
                    } else {
                        // (Not the Death Hand: its end is its warhead. Not what a carryall carries: below.)
                        if (!(u.flags & game::unit_flags::kFlying) || (u.flags & game::unit_flags::kDeathHand) ||
                            w.unitTemplate(u).specialBehavior == game::behaviour::kDeathHand || w.unitTemplate(u).deathExplosion < 0 ||
                            !modelled(u.type))
                            continue;
                    }
                    if (w.unitTemplate(u).isInfantry) continue;
                    const float rx = e.x - float(u.posX) / 65536.0f, ry = e.y - float(u.posY) / 65536.0f;
                    if (pass == 0) {
                        if (std::abs(rx) > 2.5f || std::abs(ry) > 2.5f) continue;
                    } else {
                        // A dead flyer flies on along its facing while its dead-state timer runs (UpdateUnit case
                        // DEAD -> FlyUnit) and its flash drifts the same way at its own pace (the explosion's
                        // drift): by the time the flash is seen the two are a few px apart along the heading.
                        const float a = float(u.facing & 31) * (kTwoPi / 32.0f), dx = std::sin(a), dy = -std::cos(a);
                        const float along = rx * dx + ry * dy, across = rx * dy - ry * dx;
                        if (along > 16.0f || along < -40.0f || std::abs(across) > 3.0f || u.deadStateTimer < 5) continue;
                    }
                    if (owner(u.side, u.index)) continue;
                    dead = &u;
                    flyer = pass == 1;
                }
                if (dead) break;
            }
        if (!dead) continue;
        Hulk& h = make(*dead, dead->type);
        if (!flyer) continue;
        h.sx = e.x;  // (the flash: where its events are)
        h.sy = e.y;
        crashOf(h, w, terr, *dead, nullptr);
        // What it carried dies with it (DestroyUnit; a delivery's cargo is never made): it falls too, and a
        // vehicle leaves a hulk of its own. A delivery's cargo is no unit: the first type of its list, as drawn.
        const Hulk carrier = h;  // (a copy: the pool may hand the next hulk this one's slot)
        const game::Side& side = w.sides()[std::size_t(dead->side)];
        if (dead->flags & 0x20000000) {
            const int cargo = side.deliveries[std::size_t(dead->lastMoved % 10)].units[0];
            if (cargo < 0xfe && cargo < int(std::size(units)) && !units[std::size_t(cargo)].isInfantry && modelled(cargo)) {
                Hulk& c = make(*dead, cargo);
                c.index = -1;
                crashOf(c, w, terr, *dead, &carrier);
            }
        } else if ((dead->flags & game::unit_flags::kCarried) && dead->enemyIndex != game::kNoIndex) {
            const game::Unit& v = side.units[std::size_t(dead->enemyIndex) % side.units.size()];
            if (v.alive && v.state == game::UnitState::Dead && v.type >= 0 && (v.flags & game::unit_flags::kCarried) &&
                !(v.flags & game::unit_flags::kFlying) && !units[v.type].isInfantry && modelled(v.type) && !owner(v.side, v.index)) {
                Hulk& c = make(v, v.type);
                c.sx = e.x;
                c.sy = e.y;
                crashOf(c, w, terr, v, &carrier);
            }
        }
    }
    if (count_ == 0) return;
    // Age; a hulk past its life is gone.
    for (Hulk& h : hulks_) {
        if (!h.used) continue;
        h.age += dt;
        if (h.age >= h.life || h.cover >= 0.999f) {
            h.used = false;
            --count_;
            continue;
        }
        if (h.crash) {  // where it is now (the fire, the light and the units driving into it go by x, y)
            const Pose p = pose(h);
            h.x = p.pos.x;
            h.y = p.pos.z;
            h.wy = p.pos.y;
            h.agl = std::max(0.0f, p.pos.y - terr.height(p.pos.x, p.pos.z));
        }
    }
    if (count_ == 0 || dt <= 0) return;
    // A live unit in a hulk (or on its turret): it crumbles away under it at once.
    static thread_local std::array<std::uint8_t, kMax> in, inTurret;
    in.fill(0);
    inTurret.fill(0);
    for (const game::Side& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const game::Unit& u = side.units[std::size_t(i)];
            if (!u.alive || u.type < 0 || u.state == game::UnitState::Dead || (u.flags & game::unit_flags::kExcluded) ||
                (u.flags & game::unit_flags::kNotDrawn))
                continue;
            const float ux = float(u.posX) / 65536.0f, uy = float(u.posY) / 65536.0f;
            const bool inf = w.unitTemplate(u).isInfantry != 0;
            for (std::size_t k = 0; k < hulks_.size(); ++k) {
                const Hulk& h = hulks_[k];
                if (!h.used || (h.crash && h.age < h.fallT)) continue;  // (still in the air: over them)
                const float r = (inf ? 11.0f : 25.0f) + 3.0f * float(h.size);
                if (std::abs(ux - h.x) < r && std::abs(uy - h.y) < r && std::hypot(ux - h.x, uy - h.y) < r) in[k] = 1;
                if (h.turret) {
                    const Pose p = pose(h);
                    const float tx = h.pivot.x + p.turretOff.x, tz = h.pivot.z + p.turretOff.z, tr = inf ? 8.0f : 19.0f;
                    if (!p.turretAir && std::hypot(ux - tx, uy - tz) < tr) inTurret[k] = 1;
                }
            }
        }
    for (std::size_t k = 0; k < hulks_.size(); ++k) {
        Hulk& h = hulks_[k];
        if (!h.used) continue;
        // (It never comes back: what crumbled stays gone.)
        if (in[k]) h.cover = std::min(1.0f, h.cover + dt / kCoverIn);
        else if (h.cover > 0) h.cover = std::min(1.0f, h.cover + dt / (kCoverIn * 3.0f));
        if (inTurret[k] || h.turretCover > 0) h.turretCover = std::min(1.0f, h.turretCover + dt / kCoverIn);
    }
}

}  // namespace d2k::render3d
