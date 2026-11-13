// Combat: targeting, firing, attack states, damage and death (re/notes/combat.md).
// References: UnitAutoTarget 0x496480, FindNearestEnemy 0x4a4900, FindNearestEnemyBuilding
// 0x4a4fd0, CanUnitAttackTile 0x497270, CanBuildingAttackTile 0x497140, InRange 0x4970f0,
// NewBestBullet 0x49e9d0, FireAt 0x495b90, AttackUnit 0x496b00, AttackBuilding 0x496d10,
// AttackTile 0x496ff0, DamageTiles 0x49d2e0, DestroyUnit 0x49cde0, DestroyBuilding 0x49ca10,
// ReleaseSurvivors 0x4977a0, GetRandomAdjacentTile 0x4979a0.
// Arithmetic follows the original: positions are truncated toward zero when converted to
// pixels/tiles (Ghidra's `(v + (v >> 31 & 0xffff)) >> 16` idiom), u16 pixel coordinates wrap.

#include "game/geometry.hpp"
#include "game/tables.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
inline int px16(std::int32_t v) { return v / 65536; }  // 16.16 -> pixels, toward zero
inline int div32(int v) { return v / 32; }             // pixels -> tiles, toward zero

// InRange 0x4970f0: squared distance of u16 pixel points against a u16 range.
bool inRange(int x1, int y1, int x2, int y2, std::uint32_t range) {
    const int dx = (x1 & 0xffff) - (x2 & 0xffff), dy = (y1 & 0xffff) - (y2 & 0xffff);
    const std::uint32_t r = range & 0xffff;
    return dx * dx + dy * dy <= static_cast<int>(r * r);
}

// Square spiral used by the target searches (tables 0x4eb2d8/0x4eb2dc/0x4eb2e0): W, S, E, N
// legs whose length grows by one every second leg. Coordinates are bytes (wrap at 256).
struct Spiral {
    std::uint8_t x, y;
    int dir = 0;
    std::uint8_t count = 1, leg = 1;
    Spiral(int sx, int sy) : x(static_cast<std::uint8_t>(sx)), y(static_cast<std::uint8_t>(sy)) {}
    void step() {
        static constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, 1, 0, -1}, kInc[4] = {0, 1, 0, 1};
        x = static_cast<std::uint8_t>(x + kDx[dir]);
        y = static_cast<std::uint8_t>(y + kDy[dir]);
        if (--count == 0) {
            leg = static_cast<std::uint8_t>(leg + kInc[dir]);
            count = leg;
            dir = (dir + 1) & 3;
        }
    }
};

// Number of spiral legs for a pixel range: (range * 2) / 32 + 2.
int spiralLegs(std::uint32_t range) { return static_cast<int>(range * 2) / 32 + 2; }
}  // namespace

// ---------------------------------------------------------------------------------------
// Weapon / warhead data

int World::warheadVersus(int w, int armour) const {
    if (!rules_.armour) return 100;
    const int wh = weapon(w).warhead;
    if (wh < 0 || wh >= formats::kOrigWarheads || armour < 0 || armour >= formats::kOrigArmourTypes) return 0;
    return rules_.armour->warheads[wh].versusArmour[armour];
}

std::uint32_t World::warheadRadius(int w) const {
    if (!rules_.armour) return 0;
    const int wh = weapon(w).warhead;
    return wh < formats::kOrigWarheads ? rules_.armour->warheads[wh].radius : 0;
}

ArtSize World::buildingFrameSize(int art, int frame) const {
    if (const auto* a = rules_.buildingArt) {
        const std::size_t i = static_cast<std::size_t>(art) * 32 + static_cast<std::size_t>(frame);
        if (art >= 0 && frame >= 0 && i < a->normal.size() && (a->normal[i].width || a->normal[i].height))
            return a->normal[i];
        if (art >= 0 && static_cast<std::size_t>(art) < a->base.size()) return a->base[static_cast<std::size_t>(art)];
    }
    return {};
}

// The image DestroyBuilding/UpdateBuilding take sizes from: normal frame 0, else the base.
ArtSize World::buildingImageSize(const Building& b) const {
    const auto& t = buildingTemplate(b);
    ArtSize s = buildingFrameSize(t.buildingArt, 0);
    if (!s.width && !s.height) s = {t.artHeight, t.artWidth};  // fallback: template art box
    return s;
}

// ---------------------------------------------------------------------------------------
// Tile helpers

// ClosestBuildingTile 0x49d170: the solid tile with the smallest Manhattan distance (first
// in row-major order on ties). The origin is the top-left tile of the art box.
int World::closestBuildingTileManhattan(const Building& b, int x, int y, int& ox, int& oy) const {
    const auto& t = buildingTemplate(b);
    const int x0 = div32(px16(b.posX)), y0 = div32(px16(b.posY) - t.artHeight);
    int best = 0x800;
    ox = x0;
    oy = y0;  // the original DebugFatals when the building has no solid tile
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(t.tilesOccupiedSolid & bit)) continue;
            const int d = std::abs(y0 + r - y) + std::abs(x0 + c - x);
            if (d < best) {
                ox = x0 + c;
                oy = y0 + r;
                best = d;
            }
        }
    return best;
}

// 0x497390: the solid tile closest (squared Euclidean) to tile (x, y). Quirk: once a tile at
// distance 0 is found, every later solid tile replaces it (the "best == 0" initial test).
void World::closestBuildingTile(int x, int y, const Building& b, int& ox, int& oy) const {
    const auto& t = buildingTemplate(b);
    const int x0 = div32(px16(b.posX) + 16), y0 = div32(px16(b.posY) - t.artHeight + 16);
    std::uint32_t best = 0, bit = 1;
    ox = x0;
    oy = y0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(t.tilesOccupiedSolid & bit)) continue;
            const std::uint32_t d =
                static_cast<std::uint32_t>((x0 + c - x) * (x0 + c - x) + (y0 + r - y) * (y0 + r - y));
            if (d < best || best == 0) {
                ox = x0 + c;
                oy = y0 + r;
                best = d;
            }
        }
}

// Weapon range selection shared by CanUnitAttackTile / CanBuildingAttackTile.
static bool pickRange(const formats::TemplatesBin& T, int p, int s, bool air, std::uint32_t& range) {
    const std::uint32_t rp = T.weapons[p].range;
    const bool pAA = T.weapons[p].antiAircraft != 0;
    bool sAA = false;
    std::uint32_t rs = 0;
    if (s != -1) {
        sAA = T.weapons[s].antiAircraft != 0;
        rs = T.weapons[s].range;
    }
    if (!air) {
        range = rs < rp ? rp : rs;
    } else if (!pAA) {
        if (!sAA) return false;
        range = rs;
    } else {
        range = (!sAA || rs <= rp) ? rp : rs;
    }
    return true;
}

// CanUnitAttackTile 0x497270: from the centre of BlockFrom to the centre of (x, y).
bool World::canUnitAttackTile(const Unit& u, int x, int y, bool air) const {
    const auto& t = unitTemplate(u);
    if (t.primaryWeapon == -1) return false;
    std::uint32_t range = 0;
    if (!pickRange(rules_.templates->bin, t.primaryWeapon, t.secondaryWeapon, air, range)) return false;
    return inRange(u.blockFromX * 32 + 16, u.blockFromY * 32 + 16, (x & 0xff) * 32 + 16, (y & 0xff) * 32 + 16, range);
}

// CanBuildingAttackTile 0x497140: from the centre of the art box's top-left tile.
bool World::canBuildingAttackTile(const Building& b, int x, int y, bool air) const {
    const auto& t = buildingTemplate(b);
    if (t.primaryWeapon == -1) return false;
    std::uint32_t range = 0;
    if (!pickRange(rules_.templates->bin, t.primaryWeapon, t.secondaryWeapon, air, range)) return false;
    return inRange(px16(b.posX) + 16, px16(b.posY) - t.artHeight + 16, (x & 0xff) * 32 + 16, (y & 0xff) * 32 + 16,
                   range);
}

