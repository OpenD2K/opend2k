#include "platform/uil_view.hpp"

#include "platform/language.hpp"

#include "formats/io.hpp"
#include "platform/ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>

namespace d2k::platform {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isNone(const std::string& s) { return s.empty() || lower(s) == "none"; }

void hline(Ui& ui, const UiStage& st, float x, float y, float w, render::Colour c) { ui.fill(st.r(x, y, w, 1), c); }
void vline(Ui& ui, const UiStage& st, float x, float y, float h, render::Colour c) { ui.fill(st.r(x, y, 1, h), c); }

// Splits text into lines of at most `w` page pixels ('\n' breaks).
std::vector<std::string> wrapLines(Ui& ui, const std::string& s, float w) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(start, nl - start);
        std::string line, word;
        auto push = [&]() {
            if (word.empty()) return;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui.textWidth(cand, 1) > w) {
                lines.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        };
        for (char ch : para) {
            if (ch == ' ') push();
            else word += ch;
        }
        push();
        lines.push_back(line);
        start = nl + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

}  // namespace

UiStage UiStage::fit(SDL_Renderer* r, bool enhanced) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    UiStage st;
    const float fs = std::min(w / 640.0f, h / 400.0f);
    st.s = enhanced ? fs : std::max(1.0f, std::floor(fs));
    st.x = std::round((w - 640 * st.s) / 2);
    st.y = std::round((h - 400 * st.s) / 2);
    return st;
}

// ---- OriginalUi ------------------------------------------------------------------------

void OriginalUi::setData(const std::string& dataDir, const formats::StringTable* text) {
    images_.clear();
    pages_.clear();
    menus_.clear();
    colours_.clear();
    gfxDir_.clear();
    dataUiDir_.clear();
    text_ = text;
    if (dataDir.empty()) return;
    gfxDir_ = formats::findFileNoCase(dataDir, "UI_GFX");
    dataUiDir_ = formats::findFileNoCase(dataDir, "UI_DATA");
    if (dataUiDir_.empty()) return;
    try {
        if (auto p = formats::findFileNoCase(dataUiDir_, "menus.uib"); !p.empty()) {
            for (auto& [k, v] : formats::parseMenus(formats::readFile(p))) menus_[upper(k)] = v;
        }
        if (auto p = formats::findFileNoCase(dataUiDir_, "colours.uib"); !p.empty())
            colours_ = formats::parseUiColours(formats::readFile(p));
    } catch (const std::exception& e) {
        SDL_Log("UI tables: %s", e.what());
    }
}

void OriginalUi::setSmooth(bool smooth) {
    if (smooth == smooth_) return;
    smooth_ = smooth;
    images_.clear();
}

const render::Texture* OriginalUi::image(const std::string& name) {
    if (isNone(name) || gfxDir_.empty()) return nullptr;
    const std::string key = lower(name);
    auto it = images_.find(key);
    if (it == images_.end()) {
        auto t = std::make_unique<render::Texture>();
        // The language's UI_GFX (the Russian title / button art) before data/UI_GFX.
        for (const std::string& dir : {uiGfxOverlayDir(), gfxDir_}) {
            if (dir.empty() || *t) continue;
            for (const char* ext : {".tga", ".bmp"}) {
                if (formats::findFileNoCase(dir, name + ext).empty()) continue;
                *t = render::Texture::load(r_, dir, name + ext, smooth_, true);
                if (*t) break;
            }
        }
        it = images_.emplace(key, std::move(t)).first;
    }
    return *it->second ? it->second.get() : nullptr;
}

const render::Texture* OriginalUi::imageIn(const std::string& sub, const std::string& name) {
    if (gfxDir_.empty()) return nullptr;
    const std::string key = lower(sub + "/" + name);
    auto it = images_.find(key);
    if (it == images_.end()) {
        auto t = std::make_unique<render::Texture>();
        const std::string dir = formats::findFileNoCase(gfxDir_, sub);
        if (!dir.empty() && !formats::findFileNoCase(dir, name + ".bmp").empty())
            *t = render::Texture::load(r_, dir, name + ".bmp", smooth_, true);
        it = images_.emplace(key, std::move(t)).first;
    }
    return *it->second ? it->second.get() : nullptr;
}

