// UpdateBuilding 0x49b7f0 and the building combat helpers (re/notes/combat.md §6):
// BuildingAutoTarget 0x496790, BuildingFireAt 0x496030, RotateBuildingTurret 0x493fd0,
// building attack states 0x4975d0 / 0x4976b0 / 0x497090, removal 0x44dee0 / 0x44df60,
// primary reassignment 0x46d9a0.

#include "game/geometry.hpp"
#include "game/world.hpp"

#include <cmath>

namespace d2k::game {

using namespace cell_flags;
using namespace building_flags;

namespace {
inline int px16(std::int32_t v) { return v / 65536; }
inline int div32(int v) { return v / 32; }
}  // namespace

// 0x493fd0: one facing step towards `want` every barrelRotationSpeed ticks.
void World::rotateBuildingTurret(Building& b, int want) {
    const int f = b.facing;
    if (f == want) return;
    if (b.turretTurnDelay != 0) {
        --b.turretTurnDelay;
        return;
    }
    const int d = f - want < 0 ? want - f : f - want;
    int step;
    if (d < 16) step = f <= want ? 1 : -1;
    else step = want < f ? 1 : -1;
    b.facing = (f + step) & 31;
    b.turretTurnDelay = buildingTemplate(b).barrelRotationSpeed;
}

// BuildingFireAt 0x496030 ("ShootTarget"): only actLikeTurret buildings shoot. Fires from the
// centre of the art box's top-left tile once the turret faces the target.
void World::buildingFireAt(Building& b, int tx, int ty, int targetSide, int targetIndex, int kind) {
    const auto& t = buildingTemplate(b);
    if (t.primaryWeapon == -1) return;  // DebugFatal "Building %s told to shoot"
    const std::uint32_t ox = static_cast<std::uint32_t>(px16(b.posX) + 16);
    const std::uint32_t oy = static_cast<std::uint32_t>(px16(b.posY) - t.artHeight + 16);
    const int want = getFacing(static_cast<int>(ox & 0xffff), static_cast<int>(oy & 0xffff), tx, ty);
    // Turrets need power >= 100 % unless the template's requireEnoughPower is 0.
    if (!t.actLikeTurret || (sides_[b.side].powerPercent < 100 && t.requireEnoughPower)) return;
    if (b.facing != want) {
        rotateBuildingTurret(b, want);
        return;
    }
    const int dx = static_cast<int>(ox & 0xffff) - tx, dy = static_cast<int>(oy & 0xffff) - ty;
    const std::uint16_t dist = static_cast<std::uint16_t>(
        static_cast<std::int64_t>(std::sqrt(static_cast<double>(static_cast<std::uint32_t>(dx * dx + dy * dy)))));
    // Quirk: the weapon choice looks at the building's stored enemy, as a building (kind 2).
    const int w = newBestBullet(t.primaryWeapon, t.secondaryWeapon, static_cast<std::uint32_t>(dist) * dist,
                                static_cast<std::int8_t>(b.enemySide), b.enemyIndex, 2);
    std::uint32_t utx = static_cast<std::uint32_t>(tx), uty = static_cast<std::uint32_t>(ty);
    const std::uint32_t n = dist >> 5;
    if (n != 0) {
        utx = utx + static_cast<std::uint32_t>(rng_.next()) % (n * 8) - n * 4;
        uty = uty + static_cast<std::uint32_t>(rng_.next()) % (n * 8) - n * 4;
    }
    if (t.muzzleFlashExplosion != -1) {
        const ArtSize sz = buildingFrameSize(t.barrelArt, t.directionFrames[b.facing & 31]);
        const int half = sz.width >> 1;
        const int e = addExplosion(b.side, t.muzzleFlashExplosion, t.exitPoint1X + px16(b.posX) + half,
                                   t.exitPoint1Y + px16(b.posY) - half, 0);
        if (e >= 0) sides_[b.side].explosions[e].frame = b.facing;
    }
    playSound(weapon(w).firingSound, static_cast<int>((ox & 0xffff) >> 5), static_cast<int>((oy & 0xffff) >> 5));
    if (targetSide == mySide_) {
        const int cx = static_cast<int>((ox & 0xffff) >> 5) & 0xff, cy = static_cast<int>((oy & 0xffff) >> 5) & 0xff;
        if (inBounds(cx, cy)) cell(cx, cy).shroud = 0;
        updateShroudInRegion(cx - 2, cy - 2, cx + 2, cy + 2);  // UpdateShroudInRegion 0x44cd10
    }
    if (kind != 1) {
        targetSide = -1;
        targetIndex = kNoIndex;
    }
    addBullet(b.side, w, 0, b.index, static_cast<int>(ox & 0xffff), static_cast<int>(oy & 0xffff),
              static_cast<int>(utx & 0xffff), static_cast<int>(uty & 0xffff), targetIndex, targetSide);
    b.reloadDelay = t.primaryWeaponLongDelay;
}

// BuildingAutoTarget 0x496790 (state 2, every 8th tick or while it has an enemy).
bool World::buildingAutoTarget(Building& b) {
    // gAIArray[side].byte1 == 0 (AI not enabled) -> only enemy turrets are attacked among
    // buildings (0x496790 line 22).
    const bool turretsOnly = !ai_[b.side & 7].enabled();
    if (b.reloadDelay != 0) return false;
    const auto& t = buildingTemplate(b);
    const int p = t.primaryWeapon, s = t.secondaryWeapon;
    if (p == -1 && s == -1) return false;
    const int ox = px16(b.posX) + 16, oy = px16(b.posY) - t.artHeight + 16;
    if (b.enemyIndex != kNoIndex && b.enemySide != b.side && b.enemySide >= 0 && b.enemySide < kMaxSides) {
        const int es = b.enemySide, ei = b.enemyIndex;
        Side& S = sides_[es];
        if (S.links[ei].kind == SlotKind::Unit) {
            const Unit& e = S.units[ei];
            const int ex = px16(e.posX), ey = px16(e.posY);
            if (canBuildingAttackTile(b, div32(ex), div32(ey), false)) {
                buildingFireAt(b, ex, ey, es, ei, 1);
                return true;
            }
        } else if (S.links[ei].kind == SlotKind::Building &&
                   buildingTemplate(S.buildings[ei]).specialBehavior == building_behaviour::kTurret) {
            int cx = 0, cy = 0;
            closestBuildingTile(div32(ox), div32(oy), S.buildings[ei], cx, cy);
            if (canBuildingAttackTile(b, cx, cy, false)) {
                buildingFireAt(b, cx * 32 + 16, cy * 32 + 16, es, ei, 2);
                return true;
            }
        }
    }
    int x = 0, y = 0, fs = 0, fi = kNoIndex, kind = 1;
    if (!findNearestEnemy(ox, oy, b.side, 0, x, y, fs, fi, p, s, false, true, true)) {
        if (!findNearestEnemyBuilding(ox, oy, b.side, x, y, fs, fi, p, s, turretsOnly)) return false;
        kind = 2;
        buildingFireAt(b, x, y, fs, fi, kind);
    } else if (const Unit* e = unit(fs, fi)) {
        int s2 = 0, i2 = 0;
        if (unitTemplate(*e).specialBehavior == behaviour::kSandworm && getUnitOnTile(x, y, s2, i2, false)) return true;
        buildingFireAt(b, x, y, fs, fi, kind);
    }
    b.enemySide = fs;
    b.enemyIndex = fi;
    return true;
}

// Building state 4 (0x4975d0): keep shooting the ordered unit while in range.
bool World::buildingAttackUnit(Building& b) {
    if (b.enemyIndex == kNoIndex || b.enemySide < 0 || b.enemySide >= kMaxSides) return false;
    const Side& S = sides_[b.enemySide];
    if (S.links[b.enemyIndex].kind != SlotKind::Unit) return false;
    const Unit& e = S.units[b.enemyIndex];
    if (e.state == UnitState::Dead) return false;
    if (!canBuildingAttackTile(b, e.blockFromX, e.blockFromY, (e.flags & unit_flags::kFlying) != 0)) return false;
    if (b.reloadDelay == 0) buildingFireAt(b, px16(e.posX), px16(e.posY), b.enemySide, b.enemyIndex, 1);
    return true;
}

// Building state 5 (0x4976b0).
bool World::buildingAttackBuilding(Building& b) {
    if (b.enemyIndex == kNoIndex || b.enemySide < 0 || b.enemySide >= kMaxSides) return false;
    const Side& S = sides_[b.enemySide];
    if (S.links[b.enemyIndex].kind != SlotKind::Building) return false;
    const Building& e = S.buildings[b.enemyIndex];
    if (e.state == 17) return false;
    int x = 0, y = 0;
    if (isBuildingInRange(b, e, x, y) && b.reloadDelay == 0)  // out of range: DebugFatal, stays
        buildingFireAt(b, x * 32 + 16, y * 32 + 16, b.enemySide, b.enemyIndex, 2);
    return true;
}

// FUN_0044dee0 / FUN_0044df60: free the footprint (drivable/buildable again unless the
// template has no concrete), clear walls, decrement the group count.
void World::removeBuildingFromMap(Building& b) {
    const auto& t = buildingTemplate(b);
    Side& S = sides_[b.side];
    --S.buildingsExistPerGroup[t.buildingGroup];
    const int x = div32(px16(b.posX)), y = div32(px16(b.posY) - t.artHeight);
    const bool restore = !(t.flags & kNoConcrete);
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1)
            if (t.tilesOccupiedAll & bit)
                if (std::uint32_t* f = linearCellFlags(x + c, y + r)) {
                    if (restore) *f |= kBuildOn;
                    *f &= ~kWall;
                }
    bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1)
            if (t.tilesOccupiedSolid & bit) {
                if (std::uint32_t* f = linearCellFlags(x + c, y + r)) {
                    *f &= 0xefffffefu;  // building + exit tile
                    if (restore) *f |= kDriveOn | kWalkOn | kBuildOn;
                }
                if (inBounds(x + c, y + r)) {
                    Cell& cl = cell(x + c, y + r);
                    if (cl.buildingSide == b.side && cl.buildingIndex == b.index) cl.buildingSide = cl.buildingIndex = -1;
                }
            }
    if (t.flags & kHasSkirt) removeBuildingSkirt(x, y, b.type);  // Map__RemoveBuildingSkirt 0x44f2f0
    // TODO(render): wall/turret frame update of the neighbours (FUN_0044e6d0).
}

