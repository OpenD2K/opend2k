#include "platform/settings.hpp"
#include "platform/user_data.hpp"

#include "render/art_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace d2k::platform {

std::string Settings::path() {
    const std::string p = prefPath();  // platform/user_data.hpp: the same folder in every build
    return p.empty() ? std::string() : p + "settings.ini";
}

// RecalcModelRate 0x45b610: GameSpeed thresholds 7/23/39/59/78/94 select 8/10/12/15/20/25 ticks
// per second (floats at 0x4c942c..); above 94 the rate is LimitedModelRate, which stays at its
// initial 60 (0x45a7a0) in single player (only network games overwrite it). The tick slots also
// cap the rate at the measured frame rate, i.e. 60 ticks/s needs >= 60 fps.
int Settings::gameTicksPerSecond(int gameSpeed) {
    if (gameSpeed <= 7) return 8;
    if (gameSpeed <= 23) return 10;
    if (gameSpeed <= 39) return 12;
    if (gameSpeed <= 59) return 15;
    if (gameSpeed <= 78) return 20;
    if (gameSpeed <= 94) return 25;
    return 60;
}

const std::vector<std::string>& Settings::resolutionChoices() {
    // The community launcher's list (Gruntmods Dune2000Config) plus the original 640x400.
    static const std::vector<std::string> k = {"auto",     "640x400",  "640x480",   "800x600",  "1024x768", "1280x720",
                                               "1280x1024", "1366x768", "1600x900", "1920x1080", "native"};
    return k;
}

const std::vector<std::string>& Settings::uiScaleChoices() {
    static const std::vector<std::string> k = {"auto", "100", "125", "150", "175", "200", "250", "300"};
    return k;
}

std::string Settings::uiScaleLabel(const std::string& v) { return v == "auto" ? std::string("Auto") : v + "%"; }

float Settings::uiScaleFor(const std::string& v, float autoK, int outW) {
    float k = autoK;
    if (v != "auto") {
        try {
            k = std::stof(v) / 100.0f;
        } catch (...) { k = autoK; }
    }
    // The 160-px panel takes at most ~40 % of the window.
    const float maxK = std::floor(float(outW) * 0.4f / 160.0f * 4.0f) / 4.0f;
    return std::clamp(std::min(k, maxK), 1.0f, 4.0f);
}

std::string Settings::resolutionLabel(const std::string& r) {
    if (r == "auto") return "Auto";
    if (r == "native") return "Native";
    return r;
}

void Settings::gameResolution(const std::string& r, int outW, int outH, int& gameW, int& gameH) {
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    int w = 0, h = 0;
    if (r == "native") {
        w = outW;
        h = outH;
    } else if (const auto x = r.find('x'); r != "auto" && x != std::string::npos) {
        try {
            w = std::stoi(r.substr(0, x));
            h = std::stoi(r.substr(x + 1));
        } catch (...) { w = h = 0; }
    }
    if (w <= 0 || h <= 0) {  // auto (or unparsable): the integer scale that fits 640x400
        const int k = std::max(1, std::min(outW / 640, outH / 400));
        w = outW / k;
        h = outH / k;
    }
    gameW = std::clamp(w, 640, 8192);
    gameH = std::clamp(h, 400, 8192);
}

