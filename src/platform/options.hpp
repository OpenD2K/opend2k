#pragma once
// Command-line options of the d2k executable (see parseArgs for the list).

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace d2k::platform {

struct Options {
    std::string dataDir;  // --data DIR (empty = discover, see data_dir.hpp)
    // --install-from INSTALLER.exe: build the data folder from the Gruntmods installer first
    // (platform/installer_import.hpp; reuses a finished copy), then continue as usual with it;
    // --install-only exits after that (headless). --game-data-root DIR: unpack under DIR
    // instead of <pref path>/GameData (tests).
    std::string installFrom;
    bool installOnly = false;
    std::string gameDataRoot;
    // The art pack download (platform/artpack_download.hpp): --artpack-manifest URL overrides the
    // manifest URL (also D2K_ARTPACK_MANIFEST_URL); --artpack-dir DIR installs into / searches DIR
    // instead of <pref path>/artpacks (tests); --artpack-check / --artpack-install run headless
    // (fetch the manifest / download + verify + install, then exit). --http-get URL FILE: one
    // download through the platform HTTP backend (tests redirects), then exit.
    std::string artpackManifest, artpackDir;
    bool artpackCheck = false, artpackInstall = false;
    // App update check (platform/app_update.hpp): --update-url URL overrides the release info URL
    // (also D2K_UPDATE_URL; plain http only to localhost); --update-check checks once, prints the
    // result and exits.
    std::string updateUrl;
    bool updateCheck = false;
    std::string httpGetUrl, httpGetOut;
    std::string map;      // --map NAME: go straight into that mission (empty = main menu)
    int playAs = -1;      // --play-as SIDE: play the --map mission as that side (GameStart::playAsSide)
    std::string tileset;  // --tileset NAME.R16 (empty = take it from the map's mission file)
    std::string screenshot;
    // --record DIR (with --frames N, --record-fps F): deterministic recording - every frame
    // advances the game by exactly 1/F s (game clock, not real time) and is saved as
    // DIR/frame_00000.bmp ...; a hidden window like the other one-frame runs.
    std::string recordDir;
    int recordFps = 30;
    bool fixedStep = false;  // --record-fps given: also --screenshot runs step 1/F s per frame (deterministic)
    std::optional<std::pair<int, int>> at;  // --at TX TY: tile to centre the view on
    float zoom = 1.0f;
    bool smoke = false;    // render one frame and exit
    int frames = 1;        // --frames N: one-frame modes run N real-time frames (screenshot of the last)
                           // and log the frame times (performance checks)
    bool simtest = false;  // headless self-test (sim_test.hpp)
    int ticks = 0;         // --ticks N: simulate N ticks before showing/screenshotting
    bool battle = false;   // --battle: every armed ground unit attacks the nearest enemy (demo)
    bool vfxDemo = false;  // --vfx-demo: a weapon effects gallery around --at (render checks; changes the world)
    bool selectAll = false;  // --select: select all player units and buildings (HUD demo)
    bool mute = false;     // --mute: no audio
    // --camera-tilt DEG / --camera-yaw DEG: initial 3D camera (reproducible screenshots).
    std::optional<float> cameraTilt, cameraYaw;
    // --models3d new|ps1 (the old "sprites" reads as new) and --model-pack NAME (Graphics: 3D; docs/3d-models.md).
    std::string models3d, modelPack;
    std::string terrain2d;  // --terrain2d original|upscaled|designed|faithful (Graphics: Enhanced; docs/enhanced-graphics.md)
    std::string terrain3d;  // --terrain3d tiles|designed|faithful (Graphics: 3D; docs/3d-mode.md "Terrain v2" / "Terrain v3")
    std::string shadows3d;  // --shadows3d on|off (Graphics: 3D; docs/3d-mode.md "Sun shadows")
    std::string timeOfDay;  // --time-of-day day|dawn|dusk|night|cycle (Enhanced / 3D; docs/enhanced-graphics.md "Lighting")
    std::string lighting;   // --lighting classic|modern (Enhanced / 3D; docs/enhanced-graphics.md "Lighting")
    // Quality / display for this run (docs/enhanced-graphics.md "Quality"): --quality low|medium|high,
    // --hdr on|off (HDR rendering), --hdr-output on|off (EDR output; windowed runs only),
    // --msaa 1|2|4, --aniso 1|4|16, --vsync on|off|adaptive, --fps-limit 0|30|60|120.
    std::string quality, hdr, hdrOutput, vsync;
    int msaa = 0, aniso = 0, fpsLimit = -1;
    std::string graphics;  // --graphics original|enhanced|3d (empty = the saved setting);
                           // --hq = enhanced, --classic-ui = original
    std::string artPack;   // --art-pack NAME (original|upscaled|<pack folder>; empty = the saved setting)
    std::string effects;   // --effects classic|modern (empty = the saved setting)
    std::string panelTransparency;  // --panel-transparency off|light|medium (Enhanced panel; empty = saved)
    std::string shroud;    // --shroud classic|soft (Enhanced shroud edge; empty = the saved setting)
    bool reveal = false;   // --reveal: draw without shroud
    bool classicPeek = false;  // --classic-peek: the classic view key held (Enhanced / 3D screenshots)
    bool perfOverlay = false;  // --perf-overlay: the frame-time overlay (Settings::perfOverlay) this run
    std::optional<std::pair<int, int>> killBuilding;  // --kill-building TX TY: destroy it before --ticks (debug)
    bool noMusic = false;  // --no-music
    bool skirmish = false; // --skirmish: start --map as a skirmish (lobby values from the settings)
    int skirmishAi = 0;    // --skirmish-ai N: computer players of that skirmish (this run; 0 = the setting)
    std::string menuScreenshot;  // --menu-screenshot FILE.bmp: render one menu frame and exit
    // --render-previews OUTDIR: render every Graphics / welcome page preview the menus can show into
    // OUTDIR (PNG + index.json; the shipped assets/previews, platform/preset_previews.hpp) and exit.
    // --preview-pack DIR: the redesign art pack to render them with (default: the installed opend2k-redesign).
    std::string renderPreviews, previewPack;
    // --cursor-at X Y: virtual mouse position (window pixels) for one-frame runs, so the
    // screenshot shows the cursor the game would pick there; --cursor-mode N: click mode
    // (gMouseMode: 2 repair, 3 sell, 4 air strike target, 5 death hand target).
    std::optional<std::pair<float, float>> cursorAt;
    int cursorMode = 0;
    std::string sidebarPage;     // --sidebar-page starport|upgrade|select-upgradable: open that sidebar page
                                 //   / select an upgradable building (screenshots)
    std::string menuPage;        // --menu-page NAME (main, campaign, missions[-a|-h|-o], briefing,
                                 //   custom, skirmish, options, datadir, datadir-progress, load, pause, pause-options,
                                 //   pause-save, pause-load)
    // Save games: --load N starts straight into save slot N (then --ticks M more ticks);
    // --save N writes the game started with --map (after --ticks) to slot N; --saves-dir DIR
    // uses DIR instead of <pref path>/saves (tests and screenshots).
    int loadSlot = -1, saveSlot = -1;
    std::string savesDir;
    // --window WxH: initial window size in points (default 1280x800); --resolution R: the
    // Original-mode game resolution for this run (auto, native or WxH; see Settings::resolution).
    int windowW = 0, windowH = 0;
    std::string resolution;
    std::string uiScale;  // --ui-scale auto|100..300: the Enhanced UI scale for this run (Settings::uiScale)
    // --phone-layout / --tablet-layout: Settings::touchLayout Phone / Tablet for this run (the iPhone
    // layout on the Mac for testing); --ui-size tiny|small|normal: Settings::phoneUiSize this run;
    // --safe-insets L,T,R,B: pretend the window has these safe-area insets (points; e.g. an iPhone
    // 12 Pro Max in landscape: 47,0,47,21) for phone-layout screenshots.
    std::string touchLayout, phoneUiSize;
    // --phone-menus D: the front end's phone layouts (Menu::phoneUi) on a desktop, D output pixels per
    // dp (e.g. 1 at --window 844x390, 2.8125 for a 450 dpi Android phone at 2340x1080); with
    // --safe-insets the welcome page keeps out of the notch too.
    float phoneMenus = 0;
    std::optional<std::array<float, 4>> safeInsets;
    // --render-resolution auto|full|half (Settings::renderResolution) for this run. One-frame and
    // headless runs default to half (point resolution), so screenshots have the --window size on
    // any display.
    std::string renderResolution;
    // --movie NAME|FILE.VQA: play that cutscene at startup (a UIL movie name such as
    // "mve_briefing_atreides3", or a file), whatever the cutscene setting; with
    // --menu-screenshot it captures the movie after --frames frames. --movie-house H for "mve_lost".
    std::string movie;
    int movieHouse = 0;
    // --ps1 IMAGE.cue|bin: PS1 disc image for this run (see Settings::ps1Image).
    std::string ps1Image;
    // --language en|fr|de|es|ru-ps1|ru-pc|ru-ai (this run; Settings::language), --text-language /
    // --voices-language / --movies-language CODE (the Advanced overrides), --translation DIR
    // (Settings::translationDir).
    std::string language, textLanguage, voicesLanguage, moviesLanguage, translationDir;
    // --ai-voices DIR: the Russian AI voice set (Settings::aiVoicesDir; default <pref path>/Voices/ru-ai).
    std::string aiVoicesDir;
    // --ru-text fargus|fixes: the Russian text without / with the fixes overlay (Settings::russianTextFixes).
    std::string ruText;
    // --sound-test: log where a set of speech samples (mentat lines, unit voices) come from in the
    // active language, then exit (no window).
    bool soundTest = false;

    // Map used when a headless/one-frame mode is requested without --map (the old default).
    static constexpr const char* kDefaultMap = "2PLAY1.MAP";
};

Options parseArgs(int argc, char** argv);

}  // namespace d2k::platform
