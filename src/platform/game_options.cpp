#include "platform/game_options.hpp"

#include "platform/language.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "render/art_pack.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace d2k::platform {

namespace {
bool gRenderFullActive = false;
}  // namespace

void setRenderFullActive(bool full) { gRenderFullActive = full; }
bool renderFullActive() { return gRenderFullActive; }

namespace {
bool gHdrRequested = false, gHdrActive = false;
}  // namespace
void setHdrOutputStart(bool requested, bool active) {
    gHdrRequested = requested;
    gHdrActive = active;
}

namespace {
using render::tr;
// Choice values in the current UI language (render/ui_text.hpp).
std::vector<std::string> trList(std::initializer_list<const char*> v) {
    std::vector<std::string> out;
    for (const char* s : v) out.emplace_back(tr(s));
    return out;
}
}  // namespace

// ---- single controls shared by the in-game rows and the Graphics page ----------------------

namespace {

void modeChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    // Original: the 1998 look; Enhanced: the modern 2D presentation; 3D: the Enhanced UI over a
    // 2.5D battlefield (docs/3d-mode.md). Main menu only: a running game keeps its mode.
    // 3D only with an OpenD2K 3D art pack installed (Settings::modelPackInstalled); otherwise the
    // choice offers Original / Enhanced (the "Mode" header names what 3D needs).
    const bool can3d = Settings::modelPackInstalled();
    int gfx = s.threeD() && can3d ? 2 : s.enhanced() ? 1 : 0;
    if (ui.choice(r, tr("Graphics mode"), gfx, can3d ? trList({"Original", "Enhanced", "3D"}) : trList({"Original", "Enhanced"})))
        s.graphics = gfx == 2 ? Settings::Graphics::ThreeD : gfx == 1 ? Settings::Graphics::Enhanced : Settings::Graphics::Original;
}

void models3dChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    // What draws units / buildings (docs/3d-models.md): the 3D art pack's models or the PS1 disc's; never
    // the original sprites.
    int m = static_cast<int>(s.models3d);
    if (ui.choice(r, tr("3D models"), m, trList({"New (art pack)", s.ps1Image.empty() ? "PS1 (no disc)" : "PS1 (disc)"})))
        s.models3d = static_cast<Settings::Models3D>(m);
}

// One row per mode, so it is never a no-op: Original graphics - the game resolution (the
// community high-res patch's GameWidth/Height); Enhanced / 3D - the in-game UI scale.
void resolutionOrScale(Ui& ui, Settings& s, const SDL_FRect& r) {
    if (!s.enhanced()) {
        const auto& choices = Settings::resolutionChoices();
        std::vector<std::string> labels;
        int idx = 0;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            labels.push_back(tr(Settings::resolutionLabel(choices[i])));
            if (choices[i] == s.resolution) idx = int(i);
        }
        if (ui.choice(r, tr("Resolution"), idx, labels)) s.resolution = choices[std::size_t(idx)];
    } else {
        const auto& choices = Settings::uiScaleChoices();
        std::vector<std::string> labels;
        int idx = 0;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            labels.push_back(tr(Settings::uiScaleLabel(choices[i])));
            if (choices[i] == s.uiScale) idx = int(i);
        }
        if (ui.choice(r, tr("UI scale"), idx, labels)) s.uiScale = choices[std::size_t(idx)];
    }
}

// The render resolution (Retina pixels or points; read when the window is created).
void renderChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int rr = static_cast<int>(s.renderResolution);
    const bool pending = s.fullPixelDensity() != renderFullActive();
    if (ui.choice(r, tr(pending ? "Restart to apply" : "Render"), rr, trList({"Auto", "Full", "Half"})))
        s.renderResolution = static_cast<Settings::RenderResolution>(rr);
}

// Art: original pixels, the built-in upscale or an installed art pack (docs/enhanced-graphics.md).
void artChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    // Refreshed when a pack is installed while running (platform/artpack_download.hpp).
    static std::vector<std::string> packs;
    static int packsGen = -1;
    if (packsGen != render::ArtPack::generation()) {
        packs = render::ArtPack::available();
        packsGen = render::ArtPack::generation();
    }
    std::vector<std::string> values = {"original", "upscaled"},
                             labels = {tr(redesignArtPackInstalled() ? "Original" : "Original (no art pack)"), tr("Upscaled")};
    for (const auto& p : packs) {
        values.push_back(p);
        labels.push_back(p);
    }
    int art = 1;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (values[i] == s.artPack) art = int(i);
    if (art == 1 && s.artPack != "upscaled") {  // a pack that is not installed (any more)
        values.push_back(s.artPack);
        labels.push_back(s.artPack + " " + tr("(missing)"));
        art = int(values.size()) - 1;
    }
    if (ui.choice(r, tr("Art"), art, labels)) s.artPack = values[std::size_t(art)];
}

void effectsChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int fx = s.effects == Settings::Effects::Modern ? 1 : 0;
    if (ui.choice(r, tr("Effects"), fx, trList({"Classic", "Modern"})))
        s.effects = fx == 1 ? Settings::Effects::Modern : Settings::Effects::Classic;
}

void healthChoice(Ui& ui, Settings& s, const SDL_FRect& r, const char* label) {
    int hb = static_cast<int>(s.healthBars);
    if (ui.choice(r, tr(label), hb, trList({"Selected", "Damaged", "Always"}))) s.healthBars = static_cast<Settings::HealthBars>(hb);
}

void shroudChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int sh = s.shroud == Settings::Shroud::Soft ? 1 : 0;
    if (ui.choice(r, tr("Shroud"), sh, trList({"Classic", "Soft"})))
        s.shroud = sh == 1 ? Settings::Shroud::Soft : Settings::Shroud::Classic;
}

void panelChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int pt = static_cast<int>(s.panelTransparency);
    if (ui.choice(r, tr("Panel transparency"), pt, trList({"Off", "Light", "Medium"})))
        s.panelTransparency = static_cast<Settings::PanelTransparency>(pt);
}

void terrain2dChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int tt = static_cast<int>(s.terrain2d);
    if (ui.choice(r, tr("Terrain"), tt, trList({"Original tiles", "Upscaled", "Designed", "Faithful"})))
        s.terrain2d = static_cast<Settings::Terrain2D>(tt);
}

void terrain3dChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int tt = static_cast<int>(s.terrain3d);
    if (ui.choice(r, tr("3D terrain"), tt, trList({"Original tiles", "Designed", "Faithful"})))
        s.terrain3d = static_cast<Settings::Terrain3D>(tt);
}

// Time of day and the lighting style (docs/enhanced-graphics.md "Lighting").
void timeOfDayChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int t = static_cast<int>(s.timeOfDay);
    if (ui.choice(r, tr("Time of day"), t, trList({"Day", "Dawn", "Dusk", "Night", "Cycle"})))
        s.timeOfDay = static_cast<Settings::TimeOfDay>(t);
}
void lightingChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int l = s.lighting == Settings::Lighting::Modern ? 1 : 0;
    if (ui.choice(r, tr("Lighting"), l, trList({"Classic", "Modern"})))
        s.lighting = l == 1 ? Settings::Lighting::Modern : Settings::Lighting::Classic;
}

