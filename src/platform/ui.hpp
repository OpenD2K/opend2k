#pragma once
// Small immediate-mode UI toolkit for the menus and the pause screen, drawn with SDL shapes and
// the original FONT.FNT (via render::TextRenderer) — or SDL's built-in debug font before any
// game data is available. The modern (Enhanced) pages switch it to the embedded TrueType UI font
// (Play, render/ttf_font.hpp: UTF-8 + Cyrillic, like the in-game panel) with a ModernScope; the
// original .uil pages keep the bitmap font. Mouse and keyboard: Up/Down/Tab move the focus, Left/Right change
// choices and sliders (or move the focus), Enter/Space activate, Esc is reported to the page.
// Touch (SDL's mouse events with which == SDL_TOUCH_MOUSEID: the iPad's menus): a tap is a click;
// a vertical drag past a few points scrolls the list / scroll area / grid it started in (with
// momentum after the finger lifts) and cancels the press, so nothing gets selected or activated;
// a horizontal drag moves a slider. List rows are selected when a touch is released, not pressed.

#include "render/text.hpp"

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace d2k::render {
class TtfFont;
}

namespace d2k::platform {

using render::Colour;

// Dune 2000 palette shared with the in-game skin (render/ui_skin.*): warm dark browns / leather,
// bronze-copper metal, sand-amber highlights.
namespace theme {
inline constexpr Colour kText{238, 224, 196, 255};
inline constexpr Colour kDim{176, 156, 124, 255};
inline constexpr Colour kAccent{240, 180, 84, 255};
inline constexpr Colour kTitle{255, 214, 140, 255};
inline constexpr Colour kDisabled{118, 104, 88, 255};
inline constexpr Colour kPanel{30, 20, 13, 214};
inline constexpr Colour kEdge{150, 102, 52, 255};
inline constexpr Colour kBad{240, 110, 90, 255};
}  // namespace theme

enum class Align { Left, Centre, Right };
enum class ButtonStyle { Normal, Primary, Subtle };

struct ListRow {
    std::string text;
    std::string detail;  // right-aligned, dimmer
    bool enabled = true;
};

class Ui {
public:
    explicit Ui(SDL_Renderer* r);
    ~Ui();
    SDL_Renderer* renderer() const { return r_; }

    // Font for all text; null = SDL's 8x8 debug font.
    void setFont(render::TextRenderer* t) { font_ = t; }
    bool hasFont() const { return font_ != nullptr || modernOn(); }

    // Modern pages: text in the embedded TrueType font (Play) while a ModernScope is alive.
    // Strings are UTF-8 (our tr() labels) or game strings in the active code page (both draw).
    struct ModernScope {
        Ui& ui;
        bool prev;
        explicit ModernScope(Ui& u, bool on = true) : ui(u), prev(u.modern_) { u.modern_ = on; }
        ~ModernScope() { ui.modern_ = prev; }
        ModernScope(const ModernScope&) = delete;
        ModernScope& operator=(const ModernScope&) = delete;
    };
    bool modern() const { return modern_; }
    // Bold text (TrueType only) while alive: titles, primary buttons.
    struct BoldScope {
        Ui& ui;
        bool prev;
        explicit BoldScope(Ui& u, bool on = true) : ui(u), prev(u.bold_) { u.bold_ = on; }
        ~BoldScope() { ui.bold_ = prev; }
        BoldScope(const BoldScope&) = delete;
        BoldScope& operator=(const BoldScope&) = delete;
    };

