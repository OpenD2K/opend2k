#pragma once
// Render-side motion smoothing. The original draws the model state as it is after the last tick
// (no interpolation, re/notes/timing.md); at the slower game speeds that shows units stepping a
// few pixels 8..25 times a second. We render at the display rate instead and draw units,
// projectiles and explosions at a position interpolated between the previous and the current
// tick. The simulation never sees these positions: apply() writes them into the World only for
// the duration of the map draw and restore() puts the exact values back, so the model stays
// bit-exact.

#include "game/world.hpp"

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
    void apply(game::World& w, float alpha);
    void restore(game::World& w);

private:
    struct Prev {
        std::int32_t x = 0, y = 0, z = 0;
        std::int32_t key = -1;    // type (+ kind) identity; -1 = slot was empty
        std::int32_t steps = 0;   // bullets: moveSteps (counts down while the same bullet flies)
    };
    struct Saved {
        std::uint8_t kind;  // 1 unit, 2 bullet, 3 explosion
        std::uint8_t side;
        std::uint16_t index;
        std::int32_t x, y, z;
    };
    bool havePrev_ = false;
    std::array<std::vector<Prev>, game::kMaxSides> units_, bullets_, explosions_;
    std::vector<Saved> saved_;
};

}  // namespace d2k::render
