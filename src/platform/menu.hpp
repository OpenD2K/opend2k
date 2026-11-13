#pragma once
// Front-end menus: main menu, campaign (house -> missions -> briefing), custom missions,
// skirmish lobby, options, and the "where is the game data?" screen. Drawn with the original
// menu artwork from data/UI_GFX and FONT.FNT through the Ui toolkit.

#include "platform/campaign_screens.hpp"
#include "platform/catalog.hpp"
#include "platform/community_menu.hpp"
#include "platform/game_data.hpp"
#include "platform/installer_import.hpp"
#include "platform/original_menus.hpp"
#include "platform/saves.hpp"
#include "render/image.hpp"
#include "render/map_preview.hpp"
#include "render/text.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace d2k::platform {

class Ui;
class Movies;
class GraphicsPreviews;
struct Settings;

// Original: the original front-end pages (platform/original_menus.hpp) — Graphics: Original
// shows Main / Skirmish / Options / Load through them; Enhanced keeps the modern pages.
// Community / CommunityCampaign: the community campaigns & maps pages (menu_community.cpp).
// Graphics: Options -> Graphics, the preset tabs Original / Enhanced / 3D / Custom (any graphics
// mode: the page stays modern while Original is picked; Back returns to the Options twin).
// ArtPack: Options -> Art pack, the downloadable OpenD2K art pack (menu_artpack.cpp).
// Updates: Options -> Updates, the app update check + the art pack's state (menu_update.cpp).
// Welcome: the first-run page (menu_artpack.cpp): what OpenD2K is, the look (Original / Enhanced /
// 3D) and the art pack offer; shown once per install after the game data is set up.
enum class MenuPage { Main, Campaign, Missions, Briefing, Custom, Skirmish, Options, DataDir, Load, Original, Community,
                      CommunityCampaign, Movies, Graphics, ArtPack, Updates, Welcome };

struct MenuResult {
    enum Kind { None, Start, Quit, DataDirChosen, Load } kind = None;
    GameStart start;      // Start
    int slot = -1;        // Load: save slot (platform/saves.hpp)
    std::string dataDir;  // DataDirChosen
};

// MenuPage::Campaign is the original campaign flow (platform/campaign_screens.hpp: house
// selection, territory map, briefing, score); Missions/Briefing remain for the custom missions.

// GameStart::campaignHouse value for games started from the custom-missions list.
constexpr int kCustomMissionsHouse = 3;

class Menu {
public:
    Menu(SDL_Window* window, SDL_Renderer* r, Ui& ui, Settings& settings);

    // Loads the artwork and the mission/map lists from a valid data folder ("" = no data yet).
    void setData(const std::string& dataDir);
    // Cutscenes (platform/movies.hpp), shared with the campaign and original page stacks.
    void setMovies(Movies* m);
    bool hasData() const { return !dataDir_.empty(); }

    void open(MenuPage p);
    MenuPage page() const { return page_; }
    // Selects a page by its --menu-page name; false if unknown.
    bool openByName(const std::string& name);
    // Back from a game. Original campaign maps: an ended battle (`score` set) opens the score
    // page and then the territory map; a quit one the territory map of the same mission.
    // Other games: skirmish lobby / custom missions / main menu.
    void returnFromGame(const GameStart& start, bool won, const game::ScoreInput* score = nullptr);
    // A campaign-map save was loaded: its territory map (house, MissionNumber, difficulty).
    void openCampaignSave(const game::SaveMeta& m);
    // The campaign's territory map is on screen (no dialog): `--save N` writes a campaign-map save.
    bool campaignMapShown() const { return page_ == MenuPage::Campaign && campaign_->onGlobe(); }
    bool saveCampaignMap(int slot, std::string& error) { return campaign_->saveToSlot(slot, std::string(), error); }
    // Front-end music for the current page ("options", "score", "" = silence).
    std::string musicTrack() const;
    int cursorId() const;  // MOUSE.R16 cursor for the current page
    std::vector<MenuSound> takeSounds();
    void setSpeechBusy(bool b);
    void setPersistent(bool p);  // false: screenshot runs (no high-score file)
    void setPreviews(GraphicsPreviews* p) { previews_ = p; }  // Options -> Graphics previews
    // Draws one frame of the current page and handles its input.
    MenuResult frame();

