#include "render/effects.hpp"

#include "render/effects_internal.hpp"

#include "render/map_renderer.hpp"
#include "render/tileset.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace d2k::render {

namespace {

using namespace fx;
constexpr std::size_t kMaxTreads = 4000;
constexpr float kTreadLife = 9.0f;

float wrap32(float d) {  // facing difference into (-16, 16]
    while (d > 16) d -= 32;
    while (d <= -16) d += 32;
    return d;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Smooth value noise for the smoke texture.
float noise2(float x, float y, std::uint32_t seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - float(xi), fy = y - float(yi);
    auto h = [&](int a, int b) { return float(hash32(std::uint32_t(a) * 73856093u ^ std::uint32_t(b) * 19349663u ^ seed) & 0xFFFF) / 65535.0f; };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

}  // namespace

Effects::Effects(SDL_Renderer* r, const formats::Templates& templates) : r_(r), t_(templates) {
    buildTextures();
    buildCraterTexture();
    classify();
    particles_.reserve(kMaxParticles);
    treads_.reserve(kMaxTreads);
}

Effects::~Effects() {
    if (atlasTex_) SDL_DestroyTexture(atlasTex_);
    if (vignette_) SDL_DestroyTexture(vignette_);
    if (craterTex_) SDL_DestroyTexture(craterTex_);
}

void Effects::reset() {
    particles_.clear();
    treads_.clear();
    treadHead_ = 0;
    lights_.clear();
    beams_.clear();
    sonic_.clear();
    wrecks_.clear();
    fires_.clear();
    vents_.clear();
    flashes_.clear();
    scorch_.clear();
    worms_.clear();
    marks_.clear();
    pending_.clear();
    for (auto& v : seenEx_) v.clear();
    for (auto& v : seenBullet_) v.clear();
    for (auto& v : units_) v.clear();
    bloomSeen_ = {};
    blooms_.clear();
    spiceTargets_.clear();
    captures_.clear();
    primed_ = false;
    trauma_ = shakeX_ = shakeY_ = 0;
}

float Effects::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ >> 8) * (1.0f / 16777216.0f);
}

// Procedural particle sprites (white, alpha-shaped; tinted by vertex colour): soft glow, smoke
// puff, spark, debris chunk, flame, ring, soft rectangle.
void Effects::buildTextures() {
    const int W = kTexCell * kTexCount, H = kTexCell;
    std::vector<std::uint32_t> px(static_cast<std::size_t>(W) * H, 0);
    for (int t = 0; t < kTexCount; ++t)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < kTexCell; ++x) {
                const float u = (float(x) + 0.5f) / kTexCell * 2 - 1, v = (float(y) + 0.5f) / kTexCell * 2 - 1;
                const float r = std::sqrt(u * u + v * v);
                float a = 0, lum = 1;
                switch (t) {
                case kGlow: a = std::exp(-r * r * 4.5f) * std::clamp((1 - r) * 4, 0.0f, 1.0f); break;
                case kSmoke: {
                    const float n = 0.55f * noise2(u * 2.5f + 7, v * 2.5f + 3, 11) + 0.3f * noise2(u * 5 + 1, v * 5 + 9, 23) +
                                    0.15f * noise2(u * 10, v * 10, 37);
                    const float edge = std::clamp(1 - (r + (n - 0.5f) * 0.5f), 0.0f, 1.0f);
                    a = std::pow(edge, 1.4f) * (0.65f + 0.5f * n);
                    lum = 0.75f + 0.25f * (1 - (v + 1) * 0.5f) + 0.15f * (n - 0.5f);  // lit from above
                    break;
                }
                case kSpark: a = std::exp(-r * r * 22) + 0.35f * std::exp(-r * r * 5); break;
                case kDebris: {
                    const float ang = std::atan2(v, u);
                    const float rr = 0.55f + 0.18f * std::sin(ang * 3 + 1.1f) + 0.1f * std::sin(ang * 5 + 0.3f);
                    a = r < rr ? 1.0f : std::clamp(1 - (r - rr) * 12, 0.0f, 1.0f);
                    lum = 0.6f + 0.4f * std::clamp(-(u + v) * 0.7f + 0.3f, 0.0f, 1.0f);
                    break;
                }
                case kFlame: {
                    const float n = noise2(u * 3 + 5, v * 3 + 2, 51);
                    a = std::clamp(1 - r * (0.9f + 0.4f * n), 0.0f, 1.0f);
                    a = a * a * (1.4f - 0.4f * a);
                    break;
                }
                case kRing: a = std::exp(-std::pow((r - 0.72f) / 0.12f, 2.0f)); break;
                case kGlowPM: a = std::exp(-r * r * 4.5f) * std::clamp((1 - r) * 4, 0.0f, 1.0f); break;
                case kWave: {  // thin bright rim, a faint veil inside
                    const float rim = r > 0.8f ? std::exp(-std::pow((r - 0.8f) / 0.06f, 2.0f))
                                               : std::exp(-std::pow((0.8f - r) / 0.14f, 2.0f));
                    a = rim + 0.07f * std::clamp((0.8f - r) * 3, 0.0f, 1.0f);
                    break;
                }
                case kGas: {  // billowing: two noise octaves warp a feathered disc
                    const float n = 0.5f * noise2(u * 2 + 3, v * 2 + 8, 71) + 0.3f * noise2(u * 4 + 9, v * 4 + 1, 73) +
                                    0.2f * noise2(u * 8 + 5, v * 8 + 6, 79);
                    const float edge = std::clamp(1 - (r + (n - 0.5f) * 0.7f), 0.0f, 1.0f);
                    a = std::pow(edge, 1.8f) * (0.55f + 0.6f * n);
                    lum = 0.82f + 0.3f * (n - 0.5f) + 0.12f * (1 - (v + 1) * 0.5f);
                    break;
                }
                case kArc: {  // crescent on +x: sharp outer rim, soft inner fall-off, tapered tips
                    const float rim = r > 0.82f ? std::exp(-std::pow((r - 0.82f) / 0.05f, 2.0f))
                                                : std::exp(-std::pow((0.82f - r) / 0.2f, 2.0f));
                    const float ca = r > 0 ? u / r : 0;  // cos of the angle from +x
                    const float span = std::clamp((ca - 0.42f) / 0.4f, 0.0f, 1.0f);
                    a = rim * span * span * (3 - 2 * span);
                    break;
                }
                case kLine: a = std::exp(-v * v * 9) * 0.85f + 0.15f * std::exp(-v * v * 2.5f); break;
                case kSoft: {
                    const float ex = std::clamp((1 - std::abs(u)) * 3, 0.0f, 1.0f), ey = std::clamp((1 - std::abs(v)) * 2.5f, 0.0f, 1.0f);
                    a = ex * ey;
                    break;
                }
                case kFire: {  // billowing cauliflower puff, brighter towards the core (the vertex colour tints it)
                    const float n = 0.45f * noise2(u * 3 + 4, v * 3 + 1, 91) + 0.33f * noise2(u * 6 + 2, v * 6 + 7, 93) +
                                    0.22f * noise2(u * 12 + 3, v * 12 + 5, 97);
                    const float edge = 1 - (r + (n - 0.5f) * 0.75f);
                    const float e = std::clamp(edge / 0.3f, 0.0f, 1.0f);
                    a = e * e * (3 - 2 * e) * std::clamp(0.62f + 0.6f * n, 0.0f, 1.0f);
                    lum = std::clamp(0.38f + 0.62f * std::pow(std::max(0.0f, 1 - r * 1.15f), 0.75f) * (0.7f + 0.55f * n), 0.0f, 1.0f);
                    break;
                }
                case kMuzzle: {  // flash star: a cone from the muzzle (u = -1) along +x, a hot core, side spikes
                    const float t = (u + 1) * 0.5f;
                    const float wd = 0.1f + 0.42f * t;
                    const float cone = std::exp(-(v * v) / (wd * wd) * 2.2f) * std::pow(std::max(0.0f, 1 - t), 1.25f) *
                                       std::clamp(t / 0.06f, 0.0f, 1.0f);
                    const float core = std::exp(-((u + 0.62f) * (u + 0.62f) * 26 + v * v * 34));
                    float spikes = 0;
                    for (float ang : {0.75f, -0.75f, 1.6f, -1.6f}) {
                        const float dx = std::cos(ang), dy = std::sin(ang), qx = u + 0.62f, qy = v;
                        const float along = qx * dx + qy * dy, perp = -qx * dy + qy * dx;
                        const float len = std::abs(ang) > 1 ? 0.32f : 0.62f;
                        if (along > 0) spikes += std::exp(-perp * perp * 520) * std::clamp(1 - along / len, 0.0f, 1.0f);
                    }
                    a = std::clamp(cone + core + 0.7f * spikes, 0.0f, 1.0f);
                    break;
                }
                case kStreak: {  // tracer: bright head at +x, a tail fading towards -x
                    const float t = (u + 1) * 0.5f;
                    a = std::exp(-v * v * 16) * std::pow(t, 2.0f) + 0.8f * std::exp(-((u - 0.82f) * (u - 0.82f) * 70 + v * v * 40));
                    a = std::clamp(a, 0.0f, 1.0f);
                    break;
                }
                case kCapsule: {  // rounded body along u, lit from the top
                    const float qx = std::max(std::abs(u) - 0.55f, 0.0f), qy = std::max(std::abs(v) - 0.12f, 0.0f);
                    const float d = std::sqrt(qx * qx + qy * qy) - 0.3f;
                    a = std::clamp(-d * 14 + 0.5f, 0.0f, 1.0f);
                    lum = std::clamp(0.72f - 0.45f * v + 0.18f * std::exp(-((v + 0.2f) * (v + 0.2f)) * 30), 0.3f, 1.0f);
                    break;
                }
                case kScorch: {  // irregular burnt blotch with a few splash rays
                    const float ang = std::atan2(v, u);
                    const float n = 0.6f * noise2(u * 3 + 11, v * 3 + 4, 101) + 0.4f * noise2(u * 7 + 2, v * 7 + 13, 103);
                    const float rays = std::pow(std::max(0.0f, std::sin(ang * 5 + 3.0f * noise2(ang * 1.5f, 0.5f, 107))), 3.0f);
                    const float rr = r / (0.58f + 0.3f * n + 0.1f * rays);
                    a = std::clamp((1 - rr) * 2.2f, 0.0f, 1.0f);
                    a = a * a * (0.7f + 0.45f * noise2(u * 14, v * 14, 109));
                    a = std::clamp(a, 0.0f, 1.0f);
                    break;
                }
                case kShock: {  // a thin sharp ring with a soft inner trail
                    a = std::exp(-std::pow((r - 0.86f) / 0.035f, 2.0f)) +
                        (r < 0.86f ? 0.3f * std::exp(-std::pow((0.86f - r) / 0.16f, 2.0f)) : 0.0f);
                    a = std::clamp(a, 0.0f, 1.0f);
                    break;
                }
                case kMound: {  // a grainy dome lit from the north-west (spice bloom mounds)
                    const float n = 0.6f * noise2(u * 3 + 17, v * 3 + 5, 131) + 0.4f * noise2(u * 6 + 3, v * 6 + 19, 137);
                    const float rr = r / (0.78f + 0.16f * n);
                    if (rr < 1) {
                        const float h = std::sqrt(1 - rr * rr);
                        const float nx = u / (0.78f + 0.16f * n), ny = v / (0.78f + 0.16f * n);
                        const float d = (-0.5f * nx - 0.55f * ny + 0.67f * h);  // light from NW and above
                        lum = std::clamp(0.45f + 0.65f * d, 0.25f, 1.0f) * (0.85f + 0.3f * noise2(u * 16, v * 16, 139));
                    }
                    a = std::clamp((1 - rr) * 6, 0.0f, 1.0f);
                    break;
                }
                default: break;
                }
                // 1px transparent border per cell so linear filtering never bleeds between cells.
                // (kLine keeps its u ends: strips join end to end; sampling stays inside the cell.)
                if (((x == 0 || x == kTexCell - 1) && t != kLine) || y == 0 || y == H - 1) a = 0;
                if (t == kGlowPM) lum = a;  // premultiplied: the Light blend ignores alpha
                const int A = std::clamp(int(a * 255 + 0.5f), 0, 255), L = std::clamp(int(lum * 255 + 0.5f), 0, 255);
                px[static_cast<std::size_t>(y) * W + t * kTexCell + x] = rgba(L, L, L, A);
            }
    atlasTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, W, H);
    if (atlasTex_) {
        SDL_UpdateTexture(atlasTex_, nullptr, px.data(), W * 4);
        SDL_SetTextureScaleMode(atlasTex_, SDL_SCALEMODE_LINEAR);
    }
    // Vignette: black, transparent in the middle, darkening smoothly towards the corners.
    constexpr int V = 256;
    std::vector<std::uint32_t> vg(V * V);
    for (int y = 0; y < V; ++y)
        for (int x = 0; x < V; ++x) {
            const float u = (x + 0.5f) / V * 2 - 1, v = (y + 0.5f) / V * 2 - 1;
            const float d = std::sqrt(u * u * 0.8f + v * v * 1.0f);
            const float a = std::clamp((d - 0.55f) / 0.9f, 0.0f, 1.0f);
            vg[static_cast<std::size_t>(y) * V + x] = rgba(0, 0, 0, int(a * a * 150));
        }
    vignette_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, V, V);
    if (vignette_) {
        SDL_UpdateTexture(vignette_, nullptr, vg.data(), V * 4);
        SDL_SetTextureBlendMode(vignette_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(vignette_, SDL_SCALEMODE_LINEAR);
    }
}

