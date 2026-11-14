#include "render/vehicle_motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace d2k::render {

namespace {

constexpr float kPi = 3.14159265f;

// A damped spring towards `target`: x'' = w^2 (target - x) - 2 zeta w x' (semi-implicit Euler in
// steps of at most 1/120 s, so a long frame does not blow it up).
void spring(float& x, float& v, float target, float hz, float zeta, float dt) {
    const float w = 2 * kPi * hz;
    while (dt > 0) {
        const float h = std::min(dt, 1.0f / 120);
        v += (w * w * (target - x) - 2 * zeta * w * v) * h;
        x += v * h;
        dt -= h;
    }
}

// a -> b with time constant tau.
float lowPass(float a, float b, float tau, float dt) { return a + (b - a) * (1 - std::exp(-dt / tau)); }

std::string upperName(const char* s, std::size_t n) {
    std::string k(s, strnlen(s, n));
    for (char& c : k) c = char(std::toupper(static_cast<unsigned char>(c)));
    return k;
}

}  // namespace

VehicleMotion::VehicleMotion(const formats::Templates& t) : t_(t) {
    // Recoil strength per unit type, by its gun (the hull of a siege tank rocks the most, a
    // trike's machine gun barely shakes it).
    kick_.assign(formats::kMaxUnitTypes, 0.0f);
    for (int i = 0; i < int(t.bin.unitCount) && i < formats::kMaxUnitTypes; ++i) {
        const auto& u = t.bin.units[i];
        if (u.isInfantry || u.primaryWeapon < 0) continue;
        const std::string n = upperName(t.bin.unitNames[i], sizeof t.bin.unitNames[i]);
        auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
        float k = 0.5f;
        if (has("SIEGE")) k = 1.4f;
        else if (has("DEVASTATOR")) k = 1.2f;
        else if (has("MISSILE") || has("DEVIATOR")) k = 0.45f;
        else if (has("SONIC")) k = 0.35f;
        else if (has("TRIKE") || has("QUAD") || has("RAIDER")) k = 0.14f;
        else if (u.barrelArt >= 0) k = 1.0f;  // combat tanks
        kick_[std::size_t(i)] = k;
    }
}

float VehicleMotion::rnd() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) * (1.0f / 16777216.0f);
}

void VehicleMotion::reset() {
    for (auto& v : units_) v.clear();
    primed_ = false;
}

const VehicleMotion::Pose* VehicleMotion::pose(int side, int index) const {
    if (side < 0 || side >= game::kMaxSides || index < 0 || index >= int(units_[side].size())) return nullptr;
    const State& s = units_[side][std::size_t(index)];
    return s.key >= 0 ? &s.out : nullptr;
}