    // Problems to show on the data-folder screen.
    std::vector<std::string> triedDataDirs;
    std::string dataDirError;
    std::string gameDataRoot;  // --game-data-root (empty = <pref path>/GameData)
    std::string loadError;  // shown on the Load page (a save that could not be loaded)
    // Set when a page changed settings that should be written at once (the welcome page's
    // choice); the App saves and clears it.
    bool wantSave = false;

private:
    struct Stage {
        float x = 0, y = 0, s = 1;
        SDL_FRect r(float x0, float y0, float w, float h) const { return {x + x0 * s, y + y0 * s, w * s, h * s}; }
    };
    void backdrop(const render::Texture& t, float dim = 0);
    // Phones (Android: short side < 600 dp; iPhone / an iOS window under 600 pt): the 640x400 stage puts ~1 dp in a
    // stage unit there, too small to touch and read; text then scales by the density and the main
    // pages (main, skirmish, options, data folder) use their own layouts with >= 48 dp targets.
    bool phoneUi() const;
    float dp() const;  // output pixels per dp (Android: the screen density)
    SDL_FRect safeArea() const;  // the window's safe area (out of the notch / home bar) in output pixels
public:
    // Screenshot / test runs on a desktop (--phone-menus D): phoneUi() on, D output pixels per dp.
    static void forcePhoneUi(float dpPixels, const std::array<float, 4>& safeInsetsPt);

private:
    MenuResult pageMain();
    MenuResult pageCampaign();  // the original flow (CampaignScreens)
    MenuResult pageMissions(bool custom);
    MenuResult pageBriefing();
    MenuResult pageSkirmish();
    MenuResult pageOptions();
    MenuResult pageGraphics();  // Options -> Graphics (preset tabs + Custom)
    MenuResult pageDataDir();
    MenuResult pageLoad();
    MenuResult pageOriginal();
    MenuResult pageCommunity();          // tabs: Campaigns / Maps & missions
    MenuResult pageCommunityCampaign();  // one campaign's missions with progress
    MenuResult pageMovies();             // rewatch the cutscenes (Enhanced main menu)
    // Art pack download (menu_artpack.cpp; platform/artpack_download.hpp): the page, the Options
    // row's label, the "download the 3D art pack?" prompt of the Graphics page (returns its
    // height in page units, 0 when not shown), and the settings switch once a download installed.
    MenuResult pageArtPack();
    std::string artPackRowLabel() const;
    float artPackPrompt(float x, float y, float w);
    bool artPackPromptWanted() const;
    static constexpr float kArtPackPromptH = 46;
    bool openArtPackPreview(const std::string& name);  // --menu-page artpack[-update|-downloading|-error|-done|-prompt]
    void applyInstalledArtPack();
    bool artPackPromptLater_ = false;  // "Later" pressed (this run)
    // First-run welcome page (menu_artpack.cpp; Settings::firstRunArtPackAsked).
    MenuResult pageWelcome();
    bool welcomeDue() const;
    void finishWelcome();
    // --menu-page welcome[-downloading|-installed|-failed|-cutscenes-*][-step2] (-step2: the phone Downloads step)
    bool openWelcomePreview(const std::string& name);
    bool welcomeWants3d_ = false;  // 3D picked on the welcome page: the 3D preset once the pack is installed
    // The welcome page's "Cutscenes (optional)" box (stage units): status, Get cutscenes (the
    // Gruntmods page), Add cutscene installer... (the data-folder page's import), the progress.
    void welcomeCutscenes(MenuResult& res, float x, float y, float w, float h);
    int previewCutscenes_ = -1;  // --menu-page welcome-cutscenes-*: the found count to show (-1 = real)
    // Phones (phoneUi): the welcome page in two steps, 0 "Choose your look" (three large cards) and 1
    // "Downloads" (the art pack and the cutscenes boxes with their full text, 48 dp buttons).
    MenuResult pageWelcomePhone(bool installed, bool can3d);
    int welcomeStep_ = 0;
    float welcomeScroll_ = 0;
    // The cutscenes box's state shared by both layouts: the iOS installer found in Documents, the
    // box's text and the small note under it.
    struct CutsceneBoxText {
        std::string iosInstaller;
        const char* text = nullptr;
        std::string note;
        render::Colour noteColour{176, 156, 124, 255};
    };
    CutsceneBoxText cutsceneBoxText(bool all);
    // The phone layout's cutscenes box inside `r` (pixels); draw = false only measures. Returns its height.
    float welcomeCutscenesPhone(MenuResult& res, const SDL_FRect& r, bool draw);
    // A redesign art pack is installed (Enhanced tab / Custom "Art" row: else offer it).
    bool artPackInstalled() const;
    void openArtPackFrom(const char* back);  // Options -> Art pack, Back returns to `back`
    std::string artPackBack_;          // page to return to: "graphics", "updates" or "" (Options)
    // App update check (menu_update.cpp; platform/app_update.hpp): the page and the Options row.
    MenuResult pageUpdates();
    std::string updatesRowLabel() const;
    bool openUpdatesPreview(const std::string& name);  // --menu-page updates[-available|-checking|-failed|-disabled|-live]
    void loadCommunityProgress();
    void saveCommunityProgress();
    void openOriginal(const std::vector<std::string>& stack);
    bool takeFolderResult(MenuResult& res);
    void title(const std::string& text, float x, float y);
    const MissionInfo* selectedMission() const;
    GameStart startFor(const MissionInfo& m, int house, int index) const;
    // "Play as" choice of the briefing page (not original; docs/play-as.md): the sides of the
    // mission shown (`playAsMap_`), the picked entry (0 = the mission's own side).
    void refreshPlayAs(const MissionInfo& m);
    std::vector<PlayAsOption> playAs_;
    std::string playAsMap_;
    int playAsSel_ = 0;
    void requestFolder();
    void requestInstaller();  // SDL file dialog for the Gruntmods installer (.exe)
    void requestPs1Image();   // SDL file dialog for a PS1 disc image (.cue / .bin)
    // Installer import shared by the data-folder and welcome pages: starts the import of a picked
    // installer (call with folderMutex_ held) / takes the finished import's result.
    void startPickedImport();
    void pollImport(MenuResult& res);
    // Cutscenes playable now / offered (Movies::gallery()), rechecked every two seconds.
    std::pair<int, int> cutsceneCount();
    Uint64 cutscenesAt_ = 0;
    int cutscenesFound_ = 0;
    void requestTranslation();  // folder dialog for a Russian translation folder (platform/language.hpp)
    void refreshPreview();
    // iPad: the data-folder page lists what the player copied into the app's Documents
    // (platform/data_dir.hpp scanDocuments, rescanned every few seconds) instead of file dialogs.
    void documentsSection(float& y, float cx, float w, float s, float bw, float bh);
    DocumentsScan docScan_;
    Uint64 docScanAt_ = 0;