const formats::UilPage* OriginalUi::page(const std::string& file) {
    if (dataUiDir_.empty() || file.empty()) return nullptr;
    const std::string key = lower(file);
    auto it = pages_.find(key);
    if (it == pages_.end()) {
        std::unique_ptr<formats::UilPage> p;
        const std::string path = formats::findFileNoCase(dataUiDir_, file + ".uil");
        if (!path.empty()) {
            try {
                p = std::make_unique<formats::UilPage>(formats::loadUil(path));
            } catch (const std::exception& e) {
                SDL_Log("%s: %s", path.c_str(), e.what());
            }
        }
        it = pages_.emplace(key, std::move(p)).first;
    }
    return it->second.get();
}

std::string OriginalUi::menuFile(const std::string& menu) const {
    auto it = menus_.find(upper(menu));
    return it == menus_.end() ? std::string() : it->second;
}

render::Colour OriginalUi::colour(const std::string& name, bool* transparent) const {
    auto it = colours_.find(name);
    const bool none = isNone(name) || lower(name) == "transparent" || it == colours_.end();
    if (transparent) *transparent = none;
    if (it == colours_.end()) return {0, 0, 0, 0};
    const std::uint32_t v = it->second;
    return {static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v), 255};
}

bool OriginalUi::hasText(const std::string& key) const { return text_ && text_->find(key); }

std::string OriginalUi::text(const std::string& key) const {
    if (!text_ || key == "NoText") return {};
    const std::string* v = text_->find(key);
    if (!v) return {};
    std::string out;
    out.reserve(v->size());
    for (unsigned char c : *v) {
        if (c == 0xAC) out += '\n';  // '¬' (Latin-1) = line break
        else if (c != '\r') out += static_cast<char>(c);
    }
    return out;
}

// ---- UilView ---------------------------------------------------------------------------

void UilView::text(Ui& ui, const UiStage& st, const std::string& s, float x, float y, render::Colour c, int align) {
    const SDL_FRect p = st.r(x, y, 0, 0);
    ui.text(s, p.x, p.y, c, st.s, align == 0 ? Align::Left : align == 1 ? Align::Centre : Align::Right, false);
}

int UilView::wrapped(Ui& ui, const UiStage& st, const std::string& s, float x, float y, float w, float lineH,
                     render::Colour c, int align) {
    const auto lines = wrapLines(ui, s, w);
    float yy = y;
    for (const auto& l : lines) {
        const float xx = align == 1 ? x + w / 2 : align == 2 ? x + w : x;
        text(ui, st, l, xx, yy, c, align);
        yy += lineH;
    }
    return static_cast<int>(lines.size());
}

void drawAnimation(OriginalUi& res, Ui& ui, const UiStage& st, const formats::UilWidget& w, Uint64 nowMs) {
    const std::string& spec = w.field(0);
    const auto sp = spec.find(' ');
    const std::string img = spec.substr(0, sp);
    const int fps = sp == std::string::npos ? 10 : std::max(1, std::atoi(spec.c_str() + sp + 1));
    const render::Texture* t = res.image(img);
    if (!t || w.w <= 0) return;
    const int frames = std::max(1, t->width() / w.w);
    const int f = static_cast<int>((nowMs * static_cast<Uint64>(fps) / 1000) % static_cast<Uint64>(frames));
    const SDL_FRect src{float(f * w.w), 0, float(w.w), float(std::min(w.h, t->height()))};
    const SDL_FRect dst = st.r(float(w.x), float(w.y), src.w, src.h);
    SDL_RenderTexture(ui.renderer(), t->get(), &src, &dst);
}

