#pragma once
// The original in-game UI (Graphics: Original), drawn the way DUNE2000.DAT draws it:
// the UIBB.R16 frame (options bar on top, sidebar on the right), the UI_ENG.R16 buttons
// (scroll arrows, repair/sell/orb/guard/retreat, upgrade/starport, options), the credits
// counter, the radar (minimap or the fading house logo), the power bar and the two build
// strips (buildings left, units right) with the clock overlay and Ready/Hold text, plus the
// mission timer and message lines. Layout, frame indices and the original functions are in
// re/notes/ui.md.
//
// Everything is laid out in the original 640x400 coordinates ("game pixels") and drawn at an
// integer scale with nearest-neighbour sampling. Wider/taller windows get a wider battlefield
// and a taller sidebar (more icons per strip), anchored right like the original high-res modes.
// Drawing only reads the World; input is translated into actions the session executes.

#include "formats/bintables.hpp"
#include "formats/font.hpp"
#include "formats/r16.hpp"
#include "formats/templates.hpp"
#include "formats/uib.hpp"
#include "game/world.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::render {

struct ClassicAction {
    enum Kind {
        None,
        BuildBuilding, CancelBuilding, BeginPlacement,  // type = building template
        BuildUnit, CancelUnit,                          // type = unit template
        SpecialWeapon,   // type = unit behaviour (saboteur 4 / fremen 12): launch now
        TargetWeapon,    // type = unit behaviour (ornithopter 9 / death hand 10): pick a target
        Upgrade, CancelUpgrade,                         // type = building group / building template
        RepairMode, SellMode, Guard, Retreat, Options,
        RadarCentre,     // x, y = world pixels of the clicked radar cell (centre the view there)
        RadarMove,       // x, y = tile: move the selected units there
        Deselect,        // right click on the radar
        StarportPick, StarportUnpick,  // type = unit template (starport page strips)
        StarportPurchase,              // PURCHASE (back to the main page, send the order)
        StarportCancel,                // MAIN / UPGRADE on the starport page with picks
    } kind = None;
    int type = -1;
    int x = 0, y = 0;
};

class ClassicSidebar {
public:
    struct Assets {
        const formats::R16File& ui;            // UI_ENG.R16 (R16Layout::Ui)
        const std::vector<std::uint8_t>& uibb; // UIBB.R16 (640x400 RGB555)
        const formats::Font* fontCol;          // FONTCOL.FNT, may be null
        formats::FontPalette fontPalette;      // FONTCOL.FPL
        const formats::R16File& data;          // DATA.R16 (icons, digits, empty slot)
        const formats::HouseColours& colours;
        const formats::Templates& templates;
        std::string readyText = "Ready", holdText = "Hold";  // Text.UIB "Ready" / "Hold"
        const formats::StringTable* text = nullptr;  // Text.UIB (unit group names for tooltips)
        const formats::TileAtr* tileAtr = nullptr;   // TILEATRn.BIN (the tile tooltips' text ids)
    };

    ClassicSidebar(SDL_Renderer* r, Assets assets);
    ~ClassicSidebar();
    ClassicSidebar(const ClassicSidebar&) = delete;
    ClassicSidebar& operator=(const ClassicSidebar&) = delete;

    static constexpr int kBaseW = 640, kBaseH = 400;
    static constexpr int kOptionsBarH = 20, kSidebarW = 160;  // _OptionsBarHeight, SideBarWidth

    // Lays the frame out for a game resolution (game pixels, at least 640x400; drawn at scale
    // 1 into the session's offscreen target, which is then scaled to the window).
    void layout(int gameW, int gameH);
    int scale() const { return scale_; }
    // Battlefield rectangle in output pixels (below the options bar, left of the sidebar).
    SDL_Rect battlefield() const;
    bool inBattlefield(float sx, float sy) const;

    // Draws the frame for the current state. camX/camY = world pixel at the battlefield's
    // top-left; messages = active message lines (oldest first); cursorMode: 0 none, 2 repair,
    // 3 sell (the original DAT_00516438; drawn as a small marker next to the mouse).
    void draw(const game::World& w, int side, float camX, float camY, const std::vector<std::string>& messages,
              int cursorMode, float mouseX, float mouseY);

    // Mouse input in output pixels. `unitsSelected`: the radar moves units instead of the view.
    ClassicAction mouseDown(float sx, float sy, int button, const game::World& w, int side, bool unitsSelected);
    void mouseUp(int button);
    // Radar drag (left button held, no units selected): world pixel of the cell under the
    // point; false when outside the radar or the radar is off.
    bool radarToWorld(float sx, float sy, const game::World& w, int side, int& wx, int& wy) const;
    void wheel(float sx, float sy, float dy, const game::World& w, int side);
    // Opens the starport page (as the STARPORT button does), e.g. for one-frame screenshots.
    void showStarportPage() { page_ = Page::Starport; }
    void showUpgradePage() { page_ = Page::Upgrade; }
    // Not original: Extended production queue mode (Settings::productionQueue) - unit icons of a
    // busy factory are not greyed and every queued type shows its count / infinity mark.
    bool extendedQueue = false;
    // Tooltips (FUN_0044b5c0): drawn only while no page is open (the session clears it while
    // paused); shroudShown = the shroud is on (_bool_shroud_4DFB04, "Unrevealed Terrain");
    // skirmish = GameType != 0 (the radar button's MapToggle tooltip).
    bool tooltips = true, shroudShown = true, skirmish = false;
    // Screenshot runs: the next frame counts as the mouse having rested (the delayed tooltips).
    void forceTooltipRest() { tipForce_ = true; }
    // UI sounds the original queues (S_ICONSCROLL, S_ENDSCROLL, BUTTON); drained by the session.
    std::vector<std::string> takeSounds() { return std::move(sounds_); }

