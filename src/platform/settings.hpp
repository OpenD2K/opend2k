#pragma once
// User settings persisted as key=value lines in SDL_GetPrefPath("OpenD2K", "Dune 2000")/settings.ini
// (~/Library/Application Support/OpenD2K/Dune 2000/settings.ini on macOS).

#include "platform/platform_info.hpp"

#include <string>
#include <vector>

namespace d2k::platform {

struct Settings {
    std::string dataDir;       // last working "Dune 2000/data" folder (empty = not chosen yet)
    // Presentation: Original = pixel-exact sprites/tiles (nearest scaling), the original selection
    // overlays and the original in-game sidebar/HUD (UIBB.R16 + UI_ENG.R16, integer-scaled);
    // Enhanced = Scale2x-upscaled, smoothed sprites/tiles with the modern panel and overlays.
    // ThreeD = the Enhanced UI over a 2.5D battlefield (docs/3d-mode.md; falls back to Enhanced
    // without SDL_gpu); everything that checks enhanced() treats it as Enhanced.
    // settings.ini key "graphics" (original|enhanced|3d); the old "hq=1" migrates to Enhanced.
    enum class Graphics { Original, Enhanced, ThreeD };
    // iPad (platform_info.hpp kTouchPlatform): Enhanced by default, the UI scaled up for touch.
    Graphics graphics = kTouchPlatform ? Graphics::Enhanced : Graphics::Original;
    bool enhanced() const { return graphics != Graphics::Original; }
    bool threeD() const { return graphics == Graphics::ThreeD; }
    static const char* graphicsName(Graphics g) {
        return g == Graphics::ThreeD ? "3d" : g == Graphics::Enhanced ? "enhanced" : "original";
    }
    static Graphics parseGraphics(const std::string& v) {
        return v == "3d" ? Graphics::ThreeD : v == "enhanced" ? Graphics::Enhanced : Graphics::Original;
    }
    // Enhanced only (docs/enhanced-graphics.md): artPack = "original" (unfiltered original
    // pixels), "upscaled" (the built-in Scale2x/4x, default) or an art-pack folder name
    // (artpacks/<name>/pack.ini); effects = Classic (the original presentation) or Modern
    // (particles, glows, treads, screen shake, smooth turning). settings.ini "artPack", "effects".
    std::string artPack = "upscaled";
    // Graphics: 3D only (docs/3d-models.md): what draws units / buildings. New = the glTF models of
    // the OpenD2K 3D art pack (modelPack = a folder name under artpacks/ with models/ and DATA/, "" =
    // the first installed, render::ArtPack::threeDPack; "new" = the built-in assets/models); PS1 = the
    // PS1 disc's 3D models (needs ps1Image). Types without a model are billboards of the 3D art pack's
    // redesigned frames - never the original sprites (owner, 2026-11-13: the original sprites do not
    // work in 3D; the old "sprites" value reads as New). settings.ini "models3d", "modelPack".
    enum class Models3D { New, Ps1 };
    Models3D models3d = Models3D::New;
    std::string modelPack;
    static const char* models3dName(Models3D m) { return m == Models3D::Ps1 ? "ps1" : "new"; }
    static Models3D models3dFrom(const std::string& v) { return v == "ps1" ? Models3D::Ps1 : Models3D::New; }
    // Graphics: 3D needs an installed OpenD2K 3D art pack (render::ArtPack::threeDPack); without one
    // the mode is not offered and a saved / --graphics 3d choice runs as Enhanced (App).
    static bool modelPackInstalled();
    // The short note shown where 3D is not offered (translated through render::tr; the place a
    // "Download" button can attach).
    static constexpr const char* kNeeds3dPackNote = "needs the OpenD2K 3D art pack";
    // Graphics: 3D only (docs/3d-mode.md "Terrain v2"): Tiles = the original tiles on the
    // heightfield; Designed = terrain v2, CC0 materials blended per cell + modelled rock cliffs
    // (needs assets/terrain, tools/fetch_terrain_assets.py; otherwise the tiles); Faithful =
    // terrain v3, "Designed (faithful)": the designed materials laid out after the original tiles
    // (docs/3d-mode.md "Terrain v3": the original's rock outlines, cliffs as relief, its concrete
    // slabs) - the terrain of the 3D preset since 2026-11-11. settings.ini "terrain3d". The
    // struct default stays Designed: one-frame / screenshot runs take their terrain from the
    // command line (--terrain3d), so their pictures do not change with the presets.
    enum class Terrain3D { Tiles, Designed, Faithful };
    Terrain3D terrain3d = Terrain3D::Designed;
    static const char* terrain3dName(Terrain3D t) {
        return t == Terrain3D::Designed ? "designed" : t == Terrain3D::Faithful ? "faithful" : "tiles";
    }
    static Terrain3D terrain3dFrom(const std::string& v) {
        return v == "tiles" ? Terrain3D::Tiles : v == "faithful" ? Terrain3D::Faithful : Terrain3D::Designed;
    }
    bool terrain3dDesigned() const { return terrain3d != Terrain3D::Tiles; }  // Designed or Faithful
    // Graphics: Enhanced (2D) only (docs/enhanced-graphics.md "Terrain"), independent of the art
    // pack and the effects: Original = the original tiles (unfiltered), Upscaled = the built-in
    // upscale or the art pack's tiles (the Enhanced look before this switch existed), Designed =
    // the designed terrain of Terrain v2 rendered from straight above (render3d/terrain_bake.*;
    // needs assets/terrain and the GPU renderer, otherwise silently Upscaled), Faithful =
    // "Designed (faithful)", terrain v3: the same bake, laid out after the original
    // tiles (docs/enhanced-graphics.md "Designed (faithful)") - the terrain of the Enhanced preset
    // (and so of new players) since 2026-11-11. settings.ini "terrain2d". The struct default stays
    // Designed (see terrain3d).
    enum class Terrain2D { Original, Upscaled, Designed, Faithful };
    Terrain2D terrain2d = Terrain2D::Designed;
    static const char* terrain2dName(Terrain2D t) {
        return t == Terrain2D::Designed ? "designed" : t == Terrain2D::Faithful ? "faithful"
               : t == Terrain2D::Original ? "original" : "upscaled";
    }
    static Terrain2D terrain2dFrom(const std::string& v) {
        return v == "original" ? Terrain2D::Original : v == "upscaled" ? Terrain2D::Upscaled
               : v == "faithful" ? Terrain2D::Faithful : Terrain2D::Designed;
    }
    bool terrain2dBaked() const { return terrain2d == Terrain2D::Designed || terrain2d == Terrain2D::Faithful; }
    // Graphics: 3D only (docs/3d-mode.md "Sun shadows"): a real sun shadow map (terrain, rocks and
    // models cast; everything receives) instead of the baked heightfield shadows. settings.ini "shadows3d".
    bool shadows3d = true;
    // Graphics: 3D only, experimental (docs/3d-mode.md "Reimagined"): the modern-RTS look - taller cliffs,
    // a lower sun with long cast shadows, procedural terrain detail (Faithful terrain) and the cinematic
    // particle effects (Modern effects). Set by the 3D preset (owner 2026-11-16); since 2026-11-18 part of the
    // 3D / 3D Classic preset match (3D = Reimagined, 3D Classic = off). settings.ini "reimagined".
    bool reimagined = false;
    // With Reimagined (docs/3d-mode.md "Ground clutter"): the ground clutter - stones, gravel, rubble, dry scrub - Off,
    // Sparse (half of it) or Dense. settings.ini "groundDetail" off|sparse|dense.
    enum class GroundDetail { Off, Sparse, Dense };
    GroundDetail groundDetail = GroundDetail::Dense;
    // With Reimagined (docs/3d-mode.md "Dust haze"): wind-blown dust veils drifting across the map (render only, not
    // part of a preset). settings.ini "dustHaze" on|off.
    bool dustHaze = true;
    // With Reimagined + Modern effects (docs/3d-mode.md "Vehicle dust trails", "Placement dust ring"): dust behind
    // moving ground vehicles and round newly placed buildings, scaled by Effects intensity (render only, not part of
    // a preset). settings.ini "groundDust" on|off.
    bool groundDust = true;
    // With Reimagined (docs/3d-mode.md "Aerial haze"): aerial perspective - the far part of a tilted view fades towards
    // a warm desert-sky haze - and a gentle bloom on the brightest sand (render only, not part of a preset).
    // settings.ini "aerialHaze" on|off.
    bool aerialHaze = true;
    // With Reimagined (docs/3d-mode.md "World beyond the map"): a decorative, non-playable desert around the
    // battlefield instead of the black void, and a few cells of camera travel past the map's edges (render
    // only, not part of a preset). settings.ini "mapSurroundings" on|off.
    bool mapSurroundings = true;
    // Its extent: false = only a band near the edge that fades to dark (default), true = the whole landscape to the
    // horizon. settings.ini "mapSurroundingsFull" on|off.
    bool mapSurroundingsFull = false;
    // With Reimagined (docs/3d-mode.md "Realistic grade"): the natural colour grade - a calmer, paler ground
    // palette with rust spice, darker models with sand dust on them, a filmic grade of the 3D picture and
    // ground occlusion round buildings and under vehicles (render only, not part of a preset). Off = the vivid
    // look, the default since 2026-11-21 (owner: with photo terrain and the new models vivid reads right for
    // Dune; Realistic is the optional Defcon-like look). settings.ini "naturalGrade" on|off.
    bool naturalGrade = false;
    // With Reimagined + Modern effects + 3D models (docs/3d-mode.md "Vehicle wrecks"): a destroyed ground vehicle's
    // model stays for a few seconds as a charred, burning hulk - turret blown off - then crumbles away; units
    // drive through it (render only: the unit is gone from the world as in the original; not part of a preset).
    // Off = the vehicle vanishes in its explosion as before. settings.ini "vehicleWrecks" on|off.
    bool vehicleWrecks = true;
    // With Reimagined (docs/3d-mode.md "Photo terrain"): real photographed ground - the CC0 scans of the photo
    // terrain pack (artpacks/photo-terrain, tools/fetch_photo_terrain.py) - instead of the procedural ripples,
    // gravel, cracks and strata; the palette stays. Without the pack the procedural ground is drawn whatever this
    // says (render only, not part of a preset). settings.ini "photoTerrain" on|off.
    bool photoTerrain = true;
    // With Reimagined (docs/3d-mode.md "Ambient occlusion"): screen-space ambient occlusion - soft darkening where
    // buildings meet the ground, between their parts, under hulls, in crevices - on the sky light only (render
    // only, not part of a preset). settings.ini "ambientOcclusion" on|off.
    bool ambientOcclusion = true;
    static const char* groundDetailName(GroundDetail v) {
        return v == GroundDetail::Off ? "off" : v == GroundDetail::Sparse ? "sparse" : "dense";
    }
    static GroundDetail groundDetailFrom(const std::string& v) {
        return v == "off" ? GroundDetail::Off : v == "sparse" ? GroundDetail::Sparse : GroundDetail::Dense;
    }
    enum class Effects { Classic, Modern };
    Effects effects = Effects::Modern;
    bool modernEffects() const { return enhanced() && effects == Effects::Modern; }
    // Modern effects only (Enhanced 2D, plain 3D, Reimagined; render/fx_budget.hpp, docs/enhanced-graphics.md
    // "Effects intensity"): Cinematic = the full look; Balanced = the effects scale down as the view gets busy,
    // only burning wrecks / buildings keep lasting smoke, smoke turns see-through over units; Clean = short
    // effects, minimal smoke. Not part of any preset. settings.ini "effectsIntensity" clean|balanced|cinematic.
    enum class EffectsIntensity { Clean, Balanced, Cinematic };
    EffectsIntensity effectsIntensity = EffectsIntensity::Clean;  // the default (owner 2026-11-16)
    static const char* effectsIntensityName(EffectsIntensity v) {
        return v == EffectsIntensity::Clean ? "clean" : v == EffectsIntensity::Cinematic ? "cinematic" : "balanced";
    }
    static EffectsIntensity effectsIntensityFrom(const std::string& v) {
        return v == "clean" ? EffectsIntensity::Clean : v == "cinematic" ? EffectsIntensity::Cinematic : v == "balanced" ? EffectsIntensity::Balanced : EffectsIntensity::Clean;
    }
    // Enhanced / 3D only (docs/enhanced-graphics.md "Vehicle animation"; render/vehicle_motion.hpp):
    // Modern = render-only hull motion of ground vehicles (squat / nose dip, roll in turns, suspension
    // bounce, engine tremble, recoil; dust when setting off with Modern effects); Original = the rigid
    // sprites. settings.ini "vehicleAnimation" original|modern; the Enhanced / 3D presets use Modern.
    enum class VehicleAnimation { Original, Modern };
    VehicleAnimation vehicleAnimation = VehicleAnimation::Modern;
    bool modernVehicles() const { return enhanced() && vehicleAnimation == VehicleAnimation::Modern; }
    static const char* effectsName(Effects e) { return e == Effects::Modern ? "modern" : "classic"; }
    // Enhanced only: the shroud edge. Classic = the original's tile edge pieces; Soft = a wide
    // blurred gradient into darkness (render/soft_shroud.hpp). settings.ini "shroud"; when the
    // key is absent it follows the effects (Modern -> Soft, Classic -> Classic).
    enum class Shroud { Classic, Soft };
    Shroud shroud = Shroud::Soft;
    bool softShroud() const { return enhanced() && shroud == Shroud::Soft; }
    static const char* shroudName(Shroud v) { return v == Shroud::Soft ? "soft" : "classic"; }
    // Enhanced only: when health bars are drawn (settings.ini "healthBars"). Buildings follow the
    // setting; units show theirs when selected, or always with Always.
    // Enhanced only (not original): the right-hand panel's background drawn translucent over the
    // battlefield, which then extends under it (settings.ini "panelTransparency"; icons, text and
    // buttons stay opaque). --panel-transparency off|light|medium.
    enum class PanelTransparency { Off, Light, Medium };
    PanelTransparency panelTransparency = PanelTransparency::Off;
    static const char* panelTransparencyName(PanelTransparency v) {
        return v == PanelTransparency::Medium ? "medium" : v == PanelTransparency::Light ? "light" : "off";
    }
    static PanelTransparency panelTransparencyFrom(const std::string& v) {
        return v == "medium" ? PanelTransparency::Medium : v == "light" ? PanelTransparency::Light : PanelTransparency::Off;
    }
    // Enhanced / 3D only (not original): the "classic view" key (Tab; the touch "1998" button) shows
    // the battlefield in the original 1998 graphics - original tiles, pixel-exact sprites, Classic
    // effects and shroud - while held (Hold) or until pressed again (Toggle); render only
    // (docs/enhanced-graphics.md "Classic view"). settings.ini "classicView" off|hold|toggle.
    enum class ClassicView { Off, Hold, Toggle };
    ClassicView classicView = ClassicView::Hold;
    static const char* classicViewName(ClassicView v) {
        return v == ClassicView::Toggle ? "toggle" : v == ClassicView::Off ? "off" : "hold";
    }
    static ClassicView classicViewFrom(const std::string& v) {
        return v == "toggle" ? ClassicView::Toggle : v == "off" ? ClassicView::Off : ClassicView::Hold;
    }
    // Enhanced / 3D only (docs/enhanced-graphics.md "Lighting"; render/lighting.hpp): the time of
    // day - Day / Dawn / Dusk / Night (cool moonlight, vehicle and building lamps) / Cycle (a slow
    // loop over a long battle) - and the lighting style: Classic = the old light, Modern = the
    // daylight look (warm sun, more contrast, bloom, 3D specular glints). settings.ini
    // "timeOfDay" (day|dawn|dusk|night|cycle), "lighting" (classic|modern). The struct defaults
    // (Day, Classic) are the look from before; the Enhanced / 3D presets set Day + Modern.
    enum class TimeOfDay { Day, Dawn, Dusk, Night, Cycle };
    TimeOfDay timeOfDay = TimeOfDay::Day;
    // The day / night cycle carries over between games (owner 2026-11-13): where the last game left it,
    // in cycle seconds (render/lighting.hpp cycleLength). settings.ini "cyclePhase".
    double cyclePhase = 0;
    // The battlefield zoom of the last played game (Enhanced / 3D; owner 2026-11-14): the next game starts with
    // it. 0 = none yet. settings.ini "lastZoom" (in points, i.e. without the display's pixel density).
    float lastZoom = 0;
    static const char* timeOfDayName(TimeOfDay t) {
        return t == TimeOfDay::Dawn ? "dawn" : t == TimeOfDay::Dusk ? "dusk" : t == TimeOfDay::Night ? "night"
               : t == TimeOfDay::Cycle ? "cycle" : "day";
    }
    static TimeOfDay timeOfDayFrom(const std::string& v) {
        return v == "dawn" ? TimeOfDay::Dawn : v == "dusk" ? TimeOfDay::Dusk : v == "night" ? TimeOfDay::Night
               : v == "cycle" ? TimeOfDay::Cycle : TimeOfDay::Day;
    }
    enum class Lighting { Classic, Modern };
    Lighting lighting = Lighting::Classic;
    static const char* lightingName(Lighting l) { return l == Lighting::Modern ? "modern" : "classic"; }
    static Lighting lightingFrom(const std::string& v) { return v == "modern" ? Lighting::Modern : Lighting::Classic; }
    // Quality (Enhanced / 3D only; docs/enhanced-graphics.md "Quality"). Graphics: Original never
    // reads these (byte-identical). settings.ini keys in brackets.
    // - hdr ("hdr"): HDR rendering with Modern lighting - the battlefield drawn into a half-float
    //   target (values above 1 survive: lamps, fire, flashes), bloom from those values, then a
    //   filmic shoulder tone map (render/hdr.*). Off = the 8-bit path from before.
    // - hdrOutput ("hdrOutput"): extended range (EDR / Windows HDR) output on a display that has
    //   headroom: highlights above SDR white. Read when the renderer is created (restart), and only
    //   when the game starts in Enhanced / 3D: the renderer then blends in linear light (SDL), which
    //   would change the Original picture. Falls back to SDR silently.
    // - msaa ("msaa" 1|2|4): the 3D scene's multisampling; aniso ("aniso" 1|4|16): anisotropic
    //   filtering of the 3D terrain / model textures.
    // The struct defaults are close to the look from before (one-frame runs): no HDR, 4x MSAA (3D
    // had it fixed), 16x anisotropy (the designed terrain had a fixed 8x, the tiles none). The
    // Enhanced / 3D presets set High (applyQuality).
    bool hdr = false;
    bool hdrOutput = false;
    int msaa = 4;
    int aniso = 16;
    // 3D resolution ("resolution3d" auto|100|75|50; not part of Quality): the 3D battlefield is
    // rendered at this share of the view's pixels per axis and scaled up (the UI stays sharp).
    // Auto (docs/3d-mode.md "Performance"): 100 % up to a pixel budget, less on very large outputs
    // (a 5K ultrawide), lowered further while frames miss the display rate. 0 = Auto.
    int resolution3d = 0;
    static int resolution3dFrom(const std::string& v) { return v == "100" ? 100 : v == "75" ? 75 : v == "50" ? 50 : 0; }
    enum class Quality { Low, Medium, High, Custom };
    void applyQuality(Quality q);
    Quality quality() const;  // the level the values match, else Custom
    // Display (not part of a preset): VSync On / Off / Adaptive ("vsync" on|off|adaptive; Adaptive =
    // late frames tear instead of waiting, falls back to On) and a frame limit ("fpsLimit" 0 =
    // unlimited | 30 | 60 | 120; frames are then paced by hand, also under vsync).
    enum class VSync { On, Off, Adaptive };
    VSync vsync = VSync::On;
    static const char* vsyncName(VSync v) { return v == VSync::Off ? "off" : v == VSync::Adaptive ? "adaptive" : "on"; }
    static VSync vsyncFrom(const std::string& v) {
        return v == "off" || v == "0" ? VSync::Off : v == "adaptive" ? VSync::Adaptive : VSync::On;
    }
    int fpsLimit = 0;
    // Display adjustments (Enhanced / 3D only, render-only: the last colour pass of the frame,
    // render/hdr.hpp DisplayAdjust; Original stays byte-identical). Not part of a preset.
    // "brightness" / "contrast" / "saturation" -50..50 (0 = as rendered), "gamma" 80..140 (x100;
    // 100 = as rendered, higher = brighter mid tones). Defaults: the picture unchanged.
    int brightness = 0, contrast = 0, gamma = 100, saturation = 0;
    static constexpr int kMinDisplayAdjust = -50, kMaxDisplayAdjust = 50, kMinGamma = 80, kMaxGamma = 140;
    bool displayAdjusted() const { return brightness != 0 || contrast != 0 || gamma != 100 || saturation != 0; }
    void resetDisplayAdjust() { brightness = contrast = saturation = 0, gamma = 100; }
    // Windows: the GPU backend for the next start ("gpuDriver" auto | vulkan | direct3d12; Auto = Vulkan
    // first, Direct3D 12 without a Vulkan driver - render3d/gpu.cpp). The SDL_GPU_DRIVER variable wins.
    std::string gpuDriver = "auto";
    enum class HealthBars { Selected, Damaged, Always };
    HealthBars healthBars = HealthBars::Selected;  // like the original
    static const char* healthBarsName(HealthBars h) {
        return h == HealthBars::Always ? "always" : h == HealthBars::Damaged ? "damaged" : "selected";
    }
    // Enhanced / 3D only (not original): the control-group number (1..9, 0) as a small badge left of
    // an own unit's health bar (settings.ini "groupNumbers"). Selected = only on selected units.
    // A UI aid, not part of a graphics preset. Graphics: Original keeps the original's digit (selected
    // units only, MapRenderer::drawClassicUnitSelection).
    enum class GroupNumbers { Off, Selected, Always };
    GroupNumbers groupNumbers = GroupNumbers::Always;
    static const char* groupNumbersName(GroupNumbers g) {
        return g == GroupNumbers::Always ? "always" : g == GroupNumbers::Selected ? "selected" : "off";
    }
    static GroupNumbers groupNumbersFrom(const std::string& v) {
        return v == "off" ? GroupNumbers::Off : v == "selected" ? GroupNumbers::Selected : GroupNumbers::Always;
    }
    // Graphics presets (Options -> Graphics; docs/enhanced-graphics.md "Presets"): one tap sets
    // every presentation value above to a known look. Original = graphics Original (the 1998
    // look; the Enhanced-only values are inert there and kept); Enhanced = our best 2D (terrain
    // Designed, Modern effects, soft shroud, health bars Selected, panel opaque, classic view
    // Hold, art = the chosen redesign pack or the original sprites - never an upscale);
    // ThreeD = the Enhanced values plus graphics 3D, 3D terrain Faithful, sun shadows, the art pack's
    // models and the Reimagined look ("3D (Reimagined)"; the "lite" iPads: Reimagined off);
    // ThreeDClassic = "3D Classic", the same with the original 1998 tiles on the heightfield
    // (terrain Tiles) and Reimagined off (owner decision 2026-11-18). Custom = anything else -
    // also the legacy 3D terrains (Designed, plain non-Reimagined Faithful), which get no new
    // upgrades. Display values (resolution, UI scale,
    // fullscreen, render resolution) are not part of a preset. settings.ini "graphicsPreset"
    // (original|enhanced|3d|3d-classic|custom); a stored preset whose values no longer match reads as the
    // preset the values match, else Custom. New players (no settings.ini) get Enhanced; the
    // struct defaults stay as they were (one-frame / screenshot / simtest runs use them).
    // The order is the Graphics page's tab order (menus index tabs / cards by it).
    enum class GraphicsPreset { Original, Enhanced, ThreeD, ThreeDClassic, Custom, Auto };  // Auto = not stored: detect
    GraphicsPreset graphicsPreset = GraphicsPreset::Auto;
    static const char* graphicsPresetName(GraphicsPreset p) {
        return p == GraphicsPreset::Original ? "original" : p == GraphicsPreset::Enhanced ? "enhanced"
             : p == GraphicsPreset::ThreeD ? "3d" : p == GraphicsPreset::ThreeDClassic ? "3d-classic"
             : p == GraphicsPreset::Custom ? "custom" : "";
    }
    static GraphicsPreset graphicsPresetFrom(const std::string& v) {
        return v == "original" ? GraphicsPreset::Original : v == "enhanced" ? GraphicsPreset::Enhanced
             : v == "3d" ? GraphicsPreset::ThreeD : v == "3d-classic" ? GraphicsPreset::ThreeDClassic
             : v == "custom" ? GraphicsPreset::Custom : GraphicsPreset::Auto;
    }
    // Whether the current values are exactly preset `p` (Original / Enhanced / ThreeD / ThreeDClassic).
    static bool isThreeDPreset(GraphicsPreset p) { return p == GraphicsPreset::ThreeD || p == GraphicsPreset::ThreeDClassic; }
    bool matchesPreset(GraphicsPreset p) const;
    // The preset shown as active: the stored one while it matches (Custom stays Custom), else the
    // first preset the values match, else Custom.
    GraphicsPreset activePreset() const;
    // Sets the values of preset `p` (Original / Enhanced / ThreeD / ThreeDClassic) and remembers it.
    void applyPreset(GraphicsPreset p);
    // The values a preset sets, for change detection: an option row that changes any of them
    // marks the preset Custom (noteGraphicsEdit).
    struct PresetValues {
        Graphics graphics;
        std::string artPack;
        Terrain2D terrain2d;
        Terrain3D terrain3d;
        bool shadows3d;
        Effects effects;
        Shroud shroud;
        HealthBars healthBars;
        PanelTransparency panelTransparency;
        ClassicView classicView;
        TimeOfDay timeOfDay;
        Lighting lighting;
        VehicleAnimation vehicleAnimation;
        bool reimagined;  // tells 3D (Reimagined) from the plain 3D looks (owner 2026-11-18)
        bool operator==(const PresetValues&) const = default;
    };
    PresetValues presetValues() const;
    // Call with the values from before an options frame: any change makes the preset Custom.
    void noteGraphicsEdit(const PresetValues& before) {
        if (!(presetValues() == before)) graphicsPreset = GraphicsPreset::Custom;
    }
    // <pref path>/previews: the Graphics page's rendered previews (platform/preset_previews.hpp);
    // empty without a pref path.
    static std::string previewCacheDir();
    bool fullscreen = kTouchPlatform;  // iPad: always full screen
    float musicVolume = 0.35f; // 0..1
    float soundVolume = 0.8f;  // 0..1
    // Original options (dune2000.ini [Options]; re/notes/timing.md):
    // GameSpeed 0..100 (default 75) picks the model tick rate (gameTicksPerSecond()),
    // ScrollRate 4..40 (default 20) = pixels the view scrolls per drawn frame.
    int gameSpeed = 75;
    int scrollRate = 20;
    // False until gameSpeed is read from settings.ini; then the original dune2000.ini is imported once.
    bool speedFromFile = false;
    // Mouse scheme: Original = left click selects / orders, right click deselects (0x4433a0);
    // Modern = left click selects, right click orders.
    enum class Controls { Original, Modern };
    Controls controls = Controls::Original;
    bool smoothMotion = true;
    // Frame-time overlay (fps, frame / CPU ms; App::drawPerfOverlay). iPad: four-finger tap;
    // Options "Frame time". settings.ini "perfOverlay".
    bool perfOverlay = false;
    // iPad: the device performance profile applied once to the defaults ("" = not yet; "lite" for
    // A10/A12-class iPads, "standard" otherwise; platform_info.hpp deviceTier()). "deviceProfile".
    std::string deviceProfile;
    // Render resolution (not original): Full = the window's real pixels on a Retina display
    // (SDL_WINDOW_HIGH_PIXEL_DENSITY: 2160x1620 on a 10.2" iPad, 2x on a Retina Mac), Half = the
    // point resolution (1080x810; a quarter of the pixels to fill), Auto = Half on the A10 /
    // A12-class iPads (platform_info.hpp DeviceTier::Low), Full everywhere else. Layout, map
    // zoom, UI scale and touch distances are in points times the density, so both look the same
    // size; Full is sharper. Read when the window is created: a change applies at the next start.
    // settings.ini "renderResolution" (auto|full|half); --render-resolution for one run.
    enum class RenderResolution { Auto, Full, Half };
    RenderResolution renderResolution = RenderResolution::Auto;
    static const char* renderResolutionName(RenderResolution v) {
        return v == RenderResolution::Full ? "full" : v == RenderResolution::Half ? "half" : "auto";
    }
    static RenderResolution renderResolutionFrom(const std::string& v) {
        return v == "full" ? RenderResolution::Full : v == "half" ? RenderResolution::Half : RenderResolution::Auto;
    }
    // Whether the window gets SDL_WINDOW_HIGH_PIXEL_DENSITY (resolves Auto with the device tier).
    bool fullPixelDensity() const;
    // The lighter defaults for a slow device: Classic effects, classic shroud, 3D -> Enhanced, the
    // built-in upscaled art (not a pack), panel transparency off (docs/ios.md "Performance").
    void applyLiteProfile();
    // Unit production clicks (not original; re/notes/economy.md §10): Original = the original's
    // pick / hold / cancel; Extended = counted queues (left +1, Shift+left +10, right = infinite /
    // cancel, Ctrl+right = one fewer). settings.ini "productionQueue" (original|extended).
    enum class ProductionQueue { Original, Extended };
    ProductionQueue productionQueue = ProductionQueue::Extended;
    bool extendedQueue() const { return productionQueue == ProductionQueue::Extended; }
    // The cheat menu (pause menu button, Ctrl+Shift+C). settings.ini "cheats".
    bool cheats = true;
    // Factory rally points (not original; game/rally.cpp): with an own factory selected, Original
    // controls Alt+left click / Modern controls right click on the ground sets its rally point.
    // settings.ini "rallyPoints".
    bool rallyPoints = true;
    // Attack-move (not original; game/attack_move.cpp): hold A and click the ground (Original
    // controls: left click; Modern: right click, or A then a click) - the selected units walk there
    // and fight what they meet. On in the Enhanced / 3D presets, off in Original (not part of the
    // preset match). settings.ini "attackMove".
    bool attackMove = false;
    // "Unit movement" (not original; game/modern_move.cpp, docs/modern-movement.md): a game rule
    // chosen per game - Original = the original's tile-by-tile MoveUnit (bit-exact), Modern =
    // free-angle driving with acceleration, smooth paths and group spreading for ground vehicles.
    // Used for new campaign / custom games and as the skirmish lobby's default; saved with each
    // game (GameStart / SaveMeta). Original everywhere by default (not part of any preset).
    // settings.ini "unitMovement" (original|modern).
    bool modernMovement = false;
    // Smooth camera (not original; docs/STATUS.md "Smooth camera", platform/camera_smooth.hpp):
    // keyboard / edge scrolling accelerates and decelerates, a right-drag pan keeps a little
    // momentum when released, the wheel zoom glides to its target around the cursor, minimap jumps
    // glide when near, 3D turn / tilt keys ease, and vehicles' drawn positions ease their starts /
    // stops (render only). On in the Enhanced / 3D presets, off in Original (the original
    // scrolling; not part of the preset match). settings.ini "smoothCamera".
    bool smoothCamera = false;
    // Smooth driving (not original; docs/STATUS.md "Smooth driving", render/smooth_drive.hpp): ground
    // vehicles are drawn driving round the corners of their route (curves through the tile centres,
    // creeping round while the original turns in place), facing where they drive, their turrets
    // slewing between the 32 steps; render only (needs Smooth unit motion; Enhanced / 3D). On in
    // the Enhanced / 3D presets, off in Original (not part of the preset match). settings.ini
    // "smoothDriving".
    bool smoothDriving = false;
    // Building placement (Graphics: 3D with models only; not original; docs/3d-mode.md "Drop-in
    // build-up", "Assemble build-up"): how a placed building's model appears during the original's
    // build-up time - Rise = out of the rock behind a rising cut (render/buildup_fx.hpp), DropIn = it
    // comes down folded, lands in a dust cloud and unfolds (render/building_dropin.hpp), Assemble = it
    // is built up from the ground part by part (render/building_assemble.hpp). Render only, same
    // duration. Assemble in the Enhanced / 3D presets, Rise in Original (not part of the preset match).
    // settings.ini "buildingPlacement" (rise|dropin|assemble); --building-placement for one run.
    enum class BuildingPlacement { Rise, DropIn, Assemble };
    BuildingPlacement buildingPlacement = BuildingPlacement::Rise;
    static const char* buildingPlacementName(BuildingPlacement v) {
        return v == BuildingPlacement::DropIn ? "dropin" : v == BuildingPlacement::Assemble ? "assemble" : "rise";
    }
    static BuildingPlacement buildingPlacementFrom(const std::string& v) {
        if (v == "assemble") return BuildingPlacement::Assemble;
        return v == "dropin" || v == "drop-in" ? BuildingPlacement::DropIn : BuildingPlacement::Rise;
    }
    // Limits (not original when on; default Original in every preset). "Order limit": the
    // original gives one group order to at most 64 of the selected units (Generate*Order
    // 0x459450..; re/notes/limits.md), Unlimited = all of them - applies at once. "Unit limit":
    // the original's 1000 shared object slots per side, Unlimited = the pool grows (up to
    // game::kMaxObjectsUnlimited) - used by new games (saved with the game).
    // settings.ini "orderLimit" / "unitLimit" (original|unlimited).
    bool unlimitedOrders = false;
    bool unlimitedUnits = false;
    // Cutscenes (platform/movies.hpp): play the VQA movies where the original does (house
    // selection, briefings, endings, ...). settings.ini "cutscenes".
    bool cutscenes = true;
    // Startup movie: the original plays the full intro (MVE_INTRO1) the first time and the short
    // title movie (MVE_INTRO3 = mve_fast) on later starts. "introOnStartup", "introSeen".
    bool introOnStartup = true;
    bool introSeen = false;
    // PS1 disc image (.cue or .bin) used for movies that have no PC VQA file; the Russian disc
    // also gives the "Русский (PS1)" speech and cutscenes. "ps1Image".
    std::string ps1Image;
    // Game language (platform/language.hpp, docs/languages.md): "en", "fr", "de", "es", "ru-ps1",
    // "ru-pc", "ru-ai" switches text, fonts, speech and cutscenes together; the Advanced overrides
    // languageText / languageVoices / languageMovies ("" = follow `language`) mix them.
    // translationDir: a Russian PC translation folder ("" = <Dune 2000>/../ru-pc when present).
    std::string language = "en";
    std::string languageText, languageVoices, languageMovies;
    std::string translationDir;
    // The Russian AI voice set (Русский (AI); the ru-ai-voices download or tools/voice/gen_ru_ai.py): a folder holding GAMESFX/*.AUD
    // ("" = <pref path>/Voices/ru-ai). "aiVoicesDir".
    std::string aiVoicesDir;
    // Russian text: the Fargus translation with OpenD2K's fixes (artpacks/lang/ru-fixes/Text.uib,
    // docs/russian-translation-review.md) on top (true, default) or the Fargus text only.
    // "russianTextFixes".
    bool russianTextFixes = true;
    // Original graphics: the game resolution (the community high-res patch's GameWidth /
    // GameHeight): "auto" (largest integer scale with at least 640x400 game pixels, filling the
    // window), "native" (the window's pixel size at scale 1) or "WxH" (e.g. "1024x768"). The
    // battlefield grows with it; the sidebar keeps its pixel size at the right edge. The frame is
    // rendered at that size and scaled to the window (nearest neighbour, letterboxed).
    std::string resolution = "auto";
    // The values offered in the options (first "auto", last "native").
    static const std::vector<std::string>& resolutionChoices();
    static std::string resolutionLabel(const std::string& r);  // "Auto", "Native", "1024x768"
    // Enhanced / 3D graphics: the in-game UI scale (screen pixels per original UI pixel):
    // "auto" (window height / 540 in quarter steps: 1.5 at 800 rows, 2 at 1080) or a percentage
    // "100".."300" (100 = the original 640x400 UI size). The battlefield is not scaled (zoom).
    std::string uiScale = kTouchPlatform ? "175" : "auto";
    static const std::vector<std::string>& uiScaleChoices();
    static std::string uiScaleLabel(const std::string& v);  // "Auto", "125%"
    // The UI scale for a window (outW x outH pixels); auto = autoK. Kept below ~40 % of the width.
    static float uiScaleFor(const std::string& v, float autoK, int outW);
    // Touch layout (Enhanced / 3D, not original; docs/ios.md "Phone layout"): Phone = the full-screen
    // battlefield with a slim top bar, a small transparent radar and the sidebar as a right-edge
    // drawer (Session::phoneLayout); Tablet = the iPad column; Auto = Phone on an iPhone or a screen
    // whose short side is under 500 points, Tablet elsewhere (a Mac stays Tablet: --phone-layout
    // forces Phone for one run). settings.ini "touchLayout" (auto|phone|tablet).
    enum class TouchLayout { Auto, Phone, Tablet };
    TouchLayout touchLayout = TouchLayout::Auto;
    static const char* touchLayoutName(TouchLayout v) {
        return v == TouchLayout::Phone ? "phone" : v == TouchLayout::Tablet ? "tablet" : "auto";
    }
    static TouchLayout touchLayoutFrom(const std::string& v) {
        return v == "phone" ? TouchLayout::Phone : v == "tablet" ? TouchLayout::Tablet : TouchLayout::Auto;
    }
    // Phone layout UI size: the top bar icons ~22 / 26 / 30 points (touch areas stay >= 44).
    // settings.ini "phoneUiSize" (tiny|small|normal).
    enum class PhoneUiSize { Tiny, Small, Normal };
    PhoneUiSize phoneUiSize = PhoneUiSize::Small;
    static const char* phoneUiSizeName(PhoneUiSize v) {
        return v == PhoneUiSize::Tiny ? "tiny" : v == PhoneUiSize::Normal ? "normal" : "small";
    }
    static PhoneUiSize phoneUiSizeFrom(const std::string& v) {
        return v == "tiny" ? PhoneUiSize::Tiny : v == "normal" ? PhoneUiSize::Normal : PhoneUiSize::Small;
    }
    // Phone layout quick-build strip: pinned unit types ("12,5") and how often each type was
    // ordered ("12:30,5:4"; most-built first). settings.ini "quickBuildPins" / "quickBuildCounts".
    std::string quickBuildPins, quickBuildCounts;
    // Game resolution (>= 640x400) for an output size in pixels.
    static void gameResolution(const std::string& r, int outW, int outH, int& gameW, int& gameH);  // interpolate unit/projectile positions between model ticks (render only)
    // Skirmish lobby choices, remembered between runs.
    std::string skirmishMap = "2PLAY1";
    int skirmishHouse = 0, skirmishAi = 1, skirmishTech = 7, skirmishCredits = 5000, skirmishDifficulty = 1;
    // The original skirmish page's other values (Skirmish.uil): gNetUnitCount 0..10, gNetWorms
    // 0..3, gNetCrates, gNetPlayerColour 0..7, gNetPlayerName. skirmishDifficulty doubles as the
    // campaign's gDifficultyLevel (one global in the original, set by skill.uil too).
    int skirmishUnits = 10, skirmishWorms = 1, skirmishColour = 0;
    bool skirmishCrates = false;
    std::string playerName = "Player";

