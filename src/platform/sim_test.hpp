#pragma once
// Headless checks run by `d2k --map NAME --simtest` (no window): movement of a vehicle and an
// infantry unit, a combat exchange, the harvest economy and production. Plus the --battle demo.

#include "platform/game_data.hpp"

namespace d2k::platform {

// Returns the process exit code (0 = all checks passed).
int runSimTest(const GameData& g);

// Demo helper (--battle): every armed ground unit attacks the nearest enemy ground unit.
void orderBattle(game::World& world);
// --vfx-demo: before every tick, a weapon effects gallery around (cx, cy) world px: rows of
// shots of each weapon kind (gun, cannon, rocket, missile, artillery, devastator, deviator,
// sonic, bombs) and explosions (vehicle deaths with their wreck flash, Death Hand). Changes the
// world (debug / render checks only).
void vfxDemoTick(game::World& world, int cx, int cy);

}  // namespace d2k::platform
