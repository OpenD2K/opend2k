#pragma once
// Live game state: sides with their object pools, units, buildings, map cells, tick
// counter, RNG. Field meanings and update logic mirror the original game's model
// (re/notes/map-and-spawn.md, re/notes/movement.md); the C++ layout is our own.
// Pure logic: no SDL / OS calls, deterministic.
//
// Source layout:
//   world.cpp    - object pools, model tick (ModelUpdate), UpdateUnit state machine, orders
//   spawn.cpp    - mission setup (Setup_SpawnMapObjects, ModelAddUnit/Building/Concrete)
//   movement.cpp - MoveUnit, GetNextSquare, CanUnitUseSquare, GetSpeed, turning, occupancy
//   pathfind.cpp - FindPath / SmoothPath
//   combat.cpp   - targeting, FireAt, attack states, DamageTiles, DestroyUnit/Building
//   projectiles.cpp - bullets and explosions (ModelAddBullet/Explosion, UpdateBullet/Explosion)
//   buildings.cpp - UpdateBuilding (turrets, decay, animation counters, death)
//   economy.cpp  - cash/spice drip and storage, power, building repair, harvesting states 11-16,
//                  free-harvester deliveries, carryalls (FlyUnit, pickup queue), starport upkeep
//   production.cpp - build queues, costs/speeds, sidebar icons (prerequisites), placement, sell,
//                  primary building, upgrades, MCV deploy; the public economy/production API
//   shroud.cpp   - local player's shroud: RevealCircle, UpdateShroudInRegion, unit/building sight
//   mission.cpp  - .MIS events and conditions (Mission__CheckEvents), win/lose, timer, messages
//   ai.cpp       - the computer player (CAI: InitAI / ActivateAI / DoAI and helpers)

#include "formats/bintables.hpp"
#include "formats/map.hpp"
#include "formats/mis.hpp"
#include "formats/techpos.hpp"
#include "formats/templates.hpp"
#include "formats/tileatr.hpp"
#include "formats/vars.hpp"
#include "game/ai.hpp"
#include "game/pathfind.hpp"
#include "game/random.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2k::game {

constexpr int kMaxSides = 8;
constexpr int kObjectsPerSide = 1000;     // CSide::objects[1000], shared by all object kinds
constexpr int kMaxUnitsPerSide = kObjectsPerSide;  // UpdateUnit 0x497b90 assert
constexpr int kRouteLength = 12;
constexpr int kNoIndex = 0xFFFF;          // "no object" in 16-bit index fields

// Fixed-point helpers: positions are 16.16 pixels; tiles are 32 px.
constexpr std::int32_t kFix = 1 << 16;
constexpr std::int32_t toFix(int pixels) { return pixels * kFix; }
constexpr int fixToPixel(std::int32_t f) { return f >> 16; }
constexpr int fixToTile(std::int32_t f) { return (f >> 16) >> 5; }
constexpr std::int32_t tileCentre(int tile) { return toFix(tile * 32 + 16); }

// Cell flag bits (cell +4; re/notes/map-and-spawn.md §1.3).
namespace cell_flags {
constexpr std::uint32_t kOwnerMask = 0x7;          // side of whatever occupies the cell
constexpr std::uint32_t kVehicle = 0x8;            // ground vehicle
constexpr std::uint32_t kBuilding = 0x10;          // solid building tile
constexpr std::uint32_t kInfantryMask = 0x3E0;     // 0x20 << spot, spot 0..4
constexpr std::uint32_t kAnyUnit = 0x3E8;
constexpr std::uint32_t kOccupied = 0x3F8;
constexpr std::uint32_t kWall = 0x400;
constexpr std::uint32_t kConcrete = 0x800;
constexpr std::uint32_t kCrate = 0x1000;
constexpr std::uint32_t kDriveOn = 0x2000;
constexpr std::uint32_t kWalkOn = 0x4000;
constexpr std::uint32_t kBuildOn = 0x8000;
constexpr std::uint32_t kSand = 0x10000;
constexpr std::uint32_t kConcreteOwnerMask = 0xE0000;  // bits 17-19
constexpr std::uint32_t kSpiceMask = 0x700000;         // bits 20-22, amount 0..7
constexpr std::uint32_t kFlyer = 0x800000;
constexpr std::uint32_t kWorm = 0x1000000;
constexpr std::uint32_t kInfantryOwnerMask = 0xE000000;  // bits 25-27
constexpr std::uint32_t kExitTile = 0x10000000;
constexpr int kConcreteOwnerShift = 17, kSpiceShift = 20, kInfantryOwnerShift = 25, kTerrainShift = 29;
}  // namespace cell_flags

// Unit flag bits (Unit +0x08; re/notes/movement.md §0).
namespace unit_flags {
constexpr std::uint32_t kAttacked = 0x1;
constexpr std::uint32_t kFired = 0x2;
constexpr std::uint32_t kCloaked = 0x4;
constexpr std::uint32_t kStealth = 0x10;
constexpr std::uint32_t kTurningInPlace = 0x20;
constexpr std::uint32_t kFlying = 0x40;
constexpr std::uint32_t kCarried = 0x100;
constexpr std::uint32_t kFixedBarrel = 0x8000;
constexpr std::uint32_t kDeathHand = 0x20000;
constexpr std::uint32_t kNotDrawn = 0x200000;
constexpr std::uint32_t kGone = 0x400000;          // UNCERTAIN name: set when crushed/carried away
constexpr std::uint32_t kSelfHealing = 0x800000;
constexpr std::uint32_t kDrawnBehind = 0x1000000;  // infantry on a vehicle's cell
constexpr std::uint32_t kDeviated = 0x1000;        // ChangeUnitOwner'd by a deviator (returns to oldSide)
constexpr std::uint32_t kChangedOwner = 0x2000000; // set on the copy made by ChangeUnitOwner
constexpr std::uint32_t kInvulnerable = 0x8000000;
constexpr std::uint32_t kBlockToMarked = 0x10000000;
constexpr std::uint32_t kRecheckRoute = 0x40000000;
constexpr std::uint32_t kStateChanged = 0x80000000;
constexpr std::uint32_t kExcluded = 0x400140;      // dead/carried/flying: not in tile queries
}  // namespace unit_flags

// Building flag bits (Building +0x28; dune2000-sp-patches side.h BuildingFlags).
namespace building_flags {
constexpr std::uint32_t kRepairing = 0x1;
constexpr std::uint32_t kBuildup = 0x2;        // build-up animation running (+0x3e counter)
constexpr std::uint32_t kSelling = 0x4;        // sell animation running (+0x3e counts down)
constexpr std::uint32_t kSold = 0x8;
constexpr std::uint32_t kAnimPermanent = 0x10;
constexpr std::uint32_t kAnimOnce = 0x20;
constexpr std::uint32_t kHasAnimation = 0x40;
constexpr std::uint32_t kPrimary = 0x80;
constexpr std::uint32_t kSelectable = 0x200;   // template flag: selectable/repairable, smokes, survivors
constexpr std::uint32_t kWasAttacked = 0x1000;
constexpr std::uint32_t kNoSurvivors = 0x2000; // DestroyBuilding(.., 1)
constexpr std::uint32_t kDecays = 0x8000;
constexpr std::uint32_t kDontRemoveTiles = 0x10000;
constexpr std::uint32_t kWasSold = 0x20000;
constexpr std::uint32_t kReleaseInfantry = 0x40000;
constexpr std::uint32_t kInfantryReleased = 0x80000;
constexpr std::uint32_t kHasSkirt = 0x200000;
constexpr std::uint32_t kNoConcrete = 0x400000;
}  // namespace building_flags

// Weapon (bullet) flag bits (WeaponTemplate::flags, copied to Bullet +0x04).
namespace weapon_flags {
constexpr std::uint32_t kArc = 0x1;            // ballistic arc (FallSpeed)
constexpr std::uint32_t kShadow = 0x2;         // drawn with a ground shadow (set by kArc / kFalling)
constexpr std::uint32_t kTrail = 0x10;         // trail explosion every tick
constexpr std::uint32_t kHoming = 0x20;
constexpr std::uint32_t kDeviator = 0x40;      // DamageTiles "deviate" argument
constexpr std::uint32_t kAnimated = 0x80;      // frame cycles instead of following the direction
constexpr std::uint32_t kFalling = 0x100;      // starts at the firer's height and falls (debris)
constexpr std::uint32_t kAdditive = 0x200;     // render only
constexpr std::uint32_t kBounce = 0x400;
constexpr std::uint32_t kRangeLimited = 0x800; // flies `range` px; damages along the 2nd half
constexpr std::uint32_t kStopsAtWalls = 0x1000;
}  // namespace weapon_flags

// Explosion flag bits (Explosion +0x04 = animationArtFlags[type] ^ ModelAddExplosion flags).
namespace explosion_flags {
constexpr std::uint32_t kRises = 0x1;          // PosZ += 2 px per tick
constexpr std::uint32_t kHouseRemap = 0x8;
constexpr std::uint32_t kHidden = 0x20;        // not drawn until the first frame advance
constexpr std::uint32_t kBlink = 0x400;        // muzzle flash: 8 ticks, pattern bits
constexpr std::uint32_t kBlinkOff = 0x800;     // hidden this tick (kBlink)
constexpr std::uint32_t kDrift = 0x1000;       // add (DriftX, DriftY) once
}  // namespace explosion_flags

// Unit template SpecialBehavior values used by the model.
namespace behaviour {
constexpr int kNone = 0, kHarvester = 1, kCarryall = 2, kEngineer = 3, kSaboteur = 4, kSandworm = 5,
              kMcv = 6, kDevastator = 7, kFrigate = 8, kOrnithopter = 9, kDeathHand = 10, kSardaukar = 11, kFremen = 12,
              kThumper = 14, kBird = 15;
}
// Building template SpecialBehavior values used by the model.
namespace building_behaviour {
constexpr int kOutpost = 1, kConstructionYard = 2, kRefinery = 4, kSilo = 5, kRepairPad = 6, kLightFactory = 7,
              kHeavyFactory = 8, kStarport = 13, kWall = 14, kConcrete = 15, kTurret = 16;
}

// GroupIDs table (Templates.bin groupIds[86], @0x6d60d8 in the exe; names from D2kEditor
// game_lists.ini). Indexes used by the economy/production code.
namespace group_ids {
constexpr int kBarracksQueue = 1, kWorQueue = 2, kLightFactoryQueue = 3, kHeavyFactoryQueue = 4, kHiTechQueue = 5,
              kAtHiTechQueue = 6, kRefinery = 7, kAtreidesPalace = 10, kHarkonnenPalace = 11, kOrdosPalace = 12,
              kFrigate = 14, kMcv = 15, kHarvester = 16, kCarryall = 17, kSaboteur = 20, kFremen = 21,
              kHarvesterUnload = 64, kHarvesterDust0 = 65, kRefineryProcessing = 80;
}

// The original's battlefield view size at its default 640x400 resolution (BattleFieldWidth =
// GameWidth - SideBarWidth, BattleFieldHeight = GameHeight - _OptionsBarHeight). The model reads
// it in one place, CSide_46CF10 (ornithopter strike entry point); our view size never feeds the
// simulation, so the original default is used for determinism.
constexpr int kBattleFieldWidth = 480, kBattleFieldHeight = 380;

// Production progress runs 0..kBuildDone (0x5a00) for buildings, units and upgrades.
constexpr int kBuildDone = 0x5a00;

enum class UnitState : std::int32_t {
    Dummy = 0, None = 1, Idle = 2, Guarding = 3, AttackingUnit = 4, AttackingBuilding = 5,
    AttackingTile = 6, Moving = 7, LeavingBuilding = 8, MovingAway = 9, AttackingTileOnRoute = 10,
    MovingToHarvest = 11, Harvesting = 12, MovingToRefinery = 13, EnteringRefinery = 14,
    UnloadingSpice = 15, LeavingRefinery = 16, Dead = 17, AwaitingAirlift = 18, Airlifted = 19,
    Invalid = 20, FlyingDown = 21, FlyingUp = 22, Infiltrating = 23, MovingToRepairPad = 24,
    Repairing = 25, LeavingRepairPad = 26, Wandering = 27, Eating = 28, Sleeping = 29,
    Deploying = 30, Selfdestruct = 31, Thumpering = 32, State33 = 33,
};

// Object kinds of a CSide pool slot (slot +0x90).
enum class SlotKind : std::uint8_t { Free = 0, Unit = 1, Building = 2, Bullet = 3, Explosion = 4 };