void VehicleMotion::update(const game::World& w, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    time_ += dt;
    const std::uint32_t now = w.gameTicks();
    if (!primed_ || now < lastShotTick_) lastShotTick_ = now;
    primed_ = true;
    for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
        const auto& side = w.sides()[std::size_t(s)];
        auto& st = units_[s];
        if (st.size() != side.units.size()) st.assign(side.units.size(), State{});
        for (std::size_t i = 0; i < side.units.size(); ++i)
            if (!side.units[i].alive) st[i].key = -1;
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const game::Unit& u = side.units[std::size_t(i)];
            State& v = st[std::size_t(i)];
            if (!u.alive || u.type < 0 || u.type >= formats::kMaxUnitTypes) {
                v.key = -1;
                continue;
            }
            const auto& tp = t_.bin.units[u.type];
            // Ground vehicles only (not infantry, flyers, carried / hidden units, sandworms); a
            // harvester at the refinery (its docking frames) holds still.
            const bool ground = !tp.isInfantry && !(u.flags & 0x200140) && tp.specialBehavior != 5 &&
                                !(tp.specialBehavior == 1 && u.animFrame != 0);
            const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
            const float face = float(u.facing & 31) * (2 * kPi / 32);
            const std::int32_t key = u.type + 1;
            if (!ground) {
                v.key = -1;
                continue;
            }
            if (v.key != key) {
                v = State{};
                v.key = key;
                v.x = x;
                v.y = y;
                v.heading = face;
                v.terrain = -1;
                continue;
            }
            const float dx = x - v.x, dy = y - v.y, dist = std::hypot(dx, dy);
            v.x = x;
            v.y = y;
            if (dt <= 0) continue;  // paused: hold the pose
            if (dist > 48) {        // teleported / reused: start over
                v.vFast = v.vSlow = 0;
                continue;
            }
            // Speed: the instantaneous one low-passed twice. The original starts / stops at full
            // speed, so the difference of the two is the "acceleration" the hull reacts to.
            const float vRef = std::max(15.0f, float(tp.speed) / 65536.0f * 20.0f);
            const float vNow = dist / dt;
            v.vFast = lowPass(v.vFast, vNow, 0.06f, dt);
            v.vSlow = lowPass(v.vSlow, v.vFast, 0.28f, dt);
            const float accel = std::clamp((v.vFast - v.vSlow) / vRef, -1.3f, 1.3f);
            const float moving = std::clamp(v.vFast / vRef, 0.0f, 1.2f);
            // Turn rate: the (32-step) body facing as a continuous heading (eased towards each new
            // step, as the drawn facing is), its rate low-passed - the steps themselves would give
            // the roll a ripple at the turning rate.
            float dh = face - std::fmod(v.heading, 2 * kPi);
            while (dh > kPi) dh -= 2 * kPi;
            while (dh < -kPi) dh += 2 * kPi;
            const float turned = std::abs(dh) > kPi / 2 ? dh : dh * (1 - std::exp(-dt / 0.06f));  // (a jump: no spin)
            v.heading += turned;
            v.omega = lowPass(v.omega, std::abs(dh) > kPi / 2 ? 0.0f : turned / dt, 0.15f, dt);
            const float omega = std::clamp(v.omega, -6.0f, 6.0f);
            // Pitch: squat on starting (nose up), nose dip on stopping, with a little overshoot.
            spring(v.pitch, v.pitchV, 0.075f * accel, 2.2f, 0.42f, dt);
            // Roll: out of the turn (turning right = clockwise -> the left side goes down), with
            // speed only (a hull turning in place stays level: owner 2026-11-14, no motion while
            // standing but the recoil).
            spring(v.roll, v.rollV, -omega * 0.008f * moving, 1.8f, 0.5f, dt);
            // Suspension: one bounce where the ground class changes (sand -> rock, onto concrete).
            // (No random bumps on rough ground and no idling tremble any more: owner 2026-11-14,
            // "every unit has some small shaking".)
            const int tx = int(x) >> 5, ty = int(y) >> 5;
            if (w.inBounds(tx, ty)) {
                const std::uint32_t cf = w.cell(tx, ty).flags;
                const int cls = (cf & game::cell_flags::kConcrete) ? 8 : int(cf >> game::cell_flags::kTerrainShift);
                if (v.terrain >= 0 && cls != v.terrain && moving > 0.2f) {
                    v.liftV += 22.0f * moving;
                    v.pitchV += (rnd() < 0.5f ? -0.35f : 0.35f) * moving;
                }
                v.terrain = cls;
            }
            spring(v.lift, v.liftV, 0, 3.4f, 0.32f, dt);
            spring(v.rec, v.recV, 0, 2.6f, 0.55f, dt);
            spring(v.trec, v.trecV, 0, 4.5f, 0.75f, dt);
            Pose& o = v.out;
            o.pitch = std::clamp(v.pitch, -0.14f, 0.14f);
            o.roll = std::clamp(v.roll, -0.09f, 0.09f);
            o.lift = std::clamp(v.lift, -1.5f, 2.5f);
            o.tremble = 0;
            o.recoil = std::clamp(v.rec, -1.0f, 3.0f);
            o.turretRecoil = std::clamp(v.trec, -1.0f, 3.5f);
        }
    }
    // Recoil: every shot fired since the last frame (the world's shot log, World::shotEvents).
    if (dt > 0 && now > lastShotTick_) {
        for (const game::ShotEvent& e : w.shotEvents()) {
            const std::uint32_t ft = e.tick + std::uint32_t(std::max(0, e.delay));
            if (ft <= lastShotTick_ || ft > now || e.side < 0 || e.side >= int(w.sides().size()) || e.side >= game::kMaxSides)
                continue;
            const auto& side = w.sides()[std::size_t(e.side)];
            if (e.firer < 0 || e.firer >= int(side.links.size()) || side.links[std::size_t(e.firer)].kind != game::SlotKind::Unit)
                continue;
            if (e.firer >= int(units_[e.side].size())) continue;
            State& v = units_[e.side][std::size_t(e.firer)];
            const game::Unit& u = side.units[std::size_t(e.firer)];
            if (v.key < 0 || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            const float k = kick_[std::size_t(u.type)];
            if (k <= 0) continue;
            const float ax = float(e.dstX - e.srcX), ay = float(e.dstY - e.srcY), al = std::hypot(ax, ay);
            if (al > 0.5f) v.out.shotX = ax / al, v.out.shotY = ay / al;
            v.recV += 26.0f * k;
            if (t_.bin.units[u.type].barrelArt >= 0) v.trecV += 70.0f * k;
            // The hull rocks away from the shot: firing ahead lifts the nose, firing to the
            // right lifts the right side.
            const float face = float(u.facing & 31) * (2 * kPi / 32);
            const float fx = std::sin(face), fy = -std::cos(face);  // ground forward (y south)
            const float ahead = v.out.shotX * fx + v.out.shotY * fy;
            const float right = v.out.shotX * -fy + v.out.shotY * fx;
            v.pitchV += 0.55f * k * ahead;
            v.rollV -= 0.45f * k * right;
            v.liftV += 4.0f * k;
        }
    }
    if (dt > 0) lastShotTick_ = now;
}

}  // namespace d2k::render
