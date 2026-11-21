#pragma once
// "Vehicle animation: Modern" (Settings::vehicleAnimation; docs/enhanced-graphics.md "Vehicle
// animation"): render-only body motion of ground vehicles on top of their simulated position -
// the hull squats (nose up) when the vehicle starts and dips its nose when it stops, rolls out of
// a turn, bounces a little where the ground changes (rock, dunes, concrete edges) and on rough
// ground, trembles with its idling engine, and kicks back when it fires (body and turret / barrel).
// The original units start and stop at full speed and turn in place; these springs only read the
// drawn positions / facings and the world's shot log, so the simulation never sees them and stays
// bit-exact. 2D (MapRenderer::drawUnit) draws the pose as a small shear / offset of the sprite,
// 3D (render3d ModelRenderer::unitPlacement) as the model's pitch / roll / lift / recoil.

#include "formats/templates.hpp"
#include "game/world.hpp"
#include "render/smooth_drive.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace d2k::render {

class VehicleMotion {
public:
    // The pose of one vehicle this frame (all zero = the original's rigid sprite).
    struct Pose {
        float pitch = 0;         // radians, + = nose up (squat on starting, - = nose dip on stopping)
        float roll = 0;          // radians, + = right side down (seen along the hull)
        float lift = 0;          // world px up (suspension bounce)
        float tremble = 0;       // world px up (no idling shimmer any more: always 0; owner 2026-11-14)
        float recoil = 0;        // world px the hull is pushed back along the last shot
        float turretRecoil = 0;  // world px the turret / barrel is pushed back further
        float shotX = 0, shotY = 1;  // ground direction of the last shot (unit vector; recoil = against it)
    };

    explicit VehicleMotion(const formats::Templates& t);
    void reset();
    // Once per drawn frame, with the drawn (interpolated) positions. dt = effect seconds since the
    // last call (0 while paused: the springs hold).
    // drive (Smooth driving, nullptr = off): the hull turns with the drawn heading (it leans into
    // the curves it drives); bumps (3D only: a sub-pixel 2D bob reads as shaking) adds a low
    // suspension bob on rock while driving.
    void update(const game::World& w, float dt, const SmoothDrive* drive = nullptr, bool bumps = false);
    // nullptr = not a tracked ground vehicle (infantry, flyers, carried units, worms).
    const Pose* pose(int side, int index) const;

private:
    struct State {
        std::int32_t key = -1;      // type + 1; -1 = slot not tracked
        float x = 0, y = 0;         // last position (world px)
        float heading = 0;          // unwrapped body facing (radians)
        float vFast = 0, vSlow = 0; // speed low-passed fast / slow (px per effect second)
        float omega = 0;            // turn rate (rad/s, + = clockwise)
        float pitch = 0, pitchV = 0, roll = 0, rollV = 0, lift = 0, liftV = 0;
        float rec = 0, recV = 0, trec = 0, trecV = 0;
        int terrain = -1;           // ground class of the last cell (one bump where it changes)
        float odo = 0;              // px driven (the rock bob's phase)
        Pose out;
    };
    const formats::Templates& t_;
    std::array<std::vector<State>, game::kMaxSides> units_;
    std::vector<float> kick_;       // per unit type: recoil strength (0 = no gun)
    std::uint32_t lastShotTick_ = 0;
    bool primed_ = false;
    float time_ = 0;
    std::uint32_t rng_ = 0x2545F491u;
    float rnd();  // 0..1 (render-only generator)
};

}  // namespace d2k::render
