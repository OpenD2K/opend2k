#pragma once
// One running game: the World plus camera, input (selection, orders, sidebar, minimap),
// rendering of the map/panel/overlays, positional sound, the pause menu and the game-end
// banner. Owned by the App while a mission/skirmish is being played.

#include "game/campaign.hpp"
#include "game/world.hpp"
#include "game/savegame.hpp"
#include "game/sound_system.hpp"
#include "platform/game_data.hpp"
#include "platform/saves.hpp"
#include "platform/settings.hpp"
#include "render/cursor.hpp"
#include "render/lighting.hpp"
#include "render/map_renderer.hpp"
#include "render/motion.hpp"
#include "render/weld_fx.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <future>
#include <unordered_map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2k::render {
class ArtPack;
class TileAtlas;
class SpriteCache;
class Minimap;
class TextRenderer;
class Sidebar;
class UiSkin;
class CommandPanel;
class ClassicSidebar;
class PhoneHud;
class SceneLights;
class Bloom;
class Hdr;
struct ClassicAction;
}  // namespace d2k::render

namespace d2k::render3d {
class Renderer3D;
class ModelRenderer;
class TerrainBaker;
}

namespace d2k::platform {

class Audio;
class SoundBank;
class ChannelOutput;
class InGamePages;
class CheatMenu;
class Ui;

// Debug/demo switches from the command line.
struct SessionConfig {
    std::optional<std::pair<int, int>> at;  // tile to centre the view on
    float zoom = 1.0f;
    int ticks = 0;          // pre-simulated ticks
    bool battle = false;    // every armed unit attacks (demo)
    bool vfxDemo = false;   // --vfx-demo: weapons fire and explode in a gallery around --at (effects demo)
    bool selectAll = false; // select all own units
    bool asyncModels = false;  // Graphics previews: 3D models load on a worker thread (Session::setAsyncModels)
    bool reveal = false;    // no shroud (debug / screenshots)
    int killX = -1, killY = -1;  // --kill-building: destroyed before the --ticks run (debug)
    int cursorMode = 0;     // initial click mode (debug: cursor screenshots of repair/sell/target)
    // Screenshot runs (--cursor-at): the mouse rests here (window output pixels), so hover
    // tooltips show in a one-frame capture (the original's 18-frame delay counts as passed).
    std::optional<std::pair<float, float>> mouseAt;
    std::string sidebarPage;  // "starport" / "upgrade": open that sidebar page (both UIs)
    std::optional<float> cameraTilt, cameraYaw;  // 3D mode: initial camera, degrees
    bool classicPeek = false;  // --classic-peek: the classic view (original 1998 graphics) held on
    // --safe-insets L,T,R,B (points): the phone layout's safe area for screenshots on the Mac.
    std::optional<std::array<float, 4>> safeInsets;
    // Options -> Graphics previews (platform/preset_previews.hpp): the curated scene is laid out
    // in createWorld (presetPreviewScene) and the frame is drawn for this output size (offscreen
    // target, 1 pixel per point) instead of the window's.
    bool presetPreview = false;
    int outputW = 0, outputH = 0;
};

// LoadGame: the pause menu picked a save slot (loadSlot()); the App replaces the session.
enum class SessionResult { None, QuitToMenu, QuitApp, LoadGame };

class Session {
public:
    // Play Music event (community extended events): track to switch to, consumed by the app.
    std::string takeMusicRequest() { std::string m; m.swap(musicRequest_); return m; }
    // `audio`/`sounds` may be null (muted or headless).
    Session(SDL_Renderer* r, std::unique_ptr<GameData> data, Settings& settings, Audio* audio, SoundBank* sounds,
            const SessionConfig& cfg);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void handleEvent(const SDL_Event& e);
    // Scrolling and fixed-step simulation for the time since the last call.
    void update();
    // Recording (--record): every update advances exactly `dt` s and the render clock follows
    // it (0 = real time).
    void setFixedStep(double dt) { fixedStep_ = dt; }
    // Draws the frame; with the pause menu / end banner drawn and handled through `ui`.
    void render(Ui& ui);

