#include "render/ui_skin.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

namespace {

constexpr float kPxPerScale = 10;  // text: pixels of line height per scale unit

int chR(std::uint32_t c) { return c & 0xFF; }
int chG(std::uint32_t c) { return (c >> 8) & 0xFF; }
int chB(std::uint32_t c) { return (c >> 16) & 0xFF; }
float lum(std::uint32_t c) { return 0.299f * chR(c) + 0.587f * chG(c) + 0.114f * chB(c); }

struct V3 {
    float x, y, z;
};
V3 norm3(V3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / l, v.y / l, v.z / l};
}
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Light from the top-left (screen y points down) and the Blinn half vector for a viewer at +z.
const V3 kLight = norm3({-0.55f, -0.75f, 0.62f});
const V3 kHalf = norm3({kLight.x, kLight.y, kLight.z + 1});
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float mix(float a, float b, float t) { return a + (b - a) * t; }

std::uint32_t packF(float r, float g, float b, float a) {
    auto c = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return c(a * 255) << 24 | c(b) << 16 | c(g) << 8 | c(r);
}

// "Over" straight-alpha compositing of (s, sa) onto (r, g, b, a).
void over(float& r, float& g, float& b, float& a, float sr, float sg, float sb, float sa) {
    sa = clamp01(sa);
    const float oa = sa + a * (1 - sa);
    if (oa <= 0) { r = g = b = a = 0; return; }
    r = (sr * sa + r * a * (1 - sa)) / oa;
    g = (sg * sa + g * a * (1 - sa)) / oa;
    b = (sb * sa + b * a * (1 - sa)) / oa;
    a = oa;
}

float hashf(int n) {
    std::uint32_t x = std::uint32_t(n) * 0x9E3779B1u;
    x ^= x >> 15;
    x *= 0x85EBCA77u;
    x ^= x >> 13;
    return float(x & 0xFFFF) / 65535.0f;
}
// Smooth 2-D value noise (0..1).
float vnoise(float x, float y) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    auto h = [](int a, int b) { return hashf(a * 73856093 ^ b * 19349663); };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    return mix(mix(h(xi, yi), h(xi + 1, yi), sx), mix(h(xi, yi + 1), h(xi + 1, yi + 1), sx), sy);
}

// --- signed distance shapes (negative inside) ---

float sdRound(float px, float py, float cx, float cy, float hx, float hy, float rr) {
    rr = std::max(0.0f, std::min({rr, hx, hy}));
    const float qx = std::abs(px - cx) - (hx - rr), qy = std::abs(py - cy) - (hy - rr);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rr;
}
float sdCircle(float px, float py, float cx, float cy, float r) { return std::hypot(px - cx, py - cy) - r; }
float sdCapsule(float px, float py, float ax, float ay, float bx, float by, float r) {
    const float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const float h = clamp01((pax * bax + pay * bay) / (bax * bax + bay * bay));
    return std::hypot(pax - bax * h, pay - bay * h) - r;
}
struct P2 {
    float x, y;
};
float sdPoly(const std::vector<P2>& v, float px, float py) {
    float d = (px - v[0].x) * (px - v[0].x) + (py - v[0].y) * (py - v[0].y);
    float s = 1;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i, ++i) {
        const float ex = v[j].x - v[i].x, ey = v[j].y - v[i].y, wx = px - v[i].x, wy = py - v[i].y;
        const float t = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c1 = py >= v[i].y, c2 = py < v[j].y, c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * std::sqrt(d);
}

using Icon = UiSkin::Icon;