// 0x497490 (IsBuildingInRange_0's worker): closest target tile from the shooter's top-left
// tile, then CanBuildingAttackTile.
bool World::isBuildingInRange(const Building& b, const Building& target, int& ox, int& oy) const {
    const auto& t = buildingTemplate(b);
    closestBuildingTile(div32(px16(b.posX) + 16), div32(px16(b.posY) - t.artHeight + 16), target, ox, oy);
    return canBuildingAttackTile(b, ox, oy, false);
}

bool World::unitTileOccupiedByBuilding(const Unit& u) const {  // 0x49d280
    if (cell(u.blockFromX, u.blockFromY).flags & kBuilding) return true;
    return (u.flags & kBlockToMarked) && (cell(u.blockToX, u.blockToY).flags & kBuilding);
}

// ---------------------------------------------------------------------------------------
// Target searches

// FindNearestEnemy 0x4a4900. Spirals out from the tile of (px, py) (the start tile itself is
// skipped) looking at cells with an enemy owner; returns the unit's pixel position. With
// fixedRange == 0 the range is the longer weapon range and, after the ground search, flyers
// (anti-aircraft weapons only) and wandering sandworms are searched too.
bool World::findNearestEnemy(int px, int py, int side, std::uint32_t fixedRange, int& ox, int& oy, int& oSide,
                             int& oIndex, int primary, int secondary, bool includeCloaked, bool air,
                             bool worms) const {
    const auto& W = rules_.templates->bin.weapons;
    oIndex = kNoIndex;
    if (primary < 0) return false;  // DebugFatal "Primary bullet invalid"
    std::uint32_t range = fixedRange;
    if (fixedRange == 0) {
        range = W[primary].range;
        if (secondary != -1 && range < W[secondary].range) range = W[secondary].range;
    }
    const int sx = div32(px), sy = div32(py);
    int legs = spiralLegs(range);
    if (legs > 1) {
        Spiral sp(sx, sy);
        do {
            sp.step();
            if (sp.x < width_ && sp.y < height_) {
                const std::uint32_t f = cell(sp.x, sp.y).flags;
                if ((f & kAnyUnit) && diplomacy(side, static_cast<int>(f & kOwnerMask)) == 1) {
                    const int cx = sp.x * 32 + 16, cy = sp.y * 32 + 16;
                    int us = 0, ui = 0;
                    const Unit* e = getUnitOnTile(cx, cy, us, ui, !includeCloaked);
                    if (e && e->state != UnitState::Dead && inRange(px, py, cx, cy, range)) {
                        ox = px16(e->posX);
                        oy = px16(e->posY);
                        oIndex = ui;
                        oSide = us;
                        return true;
                    }
                }
            }
        } while (sp.leg < legs);
    }
    if (fixedRange != 0) return false;
    if (air) {
        const bool pAA = W[primary].antiAircraft != 0;
        const bool sAA = secondary != -1 && W[secondary].antiAircraft != 0;
        if (pAA || sAA) {
            if (secondary == -1 || !pAA || !sAA) {
                if (pAA) range = W[primary].range;
            } else {
                range = W[primary].range;
                if (range <= W[secondary].range) range = W[secondary].range;
            }
            const int frigate = rules_.templates->bin.groupIds[14];
            legs = spiralLegs(range);
            if (legs > 1) {
                Spiral sp(sx, sy);
                do {
                    sp.step();
                    if (sp.x < width_ && sp.y < height_ && (cell(sp.x, sp.y).flags & kFlyer)) {
                        for (int o = 0; o < kMaxSides; ++o) {
                            if (diplomacy(side, o) != 1) continue;
                            const Side& s = sides_[o];
                            for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
                                const Unit& e = s.units[i];
                                if (!(e.flags & kFlying) || e.type == static_cast<std::uint8_t>(frigate)) continue;
                                if (e.blockToX != sp.x && e.blockFromX != sp.x) continue;
                                if (e.blockFromY != sp.y && e.blockToY != sp.y) continue;
                                if (!inRange(px, py, sp.x * 32 + 16, sp.y * 32 + 16, range)) continue;
                                ox = px16(e.posX);
                                oy = px16(e.posY);
                                oIndex = e.index;
                                oSide = o;
                                return true;
                            }
                        }
                    }
                } while (sp.leg < legs);
            }
        }
    }
    if (worms) {
        range = W[primary].range;
        if (secondary != -1 && range <= W[secondary].range) range = W[secondary].range;
        legs = spiralLegs(range);
        if (legs > 1) {
            Spiral sp(sx, sy);
            do {
                sp.step();
                if (sp.x < width_ && sp.y < height_ && (cell(sp.x, sp.y).flags & kWorm)) {
                    for (int o = 0; o < kMaxSides; ++o) {  // no diplomacy test: worms belong to nobody
                        const Side& s = sides_[o];
                        for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
                            const Unit& e = s.units[i];
                            if (e.state != UnitState::Wandering || (e.flags & kCloaked)) continue;
                            if (e.blockFromX != sp.x || e.blockFromY != sp.y) continue;
                            if (!inRange(px, py, sp.x * 32 + 16, sp.y * 32 + 16, range)) continue;
                            ox = px16(e.posX);
                            oy = px16(e.posY);
                            oIndex = e.index;
                            oSide = o;
                            return true;
                        }
                    }
                }
            } while (sp.leg < legs);
        }
    }
    return false;
}

// FindNearestEnemyBuilding 0x4a4fd0: same spiral but the start tile is tested first; walls
// never, and only turrets when `turretsOnly`. Returns the tile centre in pixels.
bool World::findNearestEnemyBuilding(int px, int py, int side, int& ox, int& oy, int& oSide, int& oIndex,
                                     int primary, int secondary, bool turretsOnly) const {
    const auto& W = rules_.templates->bin.weapons;
    oIndex = kNoIndex;
    oSide = 0;
    if (primary < 0) return false;
    std::uint32_t range = secondary == -1 ? 0 : W[secondary].range;
    if (range <= W[primary].range) range = W[primary].range;
    const int legs = spiralLegs(range);
    if (legs < 2) return false;
    Spiral sp(div32(px), div32(py));
    do {
        if (sp.x < width_ && sp.y < height_) {
            const std::uint32_t f = cell(sp.x, sp.y).flags;
            int bs = 0, bi = 0;
            const Building* b = nullptr;
            if ((f & kBuilding) && diplomacy(side, static_cast<int>(f & kOwnerMask)) == 1 &&
                (b = buildingAt(sp.x, sp.y, bs, bi)) != nullptr) {
                const int beh = buildingTemplate(*b).specialBehavior;
                if (beh != building_behaviour::kWall && (!turretsOnly || beh == building_behaviour::kTurret)) {
                    const int cx = sp.x * 32 + 16, cy = sp.y * 32 + 16;
                    if (inRange(px, py, cx, cy, range)) {
                        ox = cx & 0xffff;
                        oy = cy & 0xffff;
                        oIndex = bi;
                        oSide = bs;
                        return true;
                    }
                }
            }
        }
        sp.step();
    } while (sp.leg < legs);
    return false;
}