    SessionResult result() const { return result_; }
    game::GameEnd endState() const;
    // The statistics the score page needs (CSide counters of every side, the player's side).
    game::ScoreInput scoreInput() const;
    const GameStart& start() const { return data_->start; }
    const GameData& data() const { return *data_; }
    bool paused() const { return paused_; }
    void setPaused(bool p);
    void showPauseOptions() { pauseOptions_ = true; }  // pause menu on its options page
    // Pause menu on its save (true) / load (false) slot page.
    void showPauseSlots(bool saving) { setPaused(true); openSlots(saving); }
    // Screenshot runs: open a pause page by its --menu-page name (Original graphics: the
    // original in-game pages "pause", "pause-save", "pause-load", "pause-delete", "pause-abort",
    // "pause-restart", "pause-briefing", "pause-options"; Enhanced: the modern pause menu).
    void showPausePage(const std::string& name);
    void restart();

    // Save games (platform/saves.hpp). saveToSlot writes the current state (between ticks);
    // applySave replaces the freshly created world of a session started with gameStartOf(meta)
    // by the saved state and restores the camera (throws game::SaveError).
    bool saveToSlot(int slot, std::string& error, const std::string& name = {});
    void applySave(const game::SaveMeta& meta, std::span<const std::uint8_t> worldSection);
    int loadSlot() const { return loadSlot_; }
    // PlayMentatSound 0x46f900 from the UI (e.g. "GAMESAVED"): H_/O_/S_ + name by the
    // player's house through the speech queue.
    void mentatSpeech(const char* name, bool queue);
    // FUN_004762f0: a UI sample by name on any channel (in-game menus: "BUTTON").
    void uiSound(const char* name);
    void runTicks(int n);  // simulate n model ticks now (command line: --load N --ticks M)
    // Graphics previews: centre the view on world pixel (x, y) at `zoom` (Enhanced / 3D; Original
    // keeps 1:1) with the 3D camera tilt / yaw in degrees; one model tick without input or sound.
    void previewCamera(float x, float y, float zoom, float tiltDeg, float yawDeg);
    void previewTick();
    // Graphics previews: the 3D models load on a worker thread (the menu keeps running, the scene shows
    // billboards meanwhile); modelsLoading() is true until they are in. Games load them synchronously.
    void setAsyncModels(bool on) { asyncModels_ = on; }
    bool modelsLoading() const { return modelsJob_.valid(); }
    SDL_Rect battlefieldRect() const;  // output pixels of the drawn battlefield

    // Mouse cursor for the mouse at (x, y) output pixels this frame (render/cursor.hpp), with
    // the scale it is drawn at: the original UI scale (Original) or a matching smooth scale
    // with the upscaled frames (Enhanced).
    struct CursorChoice {
        render::Cursor id = render::Cursor::Arrow;
        float scale = 1;
        bool hq = false;
    };
    CursorChoice cursorAt(float x, float y);

    // Touch input (platform/touch_controls.hpp). Points are window coordinates, like mouse
    // events; the gestures arrive as synthetic mouse events through handleEvent plus these.
    // Hud: the phone layout's top bar / tabs (tap = press, long press on a quick-build icon = its
    // menu, a horizontal swipe on the drawer tab or the drawer opens / closes the drawer).
    enum class TouchZone { Battlefield, Radar, Panel, Hud };
    TouchZone touchZone(float x, float y) const;
    // The battlefield's rectangle in window coordinates (the touch buttons sit inside it).
    SDL_FRect touchBattlefield() const;
    bool touchPlacing() const;            // a building / cheat spawn follows the pointer
    bool touchZoomable() const { return !classicUi(); }  // Original graphics: fixed scale
    // While on, clicks follow the Original mouse scheme whatever Settings::controls says
    // (the touch scheme is built on it: tap = select / move / attack).
    void setTouchInput(bool on) { touchInput_ = on; }
    void touchPan(float dx, float dy);    // the finger moved by (dx, dy): the map follows it
    void touchZoom(float factor, float x, float y);  // pinch, around (x, y)
    void touchRotate(float radians);      // 3D: two-finger twist turns the camera
    // A tap that would do nothing (no own unit selected, nothing under it, no click mode):
    // deselects (also another side's object being viewed) and returns true.
    bool touchTapNothing(float x, float y);
    // Double tap on an own unit: every own unit of that type on screen; false if no own unit.
    bool touchSelectType(float x, float y);
    enum class TouchCommand { Stop, Deploy, Guard, Menu, Deselect };
    void touchCommand(TouchCommand c);
    void touchGroup(int grp, bool assign) { controlGroup(grp, assign); }
    bool touchGroupUsed(int grp) const;   // any own unit in control group grp