// The vector icons in a unit box (u, v in 0..1, v down).
float iconSd(Icon icon, float u, float v) {
    switch (icon) {
    case Icon::Repair: {  // wrench
        const float handle = sdCapsule(u, v, 0.24f, 0.78f, 0.56f, 0.46f, 0.075f);
        const float head = std::max(sdCircle(u, v, 0.64f, 0.37f, 0.21f), -sdCircle(u, v, 0.76f, 0.25f, 0.13f));
        return std::min(handle, head);
    }
    case Icon::Sell:  // a coin rim (the "$" is the font's)
        return std::abs(sdCircle(u, v, 0.5f, 0.5f, 0.38f)) - 0.05f;
    case Icon::Guard: {  // shield with a cross cut out
        static const std::vector<P2> s = {{0.5f, 0.08f}, {0.86f, 0.2f}, {0.82f, 0.55f}, {0.5f, 0.92f}, {0.18f, 0.55f}, {0.14f, 0.2f}};
        const float cross = std::min(sdCapsule(u, v, 0.5f, 0.28f, 0.5f, 0.72f, 0.055f), sdCapsule(u, v, 0.31f, 0.44f, 0.69f, 0.44f, 0.055f));
        return std::max(sdPoly(s, u, v), -cross);
    }
    case Icon::Retreat: {  // a back arrow with a flag-pole feel: arrow to the left
        static const std::vector<P2> head = {{0.1f, 0.5f}, {0.44f, 0.2f}, {0.44f, 0.8f}};
        return std::min(sdPoly(head, u, v), sdCapsule(u, v, 0.38f, 0.5f, 0.86f, 0.5f, 0.085f));
    }
    case Icon::Upgrade: {  // double chevron up
        const float r = 0.075f;
        return std::min({sdCapsule(u, v, 0.22f, 0.5f, 0.5f, 0.24f, r), sdCapsule(u, v, 0.5f, 0.24f, 0.78f, 0.5f, r),
                         sdCapsule(u, v, 0.22f, 0.78f, 0.5f, 0.52f, r), sdCapsule(u, v, 0.5f, 0.52f, 0.78f, 0.78f, r)});
    }
    case Icon::Starport: {  // a frigate seen from above
        static const std::vector<P2> s = {{0.5f, 0.06f}, {0.63f, 0.36f}, {0.9f, 0.6f}, {0.9f, 0.7f}, {0.61f, 0.63f}, {0.59f, 0.86f},
                                          {0.41f, 0.86f}, {0.39f, 0.63f}, {0.1f, 0.7f}, {0.1f, 0.6f}, {0.37f, 0.36f}};
        return sdPoly(s, u, v);
    }
    case Icon::Options: {  // gear
        float d = sdCircle(u, v, 0.5f, 0.5f, 0.27f);
        for (int i = 0; i < 8; ++i) {
            const float a = i * 0.7853982f;
            d = std::min(d, sdCapsule(u, v, 0.5f + 0.24f * std::cos(a), 0.5f + 0.24f * std::sin(a), 0.5f + 0.37f * std::cos(a),
                                      0.5f + 0.37f * std::sin(a), 0.075f));
        }
        return std::max(d, -sdCircle(u, v, 0.5f, 0.5f, 0.12f));
    }
    case Icon::Up: {
        static const std::vector<P2> s = {{0.5f, 0.2f}, {0.84f, 0.72f}, {0.16f, 0.72f}};
        return sdPoly(s, u, v) - 0.03f;
    }
    case Icon::Down: {
        static const std::vector<P2> s = {{0.16f, 0.28f}, {0.84f, 0.28f}, {0.5f, 0.8f}};
        return sdPoly(s, u, v) - 0.03f;
    }
    case Icon::Buildings: {  // a house with a door
        static const std::vector<P2> s = {{0.1f, 0.5f}, {0.5f, 0.14f}, {0.9f, 0.5f}, {0.8f, 0.5f}, {0.8f, 0.86f}, {0.2f, 0.86f}, {0.2f, 0.5f}};
        return std::max(sdPoly(s, u, v), -sdRound(u, v, 0.5f, 0.74f, 0.09f, 0.13f, 0.02f));
    }
    case Icon::Infantry:  // a soldier with a rifle
        return std::min({sdCircle(u, v, 0.5f, 0.2f, 0.115f), sdCapsule(u, v, 0.5f, 0.42f, 0.5f, 0.58f, 0.135f),
                         sdCapsule(u, v, 0.43f, 0.66f, 0.38f, 0.9f, 0.065f), sdCapsule(u, v, 0.57f, 0.66f, 0.62f, 0.9f, 0.065f),
                         sdCapsule(u, v, 0.24f, 0.52f, 0.86f, 0.34f, 0.04f)});
    case Icon::Vehicles:  // a tank
        return std::min({sdRound(u, v, 0.5f, 0.68f, 0.4f, 0.14f, 0.07f), sdCircle(u, v, 0.45f, 0.48f, 0.15f),
                         sdCapsule(u, v, 0.5f, 0.45f, 0.92f, 0.36f, 0.045f)});
    case Icon::Special: {  // a five-pointed star
        static std::vector<P2> s;
        if (s.empty())
            for (int i = 0; i < 10; ++i) {
                const float a = -1.5707963f + i * 0.6283185f, r = (i & 1) ? 0.19f : 0.44f;
                s.push_back({0.5f + r * std::cos(a), 0.53f + r * std::sin(a)});
            }
        return sdPoly(s, u, v);
    }
    case Icon::Menu:  // three bars
        return std::min({sdCapsule(u, v, 0.2f, 0.28f, 0.8f, 0.28f, 0.07f), sdCapsule(u, v, 0.2f, 0.5f, 0.8f, 0.5f, 0.07f),
                         sdCapsule(u, v, 0.2f, 0.72f, 0.8f, 0.72f, 0.07f)});
    case Icon::Eye:
    case Icon::EyeOff: {  // an almond outline with the pupil (EyeOff: struck through)
        const float lens = std::max(sdCircle(u, v, 0.5f, 0.97f, 0.64f), sdCircle(u, v, 0.5f, 0.03f, 0.64f));
        float d = std::min(std::abs(lens) - 0.05f, sdCircle(u, v, 0.5f, 0.5f, 0.13f));
        if (icon == Icon::EyeOff) {
            d = std::max(d, -sdCapsule(u, v, 0.14f, 0.86f, 0.86f, 0.14f, 0.12f));
            d = std::min(d, sdCapsule(u, v, 0.18f, 0.82f, 0.82f, 0.18f, 0.055f));
        }
        return d;
    }
    case Icon::Radar:  // a scope ring, a sweep line and the centre
        return std::min({std::abs(sdCircle(u, v, 0.5f, 0.5f, 0.36f)) - 0.05f, sdCapsule(u, v, 0.5f, 0.5f, 0.74f, 0.26f, 0.05f),
                         sdCircle(u, v, 0.5f, 0.5f, 0.08f), sdCircle(u, v, 0.33f, 0.6f, 0.055f)});
    case Icon::ChevronLeft:
        return std::min(sdCapsule(u, v, 0.62f, 0.2f, 0.36f, 0.5f, 0.08f), sdCapsule(u, v, 0.36f, 0.5f, 0.62f, 0.8f, 0.08f));
    case Icon::ChevronRight:
        return std::min(sdCapsule(u, v, 0.38f, 0.2f, 0.64f, 0.5f, 0.08f), sdCapsule(u, v, 0.64f, 0.5f, 0.38f, 0.8f, 0.08f));
    case Icon::Stop:
        return sdRound(u, v, 0.5f, 0.5f, 0.28f, 0.28f, 0.07f);
    case Icon::Deploy: {  // an arrow down onto the ground line
        static const std::vector<P2> head = {{0.26f, 0.46f}, {0.74f, 0.46f}, {0.5f, 0.74f}};
        return std::min({sdCapsule(u, v, 0.5f, 0.14f, 0.5f, 0.52f, 0.075f), sdPoly(head, u, v), sdCapsule(u, v, 0.18f, 0.87f, 0.82f, 0.87f, 0.065f)});
    }
    case Icon::Close:
        return std::min(sdCapsule(u, v, 0.24f, 0.24f, 0.76f, 0.76f, 0.075f), sdCapsule(u, v, 0.24f, 0.76f, 0.76f, 0.24f, 0.075f));
    case Icon::Pin:  // a map pin: round head, needle
        return std::min(sdCircle(u, v, 0.5f, 0.32f, 0.2f), sdCapsule(u, v, 0.5f, 0.45f, 0.5f, 0.9f, 0.05f));
    case Icon::Bolt: {  // lightning
        static const std::vector<P2> s = {{0.6f, 0.04f}, {0.2f, 0.56f}, {0.46f, 0.56f}, {0.38f, 0.96f}, {0.8f, 0.42f}, {0.54f, 0.42f}};
        return sdPoly(s, u, v);
    }
    case Icon::None: break;
    }
    return 1e9f;
}

}  // namespace

float UiSkin::scaleFor(int outW, int outH) {
    // 1.5 at 800 rows, 2 at 1080, in quarter steps; the 160-px panel stays under 30 % of the width.
    float k = std::round(float(outH) / 540.0f * 4.0f) / 4.0f;
    k = std::min(k, std::floor(outW * 0.3f / 160.0f * 4.0f) / 4.0f);
    return std::clamp(k, 1.0f, 4.0f);
}