    // Input: feed every SDL event, then per frame beginFrame() .. widgets .. endFrame().
    void handleEvent(const SDL_Event& e);
    void beginFrame();
    void endFrame();
    void resetFocus(int id = 0) { focus_ = id; skipClamp_ = 2; }
    int focus() const { return focus_; }
    // Widget ids handed out so far this frame (the next widget gets this id), and the choice / toggle
    // under the mouse this frame (-1 none) with the step a click there would make (-1 the left arrow,
    // +1 the value or right arrow): the Graphics page previews the row being pointed at.
    int widgetCount() const { return count_; }
    int hoverId() const { return hoverId_; }
    int hoverDir() const { return hoverDir_; }
    // Keys the page handles itself (consumed when read).
    bool takeBack() { bool b = back_; back_ = false; return b; }
    bool takeActivate() { bool b = activate_; activate_ = false; return b; }
    // Any left click released this frame (not the end of a touch scroll).
    bool clicked() const { return mouseReleased_ && !touchScrolled_; }
    // Widget activations since the last call: the original UI plays "BUTTON" for each
    // (FUN_00437600 -> FUN_004762f0).
    int takeClickSounds() { int n = clicks_; clicks_ = 0; return n; }
    bool anyKeyOrClick() const { return mouseReleased_ || anyKey_; }
    // Raw mouse state for pages that draw their own controls (the original .uil pages).
    bool mouseDown() const { return mouseDown_; }
    bool mousePressed() const { return mousePressed_; }  // left button went down this frame
    void addClickSound() { ++clicks_; }
    // Typed text since the last call (SDL text input events) and Backspace presses; only
    // delivered while SDL text input is started (SDL_StartTextInput) by the page.
    std::string takeTextInput() { std::string t; t.swap(typed_); return t; }
    int takeBackspaces() { int n = backspaces_; backspaces_ = 0; return n; }
    // Mouse wheel steps this frame (consumed) and Up/Down presses (not consumed), for the
    // original pages' list boxes.
    float takeWheel() { float w = wheel_; wheel_ = 0; return w; }
    int vertical() const { return vert_; }
    // Touch scrolling for widgets that scroll by themselves (cheat grids, the original pages'
    // list boxes): the finger's vertical travel this frame in window points (positive = finger
    // moved down = content follows it down, so `scroll -= touchScroll(r)`), when the drag (or its
    // momentum) started inside `r`; consumed by the first caller (inner widgets come first).
    float touchScroll(const SDL_FRect& r);
    bool touchActive() const { return touchDown_; }  // a touch is pressing right now
    float mouseX() const { return mx_; }
    float mouseY() const { return my_; }

    // Text (scale multiplies the font's native size).
    float textWidth(const std::string& s, float scale = 1) const;
    float textHeight(float scale = 1) const;
    float text(const std::string& s, float x, float y, Colour c = theme::kText, float scale = 1, Align a = Align::Left,
               bool shadow = true);
    // Word-wrapped text inside r (clipped); returns the total text height (for scrolling).
    float wrapped(const std::string& s, const SDL_FRect& r, Colour c, float scale, float scrollY = 0, bool draw = true);
    // The largest of `scales` (descending) at which `s` fits into r, else the last one.
    float fitScale(const std::string& s, const SDL_FRect& r, std::initializer_list<float> scales);
    // Text shortened with "..." to fit `maxW`.
    std::string ellipsize(const std::string& s, float maxW, float scale) const;

    // Shapes.
    void fill(const SDL_FRect& r, Colour c);
    void outline(const SDL_FRect& r, Colour c, float t = 1);
    void gradient(const SDL_FRect& r, Colour top, Colour bottom);
    void panel(const SDL_FRect& r, Colour bg = theme::kPanel);
    void image(SDL_Texture* t, const SDL_FRect& dst, float alpha = 1);

    // Focusable widgets; each returns true when activated / changed.
    bool button(const SDL_FRect& r, const std::string& label, ButtonStyle style = ButtonStyle::Normal, bool enabled = true,
                float textScale = 1);
    // Invisible focusable region with a highlight frame (e.g. house emblems); `hot` = hovered/focused.
    bool hotspot(const SDL_FRect& r, bool* hot = nullptr);
    bool toggle(const SDL_FRect& r, const std::string& label, bool& value);
    bool slider(const SDL_FRect& r, const std::string& label, float& value);
    // Integer slider lo..hi (Left/Right step by 1); `valueText` is shown on the right.
    bool slider(const SDL_FRect& r, const std::string& label, int& value, int lo, int hi, const std::string& valueText);
    bool choice(const SDL_FRect& r, const std::string& label, int& index, const std::vector<std::string>& options);
    // Scrollable list; Up/Down change the selection while it has the focus. Returns the row
    // activated by double-click or Enter, else -1.
    int list(const SDL_FRect& r, const std::vector<ListRow>& rows, int& selected, float& scroll, float textScale = 1);