// Quality (docs/enhanced-graphics.md "Quality"): the Low / Medium / High shortcut, HDR rendering,
// MSAA and anisotropic filtering (3D), the HDR / EDR display output.
void qualityChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int q = static_cast<int>(s.quality());
    if (ui.choice(r, tr("Quality"), q, trList({"Low", "Medium", "High", "Custom"})) && q < 3)
        s.applyQuality(static_cast<Settings::Quality>(q));
}
void msaaChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int m = s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0;
    if (ui.choice(r, tr("Anti-aliasing (3D)"), m, trList({"Off", "2x", "4x"}))) s.msaa = m == 2 ? 4 : m == 1 ? 2 : 1;
}
void anisoChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int a = s.aniso >= 16 ? 2 : s.aniso >= 4 ? 1 : 0;
    if (ui.choice(r, tr("Texture filtering (3D)"), a, trList({"Off", "4x", "16x"}))) s.aniso = a == 2 ? 16 : a == 1 ? 4 : 1;
}
void hdrOutputChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int h = s.hdrOutput ? 1 : 0;
    const char* label = s.hdrOutput != gHdrRequested ? "Restart to apply" : "HDR display";
    if (ui.choice(r, tr(label), h, trList({"Off", gHdrActive ? "On (active)" : "On"}))) s.hdrOutput = h == 1;
}
// Display: VSync On / Off / Adaptive and the frame limit.
void vsyncChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int v = static_cast<int>(s.vsync);
    if (ui.choice(r, tr("VSync"), v, trList({"On", "Off", "Adaptive"}))) s.vsync = static_cast<Settings::VSync>(v);
}
// Windows: Vulkan / Direct3D 12 for the next start (Settings::gpuDriver).
std::string gActiveGpuDriver;  // setActiveGpuDriver
void gpuDriverChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    static const char* const kValues[] = {"auto", "vulkan", "direct3d12"};
    int g = 0;
    for (int i = 0; i < 3; ++i)
        if (s.gpuDriver == kValues[i]) g = i;
    // Applied when the game starts: the label says so until the chosen one is running (Auto: whatever runs).
    const bool pending = g > 0 && gActiveGpuDriver != kValues[g];
    const std::string active = gActiveGpuDriver == "vulkan" ? "Vulkan" : gActiveGpuDriver == "direct3d12" ? "Direct3D 12" : "";
    const std::vector<std::string> labels = {active.empty() ? std::string(tr("Auto")) : std::string(tr("Auto")) + " (" + active + ")",
                                             "Vulkan", "Direct3D 12"};
    if (ui.choice(r, tr(pending ? "Restart to apply" : "Graphics API"), g, labels)) s.gpuDriver = kValues[g];
}
void fpsLimitChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    static const int kValues[] = {0, 30, 60, 120};
    int f = 0;
    for (int i = 0; i < 4; ++i)
        if (kValues[i] == s.fpsLimit) f = i;
    if (ui.choice(r, tr("Frame limit"), f, trList({"Unlimited", "30", "60", "120"}))) s.fpsLimit = kValues[f];
}

void classicViewChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    // Tab (touch: the "1998" button) shows the battlefield in the original 1998 graphics.
    int cv = static_cast<int>(s.classicView);
    if (ui.choice(r, tr(kTouchPlatform ? "Classic view (1998 button)" : "Classic view key (Tab)"), cv, trList({"Off", "Hold", "Toggle"})))
        s.classicView = static_cast<Settings::ClassicView>(cv);
}

// The touch layout (Auto / Phone / Tablet; docs/ios.md "Phone layout") and the phone UI size.
void touchLayoutChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int tl = static_cast<int>(s.touchLayout);
    if (ui.choice(r, tr("Touch UI"), tl, trList({"Auto", "Phone", "Tablet"}))) s.touchLayout = static_cast<Settings::TouchLayout>(tl);
}
void phoneUiSizeChoice(Ui& ui, Settings& s, const SDL_FRect& r) {
    int z = static_cast<int>(s.phoneUiSize);
    if (ui.choice(r, tr("HUD size"), z, trList({"Tiny", "Small", "Normal"}))) s.phoneUiSize = static_cast<Settings::PhoneUiSize>(z);
}

// Left / right halves of a row (8 px apart).
SDL_FRect leftHalf(const SDL_FRect& r) { return {r.x, r.y, std::floor(r.w / 2) - 4, r.h}; }
SDL_FRect rightHalf(const SDL_FRect& r) {
    const float half = std::floor(r.w / 2);
    return {r.x + half + 4, r.y, r.w - half - 4, r.h};
}

}  // namespace

int gameplayOptionRows(Ui& ui, Settings& s, const std::function<SDL_FRect(int)>& row) {
    // GameSpeed 0..100 like dune2000.ini; the label shows the resulting model tick rate.
    ui.slider(row(0), render::trf("Game speed (%d ticks/s)", s.ticksPerSecond()), s.gameSpeed, Settings::kMinGameSpeed,
              Settings::kMaxGameSpeed, std::to_string(s.gameSpeed));
    // ScrollRate 4..40 pixels per frame (default 20).
    ui.slider(row(1), tr("Scroll speed"), s.scrollRate, Settings::kMinScrollRate, Settings::kMaxScrollRate, std::to_string(s.scrollRate));
    int controls = s.controls == Settings::Controls::Modern ? 1 : 0;
    if (ui.choice(row(2), tr("Mouse controls"), controls, trList({"Original", "Modern"})))
        s.controls = controls == 1 ? Settings::Controls::Modern : Settings::Controls::Original;
    // Smooth motion (render-only interpolation) | the frame-time overlay (App::drawPerfOverlay).
    ui.toggle(leftHalf(row(3)), tr("Smooth unit motion"), s.smoothMotion);
    ui.toggle(rightHalf(row(3)), tr("Frame time"), s.perfOverlay);
    // Not original: counted / infinite unit queues (re/notes/economy.md §10).
    int pq = s.extendedQueue() ? 1 : 0;
    if (ui.choice(row(4), tr("Production queue"), pq, trList({"Original", "Extended"})))
        s.productionQueue = pq == 1 ? Settings::ProductionQueue::Extended : Settings::ProductionQueue::Original;
    // Cheats | factory rally points (not original; game/rally.cpp).
    ui.toggle(leftHalf(row(5)), tr("Cheats"), s.cheats);
    ui.toggle(rightHalf(row(5)), tr("Factory rally points"), s.rallyPoints);
    return kGameplayOptionRows;
}

