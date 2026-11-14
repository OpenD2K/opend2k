#pragma once
// Barrel tips measured on the original sprites (docs/enhanced-graphics.md "Barrel tips"): where a
// unit's gun ends on screen for the frame it is drawn with, so the Modern muzzle flashes, tracers
// and projectiles start there and not inside the hull. Used in Enhanced 2D / 3D sprites for every
// type a rendered art pack gives no fx_muzzle anchors (render::PackAnchors, the 3D models' own
// anchors come first). Turreted units: the turret frame (only turret and barrel on it) - the
// opaque pixel furthest along the turret's facing within a narrow band through the pivot; others:
// the same on the body frame (the guns of a trike / quad / raider sit at its nose). Measured once
// per frame (cached), never touches the simulation.

#include "formats/templates.hpp"
#include "game/world.hpp"

#include <unordered_map>

namespace d2k::render {

class SpriteCache;

class MuzzleTips {
public:
    MuzzleTips(const SpriteCache& sprites, const formats::Templates& t) : sprites_(sprites), t_(t) {}
    // The gun's end of unit `u` drawn at `facing` (0..32, eased; the turret's for turreted units):
    // ground offset (dx, dy) from the unit position and height z (world px), so that the drawn
    // point is unit + (dx, dy - z) as in the 2D effects. False: no art / nothing measured.
    bool tip(const game::Unit& u, float facing, float& dx, float& dy, float& z) const;
    // The facing the tip follows: the turret's for units with turret art, else the body's.
    bool turreted(int type) const;

private:
    const SpriteCache& sprites_;
    const formats::Templates& t_;
    struct Tip { float sx = 0, sy = 0; bool ok = false; };
    mutable std::unordered_map<long long, Tip> cache_;  // (frame, facing) -> screen offset
    Tip measure(int frame, int facing32) const;
};

}  // namespace d2k::render
