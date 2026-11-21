#pragma once
// "Smooth driving" (Settings::smoothDriving; not original; docs/STATUS.md "Smooth driving"): the
// drawn path, body facing and turret facing of ground vehicles, render only.
//
// The original moves a vehicle tile centre to tile centre in 8 directions (MoveUnit 0x494260,
// re/notes/movement.md §1): a corner of 90 degrees or more is a full stop, a turn in place one
// facing step every RotationSpeed+1 ticks, then full speed again; facings and turrets jump between
// 32 steps. This draws the same motion as a vehicle would drive it:
//  - Corners of the sim's route (45 / 90 / 135 degrees) are drawn as a quadratic Bezier through
//    the corner tile's centre (radius <= 14 px, chosen so the curve stays within ~6 px of the
//    corner; <= ~7 px from the simulated position anywhere). The next corner is known ahead from
//    the unit's own route (Route[RouteSteps-1], BlockTo while turning). Where the original turns
//    in place, the turn's progress (its facing, interpolated between steps by the turn delay
//    counter) moves the vehicle along the middle of the curve: it creeps round while pivoting.
//    Speed eases down into the corner and up out of it (Hermite time warps of the curve).
//  - The body faces the curve's tangent (where it drives), else the simulation's facing
//    interpolated between its 32 steps at the sim's own turning rate.
//  - The turret: the simulation's turret facing interpolated between steps the same way (it slews
//    at exactly the sim's rate towards the sim's aim, independently of the drawn body).
// Only the deviation from the simulated position is computed (drawn = sim + delta); a delta that
// jumps (a route changed under a predicted corner) is blended out over ~0.1 s. Nothing is written
// back into the model: MotionInterpolator::apply moves the units to the drawn positions for the
// draw only, so the simulation stays bit-exact. Fixed-size per-slot state, no per-frame allocations.

#include "game/world.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace d2k::render {

class SmoothDrive {
public:
    struct Out {
        float facing = 0, turret = 0;  // drawn body / turret facing in 32nds of a turn (0..32, continuous)
        float odometer = 0;            // drawn px driven (+ forward, - backward; wheels / tracks)
        float speed = 0;               // drawn px per second
        float curve = 0;               // signed turn rate of the drawn heading (32nds per second)
        float spin = 0;                // drawn heading turned so far (32nds, + clockwise, unwrapped; tracks counter-run)
    };

    void reset();
    // Once per drawn frame before MotionInterpolator::apply: clock = game ticks + the fraction
    // into the next tick (alpha); dt = this frame's seconds (0 = paused / held).
    void beginFrame(double clock, float alpha, float dt);
    // A ground vehicle at its tick-interpolated position (x, y in world px): moves them onto the
    // smoothed path. prevFacing / prevTurret = the facings before the last tick, prevHeading the
    // body heading then (65536 per turn). Modern movement (World::modernMovement, the vehicle
    // already drives free-angle paths): no path faking - only the true heading interpolated
    // between the ticks and the turret eased between its steps.
    void unit(const game::World& w, const game::Unit& u, int prevFacing, int prevTurret, int prevHeading, float& x, float& y);
    // The drawn facings of a unit handled this frame (nullptr = not a smoothed vehicle).
    const Out* out(int side, int index) const;

    // Shared by the tests: the corner geometry, exposed for game_test.
    static float facingOf(float dx, float dy);  // 0..32, 0 = north, clockwise (screen y down)

private:
    struct Corner {
        bool valid = false;
        float vx = 0, vy = 0;  // the corner (tile centre)
        float ax = 0, ay = 0;  // unit direction in
        float bx = 0, by = 0;  // unit direction out
        float r = 0;           // curve radius along each leg (px)
        float c = 0;           // half the curve's parameter range spent turning in place (0 = drives through)
        int fb = 0;            // facing of the leg out (the sim's GetDir8)
        int steps = 1;         // facing steps of the turn
        bool seen = false;     // known before the vehicle entered its curve (else not drawn: a vehicle
                               // that already drove up to the corner straight pivots there, as the original)
    };
    struct State {
        std::int32_t key = -1;
        std::uint32_t frame = 0;
        std::int32_t stepX = 0, stepY = 0;  // the sim's step target last seen (16.16)
        bool hasA = false;
        float ax = 0, ay = 0;               // start of the current leg (px)
        Corner prev, next;                  // the corner behind (leaving it) / ahead (approaching it)
        float idle = 0;                     // ticks standing with nothing to do
        float dlx = 0, dly = 0;             // last raw delta
        float cx = 0, cy = 0;               // blend-out of a jumped delta
        float px = 0, py = 0;               // last simulated (interpolated) position
        float lx = 0, ly = 0;               // last drawn position
        float heading = 0, turret = 0;      // drawn facings (32nds)
        double clock = 0;
        Out out;
    };
    static bool makeCorner(Corner& k, float ax, float ay, float bx, float by, float cx, float cy);
    std::array<std::vector<State>, game::kMaxSides> units_;
    std::uint32_t frame_ = 0;
    double clock_ = 0;
    float alpha_ = 0, dt_ = 0;
};

}  // namespace d2k::render
