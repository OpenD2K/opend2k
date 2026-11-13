#include "platform/menu.hpp"
#include "platform/platform_info.hpp"
#include "platform/pointer.hpp"

#include "formats/io.hpp"
#include "formats/map.hpp"
#include "platform/data_dir.hpp"
#include "platform/app_update.hpp"
#include "platform/artpack_download.hpp"
#include "platform/game_options.hpp"
#include "platform/language.hpp"
#include "platform/movies.hpp"
#include "platform/preset_previews.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "render/art_pack.hpp"
#include "render/ui_text.hpp"

#include <functional>
#include <iterator>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace d2k::platform {

namespace {

const std::vector<int> kCredits = {1000, 2500, 5000, 7500, 10000, 15000, 20000};
const std::vector<int> kUnits = {0, 3, 5, 10};

using render::tr;
using render::trf;

// "<pattern with one %s>" filled with a string of any length (trf has a fixed buffer).
std::string trs(const char* english, const std::string& arg) {
    const std::string pat = tr(english), a = uiUtf8(arg);
    const auto at = pat.find("%s");
    return at == std::string::npos ? pat + " " + a : pat.substr(0, at) + a + pat.substr(at + 2);
}

// Two-string variant ("%s  -  by %s").
std::string trs2(const char* english, const std::string& a0, const std::string& b0) {
    const std::string a = uiUtf8(a0), b = uiUtf8(b0);
    std::string pat = tr(english);
    const auto at = pat.find("%s");
    if (at == std::string::npos) return pat;
    pat = pat.substr(0, at) + a + pat.substr(at + 2);
    const auto at2 = pat.find("%s", at + a.size());
    return at2 == std::string::npos ? pat : pat.substr(0, at2) + b + pat.substr(at2 + 2);
}

// Messages from the installer import / PS1 check (English, possibly "<fixed text>: <detail>").
std::string trMessage(const std::string& msg) {
    if (const char* t = tr(msg); t != msg.c_str() && msg != t) return t;
    for (const char* prefix : {"Not a usable installer: ", "Cannot move the unpacked files into place: "}) {
        const std::string p(prefix);
        if (msg.rfind(p, 0) == 0) return trs((p.substr(0, p.size() - 1) + " %s").c_str(), msg.substr(p.size()));
    }
    return msg;
}

// One entry of the "Play as" choice: the house name, the side number when several sides share
// the house, and a mark on the mission's own side.
std::string playAsLabel(const std::vector<PlayAsOption>& v, std::size_t i) {
    const PlayAsOption& o = v[i];
    const auto same = std::count_if(v.begin(), v.end(), [&](const PlayAsOption& p) { return p.house == o.house; });
    std::string l = tr(houseName(o.house));
    if (same > 1) l = trf("%s (side %d)", l.c_str(), o.side);
    if (o.original) l = trf("%s - mission side", l.c_str());
    return l;
}

// A campaign mission's short name ("Mission 1a", cut from the mission list) in the UI language.
std::string missionLabel(const std::string& shortName) {
    return shortName.rfind("Mission ", 0) == 0 ? trs("Mission %s", shortName.substr(8)) : shortName;
}

// The modern Movies page title of a gallery entry (Movies::gallery() keeps the English).
std::string movieTitle(const Movies::Entry& e) {
    static const char* const kHouse[3] = {"Atreides", "Harkonnen", "Ordos"};
    const int h = std::clamp(e.house, 0, 2);
    const std::string hn = kHouse[h];
    if (e.name.rfind("mve_house_", 0) == 0) return tr("House " + hn);
    if (e.name.rfind("mve_briefing_", 0) == 0) return trf((hn + " mission %d").c_str(), e.name.back() - '0');
    if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, "_wins") == 0) return tr(hn + " victory");
    if (e.name == "mve_lost") return tr(hn + " defeat");
    return tr(e.title);
}

}  // namespace

Menu::Menu(SDL_Window* window, SDL_Renderer* r, Ui& ui, Settings& settings)
    : window_(window), r_(r), ui_(ui), settings_(settings), preview_(r),
      campaign_(std::make_unique<CampaignScreens>(r, ui, settings)),
      original_(std::make_unique<OriginalMenus>(r, ui, settings)) {}

void Menu::setMovies(Movies* m) {
    movies_ = m;
    campaign_->setMovies(m);
    original_->setMovies(m);
}

void Menu::setData(const std::string& dataDir) {
    dataDir_ = dataDir;
    previewFor_ = -1;
    if (dataDir.empty()) return;
    const std::string gfx = formats::findFileNoCase(dataDir, "UI_GFX");
    const std::string d = gfx.empty() ? dataDir + "/UI_GFX" : gfx;
    // The language's own title art (the Russian translation's UI_gfx) first.
    const std::string& over = uiGfxOverlayDir();
    title_ = render::Texture::load(r_, !over.empty() && !formats::findFileNoCase(over, "Title.tga").empty() ? over : d, "Title.tga");
    multi_ = render::Texture::load(r_, d, "MULTIUI.TGA");
    back_ = render::Texture::load(r_, d, "back.tga");
    acct_[0] = render::Texture::load(r_, d, "AACCT.TGA");
    acct_[1] = render::Texture::load(r_, d, "HACCT.TGA");
    acct_[2] = render::Texture::load(r_, d, "OACCT.TGA");
    logo_[0] = render::Texture::load(r_, d, "ATLOGO.BMP");
    logo_[1] = render::Texture::load(r_, d, "HKLOGO.BMP");
    logo_[2] = render::Texture::load(r_, d, "ORLOGO.BMP");
    catalog_ = Catalog::load(dataDir);
    loadCommunityProgress();
    campaign_->setData(dataDir, &catalog_);
    original_->setData(dataDir, &catalog_);
    original_->setCommunityProgress(&progress_);
    mapSel_ = 0;
    for (std::size_t i = 0; i < catalog_.skirmish.size(); ++i)
        if (catalog_.skirmish[i].stem == settings_.skirmishMap) mapSel_ = int(i);
}

void Menu::openOriginal(const std::vector<std::string>& stack) {
    page_ = MenuPage::Original;
    original_->open(stack);
}

void Menu::open(MenuPage p) {
    // Graphics: Original -> the original pages for the screens that have one.
    if (!settings_.enhanced() && hasData()) {
        switch (p) {
        case MenuPage::Main: openOriginal({"MAINMENU"}); return;
        case MenuPage::Skirmish: openOriginal({"MAINMENU", "MULTIPLAYERTYPE", "SKIRMISH"}); return;
        case MenuPage::Options: openOriginal({"MAINMENU", "SETTINGS"}); return;
        case MenuPage::Load: {
            openOriginal({"MAINMENU"});
            original_->openByName("load");
            return;
        }
        case MenuPage::Community:
            original_->openCommunity(communityTab_, communityTab_ == 0 ? communitySel_ : customSel_);
            page_ = MenuPage::Original;
            return;
        case MenuPage::CommunityCampaign:
            original_->openCommunityCampaign(communitySel_, -1);
            page_ = MenuPage::Original;
            return;
        default: break;
        }
    }
    // The art pack's and the app's update checks: only when the player opens Options (never at
    // startup), once per run.
    if (p == MenuPage::Options || p == MenuPage::Graphics || p == MenuPage::ArtPack || p == MenuPage::Updates ||
        p == MenuPage::Welcome)
        ArtPackDownloader::instance().autoCheck(settings_.artPackUpdateCheck);
    if (p == MenuPage::Options || p == MenuPage::Updates) AppUpdateChecker::instance().autoCheck(settings_.artPackUpdateCheck);
    page_ = p;
    loadFromGlobe_ = false;
    ui_.resetFocus(p == MenuPage::Briefing ? 1 : 0);  // briefing: the Start button
    if (p == MenuPage::Welcome)  // the card of the look in use
        ui_.resetFocus(std::min(static_cast<int>(settings_.activePreset()), static_cast<int>(Settings::GraphicsPreset::Enhanced)));
    if (p == MenuPage::Campaign && hasData()) campaign_->openHouseSelect();
    listScroll_ = 0;
    briefScroll_ = 0;
    if (p == MenuPage::Graphics) {
        graphicsTab_ = static_cast<int>(settings_.activePreset());
        graphicsScroll_ = 0;
    }
    if (p == MenuPage::Load) {
        slots_ = listSaveSlots();
        slotScroll_ = 0;
        slotSel_ = 0;  // the newest save
        std::int64_t newest = INT64_MIN;
        for (const SaveSlot& sl : slots_)
            if (sl.valid && sl.meta.savedAt > newest) { newest = sl.meta.savedAt; slotSel_ = sl.index; }
    } else {
        loadError.clear();
    }
}

bool Menu::openByName(const std::string& name) {
    // Original-only pages (any graphics mode), and the original versions of the modern pages.
    static const char* const kOriginalOnly[] = {"credits", "skirmish-score", "difficulty", "select-mission", "mptype",
                                                "game-options", "sound", "no-saves", "settings", "language", "custom-missions",
                                                "original-community", "original-community-maps",
                                                "original-community-campaign"};
    for (const char* n : kOriginalOnly) {
        if (name == n) {
            page_ = MenuPage::Original;
            return original_->openByName(name);
        }
    }
    if (!settings_.enhanced() && (name.empty() || name == "main" || name == "skirmish" || name == "options" || name == "load")) {
        page_ = MenuPage::Original;
        return original_->openByName(name.empty() ? "main" : name);
    }
    if (name.empty() || name == "main") open(MenuPage::Main);
    else if (name == "campaign" || name == "house") open(MenuPage::Campaign);
    else if (name == "house-confirm" || name.rfind("campaign-map", 0) == 0 || name.rfind("briefing-original", 0) == 0 ||
             name.rfind("score", 0) == 0 || name == "quit-campaign") {
        page_ = MenuPage::Campaign;
        return campaign_->openByName(name);
    }
    else if (name == "missions" || name == "missions-a") { house_ = 0; open(MenuPage::Missions); }
    else if (name == "missions-h") { house_ = 1; open(MenuPage::Missions); }
    else if (name == "missions-o") { house_ = 2; open(MenuPage::Missions); }
    else if (name == "briefing") { briefingFromCustom_ = briefingFromCommunity_ = false; open(MenuPage::Briefing); }
    else if (name.rfind("briefing-a", 0) == 0 && name.size() > 10 && std::isdigit(static_cast<unsigned char>(name[10]))) {
        // briefing-a<index>[-as<n>]: an Atreides campaign mission (list index) with the n-th
        // "Play as" entry picked (screenshots of the play-as choice).
        house_ = 0;
        missionSel_[0] = std::atoi(name.c_str() + 10);
        briefingFromCustom_ = briefingFromCommunity_ = false;
        open(MenuPage::Briefing);
        if (const MissionInfo* m = selectedMission()) refreshPlayAs(*m);
        if (const auto at = name.find("-as"); at != std::string::npos) playAsSel_ = std::atoi(name.c_str() + at + 3);
    }
    else if (name == "custom") open(MenuPage::Custom);
    else if (name == "community") { communityTab_ = 0; open(MenuPage::Community); }
    else if (name == "community-maps") { communityTab_ = 1; open(MenuPage::Community); }
    else if (name == "community-campaign") {
        // The largest campaign with an arc structure if any (a representative page).
        communitySel_ = 0;
        for (int i = 0; i < int(catalog_.community.size()); ++i)
            if (catalog_.community[std::size_t(i)].chapters > 0) communitySel_ = i;
        communityRowSel_ = -1;
        open(MenuPage::CommunityCampaign);
    }
    else if (name == "community-briefing") {
        communitySel_ = 0;
        const auto rows = catalog_.community.empty() ? std::vector<CommunityRow>{}
                                                     : communityRows(catalog_, catalog_.community[0], progress_);
        if (!rows.empty()) customSel_ = rows[0].mission;
        briefingFromCustom_ = true;
        briefingFromCommunity_ = true;
        open(MenuPage::Briefing);
    }
    else if (name == "skirmish") open(MenuPage::Skirmish);
    else if (name == "options") open(MenuPage::Options);
    else if (name == "graphics") open(MenuPage::Graphics);
    else if (name.rfind("artpack", 0) == 0) return openArtPackPreview(name);
    else if (name.rfind("welcome", 0) == 0) return openWelcomePreview(name);
    else if (name.rfind("updates", 0) == 0) return openUpdatesPreview(name);
    else if (name == "graphics-original" || name == "graphics-enhanced" || name == "graphics-3d") {
        // Screenshots of a preset tab: the preset applied (one-frame runs never save settings).
        const bool no3d = name == "graphics-3d" && !Settings::modelPackInstalled();  // the tab only explains
        if (!no3d) settings_.applyPreset(Settings::graphicsPresetFrom(name.substr(9)));
        open(MenuPage::Graphics);
        if (no3d) graphicsTab_ = static_cast<int>(Settings::GraphicsPreset::ThreeD);
    }
    else if (name == "graphics-custom" || name == "graphics-custom-end") {  // -end: scrolled to the bottom
        open(MenuPage::Graphics);
        graphicsTab_ = static_cast<int>(Settings::GraphicsPreset::Custom);
        if (name == "graphics-custom-end") graphicsScroll_ = 1e6f;
    }
    else if (name.rfind("graphics-custom-", 0) == 0) {  // graphics-custom-<PictureOption number>: that row compared
        open(MenuPage::Graphics);
        graphicsTab_ = static_cast<int>(Settings::GraphicsPreset::Custom);
        forcedProbe_ = name.substr(16);
    }
    else if (name == "datadir") open(MenuPage::DataDir);
    else if (name == "datadir-progress") { previewImport_ = true; open(MenuPage::DataDir); }
    else if (name == "datadir-nocutscenes") { previewCutscenes_ = 0; open(MenuPage::DataDir); }
    else if (name == "load") open(MenuPage::Load);
    else if (name == "movies") open(MenuPage::Movies);
    else return false;
    return true;
}

