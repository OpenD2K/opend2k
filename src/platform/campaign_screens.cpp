#include "platform/campaign_screens.hpp"

#include "formats/io.hpp"
#include "platform/ingame_pages.hpp"
#include "platform/movies.hpp"
#include "platform/saves.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace d2k::platform {

namespace {

constexpr double kStepSeconds = 1.0 / 30;  // one CUIManager page update (assumed 30 per second)
const char kHouseLetter[3] = {'A', 'H', 'O'};
const char* const kHouseMenu[3] = {"ATREIDES", "HARKONNEN", "ORDOS"};
const char* const kHouseKey[3] = {"Atreides", "Harkonnen", "Ordos"};
const char kBarColour[3] = {'b', 'r', 'g'};  // CreateScore 0x43d820: u1_b / u1_r / u1_g
const char* const kScoreLogo[3] = {"at_score", "hk_score", "od_score"};
const char* const kPointer[3] = {"ATpoint", "HKpoint", "ORpoint"};
const char* const kScoreBoxes[2][3] = {{"ourUnitCasualty", "ourBuildingCasualty", "ourSpiceHarvested"},
                                       {"theirUnitCasualty", "theirBuildingCasualty", "theirSpiceHarvested"}};

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::string highScorePath() {
    const std::string p = Settings::path();
    if (p.empty()) return {};
    return (std::filesystem::path(p).parent_path() / "highscores.txt").string();
}

}  // namespace

CampaignScreens::CampaignScreens(SDL_Renderer* r, Ui& ui, Settings& settings)
    : r_(r), ui_(ui), settings_(settings), res_(r) {}

CampaignScreens::~CampaignScreens() {
    if (mapTex_) SDL_DestroyTexture(mapTex_);
    if (editing_) SDL_StopTextInput(SDL_GetRenderWindow(r_));
}

void CampaignScreens::setData(const std::string& dataDir, const Catalog* catalog) {
    dataDir_ = dataDir;
    catalog_ = catalog;
    res_.setData(dataDir, catalog ? &catalog->text : nullptr);
    dialog_.reset();  // rebuilt with the new data on the next Save / Load
    dialogOpen_ = false;
    table_ = {};
    if (dataDir.empty()) return;
    const std::string ui = formats::findFileNoCase(dataDir, "UI_DATA");
    const std::string path = ui.empty() ? std::string() : formats::findFileNoCase(ui, "campaign.uib");
    try {
        if (!path.empty()) table_ = game::CampaignTable::parse(formats::readFile(path));
    } catch (const std::exception& e) {
        SDL_Log("campaign.uib: %s", e.what());
    }
    globe_.dirty = true;
}

bool CampaignScreens::parseCampaignMap(const std::string& map, int& house, int& mission) {
    // [AHO]<n>V<v>(.MAP), as CUIManager__LoadMission 0x4752c0 reads it (atoi(name + 1)).
    if (map.size() < 4) return false;
    const char h = static_cast<char>(std::toupper(static_cast<unsigned char>(map[0])));
    const char* p = std::strchr("AHO", h);
    if (!p || !std::isdigit(static_cast<unsigned char>(map[1]))) return false;
    std::size_t i = 1;
    int n = 0;
    while (i < map.size() && std::isdigit(static_cast<unsigned char>(map[i]))) n = n * 10 + (map[i++] - '0');
    if (i >= map.size() || std::toupper(static_cast<unsigned char>(map[i])) != 'V') return false;
    house = static_cast<int>(p - "AHO");
    mission = n;
    return n >= 1 && n <= 9;
}

// ---- entry points ------------------------------------------------------------------------

void CampaignScreens::openHouseSelect() {
    moviePending_ = false;
    dialogOpen_ = false;
    stack_ = {"HOUSESELECTION"};
    enter();
}

void CampaignScreens::openGlobe(int house, int missionNumber) {
    house_ = std::clamp(house, 0, 2);
    mission_ = std::clamp(missionNumber, 1, 9);
    moviePending_ = false;
    dialogOpen_ = false;
    stack_ = {"MAINMENU", "GLOBE"};
    enter();
}