void UiSkin::setScale(float k) {
    k_ = k;
    // Text a little larger than the layout (0.8 k, quarter steps): 13 px at 800 rows, 15 at 1080.
    ts_ = std::max(1.0f, std::round(k * 0.8f * 4.0f) / 4.0f);
}

UiSkin::UiSkin(SDL_Renderer* r, TextRenderer& text, const formats::HouseColours& colours)
    : r_(r), text_(text), colours_(colours) {
    std::size_t n = 0;
    if (const unsigned char* d = embeddedFont("play_regular", n)) regular_ = std::make_unique<TtfFont>(r, d, n);
    if (const unsigned char* d = embeddedFont("play_bold", n)) bold_ = std::make_unique<TtfFont>(r, d, n);
    if (regular_ && !regular_->ok()) regular_.reset();
    if (bold_ && !bold_->ok()) bold_.reset();
    setHouse(0);
}

UiSkin::~UiSkin() {
    for (auto& [key, t] : baked_) drop(t);
}

UiSkin::Tex UiSkin::upload(const std::vector<std::uint32_t>& px, int w, int h) {
    Tex t;
    t.w = w;
    t.h = h;
    t.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t.t) return t;
    SDL_UpdateTexture(t.t, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_SCALEMODE_NEAREST);  // drawn 1:1
    return t;
}

void UiSkin::drop(Tex& t) {
    if (t.t) SDL_DestroyTexture(t.t);
    t.t = nullptr;
}

void UiSkin::setHouse(int house) {
    if (house < 0 || house >= formats::kHouseCount) house = 0;
    house_ = house;
    // The house ramp sorted by brightness (as the art packs map house colours).
    std::array<std::uint32_t, 16> ramp = colours_[house];
    std::sort(ramp.begin(), ramp.end(), [](auto a, auto b) { return lum(a) < lum(b); });
    auto pick = [&](float t) {
        const std::uint32_t c = ramp[std::size_t(std::clamp(t, 0.0f, 1.0f) * 15)];
        return Colour{Uint8(chR(c)), Uint8(chG(c)), Uint8(chB(c)), 255};
    };
    accent_ = pick(0.8f);
    accentDark_ = pick(0.35f);
}

Colour UiSkin::houseColour(int house) const {
    if (house < 0 || house >= formats::kHouseCount) house = 0;
    std::array<std::uint32_t, 16> ramp = colours_[house];
    std::sort(ramp.begin(), ramp.end(), [](auto a, auto b) { return lum(a) < lum(b); });
    const std::uint32_t c = ramp[12];
    return Colour{Uint8(chR(c)), Uint8(chG(c)), Uint8(chB(c)), 255};
}

// --- text ---

TtfFont* UiSkin::font(bool bold) const { return bold && bold_ ? bold_.get() : regular_.get(); }

float UiSkin::textH(float scale) const {
    const float s = scale > 0 ? scale : ts_;
    if (regular_) return regular_->lineHeight(s * kPxPerScale);
    return text_.height(s);
}

float UiSkin::textW(const std::string& s, float scale, bool bold) {
    const float sc = scale > 0 ? scale : ts_;
    if (TtfFont* f = font(bold)) return f->width(s, sc * kPxPerScale);
    return text_.width(s, sc);
}

float UiSkin::label(const std::string& s, float x, float y, Colour c, float scale, bool bold) {
    const float sc = scale > 0 ? scale : ts_;
    c.a = al(c.a);
    if (TtfFont* f = font(bold)) {
        const float o = std::max(1.0f, std::round(sc * kPxPerScale / 12));
        f->draw(s, x + o, y + o, sc * kPxPerScale, Colour{0, 0, 0, Uint8(c.a * 0.7f)});
        return f->draw(s, x, y, sc * kPxPerScale, c);
    }
    return text_.draw(s, std::round(x), std::round(y), c, sc, true);
}

// --- primitives ---

void UiSkin::fill(const SDL_FRect& r, Colour c) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, al(c.a));
    SDL_RenderFillRect(r_, &r);
}