// Effect classes from the explosion / weapon names in Templates.bin (stable across the data
// versions; unknown names get no extra effect).
void Effects::classify() {
    exClass_.assign(t_.explosionNames.size(), ExClass::None);
    exScale_.assign(t_.explosionNames.size(), 1.0f);
    exReplace_.assign(t_.explosionNames.size(), 0);
    exDir_.assign(t_.explosionNames.size(), 0);
    for (std::size_t i = 0; i < t_.explosionNames.size(); ++i) {
        const std::string& n = t_.explosionNames[i];
        ExClass c = ExClass::None;
        float scale = 1;
        // Not explosions although they are explosion objects (no burst, no shake, no light):
        //   EX_REF        groupIds[80] "Refinery processing": spawned on the refinery for every spice
        //                 blob a docked harvester unloads (economy.cpp, harvester state 13);
        //   EX_HARV       groupIds[64] the harvester-on-the-pad unload art (drawn as the unit body);
        //   EX_SPICEBLOOM groupIds[63] a spice bloom growing a stage (FUN_0044ea70), not bursting;
        //   EX_CASH / EX_CRATE* crate pick-up animations. These keep their original sprites.
        // Fireball sizes (scale): what the hit looks like, by the weapons that use it
        // (Templates.bin: hitExplosion / deathExplosion).
        if (n == "EX_HARV") c = ExClass::None;
        // EX_REF: the refinery's chimneys vent (one burst per unloaded blob) and smoke while it works.
        else if (n == "EX_REF") c = ExClass::Refinery;
        // EX_SPICEBLOOM: a bloom grows a stage (the mound itself is drawn from the crate record).
        else if (n == "EX_SPICEBLOOM") c = ExClass::BloomGrow;
        else if (n == "EX_BUILDING") { c = ExClass::Huge; scale = 0.9f; }
        else if (n == "EX_DEATHHAND") { c = ExClass::Huge; scale = 2.4f; }
        else if (n == "EX_DEVDEATH") { c = ExClass::Huge; scale = 1.7f; }
        // EX_DHM = groupIds[61] "Death Hand Missile trail": one per flight tick.
        else if (n == "EX_DHM") c = ExClass::DhmTrail;
        // EX_WHITEN1 = groupIds[58]: the flash under a vehicle's death explosion (DestroyUnit) -
        // marks where a vehicle died: the burning wreck.
        else if (n == "EX_WHITEN1") c = ExClass::Whiten;
        else if (n == "EX_BANG11") { c = ExClass::Big; scale = 0.8f; }   // trike / quad death, grenadier death
        else if (n == "EX_BANG13") { c = ExClass::Big; scale = 1.0f; }   // tank deaths
        else if (n == "EX_BANG14") { c = ExClass::Big; scale = 1.15f; }  // heavy vehicles, bombs, BULLET14
        else if (startsWith(n, "EX_BANG")) c = ExClass::Big;
        else if (n == "EX_155MMGUN") { c = ExClass::Big; scale = 0.85f; }  // howitzer, grenades
        // The deviator: EX_GREENEXP is its per-tick trail (the bullet trail below draws the gas
        // instead), EX_DEVIATE the hit: a gas cloud, not a fireball.
        else if (n == "EX_GREENEXP") c = ExClass::None;
        else if (n == "EX_DEVIATE") c = ExClass::Gas;
        else if (n == "EX_DEVASTATOR") c = ExClass::Plasma;
        else if (n == "EX_NEW ROCKET") { c = ExClass::Medium; scale = 1.25f; }  // missile tank, hydra
        else if (n == "EX_TANKGUN") { c = ExClass::Medium; scale = 1.05f; }     // 80 / 110 mm shells
        else if (n == "EX_ROCKET" || n == "EX_BAZOOKA") { c = ExClass::Medium; scale = 0.9f; }
        else if (n == "EX_GRENADE") { c = ExClass::Medium; scale = 0.9f; }
        else if (n == "EX_WALL") { c = ExClass::Medium; scale = 1.1f; }
        else if (n == "EX_MG" || n == "EX_HMG") c = ExClass::Small;
        // Directional muzzle flashes (32 facing frames, blinking): Modern draws its own flash at
        // the bullet's spawn (spawnMuzzle), so these only hide the sprite.
        else if (startsWith(n, "MUZ_") || n == "EX_TRIKEM" || n == "EX_FLASH") c = ExClass::Muzzle;
        // EX_SMOKE3 = groupIds[59]: the original's damage smoke - a damaged vehicle every ~4 ticks
        // while its smoke timer runs (UpdateUnit), a damaged building at its BUILEXP.BIN points
        // (UpdateBuilding, with EX_FLAME / EX_FLAME2). Modern smoke follows exactly these events.
        else if (n == "EX_SMOKE3") c = ExClass::Smoke;
        else if (startsWith(n, "EX_TRAIL") || startsWith(n, "EX_SMOKE") || n == "EX_PUFF") c = ExClass::Trail;
        else if (n == "EX_THPUFF") c = ExClass::Dust;
        else if (n == "EX_WORM") c = ExClass::WormMaw;
        else if (n == "EX_WORMDUST") c = ExClass::WormDust;
        else if (startsWith(n, "EX_WORMSN")) c = ExClass::WormSign;
        else if (startsWith(n, "EX_HARVDUST")) {
            // One per harvest load step (economy.cpp), its frames drawn for the harvester's facing / 4.
            c = ExClass::HarvDust;
            exDir_[i] = std::int8_t(n.size() > 11 && n[11] >= '0' && n[11] <= '7' ? n[11] - '0' : 0);
        }
        else if (startsWith(n, "EX_FLAME")) c = ExClass::Flame;
        else if (startsWith(n, "EX_DEVSPARK")) c = ExClass::Spark;
        exClass_[i] = c;
        exScale_[i] = scale;
        // Replaced (the original sprite is not drawn): every weapon effect we draw ourselves.
        // Kept: non-weapon animations and the worm's maw (the worm itself; extra sand around it). Harvest
        // dust, damage smoke, the bloom growth, wormsign and the worm's dust are replaced by modern versions.
        const bool keep = c == ExClass::None || c == ExClass::WormMaw;
        exReplace_[i] = (!keep || n == "EX_GREENEXP") ? 1 : 0;
    }
    weaponClass_.assign(formats::kMaxWeapons, WeaponClass::None);
    for (int i = 0; i < formats::kMaxWeapons; ++i) {
        const std::string n(t_.bin.weaponNames[i], strnlen(t_.bin.weaponNames[i], sizeof t_.bin.weaponNames[i]));
        WeaponClass c = WeaponClass::None;
        if (n.empty()) c = WeaponClass::None;
        else if (n == "SOUND") c = WeaponClass::Sonic;
        else if (n == "DEVASTATOR" || n == "DEVBULLET") c = WeaponClass::Plasma;
        else if (n == "DEVIATOR") c = WeaponClass::Gas;
        else if (n.find("ROCKET") != std::string::npos || n == "BAZOOKA" || n == "NON HOMING" || startsWith(n, "MTANK") ||
                 n == "HYDRA")
            c = WeaponClass::Rocket;
        else if (startsWith(n, "155MM") || n == "GRENADE" || n == "SPICEEXPL") c = WeaponClass::Artillery;
        else if (n.find("MM GUN") != std::string::npos || n == "BULLET14") c = WeaponClass::Cannon;
        else if (startsWith(n, "DEBRIS")) c = WeaponClass::Debris;
        else if (n == "ORNIBOMB" || n == "DHBOMB") c = WeaponClass::Bomb;
        else if (n == "LMG" || n == "HMG" || n == "M_LMG" || n == "M_HMG" || startsWith(n, "FREMEN")) c = WeaponClass::Gun;
        weaponClass_[i] = c;
    }
    // The spice bloom's shells (SpiceMound: groupIds[50], SPICEEXPL): globs of spice, not artillery.
    spiceWeapon_ = t_.bin.groupIds[50];
    if (spiceWeapon_ >= 0 && spiceWeapon_ < formats::kMaxWeapons) weaponClass_[std::size_t(spiceWeapon_)] = WeaponClass::Spice;
    wheeled_.assign(formats::kMaxUnitTypes, 0);
    for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
        const std::string n(t_.bin.unitNames[i], strnlen(t_.bin.unitNames[i], sizeof t_.bin.unitNames[i]));
        wheeled_[i] = n.find("TRIKE") != std::string::npos || n.find("RAIDER") != std::string::npos ||
                      n.find("QUAD") != std::string::npos;
    }
}

// Gas colours from the house ramp (COLOURS.BIN, dark -> bright): a mid shade for the body and
// a bright one for the glow, pushed away from grey so the thin gas still reads as the house.
void Effects::setHouseColours(const formats::HouseColours& c) {
    auto vivid = [](std::uint32_t col, float k, float lift) {
        const float r = float(col & 0xFF), g = float((col >> 8) & 0xFF), b = float((col >> 16) & 0xFF);
        const float grey = (r + g + b) / 3;
        auto ch = [&](float v) { return std::clamp(int((grey + (v - grey) * k) * lift + 0.5f), 0, 255); };
        return rgba(ch(r), ch(g), ch(b), 255);
    };
    for (int h = 0; h < formats::kHouseCount; ++h) {
        gasBody_[h] = vivid(c[h][9], 1.5f, 1.05f);
        gasGlow_[h] = vivid(c[h][13], 1.4f, 1.08f);
    }
}

std::uint32_t Effects::gasBody(int house) const {
    if (house >= 0 && house < formats::kHouseCount && gasBody_[house]) return gasBody_[house];
    return rgba(90, 175, 120, 255);  // Ordos green when unknown
}

std::uint32_t Effects::gasGlow(int house) const {
    if (house >= 0 && house < formats::kHouseCount && gasGlow_[house]) return gasGlow_[house];
    return rgba(185, 245, 205, 255);
}

const Effects::UnitVisual* Effects::unitVisual(int side, int index) const {
    if (side < 0 || side >= game::kMaxSides || index < 0 || index >= int(units_[side].size())) return nullptr;
    const UnitVisual& v = units_[side][index];
    return v.key >= 0 ? &v : nullptr;
}

void Effects::add(const Particle& p) {
    if (particles_.size() >= kMaxParticles) {
        // Full: replace a random old one rather than growing (keeps the cost bounded).
        particles_[std::size_t(rnd() * float(particles_.size() - 1))] = p;
        return;
    }
    particles_.push_back(p);
}

std::uint32_t Effects::terrainColour(const game::World& w, const TileAtlas& atlas, float x, float y) const {
    const int tx = int(x) >> 5, ty = int(y) >> 5;
    if (!w.inBounds(tx, ty)) return rgba(200, 160, 110, 255);
    // Average a few texels of the tile for a stable sand colour.
    std::uint32_t r = 0, g = 0, b = 0;
    const int tile = w.cell(tx, ty).tile;
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t c = atlas.pixel(tile, 6 + (i & 1) * 18, 6 + (i >> 1) * 18);
        r += c & 0xFF;
        g += (c >> 8) & 0xFF;
        b += (c >> 16) & 0xFF;
    }
    return rgba(int(r / 4), int(g / 4), int(b / 4), 255);
}

void Effects::addTrauma(float x, float y, float amount, const Camera& cam, int viewW, int viewH) {
    const float cx = cam.x + viewW / (2 * cam.zoom), cy = cam.y + viewH / (2 * cam.zoom);
    const float d = std::hypot(x - cx, y - cy) / (std::max(viewW, viewH) / cam.zoom);
    trauma_ = std::max(trauma_, std::min(1.0f, trauma_ + amount * std::clamp(1.2f - d, 0.0f, 1.0f)));
}

// Advances the particles spawned from index `first` on by `age` seconds (an effect that started
// before this frame: the first frame of a view, or an event inside the last frame's interval).
void Effects::preAge(std::size_t first, float age) {
    if (age <= 0.002f) return;
    for (std::size_t i = first; i < particles_.size(); ++i) {
        Particle& p = particles_[i];
        for (float t = 0; t < age; t += 0.04f) {
            const float h = std::min(0.04f, age - t);
            p.age += h;
            if (p.age < 0) continue;
            const float k = std::exp(-p.drag * h);
            p.vx *= k;
            p.vy *= k;
            p.vz += p.gravity * h;
            p.x += p.vx * h;
            p.y += p.vy * h;
            p.z += p.vz * h;
            if (p.z < 0) {
                p.z = 0;
                if (p.gravity < 0) {
                    p.vz = -p.vz * 0.3f;
                    p.vx *= 0.5f;
                    p.vy *= 0.5f;
                    p.vrot *= 0.5f;
                }
            }
            p.rot += p.vrot * h;
        }
    }
}

void Effects::emitContinuous(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                             int viewH, bool showShroud, bool /*prewarm*/) {
    const float vx0 = cam.x - 64, vy0 = cam.y - 64;
    const float vx1 = cam.x + viewW / cam.zoom + 64, vy1 = cam.y + viewH / cam.zoom + 96;
    auto inView = [&](float x, float y) { return x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1; };
    auto shrouded = [&](float x, float y) {
        const int tx = int(x) >> 5, ty = int(y) >> 5;
        return showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
    };
    const float ease = 1 - std::exp(-dt * 20);

    // --- Ground vehicles: smooth turning, treads, dust, damage smoke.
    for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
        const auto& side = w.sides()[s];
        auto& vis = units_[s];
        if (vis.size() != side.units.size()) vis.assign(side.units.size(), UnitVisual{});
        for (std::size_t i = 0; i < side.units.size(); ++i)
            if (!side.units[i].alive) vis[i].key = -1;
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            const auto& tp = t_.bin.units[u.type];
            UnitVisual& v = vis[i];
            const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
            const bool ground = !tp.isInfantry && !(u.flags & 0x200140) && tp.specialBehavior != game::behaviour::kSandworm;
            const std::int32_t key = u.type + 1;
            if (v.key != key || !ground) {
                v = UnitVisual{};
                v.key = ground ? key : -1;
                v.facing = float(u.facing & 31);
                v.turret = float(u.facingTurret & 31);
                v.x = x;
                v.y = y;
                continue;
            }
            const float dx = x - v.x, dy = y - v.y, dist = std::hypot(dx, dy);
            v.x = x;
            v.y = y;
            if (dt <= 0) continue;
            if (dist > 48) continue;  // teleport / reused slot
            auto approach = [&](float& f, int target) {
                const float d = wrap32(float(target & 31) - f);
                f = std::abs(d) > 6 ? float(target & 31) : f + d * ease;
                if (f < 0) f += 32;
                if (f >= 32) f -= 32;
            };
            approach(v.facing, u.facing);
            approach(v.turret, u.facingTurret);
            v.moving = dist > 0.02f;
            const bool wheels = wheeled_[u.type] != 0;
            if (!inView(x, y) || shrouded(x, y)) continue;
            const int tx = int(x) >> 5, ty = int(y) >> 5;
            const std::uint32_t cf = w.inBounds(tx, ty) ? w.cell(tx, ty).flags : 0;
            const bool concrete = (cf & game::cell_flags::kConcrete) != 0;
            const bool sand = (cf & game::cell_flags::kSand) && !concrete;
            if (v.moving && !concrete) {
                const float ux = dx / dist, uy = dy / dist;
                if (!wheels && sand) {
                    v.treadDist += dist;
                    while (v.treadDist >= 4) {
                        v.treadDist -= 4;
                        const Tread t{x - ux * 6, y - uy * 6, std::atan2(uy, ux), 0};
                        if (treads_.size() < kMaxTreads) treads_.push_back(t);
                        else treads_[treadHead_++ % kMaxTreads] = t;
                    }
                }
                v.dustDist += dist;
                // Sand throws up more dust than rock.
                const float every = (wheels ? 5.0f : 7.0f) * (sand ? 1.0f : 2.2f);
                while (v.dustDist >= every) {
                    v.dustDist -= every;
                    Particle p{};
                    p.x = x - ux * 10 + rnd(-3, 3);
                    p.y = y - uy * 10 + rnd(-3, 3);
                    p.z = 1;
                    p.vx = -ux * 8 + rnd(-5, 5) + kWindX * 0.5f;
                    p.vy = -uy * 8 + rnd(-4, 4);
                    p.vz = rnd(3, 9);
                    p.drag = 1.2f;
                    p.life = rnd(0.8f, 1.4f);
                    p.size0 = rnd(5, 7);
                    p.size1 = rnd(16, 22);
                    const std::uint32_t sandC = scaleRgb(terrainColour(w, atlas, x, y), 1.08f);
                    p.c0 = withAlpha(sandC, wheels ? 110 : 90);
                    p.c1 = withAlpha(sandC, 0);
                    p.tex = kSmoke;
                    p.blend = Blend::Alpha;
                    p.rot = rnd(0, 6.28f);
                    p.vrot = rnd(-0.8f, 0.8f);
                    add(p);
                }
            }
            // Damage smoke: not from health thresholds - it follows the original's own smoke
            // events (EX_SMOKE3 every ~4 ticks while the unit's smoke timer runs, spawnDamageSmoke).
        }
    }

    // Buildings: damage smoke and fire come from the original's BUILEXP.BIN events (EX_SMOKE3 /
    // EX_FLAME while the smoke timer runs and the point's bit is set, buildings.cpp), never from
    // health thresholds - so walls and other non-repairable structures (no kSelectable flag)
    // never burn, and the fire dies out when the original's does.

    // (Spice sparkle removed at the owner's request.)
}

