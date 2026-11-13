// Mission setup in the original order (re/notes/map-and-spawn.md §3):
// Setup_SpawnMapObjects 0x469790, ModelAddUnit 0x455870, ModelAddBuilding 0x4563b0,
// ModelAddConcrete 0x456210, GetBuildingOnConcreteCount 0x44dac0, MarkBuildingTiles 0x44dbd0,
// UpdateSpiceInRegion 0x44ec50. RNG is consumed exactly where the original does
// (spice blooms, concrete tiles).

#include "game/geometry.hpp"
#include "game/spice_tables.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
constexpr std::uint16_t kThinSpiceTile = 0x319;   // 793
constexpr std::uint16_t kThickSpiceTile = 0x2ED;  // 749
constexpr std::uint16_t kConcreteTiles[3] = {0x28B, 0x29F, 0x2B3};  // 651/671/691
constexpr int kTileDataEntries = formats::kTileDataEntries;
// Concrete pseudo types queued for specials 0x3C/0x3D/0x3E and their 4x4 masks.
constexpr std::uint32_t kConcreteMasks[3] = {0x33, 0x777, 0xFFFF};
// MyVersionOfUnit / MyVersionOfBuilding 0x46c570 / 0x46c450: last house-matching template
// of the group, else the first of the group (precomputed in Templates).
int myVersionOfUnit(const formats::Templates& t, int group, int house) {
    if (group < 0 || group >= formats::kMaxUnitTypes || house < 0 || house >= 8) return -1;
    return t.unitHouseVersion[group][house];
}
bool equalsNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}
int myVersionOfBuilding(const formats::Templates& t, int group, int house) {
    if (group < 0 || group >= formats::kMaxBuildingTypes || house < 0 || house >= 8) return -1;
    return t.buildingHouseVersion[group][house];
}
}  // namespace

std::uint32_t* World::linearCellFlags(int x, int y) {
    // The original indexes CellNumbersWidthSpan[y] + x without checking x < width (the grid
    // is a fixed 128x128 array). Reproduce the linear index; skip anything outside our grid.
    const long i = static_cast<long>(y) * width_ + x;
    if (x < 0 || y < 0 || i < 0 || i >= static_cast<long>(cells_.size())) return nullptr;
    return &cells_[static_cast<std::size_t>(i)].flags;
}

