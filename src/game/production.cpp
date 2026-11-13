// Production: build queues (one building, ten unit slots - one per factory kind - and one
// upgrade), costs and build speeds, the sidebar icon lists (prerequisites / tech level),
// building placement, selling, repairing, primary buildings, MCV deploy; plus the public
// economy/production API used by the UI. References: re/notes/economy.md.

#include "game/world.hpp"

#include "game/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
inline int px16(std::int32_t v) { return v / 65536; }
inline int div32(int v) { return v / 32; }
constexpr float kInvBuildDone = 1.0f / 23040.0f;  // 4.340278e-05 @0x4c93b8 (1/0x5a00 as float)

bool isSpecialWeapon(int beh) {  // palace / special units: built automatically, launched by order
    return beh == behaviour::kOrnithopter || beh == behaviour::kDeathHand || beh == behaviour::kSaboteur ||
           beh == behaviour::kFremen;
}
std::uint8_t gid(const formats::TemplatesBin& T, int i) { return static_cast<std::uint8_t>(T.groupIds[i]); }
}  // namespace

// =======================================================================================
// Costs and speeds

// GetDifficultyCostPercentage 0x4429b0 (campaign: only the local player uses the difficulty).
std::uint32_t World::difficultyCostPercent(int side) const {
    const int lvl = side == mySide_ ? difficulty : 1;
    if (lvl == 0) return rulesExt().costPercentageEasy;  // (75 / 125 unless a map rule changes them)
    return lvl != 1 ? rulesExt().costPercentageHard : 100;
}

// FUN_00442960: build speed by difficulty (easy builds faster).
std::uint32_t World::difficultySpeedPercent(int side) const {
    const int lvl = side == mySide_ ? difficulty : 1;
    if (lvl == 0) return rulesExt().buildSpeedPercentageEasy;
    return lvl != 1 ? rulesExt().buildSpeedPercentageHard : 100;
}

std::uint32_t World::unitCost(int type, int side) const {  // w__GetUnitCost 0x442bb0
    if (type < 0 || type >= formats::kMaxUnitTypes) return 0;
    return (difficultyCostPercent(side) * rules_.templates->bin.units[type].cost) / 100;
}

std::uint32_t World::buildingCost(int type, int level, int side) const {  // GetBuildingCost 0x442be0
    if (type < 0 || type >= formats::kMaxBuildingTypes || level < 0 || level > 3) return 0;
    const auto& t = rules_.templates->bin.buildings[type];
    const std::uint32_t c = level == 0 ? t.costBuild : t.costUpgrade[level - 1];
    return (difficultyCostPercent(side) * c) / 100;
}

namespace {
// Factory count bonus 100 / 150 / 200 % and the low-power slowdown (>>1 below 100 %, >>2 below
// 50 %, >>3 below 10 %).
std::uint32_t countBonus(std::uint32_t v) { return v == 0 ? 100 : v == 100 ? 150 : v == 150 ? 200 : v; }
std::uint32_t powerScale(std::uint32_t v, std::uint32_t power) {
    if (power < 10) return v >> 3;
    if (power < 50) return v >> 2;
    if (power < 100) return v >> 1;
    return v;
}
}  // namespace

// GetUnitBuildSpeedPercentage 0x442a00.
std::uint32_t World::unitBuildSpeedPercent(int type, int side) const {
    const auto& T = rules_.templates->bin;
    const Side& s = sides_[side];
    const auto& t = T.units[type];
    const int beh = t.specialBehavior;
    std::uint32_t v = 0;
    if (beh == behaviour::kOrnithopter || beh == behaviour::kSaboteur || beh == behaviour::kFremen) {
        v = 100;
    } else if (beh == behaviour::kDeathHand) {
        if (s.powerPercent >= 100) v = 100;
    } else {
        const std::uint8_t g = static_cast<std::uint8_t>(t.prereq1BuildingGroup);
        const std::uint8_t g2 = g == gid(T, group_ids::kAtHiTechQueue) ? gid(T, group_ids::kHiTechQueue) : g;
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
            const std::uint8_t bg = buildingTemplate(s.buildings[i]).buildingGroup;
            if (bg == g || bg == g2) v = countBonus(v);
        }
        v = powerScale(v, s.powerPercent);
    }
    return (difficultySpeedPercent(side) * v) / 100;
}

// GetBuildingBuildSpeedPercentage 0x442b10: by construction yard count.
std::uint32_t World::buildingBuildSpeedPercent(int side) const {
    const Side& s = sides_[side];
    std::uint32_t v = 0;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
        if (buildingTemplate(s.buildings[i]).specialBehavior == building_behaviour::kConstructionYard) v = countBonus(v);
    v = powerScale(v, s.powerPercent);
    return (difficultySpeedPercent(side) * v) / 100;
}

// =======================================================================================
// House versions and the sidebar

// CSide__MyVersionOfUnit 0x46c570: the last template of the group owned by the side's house,
// else the first of the group. onlyIfNotListed: -1 if a unit of that group is already an icon.
int World::sideVersionOfUnit(const Side& s, int group, bool onlyIfNotListed) const {
    const auto& T = rules_.templates->bin;
    if (onlyIfNotListed)
        for (int t : s.unitIcons)
            if (T.units[t].unitGroup == group) return -1;
    int found = -1;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (s.house & 0x1f));
    for (int i = 0; i < T.unitCount; ++i)
        if (T.units[i].unitGroup == group && ((T.units[i].ownerHouse & bit) || found == -1)) found = i;
    return found;  // -1: DebugFatal "MyVersionOfUnit didn't find a unit"
}

int World::sideVersionOfBuilding(const Side& s, int group, bool onlyIfNotListed) const {  // 0x46c450
    const auto& T = rules_.templates->bin;
    if ((group & 0xff) == 0xff) return 0;  // (sic)
    if (onlyIfNotListed)
        for (int t : s.buildingIcons)
            if (T.buildings[t].buildingGroup == group) return -1;
    int found = -1;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (s.house & 0x1f));
    for (int i = 0; i < T.buildingCount; ++i)
        if (T.buildings[i].buildingGroup == group && ((T.buildings[i].ownerHouse & bit) || found == -1)) found = i;
    return found;
}