void Menu::openCampaignSave(const game::SaveMeta& m) {
    page_ = MenuPage::Campaign;
    loadError.clear();
    campaign_->openSaved(m);
}

void Menu::returnFromGame(const GameStart& start, bool won, const game::ScoreInput* score) {
    if (start.skirmish) {
        // Original: the result page (score.uil), Continue -> the lobby.
        if (!settings_.enhanced() && score) {
            page_ = MenuPage::Original;
            original_->openScore(*score);
        } else {
            open(MenuPage::Skirmish);
        }
        return;
    }
    int house = 0, mission = 0;
    // Played as another side (not original): no campaign / community progress, back to the list.
    const bool playAs = start.playAsSide >= 0;
    if (playAs) won = false;
    // (also `--map A1V1.MAP` runs: the original's GameType 0 is any [AHO]<n>V<v> mission)
    if (!playAs && start.campaignHouse != kCustomMissionsHouse && CampaignScreens::parseCampaignMap(start.map, house, mission)) {
        page_ = MenuPage::Campaign;
        if (score) {
            // Game__MainLoopFrame 0x448550: a win raises MissionNumber (mission 9 -> 10, the
            // end movie and the credits), then the score page; a loss keeps it (MVE_LOSE ->
            // score -> globe, FUN_004738b0).
            game::ScoreInput in = *score;
            in.myHouse = house;
            in.missionNumber = won ? mission + 1 : mission;
            campaign_->openScore(in);
            // ... after the movie: MVE_LOSE (lost.uil, "mve_lost" + the house letter) or, after
            // the last mission, MVE_<HOUSE>_END (AMisEnd.uil etc.: mve_<house>_wins).
            static const char* const kHouseKeys[3] = {"atreides", "harkonnen", "ordos"};
            if (movies_) {
                if (!won) movies_->play("mve_lost", house);
                else if (mission >= 9) movies_->play(std::string("mve_") + kHouseKeys[house] + "_wins", house);
            }
        } else {
            campaign_->openGlobe(house, mission);
        }
        return;
    }
    if (start.campaignHouse >= 0 && start.campaignHouse < 3) {
        house_ = start.campaignHouse;
        const int n = int(catalog_.campaign[house_].size());
        int sel = start.missionIndex;
        if (won) {
            // Next mission number (skip the alternative "b" variant of the same mission).
            const int num = sel >= 0 && sel < n ? catalog_.campaign[house_][sel].number : 0;
            while (sel + 1 < n && catalog_.campaign[house_][sel].number == num) ++sel;
            if (sel < n && catalog_.campaign[house_][sel].number == num) sel = start.missionIndex;
        }
        missionSel_[house_] = std::clamp(sel, 0, std::max(0, n - 1));
        open(MenuPage::Missions);
        return;
    }
    if (start.campaignHouse == kCommunityHouse || start.campaignHouse == kCustomMissionsHouse) {
        // A won community mission counts for every campaign that has it.
        if (won) {
            recordCommunityWin(catalog_, progress_, std::filesystem::path(start.map).stem().string());
            saveCommunityProgress();
        }
    }
    if (start.campaignHouse == kCommunityHouse) {
        // The campaign it was started from (a loaded save has no id: the first one with the map).
        const std::string stem = std::filesystem::path(start.map).stem().string();
        int found = -1;
        for (int i = 0; i < int(catalog_.community.size()) && found < 0; ++i)
            if (catalog_.community[std::size_t(i)].id == start.communityCampaign) found = i;
        for (int i = 0; i < int(catalog_.community.size()) && found < 0; ++i)
            for (const auto& st : catalog_.community[std::size_t(i)].steps)
                for (const auto& s2 : st.stems)
                    if (found < 0 && s2.size() == stem.size() &&
                        std::equal(s2.begin(), s2.end(), stem.begin(), [](char a, char b) {
                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                        }))
                        found = i;
        if (found >= 0) communitySel_ = found;
        communityRowSel_ = -1;  // the next mission
        if (!settings_.enhanced() && hasData()) {
            page_ = MenuPage::Original;
            original_->openCommunityCampaign(communitySel_, -1);
            return;
        }
        open(MenuPage::CommunityCampaign);
        return;
    }
    if (start.campaignHouse == kCustomMissionsHouse) {
        // Original: back to the custom missions page of the mission select screen.
        if (!settings_.enhanced() && hasData()) {
            page_ = MenuPage::Original;
            original_->openCommunity(1, start.missionIndex);
            return;
        }
        communityTab_ = 1;
        customSel_ = std::max(0, start.missionIndex);
        open(MenuPage::Community);
        return;
    }
    open(MenuPage::Main);
}

std::string Menu::musicTrack() const {
    if (page_ == MenuPage::Main) return "options";
    if (page_ == MenuPage::Original) {
        const std::string o = original_->musicOverride();  // the music page's Play / Stop
        if (o == "-") return {};
        if (!o.empty()) return o;
        if (original_->scorePage()) return "score";
        // The main menu and the dialogs over it keep options.aud; other pages are silent.
        const std::string top = original_->current();
        for (const char* m : {"MAINMENU", "DIFFICULTY", "MULTIPLAYERTYPE", "SETTINGS", "OPTIONS", "SOUNDOPTIONS", "LOAD",
                              "NO_SAVED_GAMES"})
            if (top == m) return "options";
        return {};
    }
    if (page_ == MenuPage::Campaign && campaign_->scorePage()) return "score";
    return {};
}

int Menu::cursorId() const { return page_ == MenuPage::Campaign ? campaign_->cursor() : 0; }
std::vector<MenuSound> Menu::takeSounds() {
    std::vector<MenuSound> s = campaign_->takeSounds();
    for (auto& m : original_->takeSounds()) s.push_back(m);
    return s;
}
void Menu::setSpeechBusy(bool b) { campaign_->setSpeechBusy(b); }
void Menu::setPersistent(bool p) {
    campaign_->persistHighScores = p;
    persistent_ = p;
}

// ---- drawing helpers -----------------------------------------------------------------------

namespace {
float gForcedPhoneDp = 0;                   // --phone-menus D (Menu::forcePhoneUi)
std::array<float, 4> gForcedSafeInsets{};  // --safe-insets with --phone-menus (points: left, top, right, bottom)
}  // namespace

void Menu::forcePhoneUi(float dpPixels, const std::array<float, 4>& safeInsetsPt) {
    gForcedPhoneDp = dpPixels;
    gForcedSafeInsets = safeInsetsPt;
}

bool Menu::phoneUi() const {
    if (gForcedPhoneDp > 0) return true;
    if constexpr (!kTouchPlatform) return false;  // desktop: the 640x400 stage, always
    if (settings_.touchLayout == Settings::TouchLayout::Tablet) return false;
    if (settings_.touchLayout == Settings::TouchLayout::Phone || isPhoneDevice()) return true;
    if constexpr (kIOS) {  // any window under 600 pt on its short side (Split View / Slide Over too)
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        return w > 0 && h > 0 && std::min(w, h) < 600;
    }
    return false;
}

SDL_FRect Menu::safeArea() const {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    if (gForcedPhoneDp > 0) {
        const auto& in = gForcedSafeInsets;
        const float d = gForcedPhoneDp;
        return {in[0] * d, in[1] * d, w - (in[0] + in[2]) * d, h - (in[1] + in[3]) * d};
    }
    SDL_Rect a{};
    if (!SDL_GetWindowSafeArea(window_, &a) || a.w <= 0 || a.h <= 0) return {0, 0, float(w), float(h)};
    const float k = windowToPixels(window_);
    return {a.x * k, a.y * k, a.w * k, a.h * k};
}

float Menu::dp() const { return gForcedPhoneDp > 0 ? gForcedPhoneDp : pixelsPerPoint(r_); }

void Menu::backdrop(const render::Texture& t, float dim) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    st_.s = std::min(w / 640.0f, h / 400.0f);
    st_.x = std::round((w - 640 * st_.s) / 2);
    st_.y = std::round((h - 400 * st_.s) / 2);
    ts_ = Ui::bodyScaleFor(w, h);
    ui_.textScale = Ui::labelScaleFor(w, h);
    if (phoneUi()) {  // labels ~16 sp, body ~14 sp (12.5 px of text per scale step)
        ui_.textScale = std::max(ui_.textScale, std::round(16 * dp() / 12.5f * 2) / 2);
        ts_ = std::max(ts_, std::round(13 * dp() / 12.5f * 2) / 2);
    }
    ui_.minRowH = phoneUi() ? std::round(40 * dp()) : 0;
    ui_.bodyScale = ts_;
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    if (t) {
        // Fill the letterbox with a blurred-looking, darkened "cover" copy of the picture.
        const float cs = std::max(w / 640.0f, h / 400.0f) * 1.08f;
        const SDL_FRect cover{(w - 640 * cs) / 2, (h - 400 * cs) / 2, 640 * cs, 400 * cs};
        ui_.image(t.get(), cover, 0.28f);
        const SDL_FRect stage = st_.r(0, 0, 640, 400);
        ui_.image(t.get(), stage);
        // Soft edges where the stage meets the letterbox.
    } else {
        ui_.gradient({0, 0, float(w), float(h)}, Colour{40, 26, 14, 255}, Colour{6, 4, 3, 255});
    }
    if (dim > 0) ui_.fill({0, 0, float(w), float(h)}, Colour{0, 0, 0, static_cast<unsigned char>(255 * dim)});
}