std::string UilView::draw(OriginalUi& res, Ui& ui, const formats::UilPage& page, const UiStage& st,
                          const UilOverrides& ov, UilState* state) {
    std::string released;
    events_ = {};
    const float mx = st.pageX(ui.mouseX()), my = st.pageY(ui.mouseY());
    const Uint64 now = SDL_GetTicks();
    float wheel = 0;
    if (state && !ov.inert) {
        for (const auto& w : page.widgets)
            if (w.kind.rfind("LISTBOX", 0) == 0) { wheel = ui.takeWheel(); break; }
    }
    for (const formats::UilWidget& w : page.widgets) {
        if (ov.hidden.count(w.name)) continue;
        if (w.kind == "BACKGROUND") {
            if (const render::Texture* t = res.image(w.name)) ui.image(t->get(), st.r(0, 0, 640, 400));
        } else if (w.kind == "GROUPBOX") {
            if (const render::Texture* t = res.image(w.field(2))) {
                ui.image(t->get(), st.r(float(w.x), float(w.y), float(t->width()), float(t->height())));
            } else {
                bool tb = false, tr = false;
                const render::Colour bg = res.colour(w.field(0), &tb), bd = res.colour(w.field(1), &tr);
                if (!tb) ui.fill(st.r(float(w.x), float(w.y), float(w.w), float(w.h)), bg);
                if (!tr) ui.outline(st.r(float(w.x), float(w.y), float(w.w), float(w.h)), bd, st.s);
            }
        } else if (w.kind == "TEXTBOX") {
            bool tb = false;
            const render::Colour bg = res.colour(w.field(0), &tb);
            if (!tb) ui.fill(st.r(float(w.x), float(w.y), float(w.w), float(w.h)), bg);
            std::string s;
            if (auto it = ov.text.find(w.name); it != ov.text.end()) {
                s = it->second;
            } else {
                s = res.text(w.field(4));
                for (const std::string& c : w.commands())
                    if (c.rfind("EnterText ", 0) == 0) s = res.text(c.substr(10));
            }
            const render::Colour fc = res.colour(w.field(3));
            const bool left = w.suffix.rfind("_L", 0) == 0;
            const bool right = w.suffix.rfind("_R", 0) == 0;
            const int align = left ? 0 : right ? 2 : 1;
            const float lineH = float(std::max(w.h, int(ui.textHeight(1))));
            const auto lines = wrapLines(ui, s, float(w.w));
            float yy = float(w.y);
            if (lines.size() == 1) yy += std::max(0.0f, (float(w.h) - ui.textHeight(1)) / 2);
            for (const auto& l : lines) {
                text(ui, st, l, left ? float(w.x) : right ? float(w.x + w.w) : float(w.x) + w.w / 2.0f, yy, fc, align);
                if (w.suffix.find("UL") != std::string::npos) {
                    bool th = false;
                    const render::Colour hc = res.colour(w.field(1), &th);
                    const float tw = ui.textWidth(l, 1);
                    const float x0 = left ? float(w.x) : right ? float(w.x + w.w) - tw : float(w.x) + (w.w - tw) / 2;
                    hline(ui, st, x0, yy + ui.textHeight(1), tw, th ? fc : hc);
                }
                yy += lineH;
            }
        } else if (w.kind == "BUTTON") {
            const bool disabled = ov.disabled.count(w.name) > 0;
            const bool inert = disabled || ov.inert;
            const render::Texture* up = res.image(w.field(7));
            const render::Texture* dn = res.image(w.field(8));
            float bw = float(w.w), bh = float(w.h);
            if (up) {
                bw = float(up->width());
                bh = float(up->height());
            }
            const bool over = mx >= w.x && mx < w.x + bw && my >= w.y && my < w.y + bh;
            if (!inert && over && ui.mousePressed()) pressed_ = w.name;
            const bool down = !inert && pressed_ == w.name && over && ui.mouseDown();
            if (!inert && ui.clicked() && pressed_ == w.name && over) {
                released = w.name;
                ui.addClickSound();
            }
            const float alpha = disabled ? 0.45f : 1.0f;
            const SDL_FRect r = st.r(float(w.x), float(w.y), bw, bh);
            if (up) {
                ui.image((down && dn ? dn : up)->get(), r, alpha);
            } else {
                bool tu = false, td = false, th = false, tl = false;
                const render::Colour cu = res.colour(w.field(3), &tu), cd = res.colour(w.field(4), &td);
                const render::Colour hi = res.colour(w.field(1), &th), lo = res.colour(w.field(2), &tl);
                if (!(down ? td : tu)) ui.fill(r, down ? cd : cu);
                const render::Colour a = down ? lo : hi, b = down ? hi : lo;
                if (!(down ? tl : th)) {
                    hline(ui, st, float(w.x), float(w.y), bw, a);
                    vline(ui, st, float(w.x), float(w.y), bh, a);
                }
                if (!(down ? th : tl)) {
                    hline(ui, st, float(w.x), float(w.y) + bh - 1, bw, b);
                    vline(ui, st, float(w.x) + bw - 1, float(w.y), bh, b);
                }
            }
            std::string label;
            if (auto it = ov.text.find(w.name); it != ov.text.end()) label = it->second;
            else label = res.text(w.field(0));
            // A key missing from Text.UIB (skill.uil's "Ok") shows as the key itself.
            if (label.empty() && !isNone(w.field(0)) && w.field(0) != "NoText" && !res.hasText(w.field(0)))
                label = w.field(0);
            if (!label.empty()) {
                render::Colour fc = res.colour(down ? w.field(6) : w.field(5));
                if (disabled) fc.a = 120;
                text(ui, st, label, float(w.x) + bw / 2, float(w.y) + std::round((bh - ui.textHeight(1)) / 2), fc, 1);
            }
        } else if (w.kind == "ANIMATION") {
            drawAnimation(res, ui, st, w, now);
        } else if (w.kind == "CHECKBOX") {
            drawCheckBox(res, ui, w, st, ov, state, mx, my);
        } else if (w.kind == "MULTISTATEBOX") {
            drawMultiState(res, ui, w, st, ov, state, mx, my);
        } else if (w.kind == "VALUEBAR") {
            drawValueBar(res, ui, w, st, ov, state, mx, my);
        } else if (w.kind == "LISTBOX" || w.kind == "LISTBOXEMPTYSLOT") {
            drawList(res, ui, w, st, ov, state, mx, my, wheel);
        } else if (w.kind == "EDITBOX" || w.kind == "EDITBOXFOCUS") {
            drawEdit(res, ui, w, st, ov, state, mx, my);
        }
    }
    if (ui.clicked() || (!ui.mouseDown() && !ui.mousePressed())) {
        pressed_.clear();
        drag_.clear();
    }
    events_.released = released;
    return released;
}

