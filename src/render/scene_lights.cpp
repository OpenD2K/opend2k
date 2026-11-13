#include "render/scene_lights.hpp"

#include "game/world.hpp"
#include "render/buildup_fx.hpp"
#include "render/effect_anchors.hpp"
#include "render/effects.hpp"
#include "render/map_renderer.hpp"
#include "render/weld_fx.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace d2k::render {

namespace {

constexpr float kPi = 3.14159265f;

// Lamp colours (0xRRGGBB).
constexpr std::uint32_t kHead = 0xFFF0D2;    // warm white headlights
constexpr std::uint32_t kTail = 0xFF2A14;    // red tail lights
constexpr std::uint32_t kAmber = 0xFFA22A;   // work lamps, beacons
constexpr std::uint32_t kWarm = 0xFFC878;    // lit doors / windows
constexpr std::uint32_t kFlood = 0xFFE6B4;   // base floodlights
constexpr std::uint32_t kRed = 0xFF2010;     // warning beacons
constexpr std::uint32_t kGreen = 0x40FF60;   // pad lights, starboard
constexpr std::uint32_t kCyan = 0x78D8FF;    // research / wind trap glow
constexpr std::uint32_t kGlowOrange = 0xFF7A26;  // refinery processing glow

// Unit kinds.
enum : std::uint8_t { kUnone, kUsmall, kUmedium, kUbig, kUharvester, kUmcv, kUflyer };
// Building kinds.
enum : std::uint8_t {
    kBnone, kByard, kBrefinery, kBbarracks, kBfactory, kBstarport, kBoutpost, kBpalace, kBresearch, kBrepair, kBsilo,
    kBwindtrap, kBturret
};

bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Blink helpers (time in effect seconds; phase per object so a base does not blink in unison).
float blink(double t, float period, float on, float phase) {
    const double f = std::fmod(t / period + phase, 1.0);
    return f < on ? 1.0f : 0.0f;
}
// A damaged building's lights: steadily dimmer, and now and then (every ~3.5-5 s, per building) a short
// stutter - two quick dips - instead of a steady blink, which read as a rendering glitch (owner, 3D).
float sputter(double t, float phase) {
    const double period = 3.5 + 1.5 * phase;
    const double f = std::fmod(t + phase * period, period);  // seconds into this building's cycle
    const bool dip = f < 0.08 || (f > 0.16 && f < 0.22);
    return dip ? 0.45f : 0.8f;
}
float pulse(double t, float period, float phase) {
    return 0.5f + 0.5f * float(std::sin((t / period + phase) * 2 * kPi));
}
float hashPhase(int a, int b) {
    std::uint32_t h = std::uint32_t(a) * 0x9E3779B1u ^ std::uint32_t(b) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return float(h & 0xFFFF) / 65536.0f;
}

}  // namespace

SceneLights::SceneLights(SDL_Renderer* r, const formats::Templates& t) : r_(r), t_(t) {
    unitKind_.assign(formats::kMaxUnitTypes, kUnone);
    for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
        const auto& u = t_.bin.units[i];
        const std::string n = t_.bin.unitNames[i];
        if (u.isInfantry) continue;
        switch (u.specialBehavior) {
        case game::behaviour::kSandworm:
        case game::behaviour::kDeathHand:
        case game::behaviour::kBird:
            continue;
        case game::behaviour::kHarvester: unitKind_[std::size_t(i)] = kUharvester; continue;
        case game::behaviour::kMcv: unitKind_[std::size_t(i)] = kUmcv; continue;
        case game::behaviour::kCarryall:
        case game::behaviour::kFrigate:
        case game::behaviour::kOrnithopter: unitKind_[std::size_t(i)] = kUflyer; continue;
        case game::behaviour::kDevastator: unitKind_[std::size_t(i)] = kUbig; continue;
        default: break;
        }
        if (n.empty()) continue;
        if (has(n, "TRIKE") || has(n, "QUAD") || has(n, "RAIDER")) unitKind_[std::size_t(i)] = kUsmall;
        else unitKind_[std::size_t(i)] = kUmedium;
    }
    buildingKind_.assign(formats::kMaxBuildingTypes, kBnone);
    for (int i = 0; i < formats::kMaxBuildingTypes; ++i) {
        const auto& b = t_.bin.buildings[i];
        namespace bb = game::building_behaviour;
        std::uint8_t k = kBnone;
        switch (b.specialBehavior) {
        case bb::kOutpost: k = kBoutpost; break;
        case bb::kConstructionYard: k = kByard; break;
        case bb::kRefinery: k = kBrefinery; break;
        case bb::kSilo: k = kBsilo; break;
        case bb::kRepairPad: k = kBrepair; break;
        case bb::kLightFactory:
        case bb::kHeavyFactory: k = kBfactory; break;
        case bb::kStarport: k = kBstarport; break;
        case bb::kTurret: k = kBturret; break;
        case bb::kWall:
        case bb::kConcrete: k = kBnone; break;
        default: {
            const int g = b.buildingGroup;
            const std::string n = g >= 0 && g < formats::kMaxBuildingTypes ? t_.bin.buildingGroupNames[g] : "";
            if (has(n, "BARRACKS")) k = kBbarracks;
            else if (has(n, "PALACE")) k = kBpalace;
            else if (has(n, "RESEARCH")) k = kBresearch;
            else if (has(n, "WIND")) k = kBwindtrap;
            else if (has(n, "FACTORY") || has(n, "CONSTRUCTION")) k = has(n, "CONSTRUCTION") ? kByard : kBfactory;
            else if (has(n, "TURRET")) k = kBturret;
            else if (has(n, "OUTPOST")) k = kBoutpost;
            else if (has(n, "REFINERY")) k = kBrefinery;
            else if (has(n, "STAR")) k = kBstarport;
            else if (has(n, "REPAIR")) k = kBrepair;
            else if (has(n, "SILO")) k = kBsilo;
        }
        }
        if (b.hitPoints == 0) k = kBnone;
        buildingKind_[std::size_t(i)] = k;
    }
    buildTextures();
}