// Skirt rows (Map__PlaceBuildingSkirt 0x44f1b0 / Map__RemoveBuildingSkirt 0x44f2f0): the two
// tile rows y + (artHeight >> 5) - 2 + {0, 1} (a byte, like the original), columns
// x .. x + (artWidth >> 5) - 1. Only 2- or 3-tile-wide art can have a skirt (the original
// DebugFatals otherwise; we do nothing). Cells are addressed with the unchecked linear index.
namespace {
constexpr std::uint16_t kSkirtDamagedConcrete[8] = {0x228, 0x229, 0x22a, 0x22b, 0x22c, 0x23c, 0x23d, 0x23e};  // 0x4e0af0
}

void World::placeBuildingSkirt(int x, int y, int type) {
    const auto& t = rules_.templates->bin.buildings[type];
    const int w = t.artWidth >> 5;
    if (w != 2 && w != 3) return;
    const std::uint16_t plain = w == 2 ? 0x269 : 0x263, onConcrete = w == 2 ? 0x289 : 0x283;
    const int row0 = static_cast<std::uint8_t>(y + (t.artHeight >> 5) - 2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < w; ++c) {
            const long i = static_cast<long>(row0 + r) * width_ + (x & 0xff) + c;
            if (i < 0 || i >= static_cast<long>(cells_.size())) continue;
            Cell& cl = cells_[static_cast<std::size_t>(i)];
            cl.tile = static_cast<std::uint16_t>(((cl.flags & kConcrete) ? onConcrete : plain) + c + r * 0x14);
        }
}