// CSide__UpdateBuildingAndUnitIconsAndBaseBoundaries 0x46be50.
void World::updateIcons(int side) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    const std::vector<int> prevB = s.buildingIcons, prevU = s.unitIcons;
    s.buildingIcons.clear();  // FUN_0046c640
    s.unitIcons.clear();
    std::array<std::uint8_t, formats::kMaxBuildingTypes> houses{};  // owner-house mask per existing group
    bool conyard = false;
    int minX = width_, minY = height_, maxX = 0, maxY = 0;
    if (s.firstBuilding == -1) {
        minX = minY = 0;
    } else {
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
            const Building& b = s.buildings[i];
            const auto& t = buildingTemplate(b);
            if (t.techLevelBuild == 9) conyard = true;  // the construction yard marker
            houses[t.buildingGroup % formats::kMaxBuildingTypes] |= t.ownerHouse;
            const int bx = div32(px16(b.posX)), by = div32(px16(b.posY));
            minX = std::min(minX, bx - 2);
            maxX = std::max(maxX, (t.artWidth >> 5) + bx + 2);
            minY = std::min(minY, by - 2 - (t.artHeight >> 5));
            maxY = std::max(maxY, by + 2);
        }
    }
    // (Map rule buildingsAlwaysNeedPrerequisites: the flags are rebuilt every time, not sticky.)
    if (!conyard || rulesExt().buildingsAlwaysNeedPrerequisites) s.buildable.fill(0);
    if (minX == 0 && minY == 0) {
        minX = width_;
        minY = height_;
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
            const Unit& u = s.units[i];
            const int ux = div32(px16(u.posX)), uy = div32(px16(u.posY));
            minX = std::min(minX, ux);
            maxX = std::max(maxX, ux + 1);
            minY = std::min(minY, uy);
            maxY = std::max(maxY, uy + 1);
        }
    }
    s.baseMinY = minY < 1 ? 0 : (minY & 0xff);
    s.baseMaxY = std::min(maxY, height_ - 1) & 0xff;
    s.baseMinX = minX < 1 ? 0 : (minX & 0xff);
    s.baseMaxX = std::min(maxX, width_ - 1) & 0xff;
    const std::uint8_t tech = static_cast<std::uint8_t>(s.techLevel);
    auto upgradesOf = [&](int g) { return g >= 0 && g < formats::kMaxBuildingTypes ? s.upgrades[g] : 0; };
    auto housesOf = [&](int g) { return g >= 0 && g < formats::kMaxBuildingTypes ? houses[g] : 0; };
    if (conyard) {
        // Buildable flags are sticky: set here, cleared only when no construction yard is left.
        for (int i = 0; i < T.buildingCount; ++i) {
            const auto& t = T.buildings[i];
            bool ok = t.prereq1BuildingGroup != -1 && (t.prereq1OwnerHouse & housesOf(t.prereq1BuildingGroup)) != 0;
            if (tech < static_cast<std::uint8_t>(t.techLevelBuild)) ok = false;
            if (upgradesOf(t.prereq1BuildingGroup) < t.prereq1UpgradesNeeded) ok = false;
            if (t.prereq2BuildingGroup != -1) {
                if ((t.prereq2OwnerHouse & housesOf(t.prereq2BuildingGroup)) == 0) ok = false;
                if (upgradesOf(t.prereq2BuildingGroup) < t.prereq2UpgradesNeeded) ok = false;
            }
            if (ok) s.buildable[i] = 1;
        }
    }
    for (int i = 0; i < formats::kMaxBuildingTypes; ++i) {
        if (!s.buildable[i]) continue;
        const int v = sideVersionOfBuilding(s, T.buildings[i].buildingGroup, true);
        if (v >= 0 && s.buildingIcons.size() < 60) s.buildingIcons.push_back(v);  // AddBuildingIcon 0x46c680
    }
    for (int i = 0; i < T.unitCount; ++i) {
        const auto& t = T.units[i];
        bool ok = false;
        if (t.prereq1BuildingGroup != -1) {
            if (t.prereq1OwnerHouse & housesOf(t.prereq1BuildingGroup)) ok = true;
            if (upgradesOf(t.prereq1BuildingGroup) < t.prereq1UpgradesNeeded) ok = false;
        }
        if (t.prereq2BuildingGroup != -1 && housesOf(t.prereq2BuildingGroup) == 0) ok = false;
        if (tech < static_cast<std::uint8_t>(t.techLevel)) ok = false;
        if (t.multiplayerOnly) ok = false;  // campaign (!NetworkGame && GameType != 1)
        if (!ok) continue;
        const int v = sideVersionOfUnit(s, t.unitGroup, true);
        if (v >= 0 && s.unitIcons.size() < 60) s.unitIcons.push_back(v);  // AddUnitIcon 0x46c6d0
    }
    applyBuildOverrides(side);  // not original: cheat overrides (no-op without any)
    // Icons that disappeared cancel their production with a refund of the progress share.
    for (int old : prevB) {
        if (std::find(s.buildingIcons.begin(), s.buildingIcons.end(), old) != s.buildingIcons.end()) continue;
        ProductionSlot& q = s.buildingQueue;
        if (q.type >= 0 && old == q.type) {
            returnCredits(side, static_cast<std::int32_t>((q.progress * buildingCost(q.type, 0, side)) / kBuildDone));
            q.type = -1;
        }
    }
    for (int old : prevU) {
        if (std::find(s.unitIcons.begin(), s.unitIcons.end(), old) != s.unitIcons.end()) continue;
        for (auto& q : s.unitQueue)
            if (q.type >= 0 && old == q.type) {
                returnCredits(side, static_cast<std::int32_t>((q.progress * unitCost(q.type, side)) / kBuildDone));
                q.type = -1;
            }
    }
    if (side == mySide_) {
        const int dh = gid(T, 19);  // Death Hand missile
        if (std::find(prevU.begin(), prevU.end(), dh) == prevU.end() &&
            std::find(s.unitIcons.begin(), s.unitIcons.end(), dh) != s.unitIcons.end())
            mentat(side, "DEATHPREPPING", true);
    }
}

// w_CanUnitBeBuilt 0x4430d0: one unit at a time per factory group (special weapons excepted).
bool World::canUnitBeBuilt(int side, int type, bool specialOk) const {
    const auto& T = rules_.templates->bin;
    if (specialOk && isSpecialWeapon(T.units[type].specialBehavior)) return true;
    const int g = T.units[type].prereq1BuildingGroup;
    for (const auto& q : sides_[side].unitQueue) {
        if (q.type < 0) continue;
        if (T.units[q.type].prereq1BuildingGroup == g && !isSpecialWeapon(T.units[q.type].specialBehavior)) return false;
    }
    return true;
}

// CanSideUpgradeBuildingGroup 0x4431d0.
bool World::canSideUpgradeGroup(int side, int group) const {
    if (group < 0 || group >= formats::kMaxBuildingTypes) return false;
    const Side& s = sides_[side];
    if (s.buildingsExistPerGroup[group] == 0) return false;
    const int lvl = s.upgrades[group];
    if (lvl > 2) return false;
    const int t = sideVersionOfBuilding(s, group, false);
    if (t < 0) return false;
    return static_cast<std::uint8_t>(rules_.templates->bin.buildings[t].techLevelUpgrade[lvl]) <=
           static_cast<std::uint8_t>(s.techLevel);
}

