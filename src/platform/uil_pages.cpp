#include "platform/uil_pages.hpp"

#include "platform/movies.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::platform {

namespace {

bool hasBackground(const formats::UilPage& p) {
    for (const auto& w : p.widgets)
        if (w.kind == "BACKGROUND") return true;
    return false;
}

const formats::UilWidget* findButton(const formats::UilPage& p, const std::string& name) {
    for (const auto& w : p.widgets)
        if (w.kind == "BUTTON" && w.name == name) return &w;
    return nullptr;
}

}  // namespace

UilPageStack::UilPageStack(SDL_Renderer* r, Ui& ui, Settings& settings) : r_(r), ui_(ui), settings_(settings), res_(r) {}

UilPageStack::~UilPageStack() {
    if (textInput_) SDL_StopTextInput(SDL_GetRenderWindow(r_));
}

std::string UilPageStack::pageFile(const std::string& menu) const { return res_.menuFile(menu); }

// options.uil gets two rows of check boxes (not original; re/notes/campaign-flow.md §6.4):
// "Extended queue" / "Cheats", "Modern effects" / "HD art" and "Rally points", with the tick / cross art of
// setting.uil's check boxes, below the four value bars, which move closer together (16 px
// instead of 30) to make room.
const formats::UilPage* UilPageStack::page(const std::string& menu) const {
    if (menu != "OPTIONS") return res_.page(pageFile(menu));
    if (optionsBuilt_) return &optionsPage_;
    const formats::UilPage* base = res_.page(pageFile(menu));
    if (!base) return nullptr;
    const formats::UilWidget* label = base->find("gsText");
    if (!label) return base;
    optionsBuilt_ = true;
    optionsPage_ = *base;
    const formats::UilWidget labelW = *label;
    auto& ws = optionsPage_.widgets;
    static const char* const kRowNames[4][2] = {
        {"gsText", "gameSpeed"}, {"ssText", "scrollSpeed"}, {"cdvolText", "cdVol"}, {"sfxvolText", "fxVol"}};
    for (auto& w : ws) {
        for (int i = 0; i < 4; ++i)
            if (w.name == kRowNames[i][0] || w.name == kRowNames[i][1]) w.y = 118 + i * 16;
        if (w.name == "music" || w.name == "done") w.y = 247;
    }
    // Second check row: the Enhanced-mode presentation (docs/enhanced-graphics.md): "Modern
    // effects" (Settings::effects) and "HD art" (artPack upscaled / an art pack vs original).
    // Third row: "Rally points" (Settings::rallyPoints, game/rally.cpp).
    const char* const extra[] = {"extQueue", "cheats", "modernFx", "hdArt", "rally"};
    for (int i = 0; i < 5; ++i) {
        formats::UilWidget c;
        c.kind = "CHECKBOX";
        c.name = extra[i];
        c.x = 200 + (i % 2) * 118;
        c.y = 184 + (i / 2) * 16;
        c.w = c.h = 16;
        c.fields = {"JBoxOutLine", "JHighLightText", "Black", "tick", "cross", "SetValueBFTo gD2k TOGGLE"};
        ws.push_back(c);
        formats::UilWidget t = labelW;
        t.name = std::string(extra[i]) + "Text";
        t.x = c.x + 20;
        t.y = c.y + 1;
        t.w = i % 2 == 0 ? 96 : 60;
        t.h = 16;
        ws.push_back(t);
    }
    // Right of "Rally points": the Original-mode game resolution (Settings::resolution), cycled
    // like the front end's SETTINGS button (D2kCycleResolution); a plain bevelled button (no art),
    // so the in-game Game Settings page can change it while a battle runs (next frame).
    formats::UilWidget res;
    res.kind = "BUTTON";
    res.name = "resolution";
    res.x = 318;
    res.y = 216;
    res.w = 84;
    res.h = 16;
    res.fields = {"NoText", "LightRed", "VDarkRed", "Red", "DarkRed", "JRubberText", "JRubberTextDn", "None", "None"};
    res.fields.resize(formats::UilWidget::fixedFields("BUTTON"));
    res.fields.push_back("D2kCycleResolution");
    ws.push_back(res);
    return &optionsPage_;
}

void UilPageStack::cycleResolution() {
    const auto& choices = Settings::resolutionChoices();
    if (choices.empty()) return;
    const auto it = std::find(choices.begin(), choices.end(), settings_.resolution);
    const std::size_t i = it == choices.end() ? choices.size() - 1 : std::size_t(it - choices.begin());
    settings_.resolution = choices[(i + 1) % choices.size()];
}

void UilPageStack::optionsOverrides(UilOverrides& ov) const {
    ov.text["extQueueText"] = "Extended queue";
    ov.text["cheatsText"] = "Cheats";
    ov.text["modernFxText"] = "Modern effects";
    ov.text["hdArtText"] = "HD art";
    ov.text["rallyText"] = "Rally points";
    ov.text["resolution"] = "Res: " + Settings::resolutionLabel(settings_.resolution);
}

