#pragma once
// Draws the original front-end pages from their UI_DATA/*.uil scripts (formats/uil.hpp) with
// the UI_GFX pictures, FONT.FNT and the named colours of colours.uib — a small subset of the
// original CUIManager (re/notes/campaign-flow.md §1): BACKGROUND, GROUPBOX (picture or
// coloured box), TEXTBOX (left/centred, underlined, word-wrapped), BUTTON (up/down pictures
// or bevelled colour box, text in the font up/down colour), ANIMATION (picture strip at its
// fps, looping), CHECKBOX (on/off pictures or colour box; radio groups via "UnCheck X",
// "... TOGGLE" toggles), VALUEBAR[_NOVAL] (region picture + knob = 1/5 of the width, CValueBar
// 0x48c030 / 0x48c640 / 0x48cbf0), MULTISTATEBOX (one picture per state), LISTBOX (rows,
// highlight, arrow buttons and slider picture), EDITBOX[FOCUS] (typed text). MOVIE is not drawn.
//
// The 640x400 page is drawn on a "stage": Original graphics = the largest integer scale that
// fits, nearest filtering; Enhanced = the largest fractional scale, smooth filtering.

#include "formats/uib.hpp"
#include "formats/uil.hpp"
#include "render/image.hpp"
#include "render/text.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::platform {

class Ui;

struct UiStage {
    float x = 0, y = 0, s = 1;
    SDL_FRect r(float x0, float y0, float w, float h) const { return {x + x0 * s, y + y0 * s, w * s, h * s}; }
    // Page coordinates of a window point.
    float pageX(float wx) const { return (wx - x) / s; }
    float pageY(float wy) const { return (wy - y) / s; }
    static UiStage fit(SDL_Renderer* r, bool enhanced);
};

// Shared resources of the original pages: pictures, page scripts, colours, text.
class OriginalUi {
public:
    explicit OriginalUi(SDL_Renderer* r) : r_(r) {}
    void setData(const std::string& dataDir, const formats::StringTable* text);
    bool ok() const { return !gfxDir_.empty(); }
    void setSmooth(bool smooth);

    // UI_GFX/<name>.tga, else .bmp (magenta = transparent); cached; null when missing.
    const render::Texture* image(const std::string& name);
    // UI_GFX/<sub>/<name>.bmp (the territory pictures in Regions/).
    const render::Texture* imageIn(const std::string& sub, const std::string& name);
    // UI_DATA/<file>.uil (file stem, any case); cached; null when missing.
    const formats::UilPage* page(const std::string& file);
    // menus.uib: page file stem of a menu name ("GLOBE" -> "Globe"); "" when unknown.
    std::string menuFile(const std::string& menu) const;
    // colours.uib; `transparent` = Transparent / NONE / unknown.
    render::Colour colour(const std::string& name, bool* transparent = nullptr) const;
    // Text.UIB value with '¬' -> '\n'; "" when missing (and for "NoText").
    std::string text(const std::string& key) const;
    bool hasText(const std::string& key) const;

private:
    SDL_Renderer* r_;
    std::string gfxDir_, dataUiDir_;
    const formats::StringTable* text_ = nullptr;
    bool smooth_ = false;
    std::unordered_map<std::string, std::unique_ptr<render::Texture>> images_;
    std::unordered_map<std::string, std::unique_ptr<formats::UilPage>> pages_;
    std::unordered_map<std::string, std::string> menus_;
    std::unordered_map<std::string, std::uint32_t> colours_;
};

// What a page draw may change per frame.
struct UilOverrides {
    std::map<std::string, std::string> text;  // widget name -> text (replaces the text key)
    std::set<std::string> hidden;             // widgets not drawn
    std::set<std::string> disabled;           // buttons drawn dimmed, not clickable
    bool inert = false;                       // drawn as is, no input (a page under a dialog)
};

// Values of the interactive widgets of one page, owned by the page's controller. The view
// reads and updates them; the controller maps them onto our settings / data.
struct UilList {
    std::vector<std::string> rows;
    std::vector<bool> dim;  // rows drawn dimmed (optional, by index)
    std::vector<bool> white;  // rows drawn in "White" instead of the list's font colour (optional;
                              // GetGameSaves FUN_00475a00: battle saves white, campaign-map saves not)
    int sel = -1;
    int top = 0;            // first visible row
    bool selectable = true; // false: a text list (the in-game briefing)
};

struct UilState {
    std::map<std::string, bool> checks;          // CHECKBOX
    std::map<std::string, int> values;           // VALUEBAR value, MULTISTATEBOX state
    std::map<std::string, UilList> lists;        // LISTBOX / LISTBOXEMPTYSLOT
    std::map<std::string, std::string> edits;    // EDITBOX text
    std::string focus;                           // edit box receiving the keyboard ("" = none)
};

// What happened during the last draw (names of the widgets).
struct UilEvents {
    std::string released;   // BUTTON released over itself
    std::string checked;    // CHECKBOX / MULTISTATEBOX clicked (state already updated)
    std::string changed;    // VALUEBAR value changed
    std::string selected;   // list row clicked
    std::string activated;  // list row double-clicked
    std::string edited;     // edit box text changed
};

class UilView {
public:
    // Draws `page` and handles its buttons; returns the name of the button released this frame
    // ("" = none). Buttons act on release over the same button, like the original.
    // `state` holds the interactive widgets' values (null: those widgets are drawn empty).
    std::string draw(OriginalUi& res, Ui& ui, const formats::UilPage& page, const UiStage& st,
                     const UilOverrides& ov = {}, UilState* state = nullptr);
    const UilEvents& events() const { return events_; }
    void reset() { pressed_.clear(); drag_.clear(); }
    // VALUEBAR "min max step initial" (4th field).
    static void valueRange(const formats::UilWidget& w, int& lo, int& hi, int& step, int& init);
    // List rows visible in a LISTBOX of height h.
    static int listRows(const formats::UilWidget& w, Ui& ui);
    // Text drawing helpers in page coordinates (FONT.FNT tinted, no shadow).
    static void text(Ui& ui, const UiStage& st, const std::string& s, float x, float y, render::Colour c, int align = 0);
    // Word-wrapped text (lines `lineH` apart), returns the number of lines.
    static int wrapped(Ui& ui, const UiStage& st, const std::string& s, float x, float y, float w, float lineH,
                       render::Colour c, int align = 0);

private:
    void drawCheckBox(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st, const UilOverrides& ov,
                      UilState* state, float mx, float my);
    void drawMultiState(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st, const UilOverrides& ov,
                        UilState* state, float mx, float my);
    void drawValueBar(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st, const UilOverrides& ov,
                      UilState* state, float mx, float my);
    void drawList(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st, const UilOverrides& ov,
                  UilState* state, float mx, float my, float wheel);
    void drawEdit(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st, const UilOverrides& ov,
                  UilState* state, float mx, float my);

    std::string pressed_;  // widget under the mouse when the left button went down
    std::string drag_;     // value bar knob / list slider being dragged
    float dragOffset_ = 0;
    Uint64 repeatAt_ = 0;  // next auto-repeat step of a held value bar / list arrow (200 ms)
    Uint64 lastClick_ = 0;
    std::string lastClickList_;
    int lastClickRow_ = -1;
    float touchRows_ = 0;  // touch scrolling: rows travelled but not yet applied
    UilEvents events_;
};

// Executes the image strip animation of an ANIMATION widget field "<image> <fps>".
void drawAnimation(OriginalUi& res, Ui& ui, const UiStage& st, const formats::UilWidget& w, Uint64 nowMs);

}  // namespace d2k::platform