// FindUnitExitBuilding 0x442c20: the factory a unit type leaves from (primary first, then any
// building of the factory group the unit's house mask allows) and its exit tiles: spawn at
// exit point 1, walk to exit point 2. needFree: the spawn tile must have no unit.
Building* World::findUnitExitBuilding(int type, int side, int& sx, int& sy, int& tx, int& ty, bool needFree) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    const auto& ut = T.units[type];
    const std::uint32_t g = static_cast<std::uint32_t>(ut.prereq1BuildingGroup);
    auto P = [&](int k) { return s.primaryBuilding[k]; };
    int idx = kNoIndex;
    if (g == gid(T, group_ids::kLightFactoryQueue)) idx = P(1);
    else if (g == gid(T, group_ids::kHeavyFactoryQueue)) idx = P(2);
    else if (g == gid(T, group_ids::kHiTechQueue) || g == gid(T, group_ids::kAtHiTechQueue)) idx = P(3) != kNoIndex ? P(3) : P(4);
    else if (g == gid(T, group_ids::kWorQueue)) idx = P(6);
    else if (g == gid(T, group_ids::kBarracksQueue)) idx = P(5);
    else if (g == gid(T, group_ids::kAtreidesPalace)) idx = P(8) != kNoIndex ? P(8) : P(9) != kNoIndex ? P(9) : P(10);
    else if (g == gid(T, group_ids::kHarkonnenPalace)) idx = P(9) != kNoIndex ? P(9) : P(8) != kNoIndex ? P(8) : P(10);
    else if (g == gid(T, group_ids::kOrdosPalace)) idx = P(10) != kNoIndex ? P(10) : P(9) != kNoIndex ? P(9) : P(8);
    else return nullptr;  // DebugFatal "Unit expects to arrive from unknown building"
    auto exits = [&](Building& b) {
        const auto& bt = buildingTemplate(b);
        const int x = div32(px16(b.posX)), y = div32(px16(b.posY) - bt.artHeight);
        sx = static_cast<std::uint8_t>(bt.exitPoint1X + x);
        sy = static_cast<std::uint8_t>(bt.exitPoint1Y + y);
        tx = static_cast<std::uint8_t>(bt.exitPoint2X + x);
        ty = static_cast<std::uint8_t>(bt.exitPoint2Y + y);
        return !needFree || !inBounds(sx, sy) || (cell(sx, sy).flags & kAnyUnit) == 0;
    };
    if (idx != kNoIndex) {
        Building& b = s.buildings[static_cast<std::size_t>(idx) % kObjectsPerSide];
        if ((ut.prereq1OwnerHouse & buildingTemplate(b).ownerHouse) && exits(b)) return &b;
    }
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        Building& b = s.buildings[i];
        const auto& bt = buildingTemplate(b);
        if (bt.buildingGroup == g && (ut.prereq1OwnerHouse & bt.ownerHouse) && exits(b)) return &b;
    }
    return nullptr;
}

// =======================================================================================
// Queue orders

// ModelBuildUnitPick 0x455510 (order 0x27).
void World::unitPick(int side, int type) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    const int beh = T.units[type].specialBehavior;
    const char* speech = T.units[type].isInfantry ? "TRAINING" : "BUILDING";
    for (auto& q : s.unitQueue) {
        if (q.type != (type & 0xffff)) continue;
        if (!q.onHold) return;
        if (s.cash + s.spice == 0 && T.units[type].cost != 0) return;
        q.onHold = false;
        mentat(side, speech, false);
        return;
    }
    if (!canUnitBeBuilt(side, type, true)) return;
    auto it = std::find_if(s.unitQueue.begin(), s.unitQueue.end(), [](const ProductionSlot& q) { return q.type == -1; });
    if (it == s.unitQueue.end()) return;  // DebugFatal "MAX_BUILDABLE_UNITS exceeded"
    *it = ProductionSlot{};
    it->type = type & 0xff;
    if (s.cash + s.spice == 0) {
        it->onHold = true;
        if (!isSpecialWeapon(beh)) mentat(side, "LOWMONEY", true);
    } else if (!isSpecialWeapon(beh)) {
        mentat(side, speech, false);
    }
}

// FUN_00455750 (order 0x28): first click puts it on hold, the second (or a finished item)
// cancels with a refund. Quirk: only slots 0..7 are searched.
void World::unitCancel(int side, int type) {
    Side& s = sides_[side];
    for (int i = 0; i < 8; ++i) {
        ProductionSlot& q = s.unitQueue[i];
        if (q.type != (type & 0xffff)) continue;
        if (!q.onHold && q.progress != kBuildDone) {
            q.onHold = true;
            mentat(side, "ONHOLD", false);
        } else {
            returnCredits(side, static_cast<std::int32_t>((q.progress * unitCost(type & 0xffff, side)) / kBuildDone));
            q.type = -1;
            mentat(side, "CANCELLED", false);
        }
        return;
    }
}

// =======================================================================================
// Extended unit queue (NOT ORIGINAL; re/notes/economy.md §10). Side::autoQueue holds
// {type, remaining} entries in selection order; per factory group (prereq1BuildingGroup, the
// original's one-unit-per-group rule of w_CanUnitBeBuilt) the first entry feeds the group's
// ProductionSlot whenever it is free. Finite entries keep their place until their count is used
// up (so finite types are built in the order they were chosen); an infinite entry goes to the
// back after each unit it starts, so infinite types alternate round-robin (and any finite type
// chosen after them gets its turn when it comes up, then stays until done). Starting a slot is
// free; the cost is paid progressively by updateProduction as in the original, which stalls
// while the side cannot pay. The AI never queues these orders.

namespace {
ProductionSlot* slotOfType(Side& s, int type) {
    for (auto& q : s.unitQueue)
        if (q.type >= 0 && q.type == type) return &q;
    return nullptr;
}
const ProductionSlot* slotOfType(const Side& s, int type) {
    for (const auto& q : s.unitQueue)
        if (q.type >= 0 && q.type == type) return &q;
    return nullptr;
}
std::vector<AutoBuildEntry>::iterator autoEntry(Side& s, int type) {
    return std::find_if(s.autoQueue.begin(), s.autoQueue.end(), [&](const AutoBuildEntry& e) { return e.type == type; });
}
}  // namespace

void World::autoAdd(int side, int type, int n) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    if (n < 1 || std::find(s.unitIcons.begin(), s.unitIcons.end(), type) == s.unitIcons.end()) return;
    if (isSpecialWeapon(T.units[type].specialBehavior)) return;
    ProductionSlot* q = slotOfType(s, type);
    if (q && q->onHold) q->onHold = false;  // (a unit put on hold in Original queue mode)
    const int running = q ? 1 : 0;
    auto it = autoEntry(s, type);
    if (it != s.autoQueue.end()) {
        if (it->remaining == kAutoInfinite) return;
        it->remaining = std::min(it->remaining + n, kAutoMaxCount - running);
    } else {
        const int add = std::min(n, kAutoMaxCount - running);
        if (add <= 0) return;
        s.autoQueue.push_back({type, add});
    }
    mentat(side, T.units[type].isInfantry ? "TRAINING" : "BUILDING", false);
}

void World::autoInfinite(int side, int type) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    if (type < 0 || type >= formats::kMaxUnitTypes || isSpecialWeapon(T.units[type].specialBehavior)) return;
    auto it = autoEntry(s, type);
    if (it != s.autoQueue.end() && it->remaining == kAutoInfinite) {
        s.autoQueue.erase(it);
        autoCancelSlot(side, type);
        mentat(side, "CANCELLED", false);
        return;
    }
    if (std::find(s.unitIcons.begin(), s.unitIcons.end(), type) == s.unitIcons.end()) return;
    if (ProductionSlot* q = slotOfType(s, type); q && q->onHold) q->onHold = false;
    if (it != s.autoQueue.end()) it->remaining = kAutoInfinite;
    else s.autoQueue.push_back({type, kAutoInfinite});
    mentat(side, T.units[type].isInfantry ? "TRAINING" : "BUILDING", false);
}

void World::autoRemove(int side, int type) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    if (type < 0 || type >= formats::kMaxUnitTypes || isSpecialWeapon(T.units[type].specialBehavior)) return;
    auto it = autoEntry(s, type);
    if (it != s.autoQueue.end()) {
        // Infinite: the unit in production is finished, nothing more is started.
        if (it->remaining == kAutoInfinite || --it->remaining <= 0) s.autoQueue.erase(it);
        return;
    }
    if (slotOfType(s, type)) {  // only the unit in production is left: cancel it
        autoCancelSlot(side, type);
        mentat(side, "CANCELLED", false);
    }
}