void World::removeBuildingSkirt(int x, int y, int type) {
    const auto& t = rules_.templates->bin.buildings[type];
    const int w = t.artWidth >> 5;
    if (w != 2 && w != 3) return;
    const int row0 = static_cast<std::uint8_t>(y + (t.artHeight >> 5) - 2);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < w; ++c) {
            const long i = static_cast<long>(row0 + r) * width_ + (x & 0xff) + c;
            Cell* cl = (i >= 0 && i < static_cast<long>(cells_.size())) ? &cells_[static_cast<std::size_t>(i)] : nullptr;
            if (!cl || !(cl->flags & kConcrete)) {
                // rand() is drawn even when the cell lies outside our grid (the original's grid is
                // a fixed 128x128 array and always has a cell there).
                const std::uint16_t tile = kSkirtDamagedConcrete[static_cast<std::uint32_t>(rng_.next()) & 7];
                if (cl) cl->tile = tile;
            } else {
                cl->tile = 0x28b;
            }
        }
}

// 0x46d9a0: a destroyed primary building hands the role to the next building of its group.
void World::reassignPrimary(Side& S, const Building& b) {
    const int group = buildingTemplate(b).buildingGroup;
    for (int k = 0; k < static_cast<int>(S.primaryBuilding.size()); ++k) {
        if (S.primaryBuilding[k] != b.index) continue;
        int next = kNoIndex;
        for (int i = S.firstBuilding; i != -1; i = S.nextObject(i))
            if (S.buildings[i].index != b.index && buildingTemplate(S.buildings[i]).buildingGroup == group) {
                next = i;
                break;
            }
        S.primaryBuilding[k] = next;
        if (next != kNoIndex) S.buildings[next].flags |= kPrimary;
        if (k == 7 && next == kNoIndex) starportLost(S);  // no starport left: refund, drop frigate orders
    }
}

