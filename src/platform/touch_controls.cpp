#include "platform/touch_controls.hpp"

#include "platform/session.hpp"
#include "platform/pointer.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "render/ui_skin.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::platform {

namespace {
constexpr float kSlop = 12;           // finger travel (points; x ppp_ for pixels) that turns a tap into a drag
constexpr Uint64 kLongPressMs = 450;  // long press: hover / box / popup / group assign
constexpr Uint64 kDoubleTapMs = 350;
constexpr float kDoubleTapDist = 36;
constexpr float kScrollStep = 36;     // panel drag: points per wheel step
constexpr float kOffscreen = -10000;  // the hidden pointer after a tap (no hover tooltips)
constexpr Uint64 kMultiTapMs = 500;   // three / four fingers down and up within this: a tap
constexpr float kMultiSlop = 30;      // ... and none moved further than this

float dist(float ax, float ay, float bx, float by) { return std::hypot(ax - bx, ay - by); }
}  // namespace

TouchControls::Finger* TouchControls::find(SDL_FingerID id) {
    for (auto& f : fingers_)
        if (f.id == id) return &f;
    return nullptr;
}

void TouchControls::reset() {
    fingers_.clear();
    state_ = State::Idle;
    popup_.clear();
    pressedBtn_ = -1;
    gestureMax_ = 0;
    gestureMoved_ = false;
    if (peekDown_) {  // released on the next update (no Session here)
        peekDown_ = false;
        peekRelease_ = true;
    }
}

void TouchControls::send(Session& s, Ui& ui, const SDL_Event& e) {
    s.handleEvent(e);
    if (e.type == SDL_EVENT_MOUSE_MOTION) ui.handleEvent(e);  // as the App does for the mouse
}

void TouchControls::motion(Session& s, Ui& ui, float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.which = 0;  // not SDL_TOUCH_MOUSEID: the App would drop it
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = 0;
    e.motion.yrel = 0;
    send(s, ui, e);
}

void TouchControls::button(Session& s, Ui& ui, float x, float y, int b, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.which = 0;
    e.button.button = Uint8(b);
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    send(s, ui, e);
}

void TouchControls::click(Session& s, Ui& ui, float x, float y, int b, SDL_Keymod mod) {
    // Modifier clicks (x10, one fewer): the session reads SDL_GetModState().
    const SDL_Keymod old = SDL_GetModState();
    if (mod != SDL_KMOD_NONE) SDL_SetModState(mod);
    motion(s, ui, x, y);
    button(s, ui, x, y, b, true);
    button(s, ui, x, y, b, false);
    if (mod != SDL_KMOD_NONE) SDL_SetModState(old);
}

void TouchControls::hideMouse(Session& s, Ui& ui) { motion(s, ui, kOffscreen, kOffscreen); }

void TouchControls::tap(Session& s, Ui& ui, float x, float y, Uint64 now) {
    const Session::TouchZone zone = s.touchZone(x, y);
    if (zone == Session::TouchZone::Battlefield && s.touchPlacing()) {
        // Placement: the first tap shows the footprint, a second tap on it places the building.
        if (dist(x, y, placeX_, placeY_) <= 40 * ppp_) {
            click(s, ui, x, y, SDL_BUTTON_LEFT);
            placeX_ = placeY_ = -1e4f;
        } else {
            motion(s, ui, x, y);
            placeX_ = x;
            placeY_ = y;
        }
        return;
    }
    placeX_ = placeY_ = -1e4f;
    const bool dbl = now - lastTapAt_ <= kDoubleTapMs && dist(x, y, lastTapX_, lastTapY_) <= kDoubleTapDist * ppp_;
    lastTapAt_ = dbl ? 0 : now;
    lastTapX_ = x;
    lastTapY_ = y;
    if (zone == Session::TouchZone::Battlefield) {
        if (dbl && s.touchSelectType(x, y)) {
            hideMouse(s, ui);
            return;
        }
        if (s.touchTapNothing(x, y)) {
            hideMouse(s, ui);
            return;
        }
    }
    click(s, ui, x, y, SDL_BUTTON_LEFT);
    hideMouse(s, ui);
}