void CampaignScreens::openSaved(const game::SaveMeta& m) {
    settings_.skirmishDifficulty = std::clamp(m.difficulty, 0, 2);  // gDifficultyLevel (LoadGame 0x4418b0)
    openGlobe(m.campaignHouse, m.missionNumber);
}

bool CampaignScreens::onGlobe() const { return current() == "GLOBE" && !dialogOpen_ && !moviePending_; }

bool CampaignScreens::saveToSlot(int slot, const std::string& name, std::string& error) {
    if (current() != "GLOBE") {
        error = "not on the territory map";
        return false;
    }
    const game::SaveMeta m = campaignSaveMeta(house_, mission_, settings_.skirmishDifficulty, name);
    if (!writeSaveSlot(slot, game::writeCampaignSaveFile(m), error)) return false;
    // FUN_00474e20: PlayMentatSound(H_/O_/S_GAMESAVED) after the file is written.
    static const char* const kMentat[3] = {"S_", "H_", "O_"};
    sounds_.push_back({std::string(kMentat[house_]) + "GAMESAVED", true});
    SDL_Log("Saved the territory map (%s, mission %d) to %s", kHouseKey[house_], mission_, slotPath(slot).c_str());
    return true;
}

void CampaignScreens::ensureDialog() {
    if (dialog_) return;
    dialog_ = std::make_unique<InGamePages>(r_, ui_, settings_);
    dialog_->setup(dataDir_, catalog_ ? &catalog_->text : nullptr, false, std::string(), std::string());
    dialog_->saveGame = [this](int slot, const std::string& name, std::string& err) { return saveToSlot(slot, name, err); };
}

void CampaignScreens::openDialog(const std::string& menu) {
    ensureDialog();
    dialog_->canSave = !savesDir().empty();
    dialog_->openDialog(menu);
    dialogOpen_ = !dialog_->empty();
}

void CampaignScreens::openScore(const game::ScoreInput& in) {
    score_ = in;
    house_ = std::clamp(in.myHouse, 0, 2);
    mission_ = in.missionNumber;
    moviePending_ = false;
    // FUN_004738b0 "SCORE": MAINMENU, then GLOBE (CREDITS after the last mission), then the page.
    stack_ = {"MAINMENU", mission_ < 10 ? "GLOBE" : "CREDITS", "SCORE_SINGLE"};
    enter();
}

void CampaignScreens::openMission(const std::string& name) {
    moviePending_ = false;
    stack_ = {"MAINMENU"};
    run({"LoadMission " + name});
}

bool CampaignScreens::openByName(const std::string& name) {
    persistHighScores = false;
    if (name == "campaign" || name == "house") {
        openHouseSelect();
    } else if (name == "house-confirm") {
        house_ = 0;
        stack_ = {"HOUSESELECTION", "PLAY_ATREIDES"};
        enter();
    } else if (name == "campaign-map-save" || name == "campaign-map-load") {
        openGlobe(0, 3);
        finishGlobe();
        openDialog(name == "campaign-map-save" ? "SAVE" : "LOAD");
    } else if (startsWith(name, "campaign-map")) {
        const int h = name == "campaign-map-h" ? 1 : name == "campaign-map-o" ? 2 : 0;
        openGlobe(h, 3);
        finishGlobe();
    } else if (startsWith(name, "briefing-original")) {
        // "briefing-original-<PAGE>" opens a later picture-book page, e.g. A7L_Page_2 (the units).
        house_ = 0;
        mission_ = 1;
        pendingMap_ = "a1v1";
        stack_ = {"MAINMENU", "GLOBE", "PRECAP"};
        if (name.size() > 18) stack_.push_back(name.substr(18));
        enter();
    } else if (name == "quit-campaign") {
        openGlobe(0, 3);
        finishGlobe();
        stack_.push_back("QUIT_CAMPAIGN");
        enter();
    } else if (startsWith(name, "score")) {
        game::ScoreInput in;
        in.mySide = 0;
        in.myHouse = 0;
        in.lost = name == "score-lost";
        in.missionNumber = in.lost ? 3 : 4;
        in.sides[0] = {true, 0, 12, 2, 4200};
        in.sides[1] = {true, 1, 31, 9, 1900};
        in.sides[2] = {true, 2, 0, 0, 0};
        for (int i = 3; i < 8; ++i) in.sides[i] = {true, 0, 0, 0, 0};
        openScore(in);
        if (name != "score-counting") {
            finishScore();
        } else {
            for (int i = 0; i < 40; ++i) counter_.step(0);
        }
    } else {
        return false;
    }
    return true;
}

