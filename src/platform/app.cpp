#include "platform/app.hpp"
#include "platform/preset_previews.hpp"

#include "platform/app_update.hpp"
#include "platform/artpack_download.hpp"
#include "render/art_pack.hpp"

#include "render/ui_text.hpp"

#include "formats/io.hpp"
#include "platform/audio.hpp"
#include "platform/data_dir.hpp"
#include "platform/installer_import.hpp"
#include "platform/language.hpp"
#include "platform/menu.hpp"
#include "platform/movies.hpp"
#include "platform/game_options.hpp"
#include "platform/pointer.hpp"
#include "platform/saves.hpp"
#include "platform/session.hpp"
#include "platform/sim_test.hpp"
#include "platform/sound_bank.hpp"
#include "platform/touch_controls.hpp"
#include "platform/ui.hpp"
#include "render/cursor.hpp"
#include "render/hdr.hpp"
#include "render/text.hpp"
#include "render3d/gpu.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <filesystem>
#include <utility>
#include <vector>

// The window title (CMake D2K_APP_NAME: "Dune 2000" in development builds, "OpenD2K" in releases).
#ifndef D2K_APP_TITLE
#define D2K_APP_TITLE "Dune 2000"
#endif

namespace d2k::platform {

namespace {

void logMapSummary(const GameData& g) {
    int nBuildings = 0, nUnits = 0;
    for (const auto& o : g.scenario.objects) {
        nBuildings += o.kind == game::ObjectKind::Building;
        nUnits += o.kind == game::ObjectKind::Unit;
    }
    SDL_Log("Map %s: %dx%d, tileset %s, %d buildings, %d units", g.start.map.c_str(), g.scenario.width, g.scenario.height,
            g.tilesetName.c_str(), nBuildings, nUnits);
}

}  // namespace

App::App(Options opt) : opt_(std::move(opt)) {
    settings_ = Settings::load();
    persist_ = !opt_.smoke && opt_.menuScreenshot.empty() && !opt_.simtest && !opt_.soundTest && opt_.renderPreviews.empty();
    // --render-previews: exactly one art pack (--preview-pack, else the installed opend2k-redesign) is
    // visible, from a temporary folder holding a link to it, so the Art row's values are "original",
    // "upscaled" and that pack - what a player with the released pack sees.
    if (!opt_.renderPreviews.empty()) {
        namespace fs = std::filesystem;
        std::string pack = opt_.previewPack;
        if (pack.empty())
            for (const auto& d : render::ArtPack::searchDirs())
                if (std::error_code ec; fs::exists(fs::path(d) / kArtPackName / "pack.ini", ec)) {
                    pack = (fs::path(d) / kArtPackName).string();
                    break;
                }
        std::error_code ec;
        if (pack.empty() || !fs::exists(fs::path(pack) / "pack.ini", ec)) {
            SDL_Log("--render-previews: no art pack (--preview-pack DIR or an installed %s)", kArtPackName);
        } else {
            const fs::path stage = fs::temp_directory_path(ec) / ("d2k-preview-packs-" + std::to_string(SDL_GetTicksNS()));
            fs::create_directories(stage, ec);
            const fs::path abs = fs::absolute(fs::path(pack), ec).lexically_normal();
            fs::create_directory_symlink(abs, stage / (abs.filename().empty() ? abs.parent_path().filename() : abs.filename()), ec);
            if (ec) SDL_Log("--render-previews: %s", ec.message().c_str());
            render::ArtPack::setExclusiveDir(stage.string());
            previewStage_ = stage.string();
            SDL_Log("--render-previews: art pack %s", abs.string().c_str());
        }
    }
    if (!persist_) {
        // One-frame and headless runs are reproducible: defaults except the data folder.
        Settings defaults;
        defaults.dataDir = settings_.dataDir;
        settings_ = defaults;
    }
    // iPad: lighter defaults once for A10 / A12-class devices (docs/ios.md "Performance").
    if (kTouchPlatform && persist_ && settings_.deviceProfile.empty()) {
        const DeviceTier tier = deviceTier();
        if (tier == DeviceTier::Low) settings_.applyLiteProfile();
        settings_.deviceProfile = deviceTierName(tier);
        SDL_Log("Device %s: %s profile", deviceModel().c_str(), settings_.deviceProfile.c_str());
    }
    if (!opt_.savesDir.empty()) setSavesDirOverride(opt_.savesDir);
    // Art pack download (platform/artpack_download.hpp): URL / folder overrides; one-frame runs
    // never check for updates on their own (only with an explicit --artpack-manifest).
    if (!opt_.artpackDir.empty()) render::ArtPack::setUserDirOverride(opt_.artpackDir);
    ArtPackDownloader::instance().setManifestUrlOverride(opt_.artpackManifest);
    ArtPackDownloader::instance().setSettingsUrl(settings_.artPackManifestUrl);
    ArtPackDownloader::instance().setAutoCheckAllowed(persist_ || !opt_.artpackManifest.empty());
    // The app update check (platform/app_update.hpp): same rules, --update-url for tests.
    AppUpdateChecker::instance().setUrlOverride(opt_.updateUrl);
    AppUpdateChecker::instance().setSettingsUrl(settings_.appUpdateUrl);
    AppUpdateChecker::instance().setAutoCheckAllowed(persist_ || !opt_.updateUrl.empty());
    if (!opt_.graphics.empty())
        settings_.graphics = Settings::parseGraphics(opt_.graphics);
    if (!opt_.models3d.empty()) settings_.models3d = Settings::models3dFrom(opt_.models3d);
    if (!opt_.modelPack.empty()) settings_.modelPack = opt_.modelPack;
    if (!opt_.terrain3d.empty()) settings_.terrain3d = Settings::terrain3dFrom(opt_.terrain3d);
    if (!opt_.terrain2d.empty()) settings_.terrain2d = Settings::terrain2dFrom(opt_.terrain2d);
    if (!opt_.shadows3d.empty()) settings_.shadows3d = opt_.shadows3d != "off" && opt_.shadows3d != "0";
    if (!opt_.timeOfDay.empty()) settings_.timeOfDay = Settings::timeOfDayFrom(opt_.timeOfDay);
    if (!opt_.lighting.empty()) settings_.lighting = Settings::lightingFrom(opt_.lighting);
    // Quality / display (one-frame runs: the struct defaults unless these are given).
    if (opt_.quality == "low") settings_.applyQuality(Settings::Quality::Low);
    else if (opt_.quality == "medium") settings_.applyQuality(Settings::Quality::Medium);
    else if (opt_.quality == "high") settings_.applyQuality(Settings::Quality::High);
    auto onOff = [](const std::string& v) { return v == "on" || v == "1" || v == "true"; };
    if (!opt_.hdr.empty()) settings_.hdr = onOff(opt_.hdr);
    if (!opt_.hdrOutput.empty()) settings_.hdrOutput = onOff(opt_.hdrOutput);
    if (opt_.msaa > 0) settings_.msaa = opt_.msaa >= 4 ? 4 : opt_.msaa >= 2 ? 2 : 1;
    if (opt_.aniso > 0) settings_.aniso = opt_.aniso >= 16 ? 16 : opt_.aniso >= 4 ? 4 : 1;
    if (!opt_.vsync.empty()) settings_.vsync = Settings::vsyncFrom(opt_.vsync);
    if (opt_.fpsLimit >= 0) settings_.fpsLimit = opt_.fpsLimit;
    if (!opt_.resolution.empty()) settings_.resolution = opt_.resolution;  // --resolution (this run only)
    if (!opt_.uiScale.empty()) settings_.uiScale = opt_.uiScale;  // --ui-scale (this run only)
    if (!opt_.touchLayout.empty()) settings_.touchLayout = Settings::touchLayoutFrom(opt_.touchLayout);
    if (!opt_.phoneUiSize.empty()) settings_.phoneUiSize = Settings::phoneUiSizeFrom(opt_.phoneUiSize);
    if (!persist_) settings_.renderResolution = Settings::RenderResolution::Half;  // reproducible screenshots
    if (!opt_.renderResolution.empty()) settings_.renderResolution = Settings::renderResolutionFrom(opt_.renderResolution);
    if (!opt_.artPack.empty()) settings_.artPack = opt_.artPack;
    if (opt_.perfOverlay) settings_.perfOverlay = true;
    if (!opt_.effects.empty()) settings_.effects = opt_.effects == "classic" ? Settings::Effects::Classic : Settings::Effects::Modern;
    // Language for this run (--language / --text-language / ... ; platform/language.hpp).
    if (!opt_.language.empty()) {
        settings_.language = opt_.language;
        settings_.languageText = settings_.languageVoices = settings_.languageMovies = "";
    }
    if (!opt_.textLanguage.empty()) settings_.languageText = opt_.textLanguage;
    if (!opt_.voicesLanguage.empty()) settings_.languageVoices = opt_.voicesLanguage;
    if (!opt_.moviesLanguage.empty()) settings_.languageMovies = opt_.moviesLanguage;
    if (!opt_.translationDir.empty()) settings_.translationDir = opt_.translationDir;
    if (!opt_.aiVoicesDir.empty()) settings_.aiVoicesDir = opt_.aiVoicesDir;
    if (!opt_.ruText.empty()) settings_.russianTextFixes = opt_.ruText != "fargus";
    if (!opt_.ps1Image.empty() && !persist_) settings_.ps1Image = opt_.ps1Image;
    if (!opt_.panelTransparency.empty()) settings_.panelTransparency = Settings::panelTransparencyFrom(opt_.panelTransparency);
    else if (!persist_) settings_.panelTransparency = Settings::PanelTransparency::Off;
    if (!opt_.shroud.empty()) settings_.shroud = opt_.shroud == "classic" ? Settings::Shroud::Classic : Settings::Shroud::Soft;
    else if (!persist_) settings_.shroud = settings_.effects == Settings::Effects::Modern ? Settings::Shroud::Soft : Settings::Shroud::Classic;
    // Graphics: 3D draws every object from the OpenD2K 3D art pack (never the original sprites): without
    // one installed, a saved or --graphics 3d choice runs as Enhanced.
    if (settings_.threeD() && !Settings::modelPackInstalled()) {
        SDL_Log("Graphics: 3D needs the OpenD2K 3D art pack (artpacks/<name>/ with models/ and DATA/) - none installed, "
                "using Enhanced");
        settings_.graphics = Settings::Graphics::Enhanced;
    }
}

App::~App() {
    if (!previewStage_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(previewStage_, ec);  // the link only (remove_all does not follow it)
    }
    movies_.reset();
    session_.reset();
    persistSettings();  // the day / night cycle phase of a game still running at quit
    if (menu_) menu_->setPreviews(nullptr);
    previews_.reset();
    cursor_.reset();
    menu_.reset();
    text_.reset();
    ui_.reset();
    sounds_.reset();
    audio_.reset();
    shutdownVideo();
}

void App::persistSettings() {
    if (persist_ && !settings_.save()) SDL_Log("Could not save settings to %s", Settings::path().c_str());
}

int App::runSimTest() {
    const std::string dir = discoverDataDir(opt_.dataDir, settings_.dataDir);
    if (dir.empty()) {
        SDL_Log("Failed to load game data: no Dune 2000 data folder found (use --data DIR)");
        return 1;
    }
    applyLanguage(settings_, dir, opt_.ps1Image, appMoviesDir(opt_.gameDataRoot));
    GameStart start;
    start.map = opt_.map;
    start.tileset = opt_.tileset;
    GameData g;
    try {
        g = loadGameData(dir, start);
    } catch (const std::exception& e) {
        SDL_Log("Failed to load game data: %s", e.what());
        return 1;
    }
    logMapSummary(g);
    return platform::runSimTest(g);
}

// --sound-test: where the speech of the active language comes from (platform/language.hpp).
int App::runSoundTest() {
    const std::string dir = discoverDataDir(opt_.dataDir, settings_.dataDir);
    if (dir.empty()) {
        SDL_Log("No data folder");
        return 1;
    }
    applyLanguage(settings_, dir, opt_.ps1Image, appMoviesDir(opt_.gameDataRoot));
    SoundBank bank(dir);
    static const char* const kNames[] = {"BUTTON",          "S_LMG",          "S_GAMESAVED",   "H_MISSIONWON",  "O_MISSIONWON",
                                         "S_UNITREADY",     "H_BUILDREADY",   "O_LOWPOWER",    "S_UNDERATTACK", "H_WORMSIGN",
                                         "O_FRIGATEIN3",    "S_MISSIONFAILED", "S_MISSIONWON", "S_AT_INF_REP1", "S_AT_VEH_CON2"};
    int missing = 0;
    for (const char* n : kNames) {
        const std::string src = bank.sourceOf(n);
        missing += src.empty();
        SDL_Log("sound-test: %-16s -> %s", n, src.empty() ? "[missing]" : src.c_str());
    }
    // Every sample of the table: where each comes from.
    std::map<std::string, int> bySource;
    for (int id = 0;; ++id) {
        const std::string& n = bank.nameOf(id);
        if (n.empty()) break;
        std::string src = bank.sourceOf(n);
        if (const auto slash = src.find_last_of('/'); slash != std::string::npos) src.resize(slash);
        if (src.rfind("ps1:", 0) == 0) src = "the PS1 disc";
        ++bySource[src.empty() ? "[missing]" : src];
    }
    for (const auto& [src, count] : bySource) SDL_Log("sound-test: %4d samples from %s", count, src.c_str());
    return missing ? 1 : 0;
}

bool App::initVideo() {
    if constexpr (kTouchPlatform) {
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");     // a connected mouse stays a mouse
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");     // menus take taps as clicks
        SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "soloambient");
        SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");  // first swipe only shows the bar
        // Landscape only (iOS: Info.plist). Android: SDLActivity ignores the manifest's screenOrientation and
        // allows any rotation for a resizable window unless this hint names the orientations.
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");  // Back = Escape (the event loop), not "close the app"
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (settings_.fullscreen && persist_) flags |= SDL_WINDOW_FULLSCREEN;
    // One-frame / recording runs (--screenshot, --record, --smoke, --menu-screenshot, --simtest):
    // a hidden window - nothing appears on the screen, no focus, no stray input.
    if (!persist_) flags |= SDL_WINDOW_HIDDEN;
    if constexpr (kTouchPlatform) flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
    // Settings "Render resolution": Full = the display's real pixels (Retina), Half = points.
    // Everything draws in output pixels; mouse events are converted (platform/pointer.hpp).
    renderFull_ = settings_.fullPixelDensity();
    if (renderFull_) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    setRenderFullActive(renderFull_);
    const int winW = opt_.windowW > 0 ? opt_.windowW : 1280, winH = opt_.windowH > 0 ? opt_.windowH : 800;
    // HDR / EDR output (Settings::hdrOutput, render/hdr.hpp): only for a game that starts in
    // Enhanced / 3D (Original stays untouched). The window starts one pixel short and windowed, so
    // the GPU renderer re-creates its backbuffer - in the swapchain's new half-float format - at
    // the first present (it does that when the swapchain size changes).
    // (Direct3D 12 - the Graphics API setting or SDL_GPU_DRIVER: no extended-range output; its start-up
    // sequence - a window one pixel short, then resized - crashes there. render3d::gpuHdrOk.)
    const char* envDrv = std::getenv("SDL_GPU_DRIVER");
    const bool d3d12Wanted = envDrv ? std::strcmp(envDrv, "direct3d12") == 0 : persist_ && settings_.gpuDriver == "direct3d12";
    // (A hidden window - one-frame runs: screenshots, tests - never acquires a swapchain image, so the GPU
    // renderer's backbuffer would keep the one-pixel-short size while the output reports the full size:
    // viewport / scissor past the backbuffer, SDL asserts, Vulkan loses the device. Nothing to show there.)
    const bool wantHdr = !kTouchPlatform && settings_.hdrOutput && settings_.enhanced() && !d3d12Wanted &&
                         !(flags & SDL_WINDOW_HIDDEN);
    const bool startFull = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (wantHdr) flags &= ~SDL_WindowFlags(SDL_WINDOW_FULLSCREEN);
    // Windows: Options > Graphics > Graphics API (Settings::gpuDriver) for this start; the SDL_GPU_DRIVER
    // environment variable still wins. Auto = Vulkan first, then Direct3D 12 (render3d/gpu.cpp).
#ifdef _WIN32
    if (persist_ && settings_.gpuDriver != "auto" && !std::getenv("SDL_GPU_DRIVER"))
        SDL_SetHint(SDL_HINT_GPU_DRIVER, settings_.gpuDriver.c_str());
#endif
    // The SDL GPU renderer when available: the 3D mode shares its device (render3d/gpu.hpp).
    if (!render3d::createWindowAndRenderer(D2K_APP_TITLE, winW, wantHdr ? winH - 1 : winH, flags, &window_, &renderer_)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    SDL_SetWindowMinimumSize(window_, std::min(800, winW), std::min(500, winH - 1));
    {
        SDL_GPUDevice* dev = render3d::gpuDeviceOf(renderer_);
        setActiveGpuDriver(dev ? SDL_GetGPUDeviceDriver(dev) : "");  // (the Graphics API row's label)
    }
#if !defined(__APPLE__) && !defined(__ANDROID__)
    {  // The window icon (title bar, task bar; tools/make_icons.py). macOS / iOS / Android: the bundle's icon.
        static const unsigned char kIcon[] = {
#include "platform/app_icon_png.inc"
        };
        if (SDL_Surface* icon = SDL_LoadPNG_IO(SDL_IOFromConstMem(kIcon, sizeof kIcon), true)) {
            SDL_SetWindowIcon(window_, icon);
            SDL_DestroySurface(icon);
        }
    }
#endif
    // Render at the display refresh rate (Settings::vsync); smooth motion interpolates between model ticks.
    applySync();
    if (wantHdr) {
        hdrOut_ = std::make_unique<render::Hdr>(renderer_);
        hdrExtended_ = render3d::gpuHdrOk() && hdrOut_->available() && setHdrSwapchain(true);
        if (!hdrExtended_) hdrOut_.reset();
        SDL_SetWindowSize(window_, winW, winH);
        if (startFull) SDL_SetWindowFullscreen(window_, true);
        hdrCheckFrames_ = hdrExtended_ ? 3 : 0;
    }
    setHdrOutputStart(settings_.hdrOutput, hdrExtended_);
    updateHdrHeadroom(true);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    {
        int ww = 0, wh = 0, ow = 0, oh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetRenderOutputSize(renderer_, &ow, &oh);
        SDL_Log("Renderer: %s, window %dx%d points, output %dx%d pixels (render resolution %s)", SDL_GetRendererName(renderer_), ww,
                wh, ow, oh, renderFull_ ? "full" : "half");
    }
    ui_ = std::make_unique<Ui>(renderer_);
    menu_ = std::make_unique<Menu>(window_, renderer_, *ui_, settings_);
    if (opt_.phoneMenus > 0) Menu::forcePhoneUi(opt_.phoneMenus, opt_.safeInsets.value_or(std::array<float, 4>{}));
    menu_->setPersistent(persist_);
    movies_ = std::make_unique<Movies>(renderer_, settings_);
    if (!opt_.gameDataRoot.empty()) movies_->setMoviesDirOverride(appMoviesDir(opt_.gameDataRoot));
    if (!opt_.ps1Image.empty()) movies_->setPs1Override(opt_.ps1Image);
    menu_->setMovies(movies_.get());
    menu_->gameDataRoot = opt_.gameDataRoot;
    touch_ = std::make_unique<TouchControls>(settings_);
    SDL_AddEventWatch(&App::appEventWatch, this);
    return true;
}

// Runs inside the event pump (iOS may give no more time after these events).
// VSync On / Off / Adaptive (Settings::vsync). Adaptive = late frames are shown at once (tearing)
// instead of waiting a whole refresh; the GPU renderer may not have it: then On.
// The swapchain composition (EDR output): HDR_EXTENDED_LINEAR or back to SDR, with the present mode
// the renderer chose for the vsync setting (SDL_SetRenderVSync re-sets the swapchain with SDR).
bool App::setHdrSwapchain(bool hdr) {
    SDL_GPUDevice* dev = render3d::gpuDeviceOf(renderer_);
    if (!dev) return false;
    const SDL_GPUSwapchainComposition c = hdr ? SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR : SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    if (!SDL_WindowSupportsGPUSwapchainComposition(dev, window_, c)) {
        SDL_Log("HDR output: the display has no extended range swapchain - SDR");
        return false;
    }
    int v = 1;
    SDL_GetRenderVSync(renderer_, &v);
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (v == 0) {  // as the GPU renderer picks it
        mode = SDL_WindowSupportsGPUPresentMode(dev, window_, SDL_GPU_PRESENTMODE_IMMEDIATE) ? SDL_GPU_PRESENTMODE_IMMEDIATE
               : SDL_WindowSupportsGPUPresentMode(dev, window_, SDL_GPU_PRESENTMODE_MAILBOX) ? SDL_GPU_PRESENTMODE_MAILBOX
                                                                                             : SDL_GPU_PRESENTMODE_VSYNC;
    }
    if (!SDL_SetGPUSwapchainParameters(dev, window_, c, mode)) {
        SDL_Log("HDR output: swapchain change failed (%s) - SDR", SDL_GetError());
        return false;
    }
    return true;
}

// The frame's target: the EDR canvas (half float, sRGB-coded values kept above 1) or the window.
void App::beginFrame() {
    if (!hdrExtended_) {
        render::setScreenTarget(nullptr);
        return;
    }
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    float cw = 0, ch = 0;
    if (canvas_) SDL_GetTextureSize(canvas_, &cw, &ch);
    if (!canvas_ || int(cw) != w || int(ch) != h) {
        if (canvas_) SDL_DestroyTexture(canvas_);
        canvas_ = render::Hdr::createTarget(renderer_, std::max(1, w), std::max(1, h));
    }
    render::setScreenTarget(canvas_);
    SDL_SetRenderTarget(renderer_, canvas_);
}

// Present; EDR: the canvas decoded to linear light into the half-float backbuffer first.
void App::presentFrame() {
    if (hdrExtended_ && canvas_ && hdrOut_) {
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderClipRect(renderer_, nullptr);
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(renderer_, &w, &h);
        hdrOut_->presentCanvas(canvas_, SDL_FRect{0, 0, float(w), float(h)});
    } else if (SDL_GetRenderTarget(renderer_)) {
        SDL_SetRenderTarget(renderer_, nullptr);
    }
    SDL_RenderPresent(renderer_);
    if (hdrCheckFrames_ > 0 && --hdrCheckFrames_ == 0) {
        // The backbuffer must have become half float (the swapchain's format) by now; else SDR.
        const SDL_Rect one{0, 0, 1, 1};
        SDL_Surface* s = SDL_RenderReadPixels(renderer_, &one);
        const bool isFloat = s && SDL_ISPIXELFORMAT_FLOAT(s->format);
        if (s) SDL_DestroySurface(s);
        if (!isFloat) {
            SDL_Log("HDR output: the backbuffer stayed 8-bit - SDR");
            setHdrSwapchain(false);
            hdrExtended_ = false;
            setHdrOutputStart(settings_.hdrOutput, false);
            updateHdrHeadroom(true);
        } else {
            SDL_Log("HDR output: extended range active (half-float backbuffer)");
        }
    }
}

void App::applySync() {
    appliedVsync_ = settings_.vsync;
    syncApplied_ = true;
    int mode = settings_.vsync == Settings::VSync::Off ? SDL_RENDERER_VSYNC_DISABLED
               : settings_.vsync == Settings::VSync::Adaptive ? SDL_RENDERER_VSYNC_ADAPTIVE : 1;
    bool ok = SDL_SetRenderVSync(renderer_, mode);
    if (!ok && mode == SDL_RENDERER_VSYNC_ADAPTIVE) {
        SDL_Log("Adaptive VSync unavailable (%s): VSync on", SDL_GetError());
        mode = 1;
        ok = SDL_SetRenderVSync(renderer_, mode);
    }
    if (!ok && mode == SDL_RENDERER_VSYNC_DISABLED) SDL_Log("VSync off unavailable (%s)", SDL_GetError());
    int now = 0;
    vsync_ = SDL_GetRenderVSync(renderer_, &now) && now != SDL_RENDERER_VSYNC_DISABLED;
    if (!vsync_ && settings_.vsync != Settings::VSync::Off) SDL_Log("VSync unavailable (%s): pacing frames to the display rate", SDL_GetError());
    SDL_Log("Display: VSync %s (%d), frame limit %s", Settings::vsyncName(settings_.vsync), now,
            settings_.fpsLimit > 0 ? std::to_string(settings_.fpsLimit).c_str() : "none");
    if (hdrExtended_) setHdrSwapchain(true);  // SDL_SetRenderVSync may have reset it to SDR
}

// The EDR / HDR headroom of the window's display (changes with the brightness on a Mac; SDL sends
// SDL_EVENT_WINDOW_HDR_STATE_CHANGED): the tone map's peak (render/hdr.hpp).
void App::updateHdrHeadroom(bool log) {
    const SDL_PropertiesID p = SDL_GetWindowProperties(window_);
    const bool on = SDL_GetBooleanProperty(p, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    const float h = on ? std::max(1.0f, SDL_GetFloatProperty(p, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f)) : 1.0f;
    if (log || std::fabs(h - hdrHeadroom_) > 0.01f)
        SDL_Log("HDR output: %s, display headroom %.2fx SDR white%s", hdrExtended_ ? "extended range" : "SDR", h,
                hdrExtended_ ? " (highlights use up to 4x)" : "");
    hdrHeadroom_ = h;
    render::setHdrOutput(hdrExtended_, h);
}

bool SDLCALL App::appEventWatch(void* self, SDL_Event* e) {
    App* app = static_cast<App*>(self);
    switch (e->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND: app->background_ = true; break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND: app->background_ = false; break;
    case SDL_EVENT_TERMINATING: app->persistSettings(); break;
    default: break;
    }
    return true;
}

// Background: the game pauses (its pause menu stays open on return), sound stops, nothing is
// drawn (Metal must not render in the background) and the settings are saved.
void App::enterBackground(bool bg) {
    if (bg == inBackground_) return;
    inBackground_ = bg;
    if (bg) {
        if (session_ && !session_->paused() && session_->endState() == game::GameEnd::None) session_->setPaused(true);
        if (touch_) touch_->reset();
        persistSettings();
    }
    if (audio_) audio_->setDevicePaused(bg);
}

void App::shutdownVideo() {
    if (window_) SDL_RemoveEventWatch(&App::appEventWatch, this);
    hdrOut_.reset();
    if (canvas_) SDL_DestroyTexture(canvas_);
    canvas_ = nullptr;
    render::setScreenTarget(nullptr);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    SDL_Quit();
}

// Everything that depends on the data folder outside a game: the menu font and artwork,
// the mission lists, sounds and music.
namespace {
// The original keeps GameSpeed / ScrollRate in dune2000.ini next to the data folder.
void importOriginalIni(const std::string& dataDir, Settings& s) {
    const std::string ini = formats::findFileNoCase(dataDir + "/..", "dune2000.ini");
    if (ini.empty()) return;
    std::ifstream in(ini);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "GameSpeed") s.gameSpeed = std::clamp(std::stoi(v), Settings::kMinGameSpeed, Settings::kMaxGameSpeed);
            else if (k == "ScrollRate") s.scrollRate = std::clamp(std::stoi(v), Settings::kMinScrollRate, Settings::kMaxScrollRate);
        } catch (...) {}
    }
    SDL_Log("Imported GameSpeed %d / ScrollRate %d from %s", s.gameSpeed, s.scrollRate, ini.c_str());
}
}  // namespace