// NewBestBullet 0x49e9d0: anti-aircraft weapon against flyers; otherwise the secondary
// weapon unless the primary is in range and strictly more effective (warhead % x damage
// against the target's armour). targetKind 1 = unit, 2 = building.
int World::newBestBullet(int p, int s, std::uint32_t dist2, int targetSide, int targetIndex, int targetKind) const {
    const auto& W = rules_.templates->bin.weapons;
    int armour = 0;
    bool flying = false;
    if (targetIndex != kNoIndex && targetIndex >= 0 && targetIndex < kObjectsPerSide && targetSide >= 0 &&
        targetSide < kMaxSides) {
        const Side& ts = sides_[targetSide];
        if (targetKind == 1) {
            // The original reads the slot as a unit whatever it holds; ours keeps the last unit data.
            const Unit& e = ts.units[targetIndex];
            armour = rules_.templates->bin.units[e.type].armorType;
            flying = (e.flags & kFlying) != 0;
        } else if (targetKind == 2) {
            // Quirk: buildings pass kind 2 even when their target is a unit; the slot is then read
            // as a building, whose type byte (+0x34) overlaps the low byte of the unit's PosX.
            const int type = ts.links[targetIndex].kind == SlotKind::Unit
                                 ? static_cast<int>(static_cast<std::uint32_t>(ts.units[targetIndex].posX) & 0xff)
                                 : ts.buildings[targetIndex].type;
            armour = rules_.templates->bin.buildings[type < formats::kMaxBuildingTypes ? type : 0].armorType;
        }
    }
    if (flying) {
        if (!W[p].antiAircraft && (s < 0 || !W[s].antiAircraft)) return p;  // DebugFatal "no AA weapon"
        return W[p].antiAircraft ? p : s;
    }
    if (s == -1) return p;
    auto eff = [&](int w) {
        return static_cast<std::uint32_t>(warheadVersus(w, armour)) * W[w].damage;
    };
    const std::uint32_t rp = W[p].range, rs = W[s].range;
    if (rp < rs) {
        if (dist2 <= rp * rp && eff(s) < eff(p)) return p;
        return s;
    }
    if (dist2 <= rs * rs) return eff(s) < eff(p) ? p : s;
    return p;
}

bool World::unitDecloak(Unit& u) {  // 0x49f5c0
    if ((u.flags & kCloaked) && u.stealthTimer == 0) {
        u.stealthTimer = 0x16;
        u.flags &= ~kCloaked;
        return true;
    }
    return false;
}

// FUN_0049f580: start cloaking (stealth units only). The 0x16-tick fade timer is restarted, or
// mirrored when a decloak fade is still running.
bool World::unitCloak(Unit& u) {
    if (!(u.flags & kStealth)) return false;
    u.stealthTimer = u.stealthTimer == 0 ? 0x16 : static_cast<std::uint8_t>(0x16 - u.stealthTimer);
    u.flags |= kCloaked;
    return true;
}

// FUN_0049f750: is there an object of another side within `r` tiles that would reveal a
// stealth unit? Only infantry (cell spot bits) and armed buildings on wall-flagged cells count
// (vehicles do not). The scan covers [x-r, min(x+r+1, W-1)) x [y-r, min(y+r+1, H-1)): the
// last row/column of the map is never scanned (original bounds).
bool World::enemyNearStealth(int x, int y, int side, int r) const {
    const int x0 = std::max(x - r, 0), y0 = std::max(y - r, 0);
    const int x1 = std::min(x + 1 + r, width_ - 1), y1 = std::min(y + 1 + r, height_ - 1);
    bool found = false;
    for (int cy = y0; cy < y1; ++cy)
        for (int cx = x0; cx < x1; ++cx) {
            const std::uint32_t f = cell(cx, cy).flags;
            if (static_cast<int>(f & kOwnerMask) == (side & 0xff)) continue;
            if (f & kInfantryMask) {
                found = true;
            } else if (f & kWall) {
                int bs = 0, bi = 0;
                const Building* b = buildingAt(cx, cy, bs, bi);
                if (b && buildingTemplate(*b).primaryWeapon != -1) found = true;
            }
        }
    return found;
}

// Unit_UpdateStealth 0x49f610 (UpdateUnit prologue): stealth units (flag 0x10) and units with
// a running fade timer (+0x31). Decloak when attacked recently (< 5), self-destructing /
// thumping, an enemy is adjacent, or below 1/3 health; cloak again once it has not fired for
// (reload >> 3) + 1 checks (not while awaiting/being airlifted).
void World::updateStealth(Unit& u) {
    const std::uint32_t f = u.flags;
    if (!(f & kStealth) && u.stealthTimer == 0) {
        u.flags = f & ~kCloaked;
        return;
    }
    if ((f & kCloaked) && u.side != mySide_) u.selected = false;
    if (u.stealthTimer != 0) --u.stealthTimer;
    if (u.stealthTimer != 0) return;
    const auto& t = unitTemplate(u);
    bool revealed = false;
    if (!(f & kCloaked)) {
        revealed = enemyNearStealth(u.blockFromX, u.blockFromY, u.side, 1);
    } else if (u.lastAttacked < 5 || u.state == UnitState::Thumpering || u.state == UnitState::Selfdestruct) {
        unitDecloak(u);
        revealed = true;
    } else if (enemyNearStealth(u.blockFromX, u.blockFromY, u.side, 1)) {
        unitDecloak(u);
        revealed = true;
    }
    if (u.health < static_cast<std::int32_t>(t.hitPoints / 3) && unitIsHiddenStealth(u)) {
        unitDecloak(u);
        return;
    }
    if (!revealed && !(u.flags & kCloaked)) {
        const bool busy = u.lastAttacked < 5 || u.state == UnitState::Thumpering || u.state == UnitState::Selfdestruct;
        if (!busy && static_cast<std::uint8_t>((t.primaryWeaponLongDelay >> 3) + 1) < u.lastFired &&
            u.state != UnitState::AwaitingAirlift && u.state != UnitState::Airlifted)
            unitCloak(u);
    }
}

// ---------------------------------------------------------------------------------------
// Firing

// FireAt 0x495b90 (debug name "ShootTarget"). Turns the body (no turret) or the turret towards
// the target pixel; when it faces it: muzzle flash, barrel offset, weapon choice, distance-
// based inaccuracy (2 rand() calls), bullet, reload. targetKind 0 tile / 1 unit / 2 building;
// only unit targets make homing bullets.
void World::fireAt(Unit& u, int tx, int ty, int targetSide, int targetIndex, int targetKind) {
    const auto& t = unitTemplate(u);
    if (t.primaryWeapon == -1) return;  // DebugFatal "Unit %s told to shoot"
    std::uint32_t mx = static_cast<std::uint32_t>(px16(u.posX)), my = static_cast<std::uint32_t>(px16(u.posY));
    std::uint32_t utx = static_cast<std::uint32_t>(tx), uty = static_cast<std::uint32_t>(ty);
    if ((mx & 0xffff) == utx && (my & 0xffff) == uty) return;
    const int want = getFacing(static_cast<int>(mx & 0xffff), static_cast<int>(my & 0xffff), tx, ty);
    const int cur = t.primaryWeaponUseBarrel ? u.facingTurret : u.facing;
    if (cur != want) {
        if (!t.primaryWeaponUseBarrel) {
            rotateBody(u, want);
        } else {
            rotateTurret(u, want);
            if (u.flags & kFixedBarrel) {
                u.turnDelay = 0;
                rotateBody(u, want);
            }
        }
        return;
    }
    if (unitIsHiddenStealth(u)) {
        unitDecloak(u);
        return;
    }
    const int f = t.barrelArt == -1 ? u.facing : u.facingTurret;
    if (t.muzzleFlashExplosion != -1) {
        const int e = addExplosion(u.side, t.muzzleFlashExplosion, static_cast<int>(mx), static_cast<int>(my), 0);
        if (e >= 0) sides_[u.side].explosions[e].frame = f;  // directional muzzle flash
    }
    // Barrel offset: sin/cos table entry [f][offset * 32] = offset pixels in direction f.
    const int off = static_cast<std::uint8_t>(t.primaryWeaponShootOffset);
    mx += static_cast<std::uint32_t>(px16(sinTable(f, off * 32)));
    my -= static_cast<std::uint32_t>(px16(cosTable(f, off * 32)));
    const int dx = static_cast<int>(mx & 0xffff) - tx, dy = static_cast<int>(my & 0xffff) - ty;
    const std::uint32_t d2 = static_cast<std::uint32_t>(dx * dx + dy * dy);
    const std::uint16_t dist = static_cast<std::uint16_t>(static_cast<std::int64_t>(std::sqrt(static_cast<double>(d2))));
    const std::uint32_t distSq = static_cast<std::uint32_t>(dist) * dist;
    const int w = newBestBullet(t.primaryWeapon, t.secondaryWeapon, distSq, targetSide, targetIndex, targetKind);
    u.lastFired = 0;
    const std::uint32_t n = dist >> 5;
    if (n != 0) {
        utx = utx + static_cast<std::uint32_t>(rng_.next()) % (n * 8) - n * 4;
        uty = uty + static_cast<std::uint32_t>(rng_.next()) % (n * 8) - n * 4;
    }
    playSound(weapon(w).firingSound, static_cast<int>(mx >> 5), static_cast<int>(my >> 5));
    if (targetSide == mySide_) {
        // The shooter is revealed to the player it attacks: its BlockTo cell is cleared and
        // the edges around it rebuilt (UpdateShroudInRegion over BlockTo -2..+1).
        if (inBounds(u.blockToX, u.blockToY)) cell(u.blockToX, u.blockToY).shroud = 0;
        updateShroudInRegion(u.blockToX - 2, u.blockToY - 2, u.blockToX + 2, u.blockToY + 2);
    }
    if (targetKind != 1) {
        targetSide = -1;
        targetIndex = kNoIndex;
    }
    addBullet(u.side, w, 0, u.index, static_cast<int>(mx & 0xffff), static_cast<int>(my & 0xffff),
              static_cast<int>(utx & 0xffff), static_cast<int>(uty & 0xffff), targetIndex, targetSide);
    u.flags |= kFired;
    u.reloadDelay = t.primaryWeaponLongDelay;
    if (t.isInfantry) setInfantryAnim(u, 2);
}