Settings Settings::load() {
    Settings s;
    const std::string file = path();
    if (file.empty()) return s;
    std::ifstream in(file);
    // Portable build (platform/user_data.hpp): its settings-default.ini starts a new player.
    if (!in)
        if (const std::string def = portablePath("settings-default.ini"); !def.empty()) {
            in.open(def);
            if (in) SDL_Log("Portable mode: first start with %s", def.c_str());
        }
    if (!in) {
        // A new player (no settings.ini yet): the Enhanced preset (owner decision 2026-11-09), with
        // EDR output where the display has it.
        s.applyPreset(GraphicsPreset::Enhanced);
        s.hdrOutput = true;
        s.firstRunArtPackAsked = false;  // the welcome page once the game data is there
        return s;
    }
    std::string line;
    bool sawGraphics = false, legacyHq = false, sawShroud = false, sawLighting = false, sawQuality = false, sawCycleDefault = false;
    auto toF = [](const std::string& v, float def) {
        try { return std::clamp(std::stof(v), 0.0f, 1.0f); } catch (...) { return def; }
    };
    auto toI = [](const std::string& v, int def) {
        try { return std::stoi(v); } catch (...) { return def; }
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "dataDir") s.dataDir = expandPortablePath(v);
        else if (k == "graphics") { s.graphics = parseGraphics(v); sawGraphics = true; }
        else if (k == "graphicsPreset") s.graphicsPreset = graphicsPresetFrom(v);
        else if (k == "hq") legacyHq = v == "1";  // before the Original/Enhanced switch
        else if (k == "artPack") s.artPack = v.empty() ? "upscaled" : v;
        else if (k == "models3d") s.models3d = models3dFrom(v);
        else if (k == "modelPack") s.modelPack = v;
        else if (k == "artPackUpdateCheck") s.artPackUpdateCheck = v != "0";
        else if (k == "artPackManifestUrl") s.artPackManifestUrl = v;
        else if (k == "firstRunArtPackAsked") s.firstRunArtPackAsked = v != "0";
        else if (k == "appUpdateUrl") s.appUpdateUrl = v;
        else if (k == "terrain3d") s.terrain3d = terrain3dFrom(v);
        else if (k == "terrain2d") s.terrain2d = terrain2dFrom(v);
        else if (k == "classicView") s.classicView = classicViewFrom(v);
        else if (k == "shadows3d") s.shadows3d = v != "0" && v != "off";
        else if (k == "timeOfDay") s.timeOfDay = timeOfDayFrom(v);
        else if (k == "lighting") { s.lighting = lightingFrom(v); sawLighting = true; }
        else if (k == "cycleDefault") sawCycleDefault = true;
        else if (k == "cyclePhase") s.cyclePhase = std::atof(v.c_str());
        else if (k == "hdr") s.hdr = v == "on" || v == "1";
        else if (k == "hdrOutput") s.hdrOutput = v == "on" || v == "1";
        else if (k == "msaa") { const int m = toI(v, 4); s.msaa = m >= 4 ? 4 : m >= 2 ? 2 : 1; sawQuality = true; }
        else if (k == "aniso") { const int a = toI(v, 1); s.aniso = a >= 16 ? 16 : a >= 4 ? 4 : 1; }
        else if (k == "vsync") s.vsync = vsyncFrom(v);
        else if (k == "gpuDriver") s.gpuDriver = v == "vulkan" || v == "direct3d12" ? v : std::string("auto");
        else if (k == "fpsLimit") { const int f = toI(v, 0); s.fpsLimit = f >= 120 ? 120 : f >= 60 ? 60 : f >= 30 ? 30 : 0; }
        else if (k == "effects") s.effects = v == "classic" ? Effects::Classic : Effects::Modern;
        else if (k == "shroud") { s.shroud = v == "classic" ? Shroud::Classic : Shroud::Soft; sawShroud = true; }
        else if (k == "panelTransparency") s.panelTransparency = panelTransparencyFrom(v);
        else if (k == "healthBars") s.healthBars = v == "always" ? HealthBars::Always : v == "damaged" ? HealthBars::Damaged : HealthBars::Selected;
        else if (k == "fullscreen") s.fullscreen = v == "1";
        else if (k == "musicVolume") s.musicVolume = toF(v, s.musicVolume);
        else if (k == "soundVolume") s.soundVolume = toF(v, s.soundVolume);
        else if (k == "gameSpeed") { s.gameSpeed = std::clamp(toI(v, 75), kMinGameSpeed, kMaxGameSpeed); s.speedFromFile = true; }
        else if (k == "scrollRate") s.scrollRate = std::clamp(toI(v, 20), kMinScrollRate, kMaxScrollRate);
        else if (k == "controls") s.controls = v == "modern" ? Controls::Modern : Controls::Original;
        else if (k == "smoothMotion") s.smoothMotion = v != "0";
        else if (k == "perfOverlay") s.perfOverlay = v == "1";
        else if (k == "deviceProfile") s.deviceProfile = v;
        else if (k == "renderResolution") s.renderResolution = renderResolutionFrom(v);
        else if (k == "productionQueue") s.productionQueue = v == "original" ? ProductionQueue::Original : ProductionQueue::Extended;
        else if (k == "cheats") s.cheats = v != "0";
        else if (k == "rallyPoints") s.rallyPoints = v != "0";
        else if (k == "cutscenes") s.cutscenes = v != "0";
        else if (k == "introOnStartup") s.introOnStartup = v != "0";
        else if (k == "introSeen") s.introSeen = v != "0";
        else if (k == "ps1Image") s.ps1Image = expandPortablePath(v);
        else if (k == "language") s.language = v.empty() ? "en" : v;
        else if (k == "languageText") s.languageText = v;
        else if (k == "languageVoices") s.languageVoices = v;
        else if (k == "languageMovies") s.languageMovies = v;
        else if (k == "translationDir") s.translationDir = expandPortablePath(v);
        else if (k == "aiVoicesDir") s.aiVoicesDir = expandPortablePath(v);
        else if (k == "russianTextFixes") s.russianTextFixes = v != "0";
        else if (k == "resolution") s.resolution = v.empty() ? "auto" : v;
        else if (k == "uiScale") s.uiScale = v.empty() ? "auto" : v;
        else if (k == "touchLayout") s.touchLayout = touchLayoutFrom(v);
        else if (k == "phoneUiSize") s.phoneUiSize = phoneUiSizeFrom(v);
        else if (k == "quickBuildPins") s.quickBuildPins = v;
        else if (k == "quickBuildCounts") s.quickBuildCounts = v;
        else if (k == "skirmishMap") s.skirmishMap = v;
        else if (k == "skirmishHouse") s.skirmishHouse = std::clamp(toI(v, 0), 0, 2);
        else if (k == "skirmishAi") s.skirmishAi = std::clamp(toI(v, 1), 1, 7);
        else if (k == "skirmishTech") s.skirmishTech = std::clamp(toI(v, 7), 1, 9);
        else if (k == "skirmishCredits") s.skirmishCredits = std::clamp(toI(v, 5000), 0, 100000);
        else if (k == "skirmishDifficulty") s.skirmishDifficulty = std::clamp(toI(v, 1), 0, 2);
        else if (k == "skirmishUnits") s.skirmishUnits = std::clamp(toI(v, 10), 0, 10);
        else if (k == "skirmishWorms") s.skirmishWorms = std::clamp(toI(v, 1), 0, 3);
        else if (k == "skirmishColour") s.skirmishColour = std::clamp(toI(v, 0), 0, 7);
        else if (k == "skirmishCrates") s.skirmishCrates = v == "1";
        else if (k == "playerName") s.playerName = v;
    }
    if (!sawGraphics && legacyHq) s.graphics = Graphics::Enhanced;
    if (!sawShroud) s.shroud = s.effects == Effects::Modern ? Shroud::Soft : Shroud::Classic;
    // The terrain of the Enhanced / 3D presets became "Designed (faithful)" (owner decision
    // 2026-11-11). A file that names one of those presets and holds exactly its values from
    // before - the terrain "designed" - follows the preset to its new terrain; a file on Custom
    // (or with any other difference) keeps what it says, "designed" included.
    // The Enhanced / 3D presets light the battlefield with the Modern lighting (2026-11-13): a file
    // from before (no "lighting" key) that names one of them follows it.
    if (!sawLighting && (s.graphicsPreset == GraphicsPreset::Enhanced || s.graphicsPreset == GraphicsPreset::ThreeD))
        s.lighting = Lighting::Modern;
    // The Enhanced / 3D presets use the day / night cycle (owner 2026-11-13): a file from before (no
    // "cycleDefault" key) that names one of them and still has the old preset's Day follows it.
    if (!sawCycleDefault && s.timeOfDay == TimeOfDay::Day &&
        (s.graphicsPreset == GraphicsPreset::Enhanced || s.graphicsPreset == GraphicsPreset::ThreeD))
        s.timeOfDay = TimeOfDay::Cycle;
    // The Quality settings came 2026-11-13: a file from before that names the Enhanced / 3D preset
    // gets High (HDR rendering, 4x MSAA, 16x anisotropy) and the EDR output, like a new player.
    if (!sawQuality && (s.graphicsPreset == GraphicsPreset::Enhanced || s.graphicsPreset == GraphicsPreset::ThreeD)) {
        s.applyQuality(Quality::High);
        s.hdrOutput = true;
    }
    if (s.graphicsPreset == GraphicsPreset::Enhanced || s.graphicsPreset == GraphicsPreset::ThreeD) {
        Settings now = s;
        if (now.terrain2d == Terrain2D::Designed) now.terrain2d = Terrain2D::Faithful;
        if (s.graphicsPreset == GraphicsPreset::ThreeD && now.terrain3d == Terrain3D::Designed) now.terrain3d = Terrain3D::Faithful;
        if (!s.matchesPreset(s.graphicsPreset) && now.matchesPreset(s.graphicsPreset)) {
            s.terrain2d = now.terrain2d;
            s.terrain3d = now.terrain3d;
        }
    }
    return s;
}