// Sonic impact: expanding light-blue rings, a burst of glowing motes and a brief ground ripple.
void Effects::spawnSonicRipple(float x, float y) {
    Particle b{};
    b.x = x;
    b.y = y;
    for (int k = 0; k < 3; ++k) {
        Particle r = b;
        r.tex = kWave;
        r.age = -0.09f * k;
        r.life = 0.6f;
        r.size0 = 8;
        r.size1 = 64 - 12 * k;
        r.blend = Blend::Add;
        r.c0 = rgba(185, 235, 255, 190 - 40 * k);
        r.c1 = rgba(80, 150, 255, 0);
        add(r);
        r.blend = Blend::Alpha;  // the colour on bright sand
        r.c0 = rgba(70, 150, 255, 120 - 25 * k);
        r.c1 = rgba(110, 190, 255, 0);
        add(r);
    }
    Particle gr = b;  // ground ripple: a wide faint veil
    gr.tex = kWave;
    gr.blend = Blend::Alpha;
    gr.life = 0.9f;
    gr.size0 = 20;
    gr.size1 = 96;
    gr.c0 = rgba(90, 170, 255, 60);
    gr.c1 = rgba(120, 200, 255, 0);
    add(gr);
    addFlash(x, y, 0, 80, rgba(130, 200, 255, 170), 0.45f);
    for (int i = 0; i < 16; ++i) {
        Particle m = b;
        m.tex = kSpark;
        m.blend = Blend::Add;
        const float a = rnd(0, 2 * kPi), sp = rnd(30, 110);
        m.vx = std::cos(a) * sp;
        m.vy = std::sin(a) * sp * 0.7f;
        m.vz = rnd(5, 40);
        m.drag = 2.2f;
        m.life = rnd(0.4f, 0.8f);
        m.size0 = rnd(2.6f, 3.6f);
        m.size1 = 1.2f;
        m.c0 = i % 3 ? rgba(170, 230, 255, 240) : rgba(240, 250, 255, 255);
        m.c1 = rgba(80, 150, 255, 0);
        add(m);
    }
}

// The sonic ribbon from the emitter to the head: a bright core filament with a layered glow,
// 10 thin interweaving sine strands (cyan / sky / white / pale violet, varied width and
// brightness, phases flowing forward), a translucent rippling band on the ground and a pulse
// running along the beam. Strips only: the 3D bridge draws them like particles.
void Effects::buildBeams() {
    static constexpr std::uint32_t kTint[4] = {rgba(120, 230, 255, 255), rgba(140, 195, 255, 255),
                                               rgba(225, 245, 255, 255), rgba(185, 175, 255, 255)};
    for (const SonicBeam& bm : sonic_) {
        const float dx = bm.hx - bm.ox, dy = bm.hy - bm.oy, L = std::hypot(dx, dy);
        if (L < 3) continue;
        const float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
        const float life = bm.dying < 0 ? std::min(1.0f, bm.age / 0.08f) : std::max(0.0f, 1 - bm.dying / 0.45f);
        if (life <= 0) continue;
        const float t = time_ + bm.seed;
        const float pulse = std::fmod(bm.age * 300, L + 60) - 30;  // travels emitter -> head
        const float step = std::clamp(L / 60, 3.0f, 4.5f);
        const int segs = std::max(2, int(L / step));
        auto strip = [&](float ax, float ay, float bx, float by, float width, std::uint32_t c, Blend bl, float z) {
            Particle p{};
            p.x = (ax + bx) * 0.5f;
            p.y = (ay + by) * 0.5f + z;  // drawn y = y - z: keep the drawn point, lift in 3D
            p.z = z;
            p.life = 1;
            p.size0 = p.size1 = width;
            p.c0 = p.c1 = c;
            p.tex = kLine;
            p.blend = bl;
            p.aligned = true;
            p.rot = std::atan2(by - ay, bx - ax);
            p.len = std::hypot(bx - ax, by - ay) * 1.08f;
            beams_.push_back(p);
        };
        // Envelope along the beam: soft at the emitter, tapering into the head.
        auto env = [&](float s) {
            return std::clamp(s / 16, 0.0f, 1.0f) * std::clamp((L - s) / 10 + 0.25f, 0.0f, 1.0f);
        };
        auto bright = [&](float s) { return 1 + 1.1f * std::exp(-std::pow((s - pulse) / 22, 2.0f)); };
        // Ground band (alpha): the blue tint and a rippling shimmer under the beam.
        for (int i = 0; i < segs; ++i) {
            const float s0 = L * i / segs, s1 = L * (i + 1) / segs, sm = (s0 + s1) * 0.5f;
            const float rip = 0.6f + 0.4f * std::sin(sm * 0.35f - t * 14);
            const float a = 46 * env(sm) * rip * life;
            strip(bm.ox + ux * s0, bm.oy + uy * s0, bm.ox + ux * s1, bm.oy + uy * s1, 18 + 4 * rip,
                  rgba(80, 160, 255, int(a)), Blend::Alpha, 0);
        }
        // Strands.
        for (int k = 0; k < 10; ++k) {
            const float h = float(hash32(std::uint32_t(k) * 7919u + 13u) & 0xFFFF) / 65535.0f;
            const float amp = 3.5f + 5.5f * h, lam = 30 + 34 * float((k * 37) % 10) / 10, w = 1.3f + 1.3f * float((k * 53) % 7) / 6;
            const float ph = float(k) * 1.7f, sp = 9 + 5 * h;
            const float br = 0.55f + 0.45f * float((k * 29) % 5) / 4;
            const std::uint32_t tint = kTint[k % 4];
            auto off = [&](float s) {
                const float grow = 0.35f + 0.65f * std::min(1.0f, s / 70);
                return amp * grow * env(s) * std::sin(s * 2 * kPi / lam - t * sp + ph) *
                       (0.75f + 0.25f * std::sin(s * 2 * kPi / (lam * 2.3f) + t * sp * 0.4f + ph * 2.3f));
            };
            float px = bm.ox, py = bm.oy;
            for (int i = 1; i <= segs; ++i) {
                const float s = L * i / segs, o = off(s);
                const float qx = bm.ox + ux * s + nx * o, qy = bm.oy + uy * s + ny * o;
                const float a = br * env(s) * bright(s) * life;
                // Alpha carries the sky blue on bright sand, a faint additive pass the glow.
                strip(px, py, qx, qy, w, withAlpha(scaleRgb(tint, 0.78f), int(std::min(255.0f, 80 * a))), Blend::Alpha, 2);
                strip(px, py, qx, qy, w * 0.7f, withAlpha(tint, int(std::min(255.0f, 34 * a))), Blend::Add, 2);
                px = qx;
                py = qy;
            }
        }
        // Core filament with a layered glow halo.
        for (int i = 0; i < segs; ++i) {
            const float s0 = L * i / segs, s1 = L * (i + 1) / segs, sm = (s0 + s1) * 0.5f;
            const float o0 = 1.2f * std::sin(s0 * 0.25f - t * 18), o1 = 1.2f * std::sin(s1 * 0.25f - t * 18);
            const float e = env(sm) * bright(sm) * life;
            const float ax = bm.ox + ux * s0 + nx * o0, ay = bm.oy + uy * s0 + ny * o0;
            const float bx = bm.ox + ux * s1 + nx * o1, by = bm.oy + uy * s1 + ny * o1;
            strip(ax, ay, bx, by, 12, rgba(70, 150, 255, int(std::min(255.0f, 50 * e))), Blend::Alpha, 2);
            strip(ax, ay, bx, by, 6, rgba(150, 215, 255, int(std::min(255.0f, 34 * e))), Blend::Add, 2);
            strip(ax, ay, bx, by, 2.4f, rgba(230, 248, 255, int(std::min(255.0f, 105 * e))), Blend::Add, 2);
        }
    }
}

// ---- Destroyed buildings ----------------------------------------------------------------------
// DestroyBuilding 0x49ca10 puts a building into state 17 for 30 ticks (re/notes/combat.md §6): it
// stays drawn under its 2 x solidTiles death explosions and is freed on tick 31. The Modern look
// adds, all keyed to the time since the death (effect seconds = ticks / 20, so the 30 ticks are
// 1.5 s at any game speed): a blast flash, a shockwave ring and screen shake (by size), a chain of
// varied fireballs, debris arcing out (bouncing, dust where it lands, some burning and leaving
// smoke, some starting small fires), the art collapsing (collapseLook), a low dust cloud rolling
// out and dust along the base line, a tall drifting smoke column that lingers for seconds, and
// afterwards a scorched ruin (scorch, rubble, glowing embers, small fires, rising sparks).
namespace {
constexpr float kWreckLife = 30.0f;
}  // namespace

void Effects::updateWrecks(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                           int viewH, bool showShroud) {
    const float vx0 = cam.x - 160, vy0 = cam.y - 160;
    const float vx1 = cam.x + viewW / cam.zoom + 160, vy1 = cam.y + viewH / cam.zoom + 220;
    auto inView = [&](float x, float y) { return x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1; };
    auto shrouded = [&](float x, float y) {
        const int tx = int(x) >> 5, ty = int(y) >> 5;
        return showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
    };
    for (auto& k : wrecks_) k.seen = false;
    for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
        const auto& side = w.sides()[s];
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.buildings[i];
            if (!b.alive || b.state != 17 || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
            if (b.flags & game::building_flags::kDontRemoveTiles) {
                // Captured (CaptureBuilding 0x46d6d0): the old record dies in state 17 with
                // DeadTimer 1 and kDontRemoveTiles while a copy lives on for the new owner. Not a
                // destruction: no wreck, no blast, no shake - only a brief house-colour flash.
                bool known = false;
                for (const auto& c : captures_)
                    if (c.side == s && c.index == i && c.type == b.type && c.tx == b.tileX && c.ty == b.tileY) known = true;
                if (!known) {
                    captures_.push_back({s, i, b.type, b.tileX, b.tileY, time_});
                    const float cx = float(b.tileX * 32 + 32), cy = float(b.tileY * 32 + 32);
                    if (inView(cx, cy) && !shrouded(cx, cy)) spawnCaptureFlash(w, b.tileX, b.tileY, b.type, s);
                }
                continue;
            }
            Wreck* k = nullptr;
            for (auto& o : wrecks_)
                if (o.present && o.side == s && o.index == i && o.type == b.type && o.tileX == b.tileX && o.tileY == b.tileY)
                    k = &o;
            if (!k) {
                // Sold (Building_MarkSold 0x44b1d0: state 17 with DeadTimer 0) or its very last tick.
                if (b.deadTimer == 0) continue;
                const auto& tp = t_.bin.buildings[b.type];
                Wreck n;
                n.side = s;
                n.index = i;
                n.type = b.type;
                n.tileX = b.tileX;
                n.tileY = b.tileY;
                n.mask = tp.tilesOccupiedSolid ? tp.tilesOccupiedSolid : tp.tilesOccupiedAll;
                if (!n.mask) n.mask = 1;
                int cells = 0, minX = 3, minY = 3, maxX = 0, maxY = 0;
                for (int c = 0; c < 16; ++c)
                    if ((n.mask >> c) & 1) {
                        ++cells;
                        minX = std::min(minX, c % 4);
                        maxX = std::max(maxX, c % 4);
                        minY = std::min(minY, c / 4);
                        maxY = std::max(maxY, c / 4);
                    }
                n.cells = cells;
                n.x0 = float((b.tileX + minX) * 32);
                n.y0 = float((b.tileY + minY) * 32);
                n.x1 = float((b.tileX + maxX + 1) * 32);
                n.y1 = float((b.tileY + maxY + 1) * 32);
                n.fw = std::max(1, int(tp.artWidth) / 32);
                n.fh = std::max(1, int(tp.artHeight) / 32);
                n.artH = std::max(24.0f, float(tp.artHeight));
                n.artX0 = float(b.tileX * 32);
                n.artW = std::max(32.0f, float(tp.artWidth));
                n.artY = float(b.tileY * 32 + tp.artHeight);
                n.big = std::clamp(float(cells - 1) / 8.0f, 0.0f, 1.0f);
                // Walls and other structures the original never repairs or smokes (no kSelectable):
                // they break apart - dust and chunks - but do not burn.
                n.lite = !(tp.flags & game::building_flags::kSelectable) || tp.specialBehavior == game::building_behaviour::kWall;
                // DeadTimer: 30 on the death tick, then one less per tick.
                n.start = time_ - float(30 - std::min<int>(30, b.deadTimer)) / 20.0f;
                n.last = -0.001f;
                n.seed = hash32(std::uint32_t(i) * 2654435761u ^ std::uint32_t(b.type) * 40503u ^ std::uint32_t(s) * 7919u ^
                                std::uint32_t(b.tileX * 131 + b.tileY) ^ std::uint32_t(w.gameTicks()) * 977u);
                n.sand = terrainColour(w, atlas, (n.x0 + n.x1) * 0.5f, n.y1 + 8);
                wrecks_.push_back(n);
                k = &wrecks_.back();
            }
            k->seen = true;
        }
    }
    for (std::size_t i = 0; i < captures_.size();) {
        if (time_ - captures_[i].t > 3) {
            captures_[i] = captures_.back();
            captures_.pop_back();
        } else {
            ++i;
        }
    }
    for (std::size_t i = 0; i < wrecks_.size();) {
        Wreck& k = wrecks_[i];
        if (!k.seen) k.present = false;
        const float e = time_ - k.start;
        if (e > kWreckLife) {
            wrecks_[i] = wrecks_.back();
            wrecks_.pop_back();
            continue;
        }
        const float cx = (k.x0 + k.x1) * 0.5f, cy = (k.y0 + k.y1) * 0.5f;
        if (k.present && !k.lite && e < 1.8f && !shrouded(cx, cy)) {  // the fire inside lights the surroundings
            const float R = std::max(k.x1 - k.x0, k.y1 - k.y0) * 0.5f;
            const float fl = 0.8f + 0.2f * std::sin(time_ * 27 + float(k.seed & 31));
            lights_.push_back({cx, cy - k.artH * 0.2f, R * 3 + 50, rgba(255, 130, 50, int(75 * fl * (1 - e / 1.8f)))});
        }
        if (e > k.last) runWreck(k, k.last, e, w, atlas, cam, viewW, viewH, inView(cx, cy) && !shrouded(cx, cy));
        k.last = std::max(k.last, e);
        ++i;
    }
    // Small fires (landed burning debris, the ruins).
    for (std::size_t i = 0; i < fires_.size();) {
        Fire& f = fires_[i];
        f.age += dt;
        if (f.age >= f.life) {
            fires_[i] = fires_.back();
            fires_.pop_back();
            continue;
        }
        ++i;
        if (!inView(f.x, f.y) || shrouded(f.x, f.y)) continue;
        const float u = f.age / f.life, k = 1 - u * u;
        const float sz = f.size * (0.45f + 0.55f * k);
        f.acc += dt * 14 * k;
        while (f.acc >= 1) {
            f.acc -= 1;
            Particle p{};
            p.x = f.x + rnd(-5, 5) * sz;
            p.y = f.y + rnd(-3, 3) * sz;
            p.z = 1;
            p.vx = rnd(-4, 4);
            p.vy = rnd(-2, 2);
            p.vz = rnd(16, 32) * (0.6f + 0.4f * sz);
            p.life = rnd(0.28f, 0.5f);
            p.size0 = rnd(8, 12) * sz;
            p.size1 = rnd(3, 5) * sz;
            const bool hot = rnd() < 0.35f;
            p.tex = kFlame;
            p.blend = hot ? Blend::Add : Blend::Alpha;
            p.c0 = hot ? rgba(255, 210, 120, 220) : rgba(255, 150, 60, 190);
            p.c1 = hot ? rgba(220, 60, 10, 0) : rgba(140, 40, 10, 0);
            p.rot = rnd(0, 6.28f);
            p.vrot = rnd(-2, 2);
            add(p);
            if (rnd() < f.smoke) {  // a little smoke (burning wrecks: a column)
                Particle q = p;
                q.tex = kSmoke;
                q.blend = Blend::Alpha;
                q.z = 6 * sz;
                q.vx = kWindX + rnd(-2, 2);
                q.vy = kWindY;
                q.vz = rnd(12, 22);
                q.drag = 0.4f;
                q.life = rnd(1.4f, 2.4f);
                q.size0 = 6 * sz;
                q.size1 = rnd(22, 32) * sz;
                q.c0 = rgba(44, 40, 36, 140);
                q.c1 = rgba(105, 100, 95, 0);
                if (f.smoke > 0.3f) {  // a burning wreck: thicker, darker, taller
                    q.life *= 1.5f;
                    q.size1 *= 1.3f;
                    q.vz *= 1.2f;
                    q.c0 = rgba(30, 28, 26, 165);
                }
                q.vrot = rnd(-0.5f, 0.5f);
                add(q);
            }
        }
        lights_.push_back({f.x, f.y - 4, 50 * sz + 14, rgba(255, 120, 40, int(66 * k * (0.8f + 0.2f * std::sin(time_ * 19 + f.x)))), 4});
    }
}