// ---- page stack ----------------------------------------------------------------------------

std::string CampaignScreens::pageFile(const std::string& menu) const {
    if (menu == "PRECAP") {
        char buf[16];
        std::snprintf(buf, sizeof buf, "L%c%dMIS", kHouseLetter[house_], mission_);
        return buf;
    }
    return res_.menuFile(menu);
}

CampaignAction CampaignScreens::special(const std::string& menu, bool& handled) {
    handled = true;
    CampaignAction a;
    const std::string m = upper(menu);
    if (m == "MAINMENU") a.kind = CampaignAction::MainMenu;
    else if (m == "CREDITS") a.kind = CampaignAction::Credits;  // after mission 9 (original_menus)
    // The territory map's Load / Save (Globe.uil): Load.uil / Save.uil over the map like the
    // original (UISaveGame FUN_00474e20 writes a campaign-map save there); Enhanced loads from
    // the modern load page (it lists both kinds of saves).
    else if (m == "LOAD" && settings_.enhanced()) a.kind = CampaignAction::LoadPage;
    else if (m == "LOAD" || m == "SAVE") openDialog(m);
    else if (m == "RISEOFMERCS") a.kind = CampaignAction::CustomMissions;
    else handled = false;
    return a;
}

void CampaignScreens::enter() {
    view_.reset();
    cursor_ = 0;
    lastNs_ = 0;
    const std::string cur = current();
    if (cur == "GLOBE") {
        globe_ = {};
        globe_.state = game::globeState(table_, house_, mission_);
        globe_.reveal = game::revealOrder(table_, globe_.state, house_, mission_);
        for (int i = 0; i < game::kRegionCount; ++i)
            globe_.shown[i] = globe_.state.fresh[i] ? game::kNoOwner : globe_.state.owner[i];
    } else if (cur == "SCORE_SINGLE") {
        const game::CampaignRecord& rec = table_.at(house_, mission_ - 1);
        counter_ = game::ScoreCounter(score_, table_.loaded ? rec.scoreMultiplier : 1.0f);
        enemyHouse_ = std::clamp(rec.enemyHouse, 0, 2);
        scoreDone_ = false;
        scoreStepNs_ = 0;
        newEntry_ = -1;
        loadHighScores();
    }
}

CampaignAction CampaignScreens::jump(const std::string& menu) {
    bool handled = false;
    CampaignAction a = special(menu, handled);
    if (handled) return a;
    stack_.push_back(menu);
    enter();
    // A page that is just a movie (CMovie FUN_0048d730 / FUN_0048d8f0): play it, then its end
    // command; at once when it cannot play (cutscenes off, no file).
    if (const formats::UilPage* p = res_.page(pageFile(menu))) {
        for (const auto& w : p->widgets) {
            if (w.kind != "MOVIE") continue;
            if (movies_ && movies_->play(w.name, house_)) {
                movieCommands_ = w.commands();
                moviePending_ = true;
                return {};
            }
            return run(w.commands());
        }
    }
    return {};
}

CampaignAction CampaignScreens::back() {
    if (!stack_.empty()) stack_.pop_back();
    if (stack_.empty()) return CampaignAction{CampaignAction::MainMenu, {}};
    bool handled = false;
    CampaignAction a = special(current(), handled);
    if (handled) return a;
    enter();
    return {};
}

CampaignAction CampaignScreens::quitTo(const std::string& menu) {
    stack_.clear();
    return jump(menu);
}

CampaignAction CampaignScreens::startPending() {
    gameStatePending_ = false;
    CampaignAction a;
    if (!catalog_ || pendingMap_.empty()) return a;
    const std::string want = upper(pendingMap_);
    const auto& list = catalog_->campaign[house_];
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string stem = upper(list[i].map.substr(0, list[i].map.find_last_of('.')));
        if (stem != want) continue;
        a.kind = CampaignAction::Start;
        a.start.map = list[i].map;
        a.start.title = std::string(kHouseNames[house_]) + " - " + list[i].shortName;
        a.start.campaignHouse = house_;
        a.start.missionIndex = static_cast<int>(i);
        a.start.sk.difficulty = settings_.skirmishDifficulty;  // gDifficultyLevel (skill.uil)
        return a;
    }
    SDL_Log("Campaign mission %s not found in Missions/", pendingMap_.c_str());
    return a;
}

