#include "platform/original_menus.hpp"

#include "formats/io.hpp"
#include "platform/language.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace d2k::platform {

namespace {

constexpr const char* kDifficultyChecks[3] = {"easyCheck", "normalCheck", "hardCheck"};
constexpr const char* kHouseChecks[3] = {"atreidesCheck", "harkonnenCheck", "ordosCheck"};
// Skirmish.uil colour boxes and their gNetPlayerColour values (no box for 4 and 7: the yellow
// colours the AI skips too, World::assignSkirmishColours).
constexpr std::pair<const char*, int> kColourChecks[] = {{"colourRedCheck", 0},   {"colourGreenCheck", 1},
                                                         {"colourBlueCheck", 2},  {"colourGreyCheck", 3},
                                                         {"colourPurpleCheck", 5}, {"colourBlackCheck", 6}};
const char* const kScoreIcon[3] = {"mp_at", "mp_hk", "mp_or"};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A copy of a widget with a new name / position / text.
formats::UilWidget clone(const formats::UilWidget& w, const std::string& name, int x, int y) {
    formats::UilWidget c = w;
    c.name = name;
    c.x = x;
    c.y = y;
    return c;
}

}  // namespace

OriginalMenus::OriginalMenus(SDL_Renderer* r, Ui& ui, Settings& settings) : UilPageStack(r, ui, settings) {}

void OriginalMenus::setData(const std::string& dataDir, const Catalog* catalog) {
    dataDir_ = dataDir;
    catalog_ = catalog;
    res_.setData(dataDir, catalog ? &catalog->text : nullptr);
    settingsBuilt_ = false;
    loadMisBuilt_ = customBuilt_ = languageBuilt_ = false;
    communityBuilt_ = communityCampaignBuilt_ = false;
    customSel_ = 0;
    briefFor_ = -1;
    tracks_.clear();
    credits_.clear();
    if (dataDir.empty()) return;
    // Sound page: the music files (the original lists its CD tracks, GetCDTracks).
    if (const std::string dir = formats::findFileNoCase(dataDir, "MUSIC"); !dir.empty()) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const std::string f = e.path().filename().string();
            if (lower(e.path().extension().string()) == ".aud") tracks_.push_back(lower(e.path().stem().string()));
            (void)f;
        }
        std::sort(tracks_.begin(), tracks_.end());
    }
    // CreateCredits 0x43d2a0: the Text.UIB entries from CreditsStart to CreditsEnd, after a
    // screen of empty lines ((CreditsScreenBottom - Top) / (10 + 5) + 1).
    if (catalog) {
        const auto& e = catalog->text.entries;
        std::size_t a = e.size(), b = e.size();
        for (std::size_t i = 0; i < e.size(); ++i) {
            if (e[i].first == "CreditsStart") a = i;
            if (e[i].first == "CreditsEnd") { b = i; break; }
        }
        if (a < b && b < e.size()) {
            credits_.assign((350 - 50) / 15 + 1, std::string());
            for (std::size_t i = a; i <= b; ++i) credits_.push_back(e[i].second);
        }
    }
}

std::string OriginalMenus::pageFile(const std::string& menu) const { return UilPageStack::pageFile(menu); }