bool App::loadData(const std::string& dir) {
    // The language first: it picks the font, texts, speech and cutscene files (platform/language.hpp).
    applyLanguage(settings_, dir, opt_.ps1Image, appMoviesDir(opt_.gameDataRoot));
    languageKey_ = languageKey(settings_);
    try {
        font_ = loadGameFont(dir, false);
    } catch (const std::exception& e) {
        SDL_Log("Font: %s", e.what());
        return false;
    }
    dataDir_ = dir;
    ui_->setFont(nullptr);
    if (menu_) menu_->setPreviews(nullptr);
    previews_.reset();  // its Ui holds the old font
    text_ = std::make_unique<render::TextRenderer>(renderer_, font_);
    ui_->setFont(text_.get());
    // Graphics page previews (one-frame runs: rendered when asked, cached only with D2K_PREVIEWS_DIR).
    previews_ = std::make_unique<GraphicsPreviews>(renderer_, text_.get());
    {
        const char* env = std::getenv("D2K_PREVIEWS_DIR");
        previews_->setData(dir, languageKey_, env && *env ? std::string(env) : persist_ ? Settings::previewCacheDir() : std::string(),
                           settings_.languageText.empty() ? settings_.language : settings_.languageText);
    }
    previews_->setSynchronous(!persist_ && !std::getenv("D2K_PREVIEWS_ASYNC"));  // (async in one-frame runs: a check)
    menu_->setPreviews(previews_.get());
    menu_->setData(dir);
    movies_->setData(dir);
    // The original cursors replace the system cursor while it is over the window.
    try {
        cursor_ = std::make_unique<render::CursorRenderer>(renderer_, formats::findFileNoCase(dir, "MOUSE.R16"));
        SDL_HideCursor();
    } catch (const std::exception& e) {
        SDL_Log("Cursors: %s (using the system cursor)", e.what());
        cursor_.reset();
        SDL_ShowCursor();
    }
    if (audio_) {
        sounds_ = std::make_unique<SoundBank>(dir);
        music_ = opt_.noMusic ? MusicPlayer{} : MusicPlayer(dir);
    }
    if (persist_ && !settings_.speedFromFile) {
        importOriginalIni(dir, settings_);
        settings_.speedFromFile = true;
        persistSettings();
    }
    SDL_Log("Game data: %s", dir.c_str());
    return true;
}