// The 0x94-byte Unit object (offsets in comments). Defaults are the zero wipe of a freed slot.
struct Unit {
    bool alive = false;  // slot currently holds a unit (not original; kind == Unit)
    int index = -1;      // +0x8C MyIndex (slot 0..999)
    int side = 0;
    int type = 0;        // +0x18 template index

    std::uint32_t speed = 0;      // +0x00 16.16 px/tick for the current tile step
    std::int32_t health = 0;      // +0x04
    std::uint32_t flags = 0;      // +0x08
    int enemyIndex = kNoIndex;    // +0x0C
    UnitState state = UnitState::Dummy;     // +0x10
    UnitState oldState = UnitState::Dummy;  // +0x14
    bool selected = false;                  // +0x19
    std::uint8_t turnDelay = 0, turretTurnDelay = 0, reloadDelay = 0;  // +0x1A/+0x1B/+0x1C
    // +0x1D flicker: set to 7 when the player orders an attack on this unit (SetUnitToFlicker
    // 0x49f310); the original decrements it once per DRAWN frame (Display list 0x428e30).
    std::uint8_t flicker = 0;
    int animFrame = 0;                     // +0x1E CurrentAnimFrame (u8)
    int animType = 0;                      // +0x20 CurrentAnimType
    std::uint8_t lying = 0;                // +0x24
    std::uint8_t animDelay = 0;            // +0x25 (infantry anim counter / worm sleep / harvest delay)
    int enemySide = 0;                     // +0x26
    std::uint8_t smokeTimer = 0;           // +0x27 damaged-vehicle smoke (set by DamageTiles)
    std::uint8_t groupId = 0xFF;           // +0x28
    std::uint8_t byte29 = 0;               // +0x29 (spice load / misc counter)
    std::uint8_t lastAttacked = 0xFF;      // +0x2A ticks/8 since last attacked
    std::uint8_t lastFired = 0xFF;         // +0x2B
    std::uint8_t lastMoved = 0xFF;         // +0x2C ticks/8 at the same tile (UNCERTAIN)
    std::uint8_t deadStateTimer = 0;       // +0x2F
    std::uint8_t attackerSide = 0xFF;      // +0x30
    std::uint8_t stealthTimer = 0;         // +0x31
    int attackerIndex = kNoIndex;          // +0x32

    std::int32_t posX = 0, posY = 0, posZ = 0;  // +0x34/+0x38/+0x3C 16.16 pixels
    std::int32_t stepTargetX = 0, stepTargetY = 0;  // +0x40/+0x44 pixel target of the step
    int targetX = 0, targetY = 0;               // +0x48/+0x49 destination tile
    int blockFromX = 0, blockFromY = 0;         // +0x4A/+0x4B tile the unit occupies
    int blockToX = 0, blockToY = 0;             // +0x4C/+0x4D next tile
    std::uint8_t posStepsMax = 0, posSteps = 0;  // +0x4E/+0x4F
    int rememberX = 0, rememberY = 0;           // +0x50/+0x51 guard post / saved target
    int cellspotTo = 0, cellspotFrom = 0;       // +0x52/+0x53 infantry sub-cell 0..4
    int facing = 0;                             // +0x54 body 0..31
    int facingTurret = 0, facingTurretTarget = 0;  // +0x55/+0x56
    int refineryIndex = kNoIndex;               // +0x58
    int repairPadIndex = kNoIndex;              // +0x5A
    std::array<std::uint8_t, kRouteLength> routeX{}, routeY{};  // +0x5C/+0x68, stored reversed
    int routeSteps = 0;                         // +0x74
    int routeMode = 0;                          // +0x75 0..3
    int routeTotalSteps = 0;                    // +0x76 (u16)
    int routeRetryDelay = 0;                    // +0x78 (u16)
    bool routeBlocked = false;                  // +0x7A "blocked last time"
    std::uint8_t byte2E = 0;                    // +0x2E delivery index of a delivered unit / deviation timer
    std::uint8_t oldSide = 0;                   // +0x2D side a deviated unit (flag 0x1000) returns to
    // Community event system (1.06p+, game/mission_ext.cpp): +0x0E Tag, +0x0F PrevWasSelected
    // (padding bytes in vanilla; always 0 for vanilla missions).
    std::uint8_t tag = 0;
    bool prevSelected = false;

    int tileX() const { return fixToTile(posX); }
    int tileY() const { return fixToTile(posY); }
};

struct Building {
    bool alive = false;  // slot currently holds a building (not original)
    int index = -1;      // +0x42 / slot index
    int side = 0;        // +0x40 MySide
    int type = 0;        // +0x34 template index
    std::int32_t posX = 0, posY = 0;  // +0x00/+0x04 16.16
    int attackTileX = 0, attackTileY = 0;  // +0x14/+0x15 (state 6)
    int tileX = 0, tileY = 0;         // +0x16/+0x17 MinX/MinY
    int maxX = 0, maxY = 0;           // +0x18/+0x19
    int wallFrame = 0;                // +0x20
    int facing = 0;                   // +0x21 turret facing 0..31
    std::int32_t health = 0;          // +0x24
    std::uint32_t flags = 0;          // +0x28
    int enemyIndex = kNoIndex;        // +0x2C
    int state = 2;                    // +0x30 (2 normal, 4/5/6 attacking, 17 dead)
    bool selected = false;            // +0x35
    std::uint8_t turretTurnDelay = 0, reloadDelay = 0;  // +0x36/+0x37
    std::uint8_t flicker = 0;         // +0x38 (SetBuildingToFlicker 0x49f320 = 7; renderer decrements)
    std::uint8_t deadTimer = 0;       // +0x3b set to 30 by DestroyBuilding, counts down in state 17
    int enemySide = 0;                // +0x3C
    std::uint8_t smokeTimer = 0;      // +0x3d damage smoke (UpdateBuilding, BUILEXP.BIN points)
    std::uint8_t buildupCounter = 0;  // +0x3e build-up (counts up) / sell (counts down); frame = /3
    std::uint8_t animCounter = 0;     // +0x3f animation overlay; frame = animCounter / animationSpeed
    std::uint8_t lastAttacked = 0xFF; // +0x41
    std::uint32_t buildupTick = 0;    // +0x44 gGameTicks when the build-up / sell animation started
    std::uint32_t timeAdded = 0;      // +0x48
    std::uint32_t smokeBits = 0;      // +0x4c which smoke points are active (bit i)
    std::uint8_t attackerSide = 0xFF; // +0x50
    int attackerIndex = kNoIndex;     // +0x52
    // Community event system (mission_ext.cpp): +0x0E Tag, +0x0F PrevWasSelected, +0x3A
    // SpecialPurpose (unused by the vanilla model).
    std::uint8_t tag = 0;
    bool prevSelected = false;
    std::uint8_t specialPurpose = 0;
};

// The Bullet object (projectile in flight; offsets of the original 0x94-byte slot).
struct Bullet {
    bool alive = false;  // slot currently holds a bullet (not original)
    int index = -1;      // slot index
    int side = 0;        // +0x1A MySide
    std::int32_t moveSteps = 0;    // +0x00 ticks of flight left
    std::uint32_t flags = 0;       // +0x04 weapon_flags (copied from the weapon)
    std::int32_t speedX = 0, speedY = 0;  // +0x08/+0x0C 16.16 px per tick (straight flight)
    std::uint32_t speed = 0;       // +0x10 weapon projectileSpeed (16.16)
    int firerIndex = kNoIndex;     // +0x14 unit/building of `side` that fired (kNoIndex once dead)
    int homingIndex = kNoIndex;    // +0x16 homing target unit
    int type = 0;                  // +0x18 weapon index
    int homingSide = -1;           // +0x19 (signed char; -1 = none)
    std::uint8_t byte1B = 0;       // +0x1B weapon byte 19 (unused by the vanilla model)
    std::uint16_t targetX = 0, targetY = 0;  // +0x1C/+0x1E target in pixels (u16 like the original)
    std::int32_t delay = 0;        // +0x20 ticks before it starts; not drawn while non-zero
    std::int32_t posX = 0, posY = 0, posZ = 0;  // +0x24/+0x28/+0x2C 16.16 px; PosZ = height
    std::int32_t fallSpeed = 0;    // +0x34 vertical speed (arc / falling)
    int animFrame = 0;             // +0x44 frame of projectile art
    int dir = 0, prevDir = 0;      // +0x45/+0x46 homing direction in projectile-art steps
    std::uint8_t tag = 0;          // community event system Tag (mission_ext.cpp)
};

// The Explosion object (animation; also muzzle flashes, smoke, debris puffs).
struct Explosion {
    bool alive = false;  // slot currently holds an explosion (not original)
    int index = -1;
    int side = 0;
    int type = 0;                  // +0x00 explosion / animation art index
    int frame = 0;                 // +0x01 animation frame (muzzle flash: the shooter's facing)
    std::uint8_t delay = 0;        // +0x02 ticks to the next frame (blink: step 0..7)
    std::uint32_t flags = 0;       // +0x04 explosion_flags
    int unitIndex = kNoIndex;      // +0x08 associated unit of `side`: freed when it dies
    std::int32_t posX = 0, posY = 0, posZ = 0;  // +0x0C/+0x10/+0x14 16.16 px
    std::int32_t driftX = 0, driftY = 0;        // +0x30/+0x34 (explosion_flags::kDrift)
    std::uint8_t tag = 0;                       // community event system Tag (mission_ext.cpp)
};

// One production slot (CSide +0x2651c building, +0x26530 units[10] stride 0x14, +0x265f8 upgrade).
struct ProductionSlot {
    int type = -1;                    // +0x00 (s16) unit / building template, -1 = empty
    std::uint16_t progress = 0;       // +0x02 0..kBuildDone
    std::uint16_t prevProgress = 0;   // +0x04
    float costAccum = 0;              // +0x08 float sum of the per-tick cost fractions
    std::int32_t paid = 0;            // +0x0C credits deducted so far
    bool onHold = false;              // +0x10
    bool placing = false;             // building slot +0x11 (0x2652d), cleared on pick/place
};

// Extended unit queue entry (NOT ORIGINAL; re/notes/economy.md §10). Side::autoQueue lists them in
// selection order; the entries of one factory group (unit prereq1BuildingGroup, the original's
// one-unit-per-group rule) form that factory's queue. `remaining` counts units not yet started
// (the one in the group's ProductionSlot is not included); kAutoInfinite = build forever.
inline constexpr int kAutoInfinite = -1;
inline constexpr int kAutoMaxCount = 99;
struct AutoBuildEntry {
    int type = -1;
    int remaining = 0;
};

// Carryall pickup queue entry (CSide +0x2660c, stride 0x14, 32 entries; CSide__AddToQueue 0x46cd00).
struct PickupEntry {
    int x = 0, y = 0;                 // +0x00/+0x01 where the unit waits (its BlockFrom)
    int destX = 0, destY = 0;         // +0x02/+0x03 where it wants to go (its Target)
    int priority = 0;                 // +0x04
    int state = 0;                    // +0x05 0 free, 1 waiting, 2 carryall assigned, 3 cancelled, 4 carried
    int unitIndex = kNoIndex;         // +0x06
    int carryallIndex = kNoIndex;     // +0x08
    std::uint32_t waitTicks = 0;      // +0x0C (counted every 8th tick, gives up after 0x4f)
    UnitState origState = UnitState::None;  // +0x10 state to return to
};

// Carryall/frigate delivery (CSide +0x262ec, stride 0x38, 10 entries).
struct Delivery {
    int x = 0, y = 0;                 // +0x00/+0x01 drop tile
    bool started = false;             // +0x02 carryall sent
    bool active = false;              // +0x03
    std::uint32_t time = 0;           // +0x04 gGameTicks at which to send it
    std::array<std::uint8_t, 40> units{};  // +0x08 unit types, 0xFF end, 0xFE delivered
    int type = 0;                     // +0x35 1 carryall, 2 starport frigate
    int deployAction = 0;             // +0x36 4 = free harvester (no reinforcement speech)
    std::uint8_t tag = 0;             // community patch: Tag given to the delivered units (delivery-tag.c)
    // Not original (cheat reinforcements): map edge the carryall enters from, 0 north, 1 east,
    // 2 south, 3 west; -1 = the nearest edge to the drop tile (Carryall_GetEntryPoint).
    std::int8_t entryEdge = -1;
};

// Pool links of one CSide slot (+0x7C next, +0x80 prev, +0x90 kind).
struct ObjectLink {
    int next = -1, prev = -1;
    SlotKind kind = SlotKind::Free;
};