    // Phone layout (Settings::touchLayout; render/phone_hud.hpp, docs/ios.md "Phone layout"):
    // decided per frame in layout() (Enhanced / 3D only).
    bool phoneLayout() const { return phone_; }
    bool phoneHidden() const { return phone_ && phoneHidden_; }  // Hide UI: only the show tab
    SDL_FRect phoneSafeArea() const { return safe_; }  // output pixels
    float phoneIconPx() const;                          // the UI size's icon size in output pixels
    float phoneAlpha() const;                           // HUD opacity (fades when idle)
    render::UiSkin* phoneSkin() const;
    void touchActivity();                               // a finger went down: the HUD wakes up
    int touchQuickType(float x, float y) const;         // a quick-build icon's unit type there, -1
    bool touchQuickPinned(int type) const;
    void touchTogglePin(int type);
    // A horizontal swipe from (x0, y0) by dx: on the drawer tab / right edge (left: open) or the
    // open drawer (right: close). True when it acted.
    bool touchSwipe(float x0, float y0, float dx, float dy);

    // Classic view (Settings::classicView, docs/enhanced-graphics.md "Classic view"): Enhanced /
    // 3D only - the battlefield drawn in the original 1998 graphics (original tiles, pixel-exact
    // sprites, Classic effects and shroud, the original selection overlay) while the key is held
    // (Hold) or until it is pressed again (Toggle). Render only: the world, camera, selection and
    // orders are untouched. press(true/false) = the Tab key / the touch "1998" button down / up.
    bool classicPeekAvailable() const {
        return appliedGraphics_ != Settings::Graphics::Original && settings_.classicView != Settings::ClassicView::Off;
    }
    void classicPeekPress(bool down);
    bool classicPeek() const;

private:
    bool originalControls() const { return touchInput_ || settings_.controls == Settings::Controls::Original; }
    // Window point -> the battlefield coordinates handleEvent uses (Original: game pixels
    // below the options bar); false when (x, y) is not on the battlefield.
    bool toBattlefield(float& x, float& y) const;
    void zoomAround(float factor, float mx, float my);  // wheel / pinch zoom around a view point
    bool touchInput_ = false;
    // Phone layout state (render/phone_hud.hpp).
    bool phoneWanted(int ptW, int ptH) const;
    void layoutPhone(int ptW, int ptH);
    void renderPhoneUi(game::World& world);
    bool phoneMouseDown(float x, float y, int button);  // true when the HUD took the press
    void phoneSidebarAction(int kind);                   // auto-close the drawer while placing / targeting
    bool sidebarLive(float x, float y) const;            // the sidebar is drawn (phone: drawer open) and under (x, y)
    void saveQuickBuild();
    std::unique_ptr<render::PhoneHud> hud_;
    bool phone_ = false, phoneHidden_ = false, phoneDrawer_ = false, phoneReopen_ = false;
    int phoneRadar_ = 0;  // 0 small, 1 large, 2 off
    SDL_FRect safe_{};
    Uint64 hudActiveNs_ = 0;
    bool hudIdleForced_ = false, hudAwakeForced_ = false;  // screenshots: --sidebar-page idle / awake
    bool quickResort_ = false;    // re-sort the quick-build strip now (a pin changed)
    std::vector<int> quickPins_;
    std::unordered_map<int, int> quickCounts_;
    void createWorld();
    // Graphics mode (Settings::graphics): Original = 1x sprites/tiles, original selection
    // overlays and the original sidebar (ClassicSidebar); Enhanced = HQ sprites + modern panel.
    void syncGraphics();
    void logModelCoverage() const;  // Graphics: 3D: types without a model (syncModels)
    void syncVisuals();  // Enhanced art pack + effects (Settings::artPack / effects)
    void syncTerrain();  // the tile atlas + the Enhanced designed terrain (Settings::terrain2d)
    // 3D mode (docs/3d-mode.md): creates / drops the 3D renderer for Settings::graphics ThreeD;
    // without SDL_gpu the session stays Enhanced (logged once).
    void sync3d();
    void syncModels();
    // The 3D renderer while it draws the battlefield (nullptr in 2D and during the classic view,
    // which shows the 2D original view; input then maps through the 2D camera too).
    render3d::Renderer3D* view3d() const { return classicPeek() ? nullptr : r3d_.get(); }
    bool use3d() const { return view3d() != nullptr; }
    // Battlefield view pixel -> world pixel: the 2D camera, or a 3D pick (units: snap to a unit
    // drawn under the point; else the terrain point).
    void screenToWorld(float sx, float sy, float& wx, float& wy, bool units = true) const;
    // Scrolls the view by (dx, dy) world pixels along the screen axes (3D: turned by the yaw).
    void panView(float dx, float dy);
    // 3D: unit (pixel position) is inside the view-pixel box.
    bool unitInScreenBox(const game::Unit& u, float x0, float y0, float x1, float y1) const;
    bool classicUi() const { return appliedGraphics_ == Settings::Graphics::Original && classic_ != nullptr; }
    bool handleClassicEvent(SDL_Event& e);  // Original UI input; translates map events to map coords
    void classicAction(const render::ClassicAction& a);
    // A unit icon click from either sidebar: the original pick / cancel orders, or with
    // Settings::productionQueue Extended the counted queue (re/notes/economy.md §10).
    void unitIconClick(int type, bool cancel);
    void classicMapClick(float mx, float my);  // repair / sell / weapon-target cursor modes
    void drawPlacement(const game::World& world);
    void unitResponse(bool order);
    void unitVoice(int type, bool order);  // one unit type's select / order voice (speech queue)
    // Sounds (re/notes/audio.md): the original's channel rules through game::SoundSystem.
    int sfxVolume() const;                 // SFXVolume 0..100 from the settings
    int sampleOf(const game::SoundEvent& ev) const;
    void playSoundEvents(const game::World& world);
    void queueSpeech(int sample, bool queue, int throttle = 0, int priority = 0);
    game::SoundView soundView() const;
    void selectInRect(float x0, float y0, float x1, float y1, bool additive);
    // Original mouse scheme (HandleBattlefieldClick 0x4433a0, left button released).
    void originalLeftClick(float sx, float sy);
    bool originalBoxSelect(float x0, float y0, float x1, float y1, bool additive);  // SelectAllUnitsInArea 0x4a5ae0
    void deselectAll();
    bool anyUnitSelected() const;
    std::pair<int, int> placementTile(float sx, float sy) const;
    bool centreFromMinimap(float sx, float sy);
    // Graphics: 3D: the ground footprint of the visible battlefield (0..viewW_ x 0..viewH_ seen by
    // the eased 3D camera, on the plane through the look-at point), world pixels, clipped to the
    // map - the minimap's view frame. False when not in 3D (the 2D rectangle applies).
    bool viewFootprint3D(std::vector<SDL_FPoint>& poly) const;
    // Places the 3D view so the ground point under the visible battlefield's centre is (wx, wy),
    // keeping zoom / yaw / tilt (2D: cam_ centred on it, as before).
    void centreViewOn(float wx, float wy);
    // Enhanced: (x, y) is over a UI panel (right-hand panel, top plates, selection panel).
    bool overUi(float x, float y) const;
    // Control group 1..10: select its units, or (assign) make the selection the group.
    void controlGroup(int grp, bool assign);
    void syncUiArt();
    void layout();
    void drawPauseMenu(Ui& ui);
    void drawPauseOptions(Ui& ui);
    void openSlots(bool saving);
    void drawPauseSlots(Ui& ui);
    void drawEndPrompt(Ui& ui);
    // Original graphics: the original in-game pages (InGamePages) instead of the modern menu.
    void drawOriginalMenu(Ui& ui);
    void ensurePages(Ui& ui);