// UnitAutoTarget 0x496480: keep shooting the current enemy while it is in range, otherwise
// pick the nearest enemy unit (incl. flyers/worms), else the nearest enemy turret. Returns
// true when reloading or when a target was engaged.
bool World::unitAutoTarget(Unit& u) {
    if (u.reloadDelay != 0) return true;
    const auto& t = unitTemplate(u);
    if (t.primaryWeapon == -1 && t.secondaryWeapon == -1) return false;
    if (u.enemyIndex != kNoIndex && u.enemySide != u.side && u.enemySide >= 0 && u.enemySide < kMaxSides) {
        const int es = u.enemySide, ei = u.enemyIndex;
        Side& s = sides_[es];
        if (s.links[ei].kind == SlotKind::Unit) {
            const Unit& e = s.units[ei];
            const int ex = px16(e.posX), ey = px16(e.posY);
            if (canUnitAttackTile(u, div32(ex), div32(ey), false)) {
                fireAt(u, ex, ey, es, ei, 1);
                return true;
            }
        } else if (s.links[ei].kind == SlotKind::Building &&
                   buildingTemplate(s.buildings[ei]).specialBehavior == building_behaviour::kTurret) {
            int cx = 0, cy = 0;
            closestBuildingTile(u.blockFromX, u.blockFromY, s.buildings[ei], cx, cy);
            if (canUnitAttackTile(u, cx, cy, false)) {
                fireAt(u, cx * 32 + 16, cy * 32 + 16, es, ei, 2);
                return true;
            }
        }
    }
    const int cx = u.blockFromX * 32 + 16, cy = u.blockFromY * 32 + 16;
    int x = 0, y = 0, fs = 0, fi = kNoIndex;
    if (findNearestEnemy(cx, cy, u.side, 0, x, y, fs, fi, t.primaryWeapon, t.secondaryWeapon, false, true, true)) {
        u.enemySide = fs;
        u.enemyIndex = fi;
        if (const Unit* e = unit(fs, fi)) {
            int s2 = 0, i2 = 0;
            if (unitTemplate(*e).specialBehavior == behaviour::kSandworm && getUnitOnTile(x, y, s2, i2, false))
                return true;  // never shoot a worm that is eating something
            fireAt(u, x, y, fs, fi, 1);
        }
        return true;
    }
    if (!findNearestEnemyBuilding(cx, cy, u.side, x, y, fs, fi, t.primaryWeapon, t.secondaryWeapon, true))
        return false;
    u.enemySide = fs;
    u.enemyIndex = fi;
    fireAt(u, x, y, fs, fi, 2);
    return true;
}

// ---------------------------------------------------------------------------------------
// Attack states

// AttackUnit 0x496b00 (states 4 and, from Guarding, 3): fire when the enemy's BlockFrom is in
// range and the unit stands on a tile centre, else move towards the enemy's BlockTo.
// Returns false when the target is gone and the unit is at rest.
bool World::attackUnit(Unit& u) {
    if (u.enemyIndex != kNoIndex && u.enemySide >= 0 && u.enemySide < kMaxSides) {
        Side& s = sides_[u.enemySide];
        Unit& e = s.units[u.enemyIndex];
        if (s.links[u.enemyIndex].kind == SlotKind::Unit && e.state != UnitState::Dead && e.blockToX < width_ &&
            e.blockToY < height_) {
            if (canUnitAttackTile(u, e.blockFromX, e.blockFromY, false) && u.posSteps == 0) {
                if (u.reloadDelay == 0) {
                    u.targetX = u.blockFromX;
                    u.targetY = u.blockFromY;
                    fireAt(u, px16(e.posX), px16(e.posY), u.enemySide, u.enemyIndex, 1);
                }
                return true;
            }
            u.targetX = e.blockToX;
            u.targetY = e.blockToY;
            moveUnit(u);
            return true;
        }
    }
    if (u.posSteps != 0) {
        moveUnit(u);
        return true;
    }
    return false;
}