// Setup_SpawnMapObjects 0x469790 (campaign path).
void World::spawnMapObjects(const formats::MapFile& map, const formats::TileAtr& tileAtr,
                            const formats::TileData& tileData, const SkirmishSetup* skirmish) {
    struct Queued { int x, y; std::uint32_t data; };
    std::vector<Queued> queue;
    std::vector<Queued> generic;  // community extended-map objects
    spawnLocations_.clear();
    spawnUsed_.clear();
    // Pass 1, row-major: static flags from TILEATR, then the special layer.
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x) {
            const auto& mc = map.at(x, y);
            Cell& c = cell(x, y);
            // Vanilla stores the raw tile word; we mask the community high bits (see TileAtr).
            c.tile = static_cast<std::uint16_t>(mc.tile & 0x0FFF);
            c.flags = tileAtr.flagsFor(mc.tile);
            c.shroud = 0x11;
            c.damage = 0;
            const unsigned special = mc.special;
            switch (special) {
            case 0: break;
            case 1:  // thin spice
                c.tile = kThinSpiceTile;
                c.flags = 0x216000;
                break;
            case 2:  // thick spice
                c.tile = kThickSpiceTile;
                c.flags = 0x416000;
                break;
            case 3: break;  // DebugFatal in the original
            case 0x17:
                // Multiplayer start location: SpawnLocations[n] = (x, y), free (DebugFatal past 8).
                if (spawnLocations_.size() < 8) {
                    spawnLocations_.push_back({x, y});
                    spawnUsed_.push_back(0);
                }
                break;
            case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
            {
                // Spice bloom: PlaceCrate(x, y, 2000 + rand() % 1000, 7, 4, regrowths), with
                // 5 / 10 / 25 / 50 / 255 regrowths for specials 0x29..0x2d.
                static constexpr std::uint8_t kBloomRespawns[5] = {5, 10, 25, 50, 255};
                const std::uint32_t timer = static_cast<std::uint32_t>(rng_.next()) % 1000 + 2000;
                placeCrate(x, y, timer, 7, 4, kBloomRespawns[special - 0x29]);
            }
                c.tile = kThinSpiceTile;
                c.flags = 0x217000;
                break;
            default: {
                // Community extended maps (setupmapstuff.c): generic crate (1TTTTIIIEEEEEEEE),
                // unit (01TCDDDSSSUUUUUU) and building (001TPHSSSBBBBBBB) special values, beyond
                // the TILEDATA table. Placed after this pass (the patch sets every cell's flags first).
                if (special >= static_cast<unsigned>(kTileDataEntries) && (special & 0xE000)) {
                    generic.push_back({x, y, special});
                    break;
                }
                if (special >= static_cast<unsigned>(kTileDataEntries)) break;  // out of the table in vanilla
                const auto& td = tileData[special];
                const std::uint32_t raw = td.index | (std::uint32_t(td.side) << 16) |
                                          (std::uint32_t(static_cast<std::uint8_t>(td.type)) << 24);
                if (raw == 0xFFFFFFFFu) break;
                std::uint32_t data = raw;
                if (special == 0x3C) data = 0x800000C8u;
                else if (special == 0x3D) data = 0x800000C9u;
                else if (special == 0x3E) data = 0x800000CAu;
                queue.push_back({x, y, data});
                break;
            }
            }
        }
    for (const auto& g : generic) {
        const int gx = g.x, gy = g.y;
        const unsigned v = g.data;
        if (v & 0x8000) {  // crate: type, image, extended data
            const int type = static_cast<int>((v >> 11) & 15), image = static_cast<int>((v >> 8) & 7);
            const int ext = static_cast<int>(v & 255);
            if (type >= 7 && type <= 10)  // spice bloom spawner / blooms
                placeCrate(gx, gy, static_cast<std::uint32_t>(rng_.next()) % 1000 + 2000, type, image, ext);
            else
                placeCrate(gx, gy, 0x7fffffffu, type, image, 0);
        } else if (v & 0x4000) {  // unit
            const int type = static_cast<int>(v & 63), side = static_cast<int>((v >> 6) & 7);
            const int dir = static_cast<int>(((v >> 9) & 7) << 2);
            if (Unit* u = unit(side, addUnit(side, type, gx, gy, gx, gy, 0, 0))) {
                u->facing = u->facingTurret = u->facingTurretTarget = dir;
                if (v & 0x1000) u->flags |= unit_flags::kStealth;
                if (v & 0x2000) u->tag = 1;
            }
        } else {  // building
            const int type = static_cast<int>(v & 127), side = static_cast<int>((v >> 7) & 7);
            const bool noHarv = (v & 1024) != 0, primary = (v & 2048) != 0, tagged = (v & 4096) != 0;
            const int idx = addBuilding(side, type, gx, gy, true, noHarv, false);
            if (Building* b = building(side, idx)) {
                if (rules_.templates->bin.buildings[type].specialBehavior == building_behaviour::kTurret)
                    b->facing = static_cast<int>(((v >> 10) & 3) << 3);
                else if (primary)
                    setBuildingAsPrimary(side, idx);
                if (tagged) b->tag = 1;
            }
        }
    }
    // Radar_Rebuild: not needed by the model.
    updateSpiceInRegion(0, 0, width_, height_);

    const auto& T = *rules_.templates;
    if (skirmish) {
        // Pass 2, skirmish (GameType 1; 0x469d6e..0x46a298). (The original DebugFatals unless
        // the map has exactly 8 spawn locations.)
        const SkirmishSetup& S = *skirmish;
        const int humans = 1;  // NetPlayerCount = 1 for a local skirmish
        // Every CAI disabled, CAI[0] (the _SPAWN.MIS template) copied to sides 1..7.
        for (auto& a : ai_) a.raw[1] = 0;
        for (int s = 1; s < kMaxSides; ++s) {
            ai_[s].raw = ai_[0].raw;
            ai_[s].raw[0] = static_cast<std::uint8_t>(s);
        }
        // Computer players: random house each, AI on.
        for (int p = humans; p < humans + S.aiPlayers && p < kMaxSides; ++p) {
            sides_[p].house = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % 3);
            ai_[p].raw[0] = static_cast<std::uint8_t>(p);
            ai_[p].raw[1] = 1;
        }
        // The MCV: the last unit template with behaviour 6.
        int mcv = -1;
        for (int i = 0; i < formats::kMaxUnitTypes; ++i)
            if (T.bin.units[i].specialBehavior == behaviour::kMcv) mcv = i;
        const int players = humans + S.aiPlayers;  // (> 8: DebugFatal)
        if (mcv < 0 || spawnLocations_.empty()) return;  // DebugFatal "No MCV" / no flags
        const int nLoc = static_cast<int>(spawnLocations_.size());
        int worms = 0;
        for (int p = 0; p < players && p < kMaxSides; ++p) {
            int loc = 0;
            if (p == 0) {
                loc = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(nLoc));
            } else {
                // The free location whose nearest used location is farthest (first one wins ties;
                // distances truncated like __ftol(sqrt(dx*dx + dy*dy))).
                std::uint32_t best = 0;
                for (int i = 0; i < nLoc; ++i) {
                    if (spawnUsed_[i]) continue;
                    std::uint32_t nearest = 0xFFFFF;
                    for (int j = 0; j < nLoc; ++j) {
                        if (j == i || spawnUsed_[j] != 1) continue;
                        const int dx = spawnLocations_[i][0] - spawnLocations_[j][0];
                        const int dy = spawnLocations_[i][1] - spawnLocations_[j][1];
                        const auto d = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                        if (d < nearest) nearest = d;
                    }
                    if (best < nearest) {
                        best = nearest;
                        loc = i;
                    }
                }
            }
            spawnUsed_[loc] = 1;
            const int sx = spawnLocations_[loc][0], sy = spawnLocations_[loc][1];
            int x = sx, y = sy;
            nearestFreeTile(x, y, 6);
            const int mcvType = myVersionOfUnit(T, T.bin.units[mcv].unitGroup, sides_[p].house);
            if (mcvType >= 0) addUnit(p, mcvType, x, y, x, y, 0, 0);
            // Starting units: TECHPOS[techLevel][k] for k < gNetUnitCount, at the spawn location
            // + (dx, dy) clamped to the map, then the nearest free tile (radius 6).
            if (S.techPos && S.techLevel >= 0 && S.techLevel < 10) {
                int type = -1;  // (an invalid house DebugFatals and reuses the previous type)
                for (int k = 0; k < S.unitCount && k < 10; ++k) {
                    const auto& e = S.techPos->entries[S.techLevel][k];
                    if (e.unitGroup[0] == -1) continue;
                    int ux = std::clamp(sx + e.dx, 0, width_ - 1), uy = std::clamp(sy + e.dy, 0, height_ - 1);
                    const int house = sides_[p].house;
                    if (house >= 0 && house <= 2) type = myVersionOfUnit(T, static_cast<std::uint8_t>(e.unitGroup[house]), house);
                    nearestFreeTile(ux, uy, 6);
                    if (type >= 0) addUnit(p, type, ux, uy, ux, uy, 0, 0);
                }
            }
            // The map's sandworms (queued units with behaviour 5), counted over all players, up
            // to gNetWorms (quirk: inside the player loop, so a large gNetWorms adds them again).
            for (const auto& q : queue) {
                if (q.data & 0x80000000u) continue;
                const int side = static_cast<int>((q.data >> 16) & 0xFF);
                if (side >= kMaxSides) continue;
                const int type = myVersionOfUnit(T, static_cast<int>(q.data & 0xFFFF), sides_[side].house);
                if (type < 0 || T.bin.units[type].specialBehavior != behaviour::kSandworm) continue;
                if (++worms > S.worms) continue;
                addUnit(side, type, q.x, q.y, q.x, q.y, 0, 0);
            }
        }
        return;
    }
    // Pass 2 (campaign: !NetworkGame && GameType != 1), queued objects in queue order.
    for (const auto& q : queue) {
        const int side = static_cast<int>((q.data >> 16) & 0xFF);
        const int group = static_cast<int>(q.data & 0xFFFF);
        if (side >= kMaxSides) continue;
        const int house = sides_[side].house;
        if (!(q.data & 0x80000000u)) {
            const int type = myVersionOfUnit(T, group, house);
            if (type >= 0) addUnit(side, type, q.x, q.y, q.x, q.y, 0, 0);
        } else if (group == 200 || group == 201 || group == 202) {
            // ModelAddConcrete(7, MyVersionOfBuilding(GroupIDs.Concrete1), x, y, 1, mask).
            // Deviation: GroupIDs (0x6d60e5) is not loaded; we take the first concrete
            // template (behaviour 15). No vanilla TILEDATA entry uses these specials.
            int type = -1;
            for (int i = 0; i < T.bin.buildingCount && type < 0; ++i)
                if (T.bin.buildings[i].specialBehavior == 15) type = i;
            if (type >= 0) addConcrete(7, type, q.x, q.y, kConcreteMasks[group - 200]);
        } else {
            const int type = myVersionOfBuilding(T, group, house);
            if (type >= 0) addBuilding(side, type, q.x, q.y, true, false, false);
        }
    }
    // (DebugFatal "No worms found on this map" when no sandworm was spawned: ignored.)
}