    SDL_Renderer* r_;
    std::unique_ptr<GameData> data_;
    Settings& settings_;
    Audio* audio_;
    SoundBank* sounds_;
    SessionConfig cfg_;
    game::SoundSystem snd_;
    std::unique_ptr<ChannelOutput> out_;

    std::unique_ptr<render::ArtPack> artPack_;  // before the caches that point at it
    std::vector<std::uint8_t> redesignedTypes_;  // per building type: redesigned art (prepareLighting)
    std::vector<std::uint8_t> redesignedUnitTypes_;  // per unit type: the same
    std::string appliedArtPack_;
    // The rendered pack's fx_weld anchors (2D Enhanced: weld flashes in the redesigned works).
    render::PackAnchors packAnchors_;
    const render::EffectAnchors* packAnchors2D() const {
        return appliedGraphics_ == Settings::Graphics::Enhanced && !packAnchors_.empty() ? &packAnchors_ : nullptr;
    }
    // Enhanced production icons (syncUiArt): the chosen art pack, or the first installed one.
    std::unique_ptr<render::ArtPack> uiArtPack_;
    std::string appliedUiPack_ = "-";
    std::unique_ptr<render::TileAtlas> atlas_;
    std::unique_ptr<render::SpriteCache> sprites_;
    std::unique_ptr<render::MapRenderer> mapRenderer_;
    std::unique_ptr<render::Minimap> minimap_;
    std::unique_ptr<render::TextRenderer> text_;
    // Enhanced in-game UI (docs/enhanced-graphics.md "In-game UI"): materials, radar block and
    // top plates, production panel (icons from iconSprites_: HQ + the UI art pack).
    std::unique_ptr<render::UiSkin> skin_;
    std::unique_ptr<render::CommandPanel> command_;
    std::unique_ptr<render::SpriteCache> iconSprites_;
    std::unique_ptr<render::Sidebar> sidebar_;
    std::unique_ptr<render::SpriteCache> uiSprites_;
    std::unique_ptr<render::ClassicSidebar> classic_;
    Settings::Graphics appliedGraphics_ = Settings::Graphics::Original;
    // Original UI click mode (DAT_00516438): 0 none, 2 repair, 3 sell, 4/5 pick a target for
    // the ornithopter / death hand (weaponBehaviour_).
    int cursorMode_ = 0, weaponBehaviour_ = -1;
    int mapTop_ = 0;  // battlefield top in output pixels (the original options bar)
    float rawMouseX_ = 0, rawMouseY_ = 0;
    std::unique_ptr<game::World> world_;
    // Cursor: last choice (kept while the left button is held) and this frame's view scroll
    // (direction -1/0/1 and whether the view is clamped at that edge) for the scroll arrows.
    render::Cursor lastCursor_ = render::Cursor::Arrow;
    int scrollDx_ = 0, scrollDy_ = 0;
    bool scrollBlockedX_ = false, scrollBlockedY_ = false;