SceneLights::~SceneLights() {
    for (SDL_Texture* t : {poolTex_, coneTex_, flareTex_})
        if (t) SDL_DestroyTexture(t);
}

void SceneLights::buildTextures() {
    if (!r_) return;
    constexpr int S = 128;
    std::vector<std::uint32_t> px(std::size_t(S) * S);
    auto make = [&](auto&& alphaAt) {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                const float u = (float(x) + 0.5f) / S, v = (float(y) + 0.5f) / S;
                const float a = std::clamp(alphaAt(u, v), 0.0f, 1.0f);
                const std::uint32_t c = std::uint32_t(std::lround(a * 255));
                // Premultiplied white (ARGB8888): rgb = alpha.
                px[std::size_t(y) * S + x] = (c << 24) | (c << 16) | (c << 8) | c;
            }
        SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, S, S);
        if (t) {
            SDL_UpdateTexture(t, nullptr, px.data(), S * 4);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        }
        return t;
    };
    // Pool: a soft disc, brightest in the middle (quadratic falloff like the 3D point lights).
    poolTex_ = make([](float u, float v) {
        const float d2 = ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 4;
        const float a = std::max(0.0f, 1 - d2);
        return a * a;
    });
    // Cone: apex at the left middle, along +u; the beam fades with the distance and its angle
    // (u = 0..1 along, v = 0..1 across; the quad is as wide as the cone at its far end).
    coneTex_ = make([](float u, float v) {
        const float along = u;
        const float across = std::abs(v - 0.5f) * 2;  // 0 centre .. 1 edge of the quad
        if (along <= 0.0f) return 0.0f;
        const float rel = across / std::max(along, 1e-3f);  // 1 = the cone's edge
        const float edge = std::clamp((1.0f - rel) / 0.45f, 0.0f, 1.0f);
        const float fall = std::max(0.0f, 1 - along * along) * std::min(1.0f, along / 0.08f);
        const float hot = 0.6f + 0.4f * (1 - along);
        return edge * edge * (3 - 2 * edge) * fall * hot;
    });
    // Flare: a hot core with a soft halo.
    flareTex_ = make([](float u, float v) {
        const float d = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2;
        const float core = std::exp(-d * d * 28.0f);
        const float halo = std::max(0.0f, 1 - d) * std::max(0.0f, 1 - d) * 0.45f;
        return core + halo;
    });
    // The Light blend (dst * (1 + src)) as the Modern effects use it.
    static const SDL_BlendMode kLight =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                   SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    for (SDL_Texture* t : {poolTex_, coneTex_})
        if (t && !SDL_SetTextureBlendMode(t, kLight)) {
            lightBlendOk_ = false;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
        }
    if (flareTex_) SDL_SetTextureBlendMode(flareTex_, SDL_BLENDMODE_ADD);
}

bool SceneLights::hidden(const game::World& w, float x, float y) const {
    if (x < view_[0] || y < view_[1] || x > view_[2] || y > view_[3]) return true;
    const int cx = int(std::floor(x / 32)), cy = int(std::floor(y / 32));
    if (!w.inBounds(cx, cy)) return true;
    return showShroud_ && (w.cell(cx, cy).shroud & 0x0F) == 1;
}

void SceneLights::pool(float x, float y, float z, float reach, std::uint32_t rgb, float a, float prio) {
    if (a < 0.01f) return;
    Lamp l;
    l.kind = Lamp::Pool;
    l.x = x;
    l.y = y;
    l.z = z;
    l.reach = reach;
    l.r = float((rgb >> 16) & 0xFF) / 255.0f;
    l.g = float((rgb >> 8) & 0xFF) / 255.0f;
    l.b = float(rgb & 0xFF) / 255.0f;
    l.a = a;
    l.priority = prio;
    lamps_.push_back(l);
}

void SceneLights::cone(float x, float y, float z, float len, float dx, float dy, float spread, std::uint32_t rgb, float a,
                       float prio) {
    if (a < 0.01f) return;
    Lamp l;
    l.kind = Lamp::Cone;
    l.x = x;
    l.y = y;
    l.z = z;
    l.reach = len;
    l.dirX = dx;
    l.dirY = dy;
    l.spread = spread;
    l.r = float((rgb >> 16) & 0xFF) / 255.0f;
    l.g = float((rgb >> 8) & 0xFF) / 255.0f;
    l.b = float(rgb & 0xFF) / 255.0f;
    l.a = a;
    l.priority = prio;
    lamps_.push_back(l);
}