// setting.uil with our rows added (see re/notes/campaign-flow.md §6.4).
const formats::UilPage* OriginalMenus::page(const std::string& menu) const {
    if (const formats::UilPage* p = communityPage(menu)) return p;  // D2K_COMMUNITY*
    if (menu == "LOADMISSION") {
        // loadmis.uil; Gruntmods' Survival button slot (top left, hidden here) becomes our
        // "Custom Missions" button with the same art.
        if (loadMisBuilt_) return &loadMisPage_;
        const formats::UilPage* base = UilPageStack::page(menu);
        if (!base) return nullptr;
        loadMisBuilt_ = true;
        loadMisPage_ = *base;
        if (const formats::UilWidget* sv = base->find("Survival")) {
            formats::UilWidget b = clone(*sv, "customMissions", sv->x, sv->y);
            b.fields[0] = "NoText";
            b.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
            b.fields.push_back("JumpToMenu D2K_COMMUNITY");
            loadMisPage_.widgets.push_back(b);
        }
        return &loadMisPage_;
    }
    if (menu == "D2K_LANGUAGE") {
        // Our language page (not original): loadmis.uil's background, title and Cancel, Load.uil's
        // list box for the languages | what the selected one gives, and a Select button.
        if (languageBuilt_) return &languagePage_;
        const formats::UilPage* mis = UilPageStack::page("LOADMISSION");
        const formats::UilPage* load = UilPageStack::page("LOAD");
        const formats::UilWidget* title = mis ? mis->find("msg") : nullptr;
        const formats::UilWidget* cancel = mis ? mis->find("cancel") : nullptr;
        const formats::UilWidget* list = load ? load->find("loadListBox") : nullptr;
        if (!title || !cancel || !list) return nullptr;
        languageBuilt_ = true;
        languagePage_ = {};
        auto& ws = languagePage_.widgets;
        for (const auto& w : mis->widgets)
            if (w.kind == "BACKGROUND") ws.push_back(w);
        ws.push_back(clone(*title, "title", title->x, title->y));
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
        box("listFrame", 18, 50, 232, 304);
        box("infoFrame", 262, 50, 360, 304);
        auto listBox = [&](const char* name, int x, int y, int w, int h) {
            formats::UilWidget l = clone(*list, name, x, y);
            l.w = w;
            l.h = h;
            l.fields.resize(formats::UilWidget::fixedFields("LISTBOX"));
            ws.push_back(l);
        };
        listBox("langList", 22, 54, 224, 296);
        listBox("langInfo", 266, 54, 352, 296);
        ws.push_back(clone(*cancel, "cancel", cancel->x, cancel->y));
        formats::UilWidget st = clone(*cancel, "select", 640 - cancel->x - 70, cancel->y);
        st.fields[0] = "NoText";
        st.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        st.fields.push_back("D2kApplyLanguage");
        ws.push_back(st);
        // Russian text: Fargus alone or with our fixes (Settings::russianTextFixes), toggled.
        formats::UilWidget fx = clone(*cancel, "ruText", 320 - 35, cancel->y);  // between Cancel and Select (70 wide)
        fx.fields[0] = "NoText";
        fx.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        fx.fields.push_back("D2kToggleRuText");
        ws.push_back(fx);
        return &languagePage_;
    }
    if (menu == "D2K_CUSTOMMISSIONS") {
        // Built from loadmis.uil (background, title text, Cancel) and Load.uil (the list box).
        if (customBuilt_) return &customPage_;
        const formats::UilPage* mis = UilPageStack::page("LOADMISSION");
        const formats::UilPage* load = UilPageStack::page("LOAD");
        const formats::UilWidget* title = mis ? mis->find("msg") : nullptr;
        const formats::UilWidget* cancel = mis ? mis->find("cancel") : nullptr;
        const formats::UilWidget* list = load ? load->find("loadListBox") : nullptr;
        if (!title || !cancel || !list) return nullptr;
        customBuilt_ = true;
        customPage_ = {};
        auto& ws = customPage_.widgets;
        for (const auto& w : mis->widgets)
            if (w.kind == "BACKGROUND") ws.push_back(w);
        formats::UilWidget t = clone(*title, "title", title->x, title->y);
        ws.push_back(t);
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
        box("listFrame", 18, 50, 292, 304);
        box("briefFrame", 322, 50, 300, 304);
        auto listBox = [&](const char* name, int x, int y, int w, int h) {
            formats::UilWidget l = clone(*list, name, x, y);
            l.w = w;
            l.h = h;
            l.fields.resize(formats::UilWidget::fixedFields("LISTBOX"));
            ws.push_back(l);
        };
        listBox("missionList", 22, 54, 284, 296);
        listBox("briefList", 326, 54, 292, 296);
        formats::UilWidget c = clone(*cancel, "cancel", cancel->x, cancel->y);
        ws.push_back(c);
        formats::UilWidget st = clone(*cancel, "start", 640 - cancel->x - 70, cancel->y);
        st.fields[0] = "NoText";
        st.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        st.fields.push_back("D2kStartCustomMission");
        ws.push_back(st);
        formats::UilWidget pa = clone(*cancel, "playAs", 320 - 69, cancel->y);  // between Cancel and Start
        pa.fields[0] = "NoText";
        pa.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        pa.fields[7] = "but3_up";  // the wide art (community tabs)
        pa.fields[8] = "but3_dn";
        pa.fields.push_back("D2kCyclePlayAs");
        ws.push_back(pa);
        return &customPage_;
    }
    if (menu != "SETTINGS") return UilPageStack::page(menu);
    if (settingsBuilt_) return &settingsPage_;
    const formats::UilPage* base = UilPageStack::page(menu);
    if (!base) return nullptr;
    settingsBuilt_ = true;
    settingsPage_ = *base;
    auto& ws = settingsPage_.widgets;
    const formats::UilWidget* box = settingsPage_.find("8bit");
    const formats::UilWidget* label = settingsPage_.find("8bitText");
    const formats::UilWidget* done = settingsPage_.find("done");
    if (!box || !label || !done) return &settingsPage_;
    const formats::UilWidget boxW = *box, labelW = *label, doneW = *done;
    // The two colour-depth rows (hidden, see overrides) give way to our rows with the same tick /
    // cross boxes (16 px apart instead of 20 to fit the box), then the Graphics preset button
    // (Original / Enhanced / 3D, Custom when mixed), Resolution and the other buttons.
    int row = 0;
    auto place = [&](formats::UilWidget& w, bool isLabel) {
        w.y = 194 + row * 16 + (isLabel ? 1 : 0);
        if (isLabel) {
            w.suffix = "_L";
            w.x = 280;
            w.w = 130;
        }
    };
    const char* extra[] = {"fullscreen", "smoothMotion", "modernMouse", "cutscenes"};
    row = 0;
    for (const char* name : extra) {
        formats::UilWidget c = clone(boxW, name, boxW.x, 0);
        place(c, false);
        c.fields.resize(formats::UilWidget::fixedFields("CHECKBOX"));
        c.fields.push_back("SetValueBFTo gD2k TOGGLE");
        ws.push_back(c);
        formats::UilWidget t = clone(labelW, std::string(name) + "Text", 280, 0);
        place(t, true);
        ws.push_back(t);
        ++row;
    }
    auto button = [&](const char* name, int y, const char* gfx, const char* cmd) {
        formats::UilWidget b = clone(doneW, name, doneW.x, y);
        b.fields[0] = "NoText";
        b.fields[7] = std::string(gfx) + "_up";
        b.fields[8] = std::string(gfx) + "_dn";
        b.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
        b.fields.push_back(cmd);
        ws.push_back(b);
    };
    button("graphics", 260, "but3", "D2kCycleGraphics");
    // Resolution and our Art Pack button (the downloadable OpenD2K art pack) share a row.
    button("resolution", 282, "But8", "D2kCycleResolution");
    button("artPack", 282, "But8", "JumpToMenu D2K_ARTPACK");
    // Game Settings and our Language page (D2K_LANGUAGE) as half-width But8 buttons.
    button("gameSettings", 304, "But8", "JumpToMenu OPTIONS");
    button("language", 304, "But8", "JumpToMenu D2K_LANGUAGE");
    // Last row: our data-folder button and Done, both as half-width But8 buttons.
    button("dataFolder", 326, "But8", "JumpToMenu D2K_DATAFOLDER");
    for (auto& w : ws) {
        if (w.name == "artPack") { w.x = 240; w.w = 80; }
        if (w.name == "resolution") { w.x = 321; w.w = 80; }
        if (w.name == "gameSettings") { w.x = 240; w.w = 80; }
        if (w.name == "language") { w.x = 321; w.w = 80; }
        if (w.name == "dataFolder") { w.x = 240; w.w = 80; }
        if (w.name == "done") {
            w.y = 326;
            w.x = 321;
            w.w = 80;
            w.fields[7] = "But8_up";
            w.fields[8] = "But8_dn";
        }
    }
    return &settingsPage_;
}