// Spawns what a wreck does between elapsed times `from` and `to` (s since the death); particles
// of an event that happened before `to` are aged by the difference.
void Effects::runWreck(Wreck& k, float from, float to, const game::World& w, const TileAtlas& atlas,
                       const Camera& cam, int viewW, int viewH, bool visible) {
    if (!visible) return;
    const float cx = (k.x0 + k.x1) * 0.5f, cy = (k.y0 + k.y1) * 0.5f;
    const float R = std::max(k.x1 - k.x0, k.y1 - k.y0) * 0.5f;
    const float sc = 0.65f + 0.6f * k.big;  // size scale
    auto fired = [&](float t) { return t > from && t <= to; };
    // A random point on a solid cell (rnd), or a hashed one (n >= 0: stable per wreck).
    auto cellPoint = [&](float& px, float& py, int n) {
        const float a = n < 0 ? rnd() : hashf(k.seed, std::uint32_t(n) * 3 + 1);
        int want = std::min(k.cells - 1, int(a * float(k.cells)));
        for (int c = 0; c < 16; ++c)
            if ((k.mask >> c) & 1) {
                if (want-- == 0) {
                    const float fx = n < 0 ? rnd() : hashf(k.seed, std::uint32_t(n) * 3 + 2);
                    const float fy = n < 0 ? rnd() : hashf(k.seed, std::uint32_t(n) * 3 + 3);
                    px = float((k.tileX + c % 4) * 32) + 4 + fx * 24;
                    py = float((k.tileY + c / 4) * 32) + 4 + fy * 24;
                    return;
                }
            }
        px = cx;
        py = cy;
    };
    auto debris = [&](int count, float speedLo, float speedHi, float vzLo, float vzHi, float zHi, float age) {
        // Concrete / metal / sand-stone shades (the texture's shading multiplies them by 0.6..1).
        static constexpr std::uint32_t kStone[5] = {rgba(190, 176, 158, 255), rgba(156, 144, 128, 255), rgba(124, 114, 102, 255),
                                                    rgba(210, 192, 162, 255), rgba(98, 90, 82, 255)};
        const std::size_t first = particles_.size();
        for (int i = 0; i < count; ++i) {
            Particle p{};
            cellPoint(p.x, p.y, -1);
            p.z = rnd(4, zHi);
            float a = std::atan2(p.y - cy, p.x - cx) + rnd(-0.8f, 0.8f);
            if (std::hypot(p.y - cy, p.x - cx) < 4) a = rnd(0, 2 * kPi);
            const float sp = rnd(speedLo, speedHi);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.75f;
            p.vz = rnd(vzLo, vzHi);
            p.gravity = -420;
            p.drag = 0.35f;
            p.life = rnd(2.4f, 4.2f);
            p.size0 = p.size1 = rnd(2.4f, 6.0f) * (rnd() < 0.15f ? 1.7f : 1.0f) * (0.85f + 0.3f * k.big);
            p.c0 = p.c1 = kStone[int(rnd() * 4.99f)];
            p.tex = kDebris;
            p.blend = Blend::Alpha;
            p.rot = rnd(0, 2 * kPi);
            p.vrot = rnd(-14, 14);
            p.fx = kFxLandDust | kFxLateFade;
            if (!k.lite && rnd() < 0.3f) p.fx |= kFxTrail;
            if (!k.lite && rnd() < 0.1f) p.fx |= kFxLandFire;
            add(p);
        }
        preAge(first, age);
    };
    // Continuous emitters, integrated over [from, to] in steps of <= 0.05 s.
    auto emit = [&](float& acc, float t0, float t1, auto rate, auto spawn) {
        for (float a = std::max(from, t0); a < std::min(to, t1);) {
            const float b = std::min({a + 0.05f, to, t1});
            acc += rate(a) * (b - a);
            while (acc >= 1) {
                acc -= 1;
                const float ts = rnd(a, b);
                const std::size_t first = particles_.size();
                spawn(ts);
                preAge(first, to - ts);
            }
            a = b;
        }
    };

    // The blast: flash on the surroundings, a white-hot core, the shake, debris flying out.
    if (k.lite) {
        // A wall section breaking: a short flash, a small kick, chunks and dust - no fire.
        if (fired(0)) {
            addFlash(cx, cy - 8, 8, R * 3 + 40, rgba(255, 190, 120, 150), 0.25f);
            addTrauma(cx, cy, 0.08f, cam, viewW, viewH);
            debris(4 + k.cells * 2, 25, 70, 50, 120, k.artH * 0.4f, to);
        }
    } else if (fired(0)) {
        const std::size_t first = particles_.size();
        addFlash(cx, cy - k.artH * 0.25f, k.artH * 0.25f, R * 6 + 80, rgba(255, 175, 95, 245), std::max(0.05f, 0.8f - to));
        Particle g{};
        g.x = cx;
        g.y = cy - k.artH * 0.25f;
        g.tex = kGlow;
        g.blend = Blend::Add;
        g.life = 0.2f;
        g.size0 = R * 1.6f + 20;
        g.size1 = R * 2.8f + 30;
        g.c0 = rgba(255, 246, 220, 230);
        g.c1 = rgba(255, 190, 110, 0);
        add(g);
        preAge(first, to);
        addTrauma(cx, cy, 0.45f + 0.55f * k.big, cam, viewW, viewH);
        if (to < 0.3f) addScorch(cx, cy, R * 7 + 120, 0.8f, 2, 0.6f + 0.4f * k.big);
        if (k.big > 0.5f) trauma_ = std::min(1.25f, trauma_ + 0.25f * k.big);  // big buildings shake harder
        debris(int(8 + float(k.cells) * 3), 35 * sc, 115 * sc, 60, 170, k.artH * 0.5f, to);
    }
    // A chain of fireballs of varied size over the death (the last ones bigger).
    const int N = k.lite ? 0 : 3 + int(float(k.cells) * 0.6f);
    for (int i = 0; i < N; ++i) {
        const float t = 0.04f + 1.3f * std::pow((float(i) + 0.6f * hashf(k.seed, 40 + i)) / float(N), 0.85f);
        if (!fired(t)) continue;
        float px = 0, py = 0;
        cellPoint(px, py, -1);
        const bool last = i >= N - 2;
        const float size = (last ? rnd(0.6f, 0.8f) : rnd(0.3f, 0.6f)) * sc;
        spawnExplosion(i % 3 == 2 ? ExClass::Medium : ExClass::Big, px, py, rnd(0, k.artH * 0.35f), to - t, cam, viewW,
                       viewH, atlas, w, i % 3 != 0, -1, size, 0.2f);
    }
    // The collapse: a second rumble and a slower wave of chunks crumbling off.
    if (fired(0.75f) && !k.lite) {
        addTrauma(cx, cy, 0.2f + 0.4f * k.big, cam, viewW, viewH);
        debris(int(4 + float(k.cells) * 2), 12, 55 * sc, 30, 110, k.artH * 0.5f, to - 0.75f);
    }
    const std::uint32_t dust = lerpColour(k.sand, rgba(170, 150, 128, 255), 0.35f);
    // A low dust cloud rolling out from the footprint.
    emit(k.dustAcc, 0.1f, 1.7f, [&](float) { return 26 + 44 * k.big; }, [&](float) {
        Particle p{};
        const float perim = rnd(0, 4);
        const float fx = perim < 2 ? rnd() : float(int(perim) & 1), fy = perim < 2 ? float(int(perim) & 1) : rnd();
        p.x = k.x0 - 4 + fx * (k.x1 - k.x0 + 8);
        p.y = k.y0 - 4 + fy * (k.y1 - k.y0 + 8);
        const float a = std::atan2(p.y - cy, p.x - cx) + rnd(-0.4f, 0.4f), sp = rnd(25, 75) * sc;
        p.vx = std::cos(a) * sp + kWindX * 0.5f;
        p.vy = std::sin(a) * sp * 0.75f;
        p.z = rnd(0, 4);
        p.vz = rnd(2, 8);
        p.drag = 1.3f;
        p.life = rnd(2.4f, 3.8f);
        p.size0 = rnd(12, 18) * sc;
        p.size1 = rnd(48, 70) * sc;
        p.c0 = withAlpha(scaleRgb(dust, rnd(0.92f, 1.06f)), 150);
        p.c1 = withAlpha(dust, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 6.28f);
        p.vrot = rnd(-0.6f, 0.6f);
        add(p);
    });
    // Dust boiling up along the base line while the art sinks into it (hides the cut).
    emit(k.baseAcc, 0.12f, 1.6f, [&](float) { return 22 + 34 * k.big; }, [&](float) {
        Particle p{};
        p.x = k.artX0 + rnd(0.05f, 0.95f) * k.artW;
        p.y = k.artY + rnd(-6, 2);
        p.z = rnd(0, 6);
        p.vx = rnd(-12, 12) + kWindX * 0.4f;
        p.vy = rnd(-3, 5);
        p.vz = rnd(10, 28);
        p.drag = 1.1f;
        p.life = rnd(1.4f, 2.4f);
        p.size0 = rnd(10, 18) * sc;
        p.size1 = rnd(30, 52) * sc;
        p.c0 = withAlpha(scaleRgb(dust, rnd(0.85f, 1.0f)), 125);
        p.c1 = withAlpha(dust, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 6.28f);
        p.vrot = rnd(-0.6f, 0.6f);
        add(p);
    });
    // The smoke column: thick and dark at first (lit by the fire from below), thinning out
    // over ~11 s, rising high and drifting with the wind.
    emit(k.smokeAcc, 0.08f, k.lite ? 0.0f : 11.0f, [&](float t) {
        return (8 + 10 * k.big) * (t < 2.5f ? 1.0f : std::max(0.12f, 1 - (t - 2.5f) / 8.5f));
    }, [&](float t) {
        Particle p{};
        p.x = cx + rnd(-0.35f, 0.35f) * (k.x1 - k.x0);
        p.y = cy + rnd(-0.3f, 0.3f) * (k.y1 - k.y0);
        p.z = std::max(4.0f, k.artH * 0.4f * (1 - std::clamp(t / 1.5f, 0.0f, 1.0f)));
        p.vx = kWindX * 1.6f + rnd(-4, 4);
        p.vy = kWindY + rnd(-3, 3);
        p.vz = rnd(26, 44) * (0.85f + 0.3f * k.big);
        p.drag = 0.22f;
        p.life = rnd(4.0f, 6.5f) * (t < 3 ? 1.0f : 0.85f);
        p.size0 = rnd(12, 20) * sc;
        p.size1 = rnd(55, 85) * sc;
        const int g = int(rnd(24, 42));
        if (t < 3 && rnd() < 0.3f) p.c0 = rgba(112, 64, 36, 175);  // lit by the fire below
        else p.c0 = rgba(g, g - 2, g - 4, 185);
        p.c1 = rgba(g + 75, g + 72, g + 68, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 6.28f);
        p.vrot = rnd(-0.4f, 0.4f);
        add(p);
    });
    // Sparks rising from the ruin for a while.
    emit(k.sparkAcc, 1.6f, k.lite ? 0.0f : 16.0f, [&](float t) { return (4 + 7 * k.big) * (1 - t / 16); }, [&](float) {
        Particle p{};
        cellPoint(p.x, p.y, -1);
        p.z = 2;
        p.vx = kWindX + rnd(-6, 6);
        p.vy = kWindY + rnd(-4, 4);
        p.vz = rnd(18, 50);
        p.gravity = 8;
        p.drag = 0.7f;
        p.life = rnd(1.0f, 2.2f);
        p.size0 = rnd(2.2f, 3.2f);
        p.size1 = 1.1f;
        p.c0 = rgba(255, 200, 110, 255);
        p.c1 = rgba(255, 80, 20, 0);
        p.tex = kSpark;
        p.blend = Blend::Add;
        add(p);
    });
    // Fires in the ruin once the art has gone down.
    const int fires = k.lite ? 0 : 1 + k.cells / 3;
    for (int j = 0; j < fires; ++j) {
        const float t = 1.2f + 0.12f * float(j);
        if (!fired(t) || fires_.size() >= 160) continue;
        float px = 0, py = 0;
        cellPoint(px, py, 200 + j);
        fires_.push_back({px, py, to - t, rnd(7, 15) * (0.7f + 0.5f * k.big), rnd(0.6f, 0.95f) * (0.8f + 0.3f * k.big), 0});
    }
}

