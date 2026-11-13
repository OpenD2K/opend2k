// Mission events and conditions, win/lose (re/notes/mission-events.md):
// Mission__LoadObjectives 0x44f8e0 (event/condition tables from the .MIS), Mission__CheckEvents
// 0x453b90, the event executor FUN_00454120, EvaluateIfBuildingsOrUnitsExistForSide 0x455290,
// Map__SetDeadOrder 0x44c520 / Map__PlayerDefeated 0x44c4f0, UncloakRemainingStealthUnit
// 0x455480 and the win/lose block of the main loop (Game__MainLoopFrame 0x448550).
// Only the vanilla event/condition types exist here; extended .MIS files (community patch
// 1.06p+) run the community event system instead (mission_ext.cpp, docs/extended-events.md).

#include "game/world.hpp"

#include <cstring>

namespace d2k::game {

namespace {
std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
}  // namespace

GameEnd World::gameEndState() const {
    if (win_) return GameEnd::Won;
    if (lose_) return GameEnd::Lost;
    return GameEnd::None;
}

// Mission__LoadObjectives (binary .MIS path): the event (64 x 72 bytes) and condition
// (48 x 28 bytes) tables, their counts and _gTimerValue (the .MIS "time limit" dword).
void World::loadMissionEvents(const formats::MisFile& mission) {
    win_ = lose_ = false;
    events_.clear();
    conditions_.clear();
    for (const auto& r : mission.events) {
        MisEvent e;
        e.x = r[0];
        e.y = r[4];
        e.value = rd32(&r[8]);
        e.numConditions = r[12];
        e.type = r[13];
        e.amount = r[14];
        e.side = r[15];
        e.arg2 = r[16];
        e.arg3 = r[17];
        e.arg4 = r[18];
        for (int i = 0; i < 12; ++i) {
            e.conditionIndex[i] = r[19 + i];
            e.conditionNot[i] = r[33 + i];
        }
        for (int i = 0; i < 25; ++i) e.data[i] = r[47 + i];
        events_.push_back(e);
    }
    for (const auto& r : mission.conditions) {
        MisCondition c;
        c.val2 = rd32(&r[0]);
        c.val1 = rd32(&r[4]);
        c.value = rd32(&r[8]);
        c.coordX = rd32(&r[12]);
        c.coordY = rd32(&r[16]);
        const std::uint32_t f = rd32(&r[20]);
        static_assert(sizeof(float) == 4);
        std::memcpy(&c.floatVal, &f, 4);
        c.side = r[24];
        c.type = r[25];
        c.arg1 = r[26];
        c.arg2 = r[27];
        conditions_.push_back(c);
    }
    timerValue_ = mission.timeLimit;
    ext_ = ExtMission{};
    if (mission.extended) extLoad(mission);
}

// CSide_return_credits (return-credits-to-spice-storage.c): refunds of cancelled builds and
// starport orders. Rule returnCreditsToSpiceStorage puts them into free spice storage first;
// without it (the default) this is CSide__AddCash.
void World::returnCredits(int side, std::int32_t amount) {
    Side& s = sides_[side & 7];
    if (!rulesExt().returnCreditsToSpiceStorage) {
        s.cashDrip += amount;
        return;
    }
    const std::int32_t freeStorage = s.maxStorage - (s.spice + s.spiceDrip);
    if (amount <= freeStorage) {
        s.spiceDrip += amount;
    } else {
        s.spiceDrip += freeStorage;
        s.cashDrip += amount - freeStorage;
    }
}

// EvaluateIfBuildingsOrUnitsExistForSide 0x455290.
// Buildings (units = false): any building that is not a wall -> gBuildingsExist[side].
// Units (units = true): any non-flying unit that is not a sandworm -> gUnitsExist[side]; also
// the skirmish "only infantry / only non-combat left" flags (_persideflags_6B8258: bit 0 = no
// armed vehicle, bit 1 = no armed infantry; both cleared by anything with a weapon or the
// MCV's type (groupIds[15])), computed only when the side has no buildings.
// Returns 1 when nothing exists.
bool World::evaluateExists(int side, bool units) {
    Side& s = sides_[side & 7];
    const auto& T = rules_.templates->bin;
    if (!units) {
        bool none = true;
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
            if (T.buildings[s.buildings[i].type].specialBehavior != building_behaviour::kWall) {
                none = false;
                break;
            }
        buildingsExist_[side & 7] = none ? 0 : 1;
        return none;
    }
    bool none = true;
    std::uint8_t& fl = persideFlags_[side & 7];
    fl = 3;
    const int mcvType = static_cast<std::uint8_t>(T.groupIds[group_ids::kMcv]);  // DAT_006d60e4._3_1_
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        const Unit& u = s.units[i];
        const auto& ut = T.units[u.type];
        if ((u.flags & unit_flags::kFlying) || ut.specialBehavior == behaviour::kSandworm) continue;
        none = false;
        if (buildingsExist_[side & 7] != 0) {
            fl = 0;
        } else if (!ut.isInfantry) {
            const std::uint8_t old = fl;
            fl = old & 0xFD;
            if (ut.primaryWeapon != -1 || u.type == mcvType) fl = old & 0xFC;
        } else {
            const std::uint8_t old = fl;
            fl = old & 0xFE;
            if (ut.primaryWeapon != -1) fl = old & 0xFC;
        }
    }
    unitsExist_[side & 7] = none ? 0 : 1;
    return none;
}