bool OriginalMenus::special(const std::string& menu) {
    if (menu == "HOUSESELECTION") {
        action_.kind = OriginalMenuAction::Campaign;
        return true;
    }
    if (menu == "LOBBY_SCREEN") {  // FUN_004738b0 case 1: GameType 1 -> SKIRMISH
        jump("SKIRMISH");
        return true;
    }
    if (menu == "09L_Page_4") {  // Gruntmods' "Select Mission" target: the mission select page
        jump("LOADMISSION");
        return true;
    }
    if (menu == "LOAD") {  // FUN_004738b0 case 5: no saved games -> NO_SAVED_GAMES
        bool any = false;
        for (const SaveSlot& s : listSaveSlots()) any = any || s.used;
        if (!any) {
            jump("NO_SAVED_GAMES");
            return true;
        }
    }
    if (menu == "D2K_DATAFOLDER") {
        action_.kind = OriginalMenuAction::DataDir;
        return true;
    }
    if (menu == "D2K_ARTPACK") {
        action_.kind = OriginalMenuAction::ArtPack;
        return true;
    }
    return false;
}

std::string OriginalMenus::resolution() const { return settings_.resolution; }
void OriginalMenus::setResolution(const std::string& r) { settings_.resolution = r; }

void OriginalMenus::stackEmpty() { setStack({"MAINMENU"}); }

void OriginalMenus::enter(const std::string& menu, UilState& s) {
    enterCommunity(menu, s);
    if (menu == "MAINMENU") {
        music_.clear();  // the main menu plays options.aud again (FUN_00473130)
    } else if (menu == "DIFFICULTY" || menu == "DIFFICULTY2") {
        for (int i = 0; i < 3; ++i) s.checks[kDifficultyChecks[i]] = settings_.skirmishDifficulty == i;
    } else if (menu == "SKIRMISH") {
        enterSkirmish(s);
    } else if (menu == "SETTINGS") {
        enterSettings(s);
    } else if (menu == "D2K_LANGUAGE") {
        UilList& l = s.lists["langList"];
        l = {};
        const int cp = formats::lang::activeCodepage();
        for (formats::lang::Id id : formats::lang::kAll) {
            l.rows.push_back(formats::lang::displayName(id, cp));
            l.dim.push_back(!formats::lang::availability(id, languageSources()).any());
            if (settings_.language == formats::lang::code(id)) l.sel = int(l.rows.size()) - 1;
        }
        if (l.sel < 0) l.sel = 0;
        languageInfoFor_ = -1;
        fillLanguageInfo(s);
    } else if (menu == "OPTIONS") {
        enterOptions(s);
    } else if (menu == "SOUNDOPTIONS") {
        UilList& l = s.lists["musicList"];
        l.rows = tracks_;
        l.sel = 0;
        for (std::size_t i = 0; i < tracks_.size(); ++i)
            if (tracks_[i] == music_) l.sel = int(i);
    } else if (menu == "LOAD") {
        slots_.clear();
        UilList& l = s.lists["loadListBox"];
        std::int64_t newest = INT64_MIN;
        for (const SaveSlot& sl : listSaveSlots()) {
            if (!sl.used) continue;
            if (sl.valid && sl.meta.savedAt > newest) {
                newest = sl.meta.savedAt;
                l.sel = int(slots_.size());
            }
            slots_.push_back(sl);
            l.rows.push_back(slotTitle(sl) + "  " + slotDetail(sl));
            l.dim.push_back(!sl.valid);
            l.white.push_back(sl.valid && !isCampaignMapSave(sl));  // FUN_00475a00: battle saves in White
        }
        if (l.sel < 0 && !slots_.empty()) l.sel = 0;
    } else if (menu == "D2K_CUSTOMMISSIONS") {
        UilList& l = s.lists["missionList"];
        if (catalog_)
            for (const auto& m : catalog_->custom) l.rows.push_back(m.name.empty() ? m.map : m.name);
        l.sel = l.rows.empty() ? -1 : std::clamp(customSel_, 0, int(l.rows.size()) - 1);
        l.top = std::max(0, l.sel - 8);
        s.lists["briefList"].selectable = false;
        briefFor_ = -1;
        fillCustomBriefing(s);
    } else if (menu == "CREDITS") {
        creditsPos_ = 0;
        creditsLast_ = 0;
        creditsEndAt_ = 0;
    }
}

// The briefing list of the custom missions page: name, author / map, then the briefing text.
void OriginalMenus::fillCustomBriefing(UilState& s) {
    const int sel = s.lists["missionList"].sel;
    if (sel == briefFor_) return;
    briefFor_ = sel;
    UilList& b = s.lists["briefList"];
    b.rows.clear();
    b.dim.clear();
    b.top = 0;
    b.selectable = false;
    if (!catalog_ || sel < 0 || sel >= int(catalog_->custom.size())) return;
    const MissionInfo& m = catalog_->custom[std::size_t(sel)];
    constexpr float kW = 292 - 26;  // list width less the scroll bar
    for (auto& line : wrapLines(m.name.empty() ? m.map : m.name, kW)) b.rows.push_back(line);
    std::string sub = m.map;
    if (!m.author.empty()) sub = "by " + m.author + "  (" + m.map + ")";
    for (auto& line : wrapLines(sub, kW)) {
        b.rows.push_back(line);
        b.dim.resize(b.rows.size(), false);
        b.dim.back() = true;
    }
    b.rows.push_back("");
    for (auto& line : wrapLines(m.briefing.empty() ? std::string("(No briefing text.)") : m.briefing, kW))
        b.rows.push_back(line);
    b.dim.resize(b.rows.size(), false);
}

