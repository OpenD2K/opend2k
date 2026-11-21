#include "render/smooth_drive.hpp"

#include "game/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace d2k::render {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kMaxRadius = 14.0f;    // px along each leg
constexpr float kMaxBow = 22.0f;       // radius * |b - a| <= this: the curve passes <= ~5.5 px from the corner
constexpr float kTurnShare = 0.18f;    // c: a turn in place covers u in [0.5 - c, 0.5 + c]
constexpr float kBlendTau = 0.09f;     // s: a jumped delta fades out
constexpr float kMaxDelta = 8.0f;      // px: never drawn further than this from the simulated position

float wrap32(float d) {
    d = std::fmod(d, 32.0f);
    if (d > 16) d -= 32;
    if (d < -16) d += 32;
    return d;
}

float norm32(float f) {
    f = std::fmod(f, 32.0f);
    return f < 0 ? f + 32 : f;
}

// Cubic Hermite 0 -> 1 with end slopes m0, m1 (monotone for slopes <= 3).
float hermite(float s, float m0, float m1) {
    s = std::clamp(s, 0.0f, 1.0f);
    const float s2 = s * s, s3 = s2 * s;
    return (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) + (s3 - s2) * m1;
}

// The direction RotateBody / RotateTurret (0x493eb0 / 0x493f40) step towards `want`.
int stepSign(int f, int want) {
    const int d = std::abs(f - want);
    if (d < 16) return f <= want ? 1 : -1;
    return want < f ? 1 : -1;
}

// A facing (body or turret) at the drawn time (one tick behind the model, + alpha), between its
// 32 steps: the sim steps once every rs + 1 ticks (delay counts rs .. 0), so the time since the
// last step is known exactly. want < 0 = unknown target (only the step just taken is eased).
float continuousFacing(int f, int prevF, int want, int delay, int rs, float alpha) {
    const float per = 1.0f / float(rs + 1);
    if (want >= 0 && f != want) {
        float sub = float(rs - delay - 1 + alpha) * per;
        if (sub < 0 && prevF == f) sub = 0;  // a new turn: the delay left over from the last one runs first
        return norm32(float(f) + float(stepSign(f, want)) * std::clamp(sub, -per, 1.0f));
    }
    if (prevF != f) {
        const float d = wrap32(float(f - prevF));
        if (std::abs(d) <= 1.01f) return norm32(float(f) + (d > 0 ? 1.0f : -1.0f) * (alpha - 1) * per);
    }
    return float(f);
}

struct Pt {
    float x, y;
};

Pt curvePoint(float vx, float vy, float ax, float ay, float bx, float by, float r, float u) {
    const float w0 = (1 - u) * (1 - u), w1 = 2 * u * (1 - u), w2 = u * u;
    return {w0 * (vx - ax * r) + w1 * vx + w2 * (vx + bx * r), w0 * (vy - ay * r) + w1 * vy + w2 * (vy + by * r)};
}

}  // namespace

float SmoothDrive::facingOf(float dx, float dy) { return norm32(std::atan2(dx, -dy) * (16.0f / kPi)); }

void SmoothDrive::reset() {
    for (auto& v : units_) v.clear();
}

void SmoothDrive::beginFrame(double clock, float alpha, float dt) {
    ++frame_;
    clock_ = clock;
    alpha_ = std::clamp(alpha, 0.0f, 1.0f);
    dt_ = std::clamp(dt, 0.0f, 0.1f);
}

const SmoothDrive::Out* SmoothDrive::out(int side, int index) const {
    if (side < 0 || side >= game::kMaxSides || index < 0 || index >= int(units_[side].size())) return nullptr;
    const State& s = units_[side][std::size_t(index)];
    return s.key >= 0 && s.frame == frame_ ? &s.out : nullptr;
}

bool SmoothDrive::makeCorner(Corner& k, float ax, float ay, float bx, float by, float cx, float cy) {
    float ix = bx - ax, iy = by - ay, ox = cx - bx, oy = cy - by;
    const float li = std::hypot(ix, iy), lo = std::hypot(ox, oy);
    if (li < 4 || lo < 4) return false;
    ix /= li, iy /= li, ox /= lo, oy /= lo;
    const float dot = ix * ox + iy * oy;
    if (dot > 0.995f || dot < -0.75f) return false;  // straight on, or a U-turn (turned in place, not curved)
    const float bow = std::hypot(ox - ix, oy - iy);
    k.valid = true;
    k.vx = bx, k.vy = by, k.ax = ix, k.ay = iy, k.bx = ox, k.by = oy;
    k.r = std::min({kMaxRadius, kMaxBow / bow, 0.45f * li, 0.45f * lo});
    const int fa = int(std::lround(facingOf(ix, iy))) & 31;
    k.fb = int(std::lround(facingOf(ox, oy))) & ~3 & 31;
    k.steps = std::max(1, std::abs(int(wrap32(float(k.fb - fa)))));
    // MoveUnit drives on while the facing error is within 6 steps; more = a stop and a turn in place.
    k.c = k.steps >= 7 ? kTurnShare : 0.0f;
    return true;
}