// AttackBuilding 0x496d10 (state 5). Engineers/saboteurs walk up to the building and
// infiltrate it; everything else fires at the closest solid tile when in range.
bool World::attackBuilding(Unit& u) {
    if (u.enemyIndex != kNoIndex && u.enemySide >= 0 && u.enemySide < kMaxSides) {
        Side& s = sides_[u.enemySide];
        Building& b = s.buildings[u.enemyIndex];
        if (s.links[u.enemyIndex].kind == SlotKind::Building && b.state != 17) {
            const auto& t = unitTemplate(u);
            const std::uint32_t ux = static_cast<std::uint32_t>(px16(u.posX)),
                                uy = static_cast<std::uint32_t>(px16(u.posY));
            int x = 0, y = 0;
            if (t.specialBehavior == behaviour::kEngineer || t.specialBehavior == behaviour::kSaboteur) {
                // 0x4a47f0: a solid tile within one tile (Chebyshev) of the unit.
                const auto& bt = buildingTemplate(b);
                const int bx0 = div32(px16(b.posX)), by0 = div32(px16(b.posY) - bt.artHeight);
                const int utx = static_cast<std::int16_t>(ux >> 5), uty = static_cast<std::int16_t>(uy >> 5);
                bool adjacent = false;
                std::uint32_t bit = 1;
                for (int r = 0; r < 4 && !adjacent; ++r)
                    for (int c = 0; c < 4 && !adjacent; ++c, bit <<= 1)
                        if ((bt.tilesOccupiedSolid & bit) && std::abs(utx - bx0 - c) < 2 && std::abs(uty - by0 - r) < 2) {
                            x = bx0 + c;
                            y = by0 + r;
                            adjacent = true;
                        }
                if (adjacent && u.posSteps == 0) {
                    if ((b.flags & 0x100000c) ||
                        (t.specialBehavior == behaviour::kEngineer && !(b.flags & 0x400)) ||
                        (t.specialBehavior == behaviour::kSaboteur && !(b.flags & 0x200))) {
                        unitAdjustState(u, UnitState::Idle);
                        return true;
                    }
                    // RemoveUnitTileData 0x44e120 (infantry: release the sub-cell spots).
                    cell(u.blockFromX, u.blockFromY).flags &= ~(0x20u << u.cellspotFrom);
                    if (u.flags & kBlockToMarked) cell(u.blockToX, u.blockToY).flags &= ~(0x20u << u.cellspotTo);
                    u.targetX = x;
                    u.targetY = y;
                    unitAdjustState(u, UnitState::Infiltrating);
                    u.flags |= kGone;
                    b.flags |= 0x1000000;  // infiltrated
                    unitDecloak(u);  // then UpdateUnit case 0x15 (updateInfiltrating)
                    return true;
                }
            } else {
                // CanUnitAttackBuilding 0x497550.
                closestBuildingTile(static_cast<int>(ux >> 5), static_cast<int>(uy >> 5), b, x, y);
                if (canUnitAttackTile(u, x, y, false) && u.posSteps == 0) {
                    if (u.reloadDelay == 0) {
                        u.targetY = u.blockToY;
                        u.targetX = u.blockToX;
                        fireAt(u, x * 32 + 16, y * 32 + 16, u.enemySide, u.enemyIndex, 2);
                    }
                    return true;
                }
            }
            closestBuildingTile(static_cast<int>(ux >> 5), static_cast<int>(uy >> 5), b, x, y);
            u.targetX = x;
            u.targetY = y;
            moveUnit(u);
            return true;
        }
    }
    if (u.posSteps != 0) {
        moveUnit(u);
        return true;
    }
    return false;
}

// AttackTile 0x496ff0 (state 6, and state 10 against a blocker). Returns false once the
// unit stands on the target tile.
bool World::attackTile(Unit& u) {
    if (u.posSteps != 0 || !canUnitAttackTile(u, u.targetX, u.targetY, false)) {
        moveUnit(u);
        return true;
    }
    if (u.reloadDelay != 0) return true;
    if (u.blockFromX == u.targetX && u.blockFromY == u.targetY) return false;
    fireAt(u, u.targetX * 32 + 16, u.targetY * 32 + 16, 0xff, kNoIndex, 0);
    return true;
}

// ---------------------------------------------------------------------------------------
// Damage

// FUN_0049fb40: a deviated unit whose old side still has buildings or units.
bool World::deviatedOwnerAlive(const Unit& u) const {
    if (!(u.flags & kDeviated) || u.oldSide > 7) return false;  // (> 7: DebugFatal)
    return buildingsExist_[u.oldSide] == 1 || unitsExist_[u.oldSide] == 1;
}

// ChangeUnitOwner 0x46d2e0: infantry cannot change sides (they die, no debris). A vehicle's cells
// are released, ModelAddUnit creates a unit of the same type for `toSide` on its BlockFrom, the
// first 0x7C bytes of the old unit are copied over (everything but the pool links), then the
// new unit forgets selection/attacker/enemy/group, moves to its BlockTo (unless keepState) and
// re-marks BlockTo; the old unit dies (flags 0x600000: not drawn, gone).
Unit* World::changeUnitOwner(int fromSide, int toSide, int index, bool keepState) {
    Unit& u = sides_[fromSide & 7].units[static_cast<std::size_t>(index) % kObjectsPerSide];
    if (u.flags & kGone) return nullptr;
    auto release = [](std::uint32_t& c) {
        c &= ~kVehicle;
        if (c & kInfantryMask) c = (c & ~kOwnerMask) | ((c >> kInfantryOwnerShift) & 7);
    };
    if (unitTemplate(u).isInfantry) {
        if (std::uint32_t* f = linearCellFlags(u.blockFromX, u.blockFromY)) *f &= ~(0x20u << (u.cellspotFrom & 0x1f));
        if (u.flags & kBlockToMarked)
            if (std::uint32_t* f = linearCellFlags(u.blockToX, u.blockToY)) *f &= ~(0x20u << (u.cellspotTo & 0x1f));
        u.flags |= 0x600000;
        unitAdjustState(u, UnitState::Dead);
        return nullptr;
    }
    if (std::uint32_t* f = linearCellFlags(u.blockFromX, u.blockFromY)) release(*f);
    if (u.flags & kBlockToMarked)
        if (std::uint32_t* f = linearCellFlags(u.blockToX, u.blockToY)) release(*f);
    const int ni = addUnit(toSide & 7, u.type, u.blockFromX & 0xff, u.blockFromY & 0xff, u.blockFromX & 0xff,
                           u.blockFromY & 0xff);
    if (ni < 0) return nullptr;
    Unit& n = sides_[toSide & 7].units[static_cast<std::size_t>(ni)];
    const bool alive = n.alive;
    const int nidx = n.index, nside = n.side;
    n = u;  // the first 0x7C bytes
    n.alive = alive;
    n.index = nidx;
    n.side = nside;
    n.selected = false;
    n.attackerSide = 0xff;
    n.attackerIndex = kNoIndex;
    n.lastAttacked = 0xff;
    n.enemySide = 0xff;
    n.enemyIndex = kNoIndex;
    u.flags |= 0x600000;
    n.oldState = UnitState::Idle;
    n.targetX = n.blockToX;
    n.targetY = n.blockToY;
    n.groupId = 0xff;
    if (!keepState) unitAdjustState(n, UnitState::Moving);
    if (n.flags & kBlockToMarked)
        if (std::uint32_t* f = linearCellFlags(n.blockToX, n.blockToY))
            *f = (*f & ~kOwnerMask) | static_cast<std::uint32_t>(toSide & 0xff) | kVehicle;
    n.flags |= kChangedOwner;
    unitAdjustState(u, UnitState::Dead);
    return &n;
}

// CaptureBuilding 0x46d6d0 (engineer): the building is removed from the map and re-added for
// `toSide` (ModelAddBuilding(.., 0, 1, 1)), its first 0x7C bytes copied over; stored spice of a
// refinery/silo share moves to the new owner; the old record dies without removing tiles; the
// old side's harvesters unloading in it change sides too.
bool World::captureBuilding(int fromSide, int toSide, int index) {
    Side& F = sides_[fromSide & 7];
    Side& Tt = sides_[toSide & 7];
    Building& b = F.buildings[static_cast<std::size_t>(index) % kObjectsPerSide];
    if (b.flags & (building_flags::kSelling | building_flags::kSold)) return false;
    const auto& bt = buildingTemplate(b);
    const int x = b.posX / 65536 / 32, y = (b.posY / 65536 - bt.artHeight) / 32;
    removeBuildingFromMap(b);
    const int ni = addBuilding(toSide & 7, b.type, x, y, false, true, true);
    if (ni < 0) return false;
    Building& n = Tt.buildings[static_cast<std::size_t>(ni)];
    const bool alive = n.alive;
    n = b;  // the first 0x7C bytes
    n.alive = alive;
    n.index = ni;
    n.side = toSide & 7;
    n.attackerSide = 0xff;
    n.attackerIndex = kNoIndex;
    if (n.selected) {  // DeselectAllForAllSides
        for (auto& sd : sides_) {
            for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) sd.units[i].selected = false;
            for (int i = sd.firstBuilding; i != -1; i = sd.nextObject(i)) sd.buildings[i].selected = false;
        }
        n.selected = true;
    }
    updateStorage();  // Model_UpdateCashAndStorage
    if (bt.specialBehavior == building_behaviour::kRefinery || bt.specialBehavior == building_behaviour::kSilo) {
        const std::uint32_t v = F.maxStorage ? static_cast<std::uint32_t>(F.spice) * 2000u / static_cast<std::uint32_t>(F.maxStorage) : 0;
        addSpice(toSide & 7, static_cast<std::int32_t>(v));
        spend(fromSide & 7, static_cast<std::int32_t>(v));
    }
    b.deadTimer = 1;
    if (b.state != 17) b.state = 17;  // Building__AdjustState
    b.flags |= 0x110000;              // kDontRemoveTiles | 0x100000
    if (n.flags & building_flags::kPrimary) n.flags &= ~0x90u;
    n.flags = (n.flags & 0xfefffffeu) | 0x2000000;
    for (int i = F.firstUnit; i != -1;) {
        const int next = F.nextObject(i);
        Unit& h = F.units[i];
        if (unitTemplate(h).specialBehavior == behaviour::kHarvester && h.state == UnitState::UnloadingSpice &&
            static_cast<std::uint16_t>(h.refineryIndex) == static_cast<std::uint16_t>(index)) {
            ++F.unitsLost;
            ++F.unitsLostPerType[h.type];
            if (Unit* c = changeUnitOwner(fromSide, toSide, h.index, true)) c->refineryIndex = ni;
        }
        i = next;
    }
    if (n.state != 17) n.state = 2;
    return true;
}