int inGameOptionRows(Ui& ui, Settings& s, const std::function<SDL_FRect(int)>& row) {
    const Settings::PresetValues before = s.presetValues();
    resolutionOrScale(ui, s, row(0));
    ui.slider(row(1), render::trf("Game speed (%d ticks/s)", s.ticksPerSecond()), s.gameSpeed, Settings::kMinGameSpeed,
              Settings::kMaxGameSpeed, std::to_string(s.gameSpeed));
    ui.slider(row(2), tr("Scroll speed"), s.scrollRate, Settings::kMinScrollRate, Settings::kMaxScrollRate, std::to_string(s.scrollRate));
    int controls = s.controls == Settings::Controls::Modern ? 1 : 0;
    if (ui.choice(row(3), tr("Mouse controls"), controls, trList({"Original", "Modern"})))
        s.controls = controls == 1 ? Settings::Controls::Modern : Settings::Controls::Original;
    ui.toggle(leftHalf(row(4)), tr("Smooth unit motion"), s.smoothMotion);
    ui.toggle(rightHalf(row(4)), tr("Frame time"), s.perfOverlay);
    int pq = s.extendedQueue() ? 1 : 0;
    if (ui.choice(row(5), tr("Production queue"), pq, trList({"Original", "Extended"})))
        s.productionQueue = pq == 1 ? Settings::ProductionQueue::Extended : Settings::ProductionQueue::Original;
    ui.toggle(leftHalf(row(6)), tr("Cheats"), s.cheats);
    renderChoice(ui, s, rightHalf(row(6)));
    // Enhanced presentation (docs/enhanced-graphics.md): art | effects, health bars | shroud.
    artChoice(ui, s, leftHalf(row(7)));
    effectsChoice(ui, s, rightHalf(row(7)));
    healthChoice(ui, s, leftHalf(row(8)), "Health");
    shroudChoice(ui, s, rightHalf(row(8)));
    // Factory rally points; 3D, same row: the sun shadow map.
    if (s.threeD()) {
        ui.toggle(leftHalf(row(9)), tr("Factory rally points"), s.rallyPoints);
        ui.toggle(rightHalf(row(9)), tr("Sun shadows"), s.shadows3d);
    } else {
        ui.toggle(row(9), tr("Factory rally points"), s.rallyPoints);
    }
    int n = kInGameOptionRows - 4;  // rows 0..9
    // Enhanced / 3D (not original): panel transparency | terrain (2D: Original tiles / Upscaled /
    // Designed / Faithful; 3D: the original tiles on the heightfield or the designed materials,
    // "Designed" (terrain v2) or "Faithful" (v3, laid out after the original tiles; short names:
    // the in-game row is half as wide), then the classic view key.
    if (s.enhanced()) {
        panelChoice(ui, s, leftHalf(row(10)));
        if (s.threeD()) terrain3dChoice(ui, s, rightHalf(row(10)));
        else terrain2dChoice(ui, s, rightHalf(row(10)));
        timeOfDayChoice(ui, s, leftHalf(row(11)));
        lightingChoice(ui, s, rightHalf(row(11)));
        classicViewChoice(ui, s, row(12));
        touchLayoutChoice(ui, s, leftHalf(row(13)));
        phoneUiSizeChoice(ui, s, rightHalf(row(13)));
        n = kInGameOptionRows;
    }
    s.noteGraphicsEdit(before);
    return n;
}