void UiSkin::outline(const SDL_FRect& r, Colour c, float t) {
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.y + r.h - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

// Baked pieces:
//   1 bezel under (state = Bezel)        2 bezel over (state = Bezel)
//   3 button plate (state = State, extra = icon | label << 8)
//   4 panel (column background)          5 well
const UiSkin::Tex& UiSkin::baked(int kind, int w, int h, int state, int extra) {
    if (bakedK_ != k_ || baked_.size() > 600) {
        for (auto& [key, t] : baked_) drop(t);
        baked_.clear();
        bakedK_ = k_;
    }
    w = std::max(4, w);
    h = std::max(4, h);
    const std::uint64_t key = std::uint64_t(kind) << 58 | std::uint64_t(state & 15) << 54 | std::uint64_t(house_ & 15) << 50 |
                              std::uint64_t(extra & 0x3FF) << 40 | std::uint64_t(w & 0xFFFFF) << 20 | std::uint64_t(h & 0xFFFFF);
    if (auto it = baked_.find(key); it != baked_.end()) return it->second;
    const float k = k_;
    const float fw = float(w), fh = float(h), cx = fw / 2, cy = fh / 2;
    const Colour acc = accent_;
    std::vector<std::uint32_t> out;
    int pad = 0;
    auto finish = [&](int W, int H) -> const Tex& {
        Tex t = upload(out, W, H);
        t.pad = pad;
        return baked_.emplace(key, t).first->second;
    };

    if (kind == 1 || kind == 2) {
        const Bezel st = Bezel(state);
        const float ro = std::round(4.0f * k), b = std::round(2.0f * k), ri = std::max(1.5f, std::round(2.0f * k));
        if (kind == 1) {
            pad = int(std::ceil(5 * k));
            const int W = w + 2 * pad, H = h + 2 * pad;
            out.assign(std::size_t(W) * H, 0);
            const bool down = st == Bezel::Pressed;
            const float sy = (down ? 0.4f : 1.3f) * k;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const float px = x + 0.5f - pad, py = y + 0.5f - pad;
                    const float dO = sdRound(px, py, cx, cy, cx, cy, ro);
                    const float dS = sdRound(px, py - sy, cx, cy, cx, cy, ro);
                    float r = 0, g = 0, bb = 0, a = 0;
                    over(r, g, bb, a, 0, 0, 0, (st == Bezel::Empty ? 0.3f : 0.55f) * (dS < 0 ? 1 : std::exp(-dS / (1.3f * k))));
                    if (st == Bezel::Hot || st == Bezel::Lit) {
                        const Colour c = st == Bezel::Hot ? Colour{255, 196, 100, 255} : acc;
                        over(r, g, bb, a, c.r, c.g, c.b, 0.8f * (dO > 0 ? std::exp(-dO / (1.8f * k)) : 1));
                    }
                    const float wc = clamp01(0.5f - dO);
                    if (wc > 0) {
                        const float t = clamp01(py / fh);
                        over(r, g, bb, a, 15 + 14 * t, 10 + 10 * t, 6 + 6 * t, wc);
                    }
                    out[std::size_t(y) * W + x] = packF(r, g, bb, a);
                }
            return finish(W, H);
        }
        out.assign(std::size_t(w) * h, 0);
        const bool down = st == Bezel::Pressed, dim = st == Bezel::Empty || st == Bezel::Disabled;
        const float sh = (down ? 2.2f : 1.4f) * k;
        const float specFlat = std::pow(kHalf.z, 30.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float dO = sdRound(px, py, cx, cy, cx, cy, ro);
                const float dI = sdRound(px, py, cx, cy, cx - b, cy - b, ri);
                float r = 0, g = 0, bb = 0, a = 0;
                const float ci = clamp01(0.5f - dI);
                if (ci > 0) {  // the picture: inner shadow from the top, soft vignette, glass gloss
                    const float dIs = sdRound(px, py - sh, cx, cy, cx - b, cy - b, ri);
                    const float a1 = clamp01((dIs + 2.0f * k) / (3.0f * k)) * (down ? 0.7f : 0.5f);
                    const float a2 = clamp01(1 + dI / (2.5f * k)) * 0.28f;
                    over(r, g, bb, a, 0, 0, 0, std::max(a1, a2) * ci);
                    if (!dim) {
                        const float xn = (px - cx) / cx;
                        const float yb = fh * (0.28f + 0.14f * (1 - xn * xn));
                        float ga = py < yb ? 0.12f * std::pow(1 - py / yb, 1.3f) * clamp01(yb - py) : 0;
                        const float e = (dI + 1.0f * k) / (0.45f * k);
                        ga += 0.16f * std::exp(-e * e) * clamp01((cy - py) / (fh * 0.25f));
                        over(r, g, bb, a, 255, 250, 240, ga * ci);
                    }
                }
                const float rc = clamp01(0.5f - dO) * clamp01(0.5f + dI);
                if (rc > 0) {  // steel rim, rounded profile lit from the top-left
                    const float gx = sdRound(px + 0.5f, py, cx, cy, cx, cy, ro) - sdRound(px - 0.5f, py, cx, cy, cx, cy, ro);
                    const float gy = sdRound(px, py + 0.5f, cx, cy, cx, cy, ro) - sdRound(px, py - 0.5f, cx, cy, cx, cy, ro);
                    const float t = clamp01(dI / std::max(1.0f, dI - dO));
                    const float slope = 3.14159f * std::cos(3.14159f * t) * 0.7f;
                    const V3 N = norm3({-gx * slope, -gy * slope, 1});
                    const float ty = clamp01(py / fh);
                    // Bronze-copper metal (warm, Dune 2000's palette).
                    float br = mix(156, 66, ty), bg = mix(112, 44, ty), bbb = mix(70, 26, ty);
                    if (dim) {
                        br *= 0.6f;
                        bg *= 0.6f;
                        bbb *= 0.6f;
                    } else if (st == Bezel::Hot) {
                        br = mix(br, 255, 0.35f);
                        bg = mix(bg, 196, 0.35f);
                        bbb = mix(bbb, 100, 0.35f);
                    } else if (st == Bezel::Lit) {
                        br = mix(br, acc.r, 0.5f);
                        bg = mix(bg, acc.g, 0.5f);
                        bbb = mix(bbb, acc.b, 0.5f);
                    }
                    const float m = 0.35f + 0.9f * std::max(0.0f, dot3(N, kLight));
                    const float spec = std::max(0.0f, std::pow(std::max(0.0f, dot3(N, kHalf)), 30.0f) - specFlat) * (dim ? 60 : 170);
                    const float e1 = clamp01(1 + dO / (0.8f * k)), e2 = clamp01(1 - dI / (0.8f * k));
                    const float dark = 1 - 0.6f * std::max(e1, e2);
                    over(r, g, bb, a, (br * m + spec) * dark, (bg * m + spec * 0.86f) * dark, (bbb * m + spec * 0.6f) * dark, rc);
                }
                out[std::size_t(y) * w + x] = packF(r, g, bb, a);
            }
        return finish(w, h);
    }

    if (kind == 3) {  // button plate with its icon
        const State st = State(state);
        const Icon icon = Icon(extra & 0xFF);
        const bool labelled = (extra >> 8) & 1;
        const bool down = st == State::Pressed || st == State::Lit, dis = st == State::Disabled, hot = st == State::Hot;
        pad = int(std::ceil(4 * k));
        const int W = w + 2 * pad, H = h + 2 * pad;
        out.assign(std::size_t(W) * H, 0);
        const float rr = std::min(std::round(3.5f * k), fh * 0.3f);
        // Icon box: centred, or a square at the left when the button has a label.
        const float is = std::round(std::min(fh * 0.72f, labelled ? fh * 0.72f : std::min(fw, fh) * 0.72f));
        const float ix = labelled ? std::round((fh - is) / 2 + k) : std::round((fw - is) / 2);
        const float iy = std::round((fh - is) / 2) + (down ? std::round(0.5f * k) : 0);
        const float grain = 1.0f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float px = x + 0.5f - pad, py = y + 0.5f - pad;
                const float dO = sdRound(px, py, cx, cy, cx, cy, rr);
                float r = 0, g = 0, b = 0, a = 0;
                // Drop shadow (tucked under when pressed) and the hover / active glow.
                if (!down) {
                    const float dS = sdRound(px, py - 1.2f * k, cx, cy, cx, cy, rr);
                    over(r, g, b, a, 0, 0, 0, (dis ? 0.3f : 0.5f) * (dS < 0 ? 1 : std::exp(-dS / (1.2f * k))));
                }
                if (hot || st == State::Lit) {
                    const Colour c = hot ? Colour{255, 190, 90, 255} : acc;
                    if (dO > 0) over(r, g, b, a, c.r, c.g, c.b, (hot ? 0.45f : 0.55f) * std::exp(-dO / (1.6f * k)));
                }
                const float cov = clamp01(0.5f - dO);
                if (cov > 0) {
                    const float ty = clamp01(py / fh);
                    float br, bg, bb;
                    // Leather-brown / bronze plates (warm: R > G > B).
                    if (down) {
                        br = mix(34, 58, ty);
                        bg = mix(24, 40, ty);
                        bb = mix(16, 25, ty);
                    } else if (dis) {
                        br = mix(48, 36, ty);
                        bg = mix(38, 28, ty);
                        bb = mix(29, 21, ty);
                    } else {
                        br = mix(hot ? 116 : 98, 46, ty);
                        bg = mix(hot ? 84 : 70, 31, ty);
                        bb = mix(hot ? 54 : 44, 19, ty);
                    }
                    const float n = (hashf(int(py) * 131 + w) - 0.5f) * 5 * grain + (vnoise(px / (6 * k), py / (2 * k)) - 0.5f) * 6;
                    br += n;
                    bg += n;
                    bb += n;
                    // Bevel: a lit 1-px edge on top, a dark one at the bottom (inverted when pressed).
                    const float gy = sdRound(px, py + 0.5f, cx, cy, cx, cy, rr) - sdRound(px, py - 0.5f, cx, cy, cx, cy, rr);
                    const float edge = clamp01(1 - std::abs(dO + 1.5f) / 1.0f);
                    const float lit = down ? -gy : -gy;  // top edges have gy < 0
                    if (!down) {
                        if (lit > 0) { br += 110 * edge * lit; bg += 84 * edge * lit; bb += 52 * edge * lit; }
                        else { br -= 20 * edge; bg -= 20 * edge; bb -= 20 * edge; }
                    } else {
                        // Pressed: inner shadow from the top.
                        const float s = clamp01(1 - (py - 0) / (3.5f * k)) * 0.5f;
                        br *= 1 - s; bg *= 1 - s; bb *= 1 - s;
                    }
                    if (st == State::Lit) {  // house-colour line inside the bottom edge
                        const float e = clamp01(1 - std::abs((fh - py) - 2.2f * k) / (1.0f * k));
                        br = mix(br, acc.r, e * 0.9f);
                        bg = mix(bg, acc.g, e * 0.9f);
                        bb = mix(bb, acc.b, e * 0.9f);
                    }
                    // Outer border: dark, amber on hover.
                    const float bdr = clamp01(1 + dO / (1.0f));
                    if (hot) { br = mix(br, 255, bdr * 0.75f); bg = mix(bg, 186, bdr * 0.75f); bb = mix(bb, 90, bdr * 0.75f); }
                    else { br = mix(br, 10, bdr * 0.85f); bg = mix(bg, 6, bdr * 0.85f); bb = mix(bb, 3, bdr * 0.85f); }
                    over(r, g, b, a, br, bg, bb, cov);
                }
                // The icon: amber metal with a dark outline and a soft shadow.
                if (icon != Icon::None) {
                    const float u = (px - ix) / is, v = (py - iy) / is;
                    if (u > -0.3f && u < 1.3f && v > -0.3f && v < 1.3f) {
                        const float d = iconSd(icon, u, v) * is;
                        const float ds = iconSd(icon, (px - ix) / is, (py - iy - std::max(1.0f, k * 0.8f)) / is) * is;
                        if (ds < 1.5f) over(r, g, b, a, 0, 0, 0, 0.55f * clamp01(0.5f - ds + 0.8f));
                        const float c = clamp01(0.5f - d);
                        if (c > 0) {
                            const float tv = clamp01(v);
                            float ir, ig, ib;
                            if (dis) { ir = 120; ig = 104; ib = 86; }
                            else if (hot) { ir = mix(255, 255, tv); ig = mix(244, 186, tv); ib = mix(205, 88, tv); }
                            else if (st == State::Lit) { ir = mix(255, acc.r, tv * 0.6f); ig = mix(232, acc.g, tv * 0.6f); ib = mix(170, acc.b, tv * 0.6f); }
                            else { ir = mix(250, 214, tv); ig = mix(224, 148, tv); ib = mix(160, 62, tv); }
                            // Edge darkening for a crisp, slightly embossed look.
                            const float e = clamp01(1 + d / (0.9f * k));
                            ir *= 1 - 0.35f * e;
                            ig *= 1 - 0.35f * e;
                            ib *= 1 - 0.35f * e;
                            over(r, g, b, a, ir, ig, ib, c);
                        }
                    }
                }
                out[std::size_t(y) * W + x] = packF(r, g, b, a);
            }
        return finish(W, H);
    }

    if (kind == 6) {  // a bare glyph (extra = icon): white (tinted when drawn) over a soft dark shadow
        const Icon icon = Icon(extra & 0xFF);
        pad = int(std::ceil(2 * k));
        const int W = w + 2 * pad, H = h + 2 * pad;
        out.assign(std::size_t(W) * H, 0);
        const float is = fw;
        const float so = std::max(1.0f, is * 0.05f);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float px = x + 0.5f - pad, py = y + 0.5f - pad;
                float r = 0, g = 0, b = 0, a = 0;
                const float ds = iconSd(icon, px / is, (py - so) / is) * is;
                if (ds < 3 * so) over(r, g, b, a, 0, 0, 0, 0.6f * clamp01(1.0f - ds / (2.5f * so)));
                const float d = iconSd(icon, px / is, py / is) * is;
                const float c = clamp01(0.5f - d);
                if (c > 0) over(r, g, b, a, 255, 255, 255, c);
                out[std::size_t(y) * W + x] = packF(r, g, b, a);
            }
        return finish(W, H);
    }

    if (kind == 4) {  // the column: dark brushed bronze / leather brown, a subtle house accent on its left edge
        out.assign(std::size_t(w) * h, 0);
        for (int y = 0; y < h; ++y) {
            const float row = (hashf(y * 977 + 13) - 0.5f) * 7;
            for (int x = 0; x < w; ++x) {
                const float ty = float(y) / fh;
                float v = mix(33, 22, ty) + row * 0.6f + (vnoise(x / (40 * k), y / (9 * k)) - 0.5f) * 8 +
                          (hashf(x * 7919 + y * 104729) - 0.5f) * 3;
                // Warm tint: the grain reads as brushed bronze over dark leather.
                float r = v * 1.42f + 3, g = v * 1.0f, b = v * 0.66f;
                // A soft vertical fold every ~the panel width / 2 for structure.
                const float fx = float(x) / fw;
                const float fold = std::exp(-std::pow((fx - 0.5f) * 9, 2)) * 3;
                r += fold * 1.3f; g += fold; b += fold * 0.6f;
                // Left edge: a sand-amber bevel line, then a faint house-colour glow.
                const float ex = float(x);
                const float line = clamp01(1.5f * k - ex) / (1.5f * k);
                const float glow = std::exp(-ex / (5 * k)) * 0.18f;
                const float lr = mix(acc.r, 214, 0.45f), lg = mix(acc.g, 150, 0.45f), lb = mix(acc.b, 72, 0.45f);
                r = mix(r, lr, clamp01(line + glow));
                g = mix(g, lg, clamp01(line + glow));
                b = mix(b, lb, clamp01(line + glow));
                out[std::size_t(y) * w + x] = packF(r, g, b, 1);
            }
        }
        return finish(w, h);
    }

    // kind 5: well
    out.assign(std::size_t(w) * h, 0);
    const float rr = std::min(std::round(3 * k), std::min(fw, fh) * 0.3f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float dO = sdRound(px, py, cx, cy, cx, cy, rr);
            const float cov = clamp01(0.5f - dO);
            if (cov <= 0) continue;
            const float ty = clamp01(py / fh);
            float v = mix(10, 16, ty) + (vnoise(px / (8 * k), py / (8 * k)) - 0.5f) * 3;
            // Inner shadow from the top, a faint lit lip at the bottom, a dark rim.
            const float dS = sdRound(px, py - 2.5f * k, cx, cy, cx, cy, rr);
            v *= 1 - 0.55f * clamp01((dS + 3 * k) / (3 * k));
            const float lip = clamp01(1 - std::abs(dO + 1.0f)) * clamp01((py - cy) / cy);
            float r = v * 1.3f + 96 * lip, g = v + 70 * lip, b = v * 0.72f + 42 * lip;
            const float rim = clamp01(1 + dO);
            r = mix(r, 3, rim * 0.6f);
            g = mix(g, 2, rim * 0.6f);
            b = mix(b, 1, rim * 0.6f);
            out[std::size_t(y) * w + x] = packF(r, g, b, cov * 0.97f);
        }
    return finish(w, h);
}

