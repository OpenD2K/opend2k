#include "platform/options.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace d2k::platform {

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        try {
            if (a == "--data") o.dataDir = next();
            else if (a == "--install-from") o.installFrom = next();
            else if (a == "--install-only") o.installOnly = true;
            else if (a == "--game-data-root") o.gameDataRoot = next();
            else if (a == "--artpack-manifest") o.artpackManifest = next();
            else if (a == "--artpack-dir") o.artpackDir = next();
            else if (a == "--artpack-check") o.artpackCheck = true;
            else if (a == "--artpack-install") o.artpackInstall = true;
            else if (a == "--update-url") o.updateUrl = next();
            else if (a == "--update-check") o.updateCheck = true;
            else if (a == "--http-get") { o.httpGetUrl = next(); o.httpGetOut = next(); }
            else if (a == "--map") o.map = next();
            else if (a == "--play-as") o.playAs = std::stoi(next());  // side 0..7 (not original, docs/play-as.md)
            else if (a == "--tileset") o.tileset = next();
            else if (a == "--zoom") o.zoom = std::stof(next());
            else if (a == "--camera-tilt") o.cameraTilt = std::stof(next());  // 3D mode, degrees
            else if (a == "--camera-yaw") o.cameraYaw = std::stof(next());
            else if (a == "--at") { const int x = std::stoi(next()); const int y = std::stoi(next()); o.at = {x, y}; }
            else if (a == "--screenshot") { o.screenshot = next(); o.smoke = true; }
            else if (a == "--record") { o.recordDir = next(); o.smoke = true; }
            else if (a == "--record-fps") { o.recordFps = std::max(1, std::stoi(next())); o.fixedStep = true; }
            else if (a == "--smoke") o.smoke = true;
            else if (a == "--frames") o.frames = std::max(1, std::stoi(next()));
            else if (a == "--simtest") o.simtest = true;
            else if (a == "--ticks") o.ticks = std::stoi(next());
            else if (a == "--battle") o.battle = true;
            else if (a == "--vfx-demo") o.vfxDemo = true;
            else if (a == "--select") o.selectAll = true;
            else if (a == "--mute") o.mute = true;
            else if (a == "--movie") o.movie = next();
            else if (a == "--movie-house") o.movieHouse = std::stoi(next());
            else if (a == "--ps1") o.ps1Image = next();
            else if (a == "--language") o.language = next();
            else if (a == "--text-language") o.textLanguage = next();
            else if (a == "--voices-language") o.voicesLanguage = next();
            else if (a == "--movies-language") o.moviesLanguage = next();
            else if (a == "--translation") o.translationDir = next();
            else if (a == "--ai-voices") o.aiVoicesDir = next();
            else if (a == "--ru-text") o.ruText = next();
            else if (a == "--sound-test") o.soundTest = true;
            else if (a == "--hq") o.graphics = "enhanced";
            else if (a == "--models3d") o.models3d = next();
            else if (a == "--model-pack") o.modelPack = next();
            else if (a == "--terrain3d") o.terrain3d = next();
            else if (a == "--terrain2d") o.terrain2d = next();
            else if (a == "--shadows3d") o.shadows3d = next();
            else if (a == "--time-of-day") o.timeOfDay = next();
            else if (a == "--skirmish-ai") o.skirmishAi = std::atoi(next().c_str());
            else if (a == "--lighting") o.lighting = next();
            else if (a == "--quality") o.quality = next();
            else if (a == "--hdr") o.hdr = next();
            else if (a == "--hdr-output") o.hdrOutput = next();
            else if (a == "--msaa") o.msaa = std::stoi(next());
            else if (a == "--aniso") o.aniso = std::stoi(next());
            else if (a == "--vsync") o.vsync = next();
            else if (a == "--fps-limit") o.fpsLimit = std::stoi(next());
            else if (a == "--graphics") {
                o.graphics = next();
                if (o.graphics != "original" && o.graphics != "enhanced" && o.graphics != "3d") {
                    SDL_Log("--graphics wants original, enhanced or 3d, not '%s'", o.graphics.c_str());
                    o.graphics.clear();
                }
            }
            else if (a == "--art-pack") o.artPack = next();
            else if (a == "--effects") {
                o.effects = next();
                if (o.effects != "classic" && o.effects != "modern") {
                    SDL_Log("--effects wants classic or modern, not '%s'", o.effects.c_str());
                    o.effects.clear();
                }
            }
            else if (a == "--panel-transparency") o.panelTransparency = next();
            else if (a == "--shroud") {
                o.shroud = next();
                if (o.shroud != "classic" && o.shroud != "soft") {
                    SDL_Log("--shroud wants classic or soft, not '%s'", o.shroud.c_str());
                    o.shroud.clear();
                }
            }
            else if (a == "--reveal") o.reveal = true;
            else if (a == "--classic-peek") o.classicPeek = true;
            else if (a == "--perf-overlay") o.perfOverlay = true;
            else if (a == "--kill-building") { const int x = std::stoi(next()); const int y = std::stoi(next()); o.killBuilding = {x, y}; }
            else if (a == "--classic-ui") o.graphics = "original";
            else if (a == "--no-music") o.noMusic = true;
            else if (a == "--skirmish") o.skirmish = true;
            else if (a == "--menu-screenshot") o.menuScreenshot = next();
            else if (a == "--menu-page") o.menuPage = next();
            else if (a == "--render-previews") o.renderPreviews = next();
            else if (a == "--preview-pack") o.previewPack = next();
            else if (a == "--cursor-at") { const float x = std::stof(next()); const float y = std::stof(next()); o.cursorAt = {x, y}; }
            else if (a == "--cursor-mode") o.cursorMode = std::stoi(next());
            else if (a == "--sidebar-page") o.sidebarPage = next();
            else if (a == "--load") o.loadSlot = std::stoi(next());
            else if (a == "--save") o.saveSlot = std::stoi(next());
            else if (a == "--saves-dir") o.savesDir = next();
            else if (a == "--window") {
                const std::string v = next();
                const auto x = v.find('x');
                if (x == std::string::npos) throw std::invalid_argument(v);
                o.windowW = std::stoi(v.substr(0, x));
                o.windowH = std::stoi(v.substr(x + 1));
            }
            else if (a == "--resolution") o.resolution = next();
            else if (a == "--ui-scale") o.uiScale = next();
            else if (a == "--phone-layout") o.touchLayout = "phone";
            else if (a == "--phone-menus") o.phoneMenus = std::stof(next());
            else if (a == "--tablet-layout") o.touchLayout = "tablet";
            else if (a == "--ui-size") o.phoneUiSize = next();
            else if (a == "--safe-insets") {
                const std::string v = next();
                std::array<float, 4> in{};
                if (std::sscanf(v.c_str(), "%f,%f,%f,%f", &in[0], &in[1], &in[2], &in[3]) != 4) throw std::invalid_argument(v);
                o.safeInsets = in;
            }
            else if (a == "--render-resolution") o.renderResolution = next();
            else if (a.rfind("-psn_", 0) == 0) { /* macOS Finder process serial number */ }
            else SDL_Log("Ignoring unknown option %s", a.c_str());
        } catch (const std::exception&) {
            SDL_Log("Bad value for option %s", a.c_str());
        }
    }
    // Headless and one-frame runs keep the old behaviour of defaulting to a skirmish map.
    if (o.map.empty() && o.loadSlot < 0 && (o.simtest || (o.smoke && o.menuScreenshot.empty()))) o.map = Options::kDefaultMap;
    return o;
}

}  // namespace d2k::platform