// The language page's right list: what the selected language gives and from where.
void OriginalMenus::fillLanguageInfo(UilState& s) {
    namespace lang = formats::lang;
    const int sel = s.lists["langList"].sel;
    if (sel == languageInfoFor_) return;
    languageInfoFor_ = sel;
    UilList& b = s.lists["langInfo"];
    b = {};
    b.selectable = false;
    if (sel < 0 || sel >= lang::kCount) return;
    const lang::Id id = lang::kAll[std::size_t(sel)];
    const lang::Sources& src = languageSources();
    const lang::Availability a = lang::availability(id, src);
    constexpr float kW = 352 - 26;
    auto add = [&](const std::string& line, bool dim) {
        for (auto& l : wrapLines(line, kW)) {
            b.rows.push_back(l);
            b.dim.push_back(dim);
        }
    };
    add(lang::displayName(id, lang::activeCodepage()), false);
    add("", false);
    add(std::string("Text and fonts: ") + (a.text ? "yes" : "no (English)"), !a.text);
    add(std::string("Voices: ") + (a.voices ? "yes" : "no (English)"), !a.voices);
    add(std::string("Cutscenes: ") + (a.movies ? "yes" : "no (English)"), !a.movies);
    if (!a.needs.empty()) {
        add("", false);
        add(a.needs, true);
    }
    add("", false);
    if (lang::isRussian(id))
        add("Translation folder: " + (src.translation.root.empty() ? std::string("none (Data Folder page)") : src.translation.root), true);
    if (lang::isRussian(id))
        add(std::string("Text fixes (button below): ") + (settings_.russianTextFixes ? "on" : "off") + ", " +
                (src.ruFixesDir.empty() ? std::string("none found (artpacks/lang/ru-fixes)") : src.ruFixesDir),
            true);
    if (id == lang::Id::RussianAi)
        add("AI voices: " + (src.aiVoicesDir.empty() ? "none at " + src.aiVoicesRoot + " (tools/voice/gen_ru_ai.py)" : src.aiVoicesDir) +
                "; missing lines play in English",
            true);
    if (id == lang::Id::RussianPs1)
        add("PS1 disc image: " + (src.ps1Image.empty() ? std::string("none (Data Folder page)") : src.ps1Image), true);
    add("Text, voices and cutscenes can be mixed in the Enhanced Options (Advanced).", true);
}

void OriginalMenus::openCustomMissions(int sel) {
    customSel_ = std::max(0, sel);
    setStack({"MAINMENU", "LOADMISSION", "D2K_CUSTOMMISSIONS"});
}

void OriginalMenus::enterSkirmish(UilState& s) {
    s.edits["nameEdit"] = settings_.playerName;
    s.focus = "nameEdit";  // EDITBOXFOCUS
    UilList& maps = s.lists["mapList"];
    if (catalog_) {
        for (std::size_t i = 0; i < catalog_->skirmish.size(); ++i) {
            maps.rows.push_back(catalog_->skirmish[i].name);
            if (catalog_->skirmish[i].stem == settings_.skirmishMap) maps.sel = int(i);
        }
    }
    if (maps.sel < 0 && !maps.rows.empty()) maps.sel = 0;
    const int visible = 7;
    maps.top = std::max(0, maps.sel - visible / 2);
    s.values["unitCountBar"] = settings_.skirmishUnits;
    s.values["techLevelBar"] = settings_.skirmishTech;
    s.values["creditBar"] = settings_.skirmishCredits;
    s.values["aiBar"] = settings_.skirmishAi;
    s.values["wormsCheck"] = settings_.skirmishWorms;
    s.checks["crateCheck"] = settings_.skirmishCrates;
    for (int i = 0; i < 3; ++i) s.checks[kHouseChecks[i]] = settings_.skirmishHouse == i;
    for (int i = 0; i < 3; ++i) s.checks[kDifficultyChecks[i]] = settings_.skirmishDifficulty == i;
    for (const auto& [name, c] : kColourChecks) s.checks[name] = settings_.skirmishColour == c;
}

void OriginalMenus::enterSettings(UilState& s) {
    s.checks["8bit"] = !settings_.enhanced();
    s.checks["16bit"] = settings_.enhanced();
    s.checks["fullscreen"] = settings_.fullscreen;
    s.checks["smoothMotion"] = settings_.smoothMotion;
    s.checks["modernMouse"] = settings_.controls == Settings::Controls::Modern;
    s.checks["cutscenes"] = settings_.cutscenes;
}

const SkirmishMap* OriginalMenus::selectedMap() const {
    if (!catalog_) return nullptr;
    auto it = states_.find("SKIRMISH");
    if (it == states_.end()) return nullptr;
    auto l = it->second.lists.find("mapList");
    if (l == it->second.lists.end()) return nullptr;
    const int sel = l->second.sel;
    return sel >= 0 && sel < int(catalog_->skirmish.size()) ? &catalog_->skirmish[std::size_t(sel)] : nullptr;
}