// ---- interactive widgets ---------------------------------------------------------------

namespace {

bool inside(float mx, float my, float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; }

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// "EnterText KEY" of a widget's commands -> the Text.UIB value ("" when none).
std::string enterText(const OriginalUi& res, const formats::UilWidget& w) {
    for (const std::string& c : w.commands())
        if (c.rfind("EnterText ", 0) == 0) return res.text(c.substr(10));
    return {};
}

}  // namespace

void UilView::valueRange(const formats::UilWidget& w, int& lo, int& hi, int& step, int& init) {
    const auto v = words(w.field(8));
    lo = v.size() > 0 ? std::atoi(v[0].c_str()) : 0;
    hi = v.size() > 1 ? std::atoi(v[1].c_str()) : 100;
    step = v.size() > 2 ? std::max(1, std::atoi(v[2].c_str())) : 1;
    init = v.size() > 3 ? std::atoi(v[3].c_str()) : lo;
    if (hi <= lo) hi = lo + 1;
}

int UilView::listRows(const formats::UilWidget& w, Ui& ui) {
    const int rowH = int(ui.textHeight(1)) + 1;
    return std::max(1, w.h / std::max(1, rowH));
}

void UilView::drawCheckBox(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st,
                           const UilOverrides& ov, UilState* state, float mx, float my) {
    const bool disabled = ov.disabled.count(w.name) > 0;
    const bool inert = disabled || ov.inert || !state;
    bool on = state ? state->checks[w.name] : false;
    const render::Texture* onImg = res.image(w.field(3));
    const render::Texture* offImg = res.image(w.field(4));
    float bw = float(w.w), bh = float(w.h);
    if (onImg) { bw = float(onImg->width()); bh = float(onImg->height()); }
    const bool over = inside(mx, my, float(w.x), float(w.y), bw, bh);
    if (!inert && over && ui.mousePressed()) pressed_ = w.name;
    if (!inert && ui.clicked() && pressed_ == w.name && over) {
        // CCheckBox: "SetValue.. TOGGLE" toggles, otherwise the box is checked and the boxes named
        // by "UnCheck X" are cleared (radio groups).
        bool toggle = false;
        std::vector<std::string> uncheck;
        for (const std::string& c : w.commands()) {
            if (c.find("TOGGLE") != std::string::npos) toggle = true;
            if (c.rfind("UnCheck ", 0) == 0) uncheck.push_back(c.substr(8));
        }
        on = toggle ? !on : true;
        state->checks[w.name] = on;
        for (const auto& n : uncheck) state->checks[n] = false;
        events_.checked = w.name;
        ui.addClickSound();
    }
    const float alpha = disabled ? 0.45f : 1.0f;
    const SDL_FRect r = st.r(float(w.x), float(w.y), bw, bh);
    const render::Texture* img = on ? onImg : offImg;
    if (img) {
        ui.image(img->get(), r, alpha);
    } else {
        bool tc = false, tb = false;
        const render::Colour c = res.colour(on ? w.field(1) : w.field(2), &tc);
        const render::Colour b = res.colour(w.field(0), &tb);
        if (!tc) ui.fill(r, c);
        if (on && !tb) ui.outline(r, b, st.s);
    }
    std::string label;
    if (auto it = ov.text.find(w.name); it != ov.text.end()) label = it->second;
    else label = enterText(res, w);
    if (!label.empty()) {
        render::Colour fc = res.colour(on ? w.field(1) : w.field(2));
        if (disabled) fc.a = 120;
        text(ui, st, label, float(w.x) + bw / 2, float(w.y) + std::round((bh - ui.textHeight(1)) / 2), fc, 1);
    }
}

