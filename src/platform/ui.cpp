#include "platform/ui.hpp"
#include "platform/pointer.hpp"

#include "formats/language.hpp"
#include "render/ttf_font.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::platform {

namespace {
SDL_FColor fc(Colour c) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f}; }
Colour mix(Colour a, Colour b, float t) {
    auto m = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}
// TrueType line height per text-scale unit: matches the original bitmap font's line box.
constexpr float kTtfPxPerScale = 12.5f;
}  // namespace

Ui::Ui(SDL_Renderer* r) : r_(r) {
    std::size_t n = 0;
    if (const unsigned char* d = render::embeddedFont("play_regular", n)) ttfRegular_ = std::make_unique<render::TtfFont>(r, d, n);
    if (const unsigned char* d = render::embeddedFont("play_bold", n)) ttfBold_ = std::make_unique<render::TtfFont>(r, d, n);
    if (ttfRegular_ && !ttfRegular_->ok()) ttfRegular_.reset();
    if (ttfBold_ && !ttfBold_->ok()) ttfBold_.reset();
}

Ui::~Ui() = default;

bool Ui::modernOn() const { return modern_ && ttfRegular_; }

render::TtfFont* Ui::ttf() const {
    if (!modernOn()) return nullptr;
    return bold_ && ttfBold_ ? ttfBold_.get() : ttfRegular_.get();
}

float Ui::ttfPx(float scale) const { return std::max(8.0f, std::round(kTtfPxPerScale * scale)); }

float Ui::labelScaleFor(int w, int h) {
    return std::max(1.0f, std::round(std::min(w / 640.0f, h / 400.0f) * 0.75f));
}

float Ui::bodyScaleFor(int w, int h) {
    return std::max(1.0f, std::round(std::min(w / 640.0f, h / 400.0f) * 0.5f));
}

// ---- input -------------------------------------------------------------------------------

namespace {
constexpr float kTouchSlop = 10;  // points a finger travels before a press becomes a drag
}