void App::drawLoading(const std::string& what) {
    beginFrame();
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    ui_->gradient({0, 0, float(w), float(h)}, Colour{26, 18, 10, 255}, Colour{0, 0, 0, 255});
    const float s = h >= 1100 ? 2.f : 1.f;
    ui_->text("Loading", w / 2.0f, h / 2.0f - ui_->textHeight(2 * s), theme::kTitle, 2 * s, Align::Centre);
    ui_->text(what, w / 2.0f, h / 2.0f + 6 * s, theme::kDim, s, Align::Centre);
    presentFrame();
}

bool App::startGame(const GameStart& start) {
    if (!opt_.smoke && opt_.menuScreenshot.empty()) drawLoading(start.title.empty() ? start.map : start.title);
    std::unique_ptr<GameData> g;
    try {
        g = std::make_unique<GameData>(loadGameData(dataDir_, start));
    } catch (const std::exception& e) {
        SDL_Log("Failed to load game data: %s", e.what());
        return false;
    }
    logMapSummary(*g);
    SessionConfig cfg;
    cfg.at = opt_.at;
    cfg.zoom = opt_.zoom;
    cfg.ticks = opt_.ticks;
    cfg.reveal = opt_.reveal;
    cfg.classicPeek = opt_.classicPeek;
    if (opt_.killBuilding) { cfg.killX = opt_.killBuilding->first; cfg.killY = opt_.killBuilding->second; }
    cfg.battle = opt_.battle;
    cfg.vfxDemo = opt_.vfxDemo;
    cfg.selectAll = opt_.selectAll;
    cfg.cursorMode = opt_.cursorMode;
    if (opt_.smoke) cfg.mouseAt = opt_.cursorAt;
    cfg.sidebarPage = opt_.sidebarPage;
    cfg.safeInsets = opt_.safeInsets;
    cfg.cameraTilt = opt_.cameraTilt;
    cfg.cameraYaw = opt_.cameraYaw;
    if (previews_) previews_->release();  // the preview game's GPU memory
    if (sounds_) sounds_->setOverlay(g->modsDataDir);  // community mission sounds (cleared in the menu)
    session_ = std::make_unique<Session>(renderer_, std::move(g), settings_, audio_.get(), sounds_.get(), cfg);
    if (!opt_.recordDir.empty() || opt_.fixedStep) {
        std::error_code ec;
        std::filesystem::create_directories(opt_.recordDir, ec);
        session_->setFixedStep(1.0 / double(opt_.recordFps));
    }
    // The demo switches apply to the first game only.
    opt_.at.reset();
    opt_.killBuilding.reset();
    opt_.ticks = 0;
    opt_.battle = opt_.selectAll = opt_.vfxDemo = false;
    return true;
}