void Menu::title(const std::string& text, float x, float y) {
    const SDL_FRect p = st_.r(x, y, 0, 0);
    const float s = std::round(ui_.textScale * 1.5f);
    ui_.text(text, p.x, p.y, theme::kTitle, s);
    const float w = ui_.textWidth(text, s);
    ui_.gradient({p.x, p.y + ui_.textHeight(s) + 4 * ts_, w + 40 * ts_, 2 * ts_}, theme::kAccent, Colour{240, 184, 80, 0});
}

const MissionInfo* Menu::selectedMission() const {
    if (briefingFromCustom_) {
        return customSel_ >= 0 && customSel_ < int(catalog_.custom.size()) ? &catalog_.custom[customSel_] : nullptr;
    }
    const auto& list = catalog_.campaign[house_];
    const int sel = missionSel_[house_];
    return sel >= 0 && sel < int(list.size()) ? &list[sel] : nullptr;
}

GameStart Menu::startFor(const MissionInfo& m, int house, int index) const {
    GameStart s;
    s.map = m.map;
    s.title = house >= kCustomMissionsHouse ? m.name : std::string(kHouseNames[house]) + " - " + m.shortName;
    s.campaignHouse = house;
    s.missionIndex = index;
    s.briefing = m.briefing;
    s.sk.difficulty = settings_.skirmishDifficulty;
    return s;
}

// ---- frame ---------------------------------------------------------------------------------

MenuResult Menu::frame() {
    if (dataDir_.empty() && page_ != MenuPage::DataDir) page_ = MenuPage::DataDir;
    applyInstalledArtPack();
    if (welcomeDue()) {  // first run: once the game data is there
        SDL_Log("First run: welcome page");
        welcomeStep_ = 0;
        open(MenuPage::Welcome);
    }
    ui_.beginFrame();
    MenuResult res;
    // A graphics switch on a settings page hands the screen to the other menu set.
    if (hasData()) {
        const bool modernTwin = page_ == MenuPage::Main || page_ == MenuPage::Skirmish || page_ == MenuPage::Options ||
                                page_ == MenuPage::Load;
        if (!settings_.enhanced() && modernTwin) {
            open(page_);
        } else if (settings_.enhanced() && page_ == MenuPage::Original) {
            const std::string top = original_->current();
            if (top == "SETTINGS" || top == "OPTIONS" || top == "SOUNDOPTIONS") open(MenuPage::Options);
            else if (top == "SKIRMISH" || top == "MULTIPLAYERTYPE") open(MenuPage::Skirmish);
            else if (top == "LOAD") open(MenuPage::Load);
            else if (top != "CREDITS" && top != "SCORE") open(MenuPage::Main);
        }
    }
    // The modern pages draw their text in the TrueType UI font (Cyrillic, matches the in-game
    // panel); the original .uil pages (campaign flow, Graphics: Original) keep the bitmap font.
    Ui::ModernScope modern(ui_, page_ != MenuPage::Original && page_ != MenuPage::Campaign);
    switch (page_) {
    case MenuPage::Original: res = pageOriginal(); break;
    case MenuPage::Main: res = pageMain(); break;
    case MenuPage::Campaign: res = pageCampaign(); break;
    case MenuPage::Missions: res = pageMissions(false); break;
    case MenuPage::Custom: res = pageMissions(true); break;
    case MenuPage::Briefing: res = pageBriefing(); break;
    case MenuPage::Skirmish: res = pageSkirmish(); break;
    case MenuPage::Options: res = pageOptions(); break;
    case MenuPage::Graphics: res = pageGraphics(); break;
    case MenuPage::ArtPack: res = pageArtPack(); break;
    case MenuPage::Updates: res = pageUpdates(); break;
    case MenuPage::DataDir: res = pageDataDir(); break;
    case MenuPage::Load: res = pageLoad(); break;
    case MenuPage::Community: res = pageCommunity(); break;
    case MenuPage::CommunityCampaign: res = pageCommunityCampaign(); break;
    case MenuPage::Movies: res = pageMovies(); break;
    case MenuPage::Welcome: res = pageWelcome(); break;
    }
    ui_.endFrame();
    return res;
}

MenuResult Menu::pageMain() {
    backdrop(title_);
    MenuResult res;
    if (phoneUi()) {  // a 2 x 4 grid of large buttons under the title picture's logo
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r_, &w, &h);
        const float d = dp(), bh = std::round(52 * d), gap = std::round(10 * d);
        const float bw = std::round(std::min(w * 0.3f, 300 * d));
        const float gw = 2 * bw + gap, gh = 4 * bh + 3 * gap;
        const float x0 = std::round((w - gw) / 2), y0 = std::round(std::max(h * 0.30f, h - gh - 24 * d));
        ui_.fill({x0 - gap, y0 - gap, gw + 2 * gap, gh + 2 * gap}, Colour{0, 0, 0, 140});
        int i = 0;
        auto cell = [&] {
            const SDL_FRect r{x0 + (i % 2) * (bw + gap), y0 + (i / 2) * (bh + gap), bw, bh};
            ++i;
            return r;
        };
        if (ui_.button(cell(), tr("Campaign"), ButtonStyle::Primary)) {
            open(MenuPage::Campaign);
            if (movies_) movies_->play("mve_intro2");
        }
        if (ui_.button(cell(), tr("Skirmish"))) open(MenuPage::Skirmish);
        if (ui_.button(cell(), tr("Select mission"))) open(MenuPage::Missions);
        if (ui_.button(cell(), tr("Community"), ButtonStyle::Normal, !catalog_.custom.empty())) {
            communityTab_ = 0;
            open(MenuPage::Community);
        }
        if (ui_.button(cell(), tr("Load game"))) open(MenuPage::Load);
        if (ui_.button(cell(), tr("Options"))) open(MenuPage::Options);
        if (ui_.button(cell(), tr("Movies"))) open(MenuPage::Movies);
        if (ui_.button(cell(), tr("Quit"))) res.kind = MenuResult::Quit;
        if (ui_.takeBack()) res.kind = MenuResult::Quit;
        return res;
    }
    // Buttons inside the empty frame of the title picture (inner area ~232..406 x 167..317).
    const float x = 240, w = 158, h = 16, gap = 2;
    float y = 170;
    const SDL_FRect frame = st_.r(232, 167, 175, 151);
    ui_.fill(frame, Colour{0, 0, 0, 120});
    if (ui_.button(st_.r(x, y, w, h), tr("Campaign"), ButtonStyle::Primary)) {
        open(MenuPage::Campaign);
        // The original's Campaign -> DIFFICULTY -> MVE_INTRO2 (Intro2.uil) -> HOUSESELECTION.
        if (movies_) movies_->play("mve_intro2");
    }
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Select mission"))) open(MenuPage::Missions);
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Skirmish"))) open(MenuPage::Skirmish);
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Community"), ButtonStyle::Normal, !catalog_.custom.empty())) {
        communityTab_ = 0;
        open(MenuPage::Community);
    }
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Load game"))) open(MenuPage::Load);
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Options"))) open(MenuPage::Options);
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Movies"))) open(MenuPage::Movies);
    y += h + gap;
    if (ui_.button(st_.r(x, y, w, h), tr("Quit"))) res.kind = MenuResult::Quit;
    if (ui_.takeBack()) res.kind = MenuResult::Quit;
    const SDL_FRect foot = st_.r(632, 388, 0, 0);
    ui_.text(tr("native rebuild"), foot.x, foot.y - ui_.textHeight(ts_), Colour{200, 180, 150, 160}, ts_, Align::Right);
    return res;
}

bool Menu::takeFolderResult(MenuResult& res) {
    std::lock_guard lock(folderMutex_);
    if (!folderPicked_) return false;
    folderPicked_ = false;
    const std::string d = normalizeDataDir(pickedFolder_);
    if (d.empty()) {
        dataDirError = tr("No Dune 2000 data found there.");
        SDL_Log("%s (%s)", dataDirError.c_str(), pickedFolder_.c_str());
        return false;
    }
    dataDirError.clear();
    res.kind = MenuResult::DataDirChosen;
    res.dataDir = d;
    return true;
}

MenuResult Menu::pageOriginal() {
    MenuResult res;
    const OriginalMenuAction a = original_->frame();
    switch (a.kind) {
    case OriginalMenuAction::None: break;
    case OriginalMenuAction::Quit: res.kind = MenuResult::Quit; break;
    case OriginalMenuAction::Campaign: open(MenuPage::Campaign); break;
    case OriginalMenuAction::CampaignMission:
        page_ = MenuPage::Campaign;
        campaign_->openMission(a.mission);
        break;
    case OriginalMenuAction::Start:
        res.kind = MenuResult::Start;
        res.start = a.start;
        break;
    case OriginalMenuAction::Load:
        res.kind = MenuResult::Load;
        res.slot = a.slot;
        break;
    case OriginalMenuAction::DataDir: open(MenuPage::DataDir); break;
    case OriginalMenuAction::Enhanced: open(MenuPage::Graphics); break;  // a preset picked on SETTINGS
    case OriginalMenuAction::ArtPack: openArtPackFrom(""); break;       // SETTINGS -> Art Pack (Back: SETTINGS)
    case OriginalMenuAction::CustomMission:
        if (a.slot >= 0 && a.slot < int(catalog_.custom.size())) {
            customSel_ = a.slot;
            res.kind = MenuResult::Start;
            res.start = startFor(catalog_.custom[std::size_t(a.slot)], kCustomMissionsHouse, a.slot);
            res.start.playAsSide = a.playAsSide;
        }
        break;
    case OriginalMenuAction::CommunityMission:
        if (a.slot >= 0 && a.slot < int(catalog_.custom.size()) && a.campaign >= 0 &&
            a.campaign < int(catalog_.community.size())) {
            customSel_ = a.slot;
            communitySel_ = a.campaign;
            res.kind = MenuResult::Start;
            res.start = startFor(catalog_.custom[std::size_t(a.slot)], kCommunityHouse, a.slot);
            res.start.communityCampaign = catalog_.community[std::size_t(a.campaign)].id;
            res.start.playAsSide = a.playAsSide;
        }
        break;
    }
    takeFolderResult(res);
    return res;
}