namespace {
// The art of the Enhanced / 3D presets: a redesign pack the player chose stays, anything else
// (the built-in upscale, an AI-upscale pack) becomes the original sprites.
bool presetArt(const std::string& artPack) { return artPack == "original" || render::ArtPack::isRedesign(artPack); }
// The redesign pack the Enhanced / 3D presets use when one is installed: the 3D pack (opend2k-redesign:
// models + DATA) if it is a redesign, else the first redesign pack; "" = none (original sprites).
std::string installedRedesign() {
    if (const std::string p = render::ArtPack::threeDPack("opend2k-redesign"); !p.empty() && render::ArtPack::isRedesign(p)) return p;
    for (const std::string& p : render::ArtPack::available())
        if (render::ArtPack::isRedesign(p)) return p;
    return {};
}
}  // namespace

bool Settings::modelPackInstalled() { return !render::ArtPack::threeDPack().empty(); }

bool Settings::matchesPreset(GraphicsPreset p) const {
    if (p == GraphicsPreset::Original) return graphics == Graphics::Original;
    if (p != GraphicsPreset::Enhanced && p != GraphicsPreset::ThreeD) return false;
    const bool common = presetArt(artPack) && effects == Effects::Modern && shroud == Shroud::Soft &&
                        healthBars == HealthBars::Selected && panelTransparency == PanelTransparency::Off &&
                        classicView == ClassicView::Hold && timeOfDay == TimeOfDay::Cycle && lighting == Lighting::Modern;
    if (p == GraphicsPreset::Enhanced) return common && graphics == Graphics::Enhanced && terrain2d == Terrain2D::Faithful;
    return common && graphics == Graphics::ThreeD && terrain3d == Terrain3D::Faithful && shadows3d && models3d == Models3D::New;
}

