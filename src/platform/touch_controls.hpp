#pragma once
// Touch controls for the iPad (docs/ios.md), built on the Original mouse scheme. The App feeds
// SDL finger events here while a game runs (and drops the mouse events SDL synthesises from
// touches); gestures become synthetic mouse events for Session::handleEvent plus a few direct
// Session::touch* calls:
//   tap                    = left click (select / move / attack / sidebar / radar); a tap that
//                            would do nothing (no own unit selected, empty ground) deselects
//   double tap on own unit = every own unit of that type on screen
//   one-finger drag        = scroll the map (on the radar: drag the view; on the panel: scroll it)
//   long press             = hover (tooltip); then drag = selection box
//   long press on panel    = popup: Build / x10 / Hold-Cancel(-infinite) / One fewer
//   two fingers            = scroll; pinch = zoom (Enhanced / 3D); twist = turn (3D)
//   building placement     = the first tap shows the footprint there, a second tap on it builds
//   three-finger tap       = deselect all (like the original's right click)
//   four-finger tap        = frame-time overlay on / off (Settings::perfOverlay)
//   phone layout           = long press on a quick-build icon: its menu (+ Pin / Unpin); a
//                            horizontal swipe on the drawer tab / the drawer opens / closes it;
//                            the buttons shrink to small icons with larger touch areas
//   on-screen buttons      = Menu, Stop, Deploy, Guard, Deselect (X), "1998" (the classic view,
//                            Enhanced / 3D: hold = peek, or a tap toggles it - Settings::classicView),
//                            groups 1-5 (tap selects, hold assigns)
// A real mouse / trackpad keeps working as on the Mac (its events are not touch events).

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace d2k::platform {

class Session;
class Ui;
struct Settings;

class TouchControls {
public:
    explicit TouchControls(Settings& settings) : settings_(settings) {}

    // A finger event during a game (window coordinates come from the normalised position).
    // Returns true when consumed (finger events always are).
    bool handleEvent(const SDL_Event& e, Session& s, Ui& ui, SDL_Window* win);
    // Per frame before drawing: long-press timers.
    void update(Session& s, Ui& ui);
    // On-screen buttons and the popup, over the game frame (not while paused / ended).
    void draw(Session& s, Ui& ui);
    // Drops any gesture (the game paused, ended or was replaced).
    void reset();
    bool used() const { return used_; }  // any touch seen in this game

private:
    enum class State { Idle, Pending, Pan, Hold, Box, RadarDrag, PanelScroll, Swipe, Two, Multi, Button, Popup, Consumed };
    struct Finger {
        SDL_FingerID id = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0;
        Uint64 t0 = 0;
    };
    struct Btn {
        SDL_FRect r{};
        std::string label;
        int kind = 0;  // 0 menu, 1 stop, 2 deploy, 3 guard, 4 deselect, 5 classic view, 10+n group n
        SDL_FRect t{};  // touch area (phone layout: larger than the small button)
    };
    struct PopupItem {
        SDL_FRect r{};
        std::string label;
        int action = 0;  // 0 build (left), 1 x10 (shift+left), 2 right click, 3 ctrl+right, 4 pin / unpin
    };

    void send(Session& s, Ui& ui, const SDL_Event& e);
    void motion(Session& s, Ui& ui, float x, float y);
    void button(Session& s, Ui& ui, float x, float y, int button, bool down);
    void click(Session& s, Ui& ui, float x, float y, int button, SDL_Keymod mod = SDL_KMOD_NONE);
    void hideMouse(Session& s, Ui& ui);
    void tap(Session& s, Ui& ui, float x, float y, Uint64 now);
    void layoutButtons(Session& s, Ui& ui);
    int buttonAt(float x, float y) const;
    void openPopup(Session& s, Ui& ui, float x, float y, int quickType = -1);
    Finger* find(SDL_FingerID id);

    Settings& settings_;
    State state_ = State::Idle;
    std::vector<Finger> fingers_;
    float lastX_ = 0, lastY_ = 0;           // previous position of the tracked finger
    float twoDist_ = 0, twoAngle_ = 0, twoCx_ = 0, twoCy_ = 0;
    float scrollAcc_ = 0;
    Uint64 lastTapAt_ = 0;
    float lastTapX_ = -1e4f, lastTapY_ = -1e4f;
    float placeX_ = -1e4f, placeY_ = -1e4f;  // where the placement footprint was last shown
    bool used_ = false;
    // Multi-finger taps (3 = deselect, 4 = overlay): most fingers down in this gesture, whether
    // any moved, when the first went down.
    int gestureMax_ = 0;
    bool gestureMoved_ = false;
    Uint64 gestureT0_ = 0;
    std::vector<Btn> buttons_;
    float unit_ = 48;  // button size (output pixels)
    float ppp_ = 1;    // output pixels per window point (platform/pointer.hpp; >1 at Render resolution: Full)
    int pressedBtn_ = -1;
    bool peekDown_ = false, peekRelease_ = false;  // the classic view button is held / to release
    bool btnLong_ = false;
    std::vector<PopupItem> popup_;
    float popupX_ = 0, popupY_ = 0;  // the long-pressed point (the click target)
    int popupPressed_ = -1;
    std::string toast_;
    Uint64 toastUntil_ = 0;
};

}  // namespace d2k::platform