void TouchControls::openPopup(Session& s, Ui& ui, float x, float y, int quickType) {
    popup_.clear();
    popupX_ = x;
    popupY_ = y;
    popupPressed_ = -1;
    const bool ext = settings_.extendedQueue();
    std::vector<std::pair<std::string, int>> items = {{render::tr("Build"), 0}};
    if (ext) items.push_back({render::tr("Build x10"), 1});
    items.push_back({ext ? render::tr("Repeat / Cancel") : render::tr("Hold / Cancel"), 2});
    if (ext) items.push_back({render::tr("One fewer"), 3});
    if (quickType >= 0) items.push_back({s.touchQuickPinned(quickType) ? render::tr("Unpin") : render::tr("Pin"), 4});
    // Phone layout: rows of at least 40 points (unit_ is the small icon size there).
    const float rowU = s.phoneLayout() ? std::max(unit_, 42 * ppp_) : unit_;
    const float w = rowU * 3.4f, h = rowU * 0.95f;
    int ww = 0, wh = 0;
    SDL_GetCurrentRenderOutputSize(ui.renderer(), &ww, &wh);
    // Left of the finger (the panel is at the right edge), vertically centred on it; under the
    // finger for the phone layout's top bar.
    float px = std::max(4.0f, x - w - rowU * 0.6f);
    float py = std::clamp(y - items.size() * (h + 4) / 2, 4.0f, std::max(4.0f, wh - items.size() * (h + 4) - 4));
    if (quickType >= 0) {
        px = std::clamp(x - w / 2, 4.0f, std::max(4.0f, ww - w - 4));
        py = std::max(y + rowU * 0.9f, s.phoneSafeArea().y + unit_ + 12 * ppp_);
    }
    for (const auto& [label, action] : items) {
        popup_.push_back({{px, py, w, h}, label, action});
        py += h + 4;
    }
    motion(s, ui, x, y);  // keep the icon's tooltip up while the popup is open
}

void TouchControls::layoutButtons(Session& s, Ui& ui) {
    buttons_.clear();
    const SDL_FRect bf = s.touchBattlefield();
    int ww = 0, wh = 0;
    SDL_GetWindowSize(SDL_GetRenderWindow(ui.renderer()), &ww, &wh);
    ppp_ = pixelsPerPoint(ui.renderer());
    const float wtp = windowToPixels(ui.renderer());  // window coordinates -> output pixels
    const float outH = float(wh) * wtp;
    if (s.phoneLayout()) {
        // Phone layout: small icons (the UI size) along the bottom-left edge of the safe area; the
        // touch areas are >= 44 points and reach the screen's bottom edge. The menu is in the top bar.
        if (s.phoneHidden()) return;
        const SDL_FRect sa = s.phoneSafeArea();
        unit_ = s.phoneIconPx();
        const float gap = std::round(8 * ppp_), minT = 44 * ppp_;
        const float y = sa.y + sa.h - unit_ - std::round(6 * ppp_);
        float x = sa.x + std::round(6 * ppp_);
        std::vector<int> kinds = {1, 2, 3, 4};
        if (s.classicPeekAvailable()) kinds.push_back(5);
        for (int g = 1; g <= 5; ++g) kinds.push_back(10 + g);
        for (int k : kinds) {
            const float w = k == 5 ? std::round(unit_ * 1.5f) : unit_;
            if (k == 11) x += gap;  // groups apart from the commands
            Btn b;
            b.r = {x, y, w, unit_};
            b.kind = k;
            b.label = k == 5 ? "1998" : k > 10 ? std::to_string(k - 10) : "";
            const float tw = std::max(w + gap, minT), th = std::max(unit_ + 2 * gap, minT);
            b.t = {x + w / 2 - tw / 2, std::min(y - gap, outH - th), tw, outH - std::min(y - gap, outH - th)};
            buttons_.push_back(b);
            x += w + gap;
        }
        return;
    }
    // ~44-56 points: Apple's minimum touch target is 44 (sizes below in output pixels).
    unit_ = std::clamp(std::min(float(ww), float(wh)) * wtp / ppp_ / 15.0f, 44.0f, 64.0f) * ppp_;  // window size in points
    const float gap = 6 * ppp_, pad = 8 * ppp_;
    struct Def {
        const char* label;
        int kind;
    };
    std::vector<Def> defs = {{"Menu", 0}, {"Stop", 1}, {"Deploy", 2}, {"Guard", 3}, {"", 4}};
    // The classic view (Enhanced / 3D, Settings::classicView): hold = original graphics while held,
    // Toggle = a tap switches it.
    if (s.classicPeekAvailable()) defs.push_back({"1998", 5});
    for (int g = 1; g <= 5; ++g) defs.push_back({nullptr, 10 + g});
    float x = bf.x + pad;
    const float y = bf.y + bf.h - unit_ - pad;
    for (const Def& d : defs) {
        const bool wide = d.label != nullptr && *d.label;
        const float w = wide ? unit_ * 1.5f : unit_;
        if (x + w > bf.x + bf.w - pad) break;
        std::string label = d.kind == 5 ? std::string(d.label)
                            : wide      ? std::string(render::tr(d.label))
                            : d.kind > 10 ? std::to_string(d.kind - 10)
                                          : std::string();
        buttons_.push_back({{x, y, w, unit_}, std::move(label), d.kind, {x - 3, y - 3, w + 6, unit_ + 6}});
        x += w + gap;
        if (d.kind == 4 || d.kind == 5) x += gap * 2;  // groups apart from the commands
    }
}