// Cancel the unit in production like the original's second cancel click (FUN_00455750):
// refund progress * cost / 0x5a00.
void World::autoCancelSlot(int side, int type) {
    Side& s = sides_[side];
    if (ProductionSlot* q = slotOfType(s, type)) {
        returnCredits(side, static_cast<std::int32_t>((q->progress * unitCost(type, side)) / kBuildDone));
        q->type = -1;
    }
}

// Per tick before the unit slots advance: drop entries whose type can no longer be built
// (icon gone: prerequisite / factory lost; or no factory to leave from), then start the first
// entry of every factory group whose slot is free.
void World::updateAutoQueue(int side) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    std::vector<AutoBuildEntry> keep, back;
    keep.reserve(s.autoQueue.size());
    for (AutoBuildEntry e : s.autoQueue) {
        int sx = 0, sy = 0, tx = 0, ty = 0;
        if (e.type < 0 || e.type >= formats::kMaxUnitTypes || isSpecialWeapon(T.units[e.type].specialBehavior) ||
            std::find(s.unitIcons.begin(), s.unitIcons.end(), e.type) == s.unitIcons.end() ||
            (!findUnitExitBuilding(e.type, side, sx, sy, tx, ty, false) &&
             unitOverride(side, e.type) != BuildOverride::Allowed))
            continue;
        if (canUnitBeBuilt(side, e.type, false)) {
            auto it = std::find_if(s.unitQueue.begin(), s.unitQueue.end(), [](const ProductionSlot& q) { return q.type == -1; });
            if (it != s.unitQueue.end()) {
                *it = ProductionSlot{};
                it->type = e.type;
                if (e.remaining == kAutoInfinite) {
                    back.push_back(e);
                    continue;
                }
                if (--e.remaining <= 0) continue;
            }
        }
        keep.push_back(e);
    }
    keep.insert(keep.end(), back.begin(), back.end());
    s.autoQueue = std::move(keep);
}

// ModelBuildBuildingPick 0x455fc0 (order 0x24). Quirk: with an item on hold, any pick resumes it.
void World::buildingPick(int side, int type) {
    Side& s = sides_[side];
    ProductionSlot& q = s.buildingQueue;
    if (q.type < 0) {
        q = ProductionSlot{};
        q.type = type;
        if (s.spice + s.cash == 0) {
            q.onHold = true;
            mentat(side, "LOWMONEY", true);
            return;
        }
    } else {
        if (!q.onHold || s.spice + s.cash == 0) return;
        q.onHold = false;
    }
    mentat(side, "BUILDING", false);
}

// FUN_004560f0 (order 0x25).
void World::buildingCancel(int side, int type) {
    Side& s = sides_[side];
    ProductionSlot& q = s.buildingQueue;
    if ((type & 0xffff) != q.type) return;
    if (!q.onHold && q.progress < kBuildDone) {
        q.onHold = true;
        q.placing = false;
        mentat(side, "ONHOLD", false);
    } else {
        returnCredits(side, static_cast<std::int32_t>((q.progress * buildingCost(q.type, 0, side)) / kBuildDone));
        q.type = -1;
        mentat(side, "CANCELLED", false);
    }
}

// FUN_00457350 (order 0x2d): by building group.
void World::upgradePick(int side, int group) {
    if (group > 99 || !canSideUpgradeGroup(side, group)) return;
    Side& s = sides_[side];
    ProductionSlot& q = s.upgradeQueue;
    if (q.type < 0) {
        q = ProductionSlot{};
        q.type = sideVersionOfBuilding(s, group, false);
        if (s.spice + s.cash == 0) {
            q.onHold = true;
            mentat(side, "LOWMONEY", true);
            return;
        }
    } else {
        if (!q.onHold || s.spice + s.cash == 0) return;
        q.onHold = false;
    }
    mentat(side, "BUILDUPGRADE", false);
}

// FUN_00457490 (order 0x2e): by building *template* (the sidebar passes its upgrade list
// entry). Quirk: the refund indexes the upgrade levels by that template index.
void World::upgradeCancel(int side, int type) {
    Side& s = sides_[side];
    ProductionSlot& q = s.upgradeQueue;
    if (type != q.type) return;
    if (!q.onHold) {
        q.onHold = true;
        mentat(side, "ONHOLD", false);
    } else {
        const int lvl = type >= 0 && type < formats::kMaxBuildingTypes ? s.upgrades[type] : 0;
        returnCredits(side, static_cast<std::int32_t>((q.progress * buildingCost(q.type, lvl + 1, side)) / kBuildDone));
        q.type = -1;
        mentat(side, "CANCELLED", false);
    }
}

// FUN_00457740: every footprint tile (TilesOccupiedAll) on the map and free of units/buildings.
bool World::footprintFree(int type, int x, int y) const {
    const auto& t = rules_.templates->bin.buildings[type];
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(t.tilesOccupiedAll & bit)) continue;
            const int cx = (x & 0xff) + c, cy = (y & 0xff) + r;
            if (cx >= width_ || cy >= height_ || (cell(cx, cy).flags & kOccupied)) return false;
        }
    return true;
}

// ProcessOrder case 0x26 (place the finished building).
void World::placeBuilding(int side, int type, int x, int y) {
    const auto& t = rules_.templates->bin.buildings[type];
    Side& s = sides_[side];
    s.buildingQueue.placing = false;
    if (t.specialBehavior == building_behaviour::kConcrete) {
        s.buildingQueue.type = -1;
        addConcrete(side, type, x, y, t.tilesOccupiedSolid);
        if (side == mySide_) playNamed("S_BUILDCONC", 0, x, y);
    } else if (footprintFree(type, x, y)) {
        s.buildingQueue.type = -1;
        addBuilding(side, type, x, y, false, false, false);
        if (side == mySide_) playNamed("S_BUILDUP", 0, x, y);
    }
}

// SetBuildingAsPrimary 0x44b1f0 (order 0x20).
void World::setBuildingAsPrimary(int side, int index) {
    Building* b = building(side, index);
    if (!b) return;
    Side& s = sides_[side];
    int slot = -1;
    std::uint32_t clear = 0x90;
    switch (buildingTemplate(*b).specialBehavior) {
    case 2: slot = 0; break;
    case 7: slot = 1; break;
    case 8: slot = 2; break;
    case 9: slot = 3; break;
    case 10: slot = 4; break;
    case 11: slot = 5; break;
    case 12: slot = 6; clear = 0x80; break;
    case 13: slot = 7; clear = 0x80; break;
    case 0x11: slot = 9; clear = 0x80; break;
    case 0x12: slot = 10; clear = 0x80; break;
    case 0x13: slot = 8; clear = 0x80; break;
    default: return;
    }
    const int old = s.primaryBuilding[slot];
    if (old != kNoIndex) s.buildings[static_cast<std::size_t>(old) % kObjectsPerSide].flags &= ~clear;  // (DebugFatal if none)
    s.primaryBuilding[slot] = index;
    b->flags |= building_flags::kPrimary;
}