// The ground marks of every wreck at the current time: shockwave ring, scorch, rubble, embers.
void Effects::buildMarks() {
    marks_.clear();
    bloomMarks();  // spice bloom mounds (under every other mark)
    wormMarks();   // the sand humps of moving worms
    for (const Wreck& k : wrecks_) {
        const float e = time_ - k.start;
        const float cx = (k.x0 + k.x1) * 0.5f, cy = (k.y0 + k.y1) * 0.5f;
        const float R = std::max(k.x1 - k.x0, k.y1 - k.y0) * 0.5f;
        if (e < 0.85f && !k.lite) {  // shockwave: a dark ring of pushed sand with a bright rim
            const float u = e / 0.85f, r = R * 0.8f + (R * 3.0f + 40 + 40 * k.big) * (1 - (1 - u) * (1 - u));
            const float a = std::pow(1 - u, 1.5f);
            marks_.push_back({cx, cy, r, r * 0.82f, 0, kRing, Blend::Alpha, rgba(80, 58, 36, int(120 * a))});
            marks_.push_back({cx, cy, r * 0.96f, r * 0.79f, 0, kRing, Blend::Add, rgba(255, 215, 160, int(70 * a))});
        }
        const float fadeOut = 1 - smooth01(20, 28, e);
        const float scorch = smooth01(0.25f, 1.4f, e) * fadeOut * (k.lite ? 0.35f : 1.0f);
        if (scorch > 0.01f) {
            const float w = k.x1 - k.x0 + 12, h = k.y1 - k.y0 + 12;
            marks_.push_back({cx, cy, w * 0.62f, h * 0.62f, hashf(k.seed, 300) * 6.28f, kSmoke, Blend::Alpha,
                              rgba(26, 20, 15, int(125 * scorch))});
            const int n = 2 + k.cells;
            for (int i = 0; i < n; ++i) {
                const float x = k.x0 - 6 + hashf(k.seed, 310 + i) * w, y = k.y0 - 6 + hashf(k.seed, 330 + i) * h;
                const float s = (0.8f + 0.5f * hashf(k.seed, 350 + i)) * 17;
                marks_.push_back({x, y, s, s * 0.85f, hashf(k.seed, 370 + i) * 6.28f, kSmoke, Blend::Alpha,
                                  rgba(20, 16, 12, int(170 * scorch))});
            }
        }
        const float rubble = smooth01(0.9f, 1.5f, e) * fadeOut;
        if (rubble > 0.01f) {
            static constexpr std::uint32_t kRub[4] = {rgba(150, 136, 118, 255), rgba(178, 162, 140, 255), rgba(112, 102, 90, 255),
                                                      rgba(196, 178, 150, 255)};
            const int n = 5 + k.cells * 3;
            for (int i = 0; i < n; ++i) {
                const float x = k.x0 - 2 + hashf(k.seed, 400 + i) * (k.x1 - k.x0 + 4);
                const float y = k.y0 - 2 + hashf(k.seed, 450 + i) * (k.y1 - k.y0 + 4);
                const float s = 1.3f + 2.4f * hashf(k.seed, 500 + i) * (1 + 0.4f * k.big);
                marks_.push_back({x, y, s, s, hashf(k.seed, 550 + i) * 6.28f, kDebris, Blend::Alpha,
                                  withAlpha(kRub[i & 3], int(235 * rubble))});
            }
        }
        const float glow = k.lite ? 0.0f : smooth01(0.5f, 1.4f, e) * (1 - smooth01(4, 14, e));
        if (glow > 0.01f) {  // embers glowing in the scorch
            const int n = 2 + k.cells;
            for (int i = 0; i < n; ++i) {
                const float h = hashf(k.seed, 600 + i);
                const float x = k.x0 + hashf(k.seed, 620 + i) * (k.x1 - k.x0), y = k.y0 + hashf(k.seed, 640 + i) * (k.y1 - k.y0);
                const float fl = 0.6f + 0.4f * std::sin(time_ * (4 + 6 * h) + h * 20);
                const float s = 4 + 5 * h;
                marks_.push_back({x, y, s, s * 0.8f, 0, kGlow, Blend::Add, rgba(255, 105, 30, int(150 * glow * fl))});
            }
        }
    }
    // Explosion scorch, vehicle wreck hulks, shockwaves and dust rings.
    for (const Scorch& m : scorch_) {
        const float u = m.age / m.life;
        switch (m.kind) {
        case 0:
        case 1: {
            // Fades in fast, holds, fades out over the last 40 % of its life.
            const float a = m.strength * smooth01(0, 0.3f, m.age) * (1 - smooth01(0.6f, 1.0f, u));
            if (a < 0.01f) break;
            const float sz = m.size * (0.85f + 0.15f * smooth01(0, 0.4f, m.age));
            marks_.push_back({m.x, m.y, sz * 0.5f, sz * 0.42f, m.rot, kScorch, Blend::Alpha, rgba(24, 18, 14, int(165 * a))});
            if (m.kind == 1) {  // the charred hulk: blackened chunks of the vehicle
                for (int i = 0; i < 5; ++i) {
                    const float x = m.x + (hashf(m.seed, 10 + i) - 0.5f) * 16, y = m.y + (hashf(m.seed, 20 + i) - 0.5f) * 12;
                    const float s = 2.5f + 3.5f * hashf(m.seed, 30 + i);
                    marks_.push_back({x, y, s, s * 0.8f, hashf(m.seed, 40 + i) * 6.28f, kDebris, Blend::Alpha,
                                      rgba(42, 36, 32, int(240 * a))});
                }
            }
            // Hot ground glowing for a moment, then cooling.
            const float hot = m.kind == 1 ? 1 - smooth01(0, 7, m.age) : 1 - smooth01(0, 2.2f, m.age);
            if (hot > 0.01f) {
                const float fl = 0.8f + 0.2f * std::sin(time_ * 9 + float(m.seed & 63));
                marks_.push_back({m.x, m.y, sz * 0.26f, sz * 0.21f, m.rot, kGlow, Blend::Add,
                                  rgba(255, 110, 36, int(110 * hot * fl * m.strength))});
            }
            break;
        }
        case 2: {  // shockwave: a bright thin ring racing out, a darker ring of pushed sand behind
            const float e = 1 - (1 - u) * (1 - u);
            const float r = m.size * 0.5f * (0.12f + 0.88f * e);
            const float a = std::pow(1 - u, 1.6f) * m.strength;
            marks_.push_back({m.x, m.y, r * 1.03f, r * 0.83f, 0, kRing, Blend::Alpha, rgba(70, 52, 34, int(90 * a))});
            marks_.push_back({m.x, m.y, r, r * 0.8f, 0, kShock, Blend::Add, rgba(255, 228, 185, int(170 * a))});
            break;
        }
        case 4: {  // spice stain: a splash of spice soaking into the sand, fading
            const float a = m.strength * smooth01(0, 0.25f, m.age) * (1 - smooth01(0.5f, 1.0f, u));
            if (a < 0.01f) break;
            marks_.push_back({m.x, m.y, m.size * 0.5f, m.size * 0.42f, m.rot, kScorch, Blend::Alpha, withAlpha(m.c, int(150 * a))});
            break;
        }
        case 5: {  // sand furrow: the sand a worm pushed aside - a lit ridge either side of a soft trough, settling
            const float a = m.strength * smooth01(0, 0.25f, m.age) * (1 - smooth01(0.2f, 1.0f, u));
            if (a < 0.01f) break;
            const float L = m.size * (0.42f + 0.18f * hashf(m.seed, 1)), W = m.size * 0.36f * (1 - 0.55f * u);
            const float nx = -std::sin(m.rot), ny = std::cos(m.rot);
            marks_.push_back({m.x, m.y, L, W * 0.8f, m.rot, kScorch, Blend::Alpha, withAlpha(scaleRgb(m.c, 0.72f), int(48 * a))});
            for (int k = -1; k <= 1; k += 2) {  // the ridges, a little irregular; the one facing north-west lit
                const float o = W * (0.62f + 0.25f * hashf(m.seed, 3 + k)) * float(k);
                const float lit = ny * float(k) < 0 ? 0.62f : 0.4f;  // towards a warm highlight (the dome texture shades it)
                marks_.push_back({m.x + nx * o, m.y + ny * o, L * (0.7f + 0.3f * hashf(m.seed, 7 + k)), W * (0.34f + 0.14f * hashf(m.seed, 9 + k)),
                                  m.rot, kMound, Blend::Alpha, withAlpha(lerpColour(m.c, rgba(255, 242, 214, 255), lit), int(120 * a))});
            }
            break;
        }
        case 6: {  // churned sand where a worm swallowed something: a dark hollow in a rim of heaved sand, smoothed by the wind
            const float a = m.strength * smooth01(0, 0.4f, m.age) * (1 - smooth01(0.3f, 1.0f, u));
            if (a < 0.01f) break;
            const std::uint32_t hi = lerpColour(m.c, rgba(255, 242, 214, 255), 0.35f);
            marks_.push_back({m.x, m.y, m.size * 0.62f, m.size * 0.52f, 0, kRing, Blend::Alpha, withAlpha(hi, int(110 * a))});
            marks_.push_back({m.x, m.y, m.size * 0.42f, m.size * 0.34f, m.rot, kScorch, Blend::Alpha,
                              withAlpha(scaleRgb(m.c, 0.68f), int(105 * a))});
            for (int i = 0; i < 9; ++i) {  // lumps of sand thrown round the hollow
                const float ang = float(i) / 9 * 2 * kPi + hashf(m.seed, 60 + i) * 0.5f;
                const float r = m.size * (0.42f + 0.16f * hashf(m.seed, 70 + i));
                const float sz = m.size * (0.06f + 0.05f * hashf(m.seed, 80 + i));
                marks_.push_back({m.x + std::cos(ang) * r, m.y + std::sin(ang) * r * 0.82f, sz, sz * 0.7f, ang, kSmoke, Blend::Alpha,
                                  withAlpha(hi, int(90 * a))});
            }
            break;
        }
        case 7: {  // sand shockwave: a ring of sand heaved out along the surface (dark trough, light crest)
            const float e = 1 - (1 - u) * (1 - u);
            const float r = m.size * 0.5f * (0.15f + 0.85f * e);
            const float a = std::pow(1 - u, 1.4f) * m.strength;
            marks_.push_back({m.x, m.y, r * 1.04f, r * 0.85f, 0, kRing, Blend::Alpha, withAlpha(scaleRgb(m.c, 0.6f), int(130 * a))});
            marks_.push_back({m.x, m.y, r, r * 0.82f, 0, kShock, Blend::Alpha, withAlpha(lerpColour(m.c, rgba(255, 242, 214, 255), 0.5f), int(230 * a))});
            break;
        }
        case 3: {  // dust ring: the ground's dust kicked out along the surface
            const float e = 1 - (1 - u) * (1 - u) * (1 - u);
            const float r = m.size * 0.5f * (0.2f + 0.8f * e);
            const float a = std::pow(1 - u, 1.3f) * m.strength;
            marks_.push_back({m.x, m.y, r, r * 0.8f, 0, kShock, Blend::Alpha, withAlpha(m.c, int(120 * a))});
            break;
        }
        default: break;
        }
    }
}

void Effects::drawMarks(const Camera& cam) {
    if (marks_.empty()) return;
    const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    vAlpha_.clear();
    vAdd_.clear();
    vLight_.clear();
    for (const GroundMark& m : marks_) {
        auto& dst = m.blend == Blend::Add ? vAdd_ : m.blend == Blend::Light ? vLight_ : vAlpha_;
        quad(dst, m.x * z - ox, m.y * z - oy, m.hw * z, m.hh * z, m.rot, m.c, 1, m.tex);
    }
    if (graded_)
        for (auto& vx : vAlpha_) {
            vx.color.r *= grade_[0];
            vx.color.g *= grade_[1];
            vx.color.b *= grade_[2];
        }
    flush(vAlpha_, Blend::Alpha, atlasTex_);
    flush(vLight_, Blend::Light, atlasTex_);
    flush(vAdd_, Blend::Add, atlasTex_);
}

bool Effects::collapseLook(int side, int index, CollapseLook& out) const {
    const Wreck* k = nullptr;
    for (const auto& o : wrecks_)
        if (o.present && o.side == side && o.index == index) k = &o;
    if (!k) return false;
    const float e = std::max(0.0f, time_ - k->start);
    const int n = std::clamp(int(k->artW / 22) + 1, 2, 7);
    out.pieces = n;
    float sum = 0;
    for (int i = 0; i < n; ++i) {
        CollapseLook::Piece& p = out.piece[i];
        p.u0 = float(i) / float(n);
        p.u1 = float(i + 1) / float(n);
        const float off = n > 1 ? (float(i) - float(n - 1) * 0.5f) / (float(n - 1) * 0.5f) : 0;  // -1 .. 1
        const float h = hashf(k->seed, 700 + i);
        const float delay = 0.1f + 0.25f * h + 0.12f * std::abs(off);  // the middle goes first
        const float local = std::clamp((e - delay) / 1.15f, 0.0f, 1.0f);
        p.sink = std::min(0.97f, local * local * (0.88f + 0.12f * h));
        const float dir = off != 0 ? off : (h < 0.5f ? -1.0f : 1.0f);
        p.lean = dir * (2 + 6 * k->big) * std::sqrt(local) * (0.6f + 0.4f * hashf(k->seed, 720 + i));
        sum += p.sink;
    }
    out.sink = sum / float(n);
    out.shade = 1 - 0.55f * smooth01(0, 1.3f, e);
    const float amp = (e < 0.15f ? 1.8f : 1.1f) * (1 - std::clamp(e / 1.6f, 0.0f, 1.0f)) * (0.7f + 0.8f * k->big);
    out.dx = amp * std::sin(time_ * 71 + float(k->seed & 63));
    out.dy = amp * 0.6f * std::sin(time_ * 53 + float((k->seed >> 6) & 63));
    return true;
}