int TouchControls::buttonAt(float x, float y) const {
    // The touch areas may overlap (phone layout): the nearest button wins.
    const SDL_FPoint p{x, y};
    int best = -1;
    float bestD = 1e30f;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const SDL_FRect& t = buttons_[i].t;
        if (!SDL_PointInRectFloat(&p, &t)) continue;
        const SDL_FRect& r = buttons_[i].r;
        const float dx = x - (r.x + r.w / 2), dy = y - (r.y + r.h / 2);
        if (dx * dx + dy * dy < bestD) {
            bestD = dx * dx + dy * dy;
            best = int(i);
        }
    }
    return best;
}

bool TouchControls::handleEvent(const SDL_Event& e, Session& s, Ui& ui, SDL_Window* win) {
    if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_MOTION && e.type != SDL_EVENT_FINGER_UP &&
        e.type != SDL_EVENT_FINGER_CANCELED)
        return false;
    used_ = true;
    s.setTouchInput(true);
    // Finger positions are normalised to the window: output pixels like everything else.
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(win, &ww, &wh);
    const float x = e.tfinger.x * float(ww), y = e.tfinger.y * float(wh);
    const Uint64 now = SDL_GetTicks();
    layoutButtons(s, ui);

    if (e.type == SDL_EVENT_FINGER_DOWN) {
        s.touchActivity();
        fingers_.push_back({e.tfinger.fingerID, x, y, x, y, now});
        if (fingers_.size() == 1) {
            gestureMax_ = 1;
            gestureMoved_ = false;
            gestureT0_ = now;
        }
        gestureMax_ = std::max(gestureMax_, int(fingers_.size()));
        if (fingers_.size() >= 3) {
            // Three or more fingers: a multi-finger tap (deselect / overlay) or nothing. A
            // gesture in progress is wound down first.
            if (state_ == State::Hold) hideMouse(s, ui);
            if (state_ == State::Pending || state_ == State::Pan || state_ == State::Hold || state_ == State::Two)
                state_ = State::Multi;
            return true;
        }
        if (fingers_.size() == 1) {
            if (state_ == State::Popup) {
                const SDL_FPoint p{x, y};
                popupPressed_ = -1;
                for (std::size_t i = 0; i < popup_.size(); ++i)
                    if (SDL_PointInRectFloat(&p, &popup_[i].r)) popupPressed_ = int(i);
                if (popupPressed_ < 0) {  // outside: close it
                    popup_.clear();
                    hideMouse(s, ui);
                    state_ = State::Consumed;
                }
                return true;
            }
            if (const int b = buttonAt(x, y); b >= 0) {
                pressedBtn_ = b;
                btnLong_ = false;
                state_ = State::Button;
                if (buttons_[std::size_t(b)].kind == 5) {  // classic view: acts on press (like the Tab key)
                    peekDown_ = true;
                    s.classicPeekPress(true);
                }
                return true;
            }
            state_ = State::Pending;
            lastX_ = x;
            lastY_ = y;
            scrollAcc_ = 0;
        } else if (fingers_.size() == 2) {
            if (state_ == State::Pending || state_ == State::Pan || state_ == State::Hold) {
                if (state_ == State::Hold) hideMouse(s, ui);
                state_ = State::Two;
                const Finger &a = fingers_[0], &b = fingers_[1];
                twoDist_ = std::max(1.0f, dist(a.x, a.y, b.x, b.y));
                twoAngle_ = std::atan2(b.y - a.y, b.x - a.x);
                twoCx_ = (a.x + b.x) / 2;
                twoCy_ = (a.y + b.y) / 2;
            }
        }
        return true;
    }

    Finger* f = find(e.tfinger.fingerID);
    if (!f) return true;
    f->x = x;
    f->y = y;
    const bool primary = f == &fingers_[0];
    if (dist(x, y, f->x0, f->y0) > kMultiSlop * ppp_) gestureMoved_ = true;

    if (e.type == SDL_EVENT_FINGER_MOTION) {
        switch (state_) {
        case State::Pending:
            if (!primary || dist(x, y, f->x0, f->y0) < kSlop * ppp_) break;
            switch (s.touchZone(f->x0, f->y0)) {
            case Session::TouchZone::Battlefield:
                state_ = State::Pan;
                s.touchPan(x - f->x0, y - f->y0);
                break;
            case Session::TouchZone::Radar:  // press on the radar and drag the view along
                state_ = State::RadarDrag;
                motion(s, ui, f->x0, f->y0);
                button(s, ui, f->x0, f->y0, SDL_BUTTON_LEFT, true);
                motion(s, ui, x, y);
                break;
            case Session::TouchZone::Panel:
                // Phone layout: a sideways swipe on the drawer closes it; otherwise the list scrolls.
                if (s.phoneLayout() && std::abs(x - f->x0) > std::abs(y - f->y0)) state_ = State::Swipe;
                else {
                    state_ = State::PanelScroll;
                    scrollAcc_ = y - f->y0;
                }
                break;
            case Session::TouchZone::Hud:  // the drawer tab swipes; other drags on the bar do nothing
                state_ = std::abs(x - f->x0) > std::abs(y - f->y0) ? State::Swipe : State::Consumed;
                break;
            }
            lastX_ = x;
            lastY_ = y;
            break;
        case State::Pan:
            if (!primary) break;
            s.touchPan(x - lastX_, y - lastY_);
            lastX_ = x;
            lastY_ = y;
            break;
        case State::Hold:
            if (primary && dist(x, y, f->x0, f->y0) >= kSlop * ppp_) {  // long press + drag: selection box
                state_ = State::Box;
                motion(s, ui, f->x0, f->y0);
                button(s, ui, f->x0, f->y0, SDL_BUTTON_LEFT, true);
                motion(s, ui, x, y);
            }
            break;
        case State::Box:
        case State::RadarDrag:
            if (primary) motion(s, ui, x, y);
            break;
        case State::PanelScroll: {
            if (!primary) break;
            scrollAcc_ += y - lastY_;
            lastY_ = y;
            while (std::abs(scrollAcc_) >= kScrollStep * ppp_) {
                const float dir = scrollAcc_ > 0 ? 1.0f : -1.0f;  // finger down = content down = wheel up
                scrollAcc_ -= dir * kScrollStep * ppp_;
                SDL_Event w{};
                w.type = SDL_EVENT_MOUSE_WHEEL;
                w.wheel.timestamp = SDL_GetTicksNS();
                w.wheel.which = 0;
                w.wheel.x = 0;
                w.wheel.y = dir;
                w.wheel.mouse_x = f->x0;
                w.wheel.mouse_y = f->y0;
                SDL_WarpMouseInWindow(nullptr, f->x0 / windowToPixels(ui.renderer()), f->y0 / windowToPixels(ui.renderer()));  // the wheel handler reads the mouse state (points)
                send(s, ui, w);
            }
            break;
        }
        case State::Two: {
            if (fingers_.size() < 2) break;
            const Finger &a = fingers_[0], &b = fingers_[1];
            const float d = std::max(1.0f, dist(a.x, a.y, b.x, b.y)), ang = std::atan2(b.y - a.y, b.x - a.x);
            const float cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
            s.touchPan(cx - twoCx_, cy - twoCy_);
            if (s.touchZoomable() && std::abs(d / twoDist_ - 1) > 0.002f) s.touchZoom(d / twoDist_, cx, cy);
            float da = ang - twoAngle_;
            if (da > 3.14159f) da -= 6.28318f;
            if (da < -3.14159f) da += 6.28318f;
            s.touchRotate(da);
            twoDist_ = d;
            twoAngle_ = ang;
            twoCx_ = cx;
            twoCy_ = cy;
            break;
        }
        case State::Button:
            if (pressedBtn_ >= 0 && buttonAt(x, y) != pressedBtn_) pressedBtn_ = -1;  // slid off: no action
            break;
        default: break;
        }
        return true;
    }

    // Finger up / cancelled.
    const bool cancelled = e.type == SDL_EVENT_FINGER_CANCELED;
    const Finger up = *f;
    fingers_.erase(fingers_.begin() + (f - fingers_.data()));
    if (state_ == State::Multi) {
        if (!fingers_.empty()) return true;
        state_ = State::Idle;
        if (!cancelled && !gestureMoved_ && now - gestureT0_ <= kMultiTapMs) {
            if (gestureMax_ == 3) s.touchCommand(Session::TouchCommand::Deselect);
            else if (gestureMax_ == 4) settings_.perfOverlay = !settings_.perfOverlay;
        }
        return true;
    }
    if (!primary && state_ != State::Two) return true;  // an extra finger during a one-finger gesture
    switch (state_) {
    case State::Pending:
        if (!cancelled) tap(s, ui, up.x0, up.y0, now);
        break;
    case State::Hold: hideMouse(s, ui); break;
    case State::Box: {
        // A box needs >= 4 px each way (else the session takes it as a click = an order).
        float ex = up.x, ey = up.y;
        if (std::abs(ex - up.x0) < 5) ex = up.x0 + (ex >= up.x0 ? 5.0f : -5.0f);
        if (std::abs(ey - up.y0) < 5) ey = up.y0 + (ey >= up.y0 ? 5.0f : -5.0f);
        motion(s, ui, ex, ey);
        button(s, ui, ex, ey, SDL_BUTTON_LEFT, false);
        hideMouse(s, ui);
        break;
    }
    case State::RadarDrag:
        button(s, ui, up.x, up.y, SDL_BUTTON_LEFT, false);
        hideMouse(s, ui);
        break;
    case State::Swipe:
        if (!cancelled) s.touchSwipe(up.x0, up.y0, up.x - up.x0, up.y - up.y0);
        break;
    case State::Button:
        if (peekDown_) {  // the classic view button let go (also when slid off it)
            peekDown_ = false;
            s.classicPeekPress(false);
        }
        if (pressedBtn_ >= 0 && !cancelled && !btnLong_) {
            const int k = buttons_[std::size_t(pressedBtn_)].kind;
            if (k == 0) s.touchCommand(Session::TouchCommand::Menu);
            else if (k == 1) s.touchCommand(Session::TouchCommand::Stop);
            else if (k == 2) s.touchCommand(Session::TouchCommand::Deploy);
            else if (k == 3) s.touchCommand(Session::TouchCommand::Guard);
            else if (k == 4) s.touchCommand(Session::TouchCommand::Deselect);
            else if (k > 10) s.touchGroup(k - 10, false);
        }
        pressedBtn_ = -1;
        break;
    case State::Popup:
        if (popupPressed_ >= 0 && !cancelled) {
            const SDL_FPoint p{up.x, up.y};
            const PopupItem it = popup_[std::size_t(popupPressed_)];
            if (SDL_PointInRectFloat(&p, &it.r)) {
                if (it.action == 4) s.touchTogglePin(s.touchQuickType(popupX_, popupY_));
                else if (it.action == 0) click(s, ui, popupX_, popupY_, SDL_BUTTON_LEFT);
                else if (it.action == 1) click(s, ui, popupX_, popupY_, SDL_BUTTON_LEFT, SDL_KMOD_LSHIFT);
                else if (it.action == 2) click(s, ui, popupX_, popupY_, SDL_BUTTON_RIGHT);
                else click(s, ui, popupX_, popupY_, SDL_BUTTON_RIGHT, SDL_KMOD_LCTRL);
                popup_.clear();
                hideMouse(s, ui);
            }
        }
        popupPressed_ = -1;
        if (!popup_.empty()) return true;  // stays open
        break;
    default: break;
    }
    if (fingers_.empty()) state_ = State::Idle;
    else state_ = State::Consumed;  // the rest of a two-finger gesture
    return true;
}