// FUN_00456ee0 (order 0x23): start the sell animation (counter = build-up frames * 3, counting
// down); the refund is paid when the building is freed (ModelUpdate). Quirk: the frame table
// is indexed by building type here (by build-up art in UpdateBuilding).
void World::sellBuilding(int side, int index) {
    Building* b = building(side, index);
    if (!b || (b->flags & 0x1000000) || b->state == 17) return;
    const std::uint8_t c = static_cast<std::uint8_t>(rules_.templates->bin.buildupArtFrames[b->type] * 3);
    b->buildupCounter = c;
    if (c == 0) {
        b->flags |= building_flags::kSold;
    } else {
        b->flags |= building_flags::kSelling;
        b->buildupCounter = static_cast<std::uint8_t>(c - 1);
        b->buildupTick = gameTicks_;
        b->selected = false;
    }
    if (side == mySide_ && buildingTemplate(*b).specialBehavior != building_behaviour::kWall) mentat(side, "SELL", false);
}

// FUN_004466e0: special-weapon units on the sidebar are picked automatically every tick.
void World::autoPickSpecialWeapons() {
    const auto& T = rules_.templates->bin;
    for (int side = 0; side < kMaxSides; ++side) {
        for (int t : sides_[side].unitIcons)  // (unitPick does not change the icon list)
            if (t != -1 && isSpecialWeapon(T.units[t].specialBehavior)) unitPick(side, t);
    }
}

// One production tick of a slot (FUN_004467e0): the cost is deducted progressively. Each tick
// adds f = step * (1/0x5a00) * cost to a float accumulator; the amount spent is
// trunc(f + accumulator) - paid (x87 double precision), the last step pays the remainder.
// Nothing happens (production stalls) when the side cannot pay.
bool World::productionStep(int side, ProductionSlot& q, std::uint32_t step, std::int32_t cost, bool paidMask16) {
    double st0 = static_cast<double>(static_cast<std::int64_t>(step)) * static_cast<double>(kInvBuildDone);
    st0 = st0 * static_cast<double>(cost);
    const float f = static_cast<float>(st0);
    const double sum = st0 + static_cast<double>(q.costAccum);
    std::int32_t amount = static_cast<std::int32_t>(sum) - q.paid;
    if (step + q.progress > static_cast<std::uint32_t>(kBuildDone - 1)) amount = cost - q.paid;
    if (!spend(side, amount)) return false;
    q.paid += paidMask16 ? (amount & 0xffff) : amount;
    q.costAccum = static_cast<float>(static_cast<double>(f) + static_cast<double>(q.costAccum));
    q.prevProgress = q.progress;
    q.progress = static_cast<std::uint16_t>(q.progress + step);
    return true;
}

// FUN_004467e0 (after ModelTick): advance every side's building, unit and upgrade production.
void World::updateProduction() {
    const auto& T = rules_.templates->bin;
    for (int side = 0; side < kMaxSides; ++side) {
        Side& s = sides_[side];
        if (!s.autoQueue.empty()) updateAutoQueue(side);  // not original (extended queue)
        ProductionSlot& bq = s.buildingQueue;
        if (bq.type >= 0 && bq.progress < kBuildDone && !bq.onHold) {
            const std::uint32_t pct = buildingBuildSpeedPercent(side);
            std::uint32_t step = (T.buildings[bq.type].buildSpeedBuild * pct) / 100;
            if (step == 0 && buildingOverride(side, bq.type) == BuildOverride::Allowed)  // not original: no yard
                step = (T.buildings[bq.type].buildSpeedBuild * difficultySpeedPercent(side)) / 100;
            if (cheats_.instantBuild[side]) step = kBuildDone;  // not original (cheat)
            const std::int32_t cost = static_cast<std::int32_t>(buildingCost(bq.type, 0, side));
            if (productionStep(side, bq, step, cost, false) && bq.progress > kBuildDone - 1) {
                bq.progress = kBuildDone;
                mentat(side, "BUILDREADY", true);
            }
        }
        for (auto& q : s.unitQueue) {
            if (q.type < 0) continue;
            int sx = 0, sy = 0, tx = 0, ty = 0;
            Building* fac = findUnitExitBuilding(q.type, side, sx, sy, tx, ty, false);
            if (!fac) {  // factory gone: the slot is dropped without a refund
                if (cheatProductionNoFactory(side, q)) continue;  // not original: cheat-allowed unit
                q.type = -1;
                continue;
            }
            const int beh = T.units[q.type].specialBehavior;
            if (q.onHold) {
                fac->flags &= ~building_flags::kAnimPermanent;
                continue;
            }
            if (q.progress < kBuildDone) {
                if (beh != behaviour::kDeathHand) fac->flags |= building_flags::kAnimPermanent;
                const std::uint32_t pct = unitBuildSpeedPercent(q.type, side);
                std::uint32_t step = (T.units[q.type].buildSpeed * pct) / 100;
                if (cheats_.instantBuild[side]) step = kBuildDone;  // not original (cheat)
                const std::int32_t cost = static_cast<std::int32_t>(unitCost(q.type, side));
                if (!productionStep(side, q, step, cost, false) || q.progress <= kBuildDone - 1) continue;
                q.progress = kBuildDone;
                if (beh == behaviour::kDeathHand) {
                    mentat(side, "DEATHREADY", true);
                } else if (!isSpecialWeapon(beh)) {
                    // (special weapons only queue a text message: AirStrikeReady / SaboteurReady / FremenReady)
                    if (Building* exit = findUnitExitBuilding(q.type, side, sx, sy, tx, ty, true)) {
                        rallyUnitLeft(side, addUnit(side, q.type, sx, sy, tx, ty, 0, 0), *exit);  // (rally: not original)
                        q.type = -1;
                        mentat(side, "UNITREADY", true);
                    }
                    fac->flags &= ~building_flags::kAnimPermanent;
                }
            } else if (q.progress != kBuildDone) {
                fac->flags &= ~building_flags::kAnimPermanent;
            } else if (Building* exit = isSpecialWeapon(beh) ? nullptr : findUnitExitBuilding(q.type, side, sx, sy, tx, ty, true)) {
                rallyUnitLeft(side, addUnit(side, q.type, sx, sy, tx, ty, 0, 0), *exit);  // the exit was blocked before
                q.type = -1;
                mentat(side, "UNITREADY", true);
            }
        }
        ProductionSlot& uq = s.upgradeQueue;
        if (uq.type >= 0) {
            const int g = T.buildings[uq.type].buildingGroup;
            if (s.buildingsExistPerGroup[g] == 0) {
                returnCredits(side, static_cast<std::int32_t>((uq.progress * buildingCost(uq.type, s.upgrades[g] + 1, side)) / kBuildDone));
                uq.type = -1;
            } else if (uq.progress < kBuildDone && !uq.onHold) {
                const int lvl = s.upgrades[g];
                const int mv = sideVersionOfBuilding(s, g, false);
                if (mv >= 0 && lvl < 3) {
                    std::uint32_t step = T.buildings[mv].buildSpeedUpgrade[lvl];
                    if (cheats_.instantBuild[side]) step = kBuildDone;  // not original (cheat)
                    const std::int32_t cost = static_cast<std::int32_t>(buildingCost(mv, lvl + 1, side));
                    if (productionStep(side, uq, step, cost, true) && uq.progress > kBuildDone - 1) {
                        ++s.upgrades[g];
                        uq.type = -1;
                        updateIcons(side);
                    }
                }
            }
        }
    }
    // Local player advisories: LOWPOWER / SILOSNEEDED are requested every tick while the
    // condition holds; the sound queue's 2000-tick repeat guard spaces them out.
    Side& me = sides_[mySide_ & 7];
    if (me.unitIcons.size() > me.prevUnitIcons || me.buildingIcons.size() > me.prevBuildingIcons)
        mentat(mySide_, "NEWOPTIONS", true);
    me.prevUnitIcons = me.unitIcons.size();
    me.prevBuildingIcons = me.buildingIcons.size();
    if (me.powerPercent < 100 && me.powerOutput != 0) mentat(mySide_, "LOWPOWER", true, 2000);
    if (static_cast<std::uint32_t>(me.maxStorage) < static_cast<std::uint32_t>(me.spice + 100) &&
        me.buildingsExistPerGroup[gid(T, group_ids::kRefinery)] != 0)
        mentat(mySide_, "SILOSNEEDED", true, 2000);
    const std::uint32_t total = static_cast<std::uint32_t>(me.cash + me.spice);
    if (total == 0) {
        if (!lowMoneyWarned_) {
            lowMoneyWarned_ = true;
            mentat(mySide_, "LOWMONEY", true, 2000);
        }
    } else if (total > 99) {
        lowMoneyWarned_ = false;
    }
}