void SceneLights::flare(float x, float y, float z, float size, std::uint32_t rgb, float a, float pull) {
    if (a < 0.01f) return;
    Flare f;
    f.pull = pull;
    f.x = x;
    f.y = y;
    f.z = z;
    f.size = size;
    f.r = float((rgb >> 16) & 0xFF) / 255.0f;
    f.g = float((rgb >> 8) & 0xFF) / 255.0f;
    f.b = float(rgb & 0xFF) / 255.0f;
    f.a = a;
    flares_.push_back(f);
}

void SceneLights::update(const game::World& w, const SceneLight& light, double time, const float view[4], bool showShroud,
                         const Effects* fx, const EffectAnchors* anchors, const std::vector<std::uint8_t>* redesigned,
                         const std::vector<std::uint8_t>* redesignedUnits) {
    lamps_.clear();
    flares_.clear();
    world_ = &w;
    if (light.lamps <= 0.001f) return;
    constexpr float kMargin = 160;  // lamps whose light can reach into the view
    view_[0] = view[0] - kMargin;
    view_[1] = view[1] - kMargin;
    view_[2] = view[2] + kMargin;
    view_[3] = view[3] + kMargin + 64;
    showShroud_ = showShroud;
    const float k = light.lamps;
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& side = w.sides()[std::size_t(s)];
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const int ty = side.buildings[std::size_t(i)].type;
            const int re = redesigned && ty >= 0 && std::size_t(ty) < redesigned->size() ? (*redesigned)[std::size_t(ty)] : 0;
            addBuilding(w, s, i, k, time, anchors, re, light);
        }
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[std::size_t(i)];
            if (!u.alive || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            const std::uint8_t kind = unitKind_[std::size_t(u.type)];
            if (kind == kUnone) continue;
            if (redesignedUnits && std::size_t(u.type) < redesignedUnits->size() && (*redesignedUnits)[std::size_t(u.type)]) {
                float facing = float(u.facing & 31);
                if (fx && kind != kUflyer)
                    if (const Effects::UnitVisual* v = fx->unitVisual(s, i)) facing = v->facing;
                addModelUnit(w, s, i, facing, k, time, anchors);
                continue;
            }
            if (kind == kUflyer) {
                addFlyer(w, s, i, k, time);
                continue;
            }
            float facing = float(u.facing & 31);
            if (fx)
                if (const Effects::UnitVisual* v = fx->unitVisual(s, i)) facing = v->facing;
            addVehicle(w, s, i, facing, k, time);
        }
    }
}

void SceneLights::addVehicle(const game::World& w, int s, int i, float facing, float k, double time) {
    const auto& u = w.sides()[std::size_t(s)].units[std::size_t(i)];
    // Not on the ground / not drawn / cloaked or fading.
    if (u.flags & (0x200140u | 0x1000000u | 0x4u)) return;
    if (u.stealthTimer != 0 || u.state == game::UnitState::Dead) return;
    const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
    if (hidden(w, x, y)) return;
    const std::uint8_t kind = unitKind_[std::size_t(u.type)];
    const float a = facing * (2 * kPi / 32);
    const float dx = std::sin(a), dy = -std::cos(a);  // forward (0 = north, clockwise)
    const float px = -dy, py = dx;                    // to the right
    float half = 8, wide = 3.5f, reach = 72, hz = 4;
    switch (kind) {
    case kUsmall: half = 7; wide = 3; reach = 64; hz = 3.5f; break;
    case kUmedium: half = 10; wide = 4.5f; reach = 80; hz = 4.5f; break;
    case kUbig:
    case kUharvester:
    case kUmcv: half = 13; wide = 6; reach = 92; hz = 6; break;
    default: break;
    }
    const float fx0 = x + dx * half, fy0 = y + dy * half;
    // Headlights: one beam on the ground ahead, two lamps.
    cone(fx0, fy0, hz, reach, dx, dy, 0.42f, kHead, 0.85f * k, 3);
    flare(fx0 + px * wide, fy0 + py * wide, hz, 5.5f, kHead, 0.75f * std::max(k, 0.25f));
    flare(fx0 - px * wide, fy0 - py * wide, hz, 5.5f, kHead, 0.75f * std::max(k, 0.25f));
    // Tail lights.
    const float bx = x - dx * half, by = y - dy * half;
    flare(bx + px * wide, by + py * wide, hz, 4.5f, kTail, 0.8f * std::max(k, 0.3f));
    flare(bx - px * wide, by - py * wide, hz, 4.5f, kTail, 0.8f * std::max(k, 0.3f));
    pool(bx - dx * 4, by - dy * 4, 2, 26, kTail, 0.35f * k, 0.5f);
    if (kind == kUharvester || kind == kUmcv) {
        // Rotating amber beacon on the roof; harvesting: work lamps on both sides.
        const float ph = hashPhase(s, i);
        const float b = pulse(time, 0.9f, ph);
        flare(x, y, 16, 7.0f, kAmber, (0.35f + 0.65f * b) * std::max(k, 0.3f));
        pool(x, y, 16, 46, kAmber, 0.22f * b * k, 0.8f);
        if (kind == kUharvester && u.state == game::UnitState::Harvesting) {
            pool(x + px * 16, y + py * 16, 10, 58, kAmber, 0.55f * k, 1.5f);
            pool(x - px * 16, y - py * 16, 10, 58, kAmber, 0.55f * k, 1.5f);
        }
    }
}