    render::Camera cam_;
    // 3D mode: renderer + the wanted yaw / tilt (radians); the view centre and zoom come from cam_.
    std::unique_ptr<render3d::ModelRenderer> models3d_;  // before r3d_ (which points at it)
    std::string appliedModels_;
    std::unique_ptr<render3d::Renderer3D> r3d_;
    // setAsyncModels: the models being read (declared after models3d_ / r3d_: joined first on destruction).
    bool asyncModels_ = false;
    double lastCycleSeconds_ = 0;  // the day / night cycle clock at the last frame (Settings::cyclePhase)
    // (a shared_ptr: its deleter is made in session.cpp, where ModelRenderer is complete)
    std::future<std::shared_ptr<std::unique_ptr<render3d::ModelRenderer>>> modelsJob_;
    std::string modelsJobKey_;
    bool threeDFailed_ = false, snapCamera_ = true, middleDrag_ = false;
    // Enhanced "Terrain: Designed" (render3d/terrain_bake.*): the baked terrain chunks.
    std::unique_ptr<render3d::TerrainBaker> baker_;
    bool bakerFailed_ = false;
    // Time of day / Modern lighting (render/lighting.hpp; Enhanced / 3D only): this frame's light,
    // the lamps on units and buildings, bloom. prepareLighting sets them up for the frame (view =
    // the ground rectangle shown, world px); false = the old light (nothing to do).
    render::SceneLight light_;
    std::unique_ptr<render::SceneLights> sceneLights_;
    std::unique_ptr<render::Bloom> bloom_;
    std::unique_ptr<render::Hdr> hdr_;  // the HDR tone map (Settings::hdr; render/hdr.*)
    render::Hdr* hdrIfOn();             // hdr_ when HDR rendering is on and available, else nullptr
    bool prepareLighting(const game::World& w, bool threeD, const float view[4]);
    std::string appliedTerrainKey_;
    float camYaw_ = 0, camTilt_ = 0.7f;
    // Classic view: the original-graphics battlefield, built on first use and kept while the
    // session lives (instant afterwards): the original tile atlas, a 1x sprite cache (no art pack)
    // and a MapRenderer set up like Graphics: Original; drawn at 1 world pixel per texel into
    // peekTarget_, then scaled to the Enhanced zoom (nearest / pixel-art filtering).
    void ensureClassicPeek();
    void drawClassicPeek(const game::World& world, const game::Scenario& s, int viewW, int viewH);
    void drawClassicPeekLabel();
    std::unique_ptr<render::TileAtlas> peekAtlas_;
    std::unique_ptr<render::SpriteCache> peekSprites_;
    std::unique_ptr<render::MapRenderer> peekMap_;
    SDL_Texture* peekTarget_ = nullptr;
    bool peekHeld_ = false, peekToggled_ = false, peekWasOn_ = false;
    Uint64 lastRenderNs_ = 0;
    int playerSide_ = 0;
    int placingType_ = -1;  // building type being placed (cursor footprint), -1 = none
    struct LogLine { std::string text; std::uint32_t until; };
    std::vector<LogLine> messageLog_;  // mission messages, shown ~400 ticks like the original
    double tickAccumulator_ = 0;  // model ticks owed (fraction = interpolation alpha)
    Uint64 last_ = 0;             // SDL_GetTicksNS of the previous update
    double fixedStep_ = 0;        // --record: fixed frame step (s), 0 = real time
    double cameraMoveT_ = 0;      // D2K_CAMERA_MOVE: seconds into the move
    void applyCameraMove(double dt);
    render::MotionInterpolator motion_;
    // Layout of the current frame.
    int outW_ = 0, outH_ = 0, viewW_ = 0, viewH_ = 0;
    float ppp_ = 1;  // output pixels per window point (layout(); >1 at Render resolution: Full on Retina)
    int drawW_ = 0;  // drawn battlefield width: viewW_, or the whole window under a translucent panel  // outW_/outH_: the window's output pixels
    // Original graphics: the frame (battlefield + original UI) is drawn into classicTarget_ at
    // the game resolution (Settings::resolution) and scaled into present_ (window pixels).
    SDL_Texture* classicTarget_ = nullptr;
    // Enhanced minimap off: house logo fade-in counter (DAT_0051647c); starts faded in, fades in
    // again whenever the radar goes off.
    int radarFade_ = 0x41;
    int gameW_ = 640, gameH_ = 400;
    SDL_FRect present_{};
    void toGame(float& x, float& y) const;      // window output pixels -> game pixels
    void toGameEvent(SDL_Event& e) const;       // the same for a mouse event's coordinates
    SDL_FRect minimapArea_{};
    // Mouse interaction state: left = select (click or box), right = order (click) or pan (drag).
    bool dragging_ = false, rightMoved_ = false, leftDown_ = false, minimapDrag_ = false;
    float leftX_ = 0, leftY_ = 0, mouseX_ = -10000, mouseY_ = -10000;  // map coordinates (below mapTop_)
    bool paused_ = false;
    bool pauseOpened_ = false;  // reset the menu focus on the next pause frame
    bool pauseOptions_ = false; // pause menu shows the in-game options page
    float pauseOptionsScroll_ = 0;  // its rows scroll when the window is too small
    int pauseSlots_ = 0;        // pause menu shows the save (1) / load (2) slot page
    std::vector<SaveSlot> slots_;
    int slotSel_ = 0, loadSlot_ = -1;
    float slotScroll_ = 0;
    std::string slotStatus_;    // "Saved to slot 3" / error text on the slot page
    Uint64 endSeenAt_ = 0;  // SDL ticks when the game end was first drawn
    // Cheats page (platform/cheat_menu.hpp): Ctrl+Shift+C or the pause menu; its spawn
    // placement mode takes battlefield clicks while active.
    std::unique_ptr<CheatMenu> cheatMenu_;
    bool cheatsOpen_ = false, cheatsFromPause_ = false;
    void openCheats(bool fromPause);
    void drawCheats(Ui& ui);
    std::unique_ptr<InGamePages> pages_;  // original in-game pages (created on first use)
    std::string pendingPage_;   // screenshot: page to open instead of the menu
    bool replayShown_ = false;  // REPLAY_MISSION prompt opened for this game end
    SessionResult result_ = SessionResult::None;
    std::string musicRequest_;
};

// UI_ENG.R16 banner frames ("MISSION ACCOMPLISHED" 133x31, "MISSION FAILED" 67x31).
constexpr int kWonBannerFrame = 49, kLostBannerFrame = 50;

}  // namespace d2k::platform