// =======================================================================================
// MCV deploy

// CheckIfMCVCanBeDeployedOn 0x443250: the 3x3 block around the MCV must be on the map (not the
// first row), buildable and free (the MCV's own cell excepted).
bool World::canDeploy(int side, int unitIndex) const {
    const Unit* u = const_cast<World*>(this)->unit(side, unitIndex);
    if (!u) return false;
    switch (unitTemplate(*u).specialBehavior) {
    case behaviour::kMcv: return canDeployMcv(side, unitIndex);
    case behaviour::kDevastator: return true;
    case behaviour::kThumper: return (cell(u->blockToX, u->blockToY).flags & cell_flags::kSand) != 0;
    case behaviour::kSaboteur: return u->byte29 >= 0xa0;
    default: return false;
    }
}

bool World::canDeployMcv(int side, int unitIndex) const {
    const Unit* u = const_cast<World*>(this)->unit(side, unitIndex);
    if (!u) return false;
    const int x = u->blockFromX, y = u->blockFromY;
    for (int yy = y - 1; yy <= y + 1; ++yy)
        for (int xx = x - 1; xx <= x + 1; ++xx) {
            if (xx < 0 || xx >= width_ || yy < 1 || yy >= height_) return false;
            const std::uint32_t f = cell(xx, yy).flags;
            if ((!(f & kBuildOn) || (f & kOccupied)) && (xx != x || yy != y)) return false;
        }
    return true;
}

// UpdateUnit case 0x1c (DEPLOYING, state 30): turn to facing 29, then replace the MCV by the
// side's construction yard (top-left one tile up-left), keeping the health ratio.
void World::updateDeploying(Unit& u) {
    if (u.posSteps != 0) {
        u.targetX = u.rememberX;
        u.targetY = u.rememberY;
        moveUnit(u);
        return;
    }
    if (rotateBody(u, 0x1d)) return;
    const auto& T = rules_.templates->bin;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (sides_[u.side].house & 0x1f));
    int conyard = -1;
    for (int i = 0; i <= T.buildingCount && i < formats::kMaxBuildingTypes && conyard < 0; ++i)
        if ((T.buildings[i].ownerHouse & bit) && T.buildings[i].specialBehavior == building_behaviour::kConstructionYard)
            conyard = i;
    if (conyard < 0 || !canDeployMcv(u.side, u.index)) {  // ("Cannot find correct construction yard")
        u.state = UnitState::Idle;
        return;
    }
    cell(u.blockFromX, u.blockFromY).flags &= ~kVehicle;
    const float ratio = static_cast<float>(u.health) / static_cast<float>(static_cast<std::int32_t>(unitTemplate(u).hitPoints));
    const int idx = addBuilding(u.side, conyard, u.blockFromX - 1, u.blockFromY - 1, false, false, false);
    if (u.side == mySide_) playNamed("S_BUILDUP", 0, u.blockFromX, u.blockFromY);
    if (idx >= 0)
        sides_[u.side].buildings[idx].health = static_cast<std::int32_t>(
            static_cast<std::int64_t>(static_cast<double>(ratio) * static_cast<double>(T.buildings[conyard].hitPoints)));
    u.flags |= 0x600000;  // not drawn, gone (the dead state keeps its cells)
    unitAdjustState(u, UnitState::Dead);
}

// =======================================================================================
// Public API: queries

std::int32_t World::credits(int side) const {
    const Side& s = sides_[side & 7];
    return s.cash + s.spice;
}

SideEconomy World::economy(int side) const {
    const Side& s = sides_[side & 7];
    SideEconomy e;
    e.credits = s.cash + s.spice;
    e.cash = s.cash;
    e.spice = s.spice;
    e.storage = s.maxStorage;
    e.cashDrip = s.cashDrip;
    e.spiceDrip = s.spiceDrip;
    e.powerOutput = s.powerOutput;
    e.powerDemand = s.powerDemand;
    e.powerPercent = s.powerPercent;
    return e;
}

bool World::radarActive(int side) const {
    const Side& s = sides_[side & 7];
    if (s.powerPercent < 100) return false;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
        if (buildingTemplate(s.buildings[i]).specialBehavior == building_behaviour::kOutpost) return true;
    return false;
}

std::vector<BuildOption> World::buildableBuildings(int side) const {
    std::vector<BuildOption> r;
    const auto& T = *rules_.templates;
    for (int t : sides_[side & 7].buildingIcons)
        r.push_back({t, T.bin.buildings[t].buildingGroup, static_cast<int>(buildingCost(t, 0, side)),
                     T.firstBuildingIconFrame + t, 0});
    return r;
}

std::vector<BuildOption> World::buildableUnits(int side) const {
    std::vector<BuildOption> r;
    const auto& T = *rules_.templates;
    for (int t : sides_[side & 7].unitIcons)
        r.push_back({t, T.bin.units[t].unitGroup, static_cast<int>(unitCost(t, side)), T.firstUnitIconFrame + t, 0});
    return r;
}

// The upgrade sidebar: one entry per building group with upgrades, if it can be upgraded now.
std::vector<BuildOption> World::upgradableBuildings(int side) const {
    std::vector<BuildOption> r;
    const auto& T = *rules_.templates;
    const Side& s = sides_[side & 7];
    std::array<bool, formats::kMaxBuildingTypes> seen{};
    for (int i = 0; i < T.bin.buildingCount; ++i) {
        const int g = T.bin.buildings[i].buildingGroup;
        if (static_cast<std::uint8_t>(T.bin.buildings[i].techLevelUpgrade[0]) == 0xff || seen[g]) continue;
        seen[g] = true;
        if (!canSideUpgradeGroup(side, g)) continue;
        const int t = sideVersionOfBuilding(s, g, false);
        r.push_back({t, g, static_cast<int>(buildingCost(t, s.upgrades[g] + 1, side)), T.firstBuildingIconFrame + t,
                     s.upgrades[g]});
    }
    return r;
}

namespace {
QueueState stateOf(const ProductionSlot& q, int cost, int slot) {
    QueueState st;
    st.slot = slot;
    if (q.type < 0) return st;
    st.active = true;
    st.type = q.type;
    st.progress = q.progress;
    st.fraction = static_cast<float>(q.progress) / static_cast<float>(kBuildDone);
    st.paid = q.paid;
    st.cost = cost;
    st.onHold = q.onHold;
    st.ready = q.progress == kBuildDone;
    return st;
}
}  // namespace