namespace {

// A group header of the Graphics page: the name, a note on the right (dim) and a rule below.
float header(Ui& ui, const OptionListLayout& L, float y, const char* label, const char* note = nullptr) {
    const SDL_FRect r = L.rect(y, L.headerH);
    const float ts = ui.textScale;
    const float ty = r.y + r.h - ui.textHeight(ts) - 3;
    ui.text(tr(label), r.x + 4, ty, theme::kAccent, ts);
    if (note) ui.text(tr(note), r.x + r.w - 4, ty, theme::kDim, ts, Align::Right);
    ui.fill(SDL_FRect{r.x, r.y + r.h - 1, r.w, 1}, Colour{150, 102, 52, 140});
    return L.headerH + L.gap;
}

}  // namespace

namespace {
int gRedesignOverride = -1;
}

void overrideRedesignArtPackInstalled(int v) { gRedesignOverride = v; }

bool redesignArtPackInstalled() {
    if (gRedesignOverride >= 0) return gRedesignOverride > 0;
    static bool any = false;
    static int gen = -1;
    if (gen != render::ArtPack::generation()) {
        gen = render::ArtPack::generation();
        any = false;
        for (const std::string& p : render::ArtPack::available()) any = any || render::ArtPack::isRedesign(p);
    }
    return any;
}

void setActiveGpuDriver(const char* driver) { gActiveGpuDriver = driver ? driver : ""; }

float displayOptionList(Ui& ui, Settings& s, const OptionListLayout& L, float y0, SDL_Window* window) {
    float y = y0;
    resolutionOrScale(ui, s, L.rect(y, L.rowH));
    y += L.rowH + L.gap;
    const SDL_FRect r = L.rect(y, L.rowH);
    if (ui.toggle(leftHalf(r), tr("Fullscreen"), s.fullscreen) && window) SDL_SetWindowFullscreen(window, s.fullscreen);
    renderChoice(ui, s, rightHalf(r));
    y += L.rowH + L.gap;
    const SDL_FRect r2 = L.rect(y, L.rowH);
    touchLayoutChoice(ui, s, leftHalf(r2));
    phoneUiSizeChoice(ui, s, rightHalf(r2));
    y += L.rowH + L.gap;
    const SDL_FRect r3 = L.rect(y, L.rowH);
    vsyncChoice(ui, s, leftHalf(r3));
    fpsLimitChoice(ui, s, rightHalf(r3));
    y += L.rowH + L.gap;
    // HDR: rendering (Enhanced / 3D, Modern lighting) and the EDR / HDR display output, under every tab.
    const SDL_FRect r4 = L.rect(y, L.rowH);
    ui.toggle(leftHalf(r4), tr("HDR rendering"), s.hdr);
    hdrOutputChoice(ui, s, rightHalf(r4));
    y += L.rowH + L.gap;
#if defined(_WIN32)
    gpuDriverChoice(ui, s, L.rect(y, L.rowH));
    y += L.rowH + L.gap;
#endif
    return y - y0;
}