    // Scrollable region: widgets drawn between beginScroll() and endScroll() are clipped to
    // `view` and only react to the mouse inside it. The page lays its content out from the
    // returned y (view.y - scroll); `contentH` is the full content height. The mouse wheel
    // over the view scrolls, a scroll bar is drawn in the `barW` pixels right of the view (it
    // can be dragged), and a widget that gets the keyboard focus is scrolled into view.
    float beginScroll(const SDL_FRect& view, float& scroll, float contentH, float barW = 6);
    void endScroll();

    float textScale = 1;  // scale for buttons and widget labels (set by the page from the window size)
    float bodyScale = 1;  // scale for list rows and running text
    float minRowH = 0;    // list rows at least this tall (output pixels; phones: a touch target)

    // Integer text scales for a window of w x h pixels (the font is a 14 px bitmap font).
    static float labelScaleFor(int w, int h);
    static float bodyScaleFor(int w, int h);

private:
    int nextId() { return count_++; }
    bool modernOn() const;
    render::TtfFont* ttf() const;
    float ttfPx(float scale) const;
    bool hovered(const SDL_FRect& r) const;
    void noteFocus(int id, const SDL_FRect& r);  // scroll areas: remember the focused widget's rect
    bool pressAndRelease(int id, bool hover);
    void focusRing(const SDL_FRect& r);
    bool sliderImpl(const SDL_FRect& r, const std::string& label, float& value, float keyStep, const std::string& valueText);

    SDL_Renderer* r_;
    render::TextRenderer* font_ = nullptr;
    std::unique_ptr<render::TtfFont> ttfRegular_, ttfBold_;
    bool modern_ = false, bold_ = false;
    // Persistent state.
    int focus_ = 0, lastCount_ = 0, pressId_ = -1, dragId_ = -1;
    int skipClamp_ = 0;  // frames to keep a freshly set focus unclamped (the page changed)
    Uint64 lastClickTicks_ = 0;
    int lastClickRow_ = -1;
    // Per-frame input.
    float mx_ = 0, my_ = 0, wheel_ = 0;
    bool mouseMoved_ = false, mousePressed_ = false, mouseReleased_ = false, mouseDown_ = false;
    int vert_ = 0, horiz_ = 0, tab_ = 0, page_ = 0;
    bool activate_ = false, back_ = false, anyKey_ = false;
    int clicks_ = 0;
    int hoverId_ = -1, hoverDir_ = 1;
    int count_ = 0;
    // Scroll area (beginScroll .. endScroll).
    bool scrolling_ = false, scrollFocusSeen_ = false, keyFocusMoved_ = false, scrollDrag_ = false;
    SDL_FRect scrollView_{}, scrollFocus_{};
    float* scrollRef_ = nullptr;
    float scrollContentH_ = 0, scrollBarW_ = 6;
    SDL_Rect scrollOldClip_{};
    bool scrollHadClip_ = false;
    std::string typed_;
    int backspaces_ = 0;
    // Touch (see the top): the press came from a finger; Pending until it moves a few points,
    // then Scroll (vertical) or Horizontal.
    enum class TouchMode { Pending, Scroll, Horizontal };
    bool touchDown_ = false, touchPress_ = false, touchScrolled_ = false, touchUsed_ = false;
    TouchMode touchMode_ = TouchMode::Pending;
    float tx0_ = 0, ty0_ = 0, tLastY_ = 0, touchAccum_ = 0, touchPx_ = 0, touchVel_ = 0, momentum_ = 0;
    Uint64 tLastMove_ = 0, lastFrameNs_ = 0;
};

}  // namespace d2k::platform