void UilPageStack::setStack(const std::vector<std::string>& menus) {
    movieMenu_.clear();
    stack_.clear();
    states_.clear();
    view_.reset();
    for (const std::string& m : menus) {
        stack_.push_back(m);
        enter(m, states_[m] = UilState{});
    }
    navigated_ = true;
}

void UilPageStack::jump(const std::string& menu) {
    navigated_ = true;
    if (special(menu)) return;
    stack_.push_back(menu);
    view_.reset();
    enter(menu, states_[menu] = UilState{});
    // A page that is just a movie: play it, then its end command (at once when it cannot play).
    if (const formats::UilPage* p = page(menu)) {
        for (const auto& w : p->widgets) {
            if (w.kind != "MOVIE") continue;
            // The main menu's "Replay intro" (MVE_REPLAYINTRO1/2) plays even with cutscenes off.
            const bool asked = menu.rfind("MVE_REPLAYINTRO", 0) == 0;
            if (movies_ && movies_->play(w.name, 0, asked)) {
                movieMenu_ = menu;
                movieCommands_ = w.commands();
                return;
            }
            run(menu, w.commands());
            return;
        }
    }
}

void UilPageStack::back() {
    navigated_ = true;
    if (!stack_.empty()) {
        states_.erase(stack_.back());
        stack_.pop_back();
    }
    view_.reset();
    if (stack_.empty()) { stackEmpty(); return; }
    // The original builds the page again (its Init commands read the globals again).
    enter(current(), states_[current()] = UilState{});
}

void UilPageStack::quitTo(const std::string& menu) {
    stack_.clear();
    states_.clear();
    jump(menu);
}

void UilPageStack::press(const std::string& menu, const std::string& button) {
    const formats::UilPage* p = page(menu);
    if (!p) return;
    if (const formats::UilWidget* w = findButton(*p, button)) run(menu, w->commands());
}

void UilPageStack::run(const std::string& menu, const std::vector<std::string>& cmds) {
    for (const std::string& c : cmds) {
        const auto sp = c.find(' ');
        const std::string op = c.substr(0, sp);
        const std::string arg = sp == std::string::npos ? std::string() : c.substr(sp + 1);
        if (op == "JumpToMenu") {
            jump(arg);
        } else if (op == "BackMenu") {
            back();
        } else if (op == "BackMenuAndJumpTo") {
            if (!stack_.empty()) {
                states_.erase(stack_.back());
                stack_.pop_back();
            }
            jump(arg);
        } else if (op == "QuitAndJumpToMenu") {
            quitTo(arg);
        } else if (op == "PlaySample") {
            if (arg != "BUTTON") sounds_.push_back({arg, false});  // BUTTON: the click already
        } else {
            command(menu, op, arg);
        }
        if (navigated_) return;  // the page this list belonged to is gone
    }
}

std::string UilPageStack::escapeButton(const std::string& menu) const {
    const formats::UilPage* p = page(menu);
    if (!p) return {};
    UilOverrides ov;
    const_cast<UilPageStack*>(this)->overrides(menu, ov);
    for (const char* n : {"cancel", "no", "resume", "done", "continue", "ok"}) {
        if (findButton(*p, n) && !ov.hidden.count(n) && !ov.disabled.count(n)) return n;
    }
    return {};
}

std::string UilPageStack::enterButton(const std::string& menu) const {
    const formats::UilPage* p = page(menu);
    if (!p) return {};
    UilOverrides ov;
    const_cast<UilPageStack*>(this)->overrides(menu, ov);
    for (const auto& w : p->widgets) {
        if (w.kind != "BUTTON" || ov.hidden.count(w.name) || ov.disabled.count(w.name)) continue;
        for (const auto& c : w.commands())
            if (c == "EnterActions") return w.name;
    }
    for (const char* n : {"ok", "yes", "yesAbort", "yesDelete", "start", "loadGame", "saveGame", "deleteGame", "done",
                          "continue", "resume"}) {
        if (findButton(*p, n) && !ov.hidden.count(n) && !ov.disabled.count(n)) return n;
    }
    return {};
}

