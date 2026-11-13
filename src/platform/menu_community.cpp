// Enhanced (modern) community pages: Campaigns / Maps & missions tabs and a campaign's mission
// list with progress (not original; docs/community-campaigns.md). Graphics: Original shows the
// same through the original widgets (original_community.cpp).

#include "platform/menu.hpp"
#include "platform/ui.hpp"
#include "render/ui_text.hpp"

#include <algorithm>

namespace d2k::platform {

using render::tr;

namespace {

// "by <author>" etc.: a pattern with one %s filled with a string of any length.
std::string trs(const char* english, const std::string& arg) {
    const std::string pat = tr(english);
    const auto at = pat.find("%s");
    const std::string a = uiUtf8(arg);
    return at == std::string::npos ? pat + " " + a : pat.substr(0, at) + a + pat.substr(at + 2);
}

}  // namespace

void Menu::loadCommunityProgress() {
    progress_ = {};
    if (persistent_) {
        progress_ = formats::CommunityProgress::load(communityProgressPath());
        return;
    }
    // Screenshot runs: sample progress (the first two steps of every campaign won).
    for (const auto& c : catalog_.community)
        for (std::size_t s = 0; s < c.steps.size() && s < 2; ++s)
            if (!c.steps[s].stems.empty()) progress_.markWon(c.id, c.steps[s].stems[0]);
}

void Menu::saveCommunityProgress() {
    if (persistent_) progress_.save(communityProgressPath());
}

MenuResult Menu::pageCommunity() {
    MenuResult res;
    backdrop(back_, 0.4f);
    title(tr("Community campaigns & maps"), 24, 14);
    const auto& camps = catalog_.community;
    // Tabs.
    const char* tabs[2] = {tr("Campaigns"), tr("Maps & missions")};
    for (int t = 0; t < 2; ++t) {
        if (ui_.button(st_.r(20 + t * 128, 44, 122, 20), tabs[t], communityTab_ == t ? ButtonStyle::Primary : ButtonStyle::Subtle)) {
            communityTab_ = t;
            listScroll_ = 0;
        }
    }
    const SDL_FRect listR = st_.r(20, 70, 300, 286);
    const SDL_FRect info = st_.r(332, 70, 288, 286);
    float x = info.x + 14 * st_.s, y = info.y + 12 * st_.s;
    if (communityTab_ == 0) {
        std::vector<ListRow> rows;
        for (const auto& c : camps)
            rows.push_back({c.name, std::to_string(formats::communityWonSteps(c, progress_)) + "/" +
                                        std::to_string(formats::communityRequiredSteps(c))});
        communitySel_ = std::clamp(communitySel_, 0, std::max(0, int(camps.size()) - 1));
        const bool activated = ui_.list(listR, rows, communitySel_, listScroll_) >= 0;
        ui_.panel(info);
        const formats::CommunityCampaign* c =
            communitySel_ >= 0 && communitySel_ < int(camps.size()) ? &camps[std::size_t(communitySel_)] : nullptr;
        if (c) {
            const int lh = c->side >= 0 && c->side < 3 ? c->side : -1;
            if (lh >= 0 && logo_[lh]) ui_.image(logo_[lh].get(), {info.x + info.w - 64 * st_.s, y, 52 * st_.s, 55 * st_.s});
            const SDL_FRect nameR{x, y, info.w - 92 * st_.s, 40 * st_.s};
            ui_.wrapped(c->name, nameR, theme::kTitle, ui_.fitScale(c->name, nameR, {ts_ + 1, ts_}));
            y = info.y + 72 * st_.s;
            ui_.text(communityProgressText(*c, progress_, true), x, y, theme::kAccent, ts_);
            y += ui_.textHeight(ts_) + 8 * ts_;
            std::string body;
            for (const auto& l : communityCampaignInfo(catalog_, *c, true)) body += l + "\n";
            const SDL_FRect textR{x, y, info.w - 28 * st_.s, info.y + info.h - y - 10 * st_.s};
            ui_.wrapped(body, textR, theme::kText, ts_);
        } else {
            ui_.wrapped(tr("No community campaigns found in data/Missions."), SDL_FRect{x, y, info.w - 28 * st_.s, 60 * st_.s},
                        theme::kDim, ts_);
        }
        if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Main);
        if (ui_.button(st_.r(476, 366, 144, 22), tr("Open campaign"), ButtonStyle::Primary, c != nullptr) || (activated && c)) {
            communityRowSel_ = -1;
            communityScroll_ = 0;
            open(MenuPage::CommunityCampaign);
        }
        return res;
    }
    // Maps & missions: every community mission (the former custom missions list).
    const auto& list = catalog_.custom;
    std::vector<ListRow> rows;
    rows.reserve(list.size());
    for (const auto& m : list) rows.push_back({m.name, m.author.empty() ? m.stem : m.author});
    int& sel = customSel_;
    const bool activated = ui_.list(listR, rows, sel, listScroll_) >= 0 && sel >= 0 && sel < int(list.size());
    ui_.panel(info);
    if (sel >= 0 && sel < int(list.size())) {
        const MissionInfo& m = list[std::size_t(sel)];
        const int lh = m.house >= 0 && m.house < 3 ? m.house : -1;
        if (lh >= 0 && logo_[lh]) ui_.image(logo_[lh].get(), {info.x + info.w - 64 * st_.s, y, 52 * st_.s, 55 * st_.s});
        const SDL_FRect nameR{x, y, info.w - 92 * st_.s, 40 * st_.s};
        ui_.wrapped(m.name, nameR, theme::kTitle, ui_.fitScale(m.name, nameR, {ts_ + 1, ts_}));
        y = info.y + 58 * st_.s;
        std::string sub = uiUtf8(m.map);
        if (!m.author.empty()) sub += "  -  " + trs("by %s", m.author);
        ui_.text(ui_.ellipsize(sub, info.w - 28 * st_.s, ts_), x, y, theme::kDim, ts_);
        y = info.y + 76 * st_.s;
        const SDL_FRect textR{x, y, info.w - 28 * st_.s, info.y + info.h - y - 10 * st_.s};
        const std::string txt = communityBriefingText(m, true);
        ui_.wrapped(txt, textR, theme::kText, ui_.fitScale(txt, textR, {ui_.textScale, ts_}));
    } else {
        ui_.text(tr("No missions found."), x, y, theme::kDim, ts_);
    }
    if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Main);
    if (ui_.button(st_.r(476, 366, 144, 22), tr("Briefing"), ButtonStyle::Primary, sel >= 0 && sel < int(list.size())) || activated) {
        briefingFromCustom_ = true;
        briefingFromCommunity_ = false;
        open(MenuPage::Briefing);
    }
    return res;
}