// FUN_0044f770 (end of Game_Init): for i = 0..15, side i/2, buildings (even i) / units (odd i).
void World::evaluateAllExists() {
    for (int i = 0; i < 16; ++i) evaluateExists(i >> 1, (i & 1) != 0);
}

// Map__PlayerDefeated 0x44c4f0: record the order in which sides were defeated.
void World::playerDefeated(int side) {
    if (defeatOrder_[side & 7] == -1) defeatOrder_[side & 7] = defeatCount_++;
}

// Map__SetDeadOrder 0x44c520 (end of every tick).
void World::setDeadOrder() {
    for (int s = 0; s < kMaxSides; ++s)
        if (!buildingsExist_[s] && !unitsExist_[s]) playerDefeated(s);
}

// CSide__ResetEnemyForSide 0x46eb00: units/buildings of `side` targeting `target` drop it.
void World::resetEnemyForSide(int side, int target) {
    Side& s = sides_[side & 7];
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        Unit& u = s.units[i];
        if ((u.enemySide & 0xFF) == (target & 0xFF) && u.enemyIndex != kNoIndex) {
            u.enemySide = 0xFF;
            u.enemyIndex = kNoIndex;
        }
    }
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        Building& b = s.buildings[i];
        if ((b.enemySide & 0xFF) == (target & 0xFF) && b.enemyIndex != kNoIndex) {
            b.enemySide = 0xFF;
            b.enemyIndex = kNoIndex;
        }
    }
}