CampaignAction CampaignScreens::run(const std::vector<std::string>& cmds) {
    for (const std::string& c : cmds) {
        const auto sp = c.find(' ');
        const std::string op = c.substr(0, sp);
        const std::string arg = sp == std::string::npos ? std::string() : c.substr(sp + 1);
        CampaignAction a;
        if (op == "JumpToMenu") {
            a = jump(arg);
        } else if (op == "BackMenu") {
            a = back();
        } else if (op == "BackMenuAndJumpTo") {
            if (!stack_.empty()) stack_.pop_back();
            a = jump(arg);
        } else if (op == "QuitAndJumpToMenu") {
            a = quitTo(arg);
        } else if (op == "SetValueS4To" && startsWith(arg, "gSideId ")) {
            house_ = std::clamp(std::atoi(arg.c_str() + 8), 0, 2);
        } else if (op == "SetValueU1To" && startsWith(arg, "gMissionNumber ")) {
            mission_ = std::clamp(std::atoi(arg.c_str() + 15), 1, 10);
        } else if (op == "SetValueECTo" && arg == "gGameState GameStateMainLoop") {
            gameStatePending_ = true;
        } else if (op == "QuitMenu") {
            if (gameStatePending_) return startPending();
        } else if (op == "LoadMission") {
            // CUIManager__LoadMission 0x4752c0: MissionNumber = atoi(name + 1), house by letter,
            // then the briefing movie MVE_<HOUSE><n> (-> PRECAP).
            int h = 0, n = 0;
            pendingMap_ = arg;
            if (parseCampaignMap(arg, h, n)) {
                house_ = h;
                mission_ = n;
                a = jump(std::string("MVE_") + kHouseMenu[h] + std::to_string(n));
            }
        } else if (op == "PreviousMission" || op == "NextMission") {
            // FUN_0042d0e0 / FUN_0042d110: MissionNumber -/+ 1 (1..9) and the globe again.
            const int n = mission_ + (op == "NextMission" ? 1 : -1);
            if (n >= 1 && n <= 9) {
                mission_ = n;
                enter();
            }
        } else if (op == "PlaySample") {
            if (arg != "BUTTON") sounds_.push_back({arg, false});  // BUTTON: the click sound already
        }
        if (a.kind != CampaignAction::None) return a;
    }
    return {};
}

bool CampaignScreens::scorePage() const { return current() == "SCORE_SINGLE"; }

// ---- frame -----------------------------------------------------------------------------------

CampaignAction CampaignScreens::frame() {
    const bool enhanced = settings_.enhanced();
    res_.setSmooth(enhanced);
    st_ = UiStage::fit(r_, enhanced);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    cursor_ = 0;
    if (moviePending_ && !(movies_ && movies_->active())) {  // the movie is over
        moviePending_ = false;
        const std::vector<std::string> cmds = std::move(movieCommands_);
        return run(cmds);
    }
    if (stack_.empty()) return CampaignAction{CampaignAction::MainMenu, {}};
    const std::string cur = current();
    const formats::UilPage* page = res_.page(pageFile(cur));
    if (!page) {
        UilView::text(ui_, st_, "Missing page " + pageFile(cur) + ".uil (Esc: back)", 20, 20, {220, 200, 160, 255});
        if (ui_.takeBack() || ui_.clicked()) return back();
        return {};
    }
    // Dialog pages without a picture (QUIT_CAMPAIGN) are drawn over the page below them.
    const bool dialog = !page->widgets.empty() && page->widgets.front().kind != "BACKGROUND" && stack_.size() >= 2;
    if (dialog && stack_[stack_.size() - 2] == "GLOBE") drawGlobeBelow();
    if (cur == "GLOBE" && dialogOpen_) return drawDialog();
    if (cur == "GLOBE") return drawGlobe(*page);
    if (cur == "SCORE_SINGLE") return drawScore(*page);

    const std::string hit = view_.draw(res_, ui_, *page, st_);
    const formats::UilWidget* pressed = hit.empty() ? nullptr : page->find(hit);
    // Keyboard: Enter = the button marked EnterActions (else Yes); Esc = Cancel / No / back.
    if (!pressed && ui_.takeActivate()) {
        for (const auto& w : page->widgets)
            if (w.kind == "BUTTON") {
                const auto c = w.commands();
                if (std::find(c.begin(), c.end(), "EnterActions") != c.end() || w.name == "yes") {
                    pressed = &w;
                    break;
                }
            }
    }
    if (!pressed && ui_.takeBack()) {
        for (const auto& w : page->widgets)
            if (w.kind == "BUTTON" && (w.name == "cancel" || w.name == "no")) pressed = &w;
        if (!pressed) return back();
    }
    if (pressed) return run(pressed->commands());
    return {};
}

