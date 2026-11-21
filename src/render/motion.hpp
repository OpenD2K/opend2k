#pragma once
// Render-side motion smoothing. The original draws the model state as it is after the last tick
// (no interpolation, re/notes/timing.md); at the slower game speeds that shows units stepping a
// few pixels 8..25 times a second. We render at the display rate instead and draw units,
// projectiles and explosions at a position interpolated between the previous and the current
// tick. The simulation never sees these positions: apply() writes them into the World only for
// the duration of the map draw and restore() puts the exact values back, so the model stays
// bit-exact.

#include "game/world.hpp"
#include "render/smooth_drive.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace d2k::render {

class MotionInterpolator {
public:
    void reset();
    // Call right before every World::tick(): remembers the positions the tick starts from.
    void beforeTick(const game::World& w);
    // Moves every object that also existed before the last tick to prev + (cur - prev) * alpha
    // (alpha in 0..1 = time since that tick in ticks). Objects that just appeared, slots that were
    // reused and jumps longer than a couple of tiles are drawn where they are.
    // easeDt >= 0 (Smooth camera, Enhanced / 3D): ground units' drawn positions also follow the
    // interpolated ones through a short exponential (kEaseTau), so they ease into motion and out
    // of it instead of starting / stopping at full speed in one tick (they trail by speed * tau,
    // never lead the simulation; jumps snap). easeDt = this frame's seconds; < 0 = off.
    // drive (Smooth driving, Enhanced / 3D; render/smooth_drive.hpp): ground vehicles' interpolated
    // positions are moved onto its smoothed path first (SmoothDrive::beginFrame called before).
    void apply(game::World& w, float alpha, float easeDt = -1, SmoothDrive* drive = nullptr);
    void restore(game::World& w);

    // Picking with what is on screen: the units' positions of the last apply() (the drawn ones).
    // applyDrawn() moves the live units there (restore() puts them back), for the selection box /
    // 3D picking; toSim() moves a picked world point near a drawn unit by that unit's drawn ->
    // simulated offset, so a click on the drawn unit lands on the unit the simulation has.
    // (Nothing to do between apply() and restore(): the units are drawn there already. applyDrawn
    // nests: only the outermost pair moves the units; toSim does nothing inside a pair.)
    void applyDrawn(game::World& w);
    void restoreDrawn(game::World& w);
    void toSim(const game::World& w, float& wx, float& wy) const;

    static constexpr float kEaseTau = 0.04f;  // seconds (~95 % of a speed change in 0.12 s)

private:
    struct Prev {
        std::int32_t x = 0, y = 0, z = 0;
        std::int32_t key = -1;    // type (+ kind) identity; -1 = slot was empty
        std::int32_t steps = 0;   // bullets: moveSteps (counts down while the same bullet flies)
        std::int16_t facing = 0, turret = 0;  // units: body / turret facing (Smooth driving)
        std::int32_t heading = 0;             // units: body heading, 65536 per turn (Modern movement)
    };
    struct Saved {
        std::uint8_t kind;  // 1 unit, 2 bullet, 3 explosion
        std::uint8_t side;
        std::uint16_t index;
        std::int32_t x, y, z;
    };
    struct Drawn {
        std::int32_t key = -1;   // unit type of the slot when drawn; -1 = not drawn / not moved
        std::int32_t x = 0, y = 0, z = 0;  // the drawn position (16.16)
        double ex = 0, ey = 0;   // eased position (16.16; Smooth camera)
        bool eased = false;      // ex / ey valid
        std::uint32_t frame = 0; // apply() count when last drawn
    };
    bool havePrev_ = false;
    std::array<std::vector<Prev>, game::kMaxSides> units_, bullets_, explosions_;
    std::array<std::vector<Drawn>, game::kMaxSides> drawn_;
    std::vector<Saved> saved_, savedDrawn_;
    std::uint32_t frame_ = 0;
    bool applied_ = false;  // between apply() and restore()
    int drawnDepth_ = 0;    // applyDrawn() nesting
    static void put(game::World& w, const std::vector<Saved>& v);
};

}  // namespace d2k::render