// Redesigned units (the owner: "what is the floating light over the harvester?"): the lamps above sit at offsets
// measured on the original sprites; the redesigned models are shaped differently (the harvester's cab, tanks'
// noses at other heights / lengths), so those beacons and headlamps hung in the air beside them. A unit drawn
// from a redesigned art (3D model or a pack frame rendered from one) lights only its model's own lamp anchors
// (docs/3d-models.md "Anchors"; models.ini anchor.fx_light_*), flares pulled only 3 px towards the eye in 3D.
void SceneLights::addModelUnit(const game::World& w, int s, int i, float facing, float k, double time,
                               const EffectAnchors* anchors) {
    if (!anchors) return;
    const auto& u = w.sides()[std::size_t(s)].units[std::size_t(i)];
    const bool flyer = unitKind_[std::size_t(u.type)] == kUflyer;
    if (flyer ? (u.flags & (0x200000u | 0x1000000u)) != 0 : (u.flags & (0x200140u | 0x1000000u | 0x4u)) != 0) return;
    if (u.stealthTimer != 0 || u.state == game::UnitState::Dead) return;
    const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
    if (hidden(w, x, y)) return;
    static thread_local std::vector<EffectAnchor> pts;
    if (!anchors->unitAnchors(w, s, i, "fx_light", pts) || pts.empty()) return;
    const float a = facing * (2 * kPi / 32);
    const float dx = std::sin(a), dy = -std::cos(a);  // forward on the ground
    const float ph = hashPhase(s, i);
    constexpr float kPull = 3;
    auto named = [&](const char* prefix, std::vector<EffectAnchor>& out) {
        return anchors->unitAnchors(w, s, i, prefix, out) && !out.empty();
    };
    static thread_local std::vector<EffectAnchor> q;
    if (named("fx_light_head", q)) {
        float cx = 0, cy = 0, cz = 0;
        for (const auto& p : q) {
            flare(p.x, p.y, p.z, 4.5f * p.size, kHead, 0.75f * std::max(k, 0.25f), kPull);
            cx += p.x, cy += p.y, cz += p.z;
        }
        const float n = float(q.size());
        const float reach = unitKind_[std::size_t(u.type)] == kUsmall ? 64.0f : unitKind_[std::size_t(u.type)] == kUmedium ? 80.0f : 92.0f;
        cone(cx / n, cy / n, std::max(2.0f, cz / n), reach, dx, dy, 0.42f, kHead, 0.85f * k, 3);
    }
    if (named("fx_light_tail", q)) {
        float cx = 0, cy = 0;
        for (const auto& p : q) {
            flare(p.x, p.y, p.z, 3.5f * p.size, kTail, 0.8f * std::max(k, 0.3f), kPull);
            cx += p.x, cy += p.y;
        }
        const float n = float(q.size());
        pool(cx / n - dx * 4, cy / n - dy * 4, 2, 26, kTail, 0.35f * k, 0.5f);
    }
    if (named("fx_light_beacon", q)) {
        // A rotating amber beacon: the flare pulses as the reflector turns, its light sweeps round.
        const float b = pulse(time, 0.9f, ph);
        for (const auto& p : q) {
            flare(p.x, p.y, p.z, 5.0f * p.size, kAmber, (0.35f + 0.65f * b) * std::max(k, 0.3f), kPull);
            const float sw = float(time / 0.9 + ph) * 2 * kPi;
            pool(p.x + std::cos(sw) * 10, p.y + std::sin(sw) * 8, p.z, 46, kAmber, 0.22f * b * k, 0.8f);
        }
    }
    if (u.state == game::UnitState::Harvesting && named("fx_light_work", q)) {
        // Work lamps: the lit lamp on the model, its light thrown on the ground ahead where the spice is cut.
        for (const auto& p : q) {
            flare(p.x, p.y, p.z, 4.0f * p.size, kAmber, 0.8f * std::max(k, 0.3f), kPull);
            pool(p.x + dx * 18, p.y + dy * 18, std::max(4.0f, p.z), 58, kAmber, 0.55f * k, 1.5f);
        }
    }
    if (flyer) {
        if (named("fx_light_port", q))
            for (const auto& p : q) flare(p.x, p.y, p.z, 4.5f * p.size, kRed, 0.85f * std::max(k, 0.35f), kPull);
        if (named("fx_light_starboard", q))
            for (const auto& p : q) flare(p.x, p.y, p.z, 4.5f * p.size, kGreen, 0.85f * std::max(k, 0.35f), kPull);
        if (named("fx_light_strobe", q))
            for (const auto& p : q)
                flare(p.x, p.y, p.z, 7.0f * p.size, 0xFFFFFF, blink(time, 1.1f, 0.08f, hashPhase(s, i + 77)) * std::max(k, 0.4f), kPull);
    }
}