// ---- globe -------------------------------------------------------------------------------------

void CampaignScreens::drawGlobeBelow() {
    const formats::UilPage* p = res_.page(pageFile("GLOBE"));
    if (!p) return;
    // Drawn as it is (finished map, no input).
    UilView passive;
    UilOverrides ov;
    for (const auto& w : p->widgets) {
        if (w.kind != "BACKGROUND") continue;
        ov.hidden.insert(w.name);
        if (const auto* bg = res_.image(w.name)) ui_.image(bg->get(), st_.r(0, 0, 640, 400));
    }
    if (globe_.dirty) rebuildMap();
    if (mapTex_) ui_.image(mapTex_, st_.r(0, 0, 640, 400));
    if (savesDir().empty()) ov.disabled.insert("save");
    ov.inert = true;
    passive.draw(res_, ui_, *p, st_, ov);
}

// Save.uil / Load.uil (and SAVE_DESC, CONFIRM_OVERWRITE, NO_SAVED_GAMES) over the territory map;
// closing them (Cancel, a save written) returns to the map, Load hands the slot to the menu.
CampaignAction CampaignScreens::drawDialog() {
    drawGlobeBelow();
    const InGameAction a = dialog_->frame(settings_.enhanced());
    for (MenuSound& m : dialog_->takeSounds()) sounds_.push_back(std::move(m));
    CampaignAction out;
    switch (a.kind) {
    case InGameAction::Load:
        dialogOpen_ = false;
        out.kind = CampaignAction::LoadSlot;
        out.slot = a.slot;
        break;
    case InGameAction::None: break;
    default: dialogOpen_ = false; break;  // Resume (closed / saved)
    }
    if (dialog_->empty()) dialogOpen_ = false;
    return out;
}

void CampaignScreens::finishGlobe() {
    for (int i = 0; i < game::kRegionCount; ++i) globe_.shown[i] = globe_.state.owner[i];
    globe_.phase = 3;
    globe_.counter = 32;
    globe_.dirty = true;
}

// One FUN_0042c9b0 step: phase 0 fades DUNEMAP2 into DUNEMAP (+4/32), then each new territory
// fades in (+2/32) in FUN_0042c6b0 order, the first of a house group after its speech; then
// the pointers once the speech is over.
void CampaignScreens::stepGlobe() {
    GlobeAnim& g = globe_;
    if (g.phase == 0) {
        if (g.counter > 32) {
            g.phase = 1;
            g.step = -1;
            g.counter = 33;
        } else {
            g.counter += 4;
        }
        return;
    }
    if (g.phase == 1) {
        if (g.counter <= 32) {
            g.counter += 2;
            return;
        }
        if (g.step >= 0) {
            const int r = g.reveal[g.step].region - 1;
            g.shown[r] = g.state.owner[r];
            g.dirty = true;
        }
        const int next = g.step + 1;
        if (next >= static_cast<int>(g.reveal.size())) {
            g.phase = 2;
            return;
        }
        if (!g.reveal[next].sound.empty()) {
            if (speechBusy_) return;  // "while (speaking) update" before the next speech
            sounds_.push_back({g.reveal[next].sound, true});
        }
        g.step = next;
        g.counter = 0;
        return;
    }
    if (g.phase == 2 && !speechBusy_) g.phase = 3;
}