Settings::GraphicsPreset Settings::activePreset() const {
    if (graphicsPreset == GraphicsPreset::Custom) return GraphicsPreset::Custom;
    if (graphicsPreset != GraphicsPreset::Auto && matchesPreset(graphicsPreset)) return graphicsPreset;
    for (GraphicsPreset p : {GraphicsPreset::Original, GraphicsPreset::Enhanced, GraphicsPreset::ThreeD})
        if (matchesPreset(p)) return p;
    return GraphicsPreset::Custom;
}

void Settings::applyPreset(GraphicsPreset p) {
    if (p == GraphicsPreset::Original) {
        graphics = Graphics::Original;
    } else if (p == GraphicsPreset::Enhanced || p == GraphicsPreset::ThreeD) {
        graphics = p == GraphicsPreset::ThreeD ? Graphics::ThreeD : Graphics::Enhanced;
        // A redesign pack the player chose stays; otherwise the installed redesign (the presets' look, as the
        // Graphics / welcome pages describe it), else the original sprites.
        if (artPack == "original" || !presetArt(artPack)) {
            const std::string r = installedRedesign();
            artPack = r.empty() ? "original" : r;
        }
        terrain2d = Terrain2D::Faithful;  // also the 3D mode's fallback without SDL_gpu
        effects = Effects::Modern;
        shroud = Shroud::Soft;
        healthBars = HealthBars::Selected;
        panelTransparency = PanelTransparency::Off;
        classicView = ClassicView::Hold;
        timeOfDay = TimeOfDay::Cycle;  // the day / night cycle (owner 2026-11-13)
        lighting = Lighting::Modern;
        applyQuality(Quality::High);
        if (p == GraphicsPreset::ThreeD) {
            terrain3d = Terrain3D::Faithful;
            shadows3d = true;
            models3d = Models3D::New;
        }
    } else {
        return;
    }
    graphicsPreset = p;
}