float graphicsOptionList(Ui& ui, Settings& s, const OptionListLayout& L, float y0, SDL_Window* window, PictureProbe* probe) {
    const Settings::PresetValues before = s.presetValues();
    const char* enhancedOnly = s.enhanced() ? nullptr : "Enhanced / 3D only";
    float y = y0;
    PictureOption hovered = PictureOption::None, focused = PictureOption::None;
    int hoverDir = 1;
    // Wraps one widget: notes it when it is under the mouse / has the focus (the previews).
    auto pic = [&](PictureOption o, auto&& draw) {
        const int id0 = ui.widgetCount();
        draw();
        const int id1 = ui.widgetCount();
        if (ui.hoverId() >= id0 && ui.hoverId() < id1) hovered = o, hoverDir = ui.hoverDir();
        if (ui.focus() >= id0 && ui.focus() < id1) focused = o;
    };
    auto row = [&](auto&& draw) {
        draw(L.rect(y, L.rowH));
        y += L.rowH + L.gap;
    };
    y += header(ui, L, y, "Mode", Settings::modelPackInstalled() ? nullptr : Settings::kNeeds3dPackNote);
    row([&](const SDL_FRect& r) { pic(PictureOption::Mode, [&] { modeChoice(ui, s, r); }); });
    y += header(ui, L, y, "Terrain", enhancedOnly);
    row([&](const SDL_FRect& r) { pic(PictureOption::Terrain2D, [&] { terrain2dChoice(ui, s, r); }); });
    if (L.getArtPack && !redesignArtPackInstalled()) {
        // No redesign pack: "Original" here means the 1998 sprites - offer the download.
        const SDL_FRect hr = L.rect(y, L.headerH);
        header(ui, L, y, "Art", nullptr);
        const float bw = std::min(hr.w * 0.5f, ui.textWidth(tr("Get the art pack"), ui.textScale * 0.85f) + 16 * ui.textScale);
        if (ui.button(SDL_FRect{hr.x + hr.w - bw, hr.y + 1, bw, hr.h - 3}, tr("Get the art pack"), ButtonStyle::Primary, true, 0.85f))
            L.getArtPack();
        y += L.headerH + L.gap;
    } else {
        y += header(ui, L, y, "Art", enhancedOnly);
    }
    row([&](const SDL_FRect& r) { pic(PictureOption::Art, [&] { artChoice(ui, s, r); }); });
    y += header(ui, L, y, "Effects", enhancedOnly);
    row([&](const SDL_FRect& r) { pic(PictureOption::Effects, [&] { effectsChoice(ui, s, r); }); });
    y += header(ui, L, y, "Lighting", enhancedOnly);
    row([&](const SDL_FRect& r) {
        pic(PictureOption::TimeOfDay, [&] { timeOfDayChoice(ui, s, leftHalf(r)); });
        pic(PictureOption::Lighting, [&] { lightingChoice(ui, s, rightHalf(r)); });
    });
    y += header(ui, L, y, "Quality", enhancedOnly);
    // (HDR rendering / HDR display moved to Display, under every tab: owner 2026-11-13)
    row([&](const SDL_FRect& r) {
        pic(PictureOption::Quality, [&] { qualityChoice(ui, s, leftHalf(r)); });
        pic(PictureOption::Msaa, [&] { msaaChoice(ui, s, rightHalf(r)); });
    });
    row([&](const SDL_FRect& r) { pic(PictureOption::Aniso, [&] { anisoChoice(ui, s, leftHalf(r)); }); });
    y += header(ui, L, y, "Shroud", enhancedOnly);
    row([&](const SDL_FRect& r) { pic(PictureOption::Shroud, [&] { shroudChoice(ui, s, r); }); });
    y += header(ui, L, y, "Interface", enhancedOnly);
    row([&](const SDL_FRect& r) { pic(PictureOption::Panel, [&] { panelChoice(ui, s, r); }); });
    row([&](const SDL_FRect& r) { pic(PictureOption::HealthBars, [&] { healthChoice(ui, s, r, "Health bars"); }); });
    y += header(ui, L, y, "3D", !Settings::modelPackInstalled() ? Settings::kNeeds3dPackNote : s.threeD() ? nullptr : "3D only");
    row([&](const SDL_FRect& r) { pic(PictureOption::Terrain3D, [&] { terrain3dChoice(ui, s, r); }); });
    row([&](const SDL_FRect& r) {
        pic(PictureOption::Shadows, [&] { ui.toggle(leftHalf(r), tr("Sun shadows"), s.shadows3d); });
        pic(PictureOption::Models3D, [&] { models3dChoice(ui, s, rightHalf(r)); });
    });
    y += header(ui, L, y, "Display", "Not part of a preset");
    y += displayOptionList(ui, s, L, y, window);
    y += header(ui, L, y, "Classic view", enhancedOnly);
    row([&](const SDL_FRect& r) { classicViewChoice(ui, s, r); });
    if (probe) *probe = hovered != PictureOption::None ? PictureProbe{hovered, hoverDir} : PictureProbe{focused, 1};
    s.noteGraphicsEdit(before);
    return y - y0;
}