void Ui::handleEvent(const SDL_Event& e) {
    // Touch distances / speeds are in points; positions arrive in output pixels.
    const float ppp = (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_UP) ? pixelsPerPoint(r_) : 1.0f;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mx_ = e.motion.x;
        my_ = e.motion.y;
        mouseMoved_ = true;
        if (touchDown_ && e.motion.which == SDL_TOUCH_MOUSEID) {
            const float dx = mx_ - tx0_, dy = my_ - ty0_;
            if (touchMode_ == TouchMode::Pending) {
                if (std::abs(dy) > kTouchSlop * ppp && std::abs(dy) >= std::abs(dx)) {
                    touchMode_ = TouchMode::Scroll;  // the press is cancelled: nothing activates
                    pressId_ = -1;
                    dragId_ = -1;
                    scrollDrag_ = false;
                    tLastY_ = my_;
                    touchVel_ = 0;
                } else if (std::abs(dx) > kTouchSlop * ppp) {
                    touchMode_ = TouchMode::Horizontal;
                }
            }
            if (touchMode_ == TouchMode::Scroll) {
                const Uint64 now = SDL_GetTicksNS();
                const float d = my_ - tLastY_;
                const double dt = tLastMove_ ? double(now - tLastMove_) / 1e9 : 0;
                if (dt > 0.0005) touchVel_ = 0.6f * touchVel_ + 0.4f * float(d / dt);  // pixels per second
                touchAccum_ += d;
                tLastY_ = my_;
                tLastMove_ = now;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        mx_ = e.button.x;
        my_ = e.button.y;
        if (e.button.button == SDL_BUTTON_LEFT) {
            mousePressed_ = true;
            mouseDown_ = true;
            touchDown_ = touchPress_ = e.button.which == SDL_TOUCH_MOUSEID;
            touchMode_ = TouchMode::Pending;
            touchScrolled_ = false;
            momentum_ = 0;  // a new touch stops a flick
            if (touchDown_) {
                tx0_ = mx_;
                ty0_ = my_;
                tLastMove_ = 0;
                touchUsed_ = true;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        mx_ = e.button.x;
        my_ = e.button.y;
        if (e.button.button == SDL_BUTTON_LEFT) {
            mouseReleased_ = true;
            mouseDown_ = false;
            if (touchDown_ && touchMode_ == TouchMode::Scroll) {
                touchScrolled_ = true;  // clicked() stays false for this release
                const bool fresh = tLastMove_ && SDL_GetTicksNS() - tLastMove_ < 100'000'000ull;
                momentum_ = fresh && std::abs(touchVel_) > 150 * ppp ? std::clamp(touchVel_, -4000.0f * ppp, 4000.0f * ppp) : 0;
            }
            touchDown_ = false;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: wheel_ += e.wheel.y; break;
    case SDL_EVENT_TEXT_INPUT:
        // Typed UTF-8 -> the bytes the game font draws (CP1252 / CP1251, formats/language.hpp).
        if (e.text.text) typed_ += formats::lang::toCodepage(e.text.text, formats::lang::activeCodepage());
        break;
    case SDL_EVENT_KEY_DOWN:
        anyKey_ = true;
        switch (e.key.key) {
        case SDLK_UP: --vert_; break;
        case SDLK_DOWN: ++vert_; break;
        case SDLK_LEFT: --horiz_; break;
        case SDLK_RIGHT: ++horiz_; break;
        case SDLK_PAGEUP: page_ -= 1; break;
        case SDLK_PAGEDOWN: page_ += 1; break;
        case SDLK_TAB: tab_ += (e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: activate_ = true; break;
        case SDLK_ESCAPE: back_ = true; break;
        case SDLK_BACKSPACE:
            back_ = true;
            ++backspaces_;
            break;
        default: break;
        }
        break;
    default: break;
    }
}

void Ui::beginFrame() {
    count_ = 0;
    hoverId_ = -1;
    // Touch scrolling this frame: the finger's travel since the last frame, or the flick's momentum.
    const Uint64 now = SDL_GetTicksNS();
    const float dt = lastFrameNs_ ? std::min(0.1f, float(double(now - lastFrameNs_) / 1e9)) : 0;
    lastFrameNs_ = now;
    touchPx_ = touchAccum_;
    touchAccum_ = 0;
    if (momentum_ != 0 && !touchDown_) {
        touchPx_ += momentum_ * dt;
        momentum_ *= std::exp(-3.5f * dt);
        if (std::abs(momentum_) < 30) momentum_ = 0;
    }
    if (lastCount_ > 0 && skipClamp_ == 0) focus_ = std::clamp(focus_, 0, lastCount_ - 1);
}

void Ui::endFrame() {
    const int n = count_;
    if (skipClamp_ > 0) --skipClamp_;
    keyFocusMoved_ = false;
    if (n > 0) {
        const int delta = vert_ + horiz_ + tab_;
        if (delta) {
            focus_ = ((focus_ + delta) % n + n) % n;
            keyFocusMoved_ = true;  // a scroll area brings the new focus into view next frame
        }
    }
    if (!mouseDown_) scrollDrag_ = false;
    lastCount_ = n;
    if (mouseReleased_) { pressId_ = -1; dragId_ = -1; }
    if (mouseReleased_) touchScrolled_ = false;
    if (!touchDown_ && mouseReleased_) touchPress_ = false;
    touchPx_ = 0;  // unconsumed travel is dropped
    mouseMoved_ = mousePressed_ = mouseReleased_ = false;
    wheel_ = 0;
    vert_ = horiz_ = tab_ = page_ = 0;
    activate_ = back_ = anyKey_ = false;
    typed_.clear();
    backspaces_ = 0;
}

bool Ui::hovered(const SDL_FRect& r) const {
    if (scrolling_ && !(mx_ >= scrollView_.x && mx_ < scrollView_.x + scrollView_.w && my_ >= scrollView_.y &&
                        my_ < scrollView_.y + scrollView_.h))
        return false;
    return mx_ >= r.x && mx_ < r.x + r.w && my_ >= r.y && my_ < r.y + r.h;
}

float Ui::touchScroll(const SDL_FRect& r) {
    if (touchPx_ == 0) return 0;
    if (!(tx0_ >= r.x && tx0_ < r.x + r.w && ty0_ >= r.y && ty0_ < r.y + r.h)) return 0;
    if (scrolling_ && !(tx0_ >= scrollView_.x && tx0_ < scrollView_.x + scrollView_.w && ty0_ >= scrollView_.y &&
                        ty0_ < scrollView_.y + scrollView_.h))
        return 0;
    const float px = touchPx_;
    touchPx_ = 0;
    return px;
}

void Ui::noteFocus(int id, const SDL_FRect& r) {
    if (scrolling_ && focus_ == id) {
        scrollFocus_ = r;
        scrollFocusSeen_ = true;
    }
}

float Ui::beginScroll(const SDL_FRect& view, float& scroll, float contentH, float barW) {
    scrolling_ = true;
    scrollFocusSeen_ = false;
    scrollView_ = view;
    scrollRef_ = &scroll;
    scrollContentH_ = contentH;
    scrollBarW_ = barW;
    scroll = std::clamp(scroll, 0.f, std::max(0.f, contentH - view.h));
    scrollHadClip_ = SDL_RenderClipEnabled(r_);
    SDL_GetRenderClipRect(r_, &scrollOldClip_);
    // Focus rings are drawn 2 px outside a widget: keep them visible at the edges.
    const SDL_Rect clip{int(view.x) - 3, int(view.y), int(std::ceil(view.w)) + 6, int(std::ceil(view.h))};
    SDL_SetRenderClipRect(r_, &clip);
    return view.y - scroll;
}

void Ui::endScroll() {
    if (!scrolling_) return;
    SDL_SetRenderClipRect(r_, scrollHadClip_ ? &scrollOldClip_ : nullptr);
    scrolling_ = false;
    float& scroll = *scrollRef_;
    const SDL_FRect& v = scrollView_;
    const float maxScroll = std::max(0.f, scrollContentH_ - v.h);
    const bool in = mx_ >= v.x && mx_ < v.x + v.w + scrollBarW_ + 4 && my_ >= v.y && my_ < v.y + v.h;
    if (in && wheel_ != 0) {  // (lists inside the area take the wheel first)
        scroll -= wheel_ * std::max(24.f, v.h / 8);
        wheel_ = 0;
    }
    if (touchPx_ != 0 && tx0_ >= v.x && tx0_ < v.x + v.w + scrollBarW_ + 4 && ty0_ >= v.y && ty0_ < v.y + v.h) {
        scroll -= touchPx_;  // (lists inside took theirs already)
        touchPx_ = 0;
        if (scroll <= 0 || scroll >= maxScroll) momentum_ = 0;  // a flick stops at the ends
    }
    if (keyFocusMoved_ && scrollFocusSeen_) {  // keyboard focus: scroll it into view
        const float margin = 6;
        if (scrollFocus_.y < v.y) scroll -= v.y - scrollFocus_.y + margin;
        else if (scrollFocus_.y + scrollFocus_.h > v.y + v.h) scroll += scrollFocus_.y + scrollFocus_.h - (v.y + v.h) + margin;
    }
    if (maxScroll > 0) {
        const SDL_FRect track{v.x + v.w + 2, v.y, scrollBarW_, v.h};
        const float th = std::max(20.f, v.h * v.h / scrollContentH_);
        const bool overTrack = mx_ >= track.x - 2 && mx_ < track.x + track.w + 2 && my_ >= track.y && my_ < track.y + track.h;
        if (overTrack && mousePressed_) scrollDrag_ = true;
        if (scrollDrag_ && mouseDown_) scroll = (my_ - v.y - th / 2) / std::max(1.f, v.h - th) * maxScroll;
        scroll = std::clamp(scroll, 0.f, maxScroll);
        fill(track, Colour{0, 0, 0, 120});
        fill({track.x + 1, v.y + (v.h - th) * (scroll / maxScroll), track.w - 2, th},
             scrollDrag_ || overTrack ? Colour{240, 184, 80, 230} : Colour{200, 140, 60, 200});
    }
    scroll = std::clamp(scroll, 0.f, maxScroll);
}

bool Ui::pressAndRelease(int id, bool hover) {
    if (hover && mouseMoved_) focus_ = id;
    if (hover && mousePressed_) { pressId_ = id; focus_ = id; }
    return hover && mouseReleased_ && pressId_ == id;
}

// ---- text --------------------------------------------------------------------------------

float Ui::textWidth(const std::string& s, float scale) const {
    if (render::TtfFont* f = ttf()) return f->width(s, ttfPx(scale));
    if (font_) return font_->width(s, scale);
    return float(s.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
}

float Ui::textHeight(float scale) const {
    if (render::TtfFont* f = ttf()) return f->lineHeight(ttfPx(scale));
    if (font_) return font_->height(scale);
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
}

float Ui::text(const std::string& s, float x, float y, Colour c, float scale, Align a, bool shadow) {
    const float w = textWidth(s, scale);
    if (a == Align::Centre) x -= w / 2;
    else if (a == Align::Right) x -= w;
    x = std::round(x);
    y = std::round(y);
    if (render::TtfFont* f = ttf()) {
        const float px = ttfPx(scale);
        if (shadow) {
            const float o = std::max(1.0f, std::round(px / 14));
            f->draw(s, x + o, y + o, px, Colour{0, 0, 0, static_cast<unsigned char>(c.a * 0.75f)});
        }
        return f->draw(s, x, y, px, c);
    }
    if (font_) return font_->draw(s, x, y, c, scale, shadow);
    float sx = 1, sy = 1;
    SDL_GetRenderScale(r_, &sx, &sy);
    SDL_SetRenderScale(r_, scale, scale);
    if (shadow) {
        SDL_SetRenderDrawColor(r_, 0, 0, 0, c.a * 3 / 4);
        SDL_RenderDebugText(r_, x / scale + 1, y / scale + 1, s.c_str());
    }
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(r_, x / scale, y / scale, s.c_str());
    SDL_SetRenderScale(r_, sx, sy);
    return w;
}

float Ui::wrapped(const std::string& s, const SDL_FRect& r, Colour c, float scale, float scrollY, bool draw) {
    const float lineH = textHeight(scale) + 3 * scale;
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(start, nl - start);
        std::string line, word;
        auto pushWord = [&]() {
            if (word.empty()) return;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && textWidth(cand, scale) > r.w) {
                lines.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        };
        for (char ch : para) {
            if (ch == ' ') pushWord();
            else word += ch;
        }
        pushWord();
        lines.push_back(line);
        start = nl + 1;
    }
    if (!draw) return float(lines.size()) * lineH;
    SDL_Rect old;
    const bool hadClip = SDL_RenderClipEnabled(r_);
    SDL_GetRenderClipRect(r_, &old);
    const SDL_Rect clip{int(r.x), int(r.y), int(std::ceil(r.w)) + 2, int(std::ceil(r.h))};
    SDL_SetRenderClipRect(r_, &clip);
    float y = r.y - scrollY;
    for (const auto& l : lines) {
        if (y + lineH >= r.y && y < r.y + r.h) text(l, r.x, y, c, scale);
        y += lineH;
    }
    SDL_SetRenderClipRect(r_, hadClip ? &old : nullptr);
    return float(lines.size()) * lineH;
}

float Ui::fitScale(const std::string& s, const SDL_FRect& r, std::initializer_list<float> scales) {
    float last = 1;
    for (float sc : scales) {
        last = sc;
        if (wrapped(s, r, theme::kText, sc, 0, false) <= r.h) return sc;
    }
    return last;
}

std::string Ui::ellipsize(const std::string& s, float maxW, float scale) const {
    if (textWidth(s, scale) <= maxW) return s;
    std::string t = s;
    // UTF-8 (our labels): drop whole characters so the rest stays valid UTF-8.
    bool utf8 = true;
    for (std::size_t i = 0; i < s.size() && utf8;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) utf8 = false;
        for (int k = 1; k < n && utf8; ++k) utf8 = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        i += n ? n : 1;
    }
    while (!t.empty() && textWidth(t + "...", scale) > maxW) {
        if (utf8 && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) {
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        }
        if (!t.empty()) t.pop_back();  // the character (or its UTF-8 lead byte)
    }
    return t + "...";
}

// ---- shapes ------------------------------------------------------------------------------

void Ui::fill(const SDL_FRect& r, Colour c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r_, &r);
}

void Ui::outline(const SDL_FRect& r, Colour c, float t) {
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.y + r.h - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void Ui::gradient(const SDL_FRect& r, Colour top, Colour bottom) {
    const SDL_FColor a = fc(top), b = fc(bottom);
    const SDL_Vertex v[4] = {{{r.x, r.y}, a, {0, 0}},
                             {{r.x + r.w, r.y}, a, {0, 0}},
                             {{r.x + r.w, r.y + r.h}, b, {0, 0}},
                             {{r.x, r.y + r.h}, b, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

void Ui::panel(const SDL_FRect& r, Colour bg) {
    gradient(r, bg, Colour{static_cast<unsigned char>(bg.r / 2), static_cast<unsigned char>(bg.g / 2),
                           static_cast<unsigned char>(bg.b / 2), bg.a});
    outline(r, Colour{0, 0, 0, 160}, 1);
    outline({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, Colour{theme::kEdge.r, theme::kEdge.g, theme::kEdge.b, 150}, 1);
    fill({r.x + 2, r.y + 2, r.w - 4, 1}, Colour{255, 220, 160, 30});
}

void Ui::image(SDL_Texture* t, const SDL_FRect& dst, float alpha) {
    if (!t) return;
    SDL_SetTextureAlphaModFloat(t, alpha);
    SDL_RenderTexture(r_, t, nullptr, &dst);
    SDL_SetTextureAlphaModFloat(t, 1);
}

void Ui::focusRing(const SDL_FRect& r) {
    outline({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, Colour{theme::kAccent.r, theme::kAccent.g, theme::kAccent.b, 90}, 1);
    outline(r, theme::kAccent, 1);
}

// ---- widgets -----------------------------------------------------------------------------

bool Ui::button(const SDL_FRect& r, const std::string& label, ButtonStyle style, bool enabled, float ts) {
    const int id = nextId();
    noteFocus(id, r);
    const bool hover = enabled && hovered(r);
    bool hit = enabled && pressAndRelease(id, hover);
    const bool focused = focus_ == id;
    if (focused && enabled && activate_) { hit = true; activate_ = false; }
    const bool down = hover && mouseDown_ && pressId_ == id;
    Colour top{58, 42, 28, 225}, bottom{30, 21, 14, 225};
    if (style == ButtonStyle::Primary) { top = {170, 104, 34, 240}; bottom = {104, 58, 18, 240}; }
    if (style == ButtonStyle::Subtle) { top = {30, 24, 18, 170}; bottom = {18, 14, 10, 170}; }
    if (focused || hover) { top = mix(top, Colour{255, 200, 120, 255}, 0.18f); bottom = mix(bottom, Colour{255, 190, 100, 255}, 0.12f); }
    if (down) std::swap(top, bottom);
    if (!enabled) { top = {34, 30, 27, 180}; bottom = {24, 21, 19, 180}; }
    gradient(r, top, bottom);
    outline(r, Colour{0, 0, 0, 180});
    fill({r.x + 1, r.y + 1, r.w - 2, 1}, Colour{255, 230, 180, static_cast<unsigned char>(enabled ? 50 : 15)});
    if (focused && enabled) focusRing(r);
    const float scale = textScale * ts;
    const Colour tc = !enabled ? theme::kDisabled : (focused || hover) ? Colour{255, 240, 210, 255} : theme::kText;
    text(label, r.x + r.w / 2, r.y + (r.h - textHeight(scale)) / 2 + (down ? 1 : 0), tc, scale, Align::Centre);
    if (hit) ++clicks_;
    return hit;
}

bool Ui::hotspot(const SDL_FRect& r, bool* hot) {
    const int id = nextId();
    noteFocus(id, r);
    const bool hover = hovered(r);
    bool hit = pressAndRelease(id, hover);
    if (focus_ == id && activate_) { hit = true; activate_ = false; }
    if (hot) *hot = hover || focus_ == id;
    if (focus_ == id || hover) focusRing(r);
    if (hit) ++clicks_;
    return hit;
}

bool Ui::toggle(const SDL_FRect& r, const std::string& label, bool& value) {
    const int id = nextId();
    noteFocus(id, r);
    const bool hover = hovered(r);
    bool hit = pressAndRelease(id, hover);
    const bool focused = focus_ == id;
    if (focused && activate_) { hit = true; activate_ = false; }
    if (hover) hoverId_ = id, hoverDir_ = 1;
    if (focused && horiz_) { hit = true; horiz_ = 0; }
    if (hit) {
        value = !value;
        ++clicks_;
    }
    if (focused || hover) fill(r, Colour{255, 200, 120, 22});
    if (focused) focusRing(r);
    const float s = textScale;
    // Switch on the right, "On"/"Off" left of it, the label (shortened to fit) left of that.
    const float sw = 44 * s, sh = std::min(r.h - 8, 20.f * s);
    const SDL_FRect track{r.x + r.w - sw - 10 * s, r.y + (r.h - sh) / 2, sw, sh};
    const char* onText = modern_ ? render::tr("On") : "On";
    const char* offText = modern_ ? render::tr("Off") : "Off";
    const float valueW = std::max(textWidth(onText, s), textWidth(offText, s));
    text(ellipsize(label, std::max(0.f, track.x - 16 * s - valueW - (r.x + 10 * s)), s), r.x + 10 * s,
         r.y + (r.h - textHeight(s)) / 2, theme::kText, s);
    gradient(track, value ? Colour{190, 120, 40, 255} : Colour{50, 42, 36, 255},
             value ? Colour{130, 76, 22, 255} : Colour{34, 28, 24, 255});
    outline(track, Colour{0, 0, 0, 200});
    const SDL_FRect knob{value ? track.x + track.w - sh + 2 : track.x + 2, track.y + 2, sh - 4, sh - 4};
    gradient(knob, Colour{250, 236, 210, 255}, Colour{190, 170, 140, 255});
    text(value ? onText : offText, track.x - 8 * s, r.y + (r.h - textHeight(s)) / 2, value ? theme::kAccent : theme::kDim, s,
         Align::Right);
    return hit;
}

bool Ui::slider(const SDL_FRect& r, const std::string& label, float& value) {
    char buf[16];
    SDL_snprintf(buf, sizeof buf, "%d%%", int(std::lround(value * 100)));
    return sliderImpl(r, label, value, 0.05f, buf);
}

bool Ui::slider(const SDL_FRect& r, const std::string& label, int& value, int lo, int hi, const std::string& valueText) {
    if (hi <= lo) return false;
    float t = float(std::clamp(value, lo, hi) - lo) / float(hi - lo);
    const int old = value;
    if (sliderImpl(r, label, t, 1.0f / float(hi - lo), valueText)) value = lo + int(std::lround(t * float(hi - lo)));
    return value != old;
}

bool Ui::sliderImpl(const SDL_FRect& r, const std::string& label, float& value, float keyStep, const std::string& valueText) {
    const int id = nextId();
    noteFocus(id, r);
    const bool hover = hovered(r);
    const float s = textScale;
    const float valueW = std::max(54 * s, textWidth(valueText, s) + 16 * s);
    const SDL_FRect track{r.x + r.w * 0.48f, r.y + r.h / 2 - 3 * s, std::max(8 * s, r.w * 0.52f - 10 * s - valueW), 6 * s};
    if (hover && mouseMoved_) focus_ = id;
    if (hover && mousePressed_) { focus_ = id; dragId_ = id; pressId_ = id; }
    const bool focused = focus_ == id;
    const float old = value;
    // Touch: the knob follows a horizontal drag, or jumps to a tap when the finger lifts (a
    // vertical drag scrolls the page instead).
    if (dragId_ == id && mouseDown_ && (!touchPress_ || touchMode_ == TouchMode::Horizontal))
        value = std::clamp((mx_ - track.x) / track.w, 0.f, 1.f);
    if (touchPress_ && mouseReleased_ && pressId_ == id && hover && touchMode_ != TouchMode::Scroll)
        value = std::clamp((mx_ - track.x) / track.w, 0.f, 1.f);
    if (focused && horiz_) { value = std::clamp(value + keyStep * float(horiz_), 0.f, 1.f); horiz_ = 0; }
    if (focused || hover) fill(r, Colour{255, 200, 120, 22});
    if (focused) focusRing(r);
    text(ellipsize(label, std::max(0.f, track.x - 12 * s - (r.x + 10 * s)), s), r.x + 10 * s,
         r.y + (r.h - textHeight(s)) / 2, theme::kText, s);
    fill(track, Colour{30, 24, 20, 255});
    outline(track, Colour{0, 0, 0, 200});
    gradient({track.x, track.y, track.w * value, track.h}, Colour{230, 160, 60, 255}, Colour{160, 96, 30, 255});
    const float kx = track.x + track.w * value, kh = 16 * s;
    const SDL_FRect knob{kx - 4 * s, r.y + (r.h - kh) / 2, 8 * s, kh};
    gradient(knob, Colour{250, 236, 210, 255}, Colour{190, 170, 140, 255});
    outline(knob, Colour{0, 0, 0, 200});
    text(valueText, r.x + r.w - 10 * s, r.y + (r.h - textHeight(s)) / 2, theme::kAccent, s, Align::Right);
    return value != old;
}

bool Ui::choice(const SDL_FRect& r, const std::string& label, int& index, const std::vector<std::string>& options) {
    const int id = nextId();
    noteFocus(id, r);
    const bool hover = hovered(r);
    const float s = textScale;
    const int n = int(options.size());
    if (n == 0) return false;
    const float bw = (r.w < 360 * s ? 17 : 22) * s;  // slimmer arrows in half-width rows
    const SDL_FRect right{r.x + r.w - bw - 6 * s, r.y + 4, bw, r.h - 8};
    // The value column is 38 % of the row (aligned across full rows); a narrow (half) row whose
    // label would not fit gives the label room first, down to what the widest value needs.
    float valueW = r.w * 0.38f;
    {
        const float room = right.x - bw - 8 * s - (r.x + 10 * s);  // label + value
        const float labelW = textWidth(label, s);
        if (labelW + valueW > room) {
            float widest = 0;
            for (const auto& o : options) widest = std::max(widest, textWidth(o, s) + 10 * s);
            valueW = std::min({valueW, widest, std::max(room - labelW, room * 0.42f)});
        }
    }
    const SDL_FRect left{right.x - valueW - bw, r.y + 4, bw, r.h - 8};
    int delta = 0;
    if (hover && mouseMoved_) focus_ = id;
    if (hover && mousePressed_) { focus_ = id; pressId_ = id; }
    if (mouseReleased_ && pressId_ == id && hover) {
        if (hovered(left)) delta = -1;
        else delta = +1;  // clicking the value or the right arrow advances
    }
    const bool focused = focus_ == id;
    if (focused && horiz_) { delta = horiz_; horiz_ = 0; }
    if (focused && activate_) { delta = 1; activate_ = false; }
    if (hover) hoverId_ = id, hoverDir_ = hovered(left) ? -1 : 1;
    const int old = index;
    if (delta) {
        index = ((index + delta) % n + n) % n;
        ++clicks_;
    }
    if (focused || hover) fill(r, Colour{255, 200, 120, 22});
    if (focused) focusRing(r);
    text(ellipsize(label, std::max(0.f, left.x - 8 * s - (r.x + 10 * s)), s), r.x + 10 * s,
         r.y + (r.h - textHeight(s)) / 2, theme::kText, s);
    for (int k = 0; k < 2; ++k) {
        const SDL_FRect& b = k ? right : left;
        const bool bh = hovered(b);
        gradient(b, bh ? Colour{110, 72, 34, 255} : Colour{58, 42, 28, 255}, Colour{30, 21, 14, 255});
        outline(b, Colour{0, 0, 0, 180});
        text(k ? ">" : "<", b.x + b.w / 2, b.y + (b.h - textHeight(s)) / 2, theme::kText, s, Align::Centre);
    }
    const std::string v = ellipsize(options[std::clamp(index, 0, n - 1)], valueW - 8 * s, s);
    text(v, left.x + bw + valueW / 2, r.y + (r.h - textHeight(s)) / 2, theme::kAccent, s, Align::Centre);
    return index != old;
}

int Ui::list(const SDL_FRect& r, const std::vector<ListRow>& rows, int& selected, float& scroll, float ts) {
    const int id = nextId();
    noteFocus(id, r);
    const float s = bodyScale * ts;
    const float rowH = std::max(minRowH, std::round(textHeight(s) + 10 * bodyScale));
    const int n = int(rows.size());
    const bool hover = hovered(r);
    const bool focusedBefore = focus_ == id;
    int activated = -1;
    if (hover && mousePressed_) { focus_ = id; pressId_ = id; }
    const bool focused = focus_ == id;
    const int visible = std::max(1, int(r.h / rowH));
    bool keyMoved = false;  // follow the selection only when the keyboard moved it
    if (focused && n > 0) {
        int d = vert_ + page_ * visible;
        if (d) {
            keyMoved = true;
            selected = std::clamp(selected + d, 0, n - 1);
            vert_ = 0;
            page_ = 0;
        }
        if (activate_ && focusedBefore && selected >= 0 && selected < n) { activated = selected; activate_ = false; }
    }
    if (hover && wheel_ != 0) { scroll -= wheel_ * rowH * 2; wheel_ = 0; }
    const float maxScroll = std::max(0.f, n * rowH - r.h);
    if (const float t = touchScroll(r); t != 0) {
        scroll -= t;
        if (scroll <= 0 || scroll >= maxScroll) momentum_ = 0;
    }
    // Keep the selection visible after keyboard moves only: a finger/wheel scroll must not snap back to it.
    if (keyMoved && selected >= 0) {
        if (selected * rowH < scroll) scroll = selected * rowH;
        if ((selected + 1) * rowH > scroll + r.h) scroll = (selected + 1) * rowH - r.h;
    }
    scroll = std::clamp(scroll, 0.f, maxScroll);
    // Clicks on rows: a mouse selects on the press, a touch when it lifts without scrolling.
    if (hover && (touchPress_ ? mouseReleased_ && pressId_ == id && touchMode_ != TouchMode::Scroll : mousePressed_)) {
        const int row = int((my_ - r.y + scroll) / rowH);
        if (row >= 0 && row < n && rows[row].enabled) {
            const Uint64 now = SDL_GetTicks();
            if (row == lastClickRow_ && row == selected && now - lastClickTicks_ < 400) activated = row;
            selected = row;
            lastClickRow_ = row;
            lastClickTicks_ = now;
        }
    }
    fill(r, Colour{0, 0, 0, 110});
    SDL_Rect old;
    const bool hadClip = SDL_RenderClipEnabled(r_);
    SDL_GetRenderClipRect(r_, &old);
    const SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
    SDL_SetRenderClipRect(r_, &clip);
    const float sbW = maxScroll > 0 ? 6 * bodyScale : 0;
    for (int i = int(scroll / rowH); i < n; ++i) {
        const float y = r.y + i * rowH - scroll;
        if (y > r.y + r.h) break;
        const SDL_FRect rr{r.x, y, r.w - sbW, rowH};
        const bool sel = i == selected;
        const bool rh = hover && my_ >= y && my_ < y + rowH;
        if (sel) gradient(rr, Colour{150, 92, 30, focused ? Uint8(235) : Uint8(170)}, Colour{100, 58, 18, focused ? Uint8(235) : Uint8(170)});
        else if (rh) fill(rr, Colour{255, 200, 120, 26});
        else if (i % 2) fill(rr, Colour{255, 255, 255, 8});
        const Colour tc = !rows[i].enabled ? theme::kDisabled : sel ? Colour{255, 244, 220, 255} : theme::kText;
        float detailW = rows[i].detail.empty() ? 0 : textWidth(rows[i].detail, s) + 16 * bodyScale;
        text(ellipsize(rows[i].text, rr.w - 20 * bodyScale - detailW, s), rr.x + 10 * bodyScale,
             y + (rowH - textHeight(s)) / 2, tc, s);
        if (!rows[i].detail.empty())
            text(rows[i].detail, rr.x + rr.w - 10 * bodyScale, y + (rowH - textHeight(s)) / 2,
                 sel ? Colour{255, 226, 170, 255} : theme::kDim, s, Align::Right);
    }
    SDL_SetRenderClipRect(r_, hadClip ? &old : nullptr);
    if (maxScroll > 0) {
        const SDL_FRect tr{r.x + r.w - sbW, r.y, sbW, r.h};
        fill(tr, Colour{0, 0, 0, 120});
        const float th = std::max(20.f, r.h * r.h / (n * rowH));
        fill({tr.x + 1, r.y + (r.h - th) * (scroll / maxScroll), sbW - 2, th}, Colour{200, 140, 60, 200});
    }
    outline(r, Colour{0, 0, 0, 200});
    if (focused) outline({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, Colour{theme::kAccent.r, theme::kAccent.g, theme::kAccent.b, 140});
    return activated;
}

}  // namespace d2k::platform