Settings::PresetValues Settings::presetValues() const {
    return {graphics, artPack, terrain2d, terrain3d, shadows3d, effects, shroud, healthBars, panelTransparency, classicView,
            timeOfDay, lighting};
}

void Settings::applyQuality(Quality q) {
    switch (q) {
    case Quality::Low: hdr = false; msaa = 1; aniso = 1; break;
    case Quality::Medium: hdr = true; msaa = 2; aniso = 4; break;
    case Quality::High: hdr = true; msaa = 4; aniso = 16; break;
    case Quality::Custom: break;
    }
}

Settings::Quality Settings::quality() const {
    for (Quality q : {Quality::Low, Quality::Medium, Quality::High}) {
        Settings t = *this;
        t.applyQuality(q);
        if (t.hdr == hdr && t.msaa == msaa && t.aniso == aniso) return q;
    }
    return Quality::Custom;
}

std::string Settings::previewCacheDir() {
    const std::string p = prefPath();
    return p.empty() ? std::string() : p + "previews";
}

bool Settings::fullPixelDensity() const {
    if (renderResolution != RenderResolution::Auto) return renderResolution == RenderResolution::Full;
    return !(kTouchPlatform && deviceTier() == DeviceTier::Low);
}

void Settings::applyLiteProfile() {
    if (graphics == Graphics::ThreeD) graphics = Graphics::Enhanced;
    effects = Effects::Classic;
    shroud = Shroud::Classic;
    if (artPack != "original") artPack = "upscaled";
    if (terrain2dBaked()) terrain2d = Terrain2D::Upscaled;  // the baked chunks need ~200 MB
    panelTransparency = PanelTransparency::Off;
    lighting = Lighting::Classic;  // no bloom (render/lighting.hpp: the lite profile has no lamps either)
    applyQuality(Quality::Low);
    hdrOutput = false;
}