// Recreates the saved session: the same data a new game of that map/setup loads, then the
// saved world state on top (game/savegame.hpp).
bool App::loadSavedGame(int slot, std::string& error) {
    const std::vector<std::uint8_t> bytes = readSaveSlot(slot, error);
    if (bytes.empty()) return false;
    game::SaveMeta meta;
    std::span<const std::uint8_t> section;
    try {
        meta = game::readSaveFile(bytes, section);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (meta.kind == game::SaveKind::CampaignMap) {
        // Saved on the territory map (UISaveGame FUN_00474e20 outside a battle): no world, back to
        // that map (FUN_00475740: LOADMISSION -> MAINMENU, GLOBE), also from inside a battle.
        if (meta.campaignHouse < 0 || meta.campaignHouse > 2 || meta.missionNumber < 1 || meta.missionNumber > 9) {
            error = "Bad campaign-map save (house " + std::to_string(meta.campaignHouse) + ", mission " +
                    std::to_string(meta.missionNumber) + ")";
            return false;
        }
        session_.reset();
        menu_->openCampaignSave(meta);
        persistSettings();  // gDifficultyLevel from the save
        SDL_Log("Loaded the territory map: house %d, mission %d", meta.campaignHouse, meta.missionNumber);
        return true;
    }
    const int ticksAfter = opt_.ticks;  // --load N --ticks M: M ticks after loading
    opt_.ticks = 0;
    if (!startGame(gameStartOf(meta))) {
        error = "Could not load the game data for " + meta.map;
        return false;
    }
    try {
        session_->applySave(meta, section);
    } catch (const std::exception& e) {
        session_.reset();
        error = std::string("Could not restore the save: ") + e.what();
        return false;
    }
    if (ticksAfter > 0) session_->runTicks(ticksAfter);
    return true;
}

namespace {
// The frame as an 8-bit sRGB surface. With EDR output it is read from the half-float canvas, whose
// values are sRGB-coded (render/hdr.hpp; highlights above SDR white clip).
SDL_Surface* readFrame(SDL_Renderer* r) {
    SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
    if (!s || SDL_BITSPERPIXEL(s->format) == 32) return s;
    SDL_SetSurfaceColorspace(s, SDL_COLORSPACE_SRGB);
    SDL_Surface* c = SDL_ConvertSurfaceAndColorspace(s, SDL_PIXELFORMAT_ARGB8888, nullptr, SDL_COLORSPACE_SRGB, 0);
    SDL_DestroySurface(s);
    return c;
}
}  // namespace

bool App::saveScreenshot(const std::string& path) {
    SDL_Surface* s = readFrame(renderer_);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

// The mouse cursor, on top of everything: the session picks it from what is under the mouse;
// menus use the pointer (the original's CUIManager pages force cursor 0).
// Music per screen (CUIManager__JumpToMenu 0x473700 / FUN_00473130: the main menu plays
// options.aud from the start each time it is entered, other front-end pages stop the music;
// Game__MainLoopFrame 0x448550 starts the mission's track) and the UI click sound (FUN_00437600
// plays "BUTTON" through FUN_004762f0 at volume 100 in menus, SFXVolume in game).
void App::updateSound() {
    if (!audio_) return;
    audio_->update();
    if (movies_ && movies_->active()) {
        // CMovie (FUN_0048d730) stops the music and the sample channels; the page music starts
        // again when the movie is over.
        music_.stop();
        musicMenuPage_ = -1;
        musicSession_ = nullptr;
        audio_->stopAll();
        menuSpeech_.reset();
        ui_->takeClickSounds();
        return;
    }
    if (session_.get() != musicSession_) {
        musicSession_ = session_.get();
        musicMenuPage_ = -1;
        if (session_) {
            const GameData& g = session_->data();
            const int house = g.campaignMusic ? std::clamp(g.playerSide, 0, 2) : -1;
            music_.play(g.musicTrack, house);
        }
    }
    if (session_) {
        if (const std::string m = session_->takeMusicRequest(); !m.empty()) {
            std::string t = m;
            if (const auto dot = t.rfind('.'); dot != std::string::npos) t.resize(dot);
            const GameData& g = session_->data();
            music_.play(t, g.campaignMusic ? std::clamp(g.playerSide, 0, 2) : -1);
        }
    }
    if (!session_) {
        // A new page (or a new track on the campaign pages, e.g. the score page's score.aud).
        const std::string track = menu_->musicTrack();
        // (any track name: the original sound page plays any of them)
        const int key = int(menu_->page()) * 65536 + int(std::hash<std::string>{}(track) & 0xffff);
        if (key != musicMenuPage_) {
            musicMenuPage_ = key;
            if (!track.empty()) music_.play(track);
            else music_.stop();
        }
        // Campaign pages: speech (territory announcements, S_SCORETICK) through QueueAudioToPlay
        // on the speech channel at volume 100, UI samples on the click channels.
        if (sounds_) {
            sounds_->setOverlay({});  // back from a community mission: the game's own sounds
            ChannelOutput out(audio_.get(), sounds_.get());
            for (const MenuSound& m : menu_->takeSounds()) {
                const int id = sounds_->idOf(m.name);
                if (id < 0) continue;
                if (m.queued) menuSpeech_.queue(out, id, false, 0, 0, static_cast<std::uint32_t>(SDL_GetTicks()));
                else game::SoundSystem::playUi(out, id, 100);
            }
            menuSpeech_.update(out, 100);
            menu_->setSpeechBusy(menuSpeech_.speaking() || out.busy(0));
        }
    } else {
        menuSpeech_.reset();
    }
    music_.update(*audio_, settings_.musicVolume);
    if (const int clicks = ui_->takeClickSounds(); clicks > 0 && sounds_) {
        if (session_) {
            session_->uiSound("BUTTON");
        } else {
            ChannelOutput out(audio_.get(), sounds_.get());
            game::SoundSystem::playUi(out, sounds_->idOf("BUTTON"), 100);
        }
    }
}

void App::drawPerfOverlay() {
    if (!settings_.perfOverlay || perfFrameMs_.empty() || !ui_) return;
    const std::size_t n = std::min<std::size_t>(perfFrameMs_.size(), 60);
    double sumF = 0, sumC = 0, maxF = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t k = (perfAt_ + perfFrameMs_.size() - 1 - i) % perfFrameMs_.size();
        sumF += perfFrameMs_[k];
        sumC += perfCpuMs_[k];
        maxF = std::max(maxF, double(perfFrameMs_[k]));
    }
    const double avgF = sumF / double(n), avgC = sumC / double(n);
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
    static const std::string model = deviceModel();
    char line[200];
    SDL_snprintf(line, sizeof line, "%.0f fps   frame %.1f ms (max %.1f)   cpu %.1f ms   %dx%d   %s %s", avgF > 0 ? 1000.0 / avgF : 0.0,
                 avgF, maxF, avgC, ow, oh, model.c_str(), settings_.deviceProfile.c_str());
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    Ui::ModernScope modern(*ui_);
    const float ts = std::max(1.0f, float(oh) / 540.0f);
    const float w = ui_->textWidth(line, ts) + 16, h = ui_->textHeight(ts) + 8;
    ui_->fill({4, 4, w, h}, Colour{0, 0, 0, 170});
    ui_->text(line, 12, 8, avgF > 20 ? Colour{255, 120, 90, 255} : Colour{200, 240, 160, 255}, ts);
}

void App::drawCursor() {
    if (!cursor_ || !realMouse_) return;  // iPad: only for a connected mouse / trackpad
    float x = 0, y = 0;
    if (opt_.cursorAt) {
        x = opt_.cursorAt->first;
        y = opt_.cursorAt->second;
    } else if (!cursorPosition(window_, settings_.fullscreen, x, y)) {  // platform/pointer.hpp (notch band)
        return;
    }
    // Mouse coordinates are window points; the renderer draws in output pixels.
    int ww = 0, wh = 0, ow = 0, oh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
    if (!opt_.cursorAt && ww > 0 && wh > 0) {
        x *= float(ow) / float(ww);
        y *= float(oh) / float(wh);
    }
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    if (session_) {
        const Session::CursorChoice c = session_->cursorAt(x, y);
        cursor_->draw(c.id, x, y, c.scale, c.hq);
    } else {
        const float s = float(std::max(1, std::min(ow / 640, oh / 400)));
        cursor_->draw(static_cast<render::Cursor>(menu_->cursorId()), x, y, s, settings_.enhanced());
    }
}

// --install-from: unpack the Gruntmods installer (or reuse the copy) and use that data folder.
int App::runInstallFrom() {
    SDL_Log("Building the game data from %s ...", opt_.installFrom.c_str());
    int lastDecile = -1;
    const std::string current = opt_.dataDir.empty() ? settings_.dataDir : normalizeDataDir(opt_.dataDir);
    const ImportResult r = importInstaller(opt_.installFrom, current, opt_.gameDataRoot,
                                           [&](const formats::nsis::Progress& p) {
                                               const int d = p.inputTotal ? int(p.inputDone * 10 / p.inputTotal) : 0;
                                               if (d != lastDecile) {
                                                   lastDecile = d;
                                                   SDL_Log("  %3d%%  %zu/%zu files", d * 10, p.filesDone, p.filesTotal);
                                               }
                                               return true;
                                           });
    if (!r.ok) {
        SDL_Log("Install from %s failed: %s", opt_.installFrom.c_str(), r.error.c_str());
        return 1;
    }
    if (r.reused) SDL_Log("Using the existing copy %s", r.dataDir.c_str());
    else if (r.moviesOnly) SDL_Log("Added %zu cutscene files (%.1f MB) in %.1f s to %s", r.files, double(r.bytes) / 1e6, r.seconds, r.moviesDir.c_str());
    else SDL_Log("Unpacked %zu files (%.1f MB) in %.1f s -> %s", r.files, double(r.bytes) / 1e6, r.seconds, r.dataDir.c_str());
    if (r.dataDir.empty()) return 0;  // movies only, no data folder yet
    opt_.dataDir = r.dataDir;
    if (persist_ && opt_.gameDataRoot.empty() && settings_.dataDir != r.dataDir) {
        settings_.dataDir = r.dataDir;
        persistSettings();
    }
    return 0;
}

void App::startupMovie() {
    if (dataDir_.empty()) return;
    if (!opt_.movie.empty()) {
        const bool file = opt_.movie.find('.') != std::string::npos || opt_.movie.find('/') != std::string::npos;
        if (!(file ? movies_->playFile(opt_.movie) : movies_->play(opt_.movie, opt_.movieHouse, true)))
            SDL_Log("--movie %s: cannot play it", opt_.movie.c_str());
        return;
    }
    // FUN_00442580: the first start plays MVE_INTRO1 (Intro1.uil mve_intro1 -> MVE_INTRO2), later
    // starts MVE_INTRO3 (Intro3.uil: mve_fast, the title). The original then goes on to house
    // selection after the full intro; we return to the main menu.
    if (!persist_ || !opt_.map.empty() || opt_.loadSlot >= 0 || !settings_.introOnStartup) return;
    if (!settings_.introSeen) {
        if (movies_->play("mve_intro1")) {
            movies_->queue("mve_intro2");
            settings_.introSeen = true;
            persistSettings();
        }
    } else {
        movies_->play("mve_fast");
    }
}

int App::run() {
    if (!opt_.installFrom.empty()) {
        const int rc = runInstallFrom();
        if (rc != 0 || opt_.installOnly) return rc;
    }
    if (opt_.updateCheck) return AppUpdateChecker::instance().runBlocking([](const std::string& l) { SDL_Log("%s", l.c_str()); });
    if (opt_.artpackCheck || opt_.artpackInstall)
        return ArtPackDownloader::instance().runBlocking(opt_.artpackInstall, [](const std::string& l) { SDL_Log("%s", l.c_str()); });
    if (!opt_.httpGetUrl.empty())
        return httpGetToFile(opt_.httpGetUrl, opt_.httpGetOut, [](const std::string& l) { SDL_Log("%s", l.c_str()); });
    if (opt_.simtest) return runSimTest();
    if (opt_.soundTest) return runSoundTest();
    if (!initVideo()) return 1;
    if (!opt_.smoke && !opt_.mute && opt_.menuScreenshot.empty()) audio_ = std::make_unique<Audio>();
    movies_->setAudio(audio_.get());

    std::vector<std::string> tried;
    const std::string dir = discoverDataDir(opt_.dataDir, settings_.dataDir, &tried);
    if (!dir.empty() && loadData(dir)) {
        if (persist_ && opt_.dataDir.empty() && settings_.dataDir != dir) {
            settings_.dataDir = dir;
            persistSettings();
        }
    } else {
        SDL_Log("No Dune 2000 data folder found");
        menu_->triedDataDirs = tried;
        const bool wantsDataPage = !opt_.menuScreenshot.empty() && opt_.menuPage == "datadir";
        if ((opt_.smoke || !opt_.menuScreenshot.empty()) && !wantsDataPage) {
            SDL_Log("Failed to load game data: no data folder (use --data DIR)");
            return 1;
        }
    }

    // --render-previews OUTDIR: the shipped Graphics previews (platform/preset_previews.hpp).
    if (!opt_.renderPreviews.empty()) {
        if (!previews_) {
            SDL_Log("--render-previews: no game data (use --data DIR)");
            return 1;
        }
        return previews_->renderShippedSet(opt_.renderPreviews, settings_);
    }

    // --map: straight into that mission, as before the menu existed.
    if (!opt_.map.empty() && !dataDir_.empty()) {
        GameStart start;
        start.map = opt_.map;
        start.tileset = opt_.tileset;
        start.title = opt_.map;
        start.playAsSide = opt_.playAs;
        if (opt_.skirmish) {
            start.skirmish = true;
            start.title = "Skirmish - " + opt_.map;
            start.sk.playerHouse = settings_.skirmishHouse;
            start.sk.aiPlayers = opt_.skirmishAi > 0 ? std::clamp(opt_.skirmishAi, 1, 7) : settings_.skirmishAi;
            start.sk.techLevel = settings_.skirmishTech;
            start.sk.startingCredits = std::uint32_t(settings_.skirmishCredits);
            start.sk.difficulty = settings_.skirmishDifficulty;
        }
        if (!startGame(start) && (opt_.smoke || !opt_.menuScreenshot.empty())) return 1;
        if (session_ && opt_.saveSlot >= 0) {
            std::string err;
            if (!session_->saveToSlot(opt_.saveSlot, err)) {
                SDL_Log("Save failed: %s", err.c_str());
                if (opt_.smoke || !opt_.menuScreenshot.empty()) return 1;
            }
        }
    } else if (opt_.loadSlot >= 0 && !dataDir_.empty()) {
        std::string err;
        if (!loadSavedGame(opt_.loadSlot, err)) {
            SDL_Log("Load failed: %s", err.c_str());
            if (opt_.smoke || !opt_.menuScreenshot.empty()) return 1;
            menu_->open(MenuPage::Load);
            menu_->loadError = err;
        }
    }

    if (!opt_.menuScreenshot.empty()) {
        if (opt_.menuPage.rfind("pause", 0) == 0) {
            if (!session_) {
                SDL_Log("--menu-page pause needs --map (or --load)");
                return 1;
            }
            session_->showPausePage(opt_.menuPage);
        } else if (opt_.menuPage.empty() && opt_.loadSlot >= 0 && !session_ && menu_->page() == MenuPage::Campaign) {
            // --load N of a campaign-map save: its territory map as loaded.
        } else {
            session_.reset();
            if (!menu_->openByName(opt_.menuPage)) {
                SDL_Log("Unknown menu page '%s'", opt_.menuPage.c_str());
                return 1;
            }
            // --menu-page campaign-map[-a|-h|-o] --save N: a campaign-map save of that map.
            if (opt_.saveSlot >= 0 && opt_.map.empty() && menu_->campaignMapShown()) {
                std::string err;
                if (!menu_->saveCampaignMap(opt_.saveSlot, err)) {
                    SDL_Log("Save failed: %s", err.c_str());
                    return 1;
                }
            }
        }
    }

    startupMovie();

    int rc = 0;
    bool running = true;
    int framesLeft = opt_.frames;  // --frames: one-frame modes run this many frames
    std::vector<double> cpuMs, frameMs;
    Uint64 frameStart = SDL_GetTicksNS();
    while (running) {
        const Uint64 cpuStart = SDL_GetTicksNS();
        SDL_Event e;
        const float pixelsPerPt = windowToPixels(window_);  // 1 unless Render resolution: Full on Retina
        while (SDL_PollEvent(&e)) {
            eventToPixels(e, pixelsPerPt);  // mouse positions in output pixels from here on (platform/pointer.hpp)
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED || e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) updateHdrHeadroom(false);
            if constexpr (kAndroid) {
                // Android: the surface often starts smaller (system bars / cutout) and grows once immersive
                // fullscreen applies; the window's size in pixels was already the full size, so SDL sends no
                // PIXEL_SIZE_CHANGED and SDL_gpu (3.4) keeps the first, smaller Vulkan swapchain, which Android
                // then stretches: a cropped picture and taps in the wrong place. Re-setting the frames in flight
                // (the GPU renderer's 1) rebuilds the swapchain at the surface's current size.
                if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    if (SDL_GPUDevice* dev = render3d::gpuDeviceOf(renderer_)) SDL_SetGPUAllowedFramesInFlight(dev, 1);
                // The system Back button / gesture (SDL: AC Back, SDL_HINT_ANDROID_TRAP_BACK_BUTTON) = Escape:
                // the in-game menu, a page back.
                if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.scancode == SDL_SCANCODE_AC_BACK) {
                    e.key.scancode = SDL_SCANCODE_ESCAPE;
                    e.key.key = SDLK_ESCAPE;
                }
            }
            if (!persist_) continue;  // one-frame / recording runs take no input (hidden window)
            if (movies_->active()) {
                movies_->handleEvent(e);  // skip keys; nothing else gets input during a movie
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) swallowMouseUp_ = true;
                continue;
            }
            if (swallowMouseUp_ && e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                swallowMouseUp_ = false;  // the release of the click that skipped a movie
                continue;
            }
            const bool sessionInput = session_ && !session_->paused() && session_->endState() == game::GameEnd::None;
            // Touch (iPad; a touch screen elsewhere): SDL turns touches into mouse events with
            // which == SDL_TOUCH_MOUSEID. Menus use those; in a game the gestures are read from
            // the finger events instead (platform/touch_controls.hpp).
            const bool finger = e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_UP ||
                                e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_CANCELED;
            const bool directTouch = finger && SDL_GetTouchDeviceType(e.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT;
            SDL_MouseID mouseId = 0;
            if (e.type == SDL_EVENT_MOUSE_MOTION) mouseId = e.motion.which;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) mouseId = e.button.which;
            else if (e.type == SDL_EVENT_MOUSE_WHEEL) mouseId = e.wheel.which;
            const bool touchMouse = mouseId == SDL_TOUCH_MOUSEID;
            if (directTouch) realMouse_ = false;
            else if ((e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) && !touchMouse) realMouse_ = true;
            if (sessionInput && directTouch) {
                touch_->handleEvent(e, *session_, *ui_, window_);
                continue;
            }
            if (sessionInput && touchMouse) continue;
            if (!sessionInput && finger) touch_->reset();
            if (sessionInput && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) session_->setTouchInput(false);
            if (sessionInput) {
                session_->handleEvent(e);
                if (e.type == SDL_EVENT_MOUSE_MOTION) ui_->handleEvent(e);
            } else {
                ui_->handleEvent(e);
                if (session_) session_->handleEvent(e);  // ignored while paused; keeps the camera keys alive after the end
            }
        }

        enterBackground(background_);
        if (inBackground_) {  // iPad in the background: no drawing, just wait
            SDL_Delay(100);
            continue;
        }
        beginFrame();
        // Our own labels (render/ui_text.hpp) follow the text language.
        render::setUiLanguage(settings_.languageText.empty() ? settings_.language : settings_.languageText);
        if (movies_->active()) {
            movies_->render();
        } else if (session_) {
            const bool sessionInput = !session_->paused() && session_->endState() == game::GameEnd::None;
            if (sessionInput) touch_->update(*session_, *ui_);
            else touch_->reset();
            session_->update();
            ui_->beginFrame();
            session_->render(*ui_);
            // On-screen touch buttons: always on the iPad, elsewhere once a touch screen was used.
            if (sessionInput && (kTouchPlatform || touch_->used() || session_->phoneLayout())) touch_->draw(*session_, *ui_);
            ui_->endFrame();
        } else {
            if (previews_) previews_->work();  // offscreen, between the menu frames
            const MenuResult res = menu_->frame();
            if (res.kind == MenuResult::Quit) running = false;
            if (menu_->wantSave) {  // e.g. the first-run welcome page's choice
                menu_->wantSave = false;
                persistSettings();
            }
            // A new language (Options, the original SETTINGS page, the data-folder page): reload
            // the front end's font, texts, sounds and movie names, back on the options page.
            if (!dataDir_.empty() && languageKey(settings_) != languageKey_ && res.kind == MenuResult::None) {
                const bool fromDataPage = menu_->page() == MenuPage::DataDir;
                persistSettings();
                if (loadData(dataDir_)) menu_->open(fromDataPage ? MenuPage::DataDir : MenuPage::Options);
            }
            if (res.kind == MenuResult::DataDirChosen && loadData(res.dataDir)) {
                settings_.dataDir = res.dataDir;
                persistSettings();
                menu_->open(MenuPage::Main);
            }
            if (res.kind == MenuResult::Load) {
                persistSettings();
                std::string err;
                if (!loadSavedGame(res.slot, err)) {
                    SDL_Log("Load failed: %s", err.c_str());
                    menu_->open(MenuPage::Load);
                    menu_->loadError = err;
                }
            }
            if (res.kind == MenuResult::Start) {
                persistSettings();
                if (!startGame(res.start)) menu_->dataDirError = "Could not load " + res.start.map;
            }
        }

        if (!movies_->active()) drawCursor();
        drawPerfOverlay();
        const bool lastFrame = framesLeft <= 1;
        std::string shot = !lastFrame ? std::string() : !opt_.menuScreenshot.empty() ? opt_.menuScreenshot : opt_.screenshot;
        if (!opt_.recordDir.empty() && session_) {  // --record: every frame
            char name[32];
            SDL_snprintf(name, sizeof name, "/frame_%05d.bmp", recordFrame_++);
            shot = opt_.recordDir + name;
        }
        perfCpuEnd_ = SDL_GetTicksNS();
        if (opt_.frames > 1) cpuMs.push_back(double(perfCpuEnd_ - cpuStart) / 1e6);
        if (!shot.empty() && !saveScreenshot(shot)) {
            SDL_Log("Screenshot failed: %s", SDL_GetError());
            rc = 1;
        }
        presentFrame();
        if (!syncApplied_ || appliedVsync_ != settings_.vsync) applySync();
        {
            // Frame pacing: the frame limit (Settings::fpsLimit; also under vsync when it is below
            // the refresh rate), else the display rate when vsync is wanted but unavailable;
            // VSync Off without a limit runs uncapped.
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
            const float hz = mode && mode->refresh_rate > 0 ? mode->refresh_rate : 60.0f;
            float paceHz = 0;
            if (settings_.fpsLimit > 0 && (!vsync_ || float(settings_.fpsLimit) < hz - 1.0f)) paceHz = float(settings_.fpsLimit);
            else if (!vsync_ && settings_.vsync != Settings::VSync::Off) paceHz = hz;
            if (paceHz > 0) {
                const Uint64 frame = Uint64(1e9 / paceHz), now = SDL_GetTicksNS();
                if (nextFrameNs_ > now && nextFrameNs_ - now <= frame) SDL_DelayPrecise(nextFrameNs_ - now);
                nextFrameNs_ = std::max(nextFrameNs_ + frame, SDL_GetTicksNS());
            } else {
                nextFrameNs_ = SDL_GetTicksNS();
            }
        }
        updateSound();
        if (settings_.perfOverlay) {
            const Uint64 now = SDL_GetTicksNS();
            if (perfFrameMs_.size() < 120) {
                perfFrameMs_.push_back(0);
                perfCpuMs_.push_back(0);
            }
            perfAt_ %= perfFrameMs_.size();
            perfFrameMs_[perfAt_] = float(double(now - perfLast_) / 1e6);
            perfCpuMs_[perfAt_] = float(double(perfCpuEnd_ - cpuStart) / 1e6);
            ++perfAt_;
            perfLast_ = now;
        } else {
            perfLast_ = SDL_GetTicksNS();
        }
        if (opt_.frames > 1) {
            const Uint64 now = SDL_GetTicksNS();
            frameMs.push_back(double(now - frameStart) / 1e6);
            frameStart = now;
        }
        if ((opt_.smoke || !opt_.menuScreenshot.empty()) && --framesLeft <= 0) {
            running = false;
            if (opt_.frames > 1 && cpuMs.size() > 10) {
                // Frame-time report (skip the first frames: texture uploads, warm-up).
                auto report = [](const char* what, std::vector<double> v) {
                    v.erase(v.begin(), v.begin() + 5);
                    std::sort(v.begin(), v.end());
                    double sum = 0;
                    for (double x : v) sum += x;
                    SDL_Log("%s: avg %.2f ms, p50 %.2f, p95 %.2f, max %.2f (%zu frames)", what, sum / double(v.size()),
                            v[v.size() / 2], v[v.size() * 95 / 100], v.back(), v.size());
                };
                report("CPU update+render", cpuMs);
                report("Frame (incl. present/vsync)", frameMs);
            }
        }

        if (session_) {
            const SessionResult r = session_->result();
            if (r == SessionResult::QuitApp) running = false;
            if (r == SessionResult::LoadGame) {
                const int slot = session_->loadSlot();
                const GameStart start = session_->start();
                session_.reset();
                persistSettings();  // the day / night cycle phase (Settings::cyclePhase)
                touch_->reset();
                std::string err;
                if (!loadSavedGame(slot, err)) {
                    SDL_Log("Load failed: %s", err.c_str());
                    if (!dataDir_.empty()) menu_->returnFromGame(start, false);
                    menu_->open(MenuPage::Load);
                    menu_->loadError = err;
                }
            }
            if (r == SessionResult::QuitToMenu) {
                const GameStart start = session_->start();
                const bool won = session_->endState() == game::GameEnd::Won;
                const bool ended = session_->endState() != game::GameEnd::None;
                const game::ScoreInput score = session_->scoreInput();
                session_.reset();
                persistSettings();  // the day / night cycle phase (Settings::cyclePhase)
                touch_->reset();
                // Skirmish: the result page follows an abort too (FUN_004738b0 ABORT_MP_GAME -> SCORE).
                if (!dataDir_.empty()) menu_->returnFromGame(start, won, ended || start.skirmish ? &score : nullptr);
            }
        }
    }
    persistSettings();
    return rc;
}

}  // namespace d2k::platform
