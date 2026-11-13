#pragma once
// Which sidebar build icons the original shows greyed (Display__DrawNormalSideBarMenu 0x426e80;
// re/notes/ui.md "Strips"). Shared by the Original sidebar (ClassicSidebar) and the Enhanced one.

#include "game/world.hpp"

#include <vector>

namespace d2k::render {

// Palace weapons and the air strike (behaviours 4/9/10/12) are built automatically.
inline bool specialBuildUnit(int behaviour) { return behaviour == 4 || behaviour == 9 || behaviour == 10 || behaviour == 12; }

// Buildings: while one building is in production every other building icon is greyed.
inline bool buildingIconGreyed(const game::QueueState& q, int type) { return q.active && q.type != type; }

// Units: "busy" = some unit queue holds a unit with the same prereq1 building group and a
// non-special behaviour, or the unit itself is special. Busy and not queued itself = greyed;
// `mine` = its own queue slot (nullptr if not queued).
inline bool unitIconBusy(const game::World& w, const std::vector<game::QueueState>& queues, int type,
                         const game::QueueState** mine = nullptr) {
    const auto& T = w.rules().templates->bin;
    const auto& ut = T.units[type];
    bool busy = specialBuildUnit(ut.specialBehavior);
    if (mine) *mine = nullptr;
    for (const auto& qs : queues) {
        const auto& qt = T.units[qs.type];
        if (qt.prereq1BuildingGroup == ut.prereq1BuildingGroup && !specialBuildUnit(qt.specialBehavior)) busy = true;
        if (qs.type == type && mine) *mine = &qs;
    }
    return busy;
}

}  // namespace d2k::render