void TouchControls::update(Session& s, Ui& ui) {
    if (peekRelease_) {
        peekRelease_ = false;
        s.classicPeekPress(false);
    }
    const Uint64 now = SDL_GetTicks();
    if (fingers_.size() != 1) return;
    const Finger& f = fingers_[0];
    if (now - f.t0 < kLongPressMs) return;
    if (state_ == State::Pending) {
        switch (s.touchZone(f.x0, f.y0)) {
        case Session::TouchZone::Battlefield:
        case Session::TouchZone::Radar:
            state_ = State::Hold;  // hover: tooltips; a drag from here draws a selection box
            motion(s, ui, f.x0, f.y0);
            break;
        case Session::TouchZone::Panel:
            state_ = State::Popup;
            openPopup(s, ui, f.x0, f.y0);
            break;
        case Session::TouchZone::Hud:
            // A quick-build icon: its menu (with Pin / Unpin); elsewhere on the bar nothing.
            if (const int qt = s.touchQuickType(f.x0, f.y0); qt >= 0) {
                state_ = State::Popup;
                openPopup(s, ui, f.x0, f.y0, qt);
            } else {
                state_ = State::Consumed;
            }
            break;
        }
    } else if (state_ == State::Button && pressedBtn_ >= 0 && !btnLong_) {
        const int k = buttons_[std::size_t(pressedBtn_)].kind;
        if (k > 10) {  // hold a group button: make the selection that group
            s.touchGroup(k - 10, true);
            btnLong_ = true;
            toast_ = render::trf("Group %d set", k - 10);
            toastUntil_ = now + 1200;
        }
    }
}