void UilView::drawMultiState(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st,
                             const UilOverrides& ov, UilState* state, float mx, float my) {
    const bool inert = ov.disabled.count(w.name) > 0 || ov.inert || !state;
    const int n = std::max(1, std::atoi(w.field(1).c_str()));
    const auto gfx = words(w.field(3));
    int v = state ? std::clamp(state->values[w.name], 0, n - 1) : 0;
    const bool over = inside(mx, my, float(w.x), float(w.y), float(w.w), float(w.h));
    if (!inert && over && ui.mousePressed()) pressed_ = w.name;
    if (!inert && ui.clicked() && pressed_ == w.name && over) {
        v = (v + 1) % n;
        state->values[w.name] = v;
        events_.checked = w.name;
        ui.addClickSound();
    }
    if (v < int(gfx.size())) {
        if (const render::Texture* t = res.image(gfx[std::size_t(v)]))
            ui.image(t->get(), st.r(float(w.x), float(w.y), float(t->width()), float(t->height())));
    }
}

void UilView::drawValueBar(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st,
                           const UilOverrides& ov, UilState* state, float mx, float my) {
    const bool inert = ov.disabled.count(w.name) > 0 || ov.inert || !state;
    int lo = 0, hi = 100, step = 1, init = 0;
    valueRange(w, lo, hi, step, init);
    const render::Texture* region = res.image(w.field(6));
    const render::Texture* bar = res.image(w.field(7));
    // CValueBar 0x48c030: the size is the region picture's; the knob is a fifth of the width.
    const float bw = region ? float(region->width()) : float(w.w);
    const float bh = region ? float(region->height()) : float(w.h);
    const float kw = std::floor(bw / 5);
    int v = state ? state->values[w.name] : init;
    v = std::clamp(v, lo, hi);
    auto knobX = [&](int val) { return float(w.x) + std::floor((bw - kw) * float(val - lo) / float(hi - lo)); };
    const Uint64 now = SDL_GetTicks();
    if (!inert) {
        const bool over = inside(mx, my, float(w.x), float(w.y), bw, bh);
        const float kx = knobX(v);
        if (over && ui.mousePressed()) {
            pressed_ = w.name;
            if (mx >= kx && mx <= kx + kw) {
                drag_ = w.name;  // FUN_0048c640: grab the knob
                dragOffset_ = mx - kx;
            } else {
                repeatAt_ = 0;   // step at once, then every 200 ms while held (FUN_0048c450)
            }
        }
        int nv = v;
        if (drag_ == w.name && ui.mouseDown()) {
            const float t = std::clamp((mx - dragOffset_ - float(w.x)) / std::max(1.0f, bw - kw), 0.0f, 1.0f);
            nv = lo + int(std::lround(t * float(hi - lo) / float(step))) * step;
        } else if (pressed_ == w.name && drag_.empty() && ui.mouseDown() && over && now >= repeatAt_) {
            if (mx < kx) nv = v - step;
            else if (mx > kx + kw) nv = v + step;
            repeatAt_ = now + 200;
        }
        nv = std::clamp(nv, lo, hi);
        if (nv != v) {
            v = nv;
            state->values[w.name] = v;
            events_.changed = w.name;
        }
    }
    if (region) {
        ui.image(region->get(), st.r(float(w.x), float(w.y), bw, bh));
    } else {
        bool tb = false;
        const render::Colour bg = res.colour(w.field(0), &tb);
        if (!tb) ui.fill(st.r(float(w.x), float(w.y), bw, bh), bg);
    }
    const float kx = knobX(v);
    if (bar) {
        const SDL_FRect src{0, 0, std::min(kw, float(bar->width())), std::min(bh, float(bar->height()))};
        const SDL_FRect dst = st.r(kx, float(w.y), src.w, src.h);
        SDL_RenderTexture(ui.renderer(), bar->get(), &src, &dst);
    } else {
        bool ts = false;
        const render::Colour sc = res.colour(w.field(1), &ts);
        if (!ts) ui.fill(st.r(kx, float(w.y), kw, bh), sc);
    }
    if (w.suffix != "_NOVAL") {
        // FUN_0048cbf0: the value in a 40 px box right of the bar.
        bool tb = false;
        const render::Colour back = res.colour(w.field(5), &tb);
        if (!tb) ui.fill(st.r(float(w.x) + bw + 1, float(w.y), 40, bh), back);
        text(ui, st, std::to_string(v), float(w.x) + bw + 4, float(w.y) + std::round((bh - ui.textHeight(1)) / 2),
             res.colour(w.field(4)));
    }
}