// ---- Sonic heat haze --------------------------------------------------------------------------
void Effects::buildHaze(std::vector<SDL_Vertex>& v, std::vector<int>& idx, const ToScreen& toScreen, float texW,
                        float texH, bool coreKeep) const {
    v.clear();
    idx.clear();
    if (texW <= 0 || texH <= 0) return;
    constexpr int kRows = 9;
    for (const SonicBeam& bm : sonic_) {
        const float dx = bm.hx - bm.ox, dy = bm.hy - bm.oy, L = std::hypot(dx, dy);
        if (L < 3) continue;
        const float life = bm.dying < 0 ? std::min(1.0f, bm.age / 0.1f) : std::max(0.0f, 1 - bm.dying / 0.45f);
        if (life <= 0) continue;
        const float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
        const float t = time_ + bm.seed;
        const float pad = 12, s0 = -pad, s1 = L + pad;
        const int segs = std::clamp(int((s1 - s0) / 5), 2, 200);
        const int base = int(v.size());
        bool ok = true;
        for (int i = 0; i <= segs && ok; ++i) {
            const float s = s0 + (s1 - s0) * float(i) / float(segs);
            const float env = smooth01(0, 24, s + pad) * smooth01(0, 18, s1 - s);
            const float W = 26 * (0.7f + 0.3f * std::clamp(s / 80, 0.0f, 1.0f));
            for (int j = 0; j < kRows; ++j) {
                const float q = -1 + 2.0f * float(j) / float(kRows - 1);
                const float n = q * W;
                float prof = 1 - q * q;
                prof *= prof;
                if (coreKeep) prof *= 0.3f + 0.7f * std::min(1.0f, std::abs(n) / 9);
                const float a = 2.6f * prof * env * life;
                const float dp = a * (0.6f * std::sin(s * 0.21f - t * 9 + n * 0.17f) + 0.4f * std::sin(s * 0.5f + t * 13 - n * 0.31f));
                const float da = a * 0.5f * std::sin(s * 0.33f - t * 11 + n * 0.23f + 1.3f);
                const float wx = bm.ox + ux * s + nx * n, wy = bm.oy + uy * s + ny * n;
                float sx = 0, sy = 0, tx = 0, ty = 0;
                if (!toScreen(wx, wy, sx, sy) || !toScreen(wx + nx * dp + ux * da, wy + ny * dp + uy * da, tx, ty)) {
                    ok = false;
                    break;
                }
                v.push_back(SDL_Vertex{{sx, sy}, {1, 1, 1, 1}, {tx / texW, ty / texH}});
            }
        }
        if (!ok) {
            v.resize(std::size_t(base));
            continue;
        }
        for (int i = 0; i < segs; ++i)
            for (int j = 0; j < kRows - 1; ++j) {
                const int a = base + i * kRows + j, b = a + kRows;
                idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
    }
}

void Effects::step(float dt) {
    for (std::size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        const float before = p.age;
        p.age += dt;
        if (p.age >= p.life) {
            p = particles_.back();
            particles_.pop_back();
            continue;
        }
        if (p.age >= 0) {
            // A delayed particle that starts within this step moves only for the part of the step
            // after its start (a staggered stream stays a stream at low frame rates, not clumps).
            const float sdt = before < 0 ? p.age : dt;
            const float k = std::exp(-p.drag * sdt);
            p.vx *= k;
            p.vy *= k;
            p.vz += p.gravity * sdt;
            p.x += p.vx * sdt;
            p.y += p.vy * sdt;
            p.z += p.vz * sdt;
            if (p.z < 0) {
                p.z = 0;
                if (p.gravity < 0) {  // bounce and skid (sparks, debris)
                    if ((p.fx & (kFxLandDust | kFxLandFire)) && !(p.fx & kFxLanded) && p.vz < -40) {
                        p.fx |= kFxLanded;
                        landed(p);
                    }
                    p.vz = -p.vz * 0.3f;
                    p.vx *= 0.5f;
                    p.vy *= 0.5f;
                    p.vrot *= 0.5f;
                }
            }
            p.rot += p.vrot * dt;
            if ((p.fx & kFxTrail) && p.z > 1) {  // burning debris: smoke and a small flame behind it
                p.acc -= std::hypot(p.vx, p.vy, p.vz) * dt;
                if (p.acc <= 0) {
                    p.acc += 4.5f;
                    Particle q{};
                    q.x = p.x;
                    q.y = p.y;
                    q.z = p.z;
                    q.vx = kWindX * 0.5f;
                    q.vy = kWindY * 0.5f;
                    q.vz = 4;
                    q.drag = 0.8f;
                    q.life = 0.5f + 0.3f * float(i % 5) / 4;
                    q.size0 = 2.5f;
                    q.size1 = 9;
                    q.c0 = rgba(52, 48, 44, 110);
                    q.c1 = rgba(110, 106, 100, 0);
                    q.tex = kSmoke;
                    q.blend = Blend::Alpha;
                    q.rot = float(i % 7);
                    q.vrot = 0.6f;
                    pending_.push_back(q);
                    q.tex = kFlame;
                    q.blend = Blend::Add;
                    q.life = 0.14f;
                    q.size0 = 5;
                    q.size1 = 2;
                    q.vz = 0;
                    q.c0 = rgba(255, 190, 90, 210);
                    q.c1 = rgba(255, 90, 20, 0);
                    pending_.push_back(q);
                }
            }
        }
        ++i;
    }
    for (const Particle& q : pending_) add(q);
    pending_.clear();
    for (auto& t : treads_) t.age += dt;
}

// A debris chunk hit the ground: a dust puff, and sometimes a small fire that burns on.
void Effects::landed(const Particle& p) {
    for (int k = 0; k < 2; ++k) {
        Particle d{};
        d.x = p.x + rnd(-2, 2);
        d.y = p.y + rnd(-2, 2);
        d.z = 1;
        d.vx = p.vx * 0.3f + rnd(-6, 6);
        d.vy = p.vy * 0.3f + rnd(-4, 4);
        d.vz = rnd(4, 12);
        d.drag = 2.0f;
        d.life = rnd(0.7f, 1.2f);
        d.size0 = p.size0 * 1.2f + 2;
        d.size1 = p.size0 * 3 + rnd(8, 14);
        d.c0 = rgba(150, 122, 92, 120);
        d.c1 = rgba(170, 145, 115, 0);
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.rot = rnd(0, 6.28f);
        d.vrot = rnd(-1, 1);
        pending_.push_back(d);
    }
    if ((p.fx & kFxLandFire) && fires_.size() < 160) fires_.push_back({p.x, p.y, 0, rnd(2.5f, 6.0f), rnd(0.3f, 0.45f), 0});
}

void Effects::update(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                     bool showShroud) {
    craterAtlas_ = &atlas;
    lights_.clear();
    beams_.clear();  // per-frame strips: projectiles (projectileLook) and sonic ribbons (buildBeams)
    time_ += dt;
    const bool first = !primed_;
    if (first) {
        // First frame of this view (screenshots, after a load or a mode switch): warm the
        // continuous emitters up so smoke columns and sparkles are already there.
        for (int k = 0; k < 40; ++k) {
            lights_.clear();
            emitContinuous(w, atlas, 0.05f, cam, viewW, viewH, showShroud, true);
            step(0.05f);
            time_ += 0.05f;
        }
        lights_.clear();
    }
    emitContinuous(w, atlas, dt, cam, viewW, viewH, showShroud, false);

    const float vx0 = cam.x - 96, vy0 = cam.y - 96;
    const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 128;
    auto inView = [&](float x, float y) { return x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1; };
    auto shrouded = [&](float x, float y) {
        const int tx = int(x) >> 5, ty = int(y) >> 5;
        return showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
    };

    updateWrecks(w, atlas, dt, cam, viewW, viewH, showShroud);
    updateBlooms(w, atlas, dt, cam, viewW, viewH, showShroud, first);
    updateWorms(w, atlas, dt, cam, viewW, viewH, showShroud);  // (render/effects_events.cpp)
    updateRefineryVents(w, dt, cam, viewW, viewH, showShroud);  // (render/effects_events.cpp)
    updateWeldSparks(w, dt, cam, viewW, viewH, showShroud);     // (render/effects_events.cpp)
    // The death explosions of a wrecked building (EX_BUILDING, 2 per solid tile) do not shake on
    // their own: the wreck shakes by the building's size.
    auto inWreck = [&](float x, float y) -> const Wreck* {
        for (const Wreck& k : wrecks_)
            if (k.present && x >= k.x0 - 20 && x <= k.x1 + 20 && y >= k.y0 - 20 && y <= k.y1 + 20) return &k;
        return nullptr;
    };
    // Big bursts / explosion lights already placed this frame: one more within ~2 tiles is
    // "crowded" (no extra flash, no extra light) so overlapping explosions do not saturate.
    std::vector<std::pair<float, float>> bursts, glows;
    auto crowd = [](std::vector<std::pair<float, float>>& v, float x, float y) {
        for (const auto& [px, py] : v)
            if (std::fabs(px - x) < 56 && std::fabs(py - y) < 56) return true;
        v.emplace_back(x, y);
        return false;
    };
    // Shots: muzzle flashes (every weapon) and tracers (machine guns) from the world's shot log
    // (World::shotEvents), at the tick the bullet leaves the barrel (creation tick + start
    // delay). Machine-gun bullets are created and land within one tick, so they are never seen
    // as objects; the log carries every shot exactly once, at its real rate.
    {
        const std::uint32_t now = w.gameTicks();
        if (first || now < lastShotTick_) lastShotTick_ = now;
        if (dt > 0 && now > lastShotTick_) {
            for (const game::ShotEvent& e : w.shotEvents()) {
                const std::uint32_t ft = e.tick + std::uint32_t(std::max(0, e.delay));
                if (ft <= lastShotTick_ || ft > now) continue;
                if (e.weapon < 0 || e.weapon >= int(weaponClass_.size()) || e.side < 0 || e.side >= int(w.sides().size()))
                    continue;
                const float sx = float(e.srcX), sy = float(e.srcY);
                if (!inView(sx, sy) || shrouded(sx, sy)) continue;
                spawnShot(w, e, weaponClass_[std::size_t(e.weapon)]);
            }
            lastShotTick_ = now;
        }
    }
    for (int s = 0; s < int(w.sides().size()) && s < game::kMaxSides; ++s) {
        const auto& side = w.sides()[s];
        // Explosions.
        auto& seenE = seenEx_[s];
        if (seenE.size() != side.explosions.size()) seenE.assign(side.explosions.size(), SlotSeen{});
        for (std::size_t i = 0; i < side.explosions.size(); ++i)
            if (!side.explosions[i].alive) seenE[i].key = -1;
        for (int i = side.firstExplosion; i >= 0; i = side.nextObject(i)) {
            const auto& e = side.explosions[i];
            if (!e.alive) continue;
            // Delayed explosions (kHidden until their first frame step: a building's death
            // explosions) count from the moment they show.
            if (e.flags & game::explosion_flags::kHidden) continue;
            SlotSeen& ss = seenE[i];
            const std::int32_t key = e.type + 1;
            const bool fresh = ss.key != key || e.frame < ss.a;
            ss.key = key;
            ss.a = e.frame;
            if (fresh) ss.done = false;  // explosions: true = drawn as a spice splash (no fireball glow)
            if (e.type < 0 || e.type >= int(exClass_.size())) continue;
            const ExClass c = exClass_[e.type];
            const float x = float(e.posX) / 65536.0f, y = float(e.posY) / 65536.0f, z = float(e.posZ) / 65536.0f;
            if (!inView(x, y) || shrouded(x, y)) continue;
            const Wreck* wk = c == ExClass::Huge ? inWreck(x, y) : nullptr;
            const bool wreck = wk != nullptr;
            if (c == ExClass::HarvDust) {  // (render/effects_events.cpp)
                // A burst when the load step's dust appears, a trickle while its animation runs.
                if (fresh) spawnHarvestDust(w, atlas, x, y, exDir_[std::size_t(e.type)], true);
                else if (dt > 0 && rnd() < dt * 10) spawnHarvestDust(w, atlas, x, y, exDir_[std::size_t(e.type)], false);
                continue;
            }
            if (fresh && c != ExClass::None) {
                // Original non-weapon events (render/effects_events.cpp).
                if (c == ExClass::Smoke) {
                    spawnDamageSmoke(w, x, y, z);
                    continue;
                }
                if (c == ExClass::Refinery) {
                    spawnRefineryVent(w, x, y);
                    continue;
                }
                if (c == ExClass::BloomGrow) {
                    spawnBloomGrow(w, atlas, x, y + 16);
                    continue;
                }
                if (c == ExClass::WormSign) {
                    spawnWormSign(w, atlas, x, y);
                    continue;
                }
                if (c == ExClass::WormMaw) {
                    spawnWormStrike(w, atlas, x, y, cam, viewW, viewH);
                    continue;
                }
                if (c == ExClass::WormDust) {
                    spawnWormDust(w, atlas, x, y);
                    continue;
                }
                if (c == ExClass::Medium && spiceWeapon_ >= 0 && nearSpiceTarget(x, y)) {
                    // A spice shell of a bursting bloom came down (its hit explosion is EX_TANKGUN):
                    // a splash of spice, not a fireball.
                    spawnSpiceSplash(w, atlas, x, y);
                    ss.done = true;
                    continue;
                }
            }
            if (fresh && c != ExClass::None) {
                float age = 0;
                if (first) {
                    const int frames = e.type < int(t_.animationArtFrames.size()) ? std::max(1, int(t_.animationArtFrames[e.type])) : 8;
                    age = std::clamp(float(e.frame) / float(frames), 0.0f, 1.0f) * 0.6f;
                }
                const bool big = c == ExClass::Huge || c == ExClass::Big || c == ExClass::Medium;
                const float scale = e.type < int(exScale_.size()) ? exScale_[e.type] : 1.0f;
                if (wreck && wk->lite) {
                    // A wall (or another small non-repairable structure) breaking: small bursts.
                    spawnExplosion(ExClass::Medium, x + rnd(-4, 4), y + rnd(-4, 4), z + rnd(0, 4), age, cam, viewW, viewH, atlas, w,
                                   crowd(bursts, x, y), side.house, rnd(0.7f, 0.9f), 0.0f);
                } else if (wreck) {
                    // A destroyed building's death explosions (2 per solid tile, staggered): the
                    // original sprites are replaced by fireballs of varied size, thinner where
                    // they bunch up; the wreck adds the flash, the shake and the rest.
                    spawnExplosion(ExClass::Big, x + rnd(-4, 4), y + rnd(-4, 4), z + rnd(0, 10), age, cam, viewW, viewH, atlas, w,
                                   crowd(bursts, x, y) && rnd() < 0.6f, side.house, rnd(0.7f, 1.05f), 0.0f);
                } else {
                    spawnExplosion(c, x, y, z, age, cam, viewW, viewH, atlas, w, big && crowd(bursts, x, y), side.house, scale, 1.0f);
                }
            }
            if (ss.done) continue;
            // The fireball's glow while the explosion lasts (its flash is a timed light).
            if (!(e.flags & game::explosion_flags::kBlinkOff) && !((c == ExClass::Huge || c == ExClass::Big) && crowd(glows, x, y - z)) &&
                !wreck) {
                const float flick = 0.85f + 0.15f * std::sin(time_ * 31 + float(i));
                const int frames = e.type < int(t_.animationArtFrames.size()) ? std::max(1, int(t_.animationArtFrames[e.type])) : 8;
                const float fade = 1 - 0.7f * std::clamp(float(e.frame) / float(frames), 0.0f, 1.0f);
                const float sc = e.type < int(exScale_.size()) ? exScale_[e.type] : 1.0f;
                switch (c) {
                case ExClass::Huge: lights_.push_back({x, y - z, 170 * sc, rgba(255, 130, 45, int(64 * flick * fade)), z}); break;
                case ExClass::Big: lights_.push_back({x, y - z, 115 * sc, rgba(255, 130, 45, int(52 * flick * fade)), z}); break;
                case ExClass::Medium: lights_.push_back({x, y - z, 66 * sc, rgba(255, 150, 60, int(48 * flick * fade)), z}); break;
                case ExClass::Flame: lights_.push_back({x, y - z, 46, rgba(255, 140, 50, int(60 * flick)), z}); break;
                case ExClass::Spark: lights_.push_back({x, y - z, 30, rgba(120, 255, 150, 90), z}); break;
                case ExClass::Gas: lights_.push_back({x, y - z, 60, withAlpha(gasBody(side.house), int(40 * flick)), z}); break;
                case ExClass::Plasma: lights_.push_back({x, y - z, 96, rgba(220, 100, 255, int(110 * flick)), z}); break;
                default: break;
                }
            }
        }
        // Bullets: muzzle effects on spawn, per-frame lights and trails.
        auto& seenB = seenBullet_[s];
        if (seenB.size() != side.bullets.size()) seenB.assign(side.bullets.size(), SlotSeen{});
        // A bullet that ended (slot freed or reused): sonic shots ripple where they stop, their
        // ribbon fades out.
        auto ended = [&](SlotSeen& ss) {
            if (ss.key <= 0 || ss.key - 1 >= int(weaponClass_.size())) return;
            if (weaponClass_[ss.key - 1] == WeaponClass::Sonic) {
                if (ss.visible) spawnSonicRipple(ss.lx, ss.ly);
                for (auto& bm : sonic_)
                    if (bm.id == ss.beam && bm.dying < 0) bm.dying = 0;
            }
            ss.beam = -1;
        };
        for (std::size_t i = 0; i < side.bullets.size(); ++i)
            if (!side.bullets[i].alive) {
                if (seenB[i].key >= 0) ended(seenB[i]);
                seenB[i].key = -1;
            }
        for (int i = side.firstBullet; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.bullets[i];
            if (!b.alive || b.type < 0 || b.type >= formats::kMaxWeapons) continue;
            SlotSeen& ss = seenB[i];
            const std::int32_t key = b.type + 1;
            const float x = float(b.posX) / 65536.0f, y = float(b.posY) / 65536.0f, z = float(b.posZ) / 65536.0f;
            if (ss.key != key || b.moveSteps > ss.a) {
                if (ss.key >= 0) ended(ss);
                ss.done = first;  // the first frame shows bullets mid-flight: no muzzle flash
                ss.lx = x;
                ss.ly = y - z;
                ss.lz = z;
                ss.acc = 0;
                ss.visible = false;
            }
            ss.key = key;
            ss.a = b.moveSteps;
            if (b.delay != 0) continue;
            const WeaponClass c = weaponClass_[b.type];
            if (c == WeaponClass::Sonic) {
                // The sonic ribbon spans from where the shot was first seen to its head.
                SonicBeam* bm = nullptr;
                for (auto& o : sonic_)
                    if (o.id == ss.beam) bm = &o;
                if (!bm) {
                    SonicBeam n;
                    n.id = ss.beam = nextBeamId_++;
                    n.ox = n.hx = x;
                    n.oy = n.hy = y - z;
                    if (first && b.firerIndex >= 0 && b.firerIndex < int(side.units.size()) && side.units[b.firerIndex].alive) {
                        n.ox = float(side.units[b.firerIndex].posX) / 65536.0f;
                        n.oy = float(side.units[b.firerIndex].posY) / 65536.0f;
                    }
                    n.seed = rnd(0, 100);
                    if (first) n.age = 0.2f;  // already flying when the view opened: fully faded in
                    sonic_.push_back(n);
                    bm = &sonic_.back();
                }
                bm->hx = x;
                bm->hy = y - z;
                bm->seen = true;
            }
            if (!inView(x, y) || shrouded(x, y)) {
                ss.done = true;
                ss.lx = x;
                ss.ly = y - z;
                ss.lz = z;
                ss.visible = false;
                continue;
            }
            ss.visible = true;
            ss.done = true;  // muzzle flash / tracer: from the shot log (spawnShot)
            // Trails by distance travelled this frame.
            const float sy = y - z, dist = std::hypot(x - ss.lx, sy - ss.ly);
            if (dt > 0 && dist < 200) ss.acc += dist;
            // The projectile itself (its original sprite is not drawn): from its motion.
            if (c != WeaponClass::Gun && c != WeaponClass::Sonic) {
                float vx = x - ss.lx, vy = (y - ss.ly - ss.lz), vz = z - ss.lz;
                if (std::hypot(vx, vy) < 0.05f && (b.speedX || b.speedY)) {
                    vx = float(b.speedX);
                    vy = float(b.speedY);
                    vz = 0;
                } else if (std::hypot(vx, vy) < 0.05f) {
                    vx = float(b.targetX) - x;
                    vy = float(b.targetY) - y;
                }
                projectileLook(c, x, y, z, vx, vy, vz, side.house, i);
            }
            float spacing = 0;
            switch (c) {
            case WeaponClass::Rocket: spacing = 3; lights_.push_back({x, sy, 34, rgba(255, 150, 60, 120), z}); break;
            case WeaponClass::Artillery: spacing = 6; lights_.push_back({x, sy, 20, rgba(255, 190, 110, 90), z}); break;
            case WeaponClass::Cannon: spacing = 8; lights_.push_back({x, sy, 22, rgba(255, 210, 140, 110), z}); break;
            case WeaponClass::Sonic: spacing = 11; lights_.push_back({x, sy, 46, rgba(120, 200, 255, 130), z}); break;
            case WeaponClass::Plasma: spacing = 4; lights_.push_back({x, sy, 66, rgba(215, 100, 255, 210), z}); break;
            case WeaponClass::Gas: spacing = 3.5f; lights_.push_back({x, sy, 44, withAlpha(gasGlow(side.house), 80), z}); break;
            case WeaponClass::Debris: spacing = 6; lights_.push_back({x, sy, 20, rgba(255, 140, 50, 140), z}); break;
            case WeaponClass::Spice: spacing = 4; break;
            default: break;
            }
            if (spacing > 0 && ss.acc >= spacing) {
                const float ux = dist > 0 ? (x - ss.lx) / dist : 0, uy = dist > 0 ? (sy - ss.ly) / dist : 0;
                const float dir = std::atan2(uy, ux);
                int n = 0;
                while (ss.acc >= spacing && n++ < 16) {
                    ss.acc -= spacing;
                    const float back = ss.acc;  // place along the segment travelled
                    const float tb = dist > 0 ? std::min(1.0f, back / dist) : 0;
                    Particle p{};
                    p.x = x - ux * back;
                    p.y = sy - uy * back;
                    p.z = 0;
                    p.rot = rnd(0, 6.28f);
                    switch (c) {
                    case WeaponClass::Rocket:
                    case WeaponClass::Artillery:
                    case WeaponClass::Debris:
                    case WeaponClass::Cannon: {
                        // Smoke at the trail point's height (3D: hangs in the air where the
                        // projectile flew; 2D: the same drawn point).
                        const float pz = z - (z - ss.lz) * tb;
                        p.z = pz;
                        p.y += pz;
                        p.tex = kSmoke;
                        p.blend = Blend::Alpha;
                        p.vx = kWindX * 0.4f + rnd(-3, 3);
                        p.vy = rnd(-3, 3);
                        p.vz = rnd(2, 6);
                        p.vrot = rnd(-1, 1);
                        if (c == WeaponClass::Rocket) {  // a dense white trail that spreads and lingers
                            p.x += -uy * rnd(-1, 1);
                            p.y += ux * rnd(-1, 1);
                            p.vx += -uy * rnd(-5, 5);
                            p.vy += ux * rnd(-5, 5);
                            p.drag = 0.6f;
                            p.life = rnd(1.4f, 2.3f);
                            p.size0 = rnd(4, 5.5f);
                            p.size1 = rnd(17, 25);
                            p.c0 = rgba(250, 246, 236, 175);
                            p.c1 = rgba(214, 208, 198, 0);
                        } else if (c == WeaponClass::Debris) {
                            p.life = rnd(0.6f, 1.0f);
                            p.size0 = rnd(3, 5);
                            p.size1 = rnd(12, 17);
                            p.c0 = rgba(50, 46, 42, 150);
                            p.c1 = rgba(90, 86, 80, 0);
                        } else {  // shells: a faint wisp
                            p.life = rnd(0.35f, 0.6f);
                            p.size0 = 2.5f;
                            p.size1 = rnd(7, 10);
                            p.c0 = rgba(225, 220, 212, c == WeaponClass::Cannon ? 55 : 75);
                            p.c1 = rgba(200, 196, 190, 0);
                        }
                        break;
                    }
                    case WeaponClass::Sonic: {
                        // Arc wavefronts, convex towards the target, bigger and fainter away from
                        // the emitter, each with a fainter echo just behind.
                        float from = 0;
                        for (const auto& o : sonic_)
                            if (o.id == ss.beam) from = std::hypot(p.x - o.ox, p.y - o.oy);
                        const float R = std::clamp(7 + from * 0.1f, 7.0f, 22.0f);
                        const float fade = std::clamp(1.1f - from / 260, 0.45f, 1.0f);
                        for (int k = 0; k < 3; ++k) {
                            Particle a = p;
                            a.tex = kArc;
                            a.aligned = true;
                            a.rot = dir;
                            a.x = p.x - ux * R * (k == 2 ? 1.25f : 1.0f);
                            a.y = p.y - uy * R * (k == 2 ? 1.25f : 1.0f);
                            a.vx = ux * 34;
                            a.vy = uy * 34;
                            a.age = k == 2 ? -0.06f : 0;
                            a.life = 0.6f;
                            a.size0 = 2 * R / 0.82f;
                            a.size1 = a.size0 * 1.7f;
                            if (k == 0) {  // colour (alpha: reads as blue on bright sand)
                                a.blend = Blend::Alpha;
                                a.c0 = rgba(70, 150, 255, int(150 * fade));
                                a.c1 = rgba(110, 190, 255, 0);
                            } else {  // glow rim / echo
                                a.blend = Blend::Add;
                                a.c0 = k == 1 ? rgba(170, 230, 255, int(115 * fade)) : rgba(150, 200, 255, int(55 * fade));
                                a.c1 = rgba(80, 150, 255, 0);
                            }
                            add(a);
                        }
                        for (int k = 0; k < 2; ++k) {  // motes riding the waves
                            Particle m = p;
                            m.tex = kSpark;
                            m.blend = Blend::Add;
                            const float sd = rnd(-1, 1);
                            m.x += -uy * sd * 8;
                            m.y += ux * sd * 8;
                            m.vx = ux * rnd(20, 70) - uy * sd * rnd(5, 18);
                            m.vy = uy * rnd(20, 70) + ux * sd * rnd(5, 18);
                            m.vz = rnd(-4, 10);
                            m.drag = 1.5f;
                            m.life = rnd(0.35f, 0.7f);
                            m.size0 = rnd(2.6f, 3.6f);
                            m.size1 = 1.2f;
                            m.c0 = k ? rgba(235, 250, 255, 240) : rgba(160, 225, 255, 230);
                            m.c1 = rgba(90, 160, 255, 0);
                            add(m);
                        }
                        continue;  // added above
                    }
                    case WeaponClass::Plasma: {
                        // Magenta plasma: a hot core trail behind each of the twin bolts, violet haze,
                        // arcing motes.
                        {
                            Particle q = p;  // the second bolt's core trail
                            q.x += uy * kPlasmaTwinHalf;
                            q.y -= ux * kPlasmaTwinHalf;
                            q.tex = kGlow;
                            q.blend = Blend::Add;
                            q.life = rnd(0.22f, 0.32f);
                            q.size0 = rnd(10, 13);
                            q.size1 = 3;
                            q.c0 = rgba(240, 120, 255, 150);
                            q.c1 = rgba(140, 40, 255, 0);
                            add(q);
                            for (int k = -1; k <= 1; k += 2) {  // violet tint under both trails (reads on sand)
                                Particle v = q;
                                v.x = p.x - uy * kPlasmaTwinHalf * float(k);
                                v.y = p.y + ux * kPlasmaTwinHalf * float(k);
                                v.blend = Blend::Alpha;
                                v.size0 = rnd(8, 10);
                                v.c0 = rgba(130, 30, 200, 70);
                                v.c1 = rgba(110, 30, 190, 0);
                                add(v);
                            }
                        }
                        p.x -= uy * kPlasmaTwinHalf;
                        p.y += ux * kPlasmaTwinHalf;
                        p.tex = kGlow;
                        p.blend = Blend::Add;
                        p.life = rnd(0.22f, 0.32f);
                        p.size0 = rnd(10, 13);
                        p.size1 = 3;
                        p.c0 = rgba(240, 120, 255, 150);
                        p.c1 = rgba(140, 40, 255, 0);
                        Particle h = p;
                        h.tex = kGas;
                        h.blend = Blend::Alpha;
                        h.vx = rnd(-5, 5);
                        h.vy = rnd(-5, 5);
                        h.vz = rnd(2, 6);
                        h.drag = 1;
                        h.life = rnd(0.5f, 0.8f);
                        h.size0 = 6;
                        h.size1 = rnd(16, 20);
                        h.c0 = rgba(150, 60, 210, 60);
                        h.c1 = rgba(110, 50, 190, 0);
                        h.vrot = rnd(-1, 1);
                        add(h);
                        if (rnd() < 0.6f) {
                            Particle m = p;
                            m.tex = kSpark;
                            const float a = rnd(0, 2 * kPi), sp = rnd(20, 60);
                            m.vx = std::cos(a) * sp;
                            m.vy = std::sin(a) * sp;
                            m.vz = rnd(0, 20);
                            m.drag = 3;
                            m.life = rnd(0.2f, 0.4f);
                            m.size0 = 3;
                            m.size1 = 1;
                            m.c0 = rgba(255, 200, 255, 255);
                            m.c1 = rgba(170, 60, 255, 0);
                            m.streak = true;
                            add(m);
                        }
                        break;
                    }
                    case WeaponClass::Spice: {
                        // A bloom's spice glob: a thin trail of spice dust hanging where it flew.
                        const float pz = z - (z - ss.lz) * tb;
                        p.z = pz;
                        p.y += pz;
                        p.tex = kSmoke;
                        p.blend = Blend::Alpha;
                        p.vx = kWindX * 0.5f + rnd(-4, 4);
                        p.vy = rnd(-3, 3);
                        p.vz = rnd(-6, 2);
                        p.drag = 1.2f;
                        p.life = rnd(0.7f, 1.2f);
                        p.size0 = rnd(3, 4.5f);
                        p.size1 = rnd(10, 15);
                        p.c0 = rgba(214, 134, 66, 120);
                        p.c1 = rgba(226, 170, 110, 0);
                        p.vrot = rnd(-1, 1);
                        break;
                    }
                    case WeaponClass::Gas: {
                        // A tunnel of the firer's gas: billowing, slowly swirling and spreading
                        // puffs (a darker core now and then), a faint inner glow, drifting motes.
                        const std::uint32_t body = gasBody(side.house), glow = gasGlow(side.house);
                        const float sw = rnd(-1, 1);
                        p.tex = kGas;
                        p.blend = Blend::Alpha;
                        p.vx = -uy * sw * rnd(6, 16) + ux * rnd(4, 16) + kWindX * 0.3f;
                        p.vy = ux * sw * rnd(6, 16) + uy * rnd(4, 16);
                        p.vz = rnd(2, 8);
                        p.drag = 1.1f;
                        p.life = rnd(1.4f, 2.4f);
                        p.size0 = rnd(13, 17);
                        p.size1 = rnd(34, 46);
                        const bool dense = rnd() < 0.3f;
                        p.c0 = withAlpha(dense ? scaleRgb(body, 0.75f) : body, dense ? 175 : 150);
                        p.c1 = withAlpha(scaleRgb(body, 1.12f), 0);
                        p.vrot = sw * rnd(0.8f, 1.8f);
                        Particle g = p;  // inner glow, close to the stream
                        g.tex = kGas;
                        g.blend = Blend::Add;
                        g.vx *= 0.3f;
                        g.vy *= 0.3f;
                        g.life = rnd(0.35f, 0.55f);
                        g.size0 = rnd(8, 11);
                        g.size1 = rnd(14, 18);
                        g.c0 = withAlpha(glow, 50);
                        g.c1 = withAlpha(body, 0);
                        add(g);
                        if (rnd() < 0.35f) {
                            Particle m = p;
                            m.tex = kSpark;
                            m.blend = Blend::Add;
                            m.vx = -uy * sw * rnd(8, 22);
                            m.vy = ux * sw * rnd(8, 22);
                            m.vz = rnd(4, 14);
                            m.drag = 0.8f;
                            m.life = rnd(0.6f, 1.1f);
                            m.size0 = rnd(2.2f, 3.0f);
                            m.size1 = 1.2f;
                            m.c0 = withAlpha(glow, 220);
                            m.c1 = withAlpha(body, 0);
                            add(m);
                        }
                        break;
                    }
                    default: break;
                    }
                    add(p);
                }
            }
            ss.lx = x;
            ss.ly = sy;
            ss.lz = z;
        }
    }
    primed_ = true;
    // Sonic ribbons: age, fade out the ended ones, rebuild the strips.
    for (std::size_t i = 0; i < sonic_.size();) {
        SonicBeam& bm = sonic_[i];
        if (!bm.seen && bm.dying < 0) bm.dying = 0;
        bm.seen = false;
        bm.age += dt;
        if (bm.dying >= 0) bm.dying += dt;
        if (bm.dying > 0.45f) {
            sonic_[i] = sonic_.back();
            sonic_.pop_back();
            continue;
        }
        ++i;
    }
    buildBeams();
    if (dt > 0) step(dt);
    // Timed lights: flashes (instant or with a short attack, then a quadratic fade).
    for (std::size_t i = 0; i < flashes_.size();) {
        Flash& f = flashes_[i];
        f.age += dt;
        if (f.age >= f.life) {
            flashes_[i] = flashes_.back();
            flashes_.pop_back();
            continue;
        }
        float k = 1;
        if (f.attack > 0 && f.age < f.attack) k = f.age / f.attack;
        else {
            const float u = (f.age - f.attack) / std::max(0.001f, f.life - f.attack);
            k = (1 - u) * (1 - u);
        }
        // (0.6: a flash's alpha is its peak; the light blend doubles bright sand quickly.)
        if (!shrouded(f.x, f.y + f.z))
            lights_.push_back({f.x, f.y, f.size * (0.75f + 0.25f * k), withAlpha(f.c, int(float(f.c >> 24) * k * 0.5f)), f.z});
        ++i;
    }
    for (std::size_t i = 0; i < scorch_.size();) {
        scorch_[i].age += dt;
        if (scorch_[i].age >= scorch_[i].life) {
            scorch_.erase(scorch_.begin() + std::ptrdiff_t(i));  // keeps the time order (addScorch drops the oldest)
            continue;
        }
        ++i;
    }
    buildMarks();

    // Screen shake (decays; smooth pseudo-noise so it rumbles instead of jittering).
    if (dt > 0) trauma_ = std::max(0.0f, trauma_ - dt * 1.3f);
    const float amp = trauma_ * trauma_ * 6.0f;  // trauma goes up to 1.25 for big buildings (9 px)
    shakeX_ = amp * (0.6f * std::sin(time_ * 47.3f) + 0.4f * std::sin(time_ * 83.1f + 1.3f));
    shakeY_ = amp * (0.6f * std::sin(time_ * 53.7f + 0.7f) + 0.4f * std::sin(time_ * 71.9f + 2.1f));
}

void Effects::quad(std::vector<SDL_Vertex>& v, float cx, float cy, float hw, float hh, float angle, std::uint32_t c,
                   float alphaMul, Tex tex) {
    const float ca = std::cos(angle), sa = std::sin(angle);
    const float ax = hw * ca, ay = hw * sa, bx = -hh * sa, by = hh * ca;
    const SDL_FColor col{float(c & 0xFF) / 255.0f, float((c >> 8) & 0xFF) / 255.0f, float((c >> 16) & 0xFF) / 255.0f,
                         float(c >> 24) / 255.0f * alphaMul};
    const float u0 = (float(tex) * kTexCell + 0.5f) / float(kTexCell * kTexCount);
    const float u1 = (float(tex + 1) * kTexCell - 0.5f) / float(kTexCell * kTexCount);
    const float v0 = 0.5f / kTexCell, v1 = 1 - 0.5f / kTexCell;
    const SDL_Vertex p0{{cx - ax - bx, cy - ay - by}, col, {u0, v0}};
    const SDL_Vertex p1{{cx + ax - bx, cy + ay - by}, col, {u1, v0}};
    const SDL_Vertex p2{{cx + ax + bx, cy + ay + by}, col, {u1, v1}};
    const SDL_Vertex p3{{cx - ax + bx, cy - ay + by}, col, {u0, v1}};
    v.push_back(p0);
    v.push_back(p1);
    v.push_back(p2);
    v.push_back(p0);
    v.push_back(p2);
    v.push_back(p3);
}

void Effects::flush(std::vector<SDL_Vertex>& v, Blend b, SDL_Texture* tex) {
    if (v.empty() || !tex) return;
    if (b == Blend::Light) {
        static const SDL_BlendMode kLight =
            SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                       SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        if (lightBlendOk_ && !SDL_SetTextureBlendMode(tex, kLight)) {
            lightBlendOk_ = false;  // backend without custom blending: plain additive, dimmer
            SDL_Log("Effects: no custom blend mode (%s), lights fall back to additive", SDL_GetError());
        }
        if (!lightBlendOk_) {
            for (auto& vx : v) {
                vx.color.r *= 0.45f;
                vx.color.g *= 0.45f;
                vx.color.b *= 0.45f;
            }
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        }
    } else {
        SDL_SetTextureBlendMode(tex, b == Blend::Add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
    SDL_RenderGeometry(r_, tex, v.data(), int(v.size()), nullptr, 0);
    v.clear();
}

// ---- 3D bridge: the same looks drawGround / drawAir give, for render3d (world-space quads).
void Effects::texRect(Tex tex, float& u0, float& v0, float& u1, float& v1) {
    u0 = (float(tex) * kTexCell + 0.5f) / float(kTexCell * kTexCount);
    u1 = (float(tex + 1) * kTexCell - 0.5f) / float(kTexCell * kTexCount);
    v0 = 0.5f / kTexCell;
    v1 = 1 - 0.5f / kTexCell;
}

bool Effects::particleLook(const Particle& p, float& size, std::uint32_t& c) {
    if (p.age < 0) return false;
    const float t = p.age / p.life;
    const float e = 1 - (1 - t) * (1 - t);
    size = p.size0 + (p.size1 - p.size0) * e;
    c = (p.fx & kFxLateFade) ? withAlpha(p.c0, int(float(p.c0 >> 24) * std::min(1.0f, (1 - t) * 4)))
                             : lerpColour(p.c0, p.c1, (p.fx & kFxHot) ? t * t : t);
    if (p.tex == kSmoke && t < 0.12f) c = withAlpha(c, int(float(c >> 24) * t / 0.12f));
    if (p.blend == Blend::Light) c = premultiplied(c, 1.0f);
    return true;
}

float Effects::treadAlpha(const Tread& t) {
    if (t.age >= kTreadLife) return 0;
    const float a = 1 - t.age / kTreadLife;
    return 0.28f * a * a;
}

std::uint32_t Effects::treadColour() { return rgba(48, 36, 24, 255); }

std::uint32_t Effects::lightColour(const Light& l) { return premultiplied(l.c, 1.6f); }

void Effects::drawGround(const Camera& cam) {
    if (treads_.empty()) return;
    const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    vGround_.clear();
    for (const auto& t : treads_) {
        if (t.age >= kTreadLife) continue;
        const float a = 1 - t.age / kTreadLife;
        const float sx = t.x * z - ox, sy = t.y * z - oy;
        const float px = -std::sin(t.angle) * 5 * z, py = std::cos(t.angle) * 5 * z;
        const std::uint32_t c = rgba(48, 36, 24, 255);
        quad(vGround_, sx + px, sy + py, 2.6f * z, 1.5f * z, t.angle, c, 0.28f * a * a, kSoft);
        quad(vGround_, sx - px, sy - py, 2.6f * z, 1.5f * z, t.angle, c, 0.28f * a * a, kSoft);
    }
    flush(vGround_, Blend::Alpha, atlasTex_);
}

void Effects::drawAir(const Camera& cam) {
    const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    float vw = 0, vh = 0;
    {
        SDL_Rect vp{};
        SDL_GetRenderViewport(r_, &vp);
        vw = float(vp.w);
        vh = float(vp.h);
    }
    vAlpha_.clear();
    vAdd_.clear();
    vLight_.clear();
    auto one = [&](const Particle& p) {
        if (p.age < 0) return;
        const float t = p.age / p.life;
        const float e = 1 - (1 - t) * (1 - t);  // ease-out growth
        const float size = (p.size0 + (p.size1 - p.size0) * e) * z;
        const float sx = p.x * z - ox, sy = (p.y - p.z) * z - oy;
        const float reach = std::max(size, p.len * z);
        if (sx < -reach || sy < -reach || sx > vw + reach || sy > vh + reach) return;
        std::uint32_t c = (p.fx & kFxLateFade) ? withAlpha(p.c0, int(float(p.c0 >> 24) * std::min(1.0f, (1 - t) * 4)))
                                               : lerpColour(p.c0, p.c1, (p.fx & kFxHot) ? t * t : t);
        float am = 1;
        if (p.tex == kSmoke && t < 0.12f) am = t / 0.12f;  // soft fade-in for smoke
        auto& dst = p.blend == Blend::Add ? vAdd_ : p.blend == Blend::Light ? vLight_ : vAlpha_;
        if (p.blend == Blend::Light) c = premultiplied(c, 1.0f);
        if (p.len > 0) {
            quad(dst, sx, sy, p.len * z * 0.5f, size * 0.5f, p.rot, c, am, p.tex);
        } else if (p.streak) {
            const float svx = p.vx * z, svy = (p.vy - p.vz) * z;
            const float sp = std::hypot(svx, svy);
            const float len = std::max(size, std::min(size * 6, sp * 0.03f));
            quad(dst, sx, sy, len * 0.5f, size * 0.5f, std::atan2(svy, svx), c, am, p.tex);
        } else {
            quad(dst, sx, sy, size * 0.5f, size * 0.5f, p.rot, c, am, p.tex);
        }
    };
    for (const auto& p : beams_) one(p);
    for (const auto& p : particles_) one(p);
    for (const auto& l : lights_) {
        const float s = l.size * z;
        const float sx = l.x * z - ox, sy = l.y * z - oy;
        if (sx < -s || sy < -s || sx > vw + s || sy > vh + s) continue;
        quad(vLight_, sx, sy, s * 0.5f, s * 0.5f, 0, premultiplied(l.c, 1.6f), 1, kGlowPM);
    }
    flush(vAlpha_, Blend::Alpha, atlasTex_);
    flush(vLight_, Blend::Light, atlasTex_);
    flush(vAdd_, Blend::Add, atlasTex_);
}

void Effects::drawPost(int viewW, int viewH) {
    // A faint warm grade (multiply) and the vignette.
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r_, &prev);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColor(r_, 255, 250, 241, 255);
    const SDL_FRect all{0, 0, float(viewW), float(viewH)};
    SDL_RenderFillRect(r_, &all);
    SDL_SetRenderDrawBlendMode(r_, prev);
    if (vignette_) SDL_RenderTexture(r_, vignette_, nullptr, &all);
}

}  // namespace d2k::render