QueueState World::buildingQueue(int side) const {
    const ProductionSlot& q = sides_[side & 7].buildingQueue;
    return stateOf(q, q.type >= 0 ? static_cast<int>(buildingCost(q.type, 0, side)) : 0, -1);
}

std::vector<QueueState> World::unitQueues(int side) const {
    std::vector<QueueState> r;
    const Side& s = sides_[side & 7];
    for (int i = 0; i < 10; ++i)
        if (s.unitQueue[i].type >= 0) r.push_back(stateOf(s.unitQueue[i], static_cast<int>(unitCost(s.unitQueue[i].type, side)), i));
    return r;
}

QueueState World::unitQueueFor(int side, int unitType) const {
    const Side& s = sides_[side & 7];
    for (int i = 0; i < 10; ++i)
        if (s.unitQueue[i].type == unitType && unitType >= 0)
            return stateOf(s.unitQueue[i], static_cast<int>(unitCost(unitType, side)), i);
    return {};
}

QueueState World::upgradeQueue(int side) const {
    const Side& s = sides_[side & 7];
    const ProductionSlot& q = s.upgradeQueue;
    int cost = 0;
    if (q.type >= 0) cost = static_cast<int>(buildingCost(q.type, s.upgrades[rules_.templates->bin.buildings[q.type].buildingGroup] + 1, side));
    return stateOf(q, cost, -1);
}

// Display__DrawPlacementMarkers 0x4285c0 / Display__DrawConcretePlacementMarkers 0x4288d0.
// Every footprint tile (concrete: at least one) must be on the map, not on row 0, buildable
// and free (no unit, building or crate; concrete also no concrete yet), and some own building
// or own concrete must lie within 2 tiles of a footprint tile.
bool World::canPlaceBuilding(int side, int type, int x, int y, std::array<bool, 16>* tileOk) const {
    if (tileOk) tileOk->fill(false);
    if (type < 0 || type >= formats::kMaxBuildingTypes || x < 0) return false;
    if (y < 1) y = 1;  // the cursor row is clamped to 1 (top row never buildable)
    const auto& t = rules_.templates->bin.buildings[type];
    const bool concrete = t.specialBehavior == building_behaviour::kConcrete;
    const std::uint32_t mask = concrete ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
    std::uint8_t status = 0;
    bool offMap = false;
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(mask & bit)) continue;
            const int cx = x + c, cy = y + r;
            if (cx >= width_ || cy >= height_) {
                offMap = true;
                continue;
            }
            const int y0 = std::max(cy - 2, 0), y1 = std::min(cy + 3, height_);
            const int x0 = std::max(cx - 2, 0), x1 = std::min(cx + 3, width_);
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {  // GetOwnershipStatusOfCell 0x428c30
                    if (yy <= 0) continue;
                    const std::uint32_t f = cell(xx, yy).flags;
                    if ((f & kBuilding) && static_cast<int>(f & kOwnerMask) == side) status |= 1;
                    if ((f & kConcrete) && static_cast<int>((f >> kConcreteOwnerShift) & 7) == side) status |= 2;
                }
        }
    const bool near = status != 0 && !offMap;
    bool all = true, any = false;
    bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(mask & bit)) continue;
            const int cx = x + c, cy = y + r;
            bool ok = false;
            if (cx < width_ && cy < height_) {
                const std::uint32_t f = cell(cx, cy).flags;
                ok = (f & (kOccupied | kCrate)) == 0 && (f & kBuildOn) && near && cy >= 1 && (!concrete || !(f & kConcrete));
            }
            if (tileOk) (*tileOk)[static_cast<std::size_t>(r * 4 + c)] = ok;
            all = all && ok;
            any = any || ok;
        }
    return concrete ? any : all;
}

// =======================================================================================
// Public API: orders (with the filters the original sidebar / click handler applies)

bool World::orderBuildBuilding(int side, int type) {
    const Side& s = sides_[side & 7];
    const bool listed = std::find(s.buildingIcons.begin(), s.buildingIcons.end(), type) != s.buildingIcons.end();
    if (!listed && !(s.buildingQueue.type >= 0 && s.buildingQueue.onHold)) return false;
    orders_.push_back({Order::BuildingPick, side, kNoIndex, type, 0});
    return true;
}

bool World::orderCancelBuilding(int side, int type) {
    if (sides_[side & 7].buildingQueue.type != type || type < 0) return false;
    // (the original sidebar also sets the UI flag +0x2652d here; the model never reads it)
    orders_.push_back({Order::BuildingCancel, side, kNoIndex, type, 0});
    return true;
}

bool World::orderBuildUnit(int side, int type) {
    const Side& s = sides_[side & 7];
    if (std::find(s.unitIcons.begin(), s.unitIcons.end(), type) == s.unitIcons.end()) return false;
    orders_.push_back({Order::UnitPick, side, kNoIndex, type, 0});
    return true;
}

bool World::orderCancelUnit(int side, int type) {
    if (type < 0 || type >= formats::kMaxUnitTypes || isSpecialWeapon(rules_.templates->bin.units[type].specialBehavior))
        return false;
    orders_.push_back({Order::UnitCancel, side, kNoIndex, type, 0});
    return true;
}

bool World::orderAutoAdd(int side, int type, int n) {
    const Side& s = sides_[side & 7];
    if (n < 1 || std::find(s.unitIcons.begin(), s.unitIcons.end(), type) == s.unitIcons.end() ||
        isSpecialWeapon(rules_.templates->bin.units[type].specialBehavior))
        return false;
    orders_.push_back({Order::AutoAdd, side, kNoIndex, type, std::min(n, kAutoMaxCount)});
    return true;
}

bool World::orderAutoInfinite(int side, int type) {
    if (type < 0 || type >= formats::kMaxUnitTypes || isSpecialWeapon(rules_.templates->bin.units[type].specialBehavior))
        return false;
    orders_.push_back({Order::AutoInfinite, side, kNoIndex, type, 0});
    return true;
}

bool World::orderAutoRemove(int side, int type) {
    if (type < 0 || type >= formats::kMaxUnitTypes || isSpecialWeapon(rules_.templates->bin.units[type].specialBehavior))
        return false;
    orders_.push_back({Order::AutoRemove, side, kNoIndex, type, 0});
    return true;
}

AutoQueueState World::autoQueueFor(int side, int unitType) const {
    AutoQueueState st;
    if (unitType < 0) return st;
    const Side& s = sides_[side & 7];
    const auto& T = rules_.templates->bin;
    const bool running = slotOfType(s, unitType) != nullptr;
    const int g = T.units[unitType].prereq1BuildingGroup;
    int pos = 0;
    for (const auto& e : s.autoQueue) {
        if (e.type == unitType) {
            st.infinite = e.remaining == kAutoInfinite;
            st.count = (st.infinite ? 0 : e.remaining) + (running ? 1 : 0);
            st.position = pos;
            return st;
        }
        if (T.units[e.type].prereq1BuildingGroup == g) ++pos;
    }
    if (running) {
        st.count = 1;
        st.position = 0;
    }
    return st;
}

bool World::orderUpgrade(int side, int group) {
    if (!canSideUpgradeGroup(side, group)) return false;
    orders_.push_back({Order::UpgradePick, side, kNoIndex, group, 0});
    return true;
}