// UpdateSpiceInRegion 0x44ec50 over [x0,x1) x [y0,y1).
void World::updateSpiceInRegion(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width_) x1 = width_;
    if (y1 > height_) y1 = height_;
    auto spiceAt = [&](int x, int y) { return (cell(x, y).flags & kSpiceMask) != 0; };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            Cell& c = cell(x, y);
            const std::uint32_t level = c.flags & kSpiceMask;
            if (level == 0) continue;
            if (level <= 0x200000) {
                int m = 0;
                if (x < 1) {
                    m = 0x29;
                } else {
                    if (spiceAt(x - 1, y)) m |= 0x08;
                    if (y < height_ - 1 && spiceAt(x - 1, y + 1)) m |= 0x20;
                    if (y > 0 && spiceAt(x - 1, y - 1)) m |= 0x01;
                }
                if (x < width_ - 1) {
                    if (spiceAt(x + 1, y)) m |= 0x10;
                    if (y > 0 && spiceAt(x + 1, y - 1)) m |= 0x04;
                    if (y < height_ - 1 && spiceAt(x + 1, y + 1)) m |= 0x80;
                } else {
                    m |= 0x94;
                }
                if (y < height_ - 1) {
                    if (spiceAt(x, y + 1)) m |= 0x40;
                } else {
                    m |= 0xE0;
                }
                if (y < 1) m |= 0x07;
                else if (spiceAt(x, y - 1)) m |= 0x02;
                c.tile = kThinSpiceTiles[m];
            } else {
                c.tile = kThickSpiceTiles[kThickSpicePattern[y & 7][x & 7] - 1];
            }
        }
}