    static constexpr int kMinGameSpeed = 0, kMaxGameSpeed = 100, kMinScrollRate = 4, kMaxScrollRate = 40;
    // Model ticks per second for GameSpeed (RecalcModelRate 0x45b610).
    static int gameTicksPerSecond(int gameSpeed);
    int ticksPerSecond() const { return gameTicksPerSecond(gameSpeed); }
    // Scroll speed in screen pixels per second: ScrollRate pixels per frame of the original's
    // 60 fps (the frame cap of the community-patched game).
    float scrollPixelsPerSecond() const { return float(scrollRate) * 60.0f; }

    // The downloadable OpenD2K art pack (platform/artpack_download.*): look for an update when
    // the Options page opens (never at startup) - "artPackUpdateCheck"; the manifest URL for
    // testing ("" = the GitHub release, kArtPackManifestUrl) - "artPackManifestUrl".
    // The same switch also covers the app update check (platform/app_update.*: is a newer
    // OpenD2K release out?); "appUpdateUrl" overrides its release info URL for testing ("" =
    // the GitHub API, kAppReleaseInfoUrl).
    bool artPackUpdateCheck = true;
    std::string artPackManifestUrl;
    // The first-run welcome page (Menu::pageWelcome: what OpenD2K is, choose the look, offer the
    // art pack) was shown - "firstRunArtPackAsked". false only for a new player (no settings.ini);
    // the struct default (one-frame runs) and an older settings.ini without the key: already asked.
    bool firstRunArtPackAsked = true;
    std::string appUpdateUrl;

    static std::string path();  // empty if SDL has no pref path
    static Settings load();
    bool save() const;
};

}  // namespace d2k::platform