bool stepPictureOption(Settings& s, PictureOption o, int dir) {
    auto cyc = [dir](int v, int n) { return ((v + dir) % n + n) % n; };
    using P = PictureOption;
    switch (o) {
    case P::Mode: s.graphics = static_cast<Settings::Graphics>(cyc(static_cast<int>(s.graphics), 3)); return true;
    case P::Terrain2D: s.terrain2d = static_cast<Settings::Terrain2D>(cyc(static_cast<int>(s.terrain2d), 4)); return true;
    case P::Art: {
        static const std::vector<std::string> packs = render::ArtPack::available();  // as artChoice
        std::vector<std::string> values = {"original", "upscaled"};
        for (const auto& p : packs) values.push_back(p);
        int i = 1;
        for (std::size_t k = 0; k < values.size(); ++k)
            if (values[k] == s.artPack) i = int(k);
        s.artPack = values[std::size_t(cyc(i, int(values.size())))];
        return true;
    }
    case P::Effects: s.effects = s.effects == Settings::Effects::Modern ? Settings::Effects::Classic : Settings::Effects::Modern; return true;
    case P::TimeOfDay: s.timeOfDay = static_cast<Settings::TimeOfDay>(cyc(static_cast<int>(s.timeOfDay), 5)); return true;
    case P::Lighting: s.lighting = s.lighting == Settings::Lighting::Modern ? Settings::Lighting::Classic : Settings::Lighting::Modern; return true;
    case P::Quality: {
        const int q = static_cast<int>(s.quality());
        int n = cyc(q, 4);
        if (n == 3) n = cyc(n, 4);  // "Custom" is not a step
        s.applyQuality(static_cast<Settings::Quality>(n));
        return true;
    }
    case P::Hdr: s.hdr = !s.hdr; return true;
    case P::Msaa: {
        const int m = cyc(s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0, 3);
        s.msaa = m == 2 ? 4 : m == 1 ? 2 : 1;
        return true;
    }
    case P::Aniso: {
        const int a = cyc(s.aniso >= 16 ? 2 : s.aniso >= 4 ? 1 : 0, 3);
        s.aniso = a == 2 ? 16 : a == 1 ? 4 : 1;
        return true;
    }
    case P::Shroud: s.shroud = s.shroud == Settings::Shroud::Soft ? Settings::Shroud::Classic : Settings::Shroud::Soft; return true;
    case P::Panel: s.panelTransparency = static_cast<Settings::PanelTransparency>(cyc(static_cast<int>(s.panelTransparency), 3)); return true;
    case P::HealthBars: s.healthBars = static_cast<Settings::HealthBars>(cyc(static_cast<int>(s.healthBars), 3)); return true;
    case P::Terrain3D: s.terrain3d = static_cast<Settings::Terrain3D>(cyc(static_cast<int>(s.terrain3d), 3)); return true;
    case P::Shadows: s.shadows3d = !s.shadows3d; return true;
    case P::Models3D: s.models3d = static_cast<Settings::Models3D>(cyc(static_cast<int>(s.models3d), 2)); return true;  // New, PS1
    case P::None: break;
    }
    return false;
}

const char* pictureOptionLabel(PictureOption o) {
    static const char* const kLabels[] = {"",         "Graphics mode", "Terrain",       "Art",
                                          "Effects",  "Time of day",   "Lighting",      "Quality",
                                          "HDR rendering", "Anti-aliasing (3D)", "Texture filtering (3D)", "Shroud",
                                          "Panel transparency", "Health bars", "3D terrain", "Sun shadows", "3D models"};
    return kLabels[static_cast<int>(o)];
}

std::string pictureOptionValue(const Settings& s, PictureOption o) {
    using P = PictureOption;
    auto pick = [](int i, std::initializer_list<const char*> names) {
        const int n = int(names.size());
        return std::string(tr(names.begin()[std::clamp(i, 0, n - 1)]));
    };
    switch (o) {
    case P::Mode: return pick(static_cast<int>(s.graphics), {"Original", "Enhanced", "3D"});
    case P::Terrain2D: return pick(static_cast<int>(s.terrain2d), {"Original tiles", "Upscaled", "Designed", "Faithful"});
    case P::Art: return s.artPack == "original" ? tr("Original") : s.artPack == "upscaled" || s.artPack.empty() ? tr("Upscaled") : s.artPack;
    case P::Effects: return pick(static_cast<int>(s.effects), {"Classic", "Modern"});
    case P::TimeOfDay: return pick(static_cast<int>(s.timeOfDay), {"Day", "Dawn", "Dusk", "Night", "Cycle"});
    case P::Lighting: return pick(static_cast<int>(s.lighting), {"Classic", "Modern"});
    case P::Quality: return pick(static_cast<int>(s.quality()), {"Low", "Medium", "High", "Custom"});
    case P::Hdr: return pick(s.hdr ? 1 : 0, {"Off", "On"});
    case P::Msaa: return pick(s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0, {"Off", "2x", "4x"});
    case P::Aniso: return pick(s.aniso >= 16 ? 2 : s.aniso >= 4 ? 1 : 0, {"Off", "4x", "16x"});
    case P::Shroud: return pick(static_cast<int>(s.shroud), {"Classic", "Soft"});
    case P::Panel: return pick(static_cast<int>(s.panelTransparency), {"Off", "Light", "Medium"});
    case P::HealthBars: return pick(static_cast<int>(s.healthBars), {"Selected", "Damaged", "Always"});
    case P::Terrain3D: return pick(static_cast<int>(s.terrain3d), {"Original tiles", "Designed", "Faithful"});
    case P::Shadows: return pick(s.shadows3d ? 1 : 0, {"Off", "On"});
    case P::Models3D: return pick(static_cast<int>(s.models3d), {"New (art pack)", "PS1"});
    case P::None: break;
    }
    return {};
}