// ModelAddUnit 0x455870. Returns the object index or -1 (0xFFFF in the original).
int World::addUnit(int side, int type, int x, int y, int tx, int ty, int pxOffX, int pxOffY) {
    const auto& T = *rules_.templates;
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxUnitTypes) return -1;
    const auto& t = T.bin.units[type];
    const int beh = t.specialBehavior;
    const bool airOrWorm = beh == behaviour::kCarryall || beh == behaviour::kFrigate ||
                           beh == behaviour::kOrnithopter || beh == behaviour::kDeathHand ||
                           beh == behaviour::kSandworm;
    if (!airOrWorm) {
        if (!inBounds(x, y)) return -1;  // DebugFatal "Unit added outside map"
        const std::uint32_t f = cell(x, y).flags;
        if ((f & kBuilding) && static_cast<int>(f & kOwnerMask) != side) return -1;
    }
    Side& s = sides_[side];
    if (!s.participates) return -1;
    const int idx = s.allocObject();
    if (idx < 0) return -1;  // "ReachedUnitLimit"
    ++s.unitsBuilt;
    ++s.unitsBuiltPerType[type];

    Unit& u = s.units[idx];
    u = Unit{};
    u.alive = true;
    u.index = idx;
    u.side = side;
    u.type = type;
    u.health = static_cast<std::int32_t>(t.hitPoints);
    u.flags = t.flags;
    u.state = UnitState::Idle;
    u.oldState = UnitState::None;
    u.selected = false;
    u.turnDelay = t.unitRotationSpeed;
    u.turretTurnDelay = t.barrelRotationSpeed;
    u.reloadDelay = t.primaryWeaponLongDelay;
    u.stealthTimer = 0;
    u.animFrame = 0;
    u.animDelay = 0;
    u.animType = 0;
    u.lying = 0;
    u.deadStateTimer = t.isInfantry ? 50 : 10;
    u.smokeTimer = 0;
    u.groupId = 0xFF;
    u.enemyIndex = kNoIndex;
    u.enemySide = side;
    u.lastAttacked = 0xFF;
    u.lastFired = 0xFF;
    u.lastMoved = 0xFF;
    u.byte29 = 0;
    u.speed = t.speed;
    u.attackerIndex = kNoIndex;
    u.refineryIndex = kNoIndex;
    u.repairPadIndex = kNoIndex;
    u.attackerSide = 0xFF;
    u.routeSteps = 0;
    u.routeTotalSteps = 0;
    u.routeMode = 0;
    u.posX = (static_cast<std::int8_t>(x) * 32 + 16 + static_cast<std::int8_t>(pxOffX)) * 0x10000;
    u.routeBlocked = false;
    u.routeRetryDelay = 0;
    u.posZ = 0;
    u.posY = (static_cast<std::int8_t>(y) * 32 + 16 + static_cast<std::int8_t>(pxOffY)) * 0x10000;
    u.facing = (x == tx && y == ty) ? 0 : getDir8(x, y, tx, ty);
    u.blockFromX = u.blockToX = u.rememberX = x;
    u.blockFromY = u.blockToY = u.rememberY = y;
    u.targetX = tx;
    u.targetY = ty;
    u.facingTurret = u.facingTurretTarget = 0;
    u.stepTargetX = u.stepTargetY = 0;
    u.posSteps = 0;
    u.cellspotTo = u.cellspotFrom = 0;
    s.linkObject(idx, SlotKind::Unit);
    ++s.unitsExistPerType[type];

    if (beh == behaviour::kDeathHand) {
        u.flags |= 0x20040;
        u.posZ = 0x3C0000;
        u.facing = 0;
        u.byte29 = 8;
        unitAdjustState(u, UnitState::Moving);
        u.targetX = width_ / 2;
        u.targetY = height_ / 2;
    } else if (beh == behaviour::kHarvester) {
        ++s.harvesterCount;
        u.refineryIndex = getRefineryIndex(side);
    } else if (beh == behaviour::kCarryall) {
        ++s.carryallCount;
        u.posZ = 0x3C0000;
        u.flags |= kFlying;
        // The pickup queue grows by one entry per carryall (max 32).
        ++s.pickupCount;
        ++s.carryallsAlive;
        if (s.pickupCount > 31) s.pickupCount = 32;
        // Carryalls fly in from the map edge nearest to the target (bytes, as the original).
        int ex = tx & 0xff, ey = ty & 0xff;
        const int dir = carryallEntryPoint(ex, ey, 0);
        u.facingTurret = u.facing = dir;
        u.posX = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ex)) << 21);
        u.blockFromX = u.blockToX = u.rememberX = ex & 0xff;
        u.blockFromY = u.blockToY = u.rememberY = ey & 0xff;
        u.posY = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ey)) << 21);
        u.targetX = x & 0xff;
        u.targetY = y & 0xff;
        unitAdjustState(u, UnitState::Moving);
    } else if (beh == behaviour::kFrigate || beh == behaviour::kOrnithopter || beh == behaviour::kBird) {
        u.posZ = 0x3C0000;
        u.flags |= kFlying;
    }

    // Initial state.
    bool adjust = true;
    UnitState st = UnitState::Idle;
    if ((x == tx && y == ty) || beh == behaviour::kDeathHand || beh == behaviour::kCarryall) {
        if (beh == behaviour::kHarvester) {
            st = UnitState::Harvesting;
        } else if (side == mySide_ /* || NetworkGame */ || beh == behaviour::kCarryall ||
                   beh == behaviour::kFrigate || beh == behaviour::kDeathHand) {
            adjust = false;  // stays Idle
        } else {
            st = UnitState::Guarding;  // other sides' units in single player guard their post
        }
    } else {
        st = UnitState::LeavingBuilding;
    }
    if (adjust) unitAdjustState(u, st);
    if (beh == behaviour::kSandworm) {
        u.animDelay = vars().sandWormInitialSleep;
        unitAdjustState(u, UnitState::Sleeping);
        u.flags |= kNotDrawn;
    }

    // Occupancy.
    if (!airOrWorm) {
        std::uint32_t& f = cell(u.blockFromX, u.blockFromY).flags;
        if (f & kVehicle) {
            // DebugFatal "Tile already occupied by unit": broken map; leave the cell alone.
        } else if (!t.isInfantry) {
            f = (f & ~kOwnerMask) | static_cast<std::uint32_t>(side) | kVehicle;
        } else {
            std::uint32_t bit = 0x200;
            int spot = 4;
            while (bit >= 0x20 && (f & bit)) {
                bit >>= 1;
                --spot;
            }
            if (bit >= 0x20) {
                f = (f & ~kOwnerMask) | bit | static_cast<std::uint32_t>(side);
                f = (f & 0xF1FFFFFFu) | (static_cast<std::uint32_t>(side) << kInfantryOwnerShift);
                u.cellspotTo = u.cellspotFrom = spot;
                static constexpr int kSpot[5][2] = {{16, 21}, {23, 14}, {23, 26}, {7, 26}, {7, 14}};  // 0x4c93e8
                u.posX += (kSpot[spot][0] - 16) * 0x10000;
                u.posY += (kSpot[spot][1] - 16) * 0x10000;
            }
            // else DebugFatal "Couldn't find free slot for bloke"
        }
    }
    if (beh == behaviour::kFremen || equalsNoCase(T.bin.unitNames[type], "Stealth Raider")) u.flags |= kStealth;
    return idx;
}