void OriginalMenus::overrides(const std::string& menu, UilOverrides& ov) {
    communityOverrides(menu, ov);
    playAsOverrides(menu, ov);
    if (menu == "MAINMENU") {
        ov.hidden.insert("startNewGame");  // CInterface__Create removes it (debug builds only)
    } else if (menu == "MULTIPLAYERTYPE") {
        for (const char* b : {"serialModem", "network", "internet"}) ov.disabled.insert(b);  // no network play
    } else if (menu == "SKIRMISH") {
        ov.hidden.insert("tooltip");
        ov.text["sideName"] = kHouseNames[std::clamp(settings_.skirmishHouse, 0, 2)];
        // GetStringIfValue..Init gNetCrates / gNetWorms
        ov.text["crates"] = res_.text(settings_.skirmishCrates ? "CratesOn" : "CratesOff");
        static const char* const kWorms[4] = {"WormsOff", "WormsLow", "WormsMedium", "WormsHigh"};
        ov.text["worms"] = res_.text(kWorms[std::clamp(settings_.skirmishWorms, 0, 3)]);
        const SkirmishMap* m = selectedMap();
        if (m) ov.text["chosenMap"] = m->name;
        else ov.disabled.insert("start");
    } else if (menu == "SETTINGS") {
        for (const char* w : {"8bit", "8bitText", "16bit", "16bitText"}) ov.hidden.insert(w);  // the Graphics button instead
        {
            static const char* const kPreset[] = {"Original", "Enhanced", "3D", "Custom"};
            const int cp = formats::lang::activeCodepage();
            const int p = std::clamp(static_cast<int>(settings_.activePreset()), 0, 3);
            ov.text["graphics"] = formats::lang::toCodepage(std::string(render::tr("Graphics")) + ": " + render::tr(kPreset[p]), cp);
        }
        ov.text["fullscreenText"] = "Fullscreen";
        ov.text["smoothMotionText"] = "Smooth unit motion";
        ov.text["modernMouseText"] = "Modern mouse";
        ov.text["cutscenesText"] = "Cutscenes";
        // Half-width: the value alone ("Auto", "1280x800") after a short label.
        ov.text["resolution"] = ui_.ellipsize("Res: " + Settings::resolutionLabel(resolution()), 74, 1);
        ov.text["artPack"] = ui_.ellipsize(formats::lang::toCodepage(render::tr("Art pack"), formats::lang::activeCodepage()), 74, 1);
        ov.text["gameSettings"] = ui_.ellipsize(res_.text("GameSettings"), 74, 1);  // half-width button
        // Our label, in the text language when the font can write it ("Язык").
        ov.text["language"] = formats::lang::activeCodepage() == 1251 ? formats::lang::toCodepage("\xD0\xAF\xD0\xB7\xD1\x8B\xD0\xBA", 1251)
                                                                       : std::string("Language");
        ov.text["dataFolder"] = "Data Folder";
    } else if (menu == "OPTIONS") {
        ov.hidden.insert("Debug");
        optionsOverrides(ov);
    } else if (menu == "SOUNDOPTIONS") {
        if (tracks_.empty()) ov.disabled.insert("play");
    } else if (menu == "LOADMISSION") {
        for (const char* b : {"atmis", "hkmis", "odmis", "Survival"}) ov.hidden.insert(b);  // test missions / Gruntmods survival
        ov.text["customMissions"] = "Community";
        if (!catalog_ || catalog_->custom.empty()) ov.disabled.insert("customMissions");
        if (const formats::UilPage* p = page(menu); p && catalog_) {
            for (const auto& w : p->widgets) {
                if (w.kind != "BUTTON") continue;
                for (const auto& c : w.commands()) {
                    if (c.rfind("LoadMission ", 0) != 0) continue;
                    const std::string want = lower(c.substr(12)) + ".map";
                    bool found = false;
                    for (const auto& list : catalog_->campaign)
                        for (const auto& mi : list) found = found || lower(mi.map) == want;
                    if (!found) ov.disabled.insert(w.name);
                }
            }
        }
    } else if (menu == "D2K_LANGUAGE") {
        ov.text["title"] = "LANGUAGE";
        ov.text["select"] = "Select";
        ov.text["ruText"] = settings_.russianTextFixes ? "Fixes: on" : "Fixes: off";  // Russian text fixes
        if (!formats::lang::availability(formats::lang::Id::RussianPc, languageSources()).text) ov.disabled.insert("ruText");
        const auto it = states_.find(menu);
        const int sel = it == states_.end() || !it->second.lists.count("langList") ? -1 : it->second.lists.at("langList").sel;
        {   // The Russian text fixes button only makes sense for a Russian entry (owner request).
            const bool ru = sel >= 0 && sel < formats::lang::kCount && formats::lang::isRussian(formats::lang::kAll[std::size_t(sel)]);
            if (!ru) ov.hidden.insert("ruText");
        }
        if (sel < 0 || sel >= formats::lang::kCount ||
            !formats::lang::availability(formats::lang::kAll[std::size_t(sel)], languageSources()).any())
            ov.disabled.insert("select");
    } else if (menu == "D2K_CUSTOMMISSIONS") {
        const int n = catalog_ ? int(catalog_->custom.size()) : 0;
        ov.text["title"] = "CUSTOM MISSIONS (" + std::to_string(n) + ")";
        ov.text["start"] = "Start";
        const auto it = states_.find(menu);
        const int sel = it == states_.end() ? -1 : it->second.lists.count("missionList") ? it->second.lists.at("missionList").sel : -1;
        if (sel < 0 || sel >= n) ov.disabled.insert("start");
    } else if (menu == "LOAD") {
        const auto it = states_.find("LOAD");
        int sel = -1;
        if (it != states_.end()) {
            auto l = it->second.lists.find("loadListBox");
            if (l != it->second.lists.end()) sel = l->second.sel;
        }
        if (sel < 0 || sel >= int(slots_.size()) || !slots_[std::size_t(sel)].valid) ov.disabled.insert("loadGame");
    }
}

std::string OriginalMenus::escapeButton(const std::string& menu) const {
    if (menu == "MAINMENU") return "exit";
    if (menu == "CREDITS") return {};
    return UilPageStack::escapeButton(menu);
}