struct Side {
    int id = 0;
    int house = -1;                   // +0x24253 fHouseID
    std::int32_t cash = 0;            // +0x2425C CashReal
    std::int32_t spice = 0;           // +0x24254 SpiceReal (stored spice; counts as credits)
    std::int32_t maxStorage = 0;      // +0x24258 2000 per refinery + 1500 per silo
    std::int32_t cashDrip = 0;        // +0x24260 pending cash change, paid out gradually
    std::int32_t spiceDrip = 0;       // +0x24264 pending spice change
    int techLevel = 0;                // NetPlayersTechLevel
    std::uint32_t startingCredits = 0; // NetPlayersStartingCredits (.MIS; event 8 "Set Cash")
    bool participates = true;         // +0x24250 (CreateSide sets 1 for all 8)

    // Slot arrays indexed by object index; a slot holds one kind of object (see links[]).
    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<Bullet> bullets;
    std::vector<Explosion> explosions;

    // CSide object pool (CSide::Init 0x46b990, AllocObject 0x46bd40, LinkObject 0x46bd60,
    // FreeObject 0x46bdb0): one LIFO free list over all 1000 slots (first allocation = 999)
    // and per-kind doubly linked lists in insertion order (update order follows them).
    std::array<ObjectLink, kObjectsPerSide> links{};
    int firstUnit = -1, lastUnit = -1, firstBuilding = -1, lastBuilding = -1;
    int firstBullet = -1, lastBullet = -1, firstExplosion = -1, lastExplosion = -1;
    std::vector<int> freeList;  // top = back()

    // Statistics / counters kept by the model.
    int unitsBuilt = 0, buildingsBuilt = 0, unitsLost = 0, buildingsLost = 0;
    int unitsKilled = 0, buildingsKilled = 0;  // by this side (+0x24c90 / +0x24c94)
    int harvesterCount = 0, carryallCount = 0;
    std::array<int, formats::kMaxUnitTypes> unitsBuiltPerType{}, unitsExistPerType{}, unitsLostPerType{};
    std::array<int, formats::kMaxBuildingTypes> buildingsBuiltPerType{};
    std::array<int, formats::kMaxBuildingTypes> buildingsExistPerGroup{};
    std::array<int, 11> primaryBuilding{};  // +0x26098.. per behaviour (ModelAddBuilding step 5)

    // Power (FUN_0046ca00): output of the producers (scaled by health, +200 base when any),
    // demand of the consumers (+200), percent = output * 100 / demand (200 when no demand).
    std::uint32_t powerOutput = 0, powerDemand = 0, powerPercent = 0;  // +0x245a8/+0x245ac/+0x245b0
    std::uint32_t powerPercentPrev = 0;                                // +0x245b4 (for the power sounds)
    std::int32_t spiceHarvested = 0;                                   // +0x2477c statistic
    // Sidebar (CSide__UpdateBuildingAndUnitIconsAndBaseBoundaries 0x46be50).
    std::array<std::uint8_t, formats::kMaxBuildingTypes> buildable{};  // +0x24718 sticky per template
    std::array<std::uint8_t, formats::kMaxBuildingTypes> upgrades{};   // +0x24544 level per building group
    std::vector<int> buildingIcons, unitIcons;                         // +0x260bc / +0x261b4 template indexes
    std::size_t prevBuildingIcons = 0, prevUnitIcons = 0;              // +0x260b8 / +0x261b0
    int baseMinX = 0, baseMinY = 0, baseMaxX = 0, baseMaxY = 0;        // +0x262ea/+0x262e8/+0x262eb/+0x262e9
    // Production.
    ProductionSlot buildingQueue;                                      // +0x2651c
    std::array<ProductionSlot, 10> unitQueue{};                        // +0x26530 (one per factory kind)
    ProductionSlot upgradeQueue;                                       // +0x265f8 (type = template)
    std::vector<AutoBuildEntry> autoQueue;                             // not original: extended unit queue
    // Carryalls.
    std::array<PickupEntry, 32> pickup{};                              // +0x2660c
    int pickupCount = 0;                                               // +0x2688c queue length in use
    int carryallsAlive = 0;                                            // +0x2688d
    std::array<Delivery, 10> deliveries{};                             // +0x262ec
    // Starport (re/notes/starport.md): unit list, prices/stock upkeep (FUN_0046e7a0 /
    // FUN_0046ddc0), the order being assembled and the frigate in flight.
    std::array<int, 8> starportTypes{};                                // +0x262a8 (-1 = none)
    std::array<int, formats::kMaxUnitTypes> starportPrice{}, starportStock{}, starportOrdered{};  // +0x24448/+0x24358/+0x24268
    std::uint32_t starportPriceTimer = 0, starportStockTimer = 0;      // +0x24538/+0x2453c
    std::int32_t starportSpent = 0;   // +0x24540 credits paid for the picked units (refund on loss)
    bool starportPurchasePending = false;  // +0x262a4 purchase order queued (blocks further picks)
    bool starportFrigateBusy = false;      // +0x262a5 order sent / frigate on its way
    int frigateSpeech = 0;            // unused (the speech queue's repeat guard does it); kept for savegame
    bool starportInitialised = false;                                  // not original

    void initPool();                   // CSide::Init
    int allocObject();                 // -1 when the pool is exhausted
    void linkObject(int index, SlotKind kind);
    void freeObject(int index);
    int nextObject(int index) const { return links[index].next; }
};

// Crate / spice bloom record (gCrates 0x517b98, stride 12, 30 records; re/notes/crates.md).
// Cells holding an active record have cell_flags::kCrate. Renderer: draw active records at
// (x, y); `image` is the frame offset of the crate art (spice blooms 4..7 as they grow).
struct Crate {
    std::uint8_t x = 0, y = 0;    // +0/+1 tile
    std::uint8_t type = 0;        // +2: 0 cash, 1 explosion, 2 reveal map, 3 shroud map, 4 units,
                                  //     5 stealth; 7..10 spice bloom stages (7 = new, 10 = ripe)
    bool active = false;          // +3
    std::uint8_t image = 0;       // +4 crate image (blooms: 4 + stage)
    std::uint8_t respawns = 0;    // +5 bloom regrowths left (RecycleCrate)
    std::uint32_t timer = 0;      // +8 ticks left (crates vanish, blooms grow / respawn)
};
constexpr int kMaxCrates = 30;

// Pixel particle (0x6b9920, 4096 records of 24 bytes; FUN_0045d440 clear, FUN_0045d460 add,
// FUN_0045d4f0 per-tick update, Display__DrawParticles 0x45d550). Only the sandworm's sand
// spray (Worm_SpawnSandParticles 0x45d730) creates them; they are render-only (no rand).
// The colour is the tileset pixel under the worm darkened to 85 % (FUN_0045d650); the game has
// no tileset pixels, so the record keeps the tile and pixel and the renderer resolves it.
struct Particle {
    float x = 0, y = 0;        // +0/+4 world pixels
    float vx = 0, vy = 0;      // +8/+0xc per tick
    std::uint32_t life = 0;    // +0x10 ticks left
    std::uint16_t tile = 0;    // colour source (+0x14 holds the 16-bit colour in the original)
    std::uint8_t tilePx = 0, tilePy = 0;
    bool active = false;       // +0x16
};
constexpr int kMaxParticles = 0x1000;

struct Cell {
    std::uint16_t tile = 0;     // +0 tileset tile
    std::uint32_t flags = 0;    // +4 (cell_flags)
    std::uint8_t shroud = 0x11; // +8 local player's shroud
    std::uint8_t damage = 0;    // +9 terrain damage
    // Not in the original (it walks the owner's building list): which building occupies this
    // solid tile, kept for the renderer's wall linking. -1 = none.
    std::int16_t buildingSide = -1, buildingIndex = -1;

    int spice() const { return static_cast<int>((flags & cell_flags::kSpiceMask) >> cell_flags::kSpiceShift); }
    int owner() const { return static_cast<int>(flags & cell_flags::kOwnerMask); }
    int terrain() const { return static_cast<int>(flags >> cell_flags::kTerrainShift); }
};

// Original (uncropped) frame size of an image, TImage +0x19 / +0x1a (re/notes/rendering.md §0).
struct ArtSize {
    std::uint8_t height = 0, width = 0;
    int yoff = 0;  // TImage +0x0c hotspot Y (buildings: uncropped height minus the cropped top rows)
};
// Building art frame sizes the model reads from the loaded images: DestroyBuilding (debris
// origin), UpdateBuilding (smoke points) and the turret muzzle flash. Indexed like the
// original tables: base[art] (0x6e5880), normal[art * 32 + frame] (0x6e5c40). Optional: when
// absent or zero, the template's artWidth/artHeight are used instead.
struct BuildingArtSizes {
    std::vector<ArtSize> base;
    std::vector<ArtSize> normal;
};

// Static game rules shared by the world (loaded once from bin/).
// Map rules added by the community patches (1.06p+, ref/dune2000-sp-patches rules.ini.c),
// read from rules.ini and the mission .ini [Vars] (platform/mission_ini.*). The defaults are
// the patch's, which equal the original behaviour. Only the rules the model implements are
// here (docs/community-campaigns.md lists the others).
struct RulesExt {
    bool infiniteSpice = false;                     // InfiniteSpice: a level-3 spice cell is never thinned (0x4995a5)
    bool buildingsAlwaysNeedPrerequisites = false;  // buildable flags reset every update (0x46bfea)
    std::uint32_t guardModeRadius = 192;            // guard-mode chase radius in px (0x498ce0)
    std::uint32_t costPercentageEasy = 75, costPercentageHard = 125;          // GetDifficultyCostPercentage
    std::uint32_t buildSpeedPercentageEasy = 125, buildSpeedPercentageHard = 75;  // ..BuildSpeedPercentage
    bool returnCreditsToSpiceStorage = false;       // refunds go to spice storage first (return-credits-to-spice-storage.c)
    std::uint8_t infantryReleaseLimit = 4;          // max infantry from a destroyed/sold building (release-infantry.c)
    std::uint8_t infantryReleaseChance = 5;         // retries per infantry (max retries = limit * chance)
    bool uncloakRemainingStealthUnit = true;        // UncloakRemainingStealthUnit in the main loop
    bool showNeutralBecomeHostileMsg = true;        // (message only; not modelled)
    bool intervalsAreOffByOneTick = true;           // extended Interval condition (event-conditions.c)
};

struct Rules {
    const formats::Templates* templates = nullptr;
    const formats::SpeedTable* speed = nullptr;
    const formats::ArmourTable* armour = nullptr;          // ARMOUR.BIN (needed for damage)
    const BuildingArtSizes* buildingArt = nullptr;         // optional, see above
    const std::vector<std::uint8_t>* buildingSmoke = nullptr;  // BUILEXP.BIN raw (100 x 0x21 bytes)
    const formats::Vars* vars = nullptr;                   // VARS.BIN; null = shipped defaults
    // bin/CIRCLES.BIN (680 bytes): shroud reveal masks for radius 0..7, (2r+1)^2 bytes each,
    // 0x00 = reveal, 0xFF = keep (gShroudCirclePtrs, loaded by Setup__LoadGraphicsFile).
    // Null = the same masks built in (shroud.cpp).
    const std::vector<std::uint8_t>* circles = nullptr;
    const RulesExt* ext = nullptr;  // null = the defaults (vanilla behaviour)
};