// ModelAddConcrete 0x456210.
void World::addConcrete(int side, int type, int x, int y, std::uint32_t mask) {
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const int cx = x + c, cy = y + r;
            if (cx >= width_ || cy >= height_ || !(mask & bit)) continue;
            if (side == mySide_) revealBuildingType(type, cx, cy);  // FUN_0044e330
            Cell& cl = cell(cx, cy);
            const std::uint32_t f = cl.flags;
            if ((f & 0x13F8) == 0 && (f & kBuildOn) && !(f & kConcrete) && cy != 0) {
                cl.damage = 0;
                cl.tile = kConcreteTiles[static_cast<std::uint32_t>(rng_.next()) % 3];
                cl.flags = (static_cast<std::uint32_t>(side & 0xFF) << kConcreteOwnerShift) | (f & 0xFFF1FFFFu) |
                           kConcrete;
            }
        }
}

// GetBuildingOnConcreteCount 0x44dac0 (also zeroes terrain damage under the whole footprint).
void World::buildingConcreteCount(int type, int x, int y, int& total, int& onConcrete) {
    const auto& t = rules_.templates->bin.buildings[type];
    total = onConcrete = 0;
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const long i = static_cast<long>(y + r) * width_ + x + c;
            Cell* cl = (i >= 0 && i < static_cast<long>(cells_.size())) ? &cells_[static_cast<std::size_t>(i)] : nullptr;
            if ((t.tilesOccupiedAll & bit) && cl) cl->damage = 0;
            if (t.tilesOccupiedSolid & bit) {
                ++total;
                if (cl && (cl->flags & kConcrete)) ++onConcrete;
            }
        }
    if (total == 0) total = 1;  // DebugFatal "buildTileCount = 0"; avoid dividing by zero
}