void CampaignScreens::rebuildMap() {
    if (!mapTex_) {
        mapTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 640, 400);
        if (!mapTex_) return;
        SDL_SetTextureBlendMode(mapTex_, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureScaleMode(mapTex_, settings_.enhanced() ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_Texture* old = SDL_GetRenderTarget(r_);
    SDL_Rect vp;
    SDL_GetRenderViewport(r_, &vp);
    SDL_SetRenderTarget(r_, mapTex_);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 0);
    SDL_RenderClear(r_);
    if (const auto* t = res_.image("dunemap")) {
        const SDL_FRect d{0, 0, 640, 400};
        SDL_RenderTexture(r_, t->get(), nullptr, &d);
    }
    const auto& shapes = game::regionShapes();
    for (int i = 0; i < game::kRegionCount; ++i) {
        const int o = globe_.shown[i];
        if (o < 0 || o > 2) continue;
        const auto* t = res_.imageIn("Regions", std::string(1, kHouseLetter[o]) + std::to_string(i + 1));
        if (!t) continue;
        const SDL_FRect d{float(shapes[i].x), float(shapes[i].y), float(t->width()), float(t->height())};
        SDL_RenderTexture(r_, t->get(), nullptr, &d);
    }
    SDL_SetRenderTarget(r_, old);
    SDL_SetRenderViewport(r_, &vp);
    globe_.dirty = false;
}

CampaignAction CampaignScreens::drawGlobe(const formats::UilPage& page) {
    const Uint64 now = SDL_GetTicksNS();
    if (lastNs_ == 0) lastNs_ = now;
    globe_.acc += double(now - lastNs_) / 1e9;
    lastNs_ = now;
    globe_.acc = std::min(globe_.acc, 0.5);
    while (globe_.acc >= kStepSeconds) {
        globe_.acc -= kStepSeconds;
        stepGlobe();
    }
    if (mapSmooth_ != settings_.enhanced()) {
        mapSmooth_ = settings_.enhanced();
        globe_.dirty = true;
    }
    if (globe_.dirty) rebuildMap();

    // The page background (DUNEMAP2), the composite map fading in, the territory appearing.
    UilOverrides ov;
    for (const auto& w : page.widgets)
        if (w.kind == "BACKGROUND") {
            ov.hidden.insert(w.name);
            if (const auto* t = res_.image(w.name)) ui_.image(t->get(), st_.r(0, 0, 640, 400));
        }
    const float a0 = globe_.phase == 0 ? std::min(globe_.counter, 32) / 32.0f : 1.0f;
    if (mapTex_) ui_.image(mapTex_, st_.r(0, 0, 640, 400), a0);
    if (globe_.phase == 1 && globe_.step >= 0 && globe_.step < static_cast<int>(globe_.reveal.size())) {
        const int r = globe_.reveal[globe_.step].region - 1;
        const int o = globe_.state.owner[r];
        if (o >= 0 && o <= 2)
            if (const auto* t = res_.imageIn("Regions", std::string(1, kHouseLetter[o]) + std::to_string(r + 1))) {
                const auto& sh = game::regionShapes()[r];
                ui_.image(t->get(), st_.r(float(sh.x), float(sh.y), float(t->width()), float(t->height())),
                          std::min(globe_.counter, 32) / 32.0f);
            }
    }
    // The two territories to attack: animated pointers (FUN_0042c400, 72x70 frames at 14 fps).
    const game::CampaignRecord& rec = table_.at(house_, mission_);
    if (globe_.phase == 3) {
        if (const auto* t = res_.image(kPointer[house_])) {
            const int frames = std::max(1, t->width() / 72);
            const int f = static_cast<int>(SDL_GetTicks() * 14 / 1000 % static_cast<Uint64>(frames));
            for (int v = 0; v < 2; ++v) {
                if (rec.region[v] == 0) continue;
                int x = rec.pointerX[v], y = rec.pointerY[v];
                if (x == 0 && y == 0) {
                    const auto p = game::regionPointerPos(rec.region[v]);
                    x = p[0];
                    y = p[1];
                }
                const SDL_FRect src{float(f * 72), 0, 72, 70};
                const SDL_FRect dst = st_.r(float(x), float(y), 72, 70);
                SDL_RenderTexture(r_, t->get(), &src, &dst);
            }
        }
    }
    if (savesDir().empty()) ov.disabled.insert("save");  // no folder for save games
    const std::string hit = view_.draw(res_, ui_, page, st_, ov);

    const int mx = static_cast<int>(std::floor(st_.pageX(ui_.mouseX())));
    const int my = static_cast<int>(std::floor(st_.pageY(ui_.mouseY())));
    int variant = 0;
    const int region = game::choosableRegionAt(table_, house_, mission_, mx, my, &variant);
    if (region) cursor_ = 2;  // FUN_0042c400: the attack cursor over a territory to attack
    if (!hit.empty()) {
        if (const auto* w = page.find(hit)) return run(w->commands());
    }
    if (region && ui_.clicked()) {
        ui_.addClickSound();
        return run({"LoadMission " + rec.mission[variant]});
    }
    if (ui_.takeBack()) {
        if (const auto* w = page.find("cancel")) return run(w->commands());
        return back();
    }
    return {};
}

// ---- score ---------------------------------------------------------------------------------------

void CampaignScreens::finishScore() {
    std::uint32_t t = 0;
    for (int i = 0; i < 2000 && !counter_.done(); ++i) {
        counter_.step(t);
        t += 2001;
    }
}

void CampaignScreens::loadHighScores() {
    highScores_ = {};
    if (!persistHighScores) {
        // Sample table for the screenshot pages.
        highScores_[0] = {true, "Duncan", game::scoreRank(1210), 0, 8, 1210};
        highScores_[1] = {true, "Feyd", game::scoreRank(640), 1, 5, 640};
        highScores_[2] = {true, "Ammon", game::scoreRank(260), 2, 2, 260};
        return;
    }
    const std::string path = highScorePath();
    if (path.empty()) return;
    std::ifstream in(path);
    std::string line;
    int i = 0;
    while (i < game::kHighScores && std::getline(in, line)) {
        // score rank house mission name...
        std::istringstream ss(line);
        game::HighScore h;
        if (!(ss >> h.score >> h.rank >> h.house >> h.mission)) continue;
        std::getline(ss, h.name);
        if (!h.name.empty() && h.name[0] == ' ') h.name.erase(0, 1);
        h.used = true;
        highScores_[i++] = h;
    }
}

void CampaignScreens::saveHighScores() {
    if (!persistHighScores) return;
    const std::string path = highScorePath();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::trunc);
    for (const auto& h : highScores_)
        if (h.used) out << h.score << ' ' << h.rank << ' ' << h.house << ' ' << h.mission << ' ' << h.name << '\n';
}