void UilPageStack::drawPages(const UiStage& st) {
    navigated_ = false;
    if (!movieMenu_.empty() && !(movies_ && movies_->active())) {  // the movie is over
        const std::string menu = std::move(movieMenu_);
        const std::vector<std::string> cmds = std::move(movieCommands_);
        movieMenu_.clear();
        run(menu, cmds);
        if (stack_.empty()) return;
    }
    if (stack_.empty()) return;
    // Dialogs (pages without a background) are drawn over the pages below them.
    int first = int(stack_.size()) - 1;
    while (first > 0) {
        const formats::UilPage* p = page(stack_[std::size_t(first)]);
        if (p && hasBackground(*p)) break;
        --first;
    }
    const int top = int(stack_.size()) - 1;
    for (int i = first; i <= top; ++i) {
        const std::string menu = stack_[std::size_t(i)];
        const formats::UilPage* p = page(menu);
        if (!p) {
            if (i == top) {
                UilView::text(ui_, st, "Missing page " + pageFile(menu) + ".uil (Esc: back)", 20, 20, {220, 200, 160, 255});
                if (ui_.takeBack() || ui_.clicked()) back();
            }
            continue;
        }
        UilOverrides ov;
        overrides(menu, ov);
        UilState& s = states_[menu];
        if (i != top) {
            ov.inert = true;
            UilView passive;
            passive.draw(res_, ui_, *p, st, ov, &s);
            drawExtra(menu, st, false);
            continue;
        }
        const std::string released = view_.draw(res_, ui_, *p, st, ov, &s);
        const UilEvents ev = view_.events();
        drawExtra(menu, st, true);
        if (navigated_) break;  // drawExtra navigated (e.g. a click on the credits)
        // Edit boxes get SDL text input while focused.
        const bool wantText = !s.focus.empty();
        if (wantText != textInput_) {
            textInput_ = wantText;
            if (wantText) SDL_StartTextInput(SDL_GetRenderWindow(r_));
            else SDL_StopTextInput(SDL_GetRenderWindow(r_));
        }
        if (!ev.checked.empty() || !ev.changed.empty() || !ev.selected.empty() || !ev.activated.empty() ||
            !ev.edited.empty())
            widgetEvent(menu, ev, s);
        if (navigated_) break;
        if (!released.empty()) {
            press(menu, released);
        } else if (ui_.takeActivate()) {
            const std::string b = enterButton(menu);
            if (!b.empty()) {
                ui_.addClickSound();
                press(menu, b);
            }
        } else if (ui_.takeBack()) {
            const std::string b = escapeButton(menu);
            if (!b.empty()) {
                ui_.addClickSound();
                press(menu, b);
            }
        }
        break;
    }
    if (stack_.empty() && textInput_) {
        textInput_ = false;
        SDL_StopTextInput(SDL_GetRenderWindow(r_));
    }
}

// ---- OPTIONS (options.uil) -------------------------------------------------------------------
// gameSpeed  "15 100 5"  gLimitedModelPct -> Settings::gameSpeed (GameSpeed of dune2000.ini)
// scrollSpeed "4 40 2"   gScrollSize      -> Settings::scrollRate
// cdVol      "0 100 5"   gCDVolume        -> Settings::musicVolume (0..1)
// fxVol      "0 100 5"   gSFXVolume       -> Settings::soundVolume (0..1); PlaySample BUTTON

void UilPageStack::enterOptions(UilState& s) {
    s.values["gameSpeed"] = settings_.gameSpeed;
    s.values["scrollSpeed"] = settings_.scrollRate;
    s.values["cdVol"] = int(std::lround(settings_.musicVolume * 100));
    s.values["fxVol"] = int(std::lround(settings_.soundVolume * 100));
    s.checks["extQueue"] = settings_.extendedQueue();
    s.checks["cheats"] = settings_.cheats;
    s.checks["modernFx"] = settings_.effects == Settings::Effects::Modern;
    s.checks["hdArt"] = settings_.artPack != "original";
    s.checks["rally"] = settings_.rallyPoints;
}

void UilPageStack::applyOptions(const UilEvents& ev, UilState& s) {
    if (ev.changed == "gameSpeed") settings_.gameSpeed = std::clamp(s.values["gameSpeed"], Settings::kMinGameSpeed, Settings::kMaxGameSpeed);
    else if (ev.changed == "scrollSpeed") settings_.scrollRate = std::clamp(s.values["scrollSpeed"], Settings::kMinScrollRate, Settings::kMaxScrollRate);
    else if (ev.changed == "cdVol") settings_.musicVolume = std::clamp(s.values["cdVol"], 0, 100) / 100.0f;
    else if (ev.changed == "fxVol") {
        settings_.soundVolume = std::clamp(s.values["fxVol"], 0, 100) / 100.0f;
        ui_.addClickSound();  // "PlaySample BUTTON" on the SFX bar: hear the new volume
    }
    if (ev.checked == "extQueue")
        settings_.productionQueue = s.checks["extQueue"] ? Settings::ProductionQueue::Extended : Settings::ProductionQueue::Original;
    if (ev.checked == "cheats") settings_.cheats = s.checks["cheats"];
    if (ev.checked == "modernFx")
        settings_.effects = s.checks["modernFx"] ? Settings::Effects::Modern : Settings::Effects::Classic;
    if (ev.checked == "hdArt") settings_.artPack = s.checks["hdArt"] ? "upscaled" : "original";
    if (ev.checked == "rally") settings_.rallyPoints = s.checks["rally"];
}

std::vector<std::string> UilPageStack::wrapLines(const std::string& s, float w) const {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        std::string line, word;
        auto push = [&]() {
            if (word.empty()) return;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui_.textWidth(cand, 1) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        };
        for (char c : s.substr(start, nl - start)) {
            if (c == ' ') push();
            else word += c;
        }
        push();
        out.push_back(line);
        start = nl + 1;
    }
    return out;
}

}  // namespace d2k::platform