void SceneLights::addFlyer(const game::World& w, int s, int i, float k, double time) {
    const auto& u = w.sides()[std::size_t(s)].units[std::size_t(i)];
    if (u.flags & (0x200000u | 0x1000000u)) return;
    const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
    if (hidden(w, x, y)) return;
    const float z = float(game::fixToPixel(u.posZ) & 0xff);
    const float a = float(u.facing & 31) * (2 * kPi / 32);
    const float dx = std::sin(a), dy = -std::cos(a), px = -dy, py = dx;
    const float span = t_.bin.units[u.type].specialBehavior == game::behaviour::kFrigate ? 22.0f : 12.0f;
    const float ph = hashPhase(s, i + 77);
    // Navigation lights: red port, green starboard, a white strobe at the tail.
    flare(x - px * span, y - py * span, z, 5, kRed, 0.85f * std::max(k, 0.35f));
    flare(x + px * span, y + py * span, z, 5, kGreen, 0.85f * std::max(k, 0.35f));
    flare(x - dx * span * 0.8f, y - dy * span * 0.8f, z, 9, 0xFFFFFF, blink(time, 1.1f, 0.08f, ph) * std::max(k, 0.4f));
}

float buildingGlow(const game::World& w, const formats::Templates& T, int s, int i, float lamps, double time,
                   bool dayPulse) {
    if (lamps <= 0.001f || s < 0 || s >= int(w.sides().size())) return 0;
    const auto& side = w.sides()[std::size_t(s)];
    const auto& b = side.buildings[std::size_t(i)];
    if (!b.alive || b.state == 17 || (b.flags & 0x100006) || b.type < 0 || b.type >= formats::kMaxBuildingTypes) return 0;
    const auto& t = T.bin.buildings[b.type];
    const int g = t.buildingGroup;
    const bool windTrap = g >= 0 && g < formats::kMaxBuildingTypes && has(T.bin.buildingGroupNames[g], "WIND");
    const float ph = hashPhase(s * 131 + i, b.type);
    float k = 0.25f + 0.95f * lamps;
    if (t.hitPoints > 0 && b.health * 2 < std::int32_t(t.hitPoints)) k *= sputter(time, ph);
    if (windTrap && dayPulse) {
        if (side.powerPercent == 0) return 0;
        // The rhythm of the original's working animation (UpdateBuilding: animCounter runs over
        // frames x animationSpeed ticks while above half health; time interpolates within the tick),
        // else (damaged: no animation) the slow breath. A peaked pulse: the lights flash, then fade.
        float p = pulse(time, 2.2f, ph);
        const int a = t.buildingAnimation;
        if ((b.flags & 0x30) && (b.flags & 0x40) && a >= 0 && a < formats::kMaxBuildingTypes &&
            T.bin.buildingAnimationFrames[a] > 0) {
            const int speed = t.animationSpeed ? t.animationSpeed : 1;
            const double len = double(T.bin.buildingAnimationFrames[a]) * speed;
            const double frac = std::clamp(time * 20.0 - double(w.gameTicks()), 0.0, 1.0);
            const double f = std::fmod((double(b.animCounter) + frac) / len, 1.0);
            p = 0.5f + 0.5f * float(std::cos(f * 2 * kPi));
        }
        p = p * p;
        k = k * (0.55f + 0.45f * p) + 0.55f * std::clamp(1.0f - lamps, 0.0f, 1.0f) * p;
    } else if (windTrap) {
        k *= 0.78f + 0.3f * pulse(time, 2.2f, ph);  // the energy core breathes; it makes the power
    } else if (side.powerPercent == 0) {
        return 0;
    } else if (side.powerPercent < 100) {
        k *= 0.22f + 0.22f * blink(time, 0.23f, 0.7f, ph);
    }
    return k;
}