    SDL_Window* window_;
    SDL_Renderer* r_;
    Ui& ui_;
    Settings& settings_;
    std::string dataDir_;
    Catalog catalog_;
    render::Texture title_, multi_, back_, acct_[3], logo_[3];
    render::MapPreview preview_;
    std::unique_ptr<CampaignScreens> campaign_;
    std::unique_ptr<OriginalMenus> original_;
    Movies* movies_ = nullptr;
    int movieSel_ = 0;
    float movieScroll_ = 0;
    float optionsScroll_ = 0;  // Options page content (stage pixels * scale)
    // Graphics page: the tab shown (Settings::GraphicsPreset as int; Original..Custom), its scroll,
    // and the previews rendered from the data (GraphicsPreviews).
    int graphicsTab_ = 1;
    float graphicsScroll_ = 0;
    GraphicsPreviews* previews_ = nullptr;  // the App's (platform/preset_previews.hpp)
    std::string forcedProbe_;               // --menu-page graphics-custom-N: the row compared
    int previewFor_ = -1;

    MenuPage page_ = MenuPage::Main;
    Stage st_;
    float ts_ = 1;  // text scale
    int house_ = 0;
    bool briefingFromCustom_ = false;
    bool briefingFromCommunity_ = false;  // briefing of a community campaign mission (customSel_)
    int communityTab_ = 0;                // 0 campaigns, 1 maps & missions
    int communitySel_ = 0;                // campaign
    int communityRowSel_ = 0;             // row of the campaign page
    float communityScroll_ = 0;
    formats::CommunityProgress progress_;
    bool persistent_ = true;
    int missionSel_[3] = {0, 0, 0}, customSel_ = 0, mapSel_ = 0;
    float listScroll_ = 0, briefScroll_ = 0;
    int unitsChoice_ = 3;
    std::vector<SaveSlot> slots_;  // Load page (read when the page opens)
    bool loadFromGlobe_ = false;   // Load page opened from the territory map: Back returns there
    int slotSel_ = 0;
    float slotScroll_ = 0;  // index into the starting-units choices

    std::mutex folderMutex_;
    std::string pickedFolder_;
    bool folderPending_ = false, folderPicked_ = false;
    // Building the data folder from the Gruntmods installer (data-folder page).
    std::string pickedInstaller_;
    bool installerPicked_ = false;
    std::string pickedPs1_;
    bool ps1Picked_ = false;
    std::string pickedTranslation_;
    bool translationPicked_ = false;
    std::unique_ptr<InstallerImportTask> import_;
    bool importCancelled_ = false;
    bool previewImport_ = false;  // --menu-page datadir-progress (screenshot of the progress view)
    std::string importInfo_;      // e.g. "Added 43 cutscene files"
};

}  // namespace d2k::platform