void TouchControls::draw(Session& s, Ui& ui) {
    layoutButtons(s, ui);
    Ui::ModernScope modern(ui);
    const float ts = unit_ / 48.0f;
    render::UiSkin* skin = s.phoneSkin();
    for (std::size_t i = 0; skin && i < buttons_.size(); ++i) {
        // Phone layout: small see-through squares with a glyph / digit, fading when idle.
        using Icon = render::UiSkin::Icon;
        const Btn& b = buttons_[i];
        const bool pressed = int(i) == pressedBtn_;
        const bool group = b.kind > 10;
        const bool used = (group && s.touchGroupUsed(b.kind - 10)) || (b.kind == 5 && s.classicPeek());
        skin->setAlpha(pressed ? 1.0f : s.phoneAlpha());
        const render::Colour a = skin->accent();
        skin->fill(b.r, pressed ? render::Colour{a.r, a.g, a.b, 200} : render::Colour{8, 6, 4, 120});
        skin->outline(b.r, used ? render::Colour{a.r, a.g, a.b, 230} : render::Colour{255, 236, 200, 60}, std::max(1.0f, std::round(ppp_)));
        const render::Colour gc{240, 226, 198, 255};
        const Icon icon = b.kind == 1 ? Icon::Stop : b.kind == 2 ? Icon::Deploy : b.kind == 3 ? Icon::Guard : b.kind == 4 ? Icon::Close : Icon::None;
        if (icon != Icon::None) {
            const float g = std::round(b.r.h * 0.78f);
            skin->glyph({b.r.x + (b.r.w - g) / 2, b.r.y + (b.r.h - g) / 2, g, g}, icon, gc);
        } else {
            const float sc = b.r.h * (b.kind == 5 ? 0.46f : 0.6f) / 10;
            const float tw = skin->textW(b.label, sc, true);
            skin->label(b.label, b.r.x + (b.r.w - tw) / 2, b.r.y + (b.r.h - skin->textH(sc)) / 2,
                        group && !used ? render::Colour{170, 160, 140, 200} : gc, sc, true);
        }
        skin->setAlpha(1.0f);
    }
    for (std::size_t i = 0; !skin && i < buttons_.size(); ++i) {
        const Btn& b = buttons_[i];
        const bool pressed = int(i) == pressedBtn_;
        const bool group = b.kind > 10;
        const bool used = (group && s.touchGroupUsed(b.kind - 10)) || (b.kind == 5 && s.classicPeek());
        ui.fill(b.r, pressed ? Colour{120, 82, 40, 220} : Colour{24, 16, 10, 150});
        ui.outline(b.r, used ? theme::kAccent : Colour{150, 102, 52, 180}, pressed ? 2.0f : 1.0f);
        if (b.kind == 4) {  // Deselect: an X
            const Colour c = theme::kText;
            SDL_SetRenderDrawColor(ui.renderer(), c.r, c.g, c.b, c.a);
            const float cx = b.r.x + b.r.w / 2, cy = b.r.y + b.r.h / 2, a = b.r.h * 0.2f;
            for (float o = -1.5f * ts; o <= 1.5f * ts; o += 0.5f) {
                SDL_RenderLine(ui.renderer(), cx - a + o, cy - a, cx + a + o, cy + a);
                SDL_RenderLine(ui.renderer(), cx - a + o, cy + a, cx + a + o, cy - a);
            }
            continue;
        }
        const float th = ui.textHeight(ts);
        ui.text(b.label, b.r.x + b.r.w / 2, b.r.y + (b.r.h - th) / 2, group && !used ? theme::kDim : theme::kText, ts,
                Align::Centre);
    }
    // The popup and the toast keep the tablet's text size on the phone (its buttons are small).
    const float pts = (skin ? std::max(unit_, 42 * ppp_) : unit_) / 48.0f;
    for (std::size_t i = 0; i < popup_.size(); ++i) {
        const PopupItem& p = popup_[i];
        ui.fill(p.r, int(i) == popupPressed_ ? Colour{120, 82, 40, 240} : Colour{30, 20, 13, 235});
        ui.outline(p.r, theme::kEdge, 1.5f);
        ui.text(p.label, p.r.x + p.r.w / 2, p.r.y + (p.r.h - ui.textHeight(pts)) / 2, theme::kText, pts, Align::Centre);
    }
    if (!toast_.empty() && SDL_GetTicks() < toastUntil_ && !buttons_.empty()) {
        const SDL_FRect& r = buttons_.front().r;
        ui.text(toast_, r.x, r.y - ui.textHeight(pts) - 8, theme::kAccent, pts);
    }
}

}  // namespace d2k::platform