bool World::orderCancelUpgrade(int side, int buildingType) {
    if (buildingType < 0 || buildingType > 99) return false;
    orders_.push_back({Order::UpgradeCancel, side, kNoIndex, buildingType, 0});
    return true;
}

bool World::orderPlaceBuilding(int side, int type, int tileX, int tileY) {
    const ProductionSlot& q = sides_[side & 7].buildingQueue;
    if (q.type != type || q.progress != kBuildDone) return false;
    if (!canPlaceBuilding(side, type, tileX, tileY)) return false;  // "CANTBUILD"
    if (tileY < 1) tileY = 1;
    orders_.push_back({Order::PlaceBuilding, side, kNoIndex, tileX, tileY, 0, type});
    return true;
}

bool World::orderSetPrimary(int side, int buildingIndex) {
    const Building* b = building(side, buildingIndex);
    if (!b || (b->flags & building_flags::kPrimary)) return false;
    // MoreProductionBuildingsOfSameGroupExist 0x4432f0
    const auto& t = buildingTemplate(*b);
    if (sides_[side].buildingsExistPerGroup[t.buildingGroup] < 2) return false;
    switch (t.specialBehavior) {
    case 2: case 7: case 8: case 9: case 0xb: case 0xc: case 0xd: case 0x11: case 0x12: case 0x13: break;
    default: return false;
    }
    orders_.push_back({Order::SetPrimary, side, buildingIndex, 0, 0});
    return true;
}

bool World::orderRepairBuilding(int side, int buildingIndex) {
    const Building* b = building(side, buildingIndex);
    if (!b) return false;
    const auto& t = buildingTemplate(*b);
    if (!(t.flags & building_flags::kSelectable)) return false;
    // The click handler only starts repairs below full health; stopping is always allowed.
    if (!(b->flags & building_flags::kRepairing) && b->health >= static_cast<std::int32_t>(t.hitPoints)) return false;
    orders_.push_back({Order::RepairBuilding, side, buildingIndex, 0, 0});
    return true;
}

bool World::orderSellBuilding(int side, int buildingIndex) {
    const Building* b = building(side, buildingIndex);
    if (!b || (b->flags & (building_flags::kBuildup | building_flags::kSelling))) return false;
    orders_.push_back({Order::SellBuilding, side, buildingIndex, 0, 0});
    return true;
}

bool World::orderDeploy(int side, int unitIndex) {
    const Unit* u = unit(side, unitIndex);
    if (!u) return false;
    if (unitTemplate(*u).specialBehavior == behaviour::kMcv && !canDeployMcv(side, unitIndex)) {
        uiMentat(side, "CANTDEPLOY", true);
        return false;
    }
    orders_.push_back({Order::Deploy, side, unitIndex, 0, 0});
    return true;
}

bool World::orderSpecialWeapon(int side, int beh, int tileX, int tileY) {
    if (side < 0 || side >= kMaxSides) return false;
    const auto& T = rules_.templates->bin;
    bool ready = false;
    for (const auto& q : sides_[side].unitQueue)
        if (q.type >= 0 && T.units[q.type].specialBehavior == beh && q.progress == kBuildDone) ready = true;
    if (!ready) return false;
    orders_.push_back({Order::SpecialWeapon, side, kNoIndex, tileX & 0xffff, tileY & 0xffff, side, beh});
    return true;
}

bool World::orderRepairUnit(int side, int unitIndex, int repairPadIndex) {
    const Unit* u = unit(side, unitIndex);
    if (!u || u->state == UnitState::Dead || unitTemplate(*u).isInfantry) return false;
    orders_.push_back({Order::RepairUnit, side, unitIndex, 0, 0, side, repairPadIndex & 0xffff});
    return true;
}

bool World::orderDockAtRefinery(int side, int unitIndex, int refineryIndex) {
    const Building* r = building(side, refineryIndex);
    if (!r || buildingTemplate(*r).specialBehavior != building_behaviour::kRefinery || !unit(side, unitIndex)) return false;
    orders_.push_back({Order::DockRefinery, side, unitIndex, 0, 0, side, refineryIndex});
    return true;
}

// GenerateUnitGuardOrder 0x459980: selected, alive, armed (primary weapon) units, max 64.
bool World::orderGuardSelected(int side) {
    if (side < 0 || side >= kMaxSides) return false;
    const Side& s = sides_[side];
    int n = 0;
    for (int i = s.firstUnit; i != -1 && n < 64; i = s.nextObject(i)) {
        const Unit& u = s.units[i];
        if (!u.selected || u.state == UnitState::Dead || unitTemplate(u).primaryWeapon == -1) continue;
        orders_.push_back({Order::Guard, side, i, 0, 0});
        ++n;
    }
    return n != 0;
}

// Retreat button (0x4433a0): needs a selected unit and a base position, then
// GenerateUnitRetreatOrder 0x459ab0 takes the selected, alive units (max 64).
bool World::orderRetreatSelected(int side) {
    if (side < 0 || side >= kMaxSides) return false;
    const Side& s = sides_[side];
    bool any = false;
    for (int i = s.firstUnit; i != -1 && !any; i = s.nextObject(i)) any = s.units[i].selected;
    int x = 0, y = 0;
    if (!any || !findBestBasePosition(side, x, y)) return false;
    int n = 0;
    for (int i = s.firstUnit; i != -1 && n < 64; i = s.nextObject(i)) {
        const Unit& u = s.units[i];
        if (!u.selected || u.state == UnitState::Dead) continue;
        orders_.push_back({Order::Retreat, side, i, x, y});
        ++n;
    }
    return n != 0;
}

bool World::findBestBasePosition(int side, int& ox, int& oy) const {
    const Side& S = sides_[side & 7];
    const int W = width_, H = height_;
    if (S.firstBuilding == -1) {
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
            const Unit& u = S.units[i];
            if (u.flags & unit_flags::kFlying) continue;
            ox = std::min(((u.posX >> 16) / 32) & 0xff, W - 1);
            oy = std::min(((u.posY >> 16) / 32) & 0xff, H - 1);
            return false;
        }
        ox = static_cast<std::uint8_t>((S.baseMaxX - S.baseMinX) / 2 + S.baseMinX);
        oy = static_cast<std::uint8_t>((S.baseMaxY - S.baseMinY) / 2 + S.baseMinY);
        return false;
    }
    int ci = S.primaryBuilding[0];
    if ((ci & 0xffff) == 0xffff) ci = S.firstBuilding;
    const Building& c = S.buildings[ci % kObjectsPerSide];
    const int cx = ((c.posX >> 16) / 32) & 0xff, cy = ((c.posY >> 16) / 32) & 0xff;
    int sx = 0, sy = 0, n = 0;
    for (int i = S.firstBuilding; i != -1; i = S.nextObject(i)) {
        const Building& b = S.buildings[i];
        const int bx = ((b.posX >> 16) / 32) & 0xff, by = ((b.posY >> 16) / 32) & 0xff;
        if ((cx - bx) * (cx - bx) + (cy - by) * (cy - by) < 100) { sx += bx; sy += by; ++n; }
    }
    ox = n == 0 ? cx : (sx / n) & 0xff;
    oy = n == 0 ? cy : (sy / n) & 0xff;
    ox = std::min(ox, W - 1);
    oy = std::min(oy, H - 1);
    return true;
}

}  // namespace d2k::game
