#pragma once
// Phone layout of the Enhanced in-game UI (not original; docs/ios.md "Phone layout"): the
// battlefield takes the whole screen and the chrome shrinks to
//  - a slim, semi-transparent top bar hugging the top edge: menu, a click-mode chip (cancel
//    repair / sell / placement / target), credits, a compact power bar, the mission timer, the
//    quick-build strip (4-6 buildable units: pinned first, then the most built; production as the
//    clock wedge plus a progress ring around the icon, queue count badge), the specials slot
//    (palace weapons / air strike, launched when charged), the radar button and the hide-UI button;
//  - a small, highly transparent radar in the top-left corner (the session draws the minimap into
//    radarRect(); only with a working radar, World::radarActive);
//  - the full production sidebar (render::Sidebar) as a drawer on the right edge with a pull tab,
//    plus a row of the command buttons (repair, sell, guard, retreat, upgrades, starport);
//  - hidden UI: only a tiny tab to bring it back.
// Icons are drawn small (Settings::phoneUiSize: ~22 / 26 / 30 points) but every element has an
// invisible touch area of at least 44 points that reaches into the screen edge; overlapping areas
// go to the nearest element. Everything stays inside the safe area (notch, home indicator).
// Drawing only reads the World; presses become PhoneHudActions for the session.

#include "game/world.hpp"
#include "render/sprites.hpp"
#include "render/ui_skin.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::render {

struct PhoneHudAction {
    enum Kind {
        None, Menu, CancelMode, HideUi, ShowUi, Radar, Drawer,
        BuildUnit, CancelUnit,  // type = unit template (the session applies the Extended queue mods)
        Special,                // type = unit template of a palace weapon / air strike
        Repair, Sell, Guard, Retreat, Upgrades, Starport,
        Absorb,                 // on the bar but on nothing that acts (credits, power): just eaten
    } kind = None;
    int type = -1;
};

class PhoneHud {
public:
    PhoneHud(UiSkin& skin, SpriteCache& icons) : skin_(skin), icons_(icons) {}

    struct Params {
        int outW = 0, outH = 0;
        float ppp = 1;            // output pixels per point
        SDL_FRect safe{};         // the safe area in output pixels
        float iconPt = 26;        // icon size in points (Settings::phoneUiSize)
        int maxQuick = 5;         // quick-build slots at most
        bool hidden = false, drawerOpen = false;
        int radarMode = 0;        // 0 small, 1 large, 2 off
        bool radarActive = false;
        int mapW = 64, mapH = 64; // tiles (the radar box follows the map's aspect)
        float drawerW = 0;        // the sidebar's width (output pixels, UiSkin::px(160))
        std::vector<int> pinned;  // pinned unit types, in pin order
        const std::unordered_map<int, int>* counts = nullptr;  // orders per unit type
        int cursorMode = 0;       // the session's click mode (2 repair, 3 sell, 4/5 target)
        bool placing = false;     // a building / cheat spawn follows the pointer
        // Keep the quick-build order of the last frame (while the player is tapping it, so the
        // icon under the finger does not move when its count changes).
        bool freezeOrder = false;
    };
    void layout(const Params& p, const game::World& w, int side);

    // The drawer (when open): its background and the command button row; the session then draws
    // the Sidebar into sidebarArea().
    void drawDrawer(const game::World& w, int side, bool upgradesLit, bool starportLit, float mouseX, float mouseY);
    // The top bar, the drawer's pull tab, or (hidden) the show tab; alpha = idle fade (1 = active).
    void draw(const game::World& w, int side, float alpha, bool extendedQueue);

    PhoneHudAction hit(float x, float y, int button) const;
    // Over an element of the bar / tab / drawer button row (touch areas included), or the bar band.
    bool contains(float x, float y) const;
    bool inDrawer(float x, float y) const;  // the open drawer's rectangle
    bool onDrawerTab(float x, float y) const;
    int quickTypeAt(float x, float y) const;  // the quick-build unit type there, -1 none
    std::vector<int> quickTypes() const;      // this frame's strip, left to right

    SDL_FRect radarRect() const { return radar_; }      // empty when not shown
    SDL_FRect sidebarArea() const { return sideArea_; }  // the drawer's Sidebar rectangle
    SDL_FRect drawerRect() const { return drawer_; }
    float barBottom() const { return bar_.y + bar_.h; }
    float iconPx() const { return icon_; }

private:
    struct Elem {
        SDL_FRect v{}, t{};  // visual and touch rectangles
        PhoneHudAction::Kind kind = PhoneHudAction::None;
        int type = -1;
        int beh = 0;     // specials: unit behaviour
        int frame = -1;  // build icon frame (DATA.R16)
    };
    void drawUnitIcon(const Elem& e, const game::World& w, int side, bool special, bool extendedQueue);
    void progressRing(const SDL_FRect& r, float frac, Colour c, float t);
    const Elem* elemAt(float x, float y) const;

    UiSkin& skin_;
    SpriteCache& icons_;
    Params p_;
    float icon_ = 26, pt_ = 1;  // icon size and one point in output pixels
    SDL_FRect bar_{}, radar_{}, drawer_{}, sideArea_{}, tab_{}, tabTouch_{};
    SDL_FRect credits_{}, power_{}, timer_{};
    std::vector<Elem> elems_;
    std::vector<Elem> drawerBtns_;
    std::string modeLabel_;
    std::vector<int> lastOrder_;  // the quick-build candidates in the last frame's order
    bool timerShown_ = false;
};

}  // namespace d2k::render