void OriginalMenus::command(const std::string& menu, const std::string& op, const std::string& arg) {
    if (communityCommand(menu, op, arg)) return;
    if (op == "D2kCyclePlayAs") {
        if (!playAs_.empty()) playAsSel_ = (playAsSel_ + 1) % int(playAs_.size());
        return;
    }
    if (op == "SetValueECTo" && arg == "gGameState GameStateQuit") {
        action_.kind = OriginalMenuAction::Quit;
    } else if (op == "StartGame") {
        startSkirmish();
    } else if (op == "UILoadGame") {
        const UilList& l = state("LOAD").lists["loadListBox"];
        if (l.sel >= 0 && l.sel < int(slots_.size()) && slots_[std::size_t(l.sel)].valid) {
            action_.kind = OriginalMenuAction::Load;
            action_.slot = slots_[std::size_t(l.sel)].index;
        }
    } else if (op == "LoadMission") {
        action_.kind = OriginalMenuAction::CampaignMission;
        action_.mission = arg;
    } else if (op == "D2kStartCustomMission") {
        const int sel = state("D2K_CUSTOMMISSIONS").lists["missionList"].sel;
        if (catalog_ && sel >= 0 && sel < int(catalog_->custom.size())) {
            customSel_ = sel;
            action_.kind = OriginalMenuAction::CustomMission;
            action_.slot = sel;
        }
    } else if (op == "PlayCDTrack") {
        const UilList& l = state("SOUNDOPTIONS").lists["musicList"];
        if (l.sel >= 0 && l.sel < int(tracks_.size())) music_ = tracks_[std::size_t(l.sel)];
    } else if (op == "StopCDTrack") {
        music_ = "-";
    } else if (op == "D2kApplyLanguage") {
        // One pick switches text, voices and cutscenes (the Advanced mix is on the Enhanced
        // Options page); App reloads the front end in the new language.
        const int sel = state("D2K_LANGUAGE").lists["langList"].sel;
        if (sel >= 0 && sel < formats::lang::kCount) {
            const formats::lang::Id id = formats::lang::kAll[std::size_t(sel)];
            if (formats::lang::availability(id, languageSources()).any()) {
                settings_.language = formats::lang::code(id);
                settings_.languageText = settings_.languageVoices = settings_.languageMovies = "";
                back();
            }
        }
    } else if (op == "D2kToggleRuText") {
        // App sees the language key change and reloads the front-end texts.
        settings_.russianTextFixes = !settings_.russianTextFixes;
    } else if (op == "D2kCycleGraphics") {
        // Our addition: the graphics preset (Settings::applyPreset), Original -> Enhanced -> 3D;
        // Custom (mixed values) is shown but not picked. Enhanced / 3D hand the front end to the
        // modern pages (their Graphics page, where Custom is).
        using GP = Settings::GraphicsPreset;
        const GP cur = settings_.activePreset();
        const GP next = cur == GP::Original                                          ? GP::Enhanced
                        : cur == GP::Enhanced && Settings::modelPackInstalled() ? GP::ThreeD  // 3D needs the 3D art pack
                                                                                : GP::Original;
        settings_.applyPreset(next);
        if (settings_.enhanced()) action_.kind = OriginalMenuAction::Enhanced;
    } else if (op == "D2kCycleResolution") {
        // Our addition: the Original-mode game resolution (Settings::resolution), cycled.
        const auto& choices = Settings::resolutionChoices();
        if (!choices.empty()) {
            const auto it = std::find(choices.begin(), choices.end(), resolution());
            const std::size_t i = it == choices.end() ? choices.size() - 1 : std::size_t(it - choices.begin());
            setResolution(choices[(i + 1) % choices.size()]);
        }
    }
}

void OriginalMenus::widgetEvent(const std::string& menu, const UilEvents& ev, UilState& s) {
    communityEvent(menu, ev, s);
    if (menu == "DIFFICULTY" || menu == "DIFFICULTY2" || menu == "SKIRMISH") {
        for (int i = 0; i < 3; ++i)
            if (ev.checked == kDifficultyChecks[i]) settings_.skirmishDifficulty = i;  // SetValueU2To gDifficultyLevel
    }
    if (menu == "SKIRMISH") {
        for (int i = 0; i < 3; ++i)
            if (ev.checked == kHouseChecks[i]) settings_.skirmishHouse = i;
        for (const auto& [name, c] : kColourChecks)
            if (ev.checked == name) settings_.skirmishColour = c;
        if (ev.checked == "crateCheck") settings_.skirmishCrates = s.checks["crateCheck"];
        if (ev.checked == "wormsCheck") settings_.skirmishWorms = s.values["wormsCheck"];
        if (ev.changed == "unitCountBar") settings_.skirmishUnits = s.values["unitCountBar"];
        if (ev.changed == "techLevelBar") settings_.skirmishTech = s.values["techLevelBar"];
        if (ev.changed == "creditBar") settings_.skirmishCredits = s.values["creditBar"];
        if (ev.changed == "aiBar") settings_.skirmishAi = s.values["aiBar"];
        if (ev.edited == "nameEdit") settings_.playerName = s.edits["nameEdit"];
        if (ev.selected == "mapList")
            if (const SkirmishMap* m = selectedMap()) settings_.skirmishMap = m->stem;
    } else if (menu == "SETTINGS") {
        if (ev.checked == "8bit") settings_.graphics = Settings::Graphics::Original;
        if (ev.checked == "16bit") {
            settings_.graphics = Settings::Graphics::Enhanced;
            action_.kind = OriginalMenuAction::Enhanced;  // the modern menus take over
        }
        if (ev.checked == "fullscreen") {
            settings_.fullscreen = s.checks["fullscreen"];
            SDL_SetWindowFullscreen(SDL_GetRenderWindow(r_), settings_.fullscreen);
        }
        if (ev.checked == "smoothMotion") settings_.smoothMotion = s.checks["smoothMotion"];
        if (ev.checked == "modernMouse")
            settings_.controls = s.checks["modernMouse"] ? Settings::Controls::Modern : Settings::Controls::Original;
        if (ev.checked == "cutscenes") settings_.cutscenes = s.checks["cutscenes"];
    } else if (menu == "OPTIONS") {
        applyOptions(ev, s);
    } else if (menu == "LOAD") {
        if (!ev.activated.empty()) command(menu, "UILoadGame", "loadListBox");
    } else if (menu == "D2K_LANGUAGE") {
        if (ev.selected == "langList" || !ev.activated.empty()) fillLanguageInfo(s);
        if (ev.activated == "langList") command(menu, "D2kApplyLanguage", "");
    } else if (menu == "D2K_CUSTOMMISSIONS") {
        if (ev.selected == "missionList" || !ev.activated.empty()) fillCustomBriefing(s);
        if (ev.activated == "missionList") command(menu, "D2kStartCustomMission", "");
    }
}