void UiSkin::drawBaked(const Tex& t, const SDL_FRect& r) {
    if (!t.t) return;
    const SDL_FRect d{std::round(r.x) - t.pad, std::round(r.y) - t.pad, float(t.w), float(t.h)};
    if (alpha_ >= 1.0f) {
        SDL_RenderTexture(r_, t.t, nullptr, &d);
        return;
    }
    float m = 1.0f;
    SDL_GetTextureAlphaModFloat(t.t, &m);
    SDL_SetTextureAlphaModFloat(t.t, m * alpha_);
    SDL_RenderTexture(r_, t.t, nullptr, &d);
    SDL_SetTextureAlphaModFloat(t.t, m);
}

void UiSkin::glyph(const SDL_FRect& r, Icon icon, Colour c) {
    const int sz = std::max(4, int(std::round(std::min(r.w, r.h))));
    const Tex& t = baked(6, sz, sz, 0, int(icon));
    if (!t.t) return;
    SDL_SetTextureColorMod(t.t, c.r, c.g, c.b);
    const float m = c.a / 255.0f;
    SDL_SetTextureAlphaModFloat(t.t, m);
    drawBaked(t, {std::round(r.x + (r.w - sz) / 2), std::round(r.y + (r.h - sz) / 2), float(sz), float(sz)});
    SDL_SetTextureAlphaModFloat(t.t, 1.0f);
    SDL_SetTextureColorMod(t.t, 255, 255, 255);
}