// UpdateUnit case 0x15 (state 23, INFILTRATING): the engineer/saboteur walks into the building
// (MoveUnitSimple); inside, an engineer captures it, a saboteur destroys it; the unit is gone.
void World::updateInfiltrating(Unit& u) {
    if (moveUnitSimple(u, false, false)) return;
    const int side = u.side, es = u.enemySide & 0xff;
    Building* b = (u.enemyIndex != kNoIndex && es < kMaxSides) ? building(es, u.enemyIndex) : nullptr;
    if (b && !(b->flags & (building_flags::kSelling | building_flags::kSold))) {
        if (unitTemplate(u).specialBehavior == behaviour::kEngineer) {
            const int btype = b->type;
            (void)btype;
            if (captureBuilding(es, side, u.enemyIndex)) {
                ++sides_[es].buildingsLost;
                ++sides_[side].buildingsKilled;
                ++sides_[side].buildingsBuilt;  // +0x24784 (a capture counts as built)
                // (+0x25418 per type and side statistics are not modelled)
                if (diplomacy(es, side) == 2) breakAlliance(es, side);
                if (side == mySide_) mentat(side, "CAPTURE", true);
                else if (es == mySide_) mentat(es, "BUILDINGLOST", true);  // + message "BuildingEngineered"
            }
        } else {
            ++sides_[es].buildingsLost;
            ++sides_[side].buildingsKilled;
            if (diplomacy(es, side) == 2) breakAlliance(es, side);
            destroyBuilding(*b, false);
            if (es == mySide_) mentat(es, "BUILDINGLOST", true);
        }
    }
    u.flags |= kNotDrawn;
    setInfantryAnim(u, 0);
    ++sides_[side].unitsLost;
    ++sides_[side].unitsLostPerType[u.type];
    destroyUnit(u);
}

void World::breakAlliance(int victimSide, int attackerSide) {  // 0x49f870
    setDiplomacy(victimSide, attackerSide, 1);
    setDiplomacy(attackerSide, victimSide, 1);
    // (message "NeutralBecomeHostile%d" when the attacker is the player)
}