void CampaignScreens::drawHighScores() {
    // FUN_0043fc90: the table under the counters (hs_table at (90, 233), rows 15 px apart).
    const render::Colour white = res_.colour("White");
    if (const auto* t = res_.image("hs_table")) ui_.image(t->get(), st_.r(90, 233, float(t->width()), float(t->height())));
    UilView::text(ui_, st_, res_.text("SS_HighScore"), 90 + 410 / 2.0f, 220, white, 1);
    UilView::text(ui_, st_, res_.text("SS_Name"), 110, 235, white);
    UilView::text(ui_, st_, res_.text("SS_Rating"), 210, 235, white);
    UilView::text(ui_, st_, res_.text("SS_House"), 370, 235, white);
    UilView::text(ui_, st_, res_.text("SS_Mission"), 450, 235, white);
    UilView::text(ui_, st_, res_.text("SS_Score"), 500, 235, white);
    for (int i = 0; i < game::kHighScores; ++i) {
        const float y = 250.0f + 15 * i;
        UilView::text(ui_, st_, std::to_string(i + 1), 90, y, white);
        const game::HighScore& h = highScores_[i];
        if (!h.used) continue;
        std::string name = h.name;
        if (editing_ && i == newEntry_ && (SDL_GetTicks() / 400) % 2 == 0) name += "_";
        UilView::text(ui_, st_, name, 110, y, white);
        UilView::text(ui_, st_, res_.text("Rank" + std::to_string(std::clamp(h.rank, 0, 11) + 1)), 210, y, white);
        UilView::text(ui_, st_, h.house >= 0 && h.house < 3 ? res_.text(kHouseKey[h.house]) : std::string(), 370, y, white);
        UilView::text(ui_, st_, std::to_string(h.mission), 450, y, white);
        UilView::text(ui_, st_, std::to_string(h.score), 500, y, white);
    }
}