// UpdateBuilding 0x49b7f0. Returns false when the building is to be freed.
bool World::updateBuilding(Building& b) {
    const auto& t = buildingTemplate(b);
    const auto& T = rules_.templates->bin;
    std::uint32_t f = b.flags;
    if ((f & (kBuildup | kSelling)) == 0) {
        // Animation overlay (permanent / once) runs while above half health.
        if ((f & (kAnimPermanent | kAnimOnce)) && (f & kHasAnimation) &&
            static_cast<std::int32_t>(t.hitPoints >> 1) <= b.health) {
            b.animCounter = static_cast<std::uint8_t>(b.animCounter + 1);
            const int speed = t.animationSpeed ? t.animationSpeed : 1;  // division by zero otherwise
            if (static_cast<int>(T.buildingAnimationFrames[t.buildingAnimation % formats::kMaxBuildingTypes]) <=
                b.animCounter / speed) {
                b.animCounter = 0;
                if (f & kAnimOnce) b.flags = f & ~kAnimOnce;
            }
        }
    } else if (!(f & kBuildup)) {
        // Selling: counts down; the construction yard is sold at 60.
        const std::uint8_t c = b.buildupCounter;
        if (c == 1 || (t.specialBehavior == building_behaviour::kConstructionYard && c == 60)) {
            if (b.state != 17) b.state = 17;
            b.flags |= kSold;
        } else {
            b.buildupCounter = static_cast<std::uint8_t>(c - 1);
        }
    } else {
        b.buildupCounter = static_cast<std::uint8_t>(b.buildupCounter + 1);
        if (static_cast<int>(T.buildupArtFrames[t.buildupArt % formats::kMaxBuildingTypes]) <= b.buildupCounter / 3)
            b.flags = f & ~(kBuildup | kSelling);
    }
    if (b.reloadDelay != 0) --b.reloadDelay;
    if (b.enemyIndex != kNoIndex) {
        bool keep = false;
        if (b.enemySide >= 0 && b.enemySide < kMaxSides) {
            const Side& es = sides_[b.enemySide];
            const auto kind = es.links[b.enemyIndex].kind;
            if (kind == SlotKind::Unit) {
                const Unit& e = es.units[b.enemyIndex];
                keep = e.state != UnitState::Dead && !(e.flags & 0x200004) &&
                       (!(e.flags & unit_flags::kCarried) || (e.flags & unit_flags::kFlying));
            } else if (kind == SlotKind::Building) {
                keep = es.buildings[b.enemyIndex].state != 17;
            }
        }
        if (!keep) b.enemyIndex = kNoIndex;
    }
    f = b.flags;
    if (!(f & kInfantryReleased) && (f & kSelling) && b.buildupCounter / 3 == t.buildupFramesToShow)
        b.flags = f | kReleaseInfantry;
    if ((b.flags & kReleaseInfantry) && !(b.flags & kInfantryReleased)) {
        releaseSurvivors(b);
        b.flags &= ~kReleaseInfantry;
    }
    if (static_cast<std::int32_t>(t.hitPoints >> 1) < b.health && (b.flags & kDecays) && (gameTicks_ & 0xf) == 0)
        --b.health;  // decay without full concrete, down to half health
    f = b.flags;
    if ((f & kSold) && !(f & kWasSold)) {
        // FUN_0044b1d0: dead state; next tick it leaves the map and ModelUpdate refunds it.
        if (b.state != 17) b.state = 17;
        b.flags |= kWasSold;
        return true;
    }
    if (((gameTicks_ ^ static_cast<std::uint32_t>(b.index & 0xffff)) & 7) == 0 && b.lastAttacked < 0xff) ++b.lastAttacked;
    if (f & kWasAttacked) {
        b.lastAttacked = 0;
        b.flags = f & ~kWasAttacked;
    }
    // Damage smoke from the BUILEXP.BIN points while below half health.
    if (b.smokeTimer != 0 && (t.flags & kSelectable) && b.state != 17 && !(b.flags & (kBuildup | kSelling))) {
        const std::vector<std::uint8_t>* smoke = rules_.buildingSmoke;
        const std::size_t base = static_cast<std::size_t>(b.type) * 0x21;
        const bool have = smoke && smoke->size() >= base + 0x21;
        const int count = have ? (*smoke)[base] : 0;
        for (int i = 0; i < count && i < 8; ++i) {
            const ArtSize img = buildingImageSize(b);
            const int period = (*smoke)[base + 0x19 + i];
            const std::uint32_t r = static_cast<std::uint32_t>(rng_.next());
            if (period != 0 && (((r & 1) + gameTicks_) % static_cast<std::uint32_t>(period)) == 0 && (b.smokeBits & (1u << i))) {
                const int ex = static_cast<std::int8_t>((*smoke)[base + 1 + i]) + px16(b.posX);
                const int ey = static_cast<std::int8_t>((*smoke)[base + 9 + i]) + px16(b.posY) - img.height;
                addExplosion(b.side, (*smoke)[base + 0x11 + i], ex, ey, 0);
            }
            const std::uint32_t r2 = static_cast<std::uint32_t>(rng_.next());
            if (((r2 & 0x7f) == 0 && b.smokeTimer < 0x32) || static_cast<std::int32_t>(t.hitPoints >> 1) < b.health)
                b.smokeBits &= ~(1u << i);
        }
        if ((gameTicks_ & 3) == 0) --b.smokeTimer;
    }
    bool keepState = true;
    switch (b.state) {
    case 2:
        if (t.primaryWeapon == -1) return true;
        if (static_cast<std::uint32_t>(b.index & 7) != (gameTicks_ & 7) && b.enemyIndex == kNoIndex) return true;
        buildingAutoTarget(b);
        return true;
    case 4: keepState = buildingAttackUnit(b); break;
    case 5: keepState = buildingAttackBuilding(b); break;
    case 6:
        if (canBuildingAttackTile(b, b.attackTileX, b.attackTileY, false) && b.reloadDelay == 0)
            buildingFireAt(b, b.attackTileX * 32 + 16, b.attackTileY * 32 + 16, 0xff, kNoIndex, 0);
        return true;
    case 17: {
        if (!(b.flags & kDontRemoveTiles) && b.deadTimer == 0x19) releaseSurvivors(b);
        if (b.deadTimer != 0) {
            --b.deadTimer;
            return true;
        }
        // Exploding barrels.
        if (t.buildingGroup == static_cast<std::uint8_t>(T.groupIds[8]) && T.groupIds[22] != -1)
            damageTiles(px16(b.posX), px16(b.posY), 0, static_cast<std::uint8_t>(T.groupIds[22]), b.side, kNoIndex, false);
        reassignPrimary(sides_[b.side], b);
        if (!(b.flags & kDontRemoveTiles)) removeBuildingFromMap(b);
        return false;
    }
    default: return true;  // DebugFatal "Invalid building state"
    }
    if (!keepState && b.state != 17) b.state = 2;
    return true;
}

}  // namespace d2k::game