    // Draws `text` in FONTCOL (game pixels; caller sets the render scale). Used for the timer,
    // messages and Ready/Hold.
    void drawText(const std::string& text, int x, int y, const SDL_Color* tint = nullptr);
    int textWidth(const std::string& text) const { return assets_.fontCol ? assets_.fontCol->textWidth(text) : 0; }

private:
    void drawQueueCount(const game::World& w, int side, int type, int house, int x, int y);
    struct Tex {
        SDL_Texture* t = nullptr;
        int w = 0, h = 0;
    };
    enum class Page { Normal, Starport, Upgrade };  // _TacticalData 1 / 2 / 3
    struct Button {
        int x = 0, y = 0;  // UI_ENG.R16 position (640x400 layout)
        int state = 0;     // 0 hidden, 1 shown, 2 pressed (SideBar*Visible bytes)
    };

    Tex makeTexture(const std::vector<formats::Rgba>& px, int w, int h, bool streaming = false);
    const Tex& uiFrame(int i);
    const Tex& icon(int frame, int house, bool grey);
    std::vector<formats::Rgba> iconPixels(int frame, int house, bool grey) const;
    const Tex& workIcon(int slot, int frame, int house, int degrees);
    void blit(const Tex& t, int x, int y);
    void blitPart(const Tex& t, int sx, int sy, int w, int h, int dx, int dy);
    void fill(int x, int y, int w, int h, SDL_Color c);
    void frameRect(int x0, int y0, int x1, int y1, SDL_Color c);  // FUN_0042d140

    void updateButtons(const game::World& w, int side);
    int buttonAt(float gx, float gy) const;  // HandleSidebarButton 0x443040 pixel test, -1 none
    int bx(int i) const;
    int by(int i) const;
    void buildFrame();
    void drawBackdrop();
    void drawRadar(const game::World& w, int side, float camX, float camY);
    void drawPower(const game::World& w, int side);
    void drawCredits(const game::World& w, int side);
    void drawStrips(const game::World& w, int side);
    void drawUpgradePage(const game::World& w, int side);
    void drawStarportPage(const game::World& w, int side);
    void drawTooltip(const game::World& w, int side, float camX, float camY, float mouseX, float mouseY);
    void drawTooltipBox(const std::string& name, int price, int x0, int y0, int limitX, int limitY, SDL_Color c);
    void drawButtons();
    bool radarCell(float gx, float gy, const game::World& w, int& cx, int& cy) const;
    bool radarOn(const game::World& w, int side) const;
    void scrollStrip(int strip, int delta, bool page, const game::World& w, int side);
    int stripCount(int strip, const game::World& w, int side) const;

    SDL_Renderer* r_;
    Assets assets_;
    int scale_ = 1, gw_ = kBaseW, gh_ = kBaseH;  // game pixels
    int dx_ = 0;          // sidebar x shift (gw - 640)
    int iconCount_ = 4;   // SideBarIconCount
    int extraRows_ = 0;   // rows added below the 4th icon row (taller windows)
    std::vector<formats::Rgba> uibbPx_;  // UIBB.R16 640x400
    Tex frame_;                          // the frame at the current game resolution (buildFrame)
    std::vector<Tex> ui_;
    std::unordered_map<long long, Tex> icons_;
    struct Work { Tex tex; int frame = -1, house = -1, degrees = -1; };
    std::array<Work, 12> work_{};  // 0 building, 1..10 unit queues, 11 upgrade
    Tex textAtlas_, textMask_;
    std::vector<SDL_Rect> glyphRects_;
    Tex radar_;
    std::vector<formats::Rgba> radarPx_;
    int radarFade_ = 1;       // DAT_0051647c: logo fade-in counter (0 = radar map shown)
    std::array<Button, 15> buttons_{};
    int pressed_ = -1;
    Page page_ = Page::Normal;
    int scroll_[2] = {0, 0}, prevScroll_[2] = {0, 0};  // DAT_00516440/44 (+48/4c)
    int pulse_ = 0;           // DAT_00512d70: Ready text/frame colour cycle 0..40
    Uint64 pulseAt_ = 0;
    std::vector<std::string> sounds_;
    // Tooltip rest counter (DAT_0051756c, 60 Hz steps), last mouse world position
    // (DAT_00517560/64) and whether a tooltip was shown last frame (DAT_005175c4).
    int tipRest_ = 0, tipX_ = -1, tipY_ = -1;
    Uint64 tipAt_ = 0;
    bool tipVisible_ = false, tipForce_ = false;
};

}  // namespace d2k::render