void SceneLights::addBuilding(const game::World& w, int s, int i, float k, double time, const EffectAnchors* anchors,
                              int redesigned, const SceneLight& light) {
    const auto& side = w.sides()[std::size_t(s)];
    const auto& b = side.buildings[std::size_t(i)];
    if (!b.alive || b.state == 17 || (b.flags & 0x100000) || b.type < 0 || b.type >= formats::kMaxBuildingTypes) return;
    const std::uint8_t kind = buildingKind_[std::size_t(b.type)];
    if (kind == kBnone) return;
    const auto& t = t_.bin.buildings[b.type];
    const std::uint32_t m = t.tilesOccupiedAll ? t.tilesOccupiedAll : t.tilesOccupiedSolid;
    int c0 = 4, c1 = -1, r0 = 4, r1 = -1;
    for (int q = 0; q < 16; ++q)
        if ((m >> q) & 1) {
            c0 = std::min(c0, q % 4);
            c1 = std::max(c1, q % 4);
            r0 = std::min(r0, q / 4);
            r1 = std::max(r1, q / 4);
        }
    if (c1 < 0) c0 = r0 = c1 = r1 = 0;
    const float X0 = float((b.tileX + c0) * 32), Y0 = float((b.tileY + r0) * 32);
    const float W = float((c1 - c0 + 1) * 32), H = float((r1 - r0 + 1) * 32);
    if (hidden(w, X0 + W * 0.5f, Y0 + H * 0.5f) && hidden(w, X0, Y0) && hidden(w, X0 + W, Y0 + H)) return;
    // A redesigned building rising (render/buildup_fx.hpp): no lamps yet. A 3D model's construction
    // band glows; the 2D rubble build-up has only a dim work light over the heap.
    if (const float p = redesigned ? buildupProgress(t_, b) : -1.0f; p >= 0) {
        if (p > 0.02f && p < 0.98f) {
            if (redesigned == 2) pool(X0 + W * 0.5f, Y0 + H * 0.6f, 6 + 30 * p, 0.6f * std::max(W, H), kGlowOrange, 0.3f * k, 1.2f);
            else pool(X0 + W * 0.5f, Y0 + H * 0.7f, 14, 0.45f * std::max(W, H), kWarm, 0.12f * k, 1.0f);
        }
        return;
    }
    // Under construction (build-up running): no lamps yet. Low power: dimmed and flickering.
    float kk = k;
    if (side.powerPercent < 100) kk *= 0.35f + 0.25f * blink(time, 0.23f, 0.7f, hashPhase(i, s));
    const float ph = hashPhase(s * 131 + i, b.type);
    constexpr float kB = 1.25f;  // building pools: lit doors, bays and windows read at night
    auto at = [&](float fx, float fy, float& ox, float& oy) {
        ox = X0 + W * fx;
        oy = Y0 + H * fy;
    };
    float ax = 0, ay = 0;
    // Model anchors (3D): fx_light_* = warm lamps where the model has them.
    bool anchored = false;
    if (anchors) {
        static thread_local std::vector<EffectAnchor> tmp;
        tmp.clear();
        if (anchors->buildingAnchors(w, s, i, "fx_light", tmp) && !tmp.empty()) {
            anchored = true;
            for (const auto& an : tmp) {
                // The lamp bead is on the model (and glows itself when emissive): a small halo hugging it.
                flare(an.x, an.y, an.z, (redesigned ? 4.0f : 6.0f) * an.size, kWarm, (redesigned ? 0.6f : 0.9f) * std::max(kk, 0.2f),
                      redesigned ? 3.0f : 14.0f);
                pool(an.x, an.y, std::max(6.0f, an.z * 0.5f), 54 * an.size, kWarm, 0.45f * kk * kB, 1.2f);
            }
        }
    }
    // Weld flashes (render/weld_fx.hpp): a short blue-white light where the arc is this tick.
    if (anchors) {
        static thread_local std::vector<EffectAnchor> weld;
        WeldFlash fl;
        if (anchors->buildingAnchors(w, s, i, "fx_weld", weld) && weldFlash(w, b, t_, int(weld.size()), fl)) {
            const EffectAnchor& an = weld[std::size_t(fl.anchor)];
            pool(an.x, an.y, std::max(4.0f, an.z), 64 * an.size * fl.size, 0xA8C8FF, 0.85f * fl.a * k, 1.8f);
        }
    }
    if (redesigned) {
        // A redesigned building (docs/enhanced-graphics.md "Lamps"): no lamps at the points measured on the
        // original sprites - the new art is laid out differently (models shrunk onto their cells, other roof
        // heights, aprons), so those floated in the air or on the empty ground beside it. Its visible lights
        // are its own emissive surfaces (buildingGlow) and fx_light anchors; here only the light they throw:
        // a soft pool from inside the footprint, following the glow (power, damage, the wind trap's pulse).
        const float gl = buildingGlow(w, t_, s, i, light.lamps, time) / 1.2f;
        if (gl <= 0.01f) return;
        // Star port landing lights while the frigate comes in (the original's one-shot building animation of
        // the primary star port, re/notes/starport.md "Flight"; the 2D pack draws them as its overlay frames):
        // in 3D the model's fx_landing* anchors (models.ini: the lamp sockets in the pit) light up in sequence
        // as the overlay frames do, then all blink.
        if (kind == kBstarport && anchors && (b.flags & 0x30) && (b.flags & 0x40) && b.health * 2 >= std::int32_t(t.hitPoints)) {
            static thread_local std::vector<EffectAnchor> pads;
            pads.clear();
            const int a = t.buildingAnimation;
            if (a >= 0 && a < formats::kMaxBuildingTypes && anchors->buildingAnchors(w, s, i, "fx_landing", pads) && !pads.empty()) {
                const int frames = std::max(1, int(t_.bin.buildingAnimationFrames[a]));
                const int k = b.animCounter / std::max(1, int(t_.bin.buildings[a].animationSpeed));
                const int n = int(pads.size());
                const int lit = std::min(n, 1 + k * n / std::max(1, frames - 2));
                const float on = k >= frames - 3 ? blink(time, 0.5f, 0.55f, ph) : 1.0f;
                for (int q = 0; q < lit; ++q) {
                    const auto& p = pads[std::size_t(q)];
                    const std::uint32_t c = q == n - 1 ? kRed : q % 3 == 2 ? kAmber : kGreen;
                    flare(p.x, p.y, p.z, 4.5f * p.size, c, 0.95f * on * std::max(kk, 0.35f), 3.0f);
                }
                at(0.5f, 0.55f, ax, ay);
                pool(ax, ay, 14, 0.9f * std::max(W, H), kGreen, 0.25f * on * kk * kB, 1.6f);
            }
        }
        at(0.5f, 0.55f, ax, ay);
        const float reach = 0.9f * std::max(W, H);
        switch (kind) {
        case kBwindtrap: pool(ax, ay, 18, reach * 1.2f, kCyan, 0.32f * gl * kB, 1.3f); break;
        case kBrefinery: pool(ax, ay, 16, reach, kGlowOrange, 0.35f * gl * kB, 1.1f); break;
        case kBresearch: pool(ax, ay, 16, reach, kCyan, 0.35f * gl * kB, 1.0f); break;
        case kBturret:
        case kBsilo: break;
        default: pool(ax, ay, 14, reach * 0.8f, kWarm, 0.22f * gl * kB, 0.8f); break;
        }
        return;
    }
    switch (kind) {
    case kByard:
        at(0.5f, 1.02f, ax, ay);
        pool(ax, ay, 12, 76, kFlood, 0.5f * kk * kB, 1.4f);
        if (!anchored) {
            at(0.3f, 0.92f, ax, ay);
            flare(ax, ay, 10, 6, kWarm, 0.85f * std::max(kk, 0.2f));
            at(0.7f, 0.92f, ax, ay);
            flare(ax, ay, 10, 6, kWarm, 0.85f * std::max(kk, 0.2f));
        }
        at(0.82f, 0.25f, ax, ay);
        flare(ax, ay, 36, 7, kRed, blink(time, 1.4f, 0.5f, ph) * std::max(kk, 0.25f));
        break;
    case kBrefinery:
        at(0.38f, 0.62f, ax, ay);
        pool(ax, ay, 14, 68, kGlowOrange, (0.45f + 0.12f * pulse(time, 2.3f, ph)) * kk * kB, 1.3f);
        at(0.88f, 0.2f, ax, ay);
        flare(ax, ay, 34, 6, kRed, blink(time, 1.6f, 0.5f, ph) * std::max(kk, 0.25f));
        at(0.6f, 1.0f, ax, ay);
        flare(ax, ay, 8, 5, kAmber, 0.8f * std::max(kk, 0.2f));
        break;
    case kBbarracks:
    case kBpalace:
        at(0.5f, 1.02f, ax, ay);
        pool(ax, ay, 10, kind == kBpalace ? 80 : 56, kWarm, 0.5f * kk * kB, 1.2f);
        if (!anchored) {
            at(0.35f, 0.94f, ax, ay);
            flare(ax, ay, 9, 5.5f, kWarm, 0.8f * std::max(kk, 0.2f));
            at(0.65f, 0.94f, ax, ay);
            flare(ax, ay, 9, 5.5f, kWarm, 0.8f * std::max(kk, 0.2f));
        }
        if (kind == kBpalace) {
            at(0.12f, 0.2f, ax, ay);
            flare(ax, ay, 40, 6, kRed, blink(time, 1.8f, 0.5f, ph) * std::max(kk, 0.25f));
            at(0.88f, 0.2f, ax, ay);
            flare(ax, ay, 40, 6, kRed, blink(time, 1.8f, 0.5f, ph + 0.5f) * std::max(kk, 0.25f));
        }
        break;
    case kBfactory: {
        // The bay: a floodlight on the apron in front, two lamps on the door posts.
        at(0.5f, 1.08f, ax, ay);
        pool(ax, ay, 14, 92, kFlood, 0.55f * kk * kB, 1.5f);
        if (!anchored) {
            at(0.18f, 0.96f, ax, ay);
            flare(ax, ay, 14, 6, kFlood, 0.85f * std::max(kk, 0.2f));
            at(0.82f, 0.96f, ax, ay);
            flare(ax, ay, 14, 6, kFlood, 0.85f * std::max(kk, 0.2f));
        }
        at(0.85f, 0.22f, ax, ay);
        flare(ax, ay, 34, 6, kRed, blink(time, 1.5f, 0.5f, ph) * std::max(kk, 0.25f));
        break;
    }
    case kBstarport: {
        // Landing lights running round the pad, a dim glow over it.
        at(0.5f, 0.55f, ax, ay);
        pool(ax, ay, 16, 96, kFlood, 0.35f * kk * kB, 1.2f);
        constexpr int kN = 10;
        const int lit = int(std::fmod(time * 6.0, double(kN)));
        for (int q = 0; q < kN; ++q) {
            const float a = (float(q) / kN) * 2 * kPi;
            const float fx = 0.5f + 0.42f * std::cos(a), fy = 0.55f + 0.38f * std::sin(a);
            at(fx, fy, ax, ay);
            const float on = q == lit ? 1.0f : q == (lit + kN - 1) % kN ? 0.45f : 0.18f;
            flare(ax, ay, 4, 5, kAmber, on * std::max(kk, 0.2f));
        }
        break;
    }
    case kBoutpost:
        at(0.5f, 0.3f, ax, ay);
        flare(ax, ay, 38, 7, kRed, (0.25f + 0.75f * blink(time, 1.2f, 0.35f, ph)) * std::max(kk, 0.25f));
        at(0.5f, 1.0f, ax, ay);
        pool(ax, ay, 10, 52, kWarm, 0.4f * kk * kB, 1.0f);
        at(0.25f, 0.85f, ax, ay);
        flare(ax, ay, 10, 4.5f, kGreen, 0.7f * std::max(kk, 0.2f));
        break;
    case kBresearch:
        at(0.5f, 0.6f, ax, ay);
        pool(ax, ay, 14, 64, kCyan, (0.4f + 0.1f * pulse(time, 3.0f, ph)) * kk * kB, 1.1f);
        at(0.5f, 0.92f, ax, ay);
        flare(ax, ay, 10, 6, kCyan, 0.8f * std::max(kk, 0.2f));
        break;
    case kBrepair: {
        at(0.5f, 0.55f, ax, ay);
        pool(ax, ay, 8, 70, kGreen, 0.25f * kk * kB, 1.0f);
        const float c[4][2] = {{0.12f, 0.15f}, {0.88f, 0.15f}, {0.12f, 0.92f}, {0.88f, 0.92f}};
        for (int q = 0; q < 4; ++q) {
            at(c[q][0], c[q][1], ax, ay);
            flare(ax, ay, 4, 5, kGreen, (0.35f + 0.65f * blink(time, 1.0f, 0.5f, ph + q * 0.25f)) * std::max(kk, 0.2f));
        }
        break;
    }
    case kBsilo:
        at(0.5f, 0.8f, ax, ay);
        flare(ax, ay, 16, 4.5f, kAmber, 0.7f * std::max(kk, 0.2f));
        pool(ax, ay, 8, 34, kAmber, 0.25f * kk * kB, 0.6f);
        break;
    case kBwindtrap:
        at(0.5f, 0.75f, ax, ay);
        pool(ax, ay, 12, 50, kCyan, 0.35f * kk * kB, 0.9f);
        flare(ax, ay, 20, 5, kCyan, 0.7f * std::max(kk, 0.2f));
        break;
    case kBturret:
        at(0.5f, 0.5f, ax, ay);
        flare(ax, ay, 18, 4.5f, kRed, (0.4f + 0.6f * blink(time, 2.0f, 0.5f, ph)) * std::max(kk, 0.25f));
        break;
    default: break;
    }
}