// DamageTiles 0x49d2e0. Damage = trunc(base * (R - d) / R) with base = warhead% * damage / 100
// and d the pixel distance (from the closest solid tile centre for buildings). Bullets above
// 6 px (z > 0x60000) only hit flyers. Sides 0..7 in order, buildings before units.
void World::damageTiles(int px, int py, std::int32_t z, int w, int side, int firer, bool deviate) {
    const bool high = static_cast<std::uint32_t>(z) > 0x60000;
    if (!high) {  // a low hit on a growing spice bloom (stage 8..10) bursts it, no damage
        const int cx = static_cast<int>(static_cast<std::uint32_t>(px) >> 5), cy = static_cast<int>(static_cast<std::uint32_t>(py) >> 5);
        int ci = 0xff;
        const int ct = crateTypeAt(cx, cy, ci);  // FUN_0044e970
        if (ct == 8 || ct == 9 || ct == 10) {
            spiceMound(cx, cy, ct - 4);
            recycleCrate(ci);
            return;
        }
    }
    if (w < 0 || w >= formats::kMaxWeapons) return;  // DebugFatal "Invalid bulletType"
    const auto& T = rules_.templates->bin;
    const int spiceWeapon = T.groupIds[50];
    const std::uint32_t R = warheadRadius(w);
    const int tx = static_cast<int>(static_cast<std::uint32_t>(px) >> 5), ty = static_cast<int>(static_cast<std::uint32_t>(py) >> 5);
    if (!inBounds(tx, ty)) return;  // DebugFatal "out of range"; the original carries on
    const int attacker = static_cast<std::int8_t>(side);
    const int sideByte = side & 0xff;
    const double r = static_cast<double>(R);
    auto amount = [&](int armour, int dx, int dy, std::uint32_t& out) {
        if (std::abs(dx) >= static_cast<int>(R) || std::abs(dy) >= static_cast<int>(R)) return false;
        const double d = std::sqrt(static_cast<double>(dx * dx) + static_cast<double>(dy * dy));
        if (!(d < r)) return false;
        const std::uint32_t base = static_cast<std::uint32_t>(warheadVersus(w, armour)) * weapon(w).damage / 100;
        out = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::trunc(static_cast<double>(base) * ((r - d) / r))));
        return true;
    };
    for (int vs = 0; vs < kMaxSides; ++vs) {
        Side& S = sides_[vs];
        if (!high) {
            for (int i = S.firstBuilding; i != -1; i = S.nextObject(i)) {
                Building& b = S.buildings[i];
                if (b.state == 17 || (b.flags & 6)) continue;
                const auto& bt = buildingTemplate(b);
                int cx = 0, cy = 0;
                closestBuildingTileManhattan(b, tx, ty, cx, cy);
                std::uint32_t dmg = 0;
                if (!amount(bt.armorType, px - cx * 32 - 16, py - cy * 32 - 16, dmg)) continue;
                if (b.side != attacker && !(b.flags & 6)) {
                    b.flags |= building_flags::kWasAttacked;
                    if (vs == mySide_) mentat(vs, "UNDERATTACK", true, 300);
                }
                if (dmg < static_cast<std::uint32_t>(b.health) || b.deadTimer != 0) {
                    if (attacker != -1 && w != spiceWeapon && diplomacy(vs, sideByte) == 2) breakAlliance(vs, sideByte);
                    if (b.attackerIndex == kNoIndex && w != spiceWeapon && sideByte != vs) {
                        b.attackerSide = static_cast<std::uint8_t>(attacker);
                        b.attackerIndex = firer;
                    }
                    std::int32_t h = static_cast<std::int32_t>(static_cast<std::uint32_t>(b.health) - dmg);
                    if (h < 1) h = 0;
                    b.health = h;
                    if (h < static_cast<std::int32_t>(bt.hitPoints >> 1) && (bt.flags & building_flags::kSelectable)) {
                        b.smokeTimer = static_cast<std::uint8_t>((rng_.next() & 0x7f) + 0x40);
                        b.smokeBits |= static_cast<std::uint32_t>(rng_.next());
                    }
                } else {
                    if (vs == mySide_ && bt.specialBehavior != building_behaviour::kWall) mentat(vs, "BUILDINGLOST", true);
                    if (w != spiceWeapon) {
                        if (attacker != -1 && diplomacy(vs, sideByte) == 2) breakAlliance(vs, sideByte);
                        b.attackerIndex = firer;
                        b.attackerSide = static_cast<std::uint8_t>(attacker);
                        ++sides_[sideByte & 7].buildingsKilled;
                    }
                    ++S.buildingsLost;
                    destroyBuilding(b, false);
                }
            }
        }
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
            Unit& u = S.units[i];
            const std::uint32_t uf = u.flags;
            const auto& ut = unitTemplate(u);
            const bool notWorm = ut.specialBehavior != behaviour::kSandworm;
            bool ok = (uf & kFlying) ? high : !high;
            if ((uf & kInvulnerable) || (uf & kCarried)) ok = false;
            if (u.state == UnitState::Dead || u.state == UnitState::Infiltrating) ok = false;
            // The original skips every LEAVING_BUILDING unit (0x49d815). A survivor whose exit cell gets taken by an
            // enemy vehicle (driving in to crush it) waits in state 8 forever and can never be hurt. The community
            // patch (1.06p invulnerable-infantry-fix, in the Gruntmods edition) only skips it while it still stands
            // on a building tile - always applied here (an original bug, not gameplay).
            if (u.state == UnitState::LeavingBuilding && unitTileOccupiedByBuilding(u))
                ok = false;
            if (!ok) continue;
            std::uint32_t udmg = 0;
            if (!amount(ut.armorType, px - px16(u.posX), py - px16(u.posY), udmg)) continue;
            const std::int32_t dmg = static_cast<std::int32_t>(udmg);
            u.flags = uf | kAttacked;
            if (dmg < u.health) {
                u.health -= dmg;
                if (attacker != -1 && w != spiceWeapon && diplomacy(vs, sideByte) == 2) breakAlliance(vs, sideByte);
                if (!(u.flags & 0x2000000)) {
                    if (ut.specialBehavior == behaviour::kHarvester && vs == mySide_ && firer != kNoIndex)
                        mentat(mySide_, "HARVATTACK", true, 300);
                } else {
                    u.flags &= ~0x2000000u;
                }
                if (notWorm && u.health < static_cast<std::int32_t>(ut.hitPoints >> 1))
                    u.smokeTimer = static_cast<std::uint8_t>((rng_.next() & 0x7f) + 0x40);
                if (u.attackerIndex == kNoIndex && w != spiceWeapon && sideByte != vs) {
                    u.attackerSide = static_cast<std::uint8_t>(attacker);
                    u.attackerIndex = firer;
                }
                if (deviate && notWorm) {
                    u.flags &= ~kAttacked;
                    if (sideByte == vs || ut.isInfantry || unitTileOccupiedByBuilding(u)) {
                        u.lastAttacked = 1;
                    } else {
                        // Deviator: the unit changes to the attacker's side for 0xf0 checks.
                        if (!(u.flags & kDeviated)) u.oldSide = static_cast<std::uint8_t>(vs);
                        if (Unit* n = changeUnitOwner(vs, sideByte, u.index, false)) {
                            n->byte2E = 0xf0;
                            n->flags |= kDeviated;
                        }
                    }
                }
            } else {
                if (w != spiceWeapon) {
                    if (attacker != -1 && diplomacy(vs, sideByte) == 2) breakAlliance(vs, sideByte);
                    u.attackerIndex = firer;
                    u.attackerSide = static_cast<std::uint8_t>(attacker);
                    ++sides_[sideByte & 7].unitsKilled;
                }
                if (ut.isInfantry && rules_.armour && weapon(w).warhead < formats::kOrigWarheads)
                    setInfantryAnim(u, static_cast<int>(rules_.armour->warheads[weapon(w).warhead].infantryDeath) + 9);
                if (notWorm) {
                    if (vs == mySide_) mentat(vs, "UNITLOST", true);
                    // (a deviated unit, flag 0x1000, counts as lost for its OldSide)
                    ++S.unitsLost;
                    ++S.unitsLostPerType[u.type];
                    destroyUnit(u);
                } else {
                    u.health = 1;  // worms cannot be killed
                }
            }
        }
    }
    // Terrain damage: walkable cells without spice or buildings collect damage (cell +9);
    // concrete above 16 turns into a damaged-concrete tile and stops being concrete.
    Cell& c = cell(tx, ty);
    const std::uint32_t f = c.flags;
    if (!high && (f & kWalkOn) && !(f & kSpiceMask) && !(f & kBuilding)) {
        const std::uint32_t add = static_cast<std::uint32_t>(
                                      static_cast<int>(static_cast<std::uint32_t>(warheadVersus(w, 0)) * weapon(w).damage) / 100) /
                                  0x28;
        if (add + c.damage < 0xff) {
            c.damage = static_cast<std::uint8_t>(c.damage + add);
            if (c.damage > 0x10 && (f & kConcrete) && (f & kBuildOn)) {
                static constexpr std::uint16_t kDamagedConcreteTiles[8] = {0x228, 0x229, 0x22a, 0x22b,
                                                                           0x22c, 0x23c, 0x23d, 0x23e};  // 0x4e0af0
                c.flags = f & ~kConcrete;
                c.tile = kDamagedConcreteTiles[static_cast<std::uint32_t>(rng_.next()) & 7];
            }
        } else {
            c.damage = 0xff;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Death

// DestroyUnit 0x49cde0: death animation pick (infantry), white flash + death explosion
// drifting with the unit's last motion, death sound, state DEAD; saboteurs and behaviour 11
// explode (weapon groupIds[24] "SARDDEATH").
void World::destroyUnit(Unit& u) {
    const auto& t = unitTemplate(u);
    std::uint32_t drift = 0;
    if (!(u.flags & kFlying)) {
        if (u.flags & kCarried) u.flags |= kGone;
    } else {
        drift = explosion_flags::kDrift;
    }
    if ((u.flags & (kFlying | kCarried)) == (kFlying | kCarried) && t.specialBehavior == behaviour::kCarryall &&
        u.enemyIndex != kNoIndex)
        if (Unit* carried = unit(u.side, u.enemyIndex)) destroyUnit(*carried);
    if (t.isInfantry && (u.animType < 9 || u.animType > 13))
        setInfantryAnim(u, static_cast<int>(static_cast<std::uint32_t>(rng_.next()) & 3) + 9);
    // A dying carryall hands the units it was coming for back their saved state (pickup queue
    // entries in state 2 assigned to it become 3 = cancelled).
    if (t.specialBehavior == behaviour::kCarryall) {
        Side& S = sides_[u.side];
        for (int k = 0; k < S.pickupCount && k < static_cast<int>(S.pickup.size()); ++k) {
            PickupEntry& pe = S.pickup[k];
            if (pe.state != 2 || static_cast<std::int16_t>(pe.carryallIndex) != static_cast<std::int16_t>(u.index)) continue;
            if (pe.unitIndex != kNoIndex) unitAdjustState(S.units[static_cast<std::size_t>(pe.unitIndex) % kObjectsPerSide], pe.origState);
            pe.state = 3;
        }
    }
    const int de = t.deathExplosion;
    if (de >= 0 && u.state != UnitState::Dead) {
        int idx = u.facing * 512 + static_cast<int>((u.speed & 0x7ffffffu) >> 11);
        if (idx >= 32 * 512) idx = 32 * 512 - 1;  // beyond the table in the original
        const std::int32_t dx = sinTableFlat(idx), dy = -cosTableFlat(idx);
        const int x = px16(u.posX), y = px16(u.posY);
        addExplosion(u.side, rules_.templates->bin.groupIds[58], x, y, 0, drift, 0, dx, dy);  // EX_WHITEN1
        if (static_cast<std::size_t>(de) < explosionCount()) playSound(explosionTemplate(de).sound, div32(x), div32(y));
        addExplosion(u.side, de, x, y, u.posZ, drift, 0, dx, dy);
    }
    if (t.isInfantry && u.state != UnitState::Infiltrating)
        playNamed("S_MANDYING", static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % 9 + 1), div32(px16(u.posX)),
                  div32(px16(u.posY)));
    u.health = 0;
    unitAdjustState(u, UnitState::Dead);
    const int boom = rules_.templates->bin.groupIds[24];
    if ((t.specialBehavior == behaviour::kSardaukar || t.specialBehavior == behaviour::kSaboteur) && boom != -1)
        damageTiles(u.blockFromX << 5, u.blockFromY << 5, 0, boom, u.side, kNoIndex, false);
}

// DestroyBuilding 0x49ca10: 2 explosions per solid tile at random points of random solid
// tiles (hidden for 0/1 tick), debris bullets thrown from the art centre, state DEAD with a
// 30 tick timer (UpdateBuilding state 17 releases survivors at 25 and removes it at 0).
void World::destroyBuilding(Building& b, bool noSurvivors) {
    b.selected = false;
    const auto& t = buildingTemplate(b);
    const auto& G = rules_.templates->bin.groupIds;
    const int de = t.deathExplosion;
    if (de >= 0) {
        int solid = 0;
        for (int i = 0; i < 16; ++i) solid += (t.tilesOccupiedSolid >> i) & 1;
        const std::uint32_t ox = static_cast<std::uint32_t>(px16(b.posX));
        const std::uint32_t oy = static_cast<std::uint32_t>(px16(b.posY) - t.artHeight);
        if (static_cast<std::size_t>(de) < explosionCount())
            playSound(explosionTemplate(de).sound, static_cast<int>(ox >> 5), static_cast<int>(oy >> 5));
        if (t.screenShake != 0) screenShakes_ = 10;
        for (int k = 0; k < solid * 2; ++k) {
            std::uint32_t rx = 0, ry = 0;
            do {
                rx = static_cast<std::uint32_t>(rng_.next()) & 3;
                ry = static_cast<std::uint32_t>(rng_.next()) & 3;
            } while (!(t.tilesOccupiedSolid & (1u << (rx + ry * 4))));
            const std::uint32_t ex = rx * 32 + (ox & 0xffff) + (static_cast<std::uint32_t>(rng_.next()) & 0x1f);
            const std::uint32_t ey = ry * 32 + (oy & 0xffff) + (static_cast<std::uint32_t>(rng_.next()) & 0x1f);
            const int hidden = (rng_.next() & 0x1f) != 0 ? 1 : 0;
            addExplosion(b.side, de, static_cast<int>(ex), static_cast<int>(ey), 0, explosion_flags::kHidden, hidden);
            const int debris = static_cast<std::uint8_t>(G[49]);  // DebrisCount
            if (debris != 0) {
                const ArtSize img = buildingImageSize(b);
                const int sx = (img.width >> 1) + static_cast<int>(ox & 0xffff);
                const int sy = (img.height >> 1) + static_cast<int>(oy & 0xffff);
                const int pick = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(debris));
                const int delay = (rng_.next() & 0xf) + 8;
                const int bi = 25 + pick < static_cast<int>(sizeof(G)) ? addBullet(b.side, static_cast<std::uint8_t>(G[25 + pick]),
                                                                                  delay, b.index, sx, sy, sx, sy, kNoIndex, -1)
                                                                        : -1;
                if (bi >= 0) {
                    Bullet& d = sides_[b.side].bullets[bi];
                    d.speedX = ((rng_.next() & 0x1f) - 0x10) * 0x2000;
                    d.speedY = ((rng_.next() & 0x1f) - 0x10) * 0x2000;
                    d.posZ = (rng_.next() & 3) << 16;
                    d.fallSpeed = (-2 - (rng_.next() & 1)) * 0x10000;
                    d.flags |= weapon_flags::kBounce;
                }
            }
        }
    }
    b.deadTimer = 0x1e;
    if (noSurvivors) b.flags |= building_flags::kNoSurvivors;
    if (b.state != 17) b.state = 17;  // 0x49e120
}