MenuResult Menu::pageCommunityCampaign() {
    MenuResult res;
    backdrop(back_, 0.45f);
    const auto& camps = catalog_.community;
    if (communitySel_ < 0 || communitySel_ >= int(camps.size())) {
        open(MenuPage::Community);
        return res;
    }
    const formats::CommunityCampaign& c = camps[std::size_t(communitySel_)];
    title(c.name, 24, 14);
    const SDL_FRect sub = st_.r(24, 46, 0, 0);
    ui_.text((c.author.empty() ? std::string() : trs("by %s", c.author) + "   ") + communityProgressText(c, progress_, true),
             sub.x, sub.y,
             theme::kDim, ts_);
    const std::vector<CommunityRow> crow = communityRows(catalog_, c, progress_, true);
    if (communityRowSel_ < 0 || communityRowSel_ >= int(crow.size())) {
        communityRowSel_ = communitySuggestedRow(crow, c, progress_);
        communityScroll_ = 0;
    }
    std::vector<ListRow> rows;
    for (const auto& r : crow) rows.push_back({r.text, r.status, !r.locked});
    const SDL_FRect listR = st_.r(20, 64, 300, 292);
    const SDL_FRect info = st_.r(332, 64, 288, 292);
    const bool activated = ui_.list(listR, rows, communityRowSel_, communityScroll_) >= 0;
    const CommunityRow* r =
        communityRowSel_ >= 0 && communityRowSel_ < int(crow.size()) ? &crow[std::size_t(communityRowSel_)] : nullptr;
    ui_.panel(info);
    float x = info.x + 14 * st_.s, y = info.y + 12 * st_.s;
    if (r) {
        const MissionInfo& m = catalog_.custom[std::size_t(r->mission)];
        const int lh = m.house >= 0 && m.house < 3 ? m.house : -1;
        if (lh >= 0 && logo_[lh]) ui_.image(logo_[lh].get(), {info.x + info.w - 64 * st_.s, y, 52 * st_.s, 55 * st_.s});
        const SDL_FRect nameR{x, y, info.w - 92 * st_.s, 40 * st_.s};
        ui_.wrapped(m.name, nameR, theme::kTitle, ui_.fitScale(m.name, nameR, {ts_ + 1, ts_}));
        y = info.y + 58 * st_.s;
        const std::string state = r->locked ? std::string(tr("Locked - win the previous mission first")) : r->status;
        ui_.text(ui_.ellipsize(state.empty() ? m.map : state, info.w - 28 * st_.s, ts_), x, y,
                 r->locked ? theme::kBad : theme::kAccent, ts_);
        y = info.y + 76 * st_.s;
        const SDL_FRect textR{x, y, info.w - 28 * st_.s, info.y + info.h - y - 10 * st_.s};
        const std::string txt = communityBriefingText(m, true);
        ui_.wrapped(txt, textR, theme::kText, ui_.fitScale(txt, textR, {ui_.textScale, ts_}));
    }
    if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) {
        communityTab_ = 0;
        open(MenuPage::Community);
    }
    const bool canGo = r && !r->locked;
    if (ui_.button(st_.r(476, 366, 144, 22), tr("Briefing"), ButtonStyle::Primary, canGo) || (activated && canGo)) {
        customSel_ = r->mission;
        briefingFromCustom_ = true;
        briefingFromCommunity_ = true;
        open(MenuPage::Briefing);
    }
    return res;
}

}  // namespace d2k::platform
