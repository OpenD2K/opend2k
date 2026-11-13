#pragma once
// A CUIManager-like stack of original front-end pages (UI_DATA/*.uil through UilView), shared
// by the front end (platform/original_menus.hpp) and the in-game Esc menu
// (platform/ingame_pages.hpp). re/notes/campaign-flow.md §6 lists the pages and handlers.
//
// - The stack holds menu names (menus.uib); the top page is interactive. A page without a
//   BACKGROUND is a dialog: the pages below it are drawn too (inert), down to the last page
//   with a background — like the original, which draws dialogs over the current screen.
// - Navigation commands run here (JumpToMenu, BackMenu, BackMenuAndJumpTo, QuitAndJumpToMenu,
//   PlaySample); a page that only holds a MOVIE plays it (platform/movies.hpp, when set and
//   possible) and then runs its end command. Everything else goes to command().
// - Keyboard: Enter presses the page's EnterActions button (else its OK/Yes/Done/... button),
//   Esc its Cancel/No/Resume/... button.
// - The OPTIONS page (options.uil: game speed, scroll rate, music and SFX volume) maps onto our
//   Settings here, because both the front end and the game show it.

#include "platform/campaign_screens.hpp"
#include "platform/uil_view.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace d2k::platform {

class Ui;
class Movies;
struct Settings;

class UilPageStack {
public:
    UilPageStack(SDL_Renderer* r, Ui& ui, Settings& settings);
    virtual ~UilPageStack();

    bool empty() const { return stack_.empty(); }
    std::string current() const { return stack_.empty() ? std::string() : stack_.back(); }
    const std::vector<std::string>& stack() const { return stack_; }
    // Replaces the stack (each page's state is set up again).
    void setStack(const std::vector<std::string>& menus);
    std::vector<MenuSound> takeSounds() { std::vector<MenuSound> s; s.swap(sounds_); return s; }
    OriginalUi& res() { return res_; }
    void setMovies(Movies* m) { movies_ = m; }

protected:
    // One frame: the stack's pages on `st` (clears nothing; the caller draws what is below).
    void drawPages(const UiStage& st);

    // Hooks for the concrete page sets.
    virtual std::string pageFile(const std::string& menu) const;
    // A jump target handled outside the page stack (e.g. HOUSESELECTION -> campaign screens);
    // return true when handled.
    virtual bool special(const std::string& menu) { (void)menu; return false; }
    virtual void enter(const std::string& menu, UilState& s) { (void)menu; (void)s; }  // page became current
    virtual void overrides(const std::string& menu, UilOverrides& ov) { (void)menu; (void)ov; }
    // Non-navigation command of a button (or "Check:<name>" / "Change:<name>" / "Select:<name>"
    // / "Activate:<name>" for checkbox, value bar and list events).
    virtual void command(const std::string& menu, const std::string& op, const std::string& arg) {
        (void)menu; (void)op; (void)arg;
    }
    virtual void widgetEvent(const std::string& menu, const UilEvents& ev, UilState& s) {
        (void)menu; (void)ev; (void)s;
    }
    // Extra drawing on top of a page (score table, credits); `top` = the interactive page.
    virtual void drawExtra(const std::string& menu, const UiStage& st, bool top) { (void)menu; (void)st; (void)top; }
    virtual std::string escapeButton(const std::string& menu) const;
    virtual std::string enterButton(const std::string& menu) const;
    // The stack became empty (BackMenu on the last page).
    virtual void stackEmpty() {}

    // Navigation.
    void jump(const std::string& menu);
    void back();
    void quitTo(const std::string& menu);
    void run(const std::string& menu, const std::vector<std::string>& cmds);
    void press(const std::string& menu, const std::string& button);  // run a button's commands
    UilState& state(const std::string& menu) { return states_[menu]; }
    virtual const formats::UilPage* page(const std::string& menu) const;

    // OPTIONS page (options.uil): Settings <-> the value bars.
    // Our two check rows (Extended production queue, Cheats) get their labels here.
    void enterOptions(UilState& s);
    void applyOptions(const UilEvents& ev, UilState& s);
    void optionsOverrides(UilOverrides& ov) const;
    // D2kCycleResolution: the next Settings::resolutionChoices() entry (Original-mode resolution).
    void cycleResolution();
    // Word-wraps `s` (paragraphs at '\n') to lines of at most `w` page pixels in the UI font.
    std::vector<std::string> wrapLines(const std::string& s, float w) const;

    SDL_Renderer* r_;
    Ui& ui_;
    Settings& settings_;
    mutable OriginalUi res_;
    UilView view_;
    std::vector<std::string> stack_;
    std::map<std::string, UilState> states_;
    std::vector<MenuSound> sounds_;
    bool navigated_ = false;  // a command changed the stack during this frame
    Movies* movies_ = nullptr;
    std::string movieMenu_;                  // MOVIE page now playing
    std::vector<std::string> movieCommands_;  // ... and its end commands
    bool textInput_ = false;  // SDL text input started for an edit box
    mutable formats::UilPage optionsPage_;  // options.uil with our check rows (page())
    mutable bool optionsBuilt_ = false;
};

}  // namespace d2k::platform