// --- Economy / production API types (see production.cpp and re/notes/economy.md) ---
struct SideEconomy {
    std::int32_t credits = 0;   // what the sidebar shows: CashReal + SpiceReal
    std::int32_t cash = 0, spice = 0, storage = 0;  // CashReal, SpiceReal, MaxStorage
    std::int32_t cashDrip = 0, spiceDrip = 0;       // pending (not yet paid out) changes
    std::uint32_t powerOutput = 0, powerDemand = 0, powerPercent = 0;
};
struct BuildOption {
    int type = -1;        // unit or building template index
    int group = -1;       // unit group / building group
    int cost = 0;         // difficulty-adjusted cost (w__GetUnitCost / GetBuildingCost)
    int iconFrame = -1;   // DATA.R16 frame: Templates::firstUnitIconFrame + type (units) or
                          // firstBuildingIconFrame + type (buildings)
    int upgradeLevel = 0; // upgrades only: current level of the group
};
// One starport slot (Display__DrawStarportSideBarMenu 0x427690 order: left/right, 4 rows).
struct StarportOption {
    int type = -1;        // unit template (house version), -1 = empty slot
    int iconFrame = -1;   // DATA.R16 sidebar icon
    int price = 0;        // current price (+0x24448)
    int stock = 0;        // units available (+0x24358, 0..5)
    int ordered = 0;      // picked into the current order (+0x24268)
    bool available = false;  // can be picked now: stock, no frigate on its way, < 6 picked (drawn bright)
};
struct QueueState {
    bool active = false;  // a type is queued in this slot
    int slot = -1;        // unit queue slot 0..9 (units only)
    int type = -1;
    int progress = 0;     // 0..kBuildDone
    int total = kBuildDone;
    float fraction = 0;   // progress / total
    int paid = 0;         // credits already deducted
    int cost = 0;         // full cost
    bool onHold = false;
    bool ready = false;   // progress == kBuildDone (buildings: ready to place)
};
// Extended unit queue state of one unit type (not original): count = units still to build
// including the one in production; infinite = repeats forever.
struct AutoQueueState {
    int count = 0;
    bool infinite = false;
    int position = -1;  // order among the queued types of its factory group (0 = next/current), -1 none
};

// A sound the original would play this tick; src/game has no audio, the frontend plays them
// (re/notes/audio.md). `id` is a sound table index (template sound field) or -1 with `name`
// set for named sounds (S_MANDYING%d, mentat speech, ...).
//  - tileX >= 0: PlaySoundAt 0x46f540 (effect channels 1..5, pan/volume from the view).
//  - tileX < 0: QueueAudioToPlay 0x46f820 (the speech channel 0, one at a time, queued)
//    with its arguments `queue` / `throttle` / `priority`; `house` = PlayMentatSound
//    0x46f900 (the local player's house prefix S_/H_/O_ is prepended to `name`).
struct SoundEvent {
    int id = -1;
    const char* name = nullptr;
    int variant = 0;   // e.g. the %d of S_MANDYING%d
    int tileX = -1, tileY = -1;  // -1 = not positional
    bool house = false;          // PlayMentatSound: H_/O_/S_ + name by the local player's house
    bool queue = false;          // QueueAudioToPlay arg 2 (false: only while the queues are empty)
    int throttle = 0;            // arg 3: per-sample repeat guard in game ticks
    int priority = 0;            // arg 4: queue 0 / 1 (1 is played first)
    bool onScreen = false;       // the caller only plays it when (spotX, spotY) is inside the battlefield view
    bool unique = false;         // ... and not while the sample is already playing (FUN_0046f4f0)
    int spotX = -1, spotY = -1;  // onScreen: the map pixel that must be in view
};

// A shot (render-only output, not part of the model state): every bullet ModelAddBullet
// created, with its muzzle point and aim. Machine guns' bullets (speed 351 px/tick) are created
// and land within the same tick, so the frontend never sees them as objects; the Modern
// effects draw muzzle flashes and tracers from these instead. Kept for kShotEventTicks ticks.
struct ShotEvent {
    std::uint32_t tick = 0;  // gGameTicks when the bullet was created
    int side = 0, weapon = 0, delay = 0;
    int firer = 0xFFFF;      // firing unit / building slot (kNoIndex: none)
    int srcX = 0, srcY = 0, dstX = 0, dstY = 0;  // pixels
};
constexpr std::uint32_t kShotEventTicks = 16;

// Mission outcome for the local player (gWin / gLose).
enum class GameEnd { None, Won, Lost };

// An in-game text message queued by a mission event (event 17 "Show Message": QueueMessage
// of GetTextString(id) plus the S_CHATMSG sound). `textId` indexes the TEXT.UIB string table.
struct MissionMessage {
    int textId = 0;
    std::uint32_t tick = 0;  // gGameTicks when queued
};

// Community extended event system (1.06p+ .MIS of 143014 bytes; game/mission_ext.cpp,
// docs/extended-events.md). Raw 72-byte events / 28-byte conditions as in the file: the event
// flags (+0x2E), the condition fields used as internal state (val3 / val4 / arg1 / side) and the
// variables change while the mission runs.
struct ExtVariable {
    std::int32_t value = 0, oldValue = 0;
    std::uint32_t ticks = 0;   // gGameTicks of the last write (old value kept once per tick)
};
struct ExtMission {
    bool active = false;
    std::vector<std::array<std::uint8_t, 72>> events;
    std::vector<std::array<std::uint8_t, 28>> conditions;
    std::vector<std::int32_t> next, elseIndex;     // gEventExtraData (block structure)
    std::array<ExtVariable, 256> vars{};           // gEventVariableArray
    std::int32_t tickRandom = 0;                   // tick_random_value (rand() once per tick)
    std::vector<std::uint16_t> backupTile;         // GameMapTileStruct back_up_tile_index per cell
    std::vector<std::uint32_t> tileAtr;            // _TileBitflags of the tileset (ChangeMapTile)
    std::int32_t exitCount = 0, breakCount = 0, continueCount = 0;  // 0 between ticks
};
// UI requests from extended events (not saved; the frontend consumes them).
struct ExtUiRequests {
    bool viewport = false;
    int viewportX = 0, viewportY = 0;  // map pixels, top-left (centred = false) or centre (true)
    bool viewportCentred = false;
    std::string music;                 // Play Music: file name, "*" = random track
};

// Skirmish (GameType 1) parameters normally set by the lobby. The local player is side 0;
// computer players take sides 1..aiPlayers (Setup_SpawnMapObjects 0x469790 skirmish pass).
struct SkirmishSetup {
    const formats::TechPos* techPos = nullptr;  // bin/TECHPOS.BIN (null: MCVs only)
    int playerHouse = 0;              // gMisHouseId[MySideID] (0 Atreides, 1 Harkonnen, 2 Ordos)
    int aiPlayers = 1;                // gNetAIPlayers
    int techLevel = 7;                // gNetTechLevel
    std::uint32_t startingCredits = 5000;  // gNetStartingCredits (the AIs get 7000/10500/14000)
    int unitCount = 10;               // gNetUnitCount: TECHPOS entries used per player (<= 10)
    int worms = 1;                    // gNetWorms
    int difficulty = 1;               // gDifficultyLevel
    // Additions (defaults keep the old behaviour): srand seed of the game (the original seeds
    // with time()), the player's colour (gNetPlayerColor, 0..7) and the crates option.
    std::uint32_t seed = 1;
    int playerColor = 0;
    bool crates = false;              // gNetCrates: NumberOfCrates (VARS) random crates on the map
};

// --- Cheats (cheats.cpp; not original). Per-side build availability override of one
// building/unit template: Default = the original rules (prerequisites, tech level, house).
enum class BuildOverride : std::uint8_t { Default = 0, Allowed = 1, Blocked = 2 };
// Cheat reinforcement (not original) waiting for its time: at `due` (gGameTicks) it becomes
// ordinary carryall Delivery records (the .MIS Reinforcement event path, startReinforcement).
struct ScheduledReinforcement {
    int id = 0;
    int side = 0;
    std::uint32_t due = 0;          // game tick
    std::uint32_t ordered = 0;      // game tick the order was executed
    int x = -1, y = -1;             // drop tile; -1 = near the side's base (when it is due)
    int edge = -1;                  // Delivery::entryEdge
    std::vector<std::uint8_t> units;  // unit types (any number; 25 per carryall)
};

struct CheatState {
    std::array<std::array<std::uint8_t, formats::kMaxBuildingTypes>, kMaxSides> buildingOverride{};  // BuildOverride
    std::array<std::array<std::uint8_t, formats::kMaxUnitTypes>, kMaxSides> unitOverride{};
    // Free deliveries not yet handed to a carryall/frigate (unit types; at most 39 per trip,
    // queued whenever one of the side's 10 delivery slots is free).
    std::array<std::vector<std::uint8_t>, kMaxSides> deliveryBacklog{};
    std::array<std::uint8_t, kMaxSides> instantBuild{};  // production finishes in one tick (still paid)
    bool used = false;                                   // any cheat order was executed
    std::vector<ScheduledReinforcement> reinforcements;  // pending, in order of execution
    int nextReinforcementId = 1;
};

// Factory rally point (not original): units leaving the factory (and starport deliveries to
// it) get a move order to (x, y). `timeAdded` identifies the building (slots are reused).
struct RallyPoint {
    int side = 0, building = kNoIndex;
    std::uint32_t timeAdded = 0;
    int x = 0, y = 0;
};
// A unit on its way out of a factory with a rally point: moved there once it has left.
struct RallyPending {
    int side = 0, unit = kNoIndex;
    std::uint32_t since = 0;
    int x = 0, y = 0;
};

// What a smart order (World::smartOrder) did.
//   Friendly: the target is an own/allied object and no Ctrl - the original selects it instead
//             (the frontend decides; Modern controls move there).
//   TurretAttackUnit / TurretAttackBuilding: no units selected, the selected own barrel
//             building(s) were ordered to fire (GenerateBuildingAttack{Unit,Building}Order).
enum class SmartOrder { None, Move, AttackUnit, AttackBuilding, AttackTile, Friendly, TurretAttackUnit, TurretAttackBuilding };

class World {
public:
    // Mission start, campaign path (Game_Init 0x448550 steps 1-3: CreateSide for all sides,
    // .MIS globals, LoadMapData + TILEATR, Setup_SpawnMapObjects 0x469790).
    // `scriptSide` (not original, "Play as any side", docs/play-as.md): the side the mission was
    // written for (Missions/<map>.ini SideId) when the human plays another side `mySide`; -1 or
    // == mySide = a normal start. The chosen side loses its computer player, the mission's side
    // gets one (its .MIS AI block), and the outcome follows the play-as rules (mission.cpp).
    World(const formats::MapFile& map, const formats::MisFile& mission, const formats::TileAtr& tileAtr,
          const formats::TileData& tileData, Rules rules, int mySide = 0, int scriptSide = -1);
    // Skirmish start on a multiplayer map with _SPAWN.MIS: AI sides, random AI houses, an MCV
    // and TECHPOS starting units per player at the map's spawn locations; default win/lose rules.
    World(const formats::MapFile& map, const formats::MisFile& mission, const formats::TileAtr& tileAtr,
          const formats::TileData& tileData, Rules rules, const SkirmishSetup& setup);
    // Empty map (all cell flags 0, tile 0) for tests and tools; diplomacy: all enemies.
    World(int width, int height, Rules rules, int mySide = 0);

    // One model tick: gGameTicks++, pending orders, ModelUpdate 0x458e90.
    void tick();