// Save game slots (platform/saves.hpp): pick one and load it.
MenuResult Menu::pageLoad() {
    MenuResult res;
    backdrop(back_, 0.45f);
    title(tr("Load game"), 24, 16);
    std::vector<ListRow> rows;
    // Detail column: what the slot holds ("Campaign map" / "Battle") and when it was saved.
    for (const SaveSlot& sl : slots_) {
        std::string detail = slotDetail(sl);
        if (sl.valid) detail = slotKindLabel(sl, true) + std::string("  ") + detail;
        rows.push_back({slotTitle(sl, true), detail, sl.valid});
    }
    const SDL_FRect listR = st_.r(20, 60, 360, 296);
    const SDL_FRect info = st_.r(392, 60, 228, 296);
    const int activated = ui_.list(listR, rows, slotSel_, slotScroll_);
    const SaveSlot* sel = slotSel_ >= 0 && slotSel_ < int(slots_.size()) ? &slots_[slotSel_] : nullptr;
    ui_.panel(info);
    float x = info.x + 12 * st_.s, y = info.y + 12 * st_.s;
    if (sel && sel->valid) {
        const game::SaveMeta& m = sel->meta;
        ui_.text(ui_.ellipsize(m.name.empty() ? m.map : m.name, info.w - 24 * st_.s, ts_ + 1), x, y, theme::kTitle, ts_ + 1);
        y += ui_.textHeight(ts_ + 1) + 10 * ts_;
        auto line = [&](const std::string& t) {
            ui_.text(ui_.ellipsize(t, info.w - 24 * st_.s, ts_), x, y, theme::kText, ts_);
            y += ui_.textHeight(ts_) + 5 * ts_;
        };
        if (m.kind == game::SaveKind::CampaignMap) {
            static const char* const kHouse[3] = {"Atreides", "Harkonnen", "Ordos"};
            static const char* const kSkill[3] = {"Easy", "Normal", "Hard"};
            line(tr("Campaign map"));
            line(trf("Territory map before mission %d", m.missionNumber));
            line(trs("House: %s", tr(kHouse[std::clamp(m.campaignHouse, 0, 2)])));
            line(trs("Difficulty: %s", tr(kSkill[std::clamp(m.difficulty, 0, 2)])));
        } else {
            line(m.skirmish ? trf("Skirmish - %d computer players", int(m.aiPlayers)) : std::string(tr("Campaign mission")));
            line(trs("Map: %s", m.map));
            const std::uint32_t secs = m.gameTicks / kTicksPerGameSecond;
            line(trf("Game time: %u:%02u:%02u", secs / 3600, secs / 60 % 60, secs % 60));
        }
        line(trs("Saved: %s", slotDetail(*sel)));
    } else if (sel && sel->used) {
        ui_.text(tr("This save cannot be loaded:"), x, y, theme::kBad, ts_);
        y += ui_.textHeight(ts_) + 6 * ts_;
        ui_.wrapped(sel->error, SDL_FRect{x, y, info.w - 24 * st_.s, info.y + info.h - y - 10 * st_.s}, theme::kDim, ts_);
    } else {
        ui_.text(savesDir().empty() ? tr("No folder for save games.") : tr("Empty slot."), x, y, theme::kDim, ts_);
        y += ui_.textHeight(ts_) + 6 * ts_;
        ui_.wrapped(tr("Save a running game from its Esc menu, or the campaign from the territory map."), SDL_FRect{x, y, info.w - 24 * st_.s, 60 * st_.s},
                    theme::kDim, ts_);
    }
    if (!loadError.empty())
        ui_.wrapped(loadError, SDL_FRect{x, info.y + info.h - 60 * st_.s, info.w - 24 * st_.s, 50 * st_.s}, theme::kBad, ts_);
    const bool canLoad = sel && sel->valid;
    if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) {
        if (loadFromGlobe_) {
            // Opened from the territory map (Globe.uil Load = JumpToMenu LOAD): Cancel = BackMenu.
            loadFromGlobe_ = false;
            loadError.clear();
            page_ = MenuPage::Campaign;
        } else {
            open(MenuPage::Main);
        }
    }
    if (ui_.button(st_.r(476, 366, 144, 22), tr("Load"), ButtonStyle::Primary, canLoad) || (activated >= 0 && canLoad)) {
        res.kind = MenuResult::Load;
        res.slot = slotSel_;
    }
    return res;
}

// Rewatch the cutscenes (not in the original, which only has "Replay intro" on its main menu).
MenuResult Menu::pageMovies() {
    MenuResult res;
    backdrop(back_, 0.45f);
    title(tr("Movies"), 24, 16);
    const auto& all = Movies::gallery();
    std::vector<ListRow> rows;
    std::vector<bool> have;
    for (const auto& e : all) {
        const bool ok = movies_ && movies_->available(e.name, e.house);
        have.push_back(ok);
        rows.push_back({movieTitle(e), ok ? std::string() : std::string(tr("not found")), ok});
    }
    const SDL_FRect listR = st_.r(20, 60, 360, 296);
    const SDL_FRect info = st_.r(392, 60, 228, 296);
    const int activated = ui_.list(listR, rows, movieSel_, movieScroll_);
    movieSel_ = std::clamp(movieSel_, 0, int(all.size()) - 1);
    ui_.panel(info);
    const float x = info.x + 12 * st_.s;
    float y = info.y + 12 * st_.s;
    const float tw = info.w - 24 * st_.s;
    std::size_t found = 0;
    for (bool b : have) found += b;
    ui_.text(trf("%d of %d movies found", int(found), int(all.size())), x, y, theme::kTitle, ts_);
    y += ui_.textHeight(ts_) + 10 * ts_;
    std::string help = tr("The VQA files are looked up in:");
    if (movies_)
        for (const std::string& f : movies_->searchFolders()) help += "\n" + f;
    help += std::string("\n\n") + tr("The data-folder page can add a Gruntmods cutscene installer (.exe).");
    ui_.wrapped(help, SDL_FRect{x, y, tw, info.y + info.h - y - 10 * st_.s}, theme::kDim, ts_);
    const bool canPlay = movieSel_ >= 0 && movieSel_ < int(have.size()) && have[std::size_t(movieSel_)];
    if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Main);
    if (ui_.button(st_.r(476, 366, 144, 22), tr("Play"), ButtonStyle::Primary, canPlay) || (activated >= 0 && canPlay)) {
        const auto& e = all[std::size_t(movieSel_)];
        movies_->play(e.name, e.house, true);
    }
    return res;
}

MenuResult Menu::pageCampaign() {
    MenuResult res;
    const CampaignAction a = campaign_->frame();
    switch (a.kind) {
    case CampaignAction::None: break;
    case CampaignAction::MainMenu: open(MenuPage::Main); break;
    case CampaignAction::Credits: openOriginal({"MAINMENU", "CREDITS"}); break;
    case CampaignAction::LoadPage:
        open(MenuPage::Load);
        loadFromGlobe_ = true;
        break;
    case CampaignAction::LoadSlot:
        res.kind = MenuResult::Load;
        res.slot = a.slot;
        break;
    case CampaignAction::CustomMissions:
        communityTab_ = 1;
        open(MenuPage::Community);
        break;
    case CampaignAction::Start:
        res.kind = MenuResult::Start;
        res.start = a.start;
        break;
    }
    return res;
}

MenuResult Menu::pageMissions(bool custom) {
    MenuResult res;
    backdrop(custom ? back_ : acct_[house_], custom ? 0.35f : 0.55f);
    const auto& list = custom ? catalog_.custom : catalog_.campaign[house_];
    int& sel = custom ? customSel_ : missionSel_[house_];
    title(tr(custom ? "Custom missions" : "Select mission"), 24, 16);
    if (!custom) {
        // House tabs (the campaign flow itself is the Campaign button's territory map).
        for (int hIdx = 0; hIdx < 3; ++hIdx)
            if (ui_.button(st_.r(300 + hIdx * 108, 18, 100, 20), tr(kHouseNames[hIdx]),
                           hIdx == house_ ? ButtonStyle::Primary : ButtonStyle::Normal)) {
                house_ = hIdx;
                listScroll_ = 0;
            }
    }
    if (custom)
        ui_.text(tr("Gruntmods/community missions (extended event system: docs/extended-events.md)"),
                 st_.r(24, 44, 0, 0).x, st_.r(24, 44, 0, 0).y, theme::kDim, ts_);

    std::vector<ListRow> rows;
    rows.reserve(list.size());
    for (const auto& m : list) {
        std::string stem = m.map.substr(0, m.map.find_last_of('.'));
        rows.push_back({custom ? m.name : missionLabel(m.shortName), custom ? (m.author.empty() ? stem : m.author) : stem});
    }
    const SDL_FRect listR = st_.r(20, 60, custom ? 300 : 220, 296);
    const SDL_FRect info = st_.r(custom ? 332 : 252, 60, custom ? 288 : 368, 296);
    if (ui_.list(listR, rows, sel, listScroll_) >= 0 && sel < int(list.size())) {
        briefingFromCustom_ = custom;
        briefingFromCommunity_ = false;
        open(MenuPage::Briefing);
        return res;
    }
    ui_.panel(info);
    if (sel >= 0 && sel < int(list.size())) {
        const MissionInfo& m = list[sel];
        float x = info.x + 14 * st_.s, y = info.y + 12 * st_.s;
        const int lh = m.house >= 0 && m.house < 3 ? m.house : -1;
        if (lh >= 0 && logo_[lh]) {
            const float lw = 52 * st_.s, lhh = 55 * st_.s;
            ui_.image(logo_[lh].get(), {info.x + info.w - lw - 12 * st_.s, y, lw, lhh});
        }
        ui_.text(ui_.ellipsize(custom ? m.name : missionLabel(m.shortName), info.w - 90 * st_.s, 2 * ts_), x, y, theme::kTitle, 2 * ts_);
        y += ui_.textHeight(2 * ts_) + 6 * ts_;
        const std::string sub = m.author.empty() ? m.map : trs2("%s  -  by %s", m.map, m.author);
        ui_.text(ui_.ellipsize(sub, info.w - 28 * st_.s, ts_), x, y, theme::kDim, ts_);
        y = info.y + 74 * st_.s;
        const SDL_FRect textR{x, y, info.w - 28 * st_.s, info.y + info.h - y - 10 * st_.s};
        const std::string txt = m.briefing.empty() ? std::string(tr("(No briefing text.)")) : m.briefing;
        ui_.wrapped(txt, textR, theme::kText, ui_.fitScale(txt, textR, {ui_.textScale, ts_}));
    } else {
        ui_.text(tr("No missions found."), info.x + 14 * st_.s, info.y + 14 * st_.s, theme::kDim, ts_);
    }
    if (ui_.button(st_.r(20, 366, 96, 22), tr("Back")) || ui_.takeBack()) open(custom ? MenuPage::Campaign : MenuPage::Main);
    if (ui_.button(st_.r(476, 366, 144, 22), tr("Briefing"), ButtonStyle::Primary, sel >= 0 && sel < int(list.size()))) {
        briefingFromCustom_ = custom;
        briefingFromCommunity_ = false;
        open(MenuPage::Briefing);
    }
    return res;
}