void OriginalMenus::startSkirmish() {
    const SkirmishMap* m = selectedMap();
    if (!m) return;
    settings_.skirmishMap = m->stem;
    action_.kind = OriginalMenuAction::Start;
    GameStart& s = action_.start;
    s = {};
    s.map = m->file;
    s.skirmish = true;
    std::string name = m->name;
    if (const auto p = name.find(" ("); p != std::string::npos) name = name.substr(0, p);
    s.title = "Skirmish - " + name;
    s.sk.playerHouse = settings_.skirmishHouse;
    // gNetAIPlayers 1..5 on the page; a map has spawn points for maxPlayers - 1 opponents.
    s.sk.aiPlayers = std::clamp(settings_.skirmishAi, 1, std::max(1, m->maxPlayers - 1));
    s.sk.techLevel = settings_.skirmishTech;
    s.sk.startingCredits = std::uint32_t(settings_.skirmishCredits);
    s.sk.difficulty = settings_.skirmishDifficulty;
    s.sk.unitCount = settings_.skirmishUnits;
    s.sk.worms = settings_.skirmishWorms;
    s.sk.crates = settings_.skirmishCrates;
    s.sk.playerColor = settings_.skirmishColour;
}

// ---- credits (CreateCredits 0x43d2a0, FUN_0043d520 per frame) -----------------------------
// Lines 15 px apart (font 10 + 5) between CreditsScreenTop 50 and Bottom 350, centred in
// CreditsScreenWidth 640; the top and bottom tenth fade (White at (distance << 5) / 30); the
// text moves up 1 px whenever more than 10 ms passed; at the end a 2 s pause, then again.
void OriginalMenus::drawCredits(const UiStage& st, bool top) {
    constexpr int kTop = 50, kBottom = 350, kLine = 15, kFade = (kBottom - kTop) / 10;
    const Uint64 now = SDL_GetTicks();
    const double total = double(kLine) * double(credits_.size());
    if (creditsLast_ == 0) creditsLast_ = now;
    if (creditsPos_ < total) {
        creditsPos_ += double(now - creditsLast_) / 11.0;
        if (creditsPos_ >= total) creditsEndAt_ = now;
    } else if (now - creditsEndAt_ > 2000) {
        creditsPos_ = 0;
    }
    creditsLast_ = now;
    const int pos = int(creditsPos_);
    const int first = pos / kLine;
    for (int i = first; i < int(credits_.size()); ++i) {
        const int y = kTop - pos % kLine + (i - first) * kLine;
        if (y >= kBottom) break;
        if (y <= kTop || credits_[std::size_t(i)].empty()) continue;
        int level = 32;
        if (y > kBottom - kFade) level = std::min(kFade, kBottom - y) * 32 / kFade;
        else if (y < kTop + kFade) level = std::min(kFade, y - kTop) * 32 / kFade;
        const auto c = static_cast<unsigned char>(255 * level / 32);
        UilView::text(ui_, st, credits_[std::size_t(i)], 320, float(y), {c, c, c, 255}, 1);
    }
    if (top && (ui_.clicked() || ui_.takeBack() || ui_.takeActivate())) back();
}

// ---- skirmish result (FUN_0043dcb0) ---------------------------------------------------------

void OriginalMenus::openScore(const game::ScoreInput& in) {
    score_.clear();
    // The sides taking part; sums for the score.
    std::uint32_t U = 0, B = 0, S = 0;
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        const game::ScoreSide& sd = in.sides[std::size_t(i)];
        if (!sd.participates) continue;
        SkirmishScoreRow r;
        r.name = i == in.mySide ? settings_.playerName : res_.text("UI_Computer");
        if (r.name.empty()) r.name = i == in.mySide ? "Player" : "Computer";
        r.house = std::clamp(sd.house, 0, 2);
        r.colour = std::clamp(sd.colour, 0, 7);
        r.unitsLost = sd.unitsLost;
        r.buildingsLost = sd.buildingsLost;
        r.spice = sd.spiceHarvested;
        r.unitsKilled = sd.unitsKilled;
        r.buildingsKilled = sd.buildingsKilled;
        U += std::uint32_t(r.unitsLost);
        B += std::uint32_t(r.buildingsLost);
        S += std::uint32_t(r.spice);
        ++n;
        score_.push_back(r);
    }
    // Score per row: units (U - lost) * n * 150 / U - kills, buildings likewise with 100,
    // spice * n * 50 / S (0 for a category nobody scored in); rows sorted by it, best first.
    for (auto& r : score_) {
        const std::int64_t nu = n;
        const std::int64_t a = U ? (std::int64_t(U) - r.unitsLost) * nu * 150 / std::int64_t(U) : 0;
        const std::int64_t b = B ? (std::int64_t(B) - r.buildingsLost) * nu * 100 / std::int64_t(B) : 0;
        const std::int64_t c = S ? std::int64_t(r.spice) * nu * 50 / std::int64_t(S) : 0;
        r.score = int((a - r.unitsKilled) + (b - r.buildingsKilled) + c);
    }
    std::stable_sort(score_.begin(), score_.end(), [](const auto& x, const auto& y) { return x.score > y.score; });
    setStack({"MAINMENU", "MULTIPLAYERTYPE", "SCORE"});
}

