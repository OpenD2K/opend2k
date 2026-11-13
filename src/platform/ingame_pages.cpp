#include "platform/ingame_pages.hpp"

#include "platform/settings.hpp"
#include "platform/ui.hpp"

#include <algorithm>

namespace d2k::platform {

InGamePages::InGamePages(SDL_Renderer* r, Ui& ui, Settings& settings) : UilPageStack(r, ui, settings) {}

void InGamePages::setup(const std::string& dataDir, const formats::StringTable* text, bool skirmish,
                        const std::string& recapFile, const std::string& briefing) {
    res_.setData(dataDir, text);
    optionsBuilt_ = false;
    menuPageFor_.clear();
    skirmish_ = skirmish;
    recapFile_ = recapFile;
    briefing_ = briefing;
}

void InGamePages::openMenu() { setStack({skirmish_ ? "INGAME_SKIRMISH" : "INGAME"}); }
void InGamePages::openReplayPrompt() { setStack({"REPLAY_MISSION"}); }

void InGamePages::openDialog(const std::string& menu) {
    setStack({});
    jump(menu);  // LOAD without saves -> NO_SAVED_GAMES
}

bool InGamePages::openByName(const std::string& name) {
    const std::string base = skirmish_ ? "INGAME_SKIRMISH" : "INGAME";
    if (name == "pause") setStack({base});
    else if (name == "pause-options") setStack({base, "OPTIONS"});
    else if (name == "pause-save") setStack({base, "SAVE"});
    else if (name == "pause-load") { setStack({base}); jump("LOAD"); }
    else if (name == "pause-delete") { setStack({base}); jump("DELETEGAME"); }
    else if (name == "pause-abort") setStack({base, "CONFIRMABORT"});
    else if (name == "pause-restart") setStack({base, "CONFIRMABORT", "REALLY_RESTART"});
    else if (name == "pause-briefing") setStack({base, "RECAP"});
    else if (name == "replay" || name == "pause-replay") setStack({"REPLAY_MISSION"});
    else return false;
    return true;
}

std::string InGamePages::pageFile(const std::string& menu) const {
    if (menu == "RECAP") return recapFile_;  // CInterface__Create: IG<h><n>MIS.UID
    return UilPageStack::pageFile(menu);
}

// Not original (re/notes/campaign-flow.md §6.4): with Settings::cheats on, the in-game menu gets a
// "Cheats" button (the original button art) that opens our cheat menu below Resume; the buttons
// move closer together (INGAME 22 px apart, INGAME_SKIRMISH's smaller box 21 px, instead of 25).
const formats::UilPage* InGamePages::page(const std::string& menu) const {
    if ((menu != "INGAME" && menu != "INGAME_SKIRMISH") || !settings_.cheats) return UilPageStack::page(menu);
    if (menuPageFor_ == menu) return &menuPage_;
    const formats::UilPage* base = UilPageStack::page(menu);
    const formats::UilWidget* abort = base ? base->find("abort") : nullptr;
    if (!abort) return base;
    menuPage_ = *base;
    menuPageFor_ = menu;
    formats::UilWidget b = *abort;
    b.name = "cheats";
    b.fields[0] = "NoText";  // text from overrides()
    b.fields[7] = "but4_up";
    b.fields[8] = "but4_dn";
    b.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
    b.fields.push_back("D2kCheats");
    const int top = menu == "INGAME" ? 108 : 106, step = menu == "INGAME" ? 22 : 21;
    int i = 0;
    for (auto& w : menuPage_.widgets)
        if (w.kind == "BUTTON") w.y = top + step * i++;
    b.y = top + step * i;
    menuPage_.widgets.push_back(b);
    return &menuPage_;
}

bool InGamePages::special(const std::string& menu) {
    if (menu == "ABORT_MP_GAME" || menu == "LOADMISSION") {  // leave the game (FUN_004738b0 remaps)
        action_.kind = InGameAction::Abort;
        return true;
    }
    if (menu == "LOAD" || menu == "DELETEGAME") {  // no saves -> NO_SAVED_GAMES (FUN_004738b0 case 5)
        bool any = false;
        for (const SaveSlot& s : listSaveSlots()) any = any || s.used;
        if (!any) {
            jump("NO_SAVED_GAMES");
            return true;
        }
    }
    return false;
}

void InGamePages::listSaves(UilList& l, bool all) {
    slots_.clear();
    std::int64_t newest = INT64_MIN;
    for (const SaveSlot& sl : listSaveSlots()) {
        if (!all && !sl.used) continue;
        if (sl.valid && sl.meta.savedAt > newest && !all) {
            newest = sl.meta.savedAt;
            l.sel = int(slots_.size());
        }
        slots_.push_back(sl);
        l.rows.push_back(sl.used ? slotTitle(sl) + "  " + slotDetail(sl) : slotTitle(sl));
        l.dim.push_back(sl.used && !sl.valid);
        l.white.push_back(sl.valid && !isCampaignMapSave(sl));  // FUN_00475a00: battle saves in White
    }
    if (l.sel < 0 && !slots_.empty()) l.sel = 0;
}

std::vector<std::string> InGamePages::wrap(const std::string& s, float w) const { return wrapLines(s, w); }

void InGamePages::enter(const std::string& menu, UilState& s) {
    if (menu == "LOAD") {
        listSaves(s.lists["loadListBox"], false);
    } else if (menu == "DELETEGAME") {
        listSaves(s.lists["deleteListBox"], false);
    } else if (menu == "SAVE") {
        // LISTBOXEMPTYSLOT: every slot (empty ones too); the first empty one preselected, the
        // description typed into the edit box (selecting a used slot copies its name).
        UilList& l = s.lists["saveListBox"];
        listSaves(l, true);
        l.sel = 0;
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].used) { l.sel = int(i); break; }
        s.focus = "saveEdit";
    } else if (menu == "OPTIONS") {
        enterOptions(s);
    } else if (menu == "RECAP") {
        // The briefing list box: its "EnterText" keys (title, then the IG_ text), wrapped.
        const formats::UilPage* p = page(menu);
        if (!p) return;
        for (const auto& w : p->widgets) {
            if (w.kind != "LISTBOX") continue;
            UilList& l = s.lists[w.name];
            l.selectable = false;
            for (const auto& c : w.commands()) {
                if (c.rfind("EnterText ", 0) != 0) continue;
                std::string t = res_.text(c.substr(10));
                if (t.empty()) t = briefing_;  // custom missions (IG?0MIS: the patch's text)
                if (!l.rows.empty()) l.rows.push_back("");
                for (auto& line : wrap(t, float(w.w) - 26)) l.rows.push_back(line);
            }
        }
    }
}