MenuResult Menu::pageBriefing() {
    MenuResult res;
    const MissionInfo* m = selectedMission();
    const int house = briefingFromCustom_ ? (m && m->house >= 0 && m->house < 3 ? m->house : 0) : house_;
    backdrop(briefingFromCustom_ ? back_ : acct_[house], 0.6f);
    const MenuPage backTo = briefingFromCommunity_ ? MenuPage::CommunityCampaign
                            : briefingFromCustom_  ? MenuPage::Community
                                                   : MenuPage::Missions;
    if (!m) { open(backTo); return res; }
    const SDL_FRect box = st_.r(40, 16, 560, 336);
    ui_.panel(box, Colour{14, 10, 7, 225});
    const float pad = 16 * st_.s;
    if (m->house >= 0 && m->house < 3 && logo_[m->house])
        ui_.image(logo_[m->house].get(), {box.x + pad, box.y + pad, 52 * st_.s, 55 * st_.s});
    const float tx = box.x + pad + 64 * st_.s;
    ui_.text(tr("MISSION BRIEFING"), tx, box.y + pad, theme::kAccent, ts_);
    const float hs = ui_.textScale + 1;
    ui_.text(ui_.ellipsize(briefingFromCustom_ ? m->name : std::string(tr(kHouseNames[house])) + " - " + uiUtf8(missionLabel(m->shortName)),
                           box.x + box.w - tx - pad, hs),
             tx, box.y + pad + ui_.textHeight(ts_) + 4 * ts_, theme::kTitle, hs);
    const std::string sub = m->author.empty() ? m->map : trs2("%s  -  by %s", m->map, m->author);
    ui_.text(ui_.ellipsize(sub, box.x + box.w - tx - pad, ts_), tx, box.y + pad + ui_.textHeight(ts_) + ui_.textHeight(hs) + 10 * ts_, theme::kDim, ts_);
    const float top = box.y + pad + 70 * st_.s;
    ui_.fill({box.x + pad, top - 8 * st_.s, box.w - 2 * pad, 1}, Colour{theme::kEdge.r, theme::kEdge.g, theme::kEdge.b, 180});
    const SDL_FRect textR{box.x + pad, top, box.w - 2 * pad, box.y + box.h - top - pad};
    // Scroll with the wheel / Up / Down (the text is usually short enough to fit).
    const std::string txt = briefingFromCustom_ ? communityBriefingText(*m, true)
                            : m->briefing.empty() ? std::string(tr("(No briefing text.)")) : m->briefing;
    const float total = ui_.wrapped(txt, textR, theme::kText, ui_.fitScale(txt, textR, {ui_.textScale, ts_}), briefScroll_);
    const float maxScroll = std::max(0.f, total - textR.h);
    briefScroll_ = std::clamp(briefScroll_, 0.f, maxScroll);
    if (maxScroll > 0) ui_.text(tr("(scroll for more)"), box.x + box.w - pad, box.y + box.h - pad / 2 - ui_.textHeight(ts_) / 2,
                                theme::kDim, ts_, Align::Right);
    const bool back = ui_.button(st_.r(40, 362, 110, 24), tr("Back"));
    // Play as any side (not original): only when the mission has more than one side to offer.
    refreshPlayAs(*m);
    if (playAs_.size() > 1) {
        std::vector<std::string> opts;
        for (std::size_t i = 0; i < playAs_.size(); ++i) opts.push_back(playAsLabel(playAs_, i));
        playAsSel_ = std::clamp(playAsSel_, 0, int(opts.size()) - 1);
        ui_.choice(st_.r(160, 362, 290, 24), tr("Play as"), playAsSel_, opts);
    }
    const bool go = ui_.button(st_.r(460, 362, 140, 24), tr("Start mission"), ButtonStyle::Primary);
    if (back || ui_.takeBack()) open(backTo);
    if (go) {
        res.kind = MenuResult::Start;
        if (briefingFromCommunity_ && communitySel_ >= 0 && communitySel_ < int(catalog_.community.size())) {
            res.start = startFor(*m, kCommunityHouse, customSel_);
            res.start.communityCampaign = catalog_.community[std::size_t(communitySel_)].id;
        } else {
            res.start = briefingFromCustom_ ? startFor(*m, kCustomMissionsHouse, customSel_) : startFor(*m, house_, missionSel_[house_]);
        }
        if (playAsSel_ > 0 && playAsSel_ < int(playAs_.size())) res.start.playAsSide = playAs_[std::size_t(playAsSel_)].side;
    }
    return res;
}

void Menu::refreshPlayAs(const MissionInfo& m) {
    if (playAsMap_ == m.map) return;
    playAsMap_ = m.map;
    playAsSel_ = 0;  // per start: a new mission begins on its own side
    playAs_ = playAsOptions(dataDir_, m.map);
}

void Menu::refreshPreview() {
    if (previewFor_ == mapSel_ || mapSel_ < 0 || mapSel_ >= int(catalog_.skirmish.size())) return;
    previewFor_ = mapSel_;
    try {
        const auto map = formats::loadMap(formats::findFileNoCase(dataDir_ + "/maps", catalog_.skirmish[mapSel_].file));
        // Preview with the _SPAWN.MIS tileset (BLOXBGBS); good enough for the overview colours.
        std::string tiles = formats::findFileNoCase(dataDir_, "BLOXBGBS.R16");
        preview_.build(map, tiles);
    } catch (const std::exception& e) {
        SDL_Log("Map preview: %s", e.what());
    }
}

MenuResult Menu::pageSkirmish() {
    MenuResult res;
    backdrop(multi_);
    // Rectangles: the 640x400 stage, or (phoneUi) a phone layout in pixels: map list on the left, the
    // preview and two columns of 48 dp choices on the right, Back / Start along the bottom.
    SDL_FRect listR = st_.r(28, 56, 282, 296), pv = st_.r(326, 56, 286, 128), backR = st_.r(84, 367, 82, 22),
              goR = st_.r(470, 367, 82, 22);
    std::function<SDL_FRect(int)> choiceR = [&](int i) { return st_.r(326, 204 + i * 25.0f, 286, 22); };
    const bool phone = phoneUi();
    if (phone) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r_, &w, &h);
        const float d = dp(), m = std::round(14 * d), bh = std::round(48 * d), gap = std::round(6 * d);
        const float ts = std::round(ui_.textScale * 1.3f);
        ui_.text(tr("Skirmish"), m, m, theme::kTitle, ts);
        const float top = m + ui_.textHeight(ts) + gap, barY = h - m - bh, bottom = barY - 2 * gap;
        backR = {m, barY, std::round(150 * d), bh};
        goR = {w - m - std::round(150 * d), barY, std::round(150 * d), bh};
        const float lw = std::round(w * 0.36f);
        listR = {m, top, lw, bottom - top};
        const float rx = m + lw + 2 * gap, rw = w - m - rx, cw = (rw - gap) / 2;
        const float choicesY = bottom - 3 * bh - 2 * gap;
        pv = {rx, top, rw, std::max(40 * d, choicesY - gap - top)};
        choiceR = [=](int i) { return SDL_FRect{rx + (i % 2) * (cw + gap), choicesY + (i / 2) * (bh + gap), cw, bh}; };
    } else {
        title(tr("Skirmish"), 30, 22);
    }
    std::vector<ListRow> rows;
    for (const auto& m : catalog_.skirmish) {
        std::string name = m.name, detail;
        if (const auto p = name.find(" ("); p != std::string::npos) {
            detail = name.substr(p + 2);
            if (!detail.empty() && detail.back() == ')') detail.pop_back();
            name = name.substr(0, p);
        }
        rows.push_back({name, detail});
    }
    ui_.list(listR, rows, mapSel_, listScroll_);
    refreshPreview();
    const SkirmishMap* map = mapSel_ >= 0 && mapSel_ < int(catalog_.skirmish.size()) ? &catalog_.skirmish[mapSel_] : nullptr;

    ui_.fill(pv, Colour{0, 0, 0, 170});
    preview_.draw({pv.x + 4, pv.y + 4, pv.w - 8, pv.h - 8});
    ui_.outline(pv, Colour{0, 0, 0, 220});
    if (map && !phone) {
        const std::string buf = trf("%d x %d cells  -  up to %d players", preview_.width(), preview_.height(), map->maxPlayers);
        ui_.text(ui_.ellipsize(buf, pv.w, ts_), pv.x, pv.y + pv.h + 4 * ts_, theme::kDim, ts_);
    }

    std::vector<std::string> houses = {tr(kHouseNames[0]), tr(kHouseNames[1]), tr(kHouseNames[2])};
    ui_.choice(choiceR(0), tr("Your house"), settings_.skirmishHouse, houses);
    const int maxAi = map ? std::clamp(map->maxPlayers - 1, 1, 7) : 7;
    std::vector<std::string> ais;
    for (int i = 1; i <= maxAi; ++i) ais.push_back(std::to_string(i));
    int aiIndex = std::clamp(settings_.skirmishAi, 1, maxAi) - 1;
    ui_.choice(choiceR(1), tr("Opponents"), aiIndex, ais);
    settings_.skirmishAi = aiIndex + 1;
    std::vector<std::string> credits;
    int creditIndex = 2;
    for (std::size_t i = 0; i < kCredits.size(); ++i) {
        credits.push_back(std::to_string(kCredits[i]));
        if (kCredits[i] == settings_.skirmishCredits) creditIndex = int(i);
    }
    ui_.choice(choiceR(2), tr("Starting credits"), creditIndex, credits);
    settings_.skirmishCredits = kCredits[creditIndex];
    std::vector<std::string> techs;
    for (int i = 1; i <= 7; ++i) techs.push_back(std::to_string(i));
    int techIndex = std::clamp(settings_.skirmishTech, 1, 7) - 1;
    ui_.choice(choiceR(3), tr("Tech level"), techIndex, techs);
    settings_.skirmishTech = techIndex + 1;
    ui_.choice(choiceR(4), tr("AI difficulty"), settings_.skirmishDifficulty, {tr("Easy"), tr("Normal"), tr("Hard")});
    std::vector<std::string> units = {tr("MCV only"), trf("MCV + %d", 3), trf("MCV + %d", 5), trf("MCV + %d", 10)};
    ui_.choice(choiceR(5), tr("Starting units"), unitsChoice_, units);

    const bool back = ui_.button(backR, tr("Back"));
    const bool go = ui_.button(goR, tr("Start"), ButtonStyle::Primary, map != nullptr);
    if (back || ui_.takeBack()) open(MenuPage::Main);
    if (go && map) {
        settings_.skirmishMap = map->stem;
        res.kind = MenuResult::Start;
        GameStart& s = res.start;
        s.map = map->file;
        s.skirmish = true;
        s.title = "Skirmish - " + rows[mapSel_].text;
        s.sk.playerHouse = settings_.skirmishHouse;
        s.sk.aiPlayers = settings_.skirmishAi;
        s.sk.techLevel = settings_.skirmishTech;
        s.sk.startingCredits = std::uint32_t(settings_.skirmishCredits);
        s.sk.difficulty = settings_.skirmishDifficulty;
        s.sk.unitCount = kUnits[std::clamp(unitsChoice_, 0, int(kUnits.size()) - 1)];
    }
    return res;
}