void OriginalMenus::drawScore(const UiStage& st) {
    const render::Colour white = res_.colour("White"), grey = res_.colour("LGrey");
    auto centred = [&](const std::string& s, int x, int y, int w, int h, render::Colour c) {
        UilView::text(ui_, st, s, float(x) + w / 2.0f, float(y) + std::round((h - ui_.textHeight(1)) / 2), c, 1);
    };
    centred(res_.text("TitlePracticeScore"), 20, 20, 600, 25, white);
    centred(res_.text("PlayerNames"), 87, 53, 150, 24, white);
    centred(res_.text("Casualties"), 235, 53, 133, 24, white);
    centred(res_.text("Buildings"), 368, 53, 124, 24, white);
    centred(res_.text("Spice"), 492, 53, 59, 24, white);
    centred(res_.text("Inflicted"), 235, 77, 69, 24, white);
    centred(res_.text("Received"), 304, 77, 64, 24, white);
    centred(res_.text("Lost"), 368, 77, 60, 24, white);
    centred(res_.text("Destroyed"), 428, 77, 64, 24, white);
    centred(res_.text("Harvested"), 492, 77, 58, 24, white);
    int y = 101;
    for (const auto& r : score_) {
        if (const render::Texture* t = res_.image(kScoreIcon[r.house]))
            ui_.image(t->get(), st.r(87, float(y + 1), 24, 24));
        const render::Colour nc = res_.colour("NetColour" + std::to_string(r.colour));
        UilView::text(ui_, st, ui_.ellipsize(r.name, 124, 1), 113, float(y) + std::round((24 - ui_.textHeight(1)) / 2), nc);
        centred(std::to_string(r.unitsKilled), 235, y, 69, 24, grey);
        centred(std::to_string(r.unitsLost), 304, y, 64, 24, grey);
        centred(std::to_string(r.buildingsLost), 368, y, 60, 24, grey);
        centred(std::to_string(r.buildingsKilled), 428, y, 64, 24, grey);
        centred(std::to_string(r.spice), 492, y, 58, 24, grey);
        y += 24;
    }
}

void OriginalMenus::drawExtra(const std::string& menu, const UiStage& st, bool top) {
    if (menu == "CREDITS") drawCredits(st, top);
    else if (menu == "SCORE") drawScore(st);
}

// ---- frame -------------------------------------------------------------------------------------

OriginalMenuAction OriginalMenus::frame() {
    action_ = {};
    res_.setSmooth(false);
    st_ = UiStage::fit(r_, false);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    if (stack_.empty()) setStack({"MAINMENU"});
    drawPages(st_);
    if ((action_.kind == OriginalMenuAction::CustomMission || action_.kind == OriginalMenuAction::CommunityMission) &&
        catalog_ && action_.slot >= 0 && action_.slot < int(catalog_->custom.size()) &&
        catalog_->custom[std::size_t(action_.slot)].map == playAsMap_ && playAsSel_ > 0 &&
        playAsSel_ < int(playAs_.size()))
        action_.playAsSide = playAs_[std::size_t(playAsSel_)].side;
    return action_;
}

// --- Play as (not original) ---------------------------------------------------------------

bool OriginalMenus::playAsPage(const std::string& menu) {
    return menu == "D2K_CUSTOMMISSIONS" || menu == "D2K_COMMUNITY" || menu == "D2K_COMMUNITYCAMPAIGN";
}

void OriginalMenus::playAsOverrides(const std::string& menu, UilOverrides& ov) {
    if (!playAsPage(menu)) return;
    const MissionInfo* m = playAsMission(menu);
    if (!m) {
        ov.hidden.insert("playAs");
        return;
    }
    if (m->map != playAsMap_) {
        playAsMap_ = m->map;
        playAsSel_ = 0;
        playAs_ = playAsOptions(dataDir_, m->map);
    }
    if (playAs_.size() <= 1) {
        ov.hidden.insert("playAs");
        return;
    }
    playAsSel_ = std::clamp(playAsSel_, 0, int(playAs_.size()) - 1);
    const PlayAsOption& o = playAs_[std::size_t(playAsSel_)];
    // "As: Harkonnen", "As: Ordos 2" when sides share a house.
    int same = 0;
    for (const auto& p : playAs_) same += p.house == o.house;
    std::string t = std::string("As: ") + houseName(o.house);
    if (same > 1) t += " " + std::to_string(o.side);
    ov.text["playAs"] = t;
}

bool OriginalMenus::openByName(const std::string& name) {
    if (name == "main") setStack({"MAINMENU"});
    else if (name == "difficulty") setStack({"MAINMENU", "DIFFICULTY"});
    else if (name == "select-mission") setStack({"MAINMENU", "LOADMISSION"});
    else if (name == "custom-missions" || name == "original-community-maps") openCommunity(1, 0);
    else if (name == "original-community") openCommunity(0, 0);
    else if (name == "original-community-campaign") {
        int pick = 0;  // a campaign with arcs if there is one (a representative page)
        if (catalog_)
            for (int i = 0; i < int(catalog_->community.size()); ++i)
                if (catalog_->community[std::size_t(i)].chapters > 0) pick = i;
        openCommunityCampaign(pick, -1);
    }
    else if (name == "mptype") setStack({"MAINMENU", "MULTIPLAYERTYPE"});
    else if (name == "skirmish") setStack({"MAINMENU", "MULTIPLAYERTYPE", "SKIRMISH"});
    else if (name == "options" || name == "settings") setStack({"MAINMENU", "SETTINGS"});
    else if (name == "game-options") setStack({"MAINMENU", "SETTINGS", "OPTIONS"});
    else if (name == "language") setStack({"MAINMENU", "SETTINGS", "D2K_LANGUAGE"});
    else if (name == "sound") setStack({"MAINMENU", "SETTINGS", "OPTIONS", "SOUNDOPTIONS"});
    else if (name == "load") { setStack({"MAINMENU"}); jump("LOAD"); }
    else if (name == "no-saves") setStack({"MAINMENU", "NO_SAVED_GAMES"});
    else if (name == "credits") {
        setStack({"MAINMENU", "CREDITS"});
        creditsPos_ = 15.0 * 30;  // some text on screen
        creditsLast_ = SDL_GetTicks();
    } else if (name == "skirmish-score") {
        game::ScoreInput in;
        in.mySide = 0;
        in.sides[0] = {true, 0, 14, 3, 6200, 41, 11, 0};
        in.sides[1] = {true, 1, 37, 8, 2600, 9, 2, 1};
        in.sides[2] = {true, 2, 22, 5, 3100, 23, 3, 2};
        openScore(in);
    } else {
        return false;
    }
    return true;
}

}  // namespace d2k::platform