// Mission__CheckEvents 0x453b90 (main loop, every tick after the AI).
void World::checkEvents() {
    const int evalSide = (gameTicks_ & 0xF) >> 1;
    // The low bit of gGameTicks picks buildings (even ticks) or units (odd ticks).
    evaluateExists(evalSide, (gameTicks_ & 1) != 0);
    if (!skirmish && ext_.active) {
        extCheckEvents();  // Mod__HandleConditionsAndEvents (community event system)
        if (playAs()) checkPlayAsEnd();
        return;
    }
    if (!skirmish) {
        // Campaign: evaluate every condition, then run every event whose conditions all differ
        // from their "not" flags.
        std::array<std::uint8_t, 48> result{};
        for (std::size_t ci = 0; ci < conditions_.size() && ci < result.size(); ++ci) {
            MisCondition& c = conditions_[ci];
            std::uint8_t& r = result[ci];
            r = 0;
            switch (c.type) {
            case 0: {  // Building Exists (arg1 = building type)
                const Side& s = sides_[c.side & 7];
                for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
                    if (s.buildings[i].type == c.arg1) {
                        r = 1;
                        break;
                    }
                break;
            }
            case 1: {  // Unit Exists (arg2 = unit type)
                const Side& s = sides_[c.side & 7];
                for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
                    if (s.units[i].type == c.arg2) r = 1;
                break;
            }
            case 2:  // Interval: val1 counts down from val2; `value` = remaining repeats
                if (c.val1 == 0) {
                    if (c.value != 0) {
                        r = 1;
                        --c.value;
                        c.val1 = c.val2;
                    }
                } else {
                    --c.val1;
                }
                break;
            case 3: {  // Timer: val1 counts up every tick; arg2 = comparison against val2
                const std::uint32_t t = ++c.val1;
                switch (c.arg2) {
                case 0: r = c.val2 < t; break;
                case 1: r = t < c.val2; break;
                case 2: r = t == c.val2; break;
                case 3: r = c.val2 != 0 && t % c.val2 == 0; break;  // (the original divides by val2 unchecked)
                default: break;
                }
                break;
            }
            case 4: {  // Casualties: units lost > value and lost/killed ratio > floatVal
                const Side& s = sides_[c.side & 7];
                const std::uint32_t lost = static_cast<std::uint32_t>(s.unitsLost);
                if (c.value < lost) {
                    const std::uint32_t killed = static_cast<std::uint32_t>(s.unitsKilled);
                    const float ratio = killed == 0 ? static_cast<float>(lost)
                                                    : static_cast<float>((lost * 100) / killed) * 0.01f;
                    if (c.floatVal < ratio) r = 1;
                }
                break;
            }
            case 5: r = buildingsExist_[c.side & 7] == 0; break;  // Base Destroyed
            case 6: r = unitsExist_[c.side & 7] == 0; break;      // Units Destroyed
            case 7:  // Tile Revealed: `value` = remaining triggers
                if (c.value != 0 && inBounds(static_cast<int>(c.coordX), static_cast<int>(c.coordY)) &&
                    cell(static_cast<int>(c.coordX), static_cast<int>(c.coordY)).shroud == 0) {
                    r = 1;
                    --c.value;
                }
                break;
            case 8: {  // Spice Harvested: the local player's credits >= value
                // (play as another side: the side the mission was written for)
                const Side& me = sides_[scriptSide() & 7];
                if (c.value <= static_cast<std::uint32_t>(me.spice + me.cash)) r = 1;
                break;
            }
            case 9: r = c.value != 0; break;  // Flag
            default: break;                   // DebugFatal "Unknown event type in CheckEvent"
            }
        }
        for (const MisEvent& e : events_) {
            bool run = true;
            for (int k = 0; k < e.numConditions && k < 12; ++k) {
                const int ci = e.conditionIndex[k];
                const std::uint8_t res = ci < static_cast<int>(result.size()) ? result[ci] : 0;
                if (res == e.conditionNot[k]) run = false;
            }
            if (run) executeEvent(e);
        }
        if (playAs()) checkPlayAsEnd();
        return;
    }

    // Skirmish / multiplayer: default rules.
    bool sideWithBase = false, hostilesLeft = false;
    std::uint8_t onlyFlags = 0xFF;  // local_46
    for (int s = 0; s < kMaxSides; ++s) {
        if (persideFlags_[s] == 0 || buildingsExist_[s])
            sideWithBase = true;
        else if (unitsExist_[s])
            onlyFlags &= static_cast<std::uint8_t>(~persideFlags_[s]);
        if (unitsExist_[s] || buildingsExist_[s]) {
            for (int o = 0; o < kMaxSides; ++o)
                if ((unitsExist_[o] || buildingsExist_[o]) && (diplomacy_[s][o] != 0 || diplomacy_[o][s] != 0))
                    hostilesLeft = true;
        }
    }
    if (!hostilesLeft) gameOver_ = true;
    if (!unitsExist_[mySide_ & 7] && !buildingsExist_[mySide_ & 7]) {
        if (!win_) lose_ = true;
    } else if (gameOver_ && !lose_) {
        win_ = true;
    }
    // (_NumCratesAllowed == 0 in our model: crates are not implemented)
    if (!sideWithBase && !gameOver_) {
        if (onlyFlags == 0xFD || onlyFlags == 0xFE) {
            gameOver_ = true;
            if (mySide_ == evalSide)
                win_ = true;
            else
                lose_ = true;
        }
    }
}