void UiSkin::panel(const SDL_FRect& r) {
    const Tex& t = baked(4, int(std::round(r.w)), int(std::round(r.h)), 0);
    if (panelOpacity_ >= 1.0f || !t.t) {
        drawBaked(t, r);
        return;
    }
    // Translucent (Settings "Panel transparency"): a dark warm veil darkens and calms the
    // battlefield under the column (a cheap stand-in for a blur: deeper towards the right edge),
    // then the metal at reduced opacity. Icons, text and buttons drawn later stay opaque.
    const float veil = 1.0f - panelOpacity_;
    const SDL_FColor l{14 / 255.0f, 9 / 255.0f, 5 / 255.0f, std::min(1.0f, 0.12f + 0.4f * veil)};
    const SDL_FColor rr{10 / 255.0f, 6 / 255.0f, 3 / 255.0f, std::min(1.0f, 0.3f + 0.4f * veil)};
    const SDL_Vertex v[4] = {{{r.x, r.y}, l, {0, 0}}, {{r.x + r.w, r.y}, rr, {0, 0}}, {{r.x + r.w, r.y + r.h}, rr, {0, 0}},
                             {{r.x, r.y + r.h}, l, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
    SDL_SetTextureAlphaModFloat(t.t, panelOpacity_);
    drawBaked(t, r);
    SDL_SetTextureAlphaModFloat(t.t, 1.0f);
    // The edge line stays crisp at full strength.
    const float lw = std::max(1.0f, std::round(1.5f * k_));
    const Colour a = accent_;
    fill({std::round(r.x), std::round(r.y), lw, std::round(r.h)},
         Colour{Uint8(mix(a.r, 214, 0.45f)), Uint8(mix(a.g, 150, 0.45f)), Uint8(mix(a.b, 72, 0.45f)), 255});
}

void UiSkin::well(const SDL_FRect& r) {
    const Tex& t = baked(5, int(std::round(r.w)), int(std::round(r.h)), 0);
    if (wellOpacity_ < 1.0f && t.t) SDL_SetTextureAlphaModFloat(t.t, wellOpacity_);
    drawBaked(t, r);
    if (wellOpacity_ < 1.0f && t.t) SDL_SetTextureAlphaModFloat(t.t, 1.0f);
}

void UiSkin::frame(const SDL_FRect& r) {
    well(r);
    // House-colour corner marks with a faint glow.
    const float len = px(10), t = std::max(1.0f, std::round(k_ * 1.0f));
    // House colour toned towards sand-amber, so it stays a subtle accent on the bronze.
    const Colour a{Uint8(mix(accent_.r, 236, 0.35f)), Uint8(mix(accent_.g, 180, 0.35f)), Uint8(mix(accent_.b, 96, 0.35f)), 255};
    const Colour g{a.r, a.g, a.b, 60};
    for (int i = 0; i < 4; ++i) {
        const bool right = i & 1, bottom = i & 2;
        const float x = right ? r.x + r.w - len : r.x, y = bottom ? r.y + r.h - t : r.y;
        const float vx = right ? r.x + r.w - t : r.x, vy = bottom ? r.y + r.h - len : r.y;
        fill({x - t, y - t, len + 2 * t, 3 * t}, g);
        fill({x, y, len, t}, a);
        fill({vx, vy, t, len}, a);
    }
}

void UiSkin::button(const SDL_FRect& r, Icon icon, State s, const std::string& text) {
    const SDL_FRect rr{std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h)};
    const bool labelled = !text.empty();
    drawBaked(baked(3, int(rr.w), int(rr.h), int(s), int(icon) | (labelled ? 256 : 0)), rr);
    const bool down = s == State::Pressed || s == State::Lit;
    const float off = down ? std::round(0.5f * k_) : 0;
    const Colour c = s == State::Disabled ? Colour{120, 112, 100, 255} : s == State::Hot ? Colour{255, 240, 200, 255}
                     : s == State::Lit ? Colour{255, 226, 160, 255} : kSand;
    const float is = std::round(rr.h * 0.72f);
    if (icon == Icon::Sell) {  // the "$" inside the coin rim
        const float ix = labelled ? std::round((rr.h - is) / 2 + k_) : std::round((rr.w - is) / 2);
        const float sc = is * 0.62f / kPxPerScale;
        const float tw = textW("$", sc, true);
        const Colour dc = s == State::Disabled ? Colour{112, 104, 94, 255} : Colour{246, 206, 120, 255};
        label("$", rr.x + ix + (is - tw) / 2, rr.y + (rr.h - textH(sc)) / 2 + off, dc, sc, true);
    }
    if (!labelled) return;
    float sc = ts_;
    const float x0 = icon == Icon::None ? rr.x + px(4) : rr.x + (rr.h - is) / 2 + k_ + is + px(3);
    const float room = rr.x + rr.w - px(4) - x0;
    while (sc > 0.7f && textW(text, sc, true) > room) sc -= 0.125f;
    const float tw = textW(text, sc, true);
    const float tx = icon == Icon::None ? rr.x + (rr.w - tw) / 2 : x0 + std::max(0.0f, (room - tw) / 2);
    label(text, tx, rr.y + (rr.h - textH(sc)) / 2 + off, c, sc, true);
}

SDL_FRect UiSkin::bezelInner(const SDL_FRect& r) const {
    const float b = std::round(2.0f * k_);
    return {std::round(r.x) + b, std::round(r.y) + b, std::round(r.w) - 2 * b, std::round(r.h) - 2 * b};
}

// Empty slots are panel background: translucent with the wells (Settings "Panel transparency").
void UiSkin::bezelUnder(const SDL_FRect& r, Bezel s) {
    const Tex& t = baked(1, int(std::round(r.w)), int(std::round(r.h)), int(s));
    const bool fade = s == Bezel::Empty && wellOpacity_ < 1.0f && t.t;
    if (fade) SDL_SetTextureAlphaModFloat(t.t, wellOpacity_);
    drawBaked(t, r);
    if (fade) SDL_SetTextureAlphaModFloat(t.t, 1.0f);
}

void UiSkin::bezelOver(const SDL_FRect& r, Bezel s) {
    const Tex& t = baked(2, int(std::round(r.w)), int(std::round(r.h)), int(s));
    const bool fade = s == Bezel::Empty && wellOpacity_ < 1.0f && t.t;
    if (fade) SDL_SetTextureAlphaModFloat(t.t, std::min(1.0f, wellOpacity_ + 0.2f));
    drawBaked(t, r);
    if (fade) SDL_SetTextureAlphaModFloat(t.t, 1.0f);
}

void UiSkin::lamp(float cx, float cy, float d, Colour c) {
    auto disk = [&](float x, float y, float r, Colour in, Colour out) {
        constexpr int kN = 32;
        SDL_Vertex v[kN + 2];
        auto fc = [](Colour q) { return SDL_FColor{q.r / 255.0f, q.g / 255.0f, q.b / 255.0f, q.a / 255.0f}; };
        v[0] = {{x, y}, fc(in), {0, 0}};
        for (int i = 0; i <= kN; ++i) {
            const float a = 6.2831853f * i / kN;
            v[i + 1] = {{x + r * std::cos(a), y + r * std::sin(a)}, fc(out), {0, 0}};
        }
        int idx[kN * 3];
        for (int i = 0; i < kN; ++i) {
            idx[i * 3] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = i + 2;
        }
        SDL_RenderGeometry(r_, nullptr, v, kN + 2, idx, kN * 3);
    };
    const float r = d / 2;
    disk(cx, cy, r * 1.9f, Colour{c.r, c.g, c.b, 70}, Colour{c.r, c.g, c.b, 0});  // glow
    disk(cx, cy, r, Colour{40, 28, 18, 255}, Colour{10, 7, 4, 255});              // bezel
    disk(cx, cy, r * 0.78f, c, Colour{Uint8(c.r * 0.45f), Uint8(c.g * 0.45f), Uint8(c.b * 0.45f), 255});
    disk(cx - r * 0.25f, cy - r * 0.28f, r * 0.30f, Colour{255, 255, 255, 190}, Colour{255, 255, 255, 0});
}

void UiSkin::meter(const SDL_FRect& r, float frac, Colour c, float mark) {
    well(r);
    const float in = std::max(1.0f, std::round(k_ * 1.0f));
    const SDL_FRect bar{r.x + in, r.y + in, r.w - 2 * in, r.h - 2 * in};
    frac = clamp01(frac);
    const float fh = std::round(bar.h * frac);
    if (fh > 0) {
        const SDL_FRect f{bar.x, bar.y + bar.h - fh, bar.w, fh};
        fill(f, Colour{Uint8(c.r * 0.55f), Uint8(c.g * 0.55f), Uint8(c.b * 0.55f), 255});
        fill({f.x, f.y, std::max(1.0f, std::round(f.w * 0.55f)), f.h}, c);  // lit half
        fill({f.x, f.y, f.w, std::max(1.0f, in)}, Colour{255, 255, 255, 150});
        // Segment gaps every ~4 original px.
        for (float y = bar.y + bar.h - px(4); y > f.y; y -= px(4)) fill({bar.x, std::round(y), bar.w, std::max(1.0f, std::round(k_ * 0.6f))}, Colour{0, 0, 0, 110});
    }
    if (mark >= 0) {
        const float y = std::round(bar.y + bar.h * (1 - clamp01(mark)));
        fill({r.x - in, y, r.w + 2 * in, in}, Colour{255, 236, 200, 200});
    }
}

void UiSkin::radarOff(const SDL_FRect& r, float fade, const std::string& caption) {
    fill(r, Colour{4, 4, 4, 255});
    const Uint8 a = Uint8(clamp01(fade) * 255);
    for (float y = r.y; y < r.y + r.h; y += std::max(2.0f, px(2)))
        fill({r.x, std::round(y), r.w, 1}, Colour{accent_.r, accent_.g, accent_.b, Uint8(a * 0.08f)});
    const float sc = ts_ * 1.1f;
    const float tw = textW(caption, sc, true);
    label(caption, r.x + (r.w - tw) / 2, r.y + (r.h - textH(sc)) / 2, Colour{accent_.r, accent_.g, accent_.b, a}, sc, true);
}

void UiSkin::segBar(const SDL_FRect& r, float frac, int n, bool health, Colour c) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    fill(r, Colour{6, 4, 2, 235});
    const float one = std::max(1.0f, std::round(k_ * 0.6f));
    if (health) {
        // Original thresholds: below 1/3 red, below 1/2 yellow, else green.
        c = frac < 1.0f / 3 ? Colour{220, 50, 34, 255} : frac < 0.5f ? Colour{236, 200, 50, 255} : Colour{70, 214, 80, 255};
    }
    const float inner = r.w - 2 * one, sw = (inner - (n - 1) * one) / n;
    const float fillW = frac * n;
    for (int i = 0; i < n; ++i) {
        const SDL_FRect s{std::round(r.x + one + i * (sw + one)), r.y + one, std::round(sw), r.h - 2 * one};
        const float f = std::clamp(fillW - i, 0.0f, 1.0f);
        if (f <= 0) {
            fill(s, Colour{40, 30, 21, 255});
            continue;
        }
        const Uint8 a = Uint8(f < 1 ? 110 + 145 * f : 255);
        fill(s, Colour{Uint8(c.r * 0.55f), Uint8(c.g * 0.55f), Uint8(c.b * 0.55f), a});
        fill({s.x, s.y, s.w, std::max(1.0f, std::round(s.h * 0.55f))}, Colour{c.r, c.g, c.b, a});
        fill({s.x, s.y, s.w, one}, Colour{255, 255, 255, Uint8(a * 0.35f)});
    }
}