CampaignAction CampaignScreens::drawScore(const formats::UilPage& page) {
    // FUN_0043f030 once per page update (assumed 30 per second).
    const Uint64 now = SDL_GetTicksNS();
    if (scoreStepNs_ == 0) scoreStepNs_ = now;
    int steps = 0;
    while (now >= scoreStepNs_ && steps < 8) {
        scoreStepNs_ += Uint64(kStepSeconds * 1e9);
        ++steps;
        if (!counter_.done() && counter_.step(static_cast<std::uint32_t>(now / 1000000)))
            sounds_.push_back({"S_SCORETICK", true});
    }
    if (now > scoreStepNs_ + Uint64(1e9)) scoreStepNs_ = now;
    if (counter_.done() && !scoreDone_) {
        scoreDone_ = true;
        if (persistHighScores) {
            newEntry_ = game::insertHighScore(highScores_, counter_.ourTotal(), house_, mission_);
            if (newEntry_ >= 0) {
                editing_ = true;
                SDL_StartTextInput(SDL_GetRenderWindow(r_));
            }
        }
    }
    auto commitName = [&]() {
        if (!editing_) return;
        editing_ = false;
        SDL_StopTextInput(SDL_GetRenderWindow(r_));
        saveHighScores();
    };

    UilOverrides ov;
    for (const auto& w : page.widgets)
        if (w.kind == "BACKGROUND") {
            ov.hidden.insert(w.name);
            if (const auto* t = res_.image(w.name)) ui_.image(t->get(), st_.r(0, 0, 640, 400));
        }
    // CreateScore: the house picture at (16, 12).
    if (const auto* t = res_.image(kScoreLogo[house_])) ui_.image(t->get(), st_.r(16, 12, float(t->width()), float(t->height())));
    for (int side = 0; side < 2; ++side)
        for (int c = 0; c < 3; ++c) ov.text[kScoreBoxes[side][c]] = std::to_string(counter_.shown(side == 0, c));
    ov.text["ourScore"] = std::to_string(counter_.ourTotal());
    ov.text["theirScore"] = std::to_string(counter_.theirTotal());
    const std::string hit = view_.draw(res_, ui_, page, st_, ov);

    // The bars (x 265; ours at y 40/100/160, theirs 20 lower), clipped to counter * 100 / max.
    static const char* const kBar[3] = {"u", "b", "s"};
    for (int c = 0; c < 3; ++c)
        for (int side = 0; side < 2; ++side) {
            const int house = side == 0 ? house_ : enemyHouse_;
            const std::string name = std::string(kBar[c]) + (side == 0 ? "1_" : "2_") + kBarColour[house];
            const auto* t = res_.image(name);
            const int w = std::min(counter_.barWidth(side == 0, c), t ? t->width() : 0);
            if (!t || w <= 0) continue;
            const float y = 40.0f + 60 * c + 20 * side;
            const SDL_FRect src{0, 0, float(w), float(t->height())};
            const SDL_FRect dst = st_.r(265, y, float(w), float(t->height()));
            SDL_RenderTexture(r_, t->get(), &src, &dst);
        }
    if (scoreDone_) drawHighScores();

    if (editing_ && newEntry_ >= 0) {
        std::string& name = highScores_[newEntry_].name;
        name += ui_.takeTextInput();
        for (int n = ui_.takeBackspaces(); n > 0 && !name.empty(); --n) name.pop_back();
        if (name.size() > 15) name.resize(15);
        ui_.takeBack();  // Backspace edits the name here
        if (ui_.takeActivate()) commitName();
        if (!hit.empty()) {
            commitName();
            if (const auto* w = page.find(hit)) return run(w->commands());
        }
        return {};
    }
    if (!hit.empty() || ui_.takeActivate() || ui_.takeBack()) {
        const auto* w = page.find(hit.empty() ? "continue" : hit);
        if (w) return run(w->commands());
        return back();
    }
    return {};
}

}  // namespace d2k::platform
