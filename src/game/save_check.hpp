#pragma once
// Save/load determinism round trip (tests/game_test.cpp and --simtest): run a world N ticks,
// save, load the save into a freshly constructed world, run both M more ticks with the same
// scripted orders and compare the complete state (save bytes) along the way.

#include "game/world.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace d2k::game {

using WorldFactory = std::function<std::unique_ptr<World>()>;
// Issues orders for the tick about to run (called before every tick with its 0-based number).
using OrderScript = std::function<void(World&, std::uint32_t step)>;

struct RoundTripReport {
    bool ok = false;
    std::string error;            // first problem found
    std::size_t saveBytes = 0;    // world section size at the save point
    std::size_t fileBytes = 0;    // whole save file
    std::uint64_t hashAtSave = 0, hashAtEnd = 0;
    bool freshDiffered = false;   // the fresh world differed before loading (the test is not vacuous)
    int divergedAt = -1;          // first compared step whose state hashes differed
    int pendingOrdersSaved = 0;   // orders queued at the save point (restored by the load)
};

// The orders for step `pre` are issued on the original world BEFORE saving, so the queued
// orders travel through the save file; the loaded world does not get them again.
RoundTripReport saveRoundTrip(const WorldFactory& make, int preTicks, int postTicks, const OrderScript& orders,
                              int compareEvery = 250);

// A deterministic order script for `side` (no RNG use): moves some units around, attacks the
// nearest enemy, builds units/buildings and places them, buys from the starport.
void scriptedOrders(World& w, int side, std::uint32_t step);

}  // namespace d2k::game