MenuResult Menu::pageOptions() {
    MenuResult res;
    backdrop(back_, 0.25f);
    // Rectangles in stage units through R(): the 640x400 stage, or (phoneUi) the panel stretched over the
    // screen width with ~48 dp rows (the rows scroll; Back along the bottom).
    float sx = st_.s, sy = st_.s, ox = st_.x, oy = st_.y;
    const bool phone = phoneUi();
    if (phone) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(r_, &w, &h);
        const float d = dp(), m = std::round(10 * d);
        sx = (w - 2 * m) / 440.0f;
        sy = 48 * d / 21.0f;
        ox = m - 100 * sx;
        oy = std::round(h - m - 48 * d) - 377 * sy;  // Back's row at the bottom edge
    }
    auto R = [&](float x0, float y0, float w0, float h0) { return SDL_FRect{ox + x0 * sx, oy + y0 * sy, w0 * sx, h0 * sy}; };
    if (phone) {
        const float ts = std::round(ui_.textScale * 1.3f);
        ui_.text(tr("Options"), R(110, 0, 0, 0).x, std::round(8 * dp()), theme::kTitle, ts);
    } else {
        title(tr("Options"), 110, 30);
    }
    SDL_FRect box = R(100, 56, 440, 318);
    if (phone) {  // from under the title to above Back
        const float top = std::round(8 * dp()) + ui_.textHeight(std::round(ui_.textScale * 1.3f)) + std::round(6 * dp());
        box.h = R(0, 377, 0, 0).y - std::round(6 * dp()) - top;
        box.y = top;
    }
    ui_.panel(box);
    const float x = 116, w = 408, h = 20, gap = 1;
    // The rows scroll inside the panel (Back stays below it): wheel, scroll bar, keyboard focus.
    const int optionRows = 6 + kLanguageOptionRows + kGameplayOptionRows;
    const float contentH = 2 + optionRows * (h + gap) + 4 + 30 + 22 + 4;
    SDL_FRect view = R(104, 59, 426, 312);
    if (phone) view = {box.x + 4 * sx, box.y + 3 * sy, box.w - 14 * sx, box.h - 6 * sy};
    float y = (ui_.beginScroll(view, optionsScroll_, contentH * sy, std::max(4.f, 5 * sy)) - oy) / sy + 2;
    {
        // Graphics: its own page (presets Original / Enhanced / 3D + Custom); the button names the
        // active preset.
        static const char* const kPreset[] = {"Original", "Enhanced", "3D", "Custom"};
        const int p = std::clamp(static_cast<int>(settings_.activePreset()), 0, 3);
        if (ui_.button(R(x, y, w, h), std::string(tr("Graphics")) + ": " + tr(kPreset[p]) + "  >"))
            open(MenuPage::Graphics);
    }
    y += h + gap;
    // The downloadable art pack: installed version / update / not installed (menu_artpack.cpp).
    if (ui_.button(R(x, y, w, h), artPackRowLabel() + "  >")) {
        artPackBack_.clear();
        open(MenuPage::ArtPack);
    }
    y += h + gap;
    // Is a newer OpenD2K out? (menu_update.cpp; checked when Options opens, never at startup).
    if (ui_.button(R(x, y, w, h), updatesRowLabel() + "  >")) open(MenuPage::Updates);
    y += h + gap;
    {
        // Cutscenes (Off / On / On + the intro at startup).
        int cut = !settings_.cutscenes ? 0 : settings_.introOnStartup ? 2 : 1;
        if (ui_.choice(R(x, y, w, h), tr("Cutscenes"), cut, {tr("Off"), tr("On"), tr("On + intro")})) {
            settings_.cutscenes = cut > 0;
            settings_.introOnStartup = cut == 2;
        }
    }
    y += h + gap;
    ui_.slider(R(x, y, w, h), tr("Music volume"), settings_.musicVolume);
    y += h + gap;
    ui_.slider(R(x, y, w, h), tr("Sound volume"), settings_.soundVolume);
    y += h + gap;
    {
        const float langY = y;
        y += languageOptionRows(ui_, settings_, [&](int i) { return R(x, langY + i * (h + gap), w, h); }) * (h + gap);
    }
    const float rowsY = y;
    y += gameplayOptionRows(ui_, settings_, [&](int i) { return R(x, rowsY + i * (h + gap), w, h); }) * (h + gap);
    y += 4;
    const SDL_FRect lab = R(x + 5, y, 0, 0);
    ui_.text(tr("Game data folder"), lab.x, lab.y, theme::kText, ui_.textScale);
    ui_.text(ui_.ellipsize(dataDir_, R(0, 0, w - 10, 0).w, ts_), lab.x, lab.y + ui_.textHeight(ui_.textScale) + 4 * ts_,
             theme::kDim, ts_);
    y += 30;
    if (ui_.button(R(x + 5, y, 150, 22), tr("Change..."))) open(MenuPage::DataDir);  // folder or installer
    if (!dataDirError.empty()) {
        const SDL_FRect e = R(x + 165, y + 5, 0, 0);
        ui_.text(ui_.ellipsize(dataDirError, R(0, 0, w - 170, 0).w, ts_), e.x, e.y, theme::kBad, ts_);
    }
    ui_.endScroll();
    if (ui_.button(R(100, 377, 110, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Main);
    std::lock_guard lock(folderMutex_);
    if (folderPicked_) {
        folderPicked_ = false;
        const std::string d = normalizeDataDir(pickedFolder_);
        if (d.empty()) dataDirError = tr("No Dune 2000 data found there.");
        else { dataDirError.clear(); res.kind = MenuResult::DataDirChosen; res.dataDir = d; }
    }
    return res;
}

// Options -> Graphics (docs/enhanced-graphics.md "Presets"): tabs Original / Enhanced / 3D are
// one-tap presets (a tap applies it; the tab shows a preview, one line on the look and its main
// values); Custom lists every graphics value grouped, and any change there makes the setup Custom.
// The Display rows (not part of a preset) follow on every tab. The active preset is underlined.
MenuResult Menu::pageGraphics() {
    using GP = Settings::GraphicsPreset;
    MenuResult res;
    backdrop(back_, 0.25f);
    title(tr("Graphics"), 50, 30);
    const float px = 40, pw = 560;  // the panel (wide: the Custom tab has a preview column)
    ui_.panel(st_.r(px, 56, pw, 318));
    static const char* const kNames[] = {"Original", "Enhanced", "3D", "Custom"};
    const GP active = settings_.activePreset();
    // 3D needs an installed OpenD2K 3D art pack: without one its tab only explains that (no preset applied).
    const bool can3d = Settings::modelPackInstalled();
    // Tabs: big enough to tap (24 page px = 48+ points on an iPad).
    {
        const float tx = px + 8, tw = pw - 16, th = 24, tgap = 6;
        const float each = (tw - 3 * tgap) / 4;
        for (int i = 0; i < 4; ++i) {
            const SDL_FRect r = st_.r(tx + i * (each + tgap), 63, each, th);
            Ui::BoldScope bold(ui_, i == graphicsTab_);
            const ButtonStyle style = i == graphicsTab_ ? ButtonStyle::Primary : i == 2 && !can3d ? ButtonStyle::Subtle : ButtonStyle::Normal;
            if (ui_.button(r, tr(kNames[i]), style)) {
                graphicsTab_ = i;
                graphicsScroll_ = 0;
                if (i < 3 && (i != 2 || can3d)) settings_.applyPreset(static_cast<GP>(i));  // one tap applies the preset
            }
            if (static_cast<int>(active) == i)  // the preset in use
                ui_.fill(st_.r(tx + i * (each + tgap) + 10, 63 + th + 1.5f, each - 20, 2), theme::kAccent);
        }
    }
    const bool custom = graphicsTab_ == 3;
    // Custom: the rows on the left, the preview column (now / with the row pointed at) on the right.
    const float x = px + 16, w = custom ? 352 : pw - 32, h = 20, gap = 1;
    const float bodyTs = ui_.textScale * 0.85f;  // running text of the preset tabs
    OptionListLayout layout{[&](float yy, float hh) { return st_.r(x, yy, w, hh); }, h, gap, 18, {}};
    layout.getArtPack = [this] { openArtPackFrom("graphics"); };  // the Custom tab's "Art" header
    const SDL_FRect view = st_.r(px + 4, 94, custom ? 368 : pw - 14, 277);
    const float pvW = 256, pvH = 160;  // preset preview (16:10 like the rendered frame)
    const float promptH = graphicsTab_ == 2 && artPackPromptWanted() ? kArtPackPromptH : 0;  // "download the 3D art pack?"
    const float contentH = custom ? graphicsOptionListHeight(layout) : pvH + 10 + promptH + layout.headerH + kDisplayOptionRows * (h + gap);
    const float y0 = (ui_.beginScroll(view, graphicsScroll_, (contentH + 6) * st_.s, std::max(4.f, 5 * st_.s)) - st_.y) / st_.s + 3;
    float y = y0;
    PictureProbe probe;
    if (custom) {
        graphicsOptionList(ui_, settings_, layout, y, window_, &probe);
    } else {
        // Preview: the curated scene rendered from the game data with this preset (preset_previews.hpp).
        const SDL_FRect pv = st_.r(x, y + 2, pvW, pvH);
        GraphicsPreviews::Picture pic;
        if (previews_) pic = previews_->preset(graphicsTab_, settings_);
        else pic.unavailable = true;
        static const char* const kBadge[] = {"1998", "2D", "3D"};
        const bool needsPack = graphicsTab_ == 2 && pic.unavailable && previews_ && hasData();
        drawPreviewCard(ui_, pv, pic, kBadge[graphicsTab_],
                        needsPack ? tr("Needs the 3D art pack (a model pack)") : nullptr, bodyTs);
        // The look in one line, then its main values.
        static const char* const kLine[] = {
            "Exactly the 1998 game: pixel-exact sprites and tiles, the original sidebar, shroud and effects.",
            "Our best 2D: designed terrain, modern effects, soft shroud and the modern panel. Tab shows the 1998 view.",
            "A 2.5D battlefield: designed 3D terrain with sun shadows, modern effects and the modern panel."};
        const float tx = x + pvW + 12, tw = w - pvW - 12;
        float ty = y + 2;
        {
            Ui::BoldScope bold(ui_);
            const SDL_FRect tr0 = st_.r(tx, ty, tw, 0);
            ui_.text(tr(kNames[graphicsTab_]), tr0.x, tr0.y, theme::kTitle, ui_.textScale + 0.5f);
            ty += ui_.textHeight(ui_.textScale + 0.5f) / st_.s + 4;
        }
        ty += ui_.wrapped(tr(kLine[graphicsTab_]), st_.r(tx, ty, tw, 70), theme::kText, bodyTs) / st_.s + 6;
        std::string details;
        auto add = [&](const char* label, const std::string& value) {
            details += (details.empty() ? "" : "\n") + std::string(tr(label)) + ": " + value;
        };
        if (graphicsTab_ == 0) {
            add("Interface", tr("Original"));
        } else {
            add("Terrain", tr(graphicsTab_ == 2 ? (settings_.terrain3d == Settings::Terrain3D::Designed ? "Designed"
                                                    : settings_.terrain3d == Settings::Terrain3D::Faithful ? "Faithful"
                                                    : "Original tiles")
                                                 : (settings_.terrain2d == Settings::Terrain2D::Designed ? "Designed"
                                                    : settings_.terrain2d == Settings::Terrain2D::Faithful ? "Faithful"
                                                    : settings_.terrain2d == Settings::Terrain2D::Original ? "Original tiles" : "Upscaled")));
            if (graphicsTab_ != 2)
                add("Art", settings_.artPack != "original" ? settings_.artPack
                           : artPackInstalled()            ? std::string(tr("Original"))
                                                           : std::string(tr("Original sprites (no art pack)")));
            else if (can3d) add("Art", render::ArtPack::threeDPack(settings_.modelPack));  // 3D: the objects' art
            if (graphicsTab_ == 2) {
                static const char* const kModels[] = {"New (art pack)", "PS1"};
                add("3D models", tr(kModels[std::clamp(static_cast<int>(settings_.models3d), 0, 1)]));
            }
        }
        ty += ui_.wrapped(details, st_.r(tx, ty, tw, 60), theme::kDim, bodyTs) / st_.s + 6;
        if (graphicsTab_ == 1 && !artPackInstalled()) {
            // Enhanced without the redesign pack: never silently "original art" - offer it.
            const auto ds = ArtPackDownloader::instance().status();
            const bool busy = ds.state == ArtPackDownloader::State::Downloading || ds.state == ArtPackDownloader::State::Verifying ||
                              ds.state == ArtPackDownloader::State::Installing;
            if (ui_.button(st_.r(tx, ty, std::min(tw, 170.f), 20), busy ? artPackRowLabel() : std::string(tr("Get the art pack")),
                           ButtonStyle::Primary))
                openArtPackFrom("graphics");
        }
        if (graphicsTab_ == 2 && !can3d) {
            const SDL_FRect a = st_.r(tx, y + pvH + 2, tw, 0);
            ui_.wrapped(tr(Settings::kNeeds3dPackNote), SDL_FRect{a.x, a.y - 2 * ui_.textHeight(ui_.textScale), a.w,
                        2 * ui_.textHeight(ui_.textScale)}, theme::kBad, ui_.textScale);
        } else if (static_cast<int>(active) == graphicsTab_) {
            const SDL_FRect a = st_.r(tx, y + pvH + 2, 0, 0);
            ui_.text(tr("Active"), a.x, a.y - ui_.textHeight(ui_.textScale), Colour{150, 220, 120, 255}, ui_.textScale);
        }
        y += pvH + 10;
        if (promptH > 0) y += artPackPrompt(x, y, w);
        y += [&] {
            // Display: not part of a preset, shown under every tab.
            const SDL_FRect r = layout.rect(y, layout.headerH);
            const float ts = ui_.textScale;
            const float hy = r.y + r.h - ui_.textHeight(ts) - 3;
            ui_.text(tr("Display"), r.x + 4, hy, theme::kAccent, ts);
            ui_.text(tr("Not part of a preset"), r.x + r.w - 4, hy, theme::kDim, ts, Align::Right);
            ui_.fill(SDL_FRect{r.x, r.y + r.h - 1, r.w, 1}, Colour{150, 102, 52, 140});
            return layout.headerH + layout.gap;
        }();
        y += displayOptionList(ui_, settings_, layout, y, window_);
    }
    ui_.endScroll();
    if (custom && previews_) {
        if (!forcedProbe_.empty()) probe.option = static_cast<PictureOption>(std::atoi(forcedProbe_.c_str()));  // screenshots
        customPreviewColumn(ui_, *previews_, settings_, probe, st_.r(px + 380, 96, pw - 392, 272), bodyTs);
    }
    // After the presets: the common Custom comparisons in the background (once per setup).
    if (previews_) prefetchPreviews(*previews_, settings_);
    if (ui_.button(st_.r(px, 377, 110, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Options);
    return res;
}

void Menu::requestFolder() {
    {
        std::lock_guard lock(folderMutex_);
        if (folderPending_) return;
        folderPending_ = true;
    }
    auto cb = [](void* user, const char* const* list, int) {
        Menu* self = static_cast<Menu*>(user);
        std::lock_guard lock(self->folderMutex_);
        self->folderPending_ = false;
        if (list && list[0]) {
            self->pickedFolder_ = list[0];
            self->folderPicked_ = true;
        } else if (!list) {
            SDL_Log("Folder dialog failed: %s", SDL_GetError());
        }
    };
    SDL_ShowOpenFolderDialog(cb, this, window_, nullptr, false);
}

void Menu::requestInstaller() {
    {
        std::lock_guard lock(folderMutex_);
        if (folderPending_) return;
        folderPending_ = true;
    }
    auto cb = [](void* user, const char* const* list, int) {
        Menu* self = static_cast<Menu*>(user);
        std::lock_guard lock(self->folderMutex_);
        self->folderPending_ = false;
        if (list && list[0]) {
            self->pickedInstaller_ = list[0];
            self->installerPicked_ = true;
        } else if (!list) {
            SDL_Log("File dialog failed: %s", SDL_GetError());
        }
    };
    static const SDL_DialogFileFilter kFilters[] = {{"Gruntmods installer (*.exe)", "exe"}};
    // Android maps filters to MIME types; an .exe's type varies by provider, so no filter there.
    SDL_ShowOpenFileDialog(cb, this, window_, kAndroid ? nullptr : kFilters, kAndroid ? 0 : 1, nullptr, false);
}

void Menu::requestTranslation() {
    {
        std::lock_guard lock(folderMutex_);
        if (folderPending_) return;
        folderPending_ = true;
    }
    auto cb = [](void* user, const char* const* list, int) {
        Menu* self = static_cast<Menu*>(user);
        std::lock_guard lock(self->folderMutex_);
        self->folderPending_ = false;
        if (list && list[0]) {
            self->pickedTranslation_ = list[0];
            self->translationPicked_ = true;
        } else if (!list) {
            SDL_Log("Folder dialog failed: %s", SDL_GetError());
        }
    };
    SDL_ShowOpenFolderDialog(cb, this, window_, nullptr, false);
}

void Menu::requestPs1Image() {
    {
        std::lock_guard lock(folderMutex_);
        if (folderPending_) return;
        folderPending_ = true;
    }
    auto cb = [](void* user, const char* const* list, int) {
        Menu* self = static_cast<Menu*>(user);
        std::lock_guard lock(self->folderMutex_);
        self->folderPending_ = false;
        if (list && list[0]) {
            self->pickedPs1_ = list[0];
            self->ps1Picked_ = true;
        } else if (!list) {
            SDL_Log("File dialog failed: %s", SDL_GetError());
        }
    };
    static const SDL_DialogFileFilter kFilters[] = {{"PS1 disc image (*.cue, *.bin)", "cue;bin"}};
    SDL_ShowOpenFileDialog(cb, this, window_, kFilters, 1, nullptr, false);
}

// iPad: how to get the data in (Files app / Finder file sharing) and one button per usable item
// found in the app's Documents: a game folder, an installer (unpacked like a chosen .exe), a PS1
// disc image. The scan repeats every 2 s, so files copied while the page is open show up.
void Menu::documentsSection(float& y, float cx, float w, float s, float bw, float bh) {
    const Uint64 now = SDL_GetTicks();
    if (docScanAt_ == 0 || now - docScanAt_ > 2000) {
        docScan_ = scanDocuments();
        docScanAt_ = now;
    }
    static const char* const kIosLines[] = {"OpenD2K plays with the data files of your own copy of Dune 2000.",
                                            "Copy the Gruntmods Edition installer (.exe) or a \"Dune 2000\" game folder",
                                            "into this app's folder: Files app > On My iPad > OpenD2K,",
                                            "or from a Mac: Finder > your iPad > Files > OpenD2K (drag the file onto it).",
                                            "Cutscenes: the Gruntmods cutscene installer (.exe), .VQA files or a PS1 disc (.cue + .bin)."};
    // Android: Android/data/<app> is hidden from most file managers (11+), so the main way in is the
    // system file picker (the installer, e.g. from Downloads); a computer can still copy into the folder.
    static const char* const kAndroidLines[] = {"OpenD2K plays with the data files of your own copy of Dune 2000.",
                                                "Put the Gruntmods Edition installer (.exe) on this device (e.g. Downloads)",
                                                "and choose it below: it is unpacked once.",
                                                "Or copy a \"Dune 2000\" game folder from a computer (USB) into",
                                                "Android/data/com.opend2k.app/files."};
    const auto& lines = kAndroid ? std::vector<const char*>(std::begin(kAndroidLines), std::end(kAndroidLines))
                                 : std::vector<const char*>(std::begin(kIosLines), std::end(kIosLines));
    for (const char* l : lines) {
        std::string line = tr(l);
        if (kIOS && isPhoneDevice())  // the Files app / Finder name the device: "On My iPhone", "your iPhone"
            for (std::size_t at = line.find("iPad"); at != std::string::npos; at = line.find("iPad", at + 6)) line.replace(at, 4, "iPhone");
        ui_.text(ui_.ellipsize(line, w - 40.f, s), cx, y, theme::kText, s, Align::Centre);
        y += ui_.textHeight(s) + 6 * s;
    }
    if constexpr (kAndroid) {
        y += 4 * s;
        if (ui_.button({cx - bw / 2, y, bw, bh}, tr("Choose installer (.exe)..."), ButtonStyle::Primary)) requestInstaller();
        y += bh + 8 * s;
    }
    y += 6 * s;
    {  // What the app has: game data, cutscenes (any source: VQA files, the PS1 disc).
        const std::size_t found = hasData() ? std::size_t(cutsceneCount().first) : 0;
        const std::string gameLine = std::string(tr("Game data:")) + " " + (hasData() ? tr("found") : tr("missing"));
        const std::string movieLine =
            std::string(tr("Cutscenes:")) + " " +
            (!hasData() ? std::string("-")
                        : found ? trf("%d of %d", int(found), int(Movies::gallery().size())) : std::string(tr("none")));
        const float gw = ui_.textWidth(gameLine, s), mw = ui_.textWidth(movieLine, s), gap = 40 * s;
        const float x0 = cx - (gw + gap + mw) / 2;
        ui_.text(gameLine, x0, y, hasData() ? Colour{150, 220, 120, 255} : theme::kBad, s);
        ui_.text(movieLine, x0 + gw + gap, y, found ? Colour{150, 220, 120, 255} : hasData() ? theme::kAccent : theme::kDim, s);
        y += ui_.textHeight(s) + 6 * s;
        if (hasData() && !found) {
            ui_.text(ui_.ellipsize(tr("No cutscenes yet: the Gruntmods installer has none, they are a separate download."), w - 40.f, s),
                     cx, y, theme::kDim, s, Align::Centre);
            y += ui_.textHeight(s) + 6 * s;
            if (ui_.button({cx - bw / 2, y, bw, bh}, tr("Get cutscenes (Gruntmods page)"))) SDL_OpenURL(kCutsceneDownloadPage);
            y += bh + 6 * s;
        }
    }
    y += 6 * s;
    namespace fs = std::filesystem;
    auto name = [](const std::string& p) {
        fs::path f(p);
        if (f.filename() == "data") f = f.parent_path();  // ".../Dune 2000/data" -> "Dune 2000"
        return f.filename().string();
    };
    if (docScan_.movieFiles) {
        ui_.text(trf("%d cutscene files (.VQA) in the OpenD2K folder", int(docScan_.movieFiles)), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 6 * s;
    }
    const bool none = docScan_.dataDirs.empty() && docScan_.installers.empty() && docScan_.ps1Images.empty() && !docScan_.movieFiles;
    if (none) {
        ui_.text(tr("Nothing found in the OpenD2K folder yet (checked every few seconds)."), cx, y, theme::kAccent, s, Align::Centre);
        y += ui_.textHeight(s) + 8 * s;
        return;
    }
    const float wide = std::min(w - 40.f, bw * 2 + 16 * s);
    std::lock_guard lock(folderMutex_);
    for (const std::string& d : docScan_.dataDirs) {
        if (d == dataDir_) continue;
        if (ui_.button({cx - wide / 2, y, wide, bh}, ui_.ellipsize(std::string(tr("Use game folder")) + " \"" + name(d) + "\"", wide - 16, s),
                       ButtonStyle::Primary)) {
            pickedFolder_ = d;
            folderPicked_ = true;
        }
        y += bh + 6 * s;
    }
    for (const std::string& f : docScan_.installers) {
        const std::string verb = isCutsceneInstallerName(f) ? tr("Add cutscenes from") : tr("Unpack");
        if (ui_.button({cx - wide / 2, y, wide, bh}, ui_.ellipsize(verb + " " + name(f), wide - 16, s), ButtonStyle::Primary)) {
            pickedInstaller_ = f;
            installerPicked_ = true;
        }
        y += bh + 6 * s;
    }
    for (const std::string& f : docScan_.ps1Images) {
        if (f == settings_.ps1Image) continue;
        if (ui_.button({cx - wide / 2, y, wide, bh}, ui_.ellipsize(std::string(tr("Use PS1 disc image")) + " " + name(f), wide - 16, s))) {
            pickedPs1_ = f;
            ps1Picked_ = true;
        }
        y += bh + 6 * s;
    }
}

void Menu::startPickedImport() {
    if (!installerPicked_) return;
    installerPicked_ = false;
    dataDirError.clear();
    importInfo_.clear();
    import_ = std::make_unique<InstallerImportTask>(pickedInstaller_, dataDir_, gameDataRoot);
}

void Menu::pollImport(MenuResult& res) {
    if (!import_ || !import_->finished()) return;
    const ImportResult r = import_->result();
    import_.reset();
    importCancelled_ = false;
    cutscenesAt_ = 0;  // count again
    if (!r.ok) {
        dataDirError = trMessage(r.error);
    } else if (r.moviesOnly) {
        dataDirError.clear();
        importInfo_ = trs(trf("Added %d cutscene files to %%s", int(r.files)).c_str(), r.moviesDir);
    } else {
        dataDirError.clear();
        importInfo_.clear();
        SDL_Log("Game data %s: %s", r.reused ? "reused" : "unpacked", r.dataDir.c_str());
        res.kind = MenuResult::DataDirChosen;
        res.dataDir = r.dataDir;
    }
}

std::pair<int, int> Menu::cutsceneCount() {
    const int total = int(Movies::gallery().size());
    if (previewCutscenes_ >= 0) return {std::min(previewCutscenes_, total), total};
    const Uint64 now = SDL_GetTicks();
    if (cutscenesAt_ == 0 || now - cutscenesAt_ > 2000) {
        cutscenesFound_ = 0;
        if (hasData() && movies_)
            for (const auto& e : Movies::gallery()) cutscenesFound_ += movies_->available(e.name, e.house);
        cutscenesAt_ = now;
    }
    return {cutscenesFound_, total};
}

// Data-folder page: choose the game folder, or build one from the Gruntmods installer (with a
// progress view while it unpacks on a worker thread).
MenuResult Menu::pageDataDir() {
    MenuResult res;
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    backdrop(render::Texture{}, 0);
    // No game data means no FONT.FNT: the Ui falls back to SDL's built-in 8x8 font.
    float s = std::max(1.0f, std::floor(std::min(w / 640.0f, h / 400.0f)));
    if (phoneUi()) s = std::max(s, std::round(13 * dp() / 12.5f * 2) / 2);  // ~13 sp text on a phone
    ui_.textScale = s;
    const float cx = w / 2.0f;
    const bool phone = phoneUi();
    float y = h * (hasData() || phone ? 0.06f : 0.16f);
    const float titleS = phone ? 2.5f * s : 4 * s;
    ui_.text("DUNE 2000", cx, y, theme::kTitle, titleS, Align::Centre);
    y += ui_.textHeight(titleS) + (phone ? 6 : 18) * s;
    float bw = 280 * s, bh = 18 * s + 8;
    if (phoneUi()) {  // >= 48 dp buttons
        bh = std::max(bh, std::round(48 * dp()));
        bw = std::min(float(w) * 0.6f, std::max(bw, std::round(320 * dp())));
    }

    if (import_ || previewImport_) {
        const bool preview = !import_;
        const std::string name = preview ? "Dune 2000 Gruntmods Edition 1.6.7.exe"
                                         : std::filesystem::path(import_->installer()).filename().string();
        const float frac = preview ? 0.42f : import_->fraction();
        const std::size_t done = preview ? 6123 : import_->filesDone(), total = preview ? 14003 : import_->filesTotal();
        const std::string cur = preview ? "Dune 2000/data/GAMESFX/S06V1-07.aud" : import_->currentFile();
        ui_.text(trs("Unpacking %s", name), cx, y, theme::kText, s, Align::Centre);
        y += ui_.textHeight(s) + 14 * s;
        const SDL_FRect bar{cx - 220 * s, y, 440 * s, 14 * s};
        ui_.fill(bar, Colour{0, 0, 0, 160});
        ui_.fill({bar.x, bar.y, bar.w * std::clamp(frac, 0.f, 1.f), bar.h}, theme::kAccent);
        ui_.outline(bar, theme::kEdge);
        y += bar.h + 10 * s;
        ui_.text(trf("%d%%   %d / %d files", int(frac * 100 + 0.5f), int(done), int(total)),
                 cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 6 * s;
        ui_.text(ui_.ellipsize(cur, w - 80.f, s), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 20 * s;
        const bool cancelling = import_ && importCancelled_;
        if (ui_.button({cx - bw / 2, y, bw, bh}, cancelling ? tr("Cancelling...") : tr("Cancel"), ButtonStyle::Normal, !cancelling) ||
            ui_.takeBack()) {
            if (import_) {
                import_->cancel();
                importCancelled_ = true;
            }
        }
        pollImport(res);
        return res;
    }

    if constexpr (kTouchPlatform) {
        documentsSection(y, float(cx), float(w), s, bw, bh);
    } else {
        const char* lines[] = {"This native rebuild plays with the data files of your own copy of Dune 2000.",
                               "Choose the game's \"Dune 2000\" folder (or its \"data\" folder),",
                               "or the Gruntmods Edition installer (.exe): it is unpacked once."};
        for (const char* l : lines) {
            ui_.text(ui_.ellipsize(tr(l), w - 40.f, s), cx, y, theme::kText, s, Align::Centre);
            y += ui_.textHeight(s) + 8 * s;
        }
    }
    y += 10 * s;
    if (hasData() && !kTouchPlatform) {  // iPad: a sandbox path; documentsSection says "Game data: found"
        ui_.text(tr("Current data folder:"), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 6 * s;
        ui_.text(ui_.ellipsize(dataDir_, w - 80.f, s), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 4 * s;
    } else if (!triedDataDirs.empty() && !kTouchPlatform) {  // iPad: the sandbox paths mean nothing to the player
        ui_.text(tr("Looked in:"), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 6 * s;
        for (std::size_t i = 0; i < triedDataDirs.size() && i < 5; ++i) {
            ui_.text(ui_.ellipsize(triedDataDirs[i], w - 80.f, s), cx, y, theme::kDim, s, Align::Centre);
            y += ui_.textHeight(s) + 4 * s;
        }
    }
    {
        std::lock_guard lock(folderMutex_);
        if (folderPicked_) {
            folderPicked_ = false;
            const std::string d = normalizeDataDir(pickedFolder_);
            if (d.empty()) dataDirError = trs("No Dune 2000 data found in %s", pickedFolder_);
            else { dataDirError.clear(); res.kind = MenuResult::DataDirChosen; res.dataDir = d; }
        }
        startPickedImport();
        if (translationPicked_) {
            translationPicked_ = false;
            if (formats::lang::scanTranslation(pickedTranslation_).any()) {
                settings_.translationDir = pickedTranslation_;
                dataDirError.clear();
                importInfo_ = tr("Translation folder set");
            } else {
                dataDirError = trs("No translation text, voices or movies found in %s", pickedTranslation_);
            }
        }
        if (ps1Picked_) {
            ps1Picked_ = false;
            const std::string why = Movies::checkPs1Image(pickedPs1_);
            if (why.empty()) {
                settings_.ps1Image = pickedPs1_;
                dataDirError.clear();
                importInfo_ = ps1DiscIsRussian(pickedPs1_) ? tr("Russian PS1 disc image set: voices and cutscenes for \"Russian (PS1)\"")
                                                           : tr("PS1 disc image set: its movies fill in for missing PC cutscenes");
            } else {
                dataDirError = trMessage(why);
            }
        }
    }
    y += 14 * s;
    if (!dataDirError.empty()) {
        ui_.text(ui_.ellipsize(dataDirError, w - 80.f, s), cx, y, theme::kBad, s, Align::Centre);
        y += ui_.textHeight(s) + 10 * s;
    } else if (!importInfo_.empty()) {
        ui_.text(ui_.ellipsize(importInfo_, w - 80.f, s), cx, y, theme::kAccent, s, Align::Centre);
        y += ui_.textHeight(s) + 10 * s;
    }
    if constexpr (!kTouchPlatform) {  // iPad: no file dialogs (documentsSection lists the files)
        if (ui_.button({cx - bw - 8 * s, y + 10 * s, bw, bh}, tr("Choose folder..."), ButtonStyle::Primary)) requestFolder();
        if (ui_.button({cx + 8 * s, y + 10 * s, bw, bh}, tr("Choose installer (.exe)..."), ButtonStyle::Primary))
            requestInstaller();
        y += bh + 20 * s;
    }
    if (hasData()) {
        // Cutscenes: PC VQA files, else the PS1 disc's STR movies (platform/movies.hpp).
        const auto [found, total] = cutsceneCount();
        const std::string img = movies_ ? movies_->ps1Image() : std::string();
        const std::string info = trs(trf("Cutscenes found: %d of %d   PS1 disc image: %%s", found, total).c_str(),
                                     img.empty() ? std::string(tr("none")) : img);
        if (!kTouchPlatform || !img.empty()) {  // iPad: the count is in documentsSection's status line
            ui_.text(ui_.ellipsize(info, w - 80.f, s), cx, y, theme::kDim, s, Align::Centre);
            y += ui_.textHeight(s) + 8 * s;
        }
        if (!kTouchPlatform && found < total) {
            // Cutscenes missing: the Gruntmods page as a third button of the PS1 row (the cutscene installer
            // itself goes through "Choose installer (.exe)..." above). Touch: documentsSection has the button.
            const float b3 = (2 * bw - 16 * s) / 3, x3 = cx - bw - 8 * s;
            if (ui_.button({x3, y, b3, bh}, ui_.ellipsize(tr("Get cutscenes (Gruntmods page)"), b3 - 12 * s, s), ButtonStyle::Primary))
                SDL_OpenURL(kCutsceneDownloadPage);
            if (ui_.button({x3 + b3 + 16 * s, y, b3, bh}, ui_.ellipsize(tr("PS1 disc image..."), b3 - 12 * s, s))) requestPs1Image();
            if (ui_.button({x3 + 2 * (b3 + 16 * s), y, b3, bh}, ui_.ellipsize(tr("No PS1 disc image"), b3 - 12 * s, s), ButtonStyle::Normal,
                           !settings_.ps1Image.empty()))
                settings_.ps1Image.clear();
        } else {
            if (!kTouchPlatform && ui_.button({cx - bw - 8 * s, y, bw, bh}, tr("PS1 disc image..."))) requestPs1Image();
            if (ui_.button({kTouchPlatform ? cx - bw / 2 : cx + 8 * s, y, bw, bh}, tr("No PS1 disc image"), ButtonStyle::Normal,
                           !settings_.ps1Image.empty()))
                settings_.ps1Image.clear();
        }
        y += bh + 10 * s;
        // Languages (platform/language.hpp): a Russian PC translation folder (text, fonts, voices,
        // movies; found by itself at <Dune 2000>/../ru-pc) — the Russian PS1 disc is the image above.
        const formats::lang::Translation& t = languageSources().translation;
        std::string tinfo;
        if (t.root.empty()) tinfo = tr("Translation folder: none");
        else {
            tinfo = std::string(tr("Translation folder:")) + " " + t.root +
                    (settings_.translationDir.empty() ? std::string(" ") + tr("(found)") : std::string()) + "  - ";
            std::string parts;
            for (const auto& [have, what] : {std::pair{!t.textDir.empty(), "text"}, std::pair{!t.voicesDir.empty(), "voices"},
                                             std::pair{!t.moviesDir.empty(), "movies"}})
                if (have) parts += (parts.empty() ? "" : ", ") + std::string(tr(what));
            tinfo += t.any() ? parts : std::string(tr("nothing usable"));
        }
        ui_.text(ui_.ellipsize(tinfo, w - 80.f, s), cx, y, theme::kDim, s, Align::Centre);
        y += ui_.textHeight(s) + 8 * s;
        if constexpr (!kTouchPlatform) {
            if (ui_.button({cx - bw - 8 * s, y, bw, bh}, tr("Translation folder..."))) requestTranslation();
            if (ui_.button({cx + 8 * s, y, bw, bh}, tr("No translation folder"), ButtonStyle::Normal, !settings_.translationDir.empty()))
                settings_.translationDir.clear();
            y += bh + 10 * s;
        }
    }
    const bool back = ui_.button({cx - bw / 2, y, bw, bh}, hasData() ? tr("Back") : tr("Quit")) || ui_.takeBack();
    if (back) {
        if (hasData()) open(MenuPage::Options);
        else res.kind = MenuResult::Quit;
    }
    return res;
}

}  // namespace d2k::platform