void UilView::drawList(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st,
                       const UilOverrides& ov, UilState* state, float mx, float my, float wheel) {
    const bool inert = ov.disabled.count(w.name) > 0 || ov.inert || !state;
    static UilList empty;
    UilList& list = state ? state->lists[w.name] : empty;
    const render::Texture* up = res.image(w.field(10));
    const render::Texture* upDown = res.image(w.field(11));
    const render::Texture* dn = res.image(w.field(12));
    const render::Texture* dnDown = res.image(w.field(13));
    const render::Texture* slider = res.image(w.field(9));
    const float aw = up ? float(up->width()) : 16, ah = up ? float(up->height()) : 16;
    const float x = float(w.x), y = float(w.y), lw = float(w.w), lh = float(w.h);
    const float rowH = ui.textHeight(1) + 1;
    const int visible = listRows(w, ui);
    const int n = int(list.rows.size());
    const int maxTop = std::max(0, n - visible);
    bool tb = false, tf = false;
    const render::Colour bg = res.colour(w.field(0), &tb);
    const render::Colour fc = res.colour(w.field(1));
    const render::Colour frame = res.colour(w.field(2), &tf);
    const render::Colour hi = res.colour(w.field(3));
    if (!tb) ui.fill(st.r(x, y, lw, lh), bg);
    if (!tf) ui.outline(st.r(x, y, lw, lh), frame, st.s);
    const float textW = lw - aw - 6;
    const Uint64 now = SDL_GetTicks();
    const float trackY = y + ah, trackH = std::max(1.0f, lh - 2 * ah);
    const float knobH = n > visible ? std::max(8.0f, std::floor(trackH * float(visible) / float(n))) : trackH;
    auto knobY = [&]() { return trackY + (maxTop > 0 ? std::floor((trackH - knobH) * float(list.top) / float(maxTop)) : 0); };
    bool upHeld = false, dnHeld = false;
    if (!inert) {
        const bool overUp = inside(mx, my, x + lw - aw, y, aw, ah);
        const bool overDn = inside(mx, my, x + lw - aw, y + lh - ah, aw, ah);
        const bool overTrack = inside(mx, my, x + lw - aw, trackY, aw, trackH);
        const bool overRows = inside(mx, my, x, y, lw - aw, lh);
        if (ui.mousePressed()) {
            if (overUp) { pressed_ = w.name + "#up"; repeatAt_ = 0; }
            else if (overDn) { pressed_ = w.name + "#dn"; repeatAt_ = 0; }
            else if (overTrack) {
                const float ky = knobY();
                if (my >= ky && my < ky + knobH) { drag_ = w.name; dragOffset_ = my - ky; }
                else list.top += my < ky ? -visible : visible;
            } else if (overRows) {
                pressed_ = w.name;
            }
        }
        upHeld = pressed_ == w.name + "#up" && ui.mouseDown() && overUp;
        dnHeld = pressed_ == w.name + "#dn" && ui.mouseDown() && overDn;
        if ((upHeld || dnHeld) && now >= repeatAt_) {
            list.top += upHeld ? -1 : 1;
            repeatAt_ = now + (repeatAt_ == 0 ? 400 : 120);
            ui.addClickSound();
        }
        if (drag_ == w.name && ui.mouseDown() && maxTop > 0) {
            const float t = std::clamp((my - dragOffset_ - trackY) / std::max(1.0f, trackH - knobH), 0.0f, 1.0f);
            list.top = int(std::lround(t * float(maxTop)));
        }
        if (wheel != 0 && overRows) list.top -= int(wheel) * 3;
        // Touch: a vertical drag on the rows scrolls them (row by row, the remainder carried).
        if (const float t = ui.touchScroll(st.r(x, y, lw - aw, lh)); t != 0) {
            touchRows_ -= t / std::max(1.0f, rowH * st.s);
            const int whole = int(touchRows_);
            list.top += whole;
            touchRows_ -= float(whole);
        }
        if (list.selectable && ui.clicked() && pressed_ == w.name && overRows) {
            const int row = list.top + int((my - y - 2) / rowH);
            if (row >= 0 && row < n) {
                const bool dbl = lastClickList_ == w.name && lastClickRow_ == row && now - lastClick_ < 450;
                list.sel = row;
                events_.selected = w.name;
                if (dbl) events_.activated = w.name;
                lastClickList_ = w.name;
                lastClickRow_ = row;
                lastClick_ = now;
            }
        }
        // Keyboard: Up/Down move the selection (or scroll a text list).
        if (const int d = ui.vertical(); d != 0 && n > 0) {
            if (list.selectable) {
                list.sel = std::clamp(list.sel + d, 0, n - 1);
                if (list.sel < list.top) list.top = list.sel;
                if (list.sel >= list.top + visible) list.top = list.sel - visible + 1;
                events_.selected = w.name;
            } else {
                list.top += d;
            }
        }
    }
    list.top = std::clamp(list.top, 0, maxTop);
    for (int i = 0; i < visible && list.top + i < n; ++i) {
        const int row = list.top + i;
        const float ry = y + 2 + i * rowH;
        if (list.selectable && row == list.sel) ui.fill(st.r(x + 1, ry - 1, lw - aw - 2, rowH), hi);
        render::Colour c = fc;
        if (row < int(list.white.size()) && list.white[std::size_t(row)]) c = res.colour("White");
        if (row < int(list.dim.size()) && list.dim[std::size_t(row)]) c.a = 110;
        text(ui, st, ui.ellipsize(list.rows[std::size_t(row)], textW, 1), x + 3, ry, c);
    }
    // Scroll bar: the arrows top and bottom, the slider picture between.
    if (slider) {
        const SDL_FRect src{0, 0, std::min(aw, float(slider->width())), std::min(knobH, float(slider->height()))};
        const SDL_FRect dst = st.r(x + lw - aw, knobY(), src.w, knobH);
        SDL_RenderTexture(ui.renderer(), slider->get(), &src, &dst);
    }
    if (const render::Texture* t = upHeld && upDown ? upDown : up) ui.image(t->get(), st.r(x + lw - aw, y, aw, ah));
    if (const render::Texture* t = dnHeld && dnDown ? dnDown : dn)
        ui.image(t->get(), st.r(x + lw - aw, y + lh - ah, aw, ah));
}