void SmoothDrive::unit(const game::World& w, const game::Unit& u, int prevFacing, int prevTurret, int prevHeading, float& x,
                       float& y) {
    if (u.side < 0 || u.side >= game::kMaxSides || u.index < 0) return;
    auto& vec = units_[std::size_t(u.side)];
    if (vec.size() != w.sides()[std::size_t(u.side)].units.size()) vec.assign(w.sides()[std::size_t(u.side)].units.size(), State{});
    if (u.index >= int(vec.size())) return;
    State& s = vec[std::size_t(u.index)];
    const auto& tp = w.unitTemplate(u);
    // The simulation's facings at the drawn time, between their steps.
    int wantBody = -1;
    if (u.blockFromX != u.blockToX || u.blockFromY != u.blockToY)
        wantBody = game::getDir8(u.blockFromX, u.blockFromY, u.blockToX, u.blockToY);
    const float simF = continuousFacing(u.facing & 31, prevFacing & 31, wantBody, u.turnDelay, tp.unitRotationSpeed, alpha_);
    const bool ownTurret = !(u.flags & game::unit_flags::kFixedBarrel);
    const float simT = ownTurret ? continuousFacing(u.facingTurret & 31, prevTurret & 31, u.facingTurretTarget & 31,
                                                    u.turretTurnDelay, tp.barrelRotationSpeed, alpha_)
                                 : simF;
    const bool fresh = s.key != u.type || s.frame + 1 != frame_ || std::hypot(x - s.px, y - s.py) > 48;
    if (w.isModernVehicle(u)) {
        // Modern movement: the simulation drives the curves itself. The body = its fine heading
        // between the last two ticks; the turret = the sim's turret eased between its steps.
        const float h1 = float(w.unitHeading(u)) / 2048.0f, h0 = float(prevHeading & 0xFFFF) / 2048.0f;
        const float body = norm32(h0 + wrap32(h1 - h0) * alpha_);
        const float before = fresh ? body : s.heading;
        if (fresh) {
            s = State{};
            s.key = u.type;
            s.turret = simT;
            s.lx = x, s.ly = y;
        }
        s.frame = frame_;
        s.px = x, s.py = y;
        s.heading = body;
        if (dt_ > 0) {
            const float terr = wrap32(simT - s.turret);
            if (std::abs(terr) > 8) s.turret = simT;
            else s.turret = norm32(s.turret + terr * (1 - std::exp(-dt_ / 0.03f)));
        }
        const float mx = x - s.lx, my = y - s.ly, dist = std::hypot(mx, my);
        if (dist < 48) {
            const float a = body * (kPi / 16);
            s.out.odometer += (mx * std::sin(a) - my * std::cos(a)) >= 0 ? dist : -dist;
        }
        s.lx = x, s.ly = y;
        s.out.spin += wrap32(body - before);
        s.out.facing = body;
        s.out.turret = ownTurret ? s.turret : body;
        if (dt_ > 0) {
            s.out.speed = dist / dt_;
            s.out.curve = wrap32(body - before) / dt_;
        }
        return;
    }
    if (fresh) {
        s = State{};
        s.key = u.type;
        s.stepX = u.stepTargetX, s.stepY = u.stepTargetY;
        s.heading = simF;
        s.turret = simT;
        s.px = s.lx = x, s.py = s.ly = y;
        s.clock = clock_;
        s.frame = frame_;
        s.out.facing = simF, s.out.turret = simT;
        return;
    }
    s.frame = frame_;
    const float ticks = float(std::clamp(clock_ - s.clock, 0.0, 8.0));
    s.clock = clock_;
    const float px = x, py = y;
    const bool atCentre = u.posSteps == 0;
    const bool waiting = atCentre && u.blockFromX == u.blockToX && u.blockFromY == u.blockToY;
    s.idle = waiting ? s.idle + ticks : 0.0f;

    // A new leg: the corner just reached becomes the one being left.
    if (u.stepTargetX != s.stepX || u.stepTargetY != s.stepY) {
        const float obx = float(s.stepX) / 65536.0f, oby = float(s.stepY) / 65536.0f;
        const float nbx = float(u.stepTargetX) / 65536.0f, nby = float(u.stepTargetY) / 65536.0f;
        const bool reached = std::hypot(px - obx, py - oby) < 24;
        const float lx = nbx - obx, ly = nby - oby, ll = std::hypot(lx, ly);
        if (reached && s.next.valid && s.next.seen && std::abs(s.next.vx - obx) < 1 && std::abs(s.next.vy - oby) < 1 && ll > 1 &&
            (lx * s.next.bx + ly * s.next.by) / ll > 0.99f)
            s.prev = s.next;  // as predicted
        else
            s.prev.valid = false;
        s.hasA = true;
        s.ax = reached ? obx : px, s.ay = reached ? oby : py;
        s.next.valid = false;
        s.stepX = u.stepTargetX, s.stepY = u.stepTargetY;
    }
    const float bx = float(s.stepX) / 65536.0f, by = float(s.stepY) / 65536.0f;

    // The corner ahead: the tile after this leg's target, from the unit's own route.
    int cX = -1, cY = -1;
    if (atCentre && !waiting) {
        cX = u.blockToX, cY = u.blockToY;  // turning in place towards it
    } else if (!atCentre && u.routeSteps > 0 && u.routeSteps <= int(u.routeX.size())) {
        cX = u.routeX[std::size_t(u.routeSteps - 1)], cY = u.routeY[std::size_t(u.routeSteps - 1)];
        const int tx = int(std::floor(bx / 32)), ty = int(std::floor(by / 32));
        if (std::max(std::abs(cX - tx), std::abs(cY - ty)) != 1) cX = -1;
    }
    Corner k;
    if (cX >= 0 && s.hasA && makeCorner(k, s.ax, s.ay, bx, by, float(cX * 32 + 16), float(cY * 32 + 16))) {
        // Seen ahead = still before its curve (or the same corner as already known).
        const bool same = s.next.valid && s.next.seen && std::abs(s.next.vx - k.vx) < 1 && std::abs(s.next.vy - k.vy) < 1 &&
                          s.next.fb == k.fb;
        k.seen = same || (k.vx - px) * k.ax + (k.vy - py) * k.ay >= k.r;
        s.next = k;
    } else if (!(atCentre && s.next.valid && s.idle <= 3 && std::abs(s.next.vx - bx) < 1 && std::abs(s.next.vy - by) < 1)) {
        s.next.valid = false;  // (kept for the one tick the route is dropped after a turn in place)
    }

    // The delta from the simulated position to the curve.
    float dx = 0, dy = 0, tx = 0, ty = 0;
    bool inCorner = false;
    if (s.prev.valid) {
        const Corner& c = s.prev;
        const float sd = (px - c.vx) * c.bx + (py - c.vy) * c.by;
        if (sd < c.r && sd > -2) {
            const float m = c.c > 0 ? 0.35f : 0.8f;
            const float uu = 0.5f + c.c + (0.5f - c.c) * hermite(sd / c.r, m, 1.0f / (1 - 2 * c.c));
            const Pt q = curvePoint(c.vx, c.vy, c.ax, c.ay, c.bx, c.by, c.r, uu);
            const float d0 = std::max(sd, 0.0f);
            dx += q.x - (c.vx + c.bx * d0), dy += q.y - (c.vy + c.by * d0);
            tx = (1 - uu) * c.ax + uu * c.bx, ty = (1 - uu) * c.ay + uu * c.by;
            inCorner = true;
        }
    }
    if (s.next.valid && s.next.seen) {
        const Corner& c = s.next;
        const float d = (c.vx - px) * c.ax + (c.vy - py) * c.ay;
        if (d < c.r && d > -2) {
            const float m = c.c > 0 ? 0.35f : 0.8f;
            float uu = (0.5f - c.c) * hermite(1 - d / c.r, 1.0f / (1 - 2 * c.c), m);
            if (atCentre && c.c > 0) {  // turning in place: creep along the middle of the curve
                const float f = std::clamp(1 - std::abs(wrap32(simF - float(c.fb))) / float(c.steps), 0.0f, 1.0f);
                uu += 2 * c.c * f;
            }
            const Pt q = curvePoint(c.vx, c.vy, c.ax, c.ay, c.bx, c.by, c.r, uu);
            const float d0 = std::max(d, 0.0f);
            dx += q.x - (c.vx - c.ax * d0), dy += q.y - (c.vy - c.ay * d0);
            tx = (1 - uu) * c.ax + uu * c.bx, ty = (1 - uu) * c.ay + uu * c.by;
            inCorner = true;
        }
    }
    // A delta that jumps (a route changed under a predicted corner, a corner formed late) fades in / out.
    const float moved = std::hypot(px - s.px, py - s.py);
    if (std::hypot(dx - s.dlx, dy - s.dly) > 0.5f + 1.6f * moved) {
        s.cx += s.dlx - dx;
        s.cy += s.dly - dy;
    }
    s.dlx = dx, s.dly = dy;
    if (dt_ > 0) {
        const float k2 = std::exp(-dt_ / kBlendTau);
        s.cx *= k2, s.cy *= k2;
        if (std::abs(s.cx) < 0.01f && std::abs(s.cy) < 0.01f) s.cx = s.cy = 0;
    }
    float ox = dx + s.cx, oy = dy + s.cy;
    if (const float l = std::hypot(ox, oy); l > kMaxDelta) ox *= kMaxDelta / l, oy *= kMaxDelta / l;
    x = px + ox;
    y = py + oy;
    s.px = px, s.py = py;

    // Body: the curve's tangent / the leg's direction while driving, else the sim's facing.
    float target = simF;
    if (inCorner) {
        target = facingOf(tx, ty);
    } else if (!atCentre) {
        const float lx = bx - s.ax, ly = by - s.ay;
        if (s.hasA && std::hypot(lx, ly) > 4) target = facingOf(lx, ly);
        else if (std::hypot(bx - px, by - py) > 3) target = facingOf(bx - px, by - py);
    }
    const float before = s.heading;
    if (dt_ > 0) {
        const float err = wrap32(target - s.heading);
        if (std::abs(err) > 14) s.heading = target;  // (a jump: a slot reused, a teleport)
        else {
            // Eased, and never faster than 1.5 x the sim's own turning rate (a hull that sets off
            // while still turning swings round instead of flicking).
            const float tps = ticks / dt_;  // model ticks per second (the GameSpeed rate)
            const float cap = std::max(0.05f, 1.5f * tps / float(tp.unitRotationSpeed + 1) * dt_);
            s.heading = norm32(s.heading + std::clamp(err * (1 - std::exp(-dt_ / (inCorner ? 0.03f : 0.05f))), -cap, cap));
        }
        const float terr = wrap32((ownTurret ? simT : s.heading) - s.turret);
        if (std::abs(terr) > 8) s.turret = ownTurret ? simT : s.heading;
        else s.turret = norm32(s.turret + terr * (1 - std::exp(-dt_ / 0.03f)));
    }
    // Wheels / tracks: the distance driven, signed along the hull.
    const float mx = x - s.lx, my = y - s.ly, dist = std::hypot(mx, my);
    if (dist < 48) {
        const float a = s.heading * (kPi / 16);
        s.out.odometer += (mx * std::sin(a) - my * std::cos(a)) >= 0 ? dist : -dist;
    }
    s.lx = x, s.ly = y;
    if (static const char* dbg = std::getenv("D2K_DRIVE_DEBUG"); dbg && std::atoi(dbg) == u.index)
        std::fprintf(stderr, "drive %.3f P %.2f,%.2f B %.1f,%.1f A %.1f,%.1f ps %d bt %d,%d bf %d,%d rs %d f %d simF %.2f idle %.1f "
                     "prev %d(%.0f,%.0f c%.2f) next %d%d(%.0f,%.0f c%.2f fb%d) d %.2f,%.2f corr %.2f,%.2f\n",
                     clock_, px, py, bx, by, s.ax, s.ay, int(u.posSteps), u.blockToX, u.blockToY, u.blockFromX, u.blockFromY,
                     u.routeSteps, u.facing, simF, s.idle, s.prev.valid, s.prev.vx, s.prev.vy, s.prev.c, s.next.valid, s.next.seen,
                     s.next.vx, s.next.vy, s.next.c, s.next.fb, dx, dy, s.cx, s.cy);
    s.out.spin += wrap32(s.heading - before);
    s.out.facing = s.heading;
    s.out.turret = s.turret;
    if (dt_ > 0) {
        s.out.speed = dist / dt_;
        s.out.curve = wrap32(s.heading - before) / dt_;
    }
}

}  // namespace d2k::render