void UiSkin::sprite(const Sprite* s, const SDL_FRect& dst, Uint8 alpha) {
    if (!s || !s->texture) return;
    SDL_ScaleMode prev = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(s->texture, &prev);
    SDL_SetTextureScaleMode(s->texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureAlphaMod(s->texture, al(alpha));
    SDL_RenderTexture(r_, s->texture, nullptr, &dst);
    SDL_SetTextureAlphaMod(s->texture, 255);
    SDL_SetTextureScaleMode(s->texture, prev);
}

void UiSkin::clock(const Sprite* bright, const Sprite* grey, const SDL_FRect& dst, float frac) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    if (frac >= 1.0f || !grey) { sprite(bright, dst); return; }
    sprite(grey, dst);
    if (frac <= 0 || !bright || !bright->texture) return;
    // A fan from the centre, clockwise from 12 o'clock, its rim on the icon's edges; texture
    // coordinates follow the positions so the bright icon shows exactly inside the wedge.
    const float cx = dst.x + dst.w / 2, cy = dst.y + dst.h / 2, hw = dst.w / 2, hh = dst.h / 2;
    const int steps = std::max(2, int(frac * 180));
    std::vector<SDL_Vertex> v;
    v.reserve(std::size_t(steps) + 2);
    const SDL_FColor white{1, 1, 1, alpha_};
    v.push_back({{cx, cy}, white, {0.5f, 0.5f}});
    for (int i = 0; i <= steps; ++i) {
        const float a = frac * 6.2831853f * i / steps;
        const float dx = std::sin(a), dy = -std::cos(a);
        const float t = std::min(std::abs(dx) > 1e-6f ? hw / std::abs(dx) : 1e9f, std::abs(dy) > 1e-6f ? hh / std::abs(dy) : 1e9f);
        const float x = cx + dx * t, y = cy + dy * t;
        v.push_back({{x, y}, white, {(x - dst.x) / dst.w, (y - dst.y) / dst.h}});
    }
    std::vector<int> idx;
    for (int i = 1; i + 1 < int(v.size()); ++i) idx.insert(idx.end(), {0, i, i + 1});
    SDL_ScaleMode prev = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(bright->texture, &prev);
    SDL_SetTextureScaleMode(bright->texture, SDL_SCALEMODE_LINEAR);
    SDL_RenderGeometry(r_, bright->texture, v.data(), int(v.size()), idx.data(), int(idx.size()));
    SDL_SetTextureScaleMode(bright->texture, prev);
}