// FUN_00454120 cases 0/1 (Reinforcement / Starport Delivery): fill the first free delivery
// slot; updateDeliveries sends the carryall (frigate) when its time has come. Also used by the
// cheat reinforcements (cheats.cpp).
int World::startReinforcement(int side, int x, int y, const std::uint8_t* units, int count, int deployAction, int type) {
    Side& s = sides_[side & 7];
    int i = 0;
    while (i < 10 && s.deliveries[i].active) ++i;
    if (i >= 10) return -1;  // DebugFatal "Too many deliveries"
    Delivery& d = s.deliveries[i];
    d.active = true;
    d.started = false;
    d.x = x;
    d.y = y;
    d.time = gameTicks_;
    d.deployAction = deployAction;
    d.type = type;
    d.entryEdge = -1;  // (not original field)
    int n = 0;
    for (; n < count && n < 39; ++n) d.units[n] = units[n];
    d.units[n] = 0xFF;
    return i;
}

// FUN_00454120: execute one event.
void World::executeEvent(const MisEvent& e) {
    const int side = e.side & 7;
    Side& s = sides_[side];
    switch (e.type) {
    case 1:  // Starport Delivery: only with a primary starport
        if (s.primaryBuilding[7] == kNoIndex) return;
        [[fallthrough]];
    case 0:  // Reinforcement (carryall)
        startReinforcement(side, e.x, e.y, e.data.data(), std::min<int>(e.amount, 25), e.arg4, e.type == 0 ? 1 : 2);
        return;
    case 2:  // Allegiance: diplomacy[side][arg2] = arg3; ally/neutral drop current targets
        diplomacy_[side][e.arg2 & 7] = static_cast<std::uint8_t>(e.arg3);
        if (e.arg3 == 0 || e.arg3 == 2) resetEnemyForSide(side, e.arg2);
        return;
    case 4: ai_[side].b(0x1E) = 1; return;                 // Berserk (CAI __GoBeserk)
    case 6: ai_[side].set<std::uint32_t>(0x1D2C, e.value); return;  // Set Build Rate (UnitBuildRate)
    case 7: ai_[side].set<std::uint32_t>(0xF14, e.value); return;   // Set Attack Building Rate
    // Set Cash: only NetPlayersStartingCredits (read by CSide::InitCash and ActivateAI), not the
    // side's current cash.
    case 8: s.startingCredits = e.value; return;
    case 9: s.techLevel = e.value & 0xFF; return;                   // Set Tech (NetPlayersTechLevel)
    case 10: missionOutcome(true); return;                  // Mission Win (if !gLose: gWin = 1)
    case 11: missionOutcome(false); return;                 // Mission Fail (if !gWin: gLose = 1)
    case 14: revealCircle(e.x, e.y, e.amount); return;      // Reveal Map (RevealCircle, radius <= 7)
    case 15: timerValue_ = static_cast<std::int32_t>(e.value); return;  // Set Timer
    case 16: timerValue_ = -1; return;                      // Hide Timer
    case 17:                                                // Show Message
        speech("S_CHATMSG", false);
        messages_.push_back({static_cast<int>(rd32(&e.data[21])), gameTicks_});
        return;
    case 18:  // Unit Spawn: each unit at the nearest free tile (radius 12) to (x, y)
        for (int n = 0; n < e.amount && n < 25; ++n) {
            int x = e.x, y = e.y;
            nearestFreeTile(x, y, 12);
            addUnit(side, e.data[n], x, y, x, y, 0, 0);
        }
        return;
    case 19:  // Set Flag: condition[side byte].value = value (the side byte is a condition index)
        if (e.side < static_cast<int>(conditions_.size())) conditions_[e.side].value = e.value;
        return;
    case 20: return;  // (no-op in vanilla)
    default: return;  // DebugFatal "Unknown event type %d" (3, 5, 12, 13 and the extended types)
    }
}

// --- Play as any side (not original; docs/play-as.md) -------------------------------------