void InGamePages::overrides(const std::string& menu, UilOverrides& ov) {
    if (menu == "INGAME" || menu == "INGAME_SKIRMISH") {
        if (!canSave) ov.disabled.insert("saveMission");
        if (recapFile_.empty() || !res_.page(recapFile_)) ov.disabled.insert("briefing");
        ov.text["cheats"] = "Cheats";
        if (!gameRunning) ov.disabled.insert("cheats");
    } else if (menu == "OPTIONS") {
        ov.hidden.insert("Debug");
        optionsOverrides(ov);
        ov.disabled.insert("music");  // the music selection is a front-end page here
    } else if (menu == "LOAD") {
        const UilList& l = state(menu).lists["loadListBox"];
        if (l.sel < 0 || l.sel >= int(slots_.size()) || !slots_[std::size_t(l.sel)].valid) ov.disabled.insert("loadGame");
    } else if (menu == "SAVE") {
        if (!canSave) ov.disabled.insert("saveGame");
    }
}

void InGamePages::doSave() {
    std::string err;
    if (pendingSave_ >= 0 && saveGame && saveGame(pendingSave_, pendingName_, err)) {
        action_.kind = InGameAction::Resume;  // back to the game (UISaveGame FUN_00474e20)
        stack_.clear();
        navigated_ = true;
    } else {
        SDL_Log("Save failed: %s", err.c_str());
    }
}

void InGamePages::command(const std::string& menu, const std::string& op, const std::string& arg) {
    (void)arg;
    if (op == "UILoadGame") {
        const UilList& l = state(menu).lists["loadListBox"];
        if (l.sel >= 0 && l.sel < int(slots_.size()) && slots_[std::size_t(l.sel)].valid) {
            action_.kind = InGameAction::Load;
            action_.slot = slots_[std::size_t(l.sel)].index;
        }
    } else if (op == "ParseEditBox") {
        // Save button (UICheckSaveGame): a description is required; a used slot asks first.
        UilState& s = state(menu);
        const UilList& l = s.lists["saveListBox"];
        pendingName_ = s.edits["saveEdit"];
        pendingSave_ = l.sel >= 0 && l.sel < int(slots_.size()) ? slots_[std::size_t(l.sel)].index : -1;
        if (pendingName_.empty()) jump("SAVE_DESC");
        else if (pendingSave_ >= 0 && slots_[std::size_t(l.sel)].used) jump("CONFIRM_OVERWRITE");
        else doSave();
    } else if (op == "UISaveGame") {
        doSave();
    } else if (op == "JumpIfSelected") {
        const UilList& l = state(menu).lists["deleteListBox"];
        if (l.sel >= 0 && l.sel < int(slots_.size())) {
            pendingDelete_ = slots_[std::size_t(l.sel)].index;
            jump("CONFIRM_DELETE");
        }
    } else if (op == "DeleteGame") {
        std::string err;
        if (pendingDelete_ >= 0 && !deleteSaveSlot(pendingDelete_, err)) SDL_Log("Delete failed: %s", err.c_str());
        pendingDelete_ = -1;
        back();  // DELETEGAME again, listing what is left
        bool any = false;
        for (const SaveSlot& sl : listSaveSlots()) any = any || sl.used;
        if (!any) back();  // nothing left to delete: the menu
    } else if (op == "RestartGame") {
        action_.kind = InGameAction::Restart;
    } else if (op == "D2kCheats") {
        action_.kind = InGameAction::Cheats;
    } else if (op == "D2kCycleResolution") {
        cycleResolution();  // Session::layout picks it up at the next frame (new classicTarget_)
    }
}

void InGamePages::widgetEvent(const std::string& menu, const UilEvents& ev, UilState& s) {
    if (menu == "OPTIONS") {
        applyOptions(ev, s);
    } else if (menu == "LOAD" && !ev.activated.empty()) {
        command(menu, "UILoadGame", "");
    } else if (menu == "SAVE" && ev.selected == "saveListBox") {
        // CopyToEditBox saveEdit
        const UilList& l = s.lists["saveListBox"];
        if (l.sel >= 0 && l.sel < int(slots_.size()) && slots_[std::size_t(l.sel)].valid)
            s.edits["saveEdit"] = slots_[std::size_t(l.sel)].meta.name;
    }
}

InGameAction InGamePages::frame(bool enhanced) {
    action_ = {};
    res_.setSmooth(enhanced);
    const UiStage st = UiStage::fit(r_, enhanced);
    drawPages(st);
    return action_;
}

}  // namespace d2k::platform