void UiSkin::tooltip(const std::vector<TipLine>& lines, float mouseX, float mouseY, float maxX, float maxY) {
    if (lines.empty()) return;
    const float pad = px(5), lh = lineH() + px(2);
    float w = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) w = std::max(w, textW(lines[i].text, 0, i == 0));
    w += 2 * pad;
    const float h = lines.size() * lh + 2 * pad - px(2);
    // Left of the mouse (the panels are at the right / bottom), slightly below it; on screen.
    float x = std::max(2.0f, std::min(mouseX - w - px(10), maxX - w - 2));
    float y = mouseY + px(12);
    if (y + h > maxY - 2) y = mouseY - h - px(6);
    y = std::max(2.0f, y);
    const SDL_FRect box{std::round(x), std::round(y), std::round(w), std::round(h)};
    fill({box.x + px(1), box.y + px(2), box.w, box.h}, Colour{0, 0, 0, 120});  // drop shadow
    fill(box, Colour{30, 20, 13, 242});
    const float one = std::max(1.0f, std::round(k_ * 0.6f));
    outline(box, Colour{232, 168, 80, 200}, one);
    fill({box.x + one, box.y + one, box.w - 2 * one, one}, Colour{255, 240, 210, 40});
    float ty = box.y + pad;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        label(lines[i].text, box.x + pad, ty, lines[i].colour, 0, i == 0);
        ty += lh;
    }
}

}  // namespace d2k::render