int languageOptionRows(Ui& ui, Settings& s, const std::function<SDL_FRect(int)>& row) {
    namespace lang = formats::lang;
    const int cp = lang::activeCodepage();
    const lang::Sources& src = languageSources();
    // Offered: every language with at least one part available, plus the current pick.
    std::vector<lang::Id> ids;
    std::vector<std::string> labels;
    for (lang::Id id : lang::kAll) {
        if (!lang::availability(id, src).any() && s.language != lang::code(id)) continue;
        ids.push_back(id);
        labels.push_back(lang::displayName(id, cp, true));  // brief: the long Russian (AI) name doesn't fit the row
    }
    int cur = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (s.language == lang::code(ids[i])) cur = int(i);
    if (!ids.empty() && ui.choice(row(0), tr("Language"), cur, labels)) {
        s.language = lang::code(ids[std::size_t(cur)]);
        s.languageText = s.languageVoices = s.languageMovies = "";  // one pick switches everything
    }
    // Advanced: each part from any language ("Same" = the language above).
    static const char* const kShort[lang::kCount] = {"EN", "FR", "DE", "ES", "RU PS1", "RU PC", "RU AI"};
    auto part = [&](const SDL_FRect& r, const char* label, std::string& value, bool lang::Availability::*has) {
        std::vector<std::string> opts = {tr("Same")};
        std::vector<std::string> codes = {""};
        for (lang::Id id : lang::kAll) {
            if (!(lang::availability(id, src).*has) && value != lang::code(id)) continue;
            opts.push_back(kShort[static_cast<int>(id)]);
            codes.push_back(lang::code(id));
        }
        int i = 0;
        for (std::size_t k = 0; k < codes.size(); ++k)
            if (codes[k] == value) i = int(k);
        if (ui.choice(r, tr(label), i, opts)) value = codes[std::size_t(i)];
    };
    const SDL_FRect r1 = row(1), r2 = row(2);
    const float half = std::floor(r1.w / 2);
    part(SDL_FRect{r1.x, r1.y, half - 4, r1.h}, "Text", s.languageText, &lang::Availability::text);
    part(SDL_FRect{r1.x + half + 4, r1.y, r1.w - half - 4, r1.h}, "Voices", s.languageVoices, &lang::Availability::voices);
    part(SDL_FRect{r2.x, r2.y, half - 4, r2.h}, "Cutscenes", s.languageMovies, &lang::Availability::movies);
    // What the chosen language lacks (falls back to English), e.g. "needs the Russian PS1 disc image".
    if (const auto id = lang::fromCode(s.language)) {
        const lang::Availability a = lang::availability(*id, src);
        const std::string hint = a.needs.empty() ? std::string(tr("Advanced: mix text, voices, cutscenes")) : std::string(tr(a.needs));
        const float ts = ui.textScale;
        ui.text(ui.ellipsize(hint, r2.w - half - 16 * ts, ts), r2.x + half + 10 * ts, r2.y + (r2.h - ui.textHeight(ts)) / 2,
                a.needs.empty() ? theme::kDim : theme::kBad, ts);
    }
    // Russian text: the Fargus translation alone or with our fixes (Settings::russianTextFixes;
    // docs/russian-translation-review.md). Shown only when the text language is Russian (owner request).
    const std::string textLang = s.languageText.empty() ? s.language : s.languageText;
    const auto textId = lang::fromCode(textLang);
    const bool russianText = textId && lang::isRussian(*textId);
    if (russianText && lang::availability(lang::Id::RussianPc, src).text) {
        int fx = s.russianTextFixes ? 1 : 0;
        const bool have = !src.ruFixesDir.empty();
        if (ui.choice(row(3), tr("Russian text"), fx, trList({"Fargus", have ? "Fargus + fixes" : "Fargus + fixes (not found)"})))
            s.russianTextFixes = fx == 1;
    }
    return kLanguageOptionRows;
}

}  // namespace d2k::platform