    int width() const { return width_; }
    int height() const { return height_; }
    Cell& cell(int x, int y) { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    const Cell& cell(int x, int y) const { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    std::array<Side, kMaxSides>& sides() { return sides_; }
    const std::array<Side, kMaxSides>& sides() const { return sides_; }
    Unit* unit(int side, int index);        // GetUnit: live unit in that slot, or null
    Building* building(int side, int index);

    std::uint32_t gameTicks() const { return gameTicks_; }
    Random& rng() { return rng_; }
    const Rules& rules() const { return rules_; }
    int mySide() const { return mySide_; }
    std::uint8_t diplomacy(int observer, int other) const { return diplomacy_[observer & 7][other & 7]; }
    void setDiplomacy(int observer, int other, std::uint8_t v) { diplomacy_[observer & 7][other & 7] = v; }

    // Player orders, queued and executed at the start of the next tick (ProcessOrder 0x457810).
    void orderMove(int side, int unitIndex, int tileX, int tileY);  // order 0x13
    void orderStop(int side, int unitIndex);                        // ExecStopOrder 0x4576b0
    // ProcessOrder cases 0x14/0x15/0x16 for one unit (the Generate*Order filters are applied
    // by smartOrder; these check what ProcessOrder itself checks).
    void orderAttackUnit(int side, int unitIndex, int targetSide, int targetIndex);
    void orderAttackBuilding(int side, int unitIndex, int targetSide, int targetIndex);
    void orderAttackTile(int side, int unitIndex, int tileX, int tileY);
    // ProcessOrder cases 0xb / 0xc (orders 0x1e / 0x1f) for one building: an own building with a
    // barrel (template barrelArt != -1, not dying) goes to state 4 / 5 against the target.
    void orderBuildingAttackUnit(int side, int buildingIndex, int targetSide, int targetIndex);
    void orderBuildingAttackBuilding(int side, int buildingIndex, int targetSide, int targetIndex);
    // The order part of the battlefield click handler 0x4433a0 (mode 0, left button released; the
    // selection / deploy / dock / repair-pad cases stay in the frontend). Targets as the original:
    //   unit under the cursor (cloaked enemies count as none): own/allied without Ctrl -> Friendly;
    //     units selected -> GenerateUnitAttackUnitOrder 0x4596f0, except Alt (`forceMove`) on
    //     non-own infantry with a crusher selected and a drivable cell -> GenerateUnitMoveOrder to
    //     the cell (crushers run the infantry over, nobody attacks); no units but a selected barrel
    //     building that can reach the mouse tile (CanBuildingAttackTile 0x497140) ->
    //     GenerateBuildingAttackUnitOrder 0x459d20;
    //   building: own/allied without Ctrl -> Friendly; a selected barrel building (checked first)
    //     in range (IsBuildingInRange_0 0x497530) -> GenerateBuildingAttackBuildingOrder 0x459de0;
    //     units -> GenerateUnitAttackBuildingOrder 0x459800 (own building: only with an armed unit);
    //   nothing: Ctrl (`forceAttack`) -> GenerateUnitAttackTileOrder 0x4598d0 (units only - the
    //     original has no building attack-tile order); else GenerateUnitMoveOrder.
    // Uses the selected units of `side` in list order (max 64) with the original per-order
    // filters; flickers the target and records the unit response (PlayUnitResponse 0x49f240) for
    // unit orders. None = nothing ordered (the original then selects the object, if any).
    SmartOrder smartOrder(int side, int pixelX, int pixelY, bool forceAttack = false, bool forceMove = false);
    // GetSelectedBuildingWithBarrel 0x4a5860: the first selected own building with a barrel.
    const Building* selectedBarrelBuilding(int side) const;
    std::size_t pendingOrders() const { return orders_.size(); }  // queued for the next tick

    // ===================================================================================
    // Cheats (cheats.cpp, not original). Queued like every other order and executed at the
    // start of the next tick, so they are deterministic and survive save/load. Executing any
    // of them sets cheatsUsed(). They consume RNG only where a reused original mechanism does
    // (concrete tiles of a spawned building, the units' own AI ...).
    // ===================================================================================
    bool orderCheatSetCredits(int side, std::int32_t credits);   // credits() becomes exactly this
    bool orderCheatAddCredits(int side, std::int32_t delta);     // immediately (no drip)
    bool orderCheatTechLevel(int side, int level);               // 1..9 (9: construction yards too)
    bool orderCheatBuildingOverride(int side, int type, BuildOverride mode);
    bool orderCheatUnitOverride(int side, int type, BuildOverride mode);
    bool orderCheatResetOverrides(int side);
    // Free delivery of `count` units of `type`: by starport frigate when the side has a primary
    // starport (no stock/price/6-unit limits, several trips when needed), otherwise by carryall
    // to a free tile near the base. Credits are not touched.
    bool orderCheatDeliver(int side, int type, int count);
    bool orderCheatRevealMap();                                   // RevealMap for the local player
    bool orderCheatInstantBuild(int side, bool on);
    // Spawn objects for ANY side through ModelAddUnit / ModelAddBuilding. Units: each on the
    // nearest free tile around (tileX, tileY). Buildings: footprint top-left at (tileX, tileY);
    // must be on the map and free (canCheatSpawnBuilding), adjacency/terrain rules ignored.
    bool orderCheatSpawnUnit(int side, int type, int tileX, int tileY, int count = 1);
    bool orderCheatSpawnBuilding(int side, int type, int tileX, int tileY, bool concrete = false);
    bool canCheatSpawnBuilding(int type, int tileX, int tileY, std::array<bool, 16>* tileOk = nullptr) const;
    BuildOverride buildingOverride(int side, int type) const;
    BuildOverride unitOverride(int side, int type) const;
    bool instantBuild(int side) const { return side >= 0 && side < kMaxSides && cheats_.instantBuild[side] != 0; }
    bool cheatsUsed() const { return cheats_.used; }
    int cheatDeliveriesPending(int side) const;  // units still in the backlog
    // Reinforcement by carryall (the .MIS Reinforcement event mechanism) after `delayTicks`
    // game ticks (0 = at once): `units` (unit types) dropped at tile (x, y) - x < 0: a free tile
    // near the side's base when it is due - by carryalls entering from `edge` (0 N, 1 E, 2 S,
    // 3 W; -1 the nearest edge). 25 units per carryall, up to the side's 10 delivery slots.
    bool orderCheatReinforce(int side, const std::vector<std::uint8_t>& units, std::uint32_t delayTicks, int x = -1,
                             int y = -1, int edge = -1);
    bool orderCheatCancelReinforcement(int id);
    const std::vector<ScheduledReinforcement>& cheatReinforcements() const { return cheats_.reinforcements; }

    // ===================================================================================
    // Factory rally points (not original; rally.cpp). Orders like the others; the points are
    // saved. rallyPointsEnabled (the "Factory rally points" setting, saved with the game) off:
    // points are kept but not used.
    // ===================================================================================
    bool rallyPointsEnabled = true;
    bool canHaveRallyPoint(int side, int buildingIndex) const;  // own factory / starport
    bool orderSetRallyPoint(int side, int buildingIndex, int tileX, int tileY);
    bool orderClearRallyPoint(int side, int buildingIndex);
    bool rallyPoint(int side, int buildingIndex, int& tileX, int& tileY) const;

    // Object lookup with the original tile semantics (pixel inputs for units).
    const Unit* unitAt(int pixelX, int pixelY, int& sideOut, int& indexOut) const;  // GetUnitOnTile 0x4a5270
    const Building* buildingAt(int tileX, int tileY, int& sideOut, int& indexOut) const;  // GetBuildingOnTile_0 0x4a5640
    // Read-only queries for the UI (mouse cursor choice, render/cursor.cpp).
    const Unit* unitAt(int pixelX, int pixelY, int& sideOut, int& indexOut, bool skipCloaked) const {
        return getUnitOnTile(pixelX, pixelY, sideOut, indexOut, skipCloaked);
    }
    bool buildingCanAttackTile(const Building& b, int x, int y) const { return canBuildingAttackTile(b, x, y, false); }
    bool buildingInRangeOf(const Building& b, const Building& target) const {  // IsBuildingInRange_0 0x497530
        int ox = 0, oy = 0;
        return isBuildingInRange(b, target, ox, oy);
    }

    // Per-tick outputs for the frontend.
    const std::vector<SoundEvent>& soundEvents() const { return sounds_; }
    // Bullets created during the last kShotEventTicks ticks (ShotEvent; render only).
    const std::vector<ShotEvent>& shotEvents() const { return shots_; }
    // _ScreenShakes: set to 10 by DestroyBuilding, counted down by the model every tick
    // (FUN_00446690, which draws a random offset: see screenShakeOffset()).
    int screenShakes() const { return screenShakes_; }
    void clearScreenShakes() { screenShakes_ = 0; }  // not original: changes the RNG sequence
    // PlayUnitResponse(kind): unit type whose voice would play (-1 none) and 0 select / 1 order.
    int unitResponseType() const { return unitResponseType_; }
    int unitResponseKind() const { return unitResponseKind_; }
    // The original decrements Unit/Building::flicker once per drawn frame; call once per frame.
    void decayFlicker();
    int difficulty = 1;  // gDifficultyLevel (0 easy, 1 normal, 2 hard): cost % for survivors

    // Model functions (also used by tests). Return the object index or -1.
    int addUnit(int side, int type, int x, int y, int targetX, int targetY, int pxOffX = 0, int pxOffY = 0);
    int addBuilding(int side, int type, int x, int y, bool placeConcrete, bool noFreeHarvester = false,
                    bool noConyardAnim = false);
    void addConcrete(int side, int type, int x, int y, std::uint32_t mask4x4);

    // Movement queries (movement.cpp).
    bool canUnitUseSquare(int x, int y, const Unit& u, int side, int mode) const;  // 0x45d990
    std::int32_t getSpeed(const Unit& u) const;                                    // 0x49f3b0
    std::int32_t stepSpeed(const Unit& u) const;  // MoveUnit's damage/prone speed rule
    bool moveUnit(Unit& u);                                                        // 0x494260
    bool unitAdjustState(Unit& u, UnitState s);                                    // 0x49deb0

    const formats::UnitTemplate& unitTemplate(const Unit& u) const { return rules_.templates->bin.units[u.type]; }
    const formats::BuildingTemplate& buildingTemplate(const Building& b) const {
        return rules_.templates->bin.buildings[b.type];
    }

    // Combat model functions (combat.cpp / projectiles.cpp), public for tests and tools.
    // ModelAddBullet 0x456a30: returns the bullet index or -1.
    int addBullet(int side, int weapon, int delay, int firer, int srcX, int srcY, int dstX, int dstY,
                  int homingIndex = kNoIndex, int homingSide = -1);
    // ModelAddExplosion 0x456df0: returns the explosion index or -1.
    int addExplosion(int side, int type, int x, int y, std::int32_t z, std::uint32_t flags = 0, int delay = 0,
                     std::int32_t driftX = 0, std::int32_t driftY = 0);
    // DamageTiles 0x49d2e0 (pixel position, height 16.16, weapon, attacker side/index).
    void damageTiles(int px, int py, std::int32_t z, int weapon, int side, int firer, bool deviate);
    void destroyUnit(Unit& u);                           // DestroyUnit 0x49cde0
    void destroyBuilding(Building& b, bool noSurvivors); // DestroyBuilding 0x49ca10

    // ===================================================================================
    // Economy / production API for the UI (production.cpp). Queries are immediate; every
    // mutation is an order queued like orderMove and executed at the start of the next tick
    // (ProcessOrder). The order functions apply the same filters the original UI applies
    // before generating the order (sidebar icon present, placement valid, ...) and return
    // false when the order was not queued.
    // ===================================================================================
    SideEconomy economy(int side) const;
    std::int32_t credits(int side) const;     // CashReal + SpiceReal
    bool radarActive(int side) const;         // an outpost and power >= 100 % (sidebar/minimap)
    // Sidebar contents right now (CSide icon lists, in sidebar order).
    std::vector<BuildOption> buildableBuildings(int side) const;
    std::vector<BuildOption> buildableUnits(int side) const;
    std::vector<BuildOption> upgradableBuildings(int side) const;  // CanSideUpgradeBuildingGroup
    // Queue state. Units have one slot per factory kind (10 slots); unitQueueFor finds the slot
    // holding `type` (inactive state when none).
    QueueState buildingQueue(int side) const;
    std::vector<QueueState> unitQueues(int side) const;  // active slots only
    QueueState unitQueueFor(int side, int unitType) const;
    QueueState upgradeQueue(int side) const;
    // Production orders. Pick starts production or resumes it when on hold; cancel puts a
    // running item on hold first and cancels it (refunding what was paid) the second time.
    bool orderBuildBuilding(int side, int type);           // 0x24
    bool orderCancelBuilding(int side, int type);          // 0x25
    bool orderBuildUnit(int side, int type);               // 0x27
    bool orderCancelUnit(int side, int type);              // 0x28
    // Extended unit queue (NOT ORIGINAL, re/notes/economy.md §10; the AI never uses it).
    // orderAutoAdd: +n units of `type` (n >= 1; capped at kAutoMaxCount; no-op on an infinite type).
    // orderAutoInfinite: make `type` infinite, or, when it already is, cancel it entirely (refund
    // of the unit in production as the original cancel). orderAutoRemove: one fewer (infinite:
    // finish the current unit, then stop); reaching 0 cancels the unit in production (refund).
    // Special weapons (palace units) are rejected (they are auto-picked by the original rules).
    bool orderAutoAdd(int side, int type, int n = 1);
    bool orderAutoInfinite(int side, int type);
    bool orderAutoRemove(int side, int type);
    AutoQueueState autoQueueFor(int side, int unitType) const;
    bool orderUpgrade(int side, int buildingGroup);        // 0x2d
    bool orderCancelUpgrade(int side, int buildingType);   // 0x2e (template being upgraded)
    // Place the finished building (top-left of its 4x4 footprint at tileX/tileY).
    bool orderPlaceBuilding(int side, int type, int tileX, int tileY);  // 0x26
    // Placement cursor rule (Display__DrawPlacementMarkers 0x4285c0 / concrete 0x4288d0).
    // `tileOk` (optional, 16 entries, row-major 4x4) gets the per-tile marker state.
    bool canPlaceBuilding(int side, int type, int tileX, int tileY, std::array<bool, 16>* tileOk = nullptr) const;
    bool orderSetPrimary(int side, int buildingIndex);     // 0x20
    bool orderRepairBuilding(int side, int buildingIndex); // 0x22 toggles repairing
    bool orderSellBuilding(int side, int buildingIndex);   // 0x23
    bool orderDeploy(int side, int unitIndex);             // 0x2f (MCV deploy, also other deployers)
    bool canDeployMcv(int side, int unitIndex) const;      // CheckIfMCVCanBeDeployedOn 0x443250
    // Whether a deploy order does something for this unit (the click handler 0x4433a0 / deploy cursor rule):
    // MCV on a valid site, Devastator (self-destruct), thumper on sand, saboteur with +0x29 >= 0xa0.
    bool canDeploy(int side, int unitIndex) const;
    bool orderDockAtRefinery(int side, int unitIndex, int refineryIndex);  // 0x17 (harvesters)
    // Send a vehicle to a repair pad (order 0x21; kNoIndex = the nearest pad when it gets there).
    // Infantry and dead units are rejected (GenerateRepairSelectedUnitsOrder filter).
    bool orderRepairUnit(int side, int unitIndex, int repairPadIndex = kNoIndex);
    // Launch a ready palace weapon (order 0x30): behaviour::kDeathHand at (tileX, tileY),
    // kSaboteur / kFremen (units at the palace), kOrnithopter: the air strike on (tileX, tileY)
    // (Ornithopter_LaunchStrike 0x49e770). False when no finished item of that behaviour is queued.
    bool orderSpecialWeapon(int side, int behaviour, int tileX = 0, int tileY = 0);
    // Starport (re/notes/starport.md). The 8 slots of the starport page with price/stock/picked
    // counts; `starportOpen`: a primary starport exists (the STARPORT button); `starportBusy`:
    // an order is on its way (no picks until the frigate has unloaded, CSide +0x262a5).
    std::vector<StarportOption> starportUnits(int side) const;
    bool starportOpen(int side) const;
    bool starportBusy(int side) const;
    int starportPickedTotal(int side) const;
    std::int32_t starportOrderValue(int side) const { return sides_[side & 7].starportSpent; }
    bool orderStarportPick(int side, int type);    // GenerateStarportPickOrder 0x45a200 (0x29): pays now
    bool orderStarportUnpick(int side, int type);  // GenerateStarportUnpickOrder 0x45a260 (0x2a): refunds
    bool orderStarportPurchase(int side);          // GenerateStarportPurchaseOrder 0x45a2b0 (0x2b): send the frigate
    bool orderStarportCancel(int side);            // GenerateStarportCancelOrder 0x45a310 (0x2c): refund all picks
    // Sidebar Guard / Retreat buttons (click handler 0x4433a0): orders for the selected units of
    // `side` (at most 64, in list order). False when nothing was queued.
    bool orderGuardSelected(int side);    // GenerateUnitGuardOrder 0x459980 (0x18): alive, armed
    bool orderRetreatSelected(int side);  // GenerateUnitRetreatOrder 0x459ab0 (0x1a -> ProcessOrder case 7)
    // CSide__FindBestBasePosition 0x46e3f0: centre of the buildings within 10 tiles of the
    // construction yard (primary) / first building. False (and a fallback position) when the
    // side has no building.
    bool findBestBasePosition(int side, int& tileX, int& tileY) const;

    // Economy model functions (public for tests and tools).
    std::uint32_t unitCost(int type, int side) const;                       // w__GetUnitCost 0x442bb0
    std::uint32_t buildingCost(int type, int upgradeLevel, int side) const; // GetBuildingCost 0x442be0
    std::uint32_t unitBuildSpeedPercent(int type, int side) const;          // 0x442a00
    std::uint32_t buildingBuildSpeedPercent(int side) const;                // 0x442b10
    bool spend(int side, std::int32_t amount);  // FUN_0046c720 (checks cash+spice incl. pending)
    void addCash(int side, std::int32_t amount) { sides_[side].cashDrip += amount; }  // CSide__AddCash 0x46c7b0
    void addSpice(int side, std::int32_t amount) { sides_[side].spiceDrip += amount; } // FUN_0046c790
    void updateIcons(int side);  // CSide__UpdateBuildingAndUnitIconsAndBaseBoundaries 0x46be50
    const formats::Vars& vars() const { return rules_.vars ? *rules_.vars : defaultVars_; }
    const RulesExt& rulesExt() const {
        static const RulesExt kDefaults{};
        return rules_.ext ? *rules_.ext : kDefaults;
    }

    // ===================================================================================
    // Shroud (shroud.cpp). One shroud, for the local player (mySide()). Cell::shroud low nibble:
    // 0 clear, 1 fully shrouded, 2..15 edge piece; high nibble: 2x2 checker variant
    // ((y&1)*2 + (x&1)). The renderer draws DATA.R16 frame kShroudFirstFrame + Cell::shroud
    // (re/notes/shroud.md).
    // ===================================================================================
    static constexpr int kShroudFirstFrame = 38;
    bool shrouded(int x, int y) const { return (cell(x, y).shroud & 0xF) == 1; }
    void revealMap();                         // RevealMap 0x44c480
    void revealCircle(int x, int y, int radius);  // RevealCircle 0x44e3b0 (radius 0..7)
    // ===================================================================================
    // Mission events / outcome (mission.cpp).
    // ===================================================================================
    GameEnd gameEndState() const;             // gWin / gLose
    bool gameOver() const { return gameOver_; }  // _GameOver (skirmish rules)
    const std::vector<MissionMessage>& messages() const { return messages_; }  // queued, oldest first
    void clearMessages() { messages_.clear(); }
    std::int32_t missionTimer() const { return timerValue_; }  // _gTimerValue ticks, -1 = hidden
    // Community extended event system (mission_ext.cpp): active for extended .MIS files.
    bool extendedEvents() const { return ext_.active; }
    const ExtMission& extMission() const { return ext_; }
    std::int32_t extVariable(int i) const { return ext_.vars[static_cast<std::size_t>(i & 255)].value; }
    ExtUiRequests& extUiRequests() { return extUi_; }
    // "Play as any side" (not original; docs/play-as.md): true when the human plays a side other
    // than the one the mission was written for. scriptSide() = the side the mission's events treat
    // as "the player" (the Spice Harvested condition, extended "my side" filters); = mySide()
    // in a normal game.
    bool playAs() const { return scriptSide_ >= 0; }
    int scriptSide() const { return scriptSide_ >= 0 ? scriptSide_ : mySide_; }
    bool buildingsExist(int side) const { return buildingsExist_[side & 7] != 0; }  // gBuildingsExist
    bool unitsExist(int side) const { return unitsExist_[side & 7] != 0; }          // gUnitsExist
    // Skirmish/multiplayer rules (GameType 1 / network): default win/lose instead of .MIS events.
    bool skirmish = false;
    // Multiplayer start locations found in the map (special 0x17, at most 8, in row-major order).
    const std::vector<std::array<int, 2>>& spawnLocations() const { return spawnLocations_; }
    // Computer players (ai.cpp).
    const AiState& ai(int side) const { return ai_[side & 7]; }
    AiState& ai(int side) { return ai_[side & 7]; }
    // Screen shake (FUN_00446690, every tick): pixel offset -2..2 while shaking.
    int screenShakeOffset() const { return screenShakeOffset_; }
    // Crates and spice blooms (re/notes/crates.md). Only `active` records are on the map.
    const std::array<Crate, kMaxCrates>& crates() const { return crates_; }
    // Pixel particles (sandworm sand spray); `particleCount()` = active records (0x6d1920).
    const std::vector<Particle>& particles() const { return particles_; }
    int particleCount() const { return particleCount_; }
    // Skirmish player colours (NetPlayersColor, 6 slots; colour index 0..7, never 4 or 7 for
    // computer players). -1 outside skirmish.
    int playerColor(int side) const { return side >= 0 && side < 8 ? playerColors_[side] : -1; }
    // Public for tests/tools: PlaceCrate 0x44e8e0 and SpiceMound 0x44ef10.
    void placeCrate(int x, int y, std::uint32_t timer, int type, int image, int respawns);
    void spiceMound(int x, int y, int size);

private:
    friend struct WorldTestAccess;
    friend struct SaveAccess;  // savegame.cpp: every mutable member must be listed there

    struct Order {
        // Original order codes (ProcessOrder 0x457810, switch on code - 0x13) in comments.
        enum Kind {
            Move,             // 0x13
            Stop,             // 0x1c
            AttackUnit,       // 0x14
            AttackBuilding,   // 0x15
            AttackTile,       // 0x16
            DockRefinery,     // 0x17 GenerateDockWithRefineryOrder 0x459540
            SetPrimary,       // 0x20 GenerateBuildingSetPrimaryOrder 0x459ea0
            RepairBuilding,   // 0x22 GenerateBuildingRepairOrder 0x45a150
            SellBuilding,     // 0x23 GenerateBuildingSellOrder 0x45a1a0
            BuildingPick,     // 0x24 GenerateBuildBuildingPickOrder 0x459f30
            BuildingCancel,   // 0x25 GenerateBuildBuildingCancelOrder 0x459f80
            PlaceBuilding,    // 0x26 GenerateBuildPlaceBuildingOrder 0x459fd0
            UnitPick,         // 0x27 GenerateBuildUnitPickOrder 0x45a0b0
            UnitCancel,       // 0x28 GenerateBuildUnitCancelOrder 0x45a100
            UpgradePick,      // 0x2d GenerateUpgradePickOrder 0x45a350
            UpgradeCancel,    // 0x2e GenerateUpgradeCancelOrder 0x45a3a0
            Deploy,           // 0x2f GenerateUnitDeployOrder 0x45a3f0
            Guard,            // 0x18 (ProcessOrder case 5; AI and the sidebar Guard button)
            Retreat,          // 0x1a GenerateUnitRetreatOrder 0x459ab0 (ProcessOrder case 7)
            RepairUnit,       // 0x21 GenerateRepairSelectedUnitsOrder 0x4595e0 (ProcessOrder case 0xe)
            SpecialWeapon,    // 0x30 GenerateSpecialWeaponOrder 0x45a440 (ProcessOrder case 0x1d)
            StarportPick,     // 0x29 (ProcessOrder case 0x16, ModelStarportPick 0x456fd0)
            StarportUnpick,   // 0x2a (case 0x17, ModelStarportUnpick 0x4570d0)
            StarportPurchase, // 0x2b (case 0x18, ModelStarportPurchase 0x457140)
            StarportCancel,   // 0x2c (case 0x19, ModelStarportCancel 0x4572f0)
            Scatter,          // 0x19 GenerateUnitScatterOrder 0x459a20 (ExecScatterOrder 0x457580)
            // Extended unit queue (not original; production.cpp). x = type, y = count / mode.
            AutoAdd,          // y = units to add
            AutoInfinite,
            AutoRemove,
            // Cheats (not original; cheats.cpp). x/y/unitIndex/targetIndex carry the arguments.
            CheatSetCredits,  // x = credits
            CheatAddCredits,  // x = delta
            CheatTechLevel,   // x = level
            CheatBuildingOverride,  // x = type, y = BuildOverride
            CheatUnitOverride,      // x = type, y = BuildOverride
            CheatResetOverrides,
            CheatDeliver,     // x = type, y = count
            CheatRevealMap,
            CheatInstantBuild,  // x = on
            CheatSpawnUnit,     // x/y tile, targetIndex = type, unitIndex = count
            CheatSpawnBuilding, // x/y tile, targetIndex = type, unitIndex = concrete
            CheatReinforce,     // x/y tile (-1 base), unitIndex = edge, targetIndex = delay, payload = types
            CheatCancelReinforcement,  // x = id
            // Rally points (not original; rally.cpp; not cheats). unitIndex = building, x/y tile (x < 0: clear).
            SetRallyPoint,
            // Appended after the rally order so saved queues keep their values.
            BuildingAttackUnit,      // 0x1e GenerateBuildingAttackUnitOrder 0x459d20 (ProcessOrder case 0xb)
            BuildingAttackBuilding,  // 0x1f GenerateBuildingAttackBuildingOrder 0x459de0 (case 0xc)
        } kind;
        int side, unitIndex, x, y;
        int targetSide = 0, targetIndex = kNoIndex;
        std::vector<std::uint8_t> payload{};  // variable-size argument (CheatReinforce: unit types)
    };

    void initSides(const formats::MisFile* mission);
    void initEconomy(bool mission);  // Game_Init: icons, InitCash, queues, starport lists
    void spawnMapObjects(const formats::MapFile& map, const formats::TileAtr& tileAtr,
                         const formats::TileData& tileData, const SkirmishSetup* skirmish = nullptr);
    void updateSpiceInRegion(int x0, int y0, int x1, int y1);
    void markBuildingTiles(int side, int type, int x, int y);
    void buildingConcreteCount(int type, int x, int y, int& total, int& onConcrete);
    std::uint32_t* linearCellFlags(int x, int y);  // original's unchecked y*width+x indexing

    void processOrder(const Order& o);
    void processBuildingAttackOrder(const Order& o);  // ProcessOrder cases 0xb / 0xc
    void modelUpdate();
    bool updateUnit(Unit& u);
    void updateIdle(Unit& u);
    void updateGuarding(Unit& u);
    void updateMovingAway(Unit& u);
    bool updateDead(Unit& u);
    void updateLeavingBuilding(Unit& u);
    void updateMovingToRepairPad(Unit& u);                 // UpdateUnit case 0x16 (state 24)
    void updateRepairing(Unit& u);                         // case 0x17 (state 25)
    void updateLeavingRepairPad(Unit& u);                  // case 0x18 (state 26)
    void updateWormWandering(Unit& u);                     // UpdateUnit case 0x19 (state 27)
    void updateWormEating(Unit& u);                        // case 0x1a (state 28)
    void updateWormSleeping(Unit& u);                      // case 0x1b (state 29)
    void updateSelfdestruct(Unit& u);                      // case 0x1d (state 31)
    void updateThumper(Unit& u);                           // case 0x1e (state 32)
    bool deathHandFlight(Unit& u);                         // FUN_00494cd0 (false = exploded)
    void launchDeathHand(int side, int tileX, int tileY);  // FUN_0049e860
    void specialWeapon(int side, int beh, int tileX, int tileY);  // ProcessOrder case 0x1d
    bool findThumper(int fromX, int fromY, int& ox, int& oy);              // FUN_0044d500
    bool findVehicleOnSand(int fromX, int fromY, int& ox, int& oy) const;  // FUN_0044d630
    void wormDust(int px, int py);                         // FUN_0045d730
    void freeUnit(Side& side, Unit& u);
    void freeBuilding(Side& side, Building& b);
    // PlaySoundAt 0x46f540 (positional).
    void playSound(int id, int tileX, int tileY) { sounds_.push_back({id, nullptr, 0, tileX, tileY}); }
    // ... guarded like the worm / thumper effects: only while pixel (px, py) is inside the
    // battlefield view and the sample is not already playing.
    void playSoundIfVisible(int id, int px, int py) {
        SoundEvent e{id, nullptr, 0, px >> 5, py >> 5};
        e.onScreen = e.unique = true;
        e.spotX = px;
        e.spotY = py;
        sounds_.push_back(e);
    }
    void playNamed(const char* name, int variant, int tileX, int tileY) {
        sounds_.push_back({-1, name, variant, tileX, tileY});
    }
    // QueueAudioToPlay 0x46f820 of a named sample (S_POWERUP, S_CHATMSG, ...): always the
    // local player's (callers check the side).
    void speech(const char* name, bool queue, int throttle = 0, int priority = 0) {
        SoundEvent e{-1, name};
        e.queue = queue;
        e.throttle = throttle;
        e.priority = priority;
        sounds_.push_back(e);
    }

    // combat.cpp
    const formats::WeaponTemplate& weapon(int w) const { return rules_.templates->bin.weapons[w]; }
    int warheadVersus(int weapon, int armour) const;
    std::uint32_t warheadRadius(int weapon) const;
    int closestBuildingTileManhattan(const Building& b, int x, int y, int& ox, int& oy) const;  // 0x49d170
    void closestBuildingTile(int x, int y, const Building& b, int& ox, int& oy) const;          // 0x497390
    bool canUnitAttackTile(const Unit& u, int x, int y, bool air) const;                        // 0x497270
    bool canBuildingAttackTile(const Building& b, int x, int y, bool air) const;                // 0x497140
    bool isBuildingInRange(const Building& b, const Building& target, int& ox, int& oy) const;  // 0x497490
    bool findNearestEnemy(int px, int py, int side, std::uint32_t fixedRange, int& ox, int& oy, int& oSide,
                          int& oIndex, int primary, int secondary, bool includeCloaked, bool air,
                          bool worms) const;                                                    // 0x4a4900
    bool findNearestEnemyBuilding(int px, int py, int side, int& ox, int& oy, int& oSide, int& oIndex,
                                  int primary, int secondary, bool turretsOnly) const;         // 0x4a4fd0
    int newBestBullet(int primary, int secondary, std::uint32_t dist2, int targetSide, int targetIndex,
                      int targetKind) const;                                                    // 0x49e9d0
    bool unitDecloak(Unit& u);                                                                  // 0x49f5c0
    bool unitCloak(Unit& u);                                                                    // 0x49f580
    bool enemyNearStealth(int x, int y, int side, int r) const;                                 // 0x49f750
    void updateStealth(Unit& u);                                                                // 0x49f610
    void fireAt(Unit& u, int tx, int ty, int targetSide, int targetIndex, int targetKind);     // 0x495b90
    bool attackUnit(Unit& u);                                                                   // 0x496b00
    bool attackBuilding(Unit& u);                                                               // 0x496d10
    bool attackTile(Unit& u);                                                                   // 0x496ff0
    bool unitTileOccupiedByBuilding(const Unit& u) const;                                       // 0x49d280
    void breakAlliance(int victimSide, int attackerSide);                                       // 0x49f870
    Unit* changeUnitOwner(int fromSide, int toSide, int index, bool keepState);                 // 0x46d2e0
    bool captureBuilding(int fromSide, int toSide, int index);                                  // 0x46d6d0
    void updateInfiltrating(Unit& u);                                                           // UpdateUnit case 0x15
    bool deviatedOwnerAlive(const Unit& u) const;                                               // FUN_0049fb40
    bool getRandomAdjacentTileExt(int& x, int& y);  // GetRandomAdjacentTile 0x4979a0
    void releaseSurvivors(Building& b);                                                         // 0x4977a0
    ArtSize buildingImageSize(const Building& b) const;
    ArtSize buildingFrameSize(int art, int frame) const;

    // buildings.cpp
    bool updateBuilding(Building& b);                                                           // 0x49b7f0
    void rotateBuildingTurret(Building& b, int want);                                           // 0x493fd0
    void buildingFireAt(Building& b, int tx, int ty, int targetSide, int targetIndex, int kind); // 0x496030
    bool buildingAutoTarget(Building& b);                                                       // 0x496790
    bool buildingAttackUnit(Building& b);                                                       // 0x4975d0
    bool buildingAttackBuilding(Building& b);                                                   // 0x4976b0
    void removeBuildingFromMap(Building& b);                                                    // 0x44dee0
    void placeBuildingSkirt(int x, int y, int type);                                            // 0x44f1b0
    void removeBuildingSkirt(int x, int y, int type);                                           // 0x44f2f0
    void reassignPrimary(Side& s, const Building& b);                                           // 0x46d9a0

    // projectiles.cpp
    std::size_t explosionCount() const;                       // explosion/animation types available
    const formats::ExplosionTemplate& explosionTemplate(int type) const;
    int animationFrames(int type) const;                      // gAnimationArtFrames 0x6d5fd0
    std::uint32_t animationFlags(int type) const;             // animationArtFlags 0x6e5750
    int projectileDirections(int weapon) const;               // gProjectileArtDirs 0x795340 [weapon]
    bool updateBullet(Bullet& b);                                                               // 0x49be60
    bool updateExplosion(Explosion& e);                                                         // 0x49dd80
    bool moveUnitSimple(Unit& u, bool releaseFrom, bool markTo);                                // 0x495560

    // movement.cpp
    void getNextSquare(Unit& u);
    void storeRoute(Unit& u, const PathPoint* p, int len);
    bool pickupCrate(Unit& u);  // UnitPickupCrate 0x49ebf0
    int freeCrateIndex() const;                     // GetFreeCrateIndex 0x44e790 (-1 = none)
    int crateTypeAt(int x, int y, int& index) const; // FUN_0044e970 (0xff = none)
    int getCrateFromMap(int x, int y);              // GetCrateFromMap 0x44e9d0 (0xb = none; consumes!)
    void recycleCrate(int index);                   // RecycleCrate 0x44f110
    void updateCrates();                            // FUN_0044ea70
    void spawnRandomCrate();                        // FUN_0044e7c0 / FUN_0044e800
    void makeUnitsStealthInRange(int x, int y, int side);  // MakeUnitsStealthInRange
    void assignSkirmishColours(int playerColor);    // Game__MainLoopFrame 0x44a54e colour loop
    bool rotateBody(Unit& u, int want);  // true while still turning
    void rotateTurret(Unit& u, int want);
    bool rotateFlyerBody(Unit& u, int want);
    void setInfantryAnim(Unit& u, int anim);
    void advanceInfantryAnim(Unit& u);
    void idleTurretWander(Unit& u);
    bool findFreeAdjacentSquare(int x, int y, const Unit& u, int side, int& outX, int& outY, int mode) const;
    void askUnitsOnTileToMoveAway(const Unit& caller, int x, int y);
    Unit* getUnitOnTile(int px, int py, int& sideOut, int& indexOut, bool skipCloaked);
    const Unit* getUnitOnTile(int px, int py, int& sideOut, int& indexOut, bool skipCloaked) const;
    Unit* getNextUnitOnTile(int x, int y, int side, int& index);
    bool unitOccupiesTile(const Unit& u, int x, int y) const;
    bool buildingOccupiesTile(const Building& b, int x, int y) const;
    bool unitIsMoving(const Unit& u) const;
    bool unitIsHiddenStealth(const Unit& u) const;
    bool unitCanTakeOrders(const Unit* u, bool fromPlayer) const;
    bool unitAutoTarget(Unit& u);
    void crushCheck(Unit& u);
    void crushInfantry(Unit& u, int killerSide, int killerIndex);

    // economy.cpp
    void updateCashDrip(Side& s);                                                 // 0x46c7d0
    void updateStorage();                                                         // 0x44abe0
    void updatePower();                                                           // 0x44aae0 / 0x46ca00
    void updateBuildingRepair();                                                  // 0x44a9f0
    int getRefineryIndex(int side);                                               // 0x4a5e80
    Building* nearestBuildingWithBehaviour(int x, int y, int side, int beh, int& ox, int& oy);  // 0x4a5f80
    bool betterSpiceNearby(int& x, int& y) const;                                 // 0x44d970
    bool findNearestSpice(const Unit& u, int& x, int& y);                         // 0x44d760
    bool findDriveOnTileNear(int side, int& x, int& y) const;                     // 0x49e290
    bool nearestFreeTile(int& x, int& y, int radius) const;                       // GetNearestFreeTileForUnit 0x4a60e0
    void refineryDock(const Building& b, int& x, int& y) const;
    void updateMovingToHarvest(Unit& u);                                          // UpdateUnit case 9
    void updateHarvesting(Unit& u);                                               // case 10
    void updateMovingToRefinery(Unit& u);                                         // case 11
    void updateEnteringRefinery(Unit& u);                                         // case 12
    void updateUnloadingSpice(Unit& u);                                           // case 13
    void updateLeavingRefinery(Unit& u);                                          // case 14
    void updateAwaitingAirlift(Unit& u);                                          // case 16
    void harvesterAirliftCheck(Unit& u);                                          // LAB_004999dd
    int pickupQueuePos(const Side& s, const Unit& u) const;                       // CSide__GetQueuePos 0x46cca0
    bool addToPickupQueue(Side& s, const Unit& u, int pos, int priority);         // CSide__AddToQueue 0x46cd00
    int pickupEntryFor(const Side& s, const Unit& carryall) const;                // FUN_0046ce00
    void updatePickupQueue(Side& s);                                              // FUN_0046d180
    void updateDeliveries(Side& s);                                               // FUN_0046def0
    bool carryallDrop(Unit& u);                                                   // FUN_0049e3e0
    int carryallEntryPoint(int& x, int& y, int extra) const;                      // Carryall_GetEntryPoint 0x49e6b0
    int edgeEntryPoint(int& x, int& y, int edge) const;                           // not original (cheat reinforcements)
    bool flyUnit(Unit& u, bool hover);                                            // FlyUnit 0x494fd0
    void updateCarryallIdle(Unit& u);                                             // UpdateUnit case 0 (beh 2/8)
    void updateFlyingDown(Unit& u);                                               // case 0x13
    void updateFlyingUp(Unit& u);                                                 // case 0x14
    void dragCargo(Unit& carryall);                                               // end of UpdateUnit
    void markFlyer(int x, int y, bool on);                                        // 0x49f370 / 0x49f330
    void initStarport(Side& s);                                                   // FUN_0046e7a0
    void updateStarport(Side& s);                                                 // FUN_0046ddc0
    void queueDelivery(Side& s, int x, int y, int unitType, int type, int deployAction);
    void starportPick(int side, int type);                                        // ModelStarportPick 0x456fd0
    void starportUnpick(int side, int type);                                      // ModelStarportUnpick 0x4570d0
    void starportPurchase(int side);                                              // ModelStarportPurchase 0x457140
    void starportCancel(int side);                                                // ModelStarportCancel 0x4572f0
    bool starportRefund(Side& s);                                  // CSide__ReturnMoneyFromStarportOrder 0x46eac0
    void starportLost(Side& s);                                    // CSide__ReassignPrimaryBuilding, no starport left
    void frigateSpeech(Side& s, const Delivery& d);                // FUN_0046def0 FRIGATEINn countdown
    void homeViewPosition(int side, int& px, int& py) const;      // CSide_46CF10 (view top-left, pixels)
    void launchOrnithopters(int side, int homeX, int homeY, int tx, int ty);  // Ornithopter_LaunchStrike 0x49e770

    // cheats.cpp
    void processCheatOrder(const Order& o);
    void updateCheats();                       // hands backlog deliveries to free delivery slots
    // .MIS Reinforcement / Starport Delivery event body (FUN_00454120 cases 0/1): the first
    // free delivery slot of the side gets the units; returns the slot or -1 (all 10 busy).
    int startReinforcement(int side, int x, int y, const std::uint8_t* units, int count, int deployAction, int type);
    void setRallyPoint(int side, int buildingIndex, int x, int y);
    // A unit just added at a factory exit (or dropped at a starport): queue its rally move.
    void rallyUnitLeft(int side, int unitIndex, const Building& factory);
    void updateRally();  // moves units that have left their factory to its rally point
    void applyBuildOverrides(int side);        // end of updateIcons
    bool cheatProductionNoFactory(int side, ProductionSlot& q);  // allowed unit without a factory
    bool cheatDeliveryTarget(int side, int& x, int& y) const;

    // production.cpp
    int sideVersionOfUnit(const Side& s, int group, bool onlyIfNotListed) const;     // 0x46c570
    int sideVersionOfBuilding(const Side& s, int group, bool onlyIfNotListed) const; // 0x46c450
    std::uint32_t difficultyCostPercent(int side) const;                          // 0x4429b0
    std::uint32_t difficultySpeedPercent(int side) const;                         // 0x442960
    bool canUnitBeBuilt(int side, int type, bool specialOk) const;                // w_CanUnitBeBuilt 0x4430d0
    bool canSideUpgradeGroup(int side, int group) const;                          // 0x4431d0
    Building* findUnitExitBuilding(int type, int side, int& sx, int& sy, int& tx, int& ty, bool needFree);  // 0x442c20
    bool footprintFree(int type, int x, int y) const;                             // FUN_00457740
    void buildingPick(int side, int type);                                        // ModelBuildBuildingPick 0x455fc0
    void buildingCancel(int side, int type);                                      // FUN_004560f0
    void placeBuilding(int side, int type, int x, int y);                         // ProcessOrder 0x26
    void unitPick(int side, int type);                                            // ModelBuildUnitPick 0x455510
    void unitCancel(int side, int type);                                          // FUN_00455750
    void autoAdd(int side, int type, int n);                                      // not original
    void autoInfinite(int side, int type);                                        // not original
    void autoRemove(int side, int type);                                          // not original
    void autoCancelSlot(int side, int type);                                      // not original
    void updateAutoQueue(int side);                                               // not original
    void upgradePick(int side, int group);                                        // FUN_00457350
    void upgradeCancel(int side, int type);                                       // FUN_00457490
    void setBuildingAsPrimary(int side, int index);                               // 0x44b1f0
    void sellBuilding(int side, int index);                                       // FUN_00456ee0
    void autoPickSpecialWeapons();                                                // FUN_004466e0
    void updateProduction();                                                      // FUN_004467e0
    bool productionStep(int side, ProductionSlot& q, std::uint32_t step, std::int32_t cost, bool paidMask16);
    void updateDeploying(Unit& u);                                                // UpdateUnit case 0x1c
    // PlayMentatSound 0x46f900 for `side` == the local player (the callers' MySideID checks).
    void mentat(int side, const char* name, bool queue, int throttle = 0, int priority = 0) {
        if (side == mySide_) sounds_.push_back(mentatEvent(name, queue, throttle, priority));
    }
    static SoundEvent mentatEvent(const char* name, bool queue, int throttle = 0, int priority = 0) {
        SoundEvent e{-1, name};
        e.house = true;
        e.queue = queue;
        e.throttle = throttle;
        e.priority = priority;
        return e;
    }
    // Speech of a UI action (between ticks): reported with the next tick's sound events.
    void uiMentat(int side, const char* name, bool queue) {
        if (side == mySide_) uiSounds_.push_back(mentatEvent(name, queue));
    }

    // shroud.cpp
    void updateShroudInRegion(int x0, int y0, int x1, int y1);  // UpdateShroudInRegion 0x44cd10 (x1/y1 exclusive)
    void revealBuildingType(int type, int x, int y);            // FUN_0044e330
    void revealUnitsSlice();                                   // FUN_0044cff0 (1/8 of units per call)
    void revealTilesSeenByBuildingsAndUnits(int side);          // 0x49f4d0
    std::uint8_t circleMask(int r, int row, int col) const;     // CIRCLES.BIN mask entry
    void updateScreenShake();                                  // FUN_00446690

    // mission.cpp
    struct MisEvent {        // TEvent (72 bytes), decoded
        int x = 0, y = 0;    // +0 / +4 (low byte of coord_x[0] / coord_y[0])
        std::uint32_t value = 0;  // +8
        int numConditions = 0, type = 0, amount = 0, side = 0, arg2 = 0, arg3 = 0, arg4 = 0;  // +0xc..+0x12
        std::array<std::uint8_t, 12> conditionIndex{}, conditionNot{};  // +0x13 / +0x21
        std::array<std::uint8_t, 25> data{};  // +0x2f units (amount), message id u32 at data+21
    };
    struct MisCondition {    // TCondition (28 bytes), live state
        std::uint32_t val2 = 0, val1 = 0, value = 0;  // +0 / +4 / +8
        std::uint32_t coordX = 0, coordY = 0;         // +0xc / +0x10 (read as dwords by the original)
        float floatVal = 0;                           // +0x14
        int side = 0, type = 0, arg1 = 0, arg2 = 0;   // +0x18..+0x1b
    };
    void loadMissionEvents(const formats::MisFile& mission);  // Mission__LoadObjectives (events part)
    void checkEvents();                                       // Mission__CheckEvents 0x453b90
    void executeEvent(const MisEvent& e);                     // FUN_00454120
    bool evaluateExists(int side, bool units);                // EvaluateIfBuildingsOrUnitsExistForSide 0x455290
    void evaluateAllExists();                                 // FUN_0044f770 (Game_Init)
    void playerDefeated(int side);                            // Map__PlayerDefeated 0x44c4f0
    void setDeadOrder();                                      // Map__SetDeadOrder 0x44c520
    void updateGameEnd();                                     // main loop win/lose block
    void uncloakRemainingStealthUnit();                       // 0x455480
    void resetEnemyForSide(int side, int target);             // CSide__ResetEnemyForSide
    void returnCredits(int side, std::int32_t amount);        // CSide_return_credits (rule returnCreditsToSpiceStorage)

    // mission_ext.cpp (community extended event system, ref: dune2000-sp-patches event-system/)
    friend struct ExtEventRunner;
    void extLoad(const formats::MisFile& mission);
    void extInitMap(const formats::MapFile& map, const formats::TileAtr& tileAtr);
    void extCheckEvents();                                    // Mod__HandleConditionsAndEvents (campaign part)

    // ai.cpp (computer player)
    friend struct AiController;                    // one method per original CAI function
    void aiInit(const formats::MisFile* mission);  // InitAI FUN_0041af50 + .MIS AI segment copy
    void aiDefaults(int side);                     // InitAI FUN_0041af50 for one side
    // Play as any side (not original; mission.cpp).
    void setupPlayAs(const formats::MisFile& mission);
    bool playAsHostile(int other) const;     // enemy of the human side either way (diplomacy 1)
    void missionOutcome(bool scriptWin);      // Mission Win / Fail events (10/11, extended ET_WIN/ET_LOSE)
    void checkPlayAsEnd();                    // elimination rules, after the mission's events
    void aiActivate(int side);                     // ActivateAI FUN_0041b250
    void aiUpdate(int side);                       // DoAI FUN_0041b610
    void aiDeliveryDone(int side, int delivery);   // UpdateUnit case 0x13: CAI__FindGroupForDelivery + group flags

    int width_ = 0, height_ = 0;
    std::vector<Cell> cells_;
    std::array<Side, kMaxSides> sides_{};
    std::array<std::array<std::uint8_t, kMaxSides>, kMaxSides> diplomacy_{};  // gDiplomacy, 1 enemy
    std::uint32_t gameTicks_ = 0;
    Random rng_;
    Rules rules_;
    int mySide_ = 0;
    std::vector<Order> orders_;
    PathFinder pathFinder_;
    std::vector<SoundEvent> sounds_;
    std::vector<SoundEvent> uiSounds_;
    std::vector<ShotEvent> shots_;  // render-only output (not saved, never read by the model)
    int screenShakes_ = 0;
    int unitResponseType_ = -1, unitResponseKind_ = 0;
    formats::Vars defaultVars_{};
    bool lowMoneyWarned_ = false;  // DAT_00516480
    bool radarOnline_ = false;     // DAT_00516481
    bool lowPowerWarned_ = false, silosWarned_ = false;  // unused (speech repeat guard); kept for savegame

    // Shroud.
    std::int32_t revealCounter_ = 0;  // DAT_0054804c
    int screenShakeOffset_ = 0;       // DAT_005178d4
    // Mission.
    std::vector<MisEvent> events_;
    std::vector<MisCondition> conditions_;
    std::vector<MissionMessage> messages_;
    std::int32_t timerValue_ = -1;    // _gTimerValue
    bool win_ = false, lose_ = false, gameOver_ = false, endHandled_ = false;  // gWin, gLose, _GameOver, _bool_missionfailed
    std::array<std::uint8_t, kMaxSides> buildingsExist_{}, unitsExist_{};  // _gBuildingsExist / _gUnitsExist
    std::array<std::uint8_t, kMaxSides> persideFlags_{};  // _persideflags_6B8258
    std::array<int, kMaxSides> defeatOrder_{-1, -1, -1, -1, -1, -1, -1, -1};  // DAT_00797b70
    int defeatCount_ = 0;             // DAT_00797e38
    // Play as any side (not original): the mission's own player side (-1 = normal game) and the
    // sides seen hostile with objects since the start (bit per side; the elimination win needs one).
    int scriptSide_ = -1;
    std::uint8_t playAsHostiles_ = 0;
    // Computer players.
    std::array<AiState, kMaxSides> ai_{};
    // Multiplayer start locations (_SpawnLocations 0x6d7ba8, _FreeSpawnLocations 0x782aa0).
    std::vector<std::array<int, 2>> spawnLocations_;
    std::vector<std::uint8_t> spawnUsed_;
    // Crates / spice blooms (gCrates 0x517b98) and _NumCratesAllowed (0 outside skirmish+crates).
    std::array<Crate, kMaxCrates> crates_{};
    std::vector<Particle> particles_ = std::vector<Particle>(kMaxParticles);
    int particleCount_ = 0;
    void addParticle(float x, float y, float vx, float vy, std::uint32_t life, std::uint16_t tile, int tpx, int tpy);  // 0x45d460
    void updateParticles();  // FUN_0045d4f0
    int numCratesAllowed_ = 0;
    std::array<int, kMaxSides> playerColors_{-1, -1, -1, -1, -1, -1, -1, -1};
    CheatState cheats_;
    std::vector<RallyPoint> rallyPoints_;    // not original (rally.cpp)
    std::vector<RallyPending> rallyPending_;
    ExtMission ext_;
    ExtUiRequests extUi_;  // not saved
};

}  // namespace d2k::game
