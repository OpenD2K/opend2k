// Original-mode community pages (not original; built from the original widgets like the custom
// missions page, re/notes/campaign-flow.md §6.4):
//   D2K_COMMUNITY           tabs Campaigns / Maps & missions, a list and an info list
//   D2K_COMMUNITYCAMPAIGN   one campaign: its missions with progress, the briefing, Start
// Reached from the "Community" button of the Select Mission page (loadmis.uil).

#include "platform/community_menu.hpp"
#include "platform/original_menus.hpp"

#include <algorithm>

namespace d2k::platform {

namespace {

formats::UilWidget cloneW(const formats::UilWidget& w, const std::string& name, int x, int y) {
    formats::UilWidget c = w;
    c.name = name;
    c.x = x;
    c.y = y;
    return c;
}

constexpr float kInfoW = 292 - 26;  // info list width less the scroll bar

}  // namespace

const formats::UilPage* OriginalMenus::communityPage(const std::string& menu) const {
    const bool campaign = menu == "D2K_COMMUNITYCAMPAIGN";
    if (menu != "D2K_COMMUNITY" && !campaign) return nullptr;
    if (campaign ? communityCampaignBuilt_ : communityBuilt_) return campaign ? &communityCampaignPage_ : &communityPage_;
    const formats::UilPage* mis = UilPageStack::page("LOADMISSION");
    const formats::UilPage* load = UilPageStack::page("LOAD");
    const formats::UilWidget* title = mis ? mis->find("msg") : nullptr;
    const formats::UilWidget* cancel = mis ? mis->find("cancel") : nullptr;
    const formats::UilWidget* list = load ? load->find("loadListBox") : nullptr;
    if (!title || !cancel || !list) return nullptr;
    formats::UilPage& pg = campaign ? communityCampaignPage_ : communityPage_;
    (campaign ? communityCampaignBuilt_ : communityBuilt_) = true;
    pg = {};
    auto& ws = pg.widgets;
    for (const auto& w : mis->widgets)
        if (w.kind == "BACKGROUND") ws.push_back(w);
    ws.push_back(cloneW(*title, "title", title->x, title->y));
    auto box = [&](const char* name, int x, int y, int w, int h) {
        formats::UilWidget g;
        g.kind = "GROUPBOX";
        g.name = name;
        g.x = x;
        g.y = y;
        g.w = w;
        g.h = h;
        g.fields = {"Black", "DarkRed", "NONE"};
        ws.push_back(g);
    };
    auto listBox = [&](const char* name, int x, int y, int w, int h) {
        formats::UilWidget l = cloneW(*list, name, x, y);
        l.w = w;
        l.h = h;
        l.fields.resize(formats::UilWidget::fixedFields("LISTBOX"));
        ws.push_back(l);
    };
    auto button = [&](const char* name, int x, int y, const std::string& cmd) {
        formats::UilWidget b = cloneW(*cancel, name, x, y);
        b.fields[0] = "NoText";
        b.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        b.fields.push_back(cmd);
        ws.push_back(b);
    };
    const int top = campaign ? 50 : 70;
    if (!campaign) {
        // Tabs: text buttons like Cancel, over the two lists.
        button("tabCampaigns", 22, 46, "D2kCommunityTab 0");
        button("tabMaps", 326, 46, "D2kCommunityTab 1");
        for (auto& w : ws) {  // the wide button art of the settings page's rows
            if (w.name != "tabCampaigns" && w.name != "tabMaps") continue;
            w.fields[7] = "but3_up";
            w.fields[8] = "but3_dn";
        }
    }
    box("listFrame", 18, top, 292, 354 - top);
    box("infoFrame", 322, top, 300, 354 - top);
    listBox("mainList", 22, top + 4, 284, 346 - top);
    listBox("infoList", 326, top + 4, 292, 346 - top);
    ws.push_back(cloneW(*cancel, "cancel", cancel->x, cancel->y));
    button("start", 640 - cancel->x - 70, cancel->y, campaign ? "D2kCommunityStart" : "D2kCommunityGo");
    // Play as any side (not original): a wide button (the tabs' art) centred between Cancel and Start.
    button("playAs", 320 - 69, cancel->y, "D2kCyclePlayAs");
    ws.back().fields[7] = "but3_up";
    ws.back().fields[8] = "but3_dn";
    return &pg;
}

// The mission the "As:" button refers to: the selected custom mission (custom missions page,
// community Maps & Missions tab) or the selected step of a community campaign; null otherwise.
const MissionInfo* OriginalMenus::playAsMission(const std::string& menu) const {
    if (!catalog_) return nullptr;
    auto sel = [&](const char* list) {
        const auto it = states_.find(menu);
        return it == states_.end() || !it->second.lists.count(list) ? -1 : it->second.lists.at(list).sel;
    };
    int mission = -1;
    if (menu == "D2K_CUSTOMMISSIONS") {
        mission = sel("missionList");
    } else if (menu == "D2K_COMMUNITY") {
        if (communityTab_ == 1) mission = sel("mainList");
    } else if (menu == "D2K_COMMUNITYCAMPAIGN") {
        if (communityCampaign_ >= int(catalog_->community.size())) return nullptr;
        static const formats::CommunityProgress kNone;
        const auto rows = communityRows(*catalog_, catalog_->community[std::size_t(communityCampaign_)],
                                        progress_ ? *progress_ : kNone);
        const int r = sel("mainList");
        if (r >= 0 && r < int(rows.size()) && !rows[std::size_t(r)].locked) mission = rows[std::size_t(r)].mission;
    }
    return mission >= 0 && mission < int(catalog_->custom.size()) ? &catalog_->custom[std::size_t(mission)] : nullptr;
}

void OriginalMenus::openCommunity(int tab, int sel) {
    communityTab_ = std::clamp(tab, 0, 1);
    if (communityTab_ == 0) communitySel_ = std::max(0, sel);
    else communityMapSel_ = std::max(0, sel);
    setStack({"MAINMENU", "LOADMISSION", "D2K_COMMUNITY"});
}

void OriginalMenus::openCommunityCampaign(int campaign, int row) {
    communityCampaign_ = std::max(0, campaign);
    communitySel_ = communityCampaign_;
    communityTab_ = 0;
    communityRow_ = row;
    setStack({"MAINMENU", "LOADMISSION", "D2K_COMMUNITY", "D2K_COMMUNITYCAMPAIGN"});
}

void OriginalMenus::enterCommunity(const std::string& menu, UilState& s) {
    if (menu != "D2K_COMMUNITY" && menu != "D2K_COMMUNITYCAMPAIGN") return;
    UilList& l = s.lists["mainList"];
    l = {};
    s.lists["infoList"] = {};
    s.lists["infoList"].selectable = false;
    communityInfoFor_ = -2;
    if (!catalog_) return;
    static const formats::CommunityProgress kNone;
    const formats::CommunityProgress& prog = progress_ ? *progress_ : kNone;
    if (menu == "D2K_COMMUNITY") {
        if (communityTab_ == 0) {
            for (const auto& c : catalog_->community)
                l.rows.push_back(c.name + "  (" + std::to_string(formats::communityWonSteps(c, prog)) + "/" +
                                 std::to_string(formats::communityRequiredSteps(c)) + ")");
            l.sel = l.rows.empty() ? -1 : std::clamp(communitySel_, 0, int(l.rows.size()) - 1);
        } else {
            for (const auto& m : catalog_->custom) l.rows.push_back(m.name.empty() ? m.map : m.name);
            l.sel = l.rows.empty() ? -1 : std::clamp(communityMapSel_, 0, int(l.rows.size()) - 1);
        }
    } else {
        if (communityCampaign_ >= int(catalog_->community.size())) return;
        const auto& c = catalog_->community[std::size_t(communityCampaign_)];
        const auto rows = communityRows(*catalog_, c, prog);
        for (const auto& r : rows) {
            l.rows.push_back(r.status.empty() ? r.text : r.text + "  - " + r.status);
            l.dim.push_back(r.locked);
        }
        if (communityRow_ < 0 || communityRow_ >= int(rows.size())) communityRow_ = communitySuggestedRow(rows, c, prog);
        l.sel = rows.empty() ? -1 : std::clamp(communityRow_, 0, int(rows.size()) - 1);
    }
    l.top = std::max(0, l.sel - 8);
    fillCommunityInfo(menu, s, true);
}

// The info list: campaign description (campaigns tab) or briefing (maps tab / campaign page).
void OriginalMenus::fillCommunityInfo(const std::string& menu, UilState& s, bool force) {
    const int sel = s.lists["mainList"].sel;
    if (!force && sel == communityInfoFor_) return;
    communityInfoFor_ = sel;
    UilList& b = s.lists["infoList"];
    b.rows.clear();
    b.dim.clear();
    b.top = 0;
    b.selectable = false;
    if (!catalog_ || sel < 0) return;
    static const formats::CommunityProgress kNone;
    const formats::CommunityProgress& prog = progress_ ? *progress_ : kNone;
    auto add = [&](const std::string& t, bool dim) {
        for (auto& line : wrapLines(t, kInfoW)) {
            b.rows.push_back(line);
            b.dim.push_back(dim);
        }
    };
    const MissionInfo* m = nullptr;
    std::string state;
    if (menu == "D2K_COMMUNITY" && communityTab_ == 0) {
        if (sel >= int(catalog_->community.size())) return;
        const auto& c = catalog_->community[std::size_t(sel)];
        add(c.name, false);
        add(communityProgressText(c, prog), true);
        b.rows.push_back("");
        b.dim.push_back(false);
        for (const auto& l : communityCampaignInfo(*catalog_, c)) add(l, false);
        return;
    }
    if (menu == "D2K_COMMUNITY") {
        if (sel >= int(catalog_->custom.size())) return;
        m = &catalog_->custom[std::size_t(sel)];
    } else {
        if (communityCampaign_ >= int(catalog_->community.size())) return;
        const auto rows = communityRows(*catalog_, catalog_->community[std::size_t(communityCampaign_)], prog);
        if (sel >= int(rows.size())) return;
        m = &catalog_->custom[std::size_t(rows[std::size_t(sel)].mission)];
        state = rows[std::size_t(sel)].locked ? "Locked - win the previous mission first" : rows[std::size_t(sel)].status;
    }
    add(m->name.empty() ? m->map : m->name, false);
    add(m->author.empty() ? m->map : "by " + m->author + "  (" + m->map + ")", true);
    if (!state.empty()) add(state, true);
    b.rows.push_back("");
    b.dim.push_back(false);
    add(communityBriefingText(*m), false);
}

void OriginalMenus::communityOverrides(const std::string& menu, UilOverrides& ov) {
    if (menu != "D2K_COMMUNITY" && menu != "D2K_COMMUNITYCAMPAIGN") return;
    const auto it = states_.find(menu);
    const int sel = it == states_.end() || !it->second.lists.count("mainList") ? -1 : it->second.lists.at("mainList").sel;
    static const formats::CommunityProgress kNone;
    const formats::CommunityProgress& prog = progress_ ? *progress_ : kNone;
    if (menu == "D2K_COMMUNITY") {
        ov.text["title"] = "COMMUNITY CAMPAIGNS & MAPS";
        // The active tab in brackets.
        ov.text["tabCampaigns"] = communityTab_ == 0 ? "[ Campaigns ]" : "Campaigns";
        ov.text["tabMaps"] = communityTab_ == 1 ? "[ Maps & Missions ]" : "Maps & Missions";
        ov.text["start"] = communityTab_ == 0 ? "Open" : "Start";
        const int n = catalog_ ? int(communityTab_ == 0 ? catalog_->community.size() : catalog_->custom.size()) : 0;
        if (sel < 0 || sel >= n) ov.disabled.insert("start");
        return;
    }
    if (!catalog_ || communityCampaign_ >= int(catalog_->community.size())) {
        ov.disabled.insert("start");
        return;
    }
    const auto& c = catalog_->community[std::size_t(communityCampaign_)];
    std::string t = c.name;
    for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    ov.text["title"] = t + "  (" + std::to_string(formats::communityWonSteps(c, prog)) + "/" +
                       std::to_string(formats::communityRequiredSteps(c)) + ")";
    ov.text["start"] = "Start";
    const auto rows = communityRows(*catalog_, c, prog);
    if (sel < 0 || sel >= int(rows.size()) || rows[std::size_t(sel)].locked) ov.disabled.insert("start");
}

bool OriginalMenus::communityCommand(const std::string& menu, const std::string& op, const std::string& arg) {
    (void)menu;
    if (op == "D2kCommunityTab") {
        UilState& s = state("D2K_COMMUNITY");
        const int sel = s.lists["mainList"].sel;
        if (communityTab_ == 0) communitySel_ = std::max(0, sel);
        else communityMapSel_ = std::max(0, sel);
        communityTab_ = arg == "1" ? 1 : 0;
        enterCommunity("D2K_COMMUNITY", s);
        return true;
    }
    if (op == "D2kCommunityGo") {
        const int sel = state("D2K_COMMUNITY").lists["mainList"].sel;
        if (!catalog_ || sel < 0) return true;
        if (communityTab_ == 0) {
            if (sel < int(catalog_->community.size())) {
                communitySel_ = sel;
                communityCampaign_ = sel;
                communityRow_ = -1;
                jump("D2K_COMMUNITYCAMPAIGN");
            }
        } else if (sel < int(catalog_->custom.size())) {
            communityMapSel_ = sel;
            action_.kind = OriginalMenuAction::CustomMission;
            action_.slot = sel;
        }
        return true;
    }
    if (op == "D2kCommunityStart") {
        const int sel = state("D2K_COMMUNITYCAMPAIGN").lists["mainList"].sel;
        if (!catalog_ || communityCampaign_ >= int(catalog_->community.size())) return true;
        static const formats::CommunityProgress kNone;
        const auto rows = communityRows(*catalog_, catalog_->community[std::size_t(communityCampaign_)],
                                        progress_ ? *progress_ : kNone);
        if (sel < 0 || sel >= int(rows.size()) || rows[std::size_t(sel)].locked) return true;
        communityRow_ = sel;
        action_.kind = OriginalMenuAction::CommunityMission;
        action_.slot = rows[std::size_t(sel)].mission;
        action_.campaign = communityCampaign_;
        return true;
    }
    return false;
}

void OriginalMenus::communityEvent(const std::string& menu, const UilEvents& ev, UilState& s) {
    if (menu != "D2K_COMMUNITY" && menu != "D2K_COMMUNITYCAMPAIGN") return;
    if (ev.selected == "mainList" || !ev.activated.empty()) fillCommunityInfo(menu, s, false);
    if (ev.activated == "mainList") communityCommand(menu, menu == "D2K_COMMUNITY" ? "D2kCommunityGo" : "D2kCommunityStart", "");
}

}  // namespace d2k::platform