void UilView::drawEdit(OriginalUi& res, Ui& ui, const formats::UilWidget& w, const UiStage& st,
                       const UilOverrides& ov, UilState* state, float mx, float my) {
    const bool inert = ov.disabled.count(w.name) > 0 || ov.inert || !state;
    bool tb = false;
    const render::Colour bar = res.colour(w.field(0), &tb);
    const render::Colour cursor = res.colour(w.field(1));
    const render::Colour fc = res.colour(w.field(2));
    if (!tb) ui.fill(st.r(float(w.x), float(w.y), float(w.w), float(w.h)), bar);
    std::string s = state ? state->edits[w.name] : std::string();
    const bool focused = state && state->focus == w.name && !ov.inert;
    if (!inert) {
        if (inside(mx, my, float(w.x), float(w.y), float(w.w), float(w.h)) && ui.mousePressed()) state->focus = w.name;
        if (focused) {
            std::string typed = ui.takeTextInput();
            int bs = ui.takeBackspaces();
            // Backspace / Space are also the Ui's back / activate keys: typing, not commands.
            if (bs > 0) ui.takeBack();
            if (typed.find(' ') != std::string::npos) ui.takeActivate();
            bool changed = false;
            for (; bs > 0 && !s.empty(); --bs) { s.pop_back(); changed = true; }
            for (char c : typed) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 32 || (u >= 127 && u < 0xC0)) continue;  // printable ASCII + accented / Cyrillic letters
                if (ui.textWidth(s + c, 1) > float(w.w) - 10 || s.size() >= 40) break;
                s += c;
                changed = true;
            }
            if (changed) {
                state->edits[w.name] = s;
                events_.edited = w.name;
            }
        }
    }
    const float ty = float(w.y) + std::round((float(w.h) - ui.textHeight(1)) / 2);
    text(ui, st, s, float(w.x) + 3, ty, fc);
    if (focused && (SDL_GetTicks() / 400) % 2 == 0)
        ui.fill(st.r(float(w.x) + 4 + ui.textWidth(s, 1), ty, 1, ui.textHeight(1)), cursor);
}

}  // namespace d2k::platform