void SceneLights::draw2D(const Camera& cam, int viewW, int viewH) {
    if (!r_ || (lamps_.empty() && flares_.empty())) return;
    const float zm = cam.zoom, ox = std::round(cam.x * zm), oy = std::round(cam.y * zm);
    vPool_.clear();
    vCone_.clear();
    vFlare_.clear();
    // As the Modern effects' light glows (premultiplied x 1.6). The Light blend multiplies what is
    // drawn (dst * (1 + c)), which the time of day already darkened by the grade: a lamp lights the
    // ground's own colour, so its strength is divided by the grade (setGrade) - in up to three passes
    // of at most x2 each (vertex colours stop at 1).
    const float gain = lightBlendOk_ ? 1.6f : 0.7f;
    auto quad = [](std::vector<SDL_Vertex>& v, const SDL_FPoint p[4], SDL_FColor c) {
        const SDL_FPoint uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) v.push_back(SDL_Vertex{p[k], c, uv[k]});
    };
    for (const Lamp& l : lamps_) {
        const float a = std::min(1.0f, l.a) * gain;
        float want[3] = {l.r * a * boost_[0], l.g * a * boost_[1], l.b * a * boost_[2]};  // total factor - 1
        const float top = std::max({want[0], want[1], want[2]});
        const int passes = !lightBlendOk_ || top <= 1.0f ? 1 : top <= 3.0f ? 2 : 3;
        for (float& v : want) v = std::min(1.0f, passes == 1 ? v : std::pow(1.0f + std::min(v, 7.0f), 1.0f / float(passes)) - 1.0f);
        const SDL_FColor c{want[0], want[1], want[2], 1};
        const float sx = l.x * zm - ox, sy = l.y * zm - oy;
        const float R = l.reach * zm;
        if (sx < -R || sy < -R || sx > viewW + R || sy > viewH + R) continue;
        if (l.kind == Lamp::Pool) {
            const float h = R * 0.5f, hv = h * 0.86f;  // a little flattened: light lying on the ground
            const SDL_FPoint p[4] = {{sx - h, sy - hv}, {sx + h, sy - hv}, {sx + h, sy + hv}, {sx - h, sy + hv}};
            for (int k = 0; k < passes; ++k) quad(vPool_, p, c);
        } else {
            const float w = R * std::tan(l.spread);
            const float fx = l.dirX, fy = l.dirY, nx = -fy, ny = fx;
            const SDL_FPoint p[4] = {{sx - nx * w, sy - ny * w},
                                     {sx + fx * R - nx * w, sy + fy * R - ny * w},
                                     {sx + fx * R + nx * w, sy + fy * R + ny * w},
                                     {sx + nx * w, sy + ny * w}};
            for (int k = 0; k < passes; ++k) quad(vCone_, p, c);
        }
    }
    for (const Flare& f : flares_) {
        const float s = std::max(2.5f, f.size * zm);
        const float sx = f.x * zm - ox, sy = (f.y - f.z) * zm - oy;
        if (sx < -s || sy < -s || sx > viewW + s || sy > viewH + s) continue;
        const float a = std::min(1.0f, f.a);
        const SDL_FColor c{f.r * a, f.g * a, f.b * a, 1};
        const float h = s;  // the halo reaches twice the lamp's size
        const SDL_FPoint p[4] = {{sx - h, sy - h}, {sx + h, sy - h}, {sx + h, sy + h}, {sx - h, sy + h}};
        quad(vFlare_, p, c);
    }
    if (!vPool_.empty()) SDL_RenderGeometry(r_, poolTex_, vPool_.data(), int(vPool_.size()), nullptr, 0);
    if (!vCone_.empty()) SDL_RenderGeometry(r_, coneTex_, vCone_.data(), int(vCone_.size()), nullptr, 0);
    if (!vFlare_.empty()) SDL_RenderGeometry(r_, flareTex_, vFlare_.data(), int(vFlare_.size()), nullptr, 0);
}

}  // namespace d2k::render