// ModelAddBuilding 0x4563b0.
int World::addBuilding(int side, int type, int x, int y, bool placeConcrete, bool noFreeHarvester,
                       bool noConyardAnim) {
    const auto& T = *rules_.templates;
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxBuildingTypes) return -1;
    Side& s = sides_[side];
    if (!s.participates) return -1;
    const int idx = s.allocObject();
    if (idx < 0) return -1;
    const auto& t = T.bin.buildings[type];
    ++s.buildingsBuilt;
    ++s.buildingsBuiltPerType[type];
    Building& b = s.buildings[idx];
    b = Building{};
    b.alive = true;
    b.index = idx;
    b.type = type;
    b.state = 2;
    b.selected = false;
    b.health = static_cast<std::int32_t>(t.hitPoints);
    b.turretTurnDelay = t.barrelRotationSpeed;
    b.reloadDelay = t.primaryWeaponLongDelay;
    b.enemySide = side;
    b.enemyIndex = kNoIndex;
    b.side = side;
    b.attackerIndex = kNoIndex;
    b.attackerSide = 0xFF;
    if (!(t.flags & 0x400000) && (placeConcrete || (t.specialBehavior == 2 && !noFreeHarvester)))
        addConcrete(side, type, x, y, t.tilesOccupiedAll);
    int total = 0, onConcrete = 0;
    buildingConcreteCount(type, x, y, total, onConcrete);
    b.flags = t.flags;
    if (!(t.flags & 0x400000)) {
        const std::uint32_t hp = static_cast<std::uint32_t>(b.health);
        b.health = static_cast<std::int32_t>(((static_cast<std::uint32_t>(onConcrete) * hp) >> 1) /
                                             static_cast<std::uint32_t>(total)) +
                   b.health / 2;
        if (onConcrete != total) b.flags |= 0x8000;  // decays
    }
    b.lastAttacked = 0xFF;
    b.posX = (x & 0xFF) << 21;
    b.posY = (t.artHeight + (y & 0xFF) * 32) << 16;
    b.wallFrame = 0;
    b.facing = 0;
    b.tileX = b.maxX = x;
    b.tileY = b.maxY = y;
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1)
            if (t.tilesOccupiedSolid & bit) {
                if (b.maxX < x + c) b.maxX = x + c;
                if (b.maxY < y + r) b.maxY = y + r;
            }
    b.timeAdded = gameTicks_;
    // Build-up animation (quirk: the frame table is indexed by the building type here).
    if (!placeConcrete && T.bin.buildupArtFrames[type] != 0) {
        b.buildupCounter = 0;
        b.buildupTick = gameTicks_;
        b.flags |= building_flags::kBuildup;
    }
    ++s.buildingsExistPerGroup[t.buildingGroup];
    s.linkObject(idx, SlotKind::Building);
    updateIcons(side);
    int slot = -1;
    switch (t.specialBehavior) {
    case 2: slot = 0; break;
    case 7: slot = 1; break;
    case 8: slot = 2; break;
    case 9: slot = 3; break;
    case 10: slot = 4; break;
    case 11: slot = 5; break;
    case 12: slot = 6; break;
    case 13: slot = 7; break;
    case 0x13: slot = 8; break;
    case 0x11: slot = 9; break;
    case 0x12: slot = 10; break;
    default: break;
    }
    if (slot >= 0 && s.primaryBuilding[slot] == kNoIndex) {
        s.primaryBuilding[slot] = idx;
        b.flags |= 0x80;  // primary
    }
    if (!placeConcrete && s.primaryBuilding[0] != kNoIndex && !noConyardAnim)
        s.buildings[s.primaryBuilding[0]].flags |= 0x20;  // conyard animates once
    if (side == mySide_) revealBuildingType(type, x, y);  // FUN_0044e330
    markBuildingTiles(side, type, x, y);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if ((t.tilesOccupiedSolid >> (r * 4 + c)) & 1)
                if (inBounds(x + c, y + r)) {
                    cell(x + c, y + r).buildingSide = static_cast<std::int16_t>(side);
                    cell(x + c, y + r).buildingIndex = static_cast<std::int16_t>(idx);
                }
    // A refinery gets a free harvester, delivered by carryall to its dock (also at mission start).
    if (t.specialBehavior == building_behaviour::kRefinery && !noFreeHarvester)
        queueDelivery(s, x + 2, y + 1, static_cast<std::uint8_t>(T.bin.groupIds[group_ids::kHarvester]), 1, 4);
    // TODO(buildings): wall/turret frame (FUN_0044e580/0x44e6d0).
    if (t.flags & building_flags::kHasSkirt) placeBuildingSkirt(x, y, type);  // Map__PlaceBuildingSkirt 0x44f1b0
    return idx;
}