// GetRandomAdjacentTile 0x4979a0: scans the 3x3 block around (x, y) (clipped one short of
// the right/bottom edge) and takes each free drivable cell with probability 1/2 (rand()).
static bool getRandomAdjacentTile(World& w, Random& rng, int& x, int& y) {
    const int x0 = x, y0 = y;
    int ys = y0 - 1, ye = y0 + 2, xs = x0 - 1, xe = x0 + 2;
    if (ys < 1) ys = 0;
    if (w.height() - 1 <= ye) ye = w.height() - 1;
    if (xs < 1) xs = 0;
    if (w.width() - 1 <= xe) xe = w.width() - 1;
    for (int cy = ys; cy < ye; ++cy)
        for (int cx = xs; cx < xe; ++cx) {
            const std::uint32_t f = w.cell(cx, cy).flags;
            if ((f & kOccupied) == 0 && (f & kDriveOn)) {
                x = cx;
                y = cy;
                if (rng.next() & 1) return true;
            }
        }
    return !(x == x0 && y == y0);
}

bool World::getRandomAdjacentTileExt(int& x, int& y) { return getRandomAdjacentTile(*this, rng_, x, y); }

// ReleaseSurvivors 0x4977a0: up to 4 (rule infantryReleaseLimit) infantry of the first infantry template (count =
// building cost / 3 / unit cost) leave a destroyed or sold building. 20 placement attempts
// in total.
void World::releaseSurvivors(Building& b) {
    const auto& t = buildingTemplate(b);
    if (!(t.flags & building_flags::kSelectable) || (b.flags & building_flags::kNoSurvivors)) return;
    b.flags |= building_flags::kInfantryReleased;
    const int x0 = static_cast<std::int16_t>(div32(px16(b.posX)));
    const int y0 = static_cast<std::int16_t>(div32(px16(b.posY) - t.artHeight));
    const auto& T = rules_.templates->bin;
    int type = 0;
    while (type < formats::kMaxUnitTypes && !T.units[type].isInfantry) ++type;
    if (type >= formats::kMaxUnitTypes) return;
    // GetDifficultyCostPercentage: 75/100/125 % for the player, 100 % for everybody else.
    const int level = b.side == mySide_ ? difficulty : 1;
    const std::uint32_t pct = level == 0 ? 75 : (level == 1 ? 100 : 125);
    const std::uint32_t bcost = pct * t.costBuild / 100, ucost = pct * T.units[type].cost / 100;
    if (ucost == 0) return;  // division by zero in the original
    std::uint32_t n = bcost / 3 / ucost;
    // Rules infantryReleaseLimit / infantryReleaseChance (release-infantry.c; defaults 4 / 5 =
    // the original's "at most 4" and 20 attempts).
    const std::uint32_t limit = rulesExt().infantryReleaseLimit;
    const std::uint32_t maxTries = limit * rulesExt().infantryReleaseChance;
    if (n > limit) n = limit;
    std::uint32_t tries = 0;
    for (std::uint32_t k = 0; k < n; ++k) {
        int cx = 0, cy = 0, ax = 0, ay = 0;
        for (;;) {
            const std::uint32_t r = static_cast<std::uint32_t>(rng_.next()) & 0xff;
            cx = static_cast<std::uint8_t>((r & 3) + static_cast<std::uint32_t>(x0));
            cy = static_cast<std::uint8_t>(((r & 0xf) >> 2) + static_cast<std::uint32_t>(y0));
            ax = cx;
            ay = cy;
            const bool ok = getRandomAdjacentTile(*this, rng_, ax, ay);
            ++tries;
            const bool good = (t.tilesOccupiedSolid & (1u << (r & 0xf))) && ok && inBounds(cx, cy) &&
                              !(cell(cx, cy).flags & kAnyUnit);
            if (good || tries >= maxTries) break;
        }
        if (tries < maxTries) addUnit(b.side, type, cx, cy, ax, ay);
    }
}

}  // namespace d2k::game