bool Settings::save() const {
    const std::string file = path();
    if (file.empty()) return false;
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;
    out << "# d2k-mac settings\n"
        << "dataDir=" << contractPortablePath(dataDir) << "\n"
        << "graphics=" << graphicsName(graphics) << "\n"
        << "graphicsPreset=" << graphicsPresetName(activePreset()) << "\n"
        << "artPack=" << artPack << "\n"
        << "models3d=" << models3dName(models3d) << "\n"
        << "modelPack=" << modelPack << "\n"
        << "artPackUpdateCheck=" << (artPackUpdateCheck ? 1 : 0) << "\n"
        << "artPackManifestUrl=" << artPackManifestUrl << "\n"
        << "firstRunArtPackAsked=" << (firstRunArtPackAsked ? 1 : 0) << "\n"
        << "appUpdateUrl=" << appUpdateUrl << "\n"
        << "terrain3d=" << terrain3dName(terrain3d) << "\n"
        << "terrain2d=" << terrain2dName(terrain2d) << "\n"
        << "classicView=" << classicViewName(classicView) << "\n"
        << "shadows3d=" << (shadows3d ? "on" : "off") << "\n"
        << "timeOfDay=" << timeOfDayName(timeOfDay) << "\n"
        << "cycleDefault=1\n"
        << "cyclePhase=" << cyclePhase << "\n"
        << "lighting=" << lightingName(lighting) << "\n"
        << "hdr=" << (hdr ? "on" : "off") << "\n"
        << "hdrOutput=" << (hdrOutput ? "on" : "off") << "\n"
        << "msaa=" << msaa << "\n"
        << "aniso=" << aniso << "\n"
        << "vsync=" << vsyncName(vsync) << "\n"
        << "gpuDriver=" << gpuDriver << "\n"
        << "fpsLimit=" << fpsLimit << "\n"
        << "effects=" << effectsName(effects) << "\n"
        << "shroud=" << shroudName(shroud) << "\n"
        << "healthBars=" << healthBarsName(healthBars) << "\n"
        << "panelTransparency=" << panelTransparencyName(panelTransparency) << "\n"
        << "fullscreen=" << (fullscreen ? 1 : 0) << "\n"
        << "musicVolume=" << musicVolume << "\n"
        << "soundVolume=" << soundVolume << "\n"
        << "gameSpeed=" << gameSpeed << "\n"
        << "scrollRate=" << scrollRate << "\n"
        << "controls=" << (controls == Controls::Modern ? "modern" : "original") << "\n"
        << "smoothMotion=" << (smoothMotion ? 1 : 0) << "\n"
        << "perfOverlay=" << (perfOverlay ? 1 : 0) << "\n"
        << "deviceProfile=" << deviceProfile << "\n"
        << "renderResolution=" << renderResolutionName(renderResolution) << "\n"
        << "productionQueue=" << (extendedQueue() ? "extended" : "original") << "\n"
        << "cheats=" << (cheats ? 1 : 0) << "\n"
        << "rallyPoints=" << (rallyPoints ? 1 : 0) << "\n"
        << "cutscenes=" << (cutscenes ? 1 : 0) << "\n"
        << "introOnStartup=" << (introOnStartup ? 1 : 0) << "\n"
        << "introSeen=" << (introSeen ? 1 : 0) << "\n"
        << "ps1Image=" << contractPortablePath(ps1Image) << "\n"
        << "language=" << language << "\n"
        << "languageText=" << languageText << "\n"
        << "languageVoices=" << languageVoices << "\n"
        << "languageMovies=" << languageMovies << "\n"
        << "translationDir=" << contractPortablePath(translationDir) << "\n"
        << "aiVoicesDir=" << contractPortablePath(aiVoicesDir) << "\n"
        << "russianTextFixes=" << (russianTextFixes ? 1 : 0) << "\n"
        << "resolution=" << resolution << "\n"
        << "uiScale=" << uiScale << "\n"
        << "touchLayout=" << touchLayoutName(touchLayout) << "\n"
        << "phoneUiSize=" << phoneUiSizeName(phoneUiSize) << "\n"
        << "quickBuildPins=" << quickBuildPins << "\n"
        << "quickBuildCounts=" << quickBuildCounts << "\n"
        << "skirmishMap=" << skirmishMap << "\n"
        << "skirmishHouse=" << skirmishHouse << "\n"
        << "skirmishAi=" << skirmishAi << "\n"
        << "skirmishTech=" << skirmishTech << "\n"
        << "skirmishCredits=" << skirmishCredits << "\n"
        << "skirmishDifficulty=" << skirmishDifficulty << "\n"
        << "skirmishUnits=" << skirmishUnits << "\n"
        << "skirmishWorms=" << skirmishWorms << "\n"
        << "skirmishColour=" << skirmishColour << "\n"
        << "skirmishCrates=" << (skirmishCrates ? 1 : 0) << "\n"
        << "playerName=" << playerName << "\n";
    return bool(out);
}

}  // namespace d2k::platform