// MarkBuildingTiles 0x44dbd0.
void World::markBuildingTiles(int side, int type, int x, int y) {
    const auto& t = rules_.templates->bin.buildings[type];
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1)
            if (t.tilesOccupiedAll & bit)
                if (std::uint32_t* f = linearCellFlags(x + c, y + r)) *f &= ~kBuildOn;
    bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(t.tilesOccupiedSolid & bit)) continue;
            // Units of another side standing on a solid tile change to the building's side
            // (counted as lost for their side): only the side GetUnitOnTile reports is scanned.
            if (inBounds(x + c, y + r)) {
                int us = 0, ui = 0;
                if (getUnitOnTile((x + c) * 32, (y + r) * 32, us, ui, false)) {
                    int it = kNoIndex;
                    const int tx = static_cast<std::uint8_t>(x + c), ty = static_cast<std::uint8_t>(y + r);
                    while (Unit* v = getNextUnitOnTile(tx, ty, us, it)) {
                        if ((us & 0xff) == (side & 0xff)) continue;
                        ++sides_[us].unitsLost;
                        ++sides_[us].unitsLostPerType[v->type];
                        changeUnitOwner(us, side, it & 0xffff, false);
                    }
                }
            }
            std::uint32_t* f = linearCellFlags(x + c, y + r);
            if (!f) continue;
            *f = (*f & 0xFFFF7FF8u) | static_cast<std::uint32_t>(side) | kBuilding;
            if (t.specialBehavior == 6 && !(c == 1 && r == 1)) *f |= kExitTile;  // repair pad
            if (t.specialBehavior == 8 && c == 0 && r == 2)
                if (std::uint32_t* e = linearCellFlags(x, y + 2)) *e |= kExitTile;
            if (t.specialBehavior == 7 && c == 2 && r == 1)
                if (std::uint32_t* e = linearCellFlags(x + 2, y + 1)) *e |= kExitTile;
            // (radar pixel in side colour: renderer's business)
        }
    if (t.specialBehavior == 14 || t.specialBehavior == 16)
        if (std::uint32_t* f = linearCellFlags(x, y)) *f |= kWall;
}

}  // namespace d2k::game