// Mission setup when the human plays `mySide_` in a mission written for `scriptSide_`: the
// chosen side has no computer player; the mission's side gets one from its own .MIS AI block
// (the editors store a full parameter set for every side, with the enabled byte 0 for the
// player), or the InitAI defaults when that block is empty. Credits, tech level, houses and
// diplomacy stay as the mission defines them.
void World::setupPlayAs(const formats::MisFile& mission) {
    ai_[mySide_ & 7].b(1) = 0;
    const int s = scriptSide_ & 7;
    int used = 0;
    if (s < static_cast<int>(mission.aiSegments.size()))
        for (const std::uint8_t v : mission.aiSegments[static_cast<std::size_t>(s)]) used += v != 0;
    if (used < 64) aiDefaults(s);
    ai_[s].b(0) = static_cast<std::uint8_t>(s);
    ai_[s].b(1) = 1;
}

bool World::playAsHostile(int other) const {
    const int me = mySide_ & 7, o = other & 7;
    return o != me && (diplomacy_[me][o] == 1 || diplomacy_[o][me] == 1);
}

// Mission Win / Mission Fail events are written for the mission's own player. Played as another
// side, an event decides for that side: the same outcome when the two are not enemies (allies or
// neutral, e.g. the Fremen in an Atreides mission), the opposite when they are (the Atreides
// mission failing is a Harkonnen win). A normal game: gWin / gLose as the original.
void World::missionOutcome(bool scriptWin) {
    bool win = scriptWin;
    if (playAs() && playAsHostile(scriptSide_)) win = !win;
    if (win) {
        if (!lose_) win_ = true;
    } else if (!win_) {
        lose_ = true;
    }
}

// Elimination rules on top of the (remapped) mission events, like the skirmish rules: the human
// side loses when it has no buildings and no ground units left (gBuildingsExist / gUnitsExist),
// and wins when no side hostile to it has any left (once at least one hostile side was seen with
// objects, so a mission whose enemies arrive later does not end at once).
void World::checkPlayAsEnd() {
    const int me = mySide_ & 7;
    bool hostileLeft = false;
    for (int o = 0; o < kMaxSides; ++o) {
        if (!playAsHostile(o) || (!unitsExist_[o] && !buildingsExist_[o])) continue;
        hostileLeft = true;
        playAsHostiles_ = static_cast<std::uint8_t>(playAsHostiles_ | (1u << o));
    }
    if (!unitsExist_[me] && !buildingsExist_[me]) {
        if (!win_) lose_ = true;
    } else if (!hostileLeft && playAsHostiles_ != 0) {
        if (!lose_) win_ = true;
    }
}

// UncloakRemainingStealthUnit 0x455480 (main loop after Mission__CheckEvents): a side with
// no buildings whose remaining ground units are all cloaked has the first of them decloaked.
void World::uncloakRemainingStealthUnit() {
    if (!rulesExt().uncloakRemainingStealthUnit) return;  // rule (uncloak-remaining-unit-rule.asm)
    const int side = gameTicks_ & 7;
    if (buildingsExist_[side]) return;
    Side& s = sides_[side];
    bool cloaked = false, visible = false;
    Unit* first = nullptr;
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        Unit& u = s.units[i];
        if ((u.flags & unit_flags::kFlying) || unitTemplate(u).specialBehavior == behaviour::kSandworm) continue;
        if (!(u.flags & unit_flags::kCloaked)) {
            visible = true;
        } else {
            cloaked = true;
            if (!first) first = &u;
        }
    }
    if (cloaked && !visible) unitDecloak(*first);
}

// Main loop after Map__SetDeadOrder: the first time gWin / gLose is seen, the mission-won /
// mission-failed speech (the original also starts the end-of-mission UI; network-only steps
// such as RevealMap and the draw logic are not modelled).
void World::updateGameEnd() {
    if (endHandled_) return;
    if (win_) {
        endHandled_ = true;
        mentat(mySide_, "MISSIONWON", true);  // FUN_004762f0("S_MISSIONWON")
    } else if (lose_) {
        endHandled_ = true;
        mentat(mySide_, "MISSIONFAILED", true);
    }
}

}  // namespace d2k::game
