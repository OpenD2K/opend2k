// Modern versions of the original's non-weapon events (render/effects.hpp, Effects: Modern):
// harvest dust (EX_HARVDUST*), damage smoke and fire (EX_SMOKE3 / EX_FLAME from the units' smoke
// timers and the buildings' BUILEXP.BIN points), spice blooms (the mound drawn from the crate
// record, its growth EX_SPICEBLOOM, the burst = SpiceMound's spice shells and their splashes) and
// building capture (a house-colour flash instead of a destruction). Every effect is timed by the
// simulation's own event, so the simulation stays untouched (docs/enhanced-graphics.md
// "Original events").

#include "render/effects.hpp"

#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"
#include "render/tileset.hpp"
#include "render/weld_fx.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

using namespace fx;

namespace {
constexpr std::uint32_t kSpice = rgba(206, 114, 50, 255);      // spice (mid)
constexpr std::uint32_t kSpiceDark = rgba(150, 70, 28, 255);   // wet / dense spice
constexpr std::uint32_t kSpiceLight = rgba(240, 168, 96, 255); // spice dust in the light

// A bloom's swelling, 0..1: a slow breath that quickens with the stage; a ripe bloom whose timer
// runs low (it bursts when the timer reaches 0 and its cell is free) beats fast and swells.
float bloomPulse(int stage, std::uint32_t timer, std::uint32_t seed, float time) {
    float rate = 1.3f + 0.45f * float(stage);
    if (stage == 3 && timer < 300) rate += 5.0f * (1 - float(timer) / 300.0f);
    const float ph = float(seed & 1023) * 0.01f;
    const float s = 0.5f + 0.5f * std::sin(time * rate + ph);
    return s * s;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// Harvest dust: one EX_HARVDUST<dir> per load step (economy.cpp, harvester state 12), at the
// harvester's position; dir = facing / 4 (0 north, clockwise). A burst when it appears, then a
// trickle while the original animation runs (burst = false). Sand and spice dust boil up behind
// the harvester (where the original draws its dust) and drift off with the wind, grains are
// thrown up and fall back, a low skirt of dust rolls out under the body.
void Effects::spawnHarvestDust(const game::World& w, const TileAtlas& atlas, float x, float y, int dir, bool burst) {
    const float a = float(dir) * (kPi / 4);
    // The original's dust frames sit behind the harvester (the spice it has sifted is blown out
    // at the back): the plume starts there.
    const float fx = -std::sin(a), fy = std::cos(a);  // backwards (y down = south)
    const float ix = x + fx * 13, iy = y + fy * 11;
    const std::uint32_t ground = terrainColour(w, atlas, ix, iy);
    // Dust in the air reads paler than the ground it comes from (sunlit, finer): a pale sand-spice
    // mix, with a denser darker spice core where it leaves the harvester.
    const std::uint32_t dust = lerpColour(lerpColour(ground, kSpiceLight, 0.5f), rgba(250, 222, 176, 255), 0.45f);
    const std::uint32_t core = lerpColour(ground, kSpiceDark, 0.35f);
    const float ld = load();
    for (int i = burst ? 0 : 1; i < (burst ? 5 : 2); ++i) {  // the plume
        Particle p{};
        const float side = rnd(-1, 1);
        p.x = ix - fy * side * 6 + rnd(-2, 2);
        p.y = iy + fx * side * 5 + rnd(-2, 2);
        p.z = rnd(1, 4);
        p.vx = fx * rnd(4, 12) - fy * side * rnd(4, 10) + kWindX * 0.8f;
        p.vy = fy * rnd(4, 12) + fx * side * rnd(3, 8) + kWindY * 0.5f;
        p.vz = rnd(9, 20);
        p.drag = 0.9f;
        p.life = rnd(2.2f, 3.2f);
        p.size0 = rnd(8, 11);
        p.size1 = rnd(34, 48);
        p.c0 = withAlpha(i == 0 ? core : scaleRgb(dust, rnd(0.95f, 1.04f)), int(rnd(165, 200)));
        p.c1 = withAlpha(scaleRgb(dust, 1.06f), 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.7f, 0.7f);
        add(p);
    }
    const int grains = int((burst ? 7 : 3) * ld);
    for (int i = 0; i < grains; ++i) {  // spice grains thrown out
        Particle g{};
        const float ang = a + rnd(-1.2f, 1.2f), sp = rnd(12, 38);
        g.x = ix + rnd(-3, 3);
        g.y = iy + rnd(-3, 3);
        g.z = 2;
        g.vx = std::sin(ang) * sp;
        g.vy = -std::cos(ang) * sp * 0.8f;
        g.vz = rnd(30, 62);
        g.gravity = -190;
        g.drag = 0.6f;
        g.life = rnd(0.45f, 0.8f);
        g.size0 = g.size1 = rnd(1.6f, 2.6f);
        g.c0 = withAlpha(rnd() < 0.5f ? kSpiceDark : kSpice, 235);
        g.c1 = withAlpha(kSpice, 120);
        g.tex = kDebris;
        g.blend = Blend::Alpha;
        g.rot = rnd(0, 2 * kPi);
        g.vrot = rnd(-10, 10);
        g.fx = kFxLateFade;
        add(g);
    }
    if (rnd() < (burst ? 0.6f : 0.25f)) {  // a low skirt rolling out under the body
        Particle s{};
        const float side = rnd() < 0.5f ? -1.0f : 1.0f;
        s.x = x - fy * side * 9;
        s.y = y + fx * side * 7;
        s.z = 0.5f;
        s.vx = -fy * side * rnd(6, 12) + kWindX * 0.5f;
        s.vy = fx * side * rnd(4, 9);
        s.vz = rnd(1, 4);
        s.drag = 1.3f;
        s.life = rnd(1.2f, 1.8f);
        s.size0 = rnd(8, 11);
        s.size1 = rnd(22, 30);
        s.c0 = withAlpha(scaleRgb(ground, 1.08f), 70);
        s.c1 = withAlpha(ground, 0);
        s.tex = kSmoke;
        s.blend = Blend::Alpha;
        s.rot = rnd(0, 2 * kPi);
        s.vrot = rnd(-0.6f, 0.6f);
        add(s);
    }
}

// ---------------------------------------------------------------------------------------------
// Damage smoke and fire. The original's events: a damaged vehicle (below half health, smoke
// timer 64..191 set by each hit, one less every 4 ticks) spawns EX_SMOKE3 every ~4 ticks at its
// position; a damaged building (selectable, below half health) at each BUILEXP.BIN point whose
// bit is set, EX_SMOKE3 / EX_FLAME with the point's period (3-12 ticks), the bits clearing at
// random over the timer's last 200 ticks. So the smoke and fire flare up after hits and die out
// on their own; walls (not selectable) never smoke.

namespace {
// The building whose damage smoke this point belongs to (its smoke timer runs and its art box
// holds the point): its health fraction and the ground line under the art (3D: the smoke starts
// on the roof, at the same drawn point as in 2D).
bool smokingBuilding(const game::World& w, const formats::Templates& t, float x, float y, float& hp, float& groundY) {
    for (const auto& side : w.sides())
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.buildings[i];
            if (!b.alive || b.smokeTimer == 0 || b.type < 0 || b.type >= formats::kMaxBuildingTypes) continue;
            const auto& tp = t.bin.buildings[b.type];
            const float x0 = float(b.tileX * 32) - 8, x1 = float(b.tileX * 32 + tp.artWidth) + 8;
            const float y0 = float(b.tileY * 32) - 16, y1 = float(b.tileY * 32 + tp.artHeight) + 8;
            if (x < x0 || x > x1 || y < y0 || y > y1) continue;
            hp = tp.hitPoints ? float(b.health) / float(tp.hitPoints) : 0.4f;
            groundY = std::max(y, float(b.tileY * 32 + tp.artHeight) - 14);
            return true;
        }
    return false;
}
}  // namespace

void Effects::spawnDamageSmoke(const game::World& w, float x, float y, float z) {
    float hp = 0.4f, groundY = y;
    if (smokingBuilding(w, t_, x, y, hp, groundY)) {
        // A smoke point on a building: a dark puff, denser and higher the worse the damage.
        const float dmg = std::clamp(1 - hp * 2, 0.0f, 1.0f);  // 0 at half health .. 1 at 0
        for (int k = 0; k < 2; ++k) {
        Particle p{};
        p.x = x + rnd(-2.5f, 2.5f);
        p.y = groundY + rnd(-2, 2);
        p.z = groundY - y + rnd(0, 3);
        p.vx = kWindX * 1.3f + rnd(-3, 3);
        p.vy = kWindY + rnd(-2, 2);
        p.vz = rnd(18, 28) * (0.8f + 0.35f * dmg);
        p.drag = 0.3f;
        p.life = rnd(1.8f, 2.8f) * (0.8f + 0.35f * dmg);
        p.size0 = rnd(8, 11);
        p.size1 = rnd(30, 42) * (0.85f + 0.35f * dmg);
        if (k == 1) {  // a second, slower puff a little later: the column keeps its body
            p.age = -rnd(0.05f, 0.15f);
            p.vz *= 0.7f;
            p.size1 *= 0.85f;
        }
        const int g = int(rnd(30, 48) + 36 * (1 - dmg));
        p.c0 = rgba(g, g - 2, g - 4, int(rnd(150, 185) + 45 * dmg));
        p.c1 = rgba(std::min(255, g + 62), std::min(255, g + 58), std::min(255, g + 54), 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.5f, 0.5f);
        add(p);
        }
        return;
    }
    // A smoking vehicle (or any other EX_SMOKE3): a smaller puff; nearly dead ones flare up now
    // and then (a lick of flame and a flicker of light, gone in a moment).
    for (const auto& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || u.smokeTimer == 0 || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            const float ux = float(u.posX) / 65536.0f, uy = float(u.posY) / 65536.0f;
            if (std::fabs(ux - x) > 8 || std::fabs(uy - y) > 6) continue;
            const auto& tp = t_.bin.units[u.type];
            hp = tp.hitPoints ? float(u.health) / float(tp.hitPoints) : 0.4f;
            break;
        }
    Particle p{};
    p.x = x + rnd(-1.5f, 1.5f);
    p.y = y + rnd(-1.5f, 1.5f);
    p.z = z + 5;
    p.vx = kWindX + rnd(-3, 3);
    p.vy = kWindY + rnd(-2, 2);
    p.vz = rnd(14, 24);
    p.drag = 0.4f;
    p.life = rnd(1.0f, 1.6f);
    p.size0 = rnd(5, 7);
    p.size1 = rnd(18, 26);
    p.c0 = rgba(40, 38, 36, int(rnd(165, 200)));
    p.c1 = rgba(92, 90, 86, 0);
    p.tex = kSmoke;
    p.blend = Blend::Alpha;
    p.rot = rnd(0, 2 * kPi);
    p.vrot = rnd(-0.8f, 0.8f);
    add(p);
    if (hp < 0.25f && rnd() < 0.2f) {
        for (int k = 0; k < 2; ++k) {
            Particle f = p;
            f.x += rnd(-2, 2);
            f.z = z + 3;
            f.tex = kFlame;
            f.blend = k == 0 ? Blend::Add : Blend::Alpha;
            f.vx = rnd(-3, 3);
            f.vy = rnd(-2, 2);
            f.vz = rnd(18, 30);
            f.drag = 0;
            f.life = rnd(0.22f, 0.36f);
            f.size0 = rnd(6, 8);
            f.size1 = rnd(3, 5);
            f.c0 = k == 0 ? rgba(255, 215, 130, 225) : rgba(255, 150, 60, 200);
            f.c1 = rgba(200, 60, 10, 0);
            f.vrot = rnd(-2, 2);
            add(f);
        }
        addFlash(x, y - z - 4, z + 4, 40, rgba(255, 140, 50, 150), 0.25f, 0.03f);
    }
}

// EX_FLAME / EX_FLAME2 at a building's BUILEXP.BIN flame point (every 8-12 ticks while the
// smoke timer runs): a lick of fire of varying size, now and then a brief flare-up.
void Effects::spawnFlame(const game::World& w, float x, float y, float z) {
    float hp = 0.3f, groundY = y + z;
    const bool onBuilding = smokingBuilding(w, t_, x, y - z, hp, groundY);
    const float gx = x, gy = onBuilding ? groundY : y, gz = onBuilding ? groundY - (y - z) : z;
    const bool flare = rnd() < 0.12f;
    const float s = flare ? rnd(1.3f, 1.6f) : rnd(0.65f, 1.15f);
    const int n = flare ? 6 : 3;
    for (int i = 0; i < n; ++i) {
        Particle p{};
        p.x = gx + rnd(-3, 3) * s;
        p.y = gy + rnd(-2, 2);
        p.z = gz + rnd(0, 2);
        p.vx = rnd(-8, 8);
        p.vy = rnd(-5, 5);
        p.vz = rnd(15, 35) * s;
        p.life = rnd(0.28f, 0.5f) * (flare ? 1.3f : 1.0f);
        p.size0 = rnd(7, 11) * s;
        p.size1 = rnd(3, 6) * s;
        p.c0 = i == 0 ? rgba(255, 222, 145, 225) : rgba(255, 160, 70, 200);
        p.c1 = rgba(170, 50, 10, 0);
        p.tex = kFlame;
        p.blend = i == 0 || (flare && i < 3) ? Blend::Add : Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-2, 2);
        add(p);
    }
    if (flare) {
        for (int i = 0; i < 3; ++i) {  // embers
            Particle e{};
            e.x = gx + rnd(-3, 3);
            e.y = gy;
            e.z = gz + 4;
            e.vx = kWindX + rnd(-10, 10);
            e.vy = rnd(-6, 6);
            e.vz = rnd(30, 60);
            e.gravity = -20;
            e.drag = 0.8f;
            e.life = rnd(0.6f, 1.1f);
            e.size0 = 2.6f;
            e.size1 = 1;
            e.c0 = rgba(255, 200, 110, 255);
            e.c1 = rgba(255, 80, 20, 0);
            e.tex = kSpark;
            e.blend = Blend::Add;
            add(e);
        }
    }
    addFlash(gx, gy - gz, gz, flare ? 70 : 44 * s, rgba(255, 140, 50, flare ? 170 : 110), flare ? 0.5f : 0.35f, 0.05f);
}

// EX_REF (refinery processing, one per spice blob a docked harvester unloads; economy.cpp
// updateUnloadingSpice, state 15: every harvestUnloadDelay = 60 ticks, ~3 s). The original's
// sprite is drawn at refinery pos + refineryExplosionOffset (62, -79): flames venting from the
// refinery's two chimney tops (the short one west, the tall one north-east) with a wisp of smoke.
// Modern: every event vents a burst of flame with its light and a few embers from both chimney
// tops (spawnRefineryVent), and keeps a vent record whose heat drives a rising, drifting column
// of brown industrial smoke (updateRefineryVents): it thickens while a harvester keeps unloading
// (one event every ~3 s keeps the heat up) and fades a few seconds after the last blob.
namespace {
// Chimney tops relative to the EX_REF point (drawn world px, measured from the original EX_REF
// sprite over the refinery art; the same for every house).
struct Chimney { float dx, dy, power; };
constexpr Chimney kChimneys[2] = {{-34.5f, -9.0f, 1.0f}, {8.0f, -37.0f, 0.85f}};
constexpr float kVentSouth = 79.0f;      // the refinery's south edge below the EX_REF point (-refineryExplosionOffsetY)
// A chimney top as a particle point: ground y and height z. 2D: any split with gy - z = the
// drawn y (the smokingBuilding convention: ground = south edge - 14). 3D (cardTilt >= 0): the
// building is an upright card on its south edge facing the camera, so a point d px up the art
// sits on that card: d cos(tilt) north of the edge, d sin(tilt) high (on screen exactly d up).
void ventPoint(float x, float y, const Chimney& c, float cardTilt, float& cx, float& gy, float& z) {
    cx = x + c.dx;
    const float south = y + kVentSouth, d = kVentSouth - c.dy;
    if (cardTilt < 0) {
        gy = south - 14;
        z = d - 14;
    } else {
        gy = south - d * std::cos(cardTilt);
        z = d * std::sin(cardTilt);
    }
}
// 3D with models: the refinery whose cells hold the EX_REF point (pos + refineryExplosionOffset
// lies inside its art box), for its model's flue anchors.
bool refineryAt(const game::World& w, float x, float y, int& side, int& bi) {
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& sd = w.sides()[std::size_t(s)];
        for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
            const game::Building& b = sd.buildings[std::size_t(i)];
            if (!b.alive) continue;
            const auto& t = w.buildingTemplate(b);
            if (t.specialBehavior != game::building_behaviour::kRefinery) continue;
            const float x0 = float(b.tileX * 32), y0 = float(b.tileY * 32);
            if (x >= x0 && y >= y0 && x < x0 + float(t.artWidth) && y < y0 + float(t.artHeight)) {
                side = s;
                bi = i;
                return true;
            }
        }
    }
    return false;
}
constexpr float kVentHold = 3.6f;        // s the heat holds after an event (> one unload step)
constexpr std::uint32_t kRefSmoke = rgba(76, 44, 22, 255);       // dark orange-brown, at the vent
constexpr std::uint32_t kRefSmokeHigh = rgba(150, 128, 106, 255); // dusty brown, high up
}  // namespace

int Effects::ventPoints(const game::World& w, const RefineryVent& v, VentPoint* out) const {
    int side = -1, bi = -1;
    if (anchors_ && refineryAt(w, v.x, v.y, side, bi) && anchors_->buildingAnchors(w, side, bi, "fx_flue", anchorTmp_) &&
        !anchorTmp_.empty()) {
        const int n = std::min(int(anchorTmp_.size()), kMaxVents);
        for (int i = 0; i < n; ++i) {
            const EffectAnchor& a = anchorTmp_[std::size_t(i)];
            out[i] = VentPoint{a.x, a.y, a.z, std::clamp(a.size, 0.2f, 1.5f)};
        }
        return n;
    }
    int n = 0;
    for (const Chimney& c : kChimneys) {
        VentPoint& p = out[n++];
        ventPoint(v.x, v.y, c, cardTilt_, p.x, p.y, p.z);
        p.power = c.power;
    }
    return n;
}

void Effects::spawnRefineryVent(const game::World& w, float x, float y) {
    RefineryVent* v = nullptr;
    for (RefineryVent& r : vents_)
        if (std::fabs(r.x - x) < 6 && std::fabs(r.y - y) < 6) v = &r;
    if (!v) {
        vents_.push_back(RefineryVent{});
        v = &vents_.back();
        v->x = x;
        v->y = y;
        v->seed = hash32(std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u);
    }
    v->heat = std::min(std::max(v->heat + 0.3f, 0.55f), 1.3f);
    v->since = 0;
    VentPoint pts[kMaxVents];
    const int np = ventPoints(w, *v, pts);
    for (int pi = 0; pi < np; ++pi) {
        const float cx = pts[pi].x, gy = pts[pi].y, z = pts[pi].z;
        const float power = pts[pi].power;
        const float top = gy - z;  // drawn y
        // The flame jet: a tongue of flame shooting up out of the pipe (wide and hot at the lip,
        // tapering as it rises, bending with the wind), fed for ~0.8 s like the original sprite's
        // animation, flaring hardest at the start.
        const int n = int(48 * power + 0.5f);
        for (int i = 0; i < n; ++i) {
            Particle p{};
            p.x = cx + rnd(-1.2f, 1.2f);
            p.y = gy + rnd(-0.8f, 0.8f);
            p.z = z + rnd(0, 2);
            p.vx = kWindX * 0.6f + rnd(-5, 5);
            p.vy = rnd(-2, 2);
            const float f = float(i) / float(n);  // 0 .. 1 over the jet's life
            p.vz = rnd(55, 85) * power * (1.15f - 0.5f * f);
            p.drag = 1.0f;
            p.age = -0.8f * f * std::sqrt(f);
            p.life = rnd(0.3f, 0.45f);
            p.size0 = rnd(7, 9) * power * (1.1f - 0.35f * f);
            p.size1 = rnd(5.0f, 6.5f) * power;
            const bool hot = i % 2 == 0;
            p.tex = hot ? kFire : kFlame;
            p.blend = Blend::Add;
            p.c0 = hot ? rgba(255, 205, 120, 230) : rgba(255, 135, 50, 210);
            p.c1 = hot ? rgba(255, 90, 20, 0) : rgba(150, 40, 10, 0);
            p.rot = rnd(0, 2 * kPi);
            p.vrot = rnd(-3, 3);
            p.fx = kFxHot;
            add(p);
        }
        // The hot core at the lip of the pipe.
        Particle g{};
        g.x = cx;
        g.y = gy;
        g.z = z + 1;
        g.tex = kGlow;
        g.blend = Blend::Add;
        g.life = 0.4f;
        g.size0 = 9 * power;
        g.size1 = 15 * power;
        g.c0 = rgba(255, 200, 120, 190);
        g.c1 = rgba(255, 110, 40, 0);
        add(g);
        // A few embers carried up and away, drifting down again.
        const int ne = 2 + int(rnd() * 2.5f * power);
        for (int i = 0; i < ne; ++i) {
            Particle e{};
            e.x = cx + rnd(-1.5f, 1.5f);
            e.y = gy + rnd(-1, 1);
            e.z = z + 3;
            e.vx = kWindX * 1.5f + rnd(-14, 14);
            e.vy = rnd(-6, 6);
            e.vz = rnd(40, 75);
            e.gravity = -45;
            e.drag = 0.9f;
            e.life = rnd(0.8f, 1.5f);
            e.size0 = rnd(1.6f, 2.4f);
            e.size1 = 0.8f;
            e.c0 = rgba(255, 210, 120, 255);
            e.c1 = rgba(255, 80, 20, 0);
            e.tex = kSpark;
            e.blend = Blend::Add;
            add(e);
        }
        // The burst's own smoke: a thick puff pushed out with the flame.
        for (int i = 0; i < 2; ++i) {
            Particle s{};
            s.x = cx + rnd(-1.5f, 1.5f);
            s.y = gy + rnd(-1, 1);
            s.z = z + 5 + rnd(0, 3);
            s.vx = kWindX * 1.3f + rnd(-3, 3);
            s.vy = kWindY + rnd(-2, 2);
            s.vz = rnd(22, 32);
            s.drag = 0.45f;
            s.age = -0.1f - 0.12f * float(i);
            s.life = rnd(2.2f, 3.0f);
            s.size0 = rnd(7, 9);
            s.size1 = rnd(28, 36);
            s.c0 = withAlpha(lerpColour(kRefSmoke, rgba(120, 66, 30, 255), 0.35f), 240);
            s.c1 = withAlpha(kRefSmokeHigh, 0);
            s.tex = kSmoke;
            s.blend = Blend::Alpha;
            s.rot = rnd(0, 2 * kPi);
            s.vrot = rnd(-0.5f, 0.5f);
            add(s);
        }
        addFlash(cx, top - 3, z + 3, 64 * power, rgba(255, 150, 60, 175), 0.55f, 0.04f);
    }
}

// The vents' smoke columns (and the glow of the hot pipes) between the EX_REF events.
void Effects::updateRefineryVents(const game::World& w, float dt, const Camera& cam, int viewW, int viewH,
                                  bool showShroud) {
    const float vx0 = cam.x - 96, vy0 = cam.y - 96;
    const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 160;
    for (std::size_t i = 0; i < vents_.size();) {
        RefineryVent& v = vents_[i];
        v.since += dt;
        // Holds while the harvester keeps unloading (an event every ~3 s), then dies down.
        v.heat -= dt * (v.since < kVentHold ? 0.05f : 0.32f);
        if (v.heat <= 0) {
            vents_[i] = vents_.back();
            vents_.pop_back();
            continue;
        }
        ++i;
        if (dt <= 0) continue;
        const int tx = int(v.x) >> 5, ty = int(v.y + 40) >> 5;
        if (v.x < vx0 || v.x > vx1 || v.y < vy0 || v.y > vy1) continue;
        if (showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1) continue;
        const float k = std::min(v.heat, 1.0f), lk = load();
        VentPoint pts[kMaxVents];
        const int np = ventPoints(w, v, pts);
        for (int ci = 0; ci < np; ++ci) {
            const float cx = pts[ci].x, gy = pts[ci].y, z = pts[ci].z;
            const float power = pts[ci].power;
            const float top = gy - z;  // drawn y
            // The smoke column: denser, bigger and darker the longer the unloading goes on.
            float& acc = v.smokeAcc[ci];
            acc += dt * (5.0f + 9.0f * k) * power * lk;
            while (acc >= 1) {
                acc -= 1;
                Particle s{};
                s.x = cx + rnd(-1.5f, 1.5f);
                s.y = gy + rnd(-1, 1);
                s.z = z + 1 + rnd(0, 2);
                s.vx = kWindX * 1.9f + rnd(-2.5f, 2.5f);
                s.vy = kWindY + rnd(-1.5f, 1.5f);
                s.vz = rnd(22, 32) * (0.85f + 0.3f * k);
                s.drag = 0.4f;
                s.life = rnd(2.4f, 3.4f) * (0.8f + 0.35f * k);
                s.age = 0.1f * s.life;  // past the generic smoke fade-in: the column starts at the mouth
                s.size0 = rnd(5, 7);
                s.size1 = rnd(18, 26) * (0.8f + 0.4f * k);
                s.c0 = withAlpha(lerpColour(kRefSmokeHigh, kRefSmoke, 0.45f + 0.55f * k), int(150 + 105 * k));
                s.c1 = withAlpha(kRefSmokeHigh, 0);
                if (rnd() < 0.4f) {  // body: holds its colour and thins out late (the column reads)
                    s.c0 = withAlpha(lerpColour(kRefSmokeHigh, kRefSmoke, 0.6f), int(110 + 70 * k));
                    s.fx = kFxLateFade;
                }
                s.tex = kSmoke;
                s.blend = Blend::Alpha;
                s.rot = rnd(0, 2 * kPi);
                s.vrot = rnd(-0.45f, 0.45f);
                add(s);
            }
            // Between the bursts the hot pipe licks a small flame now and then.
            const float hot = std::max(0.0f, 1 - v.since / 2.4f) * k;
            float& lick = v.lickAcc[ci];
            lick += dt * 3.5f * hot * power;
            while (lick >= 1) {
                lick -= 1;
                Particle p{};
                p.x = cx + rnd(-1, 1);
                p.y = gy;
                p.z = z + 1;
                p.vx = kWindX * 0.8f + rnd(-3, 3);
                p.vz = rnd(22, 38);
                p.drag = 1.2f;
                p.life = rnd(0.22f, 0.38f);
                p.size0 = rnd(3.5f, 5.0f);
                p.size1 = rnd(6, 8);
                p.tex = kFlame;
                p.blend = Blend::Add;
                p.c0 = rgba(255, 170, 80, 190);
                p.c1 = rgba(160, 50, 10, 0);
                p.rot = rnd(0, 2 * kPi);
                p.fx = kFxHot;
                add(p);
                if (rnd() < 0.35f) {  // a stray ember
                    Particle e{};
                    e.x = cx;
                    e.y = gy;
                    e.z = z + 3;
                    e.vx = kWindX * 1.5f + rnd(-10, 10);
                    e.vy = rnd(-4, 4);
                    e.vz = rnd(30, 55);
                    e.gravity = -40;
                    e.drag = 0.9f;
                    e.life = rnd(0.7f, 1.2f);
                    e.size0 = 1.8f;
                    e.size1 = 0.8f;
                    e.c0 = rgba(255, 200, 110, 240);
                    e.c1 = rgba(255, 70, 20, 0);
                    e.tex = kSpark;
                    e.blend = Blend::Add;
                    add(e);
                }
            }
            // The glow of the hot pipe mouth (flickers, cools with the heat).
            const float fl = 0.8f + 0.2f * std::sin(time_ * 17 + float(v.seed & 63) + 2.1f * float(ci));
            const int a = int(55 * hot * fl + 18 * k);
            if (a > 2) lights_.push_back({cx, top - 2, 46 * power, rgba(255, 130, 50, a), z + 2});
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Spice blooms (crate records of type 7..10: 7 seed (invisible) .. 10 ripe; economy.cpp
// updateCrates). The mound is drawn from the record every frame from stage 8 (bloomMarks); a
// stage step shows its EX_SPICEBLOOM (spawnBloomGrow); a bloom of stage 8..10 that leaves its record (recycled / freed) while spice
// shells start from its cell (SpiceMound) has burst (spawnBloomBurst). The shells fly as spice
// globs and land as spice splashes (their EX_TANKGUN hit is not drawn as a fireball).
void Effects::updateBlooms(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                           int viewH, bool showShroud, bool first) {
    const float vx0 = cam.x - 96, vy0 = cam.y - 96;
    const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 128;
    auto inView = [&](float x, float y) { return x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1; };
    auto shrouded = [&](float x, float y) {
        const int tx = int(x) >> 5, ty = int(y) >> 5;
        return showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1;
    };
    // Where the spice shells in flight come down (kept a moment after they land).
    for (std::size_t i = 0; i < spiceTargets_.size();) {
        if (spiceTargets_[i].until < time_) {
            spiceTargets_[i] = spiceTargets_.back();
            spiceTargets_.pop_back();
        } else {
            ++i;
        }
    }
    if (spiceWeapon_ >= 0)
        for (int s = 0; s < int(w.sides().size()); ++s) {
            const auto& side = w.sides()[s];
            for (int i = side.firstBullet; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.bullets[i];
                if (!b.alive || b.type != spiceWeapon_) continue;
                const int key = s * 1000 + i;
                SpiceTarget* t = nullptr;
                for (auto& o : spiceTargets_)
                    if (o.key == key) t = &o;
                if (!t) {
                    spiceTargets_.push_back({key, 0, 0, 0});
                    t = &spiceTargets_.back();
                }
                t->x = float(b.targetX);
                t->y = float(b.targetY);
                t->until = time_ + 0.6f;
            }
        }
    auto shellsFrom = [&](float x, float y) {
        if (spiceWeapon_ < 0) return false;
        for (const auto& side : w.sides())
            for (int i = side.firstBullet; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.bullets[i];
                if (b.alive && b.type == spiceWeapon_ && std::fabs(float(b.posX) / 65536.0f - x) < 8 &&
                    std::fabs(float(b.posY) / 65536.0f - y) < 8)
                    return true;
            }
        return false;
    };
    blooms_.clear();
    const auto& crates = w.crates();
    for (int i = 0; i < game::kMaxCrates; ++i) {
        const auto& c = crates[std::size_t(i)];
        BloomSeen& seen = bloomSeen_[std::size_t(i)];
        const bool bloom = c.active && replacesCrate(c.type);
        if (!first && seen.active && seen.type >= 8 &&
            (!bloom || c.type < seen.type || int(c.x) != seen.x || int(c.y) != seen.y)) {
            const float bx = float(seen.x * 32 + 16), by = float(seen.y * 32 + 16);
            if (shellsFrom(bx, by) && inView(bx, by) && !shrouded(bx, by)) spawnBloomBurst(w, atlas, bx, by);
        }
        seen = {bloom, int(c.type), int(c.x), int(c.y)};
        // Type 7 is the bloom's invisible seed: its crate art (image 4 = DATA.R16 frame 106) is a
        // single pixel. A burst bloom with regrowths left goes back to type 7 on the same cell
        // (RecycleCrate 0x44f110), as do map blooms at the start; the mound shows from stage 8.
        if (!bloom || c.type == 7) continue;
        const float x = float(c.x * 32 + 16), y = float(c.y * 32 + 16);
        if (!inView(x, y) || shrouded(x, y)) continue;
        const int stage = int(c.type) - 7;
        const std::uint32_t seed = hash32(std::uint32_t(i) * 7919u ^ std::uint32_t(c.x) * 131u ^ std::uint32_t(c.y) * 977u);
        blooms_.push_back({x, y, stage, c.timer, seed});
        const float pulse = bloomPulse(stage, c.timer, seed, time_);
        // Spice motes seeping from a growing mound; a ripe one glows warm from inside.
        if (dt > 0 && stage >= 1 && rnd() < dt * (0.9f * float(stage) + 2.5f * pulse * (stage == 3))) {
            Particle m{};
            const float r = 4 + 2.4f * float(stage);
            m.x = x + rnd(-r, r) * 0.6f;
            m.y = y + rnd(-r, r) * 0.5f;
            m.z = 2 + float(stage);
            m.vx = kWindX * 0.6f + rnd(-3, 3);
            m.vy = rnd(-2, 2);
            m.vz = rnd(6, 14);
            m.drag = 0.8f;
            m.life = rnd(1.0f, 1.8f);
            m.size0 = rnd(3, 5);
            m.size1 = rnd(9, 14);
            m.c0 = withAlpha(kSpiceLight, 90);
            m.c1 = withAlpha(kSpiceLight, 0);
            m.tex = kSmoke;
            m.blend = Blend::Alpha;
            m.rot = rnd(0, 2 * kPi);
            m.vrot = rnd(-0.6f, 0.6f);
            add(m);
        }
        if (stage == 3) lights_.push_back({x, y - 4, 44 + 10 * pulse, rgba(255, 150, 60, int(26 + 34 * pulse)), 3});
    }
}

// The mound: a dark ring of disturbed sand, a shadow on the south-east, the lit dome (kMound,
// spice coloured, grainy), a highlight; from stage 2 a hot vent glowing in time with the pulse.
void Effects::bloomMarks() {
    for (const BloomLook& b : blooms_) {
        const float p = bloomPulse(b.stage, b.timer, b.seed, time_);
        const float s = float(b.stage);
        float swell = 1 + (0.025f + 0.02f * s) * p;
        if (b.stage == 3 && b.timer < 300) swell += 0.08f * (1 - float(b.timer) / 300.0f) * p;
        const float R = (5.5f + 2.6f * s) * swell;
        const float rot = hashf(b.seed, 1) * 6.28f;
        marks_.push_back({b.x + 1, b.y + 2, R * 1.75f, R * 1.45f, rot, kScorch, Blend::Alpha,
                          withAlpha(kSpiceDark, int(55 + 18 * s))});
        marks_.push_back({b.x + R * 0.28f, b.y + R * 0.32f, R * 1.25f, R * 1.0f, 0, kGlow, Blend::Alpha, rgba(70, 30, 10, 150)});
        marks_.push_back({b.x, b.y, R, R * 0.86f, 0, kMound, Blend::Alpha,
                          lerpColour(kSpice, rgba(232, 128, 52, 255), 0.25f * p)});
        marks_.push_back({b.x - R * 0.3f, b.y - R * 0.34f, R * 0.45f, R * 0.36f, 0, kGlow, Blend::Alpha,
                          withAlpha(kSpiceLight, int(70 + 50 * p))});
        if (b.stage >= 2)
            marks_.push_back({b.x, b.y - R * 0.05f, R * 0.5f, R * 0.42f, 0, kGlow, Blend::Add,
                              rgba(255, 130, 45, int((22 + 26 * (s - 1)) * p))});
    }
}

// A stage step (EX_SPICEBLOOM at the cell's top centre; x, y = cell centre): the mound heaves -
// a puff of spice dust and a few grains, a faint ring of sand.
void Effects::spawnBloomGrow(const game::World& w, const TileAtlas& atlas, float x, float y) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y + 12);
    for (int i = 0; i < 6; ++i) {
        Particle g{};
        const float a = rnd(0, 2 * kPi), sp = rnd(10, 30);
        g.x = x + std::cos(a) * 3;
        g.y = y + std::sin(a) * 3;
        g.z = 3;
        g.vx = std::cos(a) * sp;
        g.vy = std::sin(a) * sp * 0.8f;
        g.vz = rnd(25, 50);
        g.gravity = -180;
        g.life = rnd(0.4f, 0.7f);
        g.size0 = g.size1 = rnd(1.6f, 2.6f);
        g.c0 = withAlpha(rnd() < 0.5f ? kSpiceDark : kSpice, 240);
        g.c1 = withAlpha(kSpice, 160);
        g.tex = kDebris;
        g.blend = Blend::Alpha;
        g.rot = rnd(0, 2 * kPi);
        g.vrot = rnd(-10, 10);
        g.fx = kFxLateFade;
        add(g);
    }
    for (int i = 0; i < 2; ++i) {
        Particle p{};
        p.x = x + rnd(-4, 4);
        p.y = y + rnd(-3, 3);
        p.z = 2;
        p.vx = kWindX * 0.5f + rnd(-6, 6);
        p.vy = rnd(-4, 4);
        p.vz = rnd(5, 12);
        p.drag = 1;
        p.life = rnd(1.0f, 1.5f);
        p.size0 = rnd(6, 9);
        p.size1 = rnd(18, 26);
        p.c0 = withAlpha(lerpColour(sand, kSpiceLight, 0.5f), 110);
        p.c1 = withAlpha(sand, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.6f, 0.6f);
        add(p);
    }
    addScorch(x, y, 54, 0.6f, 3, 0.6f, scaleRgb(sand, 0.9f));
}

// The burst (SpiceMound 0x44ef10: the ripe bloom fires its spice shells around): a flash, a
// geyser of spice shooting up and raining back, clumps flung out, a ring of dust racing over
// the sand, a low dust cloud, a fine golden haze settling over a few seconds, a dark stain.
void Effects::spawnBloomBurst(const game::World& w, const TileAtlas& atlas, float x, float y) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y + 20);
    const float ld = load();
    addFlash(x, y - 10, 10, 150, rgba(255, 170, 90, 170), 0.4f, 0.02f);
    Particle g{};
    g.x = x;
    g.y = y;
    g.z = 6;
    g.tex = kGlow;
    g.blend = Blend::Add;
    g.life = 0.3f;
    g.size0 = 34;
    g.size1 = 70;
    g.c0 = rgba(255, 190, 120, 130);
    g.c1 = rgba(255, 130, 50, 0);
    add(g);
    const int geyser = int(30 * ld);
    for (int i = 0; i < geyser; ++i) {  // the column
        Particle p{};
        const float a = rnd(0, 2 * kPi), sp = rnd(0, 22);
        p.x = x + std::cos(a) * 2;
        p.y = y + std::sin(a) * 2;
        p.z = rnd(0, 6);
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp * 0.75f;
        p.vz = rnd(90, 240);
        p.gravity = -210;
        p.drag = 0.7f;
        p.life = rnd(1.4f, 2.4f);
        p.size0 = rnd(6, 9);
        p.size1 = rnd(26, 40);
        const float k = rnd();
        p.c0 = withAlpha(lerpColour(kSpiceDark, kSpiceLight, k), int(rnd(205, 240)));
        p.c1 = withAlpha(lerpColour(kSpiceLight, sand, 0.4f), 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-1.5f, 1.5f);
        add(p);
    }
    const int clumps = int(22 * ld);
    for (int i = 0; i < clumps; ++i) {  // clumps of spice flung out, raining down
        Particle c{};
        const float a = rnd(0, 2 * kPi), sp = rnd(30, 120);
        c.x = x;
        c.y = y;
        c.z = rnd(4, 12);
        c.vx = std::cos(a) * sp;
        c.vy = std::sin(a) * sp * 0.75f;
        c.vz = rnd(80, 190);
        c.gravity = -330;
        c.drag = 0.3f;
        c.life = rnd(1.2f, 2.0f);
        c.size0 = c.size1 = rnd(2.2f, 4.2f);
        c.c0 = c.c1 = rnd() < 0.5f ? kSpiceDark : kSpice;
        c.tex = kDebris;
        c.blend = Blend::Alpha;
        c.rot = rnd(0, 2 * kPi);
        c.vrot = rnd(-12, 12);
        c.fx = kFxLateFade | (rnd() < 0.35f ? kFxLandDust : 0);
        add(c);
    }
    for (int i = 0; i < 12; ++i) {  // the low dust cloud rolling out
        Particle d{};
        const float a = float(i) / 12 * 2 * kPi + rnd(-0.2f, 0.2f), sp = rnd(30, 60);
        d.x = x + std::cos(a) * 6;
        d.y = y + std::sin(a) * 5;
        d.z = rnd(0, 3);
        d.vx = std::cos(a) * sp + kWindX * 0.5f;
        d.vy = std::sin(a) * sp * 0.75f;
        d.vz = rnd(2, 8);
        d.drag = 1.4f;
        d.life = rnd(2.0f, 3.0f);
        d.size0 = rnd(10, 14);
        d.size1 = rnd(36, 50);
        d.c0 = withAlpha(lerpColour(sand, kSpiceLight, 0.35f), 120);
        d.c1 = withAlpha(sand, 0);
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.rot = rnd(0, 2 * kPi);
        d.vrot = rnd(-0.6f, 0.6f);
        add(d);
    }
    const int haze = int(26 * ld);
    for (int i = 0; i < haze; ++i) {  // fine spice settling slowly (lit, golden)
        Particle m{};
        m.x = x + rnd(-30, 30);
        m.y = y + rnd(-24, 24);
        m.z = rnd(30, 80);
        m.vx = kWindX + rnd(-4, 4);
        m.vy = rnd(-3, 3);
        m.vz = -rnd(6, 14);
        m.drag = 0.4f;
        m.age = -rnd(0.3f, 0.9f);  // after the column has gone up
        m.life = rnd(2.4f, 3.8f);
        m.size0 = m.size1 = rnd(1.4f, 2.4f);
        m.c0 = rgba(255, 196, 120, 210);
        m.c1 = rgba(230, 150, 80, 0);
        m.tex = kSpark;
        m.blend = Blend::Alpha;
        m.fx = kFxLateFade;
        add(m);
    }
    addScorch(x, y, 190, 1.0f, 3, 1.0f, lerpColour(sand, kSpiceLight, 0.3f));
    addScorch(x, y, 110, 0.7f, 2, 0.35f);
    addScorch(x, y, 54, 9.0f, 4, 1.0f, kSpiceDark);
}

bool Effects::nearSpiceTarget(float x, float y) const {
    for (const auto& t : spiceTargets_)
        if (std::fabs(t.x - x) < 24 && std::fabs(t.y - y) < 24) return true;
    return false;
}

// A spice shell comes down: a splash of spice grains, a puff of spice dust, a small ring, a
// stain that soaks in (the cell's spice itself is the simulation's).
void Effects::spawnSpiceSplash(const game::World& w, const TileAtlas& atlas, float x, float y) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y);
    for (int i = 0; i < 8; ++i) {
        Particle g{};
        const float a = rnd(0, 2 * kPi), sp = rnd(18, 60);
        g.x = x;
        g.y = y;
        g.z = 2;
        g.vx = std::cos(a) * sp;
        g.vy = std::sin(a) * sp * 0.75f;
        g.vz = rnd(30, 80);
        g.gravity = -260;
        g.life = rnd(0.5f, 0.9f);
        g.size0 = g.size1 = rnd(1.6f, 3.0f);
        g.c0 = g.c1 = rnd() < 0.5f ? kSpiceDark : kSpice;
        g.tex = kDebris;
        g.blend = Blend::Alpha;
        g.rot = rnd(0, 2 * kPi);
        g.vrot = rnd(-10, 10);
        g.fx = kFxLateFade;
        add(g);
    }
    for (int i = 0; i < 2; ++i) {
        Particle p{};
        p.x = x + rnd(-3, 3);
        p.y = y + rnd(-2, 2);
        p.z = 1;
        p.vx = kWindX * 0.5f + rnd(-8, 8);
        p.vy = rnd(-5, 5);
        p.vz = rnd(6, 16);
        p.drag = 1.1f;
        p.life = rnd(1.0f, 1.6f);
        p.size0 = rnd(6, 9);
        p.size1 = rnd(20, 28);
        p.c0 = withAlpha(lerpColour(kSpice, sand, 0.35f), 140);
        p.c1 = withAlpha(lerpColour(kSpiceLight, sand, 0.5f), 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-0.7f, 0.7f);
        add(p);
    }
    addScorch(x, y, 44, 0.5f, 3, 0.6f, lerpColour(sand, kSpiceLight, 0.3f));
    addScorch(x, y, rnd(18, 26), 6.0f, 4, 0.8f, kSpiceDark);
}

// ---------------------------------------------------------------------------------------------
// Capture: the building changes owner (the original just swaps the record): a soft glow in the
// new owner's house colour over the footprint, a thin ring and motes rising - no blast, no shake.
void Effects::spawnCaptureFlash(const game::World& w, int tileX, int tileY, int type, int fromSide) {
    const auto& tp = t_.bin.buildings[type];
    const std::uint32_t mask = tp.tilesOccupiedSolid ? tp.tilesOccupiedSolid : (tp.tilesOccupiedAll ? tp.tilesOccupiedAll : 1u);
    int minX = 3, minY = 3, maxX = 0, maxY = 0, cells = 0, fx = 0, fy = 0;
    for (int c = 0; c < 16; ++c)
        if ((mask >> c) & 1) {
            if (cells++ == 0) {
                fx = c % 4;
                fy = c / 4;
            }
            minX = std::min(minX, c % 4);
            maxX = std::max(maxX, c % 4);
            minY = std::min(minY, c / 4);
            maxY = std::max(maxY, c / 4);
        }
    int house = -1, newSide = -1, newIndex = -1;
    if (w.buildingAt(tileX + fx, tileY + fy, newSide, newIndex) && newSide >= 0 && newSide < int(w.sides().size()) &&
        newSide != fromSide)
        house = w.sides()[std::size_t(newSide)].house;
    const std::uint32_t glow = gasGlow(house), body = gasBody(house);
    const float x0 = float((tileX + minX) * 32), y0 = float((tileY + minY) * 32);
    const float x1 = float((tileX + maxX + 1) * 32), y1 = float((tileY + maxY + 1) * 32);
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, R = std::max(x1 - x0, y1 - y0) * 0.5f;
    const float h = float(tp.artHeight) * 0.3f;
    Particle g{};
    g.x = cx;
    g.y = cy;
    g.z = h;
    g.tex = kGlow;
    g.blend = Blend::Add;
    g.life = 0.55f;
    g.size0 = R * 1.8f;
    g.size1 = R * 2.7f;
    g.c0 = withAlpha(glow, 95);
    g.c1 = withAlpha(body, 0);
    add(g);
    addScorch(cx, cy, R * 4 + 40, 0.75f, 3, 0.9f, glow);
    const int motes = 10 + cells * 2;
    for (int i = 0; i < motes; ++i) {
        Particle m{};
        m.x = rnd(x0 + 4, x1 - 4);
        m.y = rnd(y0 + 4, y1 - 2);
        m.z = rnd(2, h + 6);
        m.vx = rnd(-4, 4);
        m.vy = rnd(-3, 3);
        m.vz = rnd(18, 42);
        m.drag = 0.9f;
        m.age = -rnd(0, 0.25f);
        m.life = rnd(0.8f, 1.4f);
        m.size0 = rnd(2.4f, 3.4f);
        m.size1 = 1.1f;
        m.c0 = withAlpha(glow, 235);
        m.c1 = withAlpha(body, 0);
        m.tex = kSpark;
        m.blend = Blend::Add;
        add(m);
    }
    addFlash(cx, cy - h, h, R * 4 + 60, withAlpha(glow, 150), 0.6f, 0.08f);
}

}  // namespace d2k::render

namespace d2k::render {

// Weld sparks (render/weld_fx.hpp): while a redesigned building works, short bright sparks spray
// from the anchor its arc is on this tick and fall (the models' / rendered pack's fx_weld anchors;
// buildings without them keep the original overlay and get none).
void Effects::updateWeldSparks(const game::World& w, float dt, const Camera& cam, int viewW, int viewH, bool showShroud) {
    if (!anchors_ || dt <= 0) return;
    const float vx0 = cam.x - 96, vy0 = cam.y - 96;
    const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 160;
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& sd = w.sides()[std::size_t(s)];
        for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
            const game::Building& b = sd.buildings[std::size_t(i)];
            if (!b.alive || !(b.flags & 0x30) || (b.flags & 0x6)) continue;  // only working buildings
            const float bx = float(b.tileX * 32), by = float(b.tileY * 32);
            if (bx + 128 < vx0 || bx > vx1 || by + 128 < vy0 || by > vy1) continue;
            if (showShroud && w.inBounds(b.tileX + 1, b.tileY + 1) && (w.cell(b.tileX + 1, b.tileY + 1).shroud & 0xF) == 1)
                continue;
            if (!anchors_->buildingAnchors(w, s, i, "fx_weld", anchorTmp_) || anchorTmp_.empty()) continue;
            WeldFlash fl;
            if (!weldFlash(w, b, t_, int(anchorTmp_.size()), fl)) continue;
            const EffectAnchor& an = anchorTmp_[std::size_t(fl.anchor)];
            // About 30 sparks a second at full arc, in little bursts.
            const float want = dt * 30.0f * fl.a * load();
            int n = int(want);
            if (rnd() < want - float(n)) ++n;
            for (int k = 0; k < n; ++k) {
                Particle p{};
                const float a = rnd(0, 6.2831853f), sp = rnd(18, 55) * std::clamp(an.size, 0.5f, 1.5f);
                p.x = an.x + rnd(-0.8f, 0.8f);
                p.y = an.y + rnd(-0.8f, 0.8f);
                p.z = an.z;
                p.vx = std::cos(a) * sp;
                p.vy = std::sin(a) * sp * 0.7f;
                p.vz = rnd(5, 35);
                p.gravity = -260;
                p.drag = 1.6f;
                p.life = rnd(0.12f, 0.3f);
                p.size0 = rnd(1.2f, 1.8f);
                p.size1 = 0.6f;
                p.c0 = rgba(225, 240, 255, 255);
                p.c1 = rgba(255, 170, 70, 0);
                p.tex = kSpark;
                p.blend = Blend::Add;
                p.streak = true;
                add(p);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Sandworms. The original shows a worm only through its effects: while it travels (state 27, not
// cloaked) Worm_SpawnSandParticles drops 8 one-pixel specks of the tile colour per tick round it
// (World::particles, the "grey dots"), now and then (rand % 80) a wormsign burst EX_WORMSN1..6 at
// it, and when it swallows a vehicle its maw EX_WORM with the dust EX_WORMDUST. Modern (the
// renderers skip the particles, the wormsign and dust sprites): the worm's own sand - a hump of sand
// heaving along over it, furrows of pushed-aside sand settling behind, ripples, dust and grains -
// a sand burst for each wormsign, and an eruption of sand, the victim's debris and a sand-coloured
// shockwave round the maw (whose art stays: it is the worm itself). All timed by the simulation's
// worm (its position and state) and its explosions; nothing is written back.
void Effects::updateWorms(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                          bool showShroud) {
    const float vx0 = cam.x - 96, vy0 = cam.y - 96;
    const float vx1 = cam.x + viewW / cam.zoom + 96, vy1 = cam.y + viewH / cam.zoom + 128;
    for (WormLook& k : worms_) k.seen = false;
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& side = w.sides()[std::size_t(s)];
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[std::size_t(i)];
            if (!u.alive || u.type < 0 || u.type >= formats::kMaxUnitTypes) continue;
            if (t_.bin.units[u.type].specialBehavior != game::behaviour::kSandworm) continue;
            WormLook* k = nullptr;
            for (WormLook& o : worms_)
                if (o.side == s && o.index == i) k = &o;
            const float x = float(u.posX) / 65536.0f, y = float(u.posY) / 65536.0f;
            if (!k) {
                worms_.push_back(WormLook{});
                k = &worms_.back();
                k->side = s;
                k->index = i;
                k->x = x;
                k->y = y;
                k->seed = hash32(std::uint32_t(s) * 977u + std::uint32_t(i) * 7919u + 1u);
            }
            k->seen = true;
            const int tx = int(x) >> 5, ty = int(y) >> 5;
            k->visible = x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1 &&
                         !(showShroud && w.inBounds(tx, ty) && (w.cell(tx, ty).shroud & 0xF) == 1);
            const float mx = x - k->x, my = y - k->y, d = std::hypot(mx, my);
            const bool stepped = d > 0.01f && d < 24;
            // Travelling under the sand where the original drops its particles: state 27, not cloaked.
            const bool travel = u.state == game::UnitState::Wandering && !(u.flags & game::unit_flags::kCloaked);
            if (stepped) k->heading = std::atan2(my, mx);
            k->x = x;
            k->y = y;
            if (dt <= 0) continue;
            k->moving += ((travel ? 1.0f : 0.0f) - k->moving) * std::min(1.0f, dt * (travel ? 5.0f : 1.6f));
            // A swallow (state 28 with the 50-tick countdown): the sand churns over it for a while.
            if (u.state == game::UnitState::Eating && u.animDelay > 40 && k->churn <= 0) k->churn = 2.6f;
            k->churn = std::max(0.0f, k->churn - dt);
            if (!k->visible) continue;
            k->sand = terrainColour(w, atlas, x, y);
            const std::uint32_t sand = k->sand, light = lerpColour(sand, rgba(255, 242, 214, 255), 0.35f), dark = scaleRgb(sand, 0.82f);
            const std::uint32_t dust = lerpColour(sand, rgba(250, 226, 184, 255), 0.4f);  // dust in the air reads paler
            const float ld = load();
            const float hx = std::cos(k->heading), hy = std::sin(k->heading);
            if (travel && stepped) {
                // Furrows of sand pushed aside, every ~6 px of travel.
                k->trailAcc += d;
                while (k->trailAcc >= 6) {
                    k->trailAcc -= 6;
                    addScorch(x - hx * 9, y - hy * 7, rnd(22, 28), rnd(4.0f, 5.0f), 5, 1.0f, sand);
                    if (!scorch_.empty()) scorch_.back().rot = k->heading;
                }
            }
            if (travel) {  // ripples running out from the hump
                k->rippleAcc += dt;
                if (k->rippleAcc > 0.45f) {
                    k->rippleAcc = rnd(0, 0.12f);
                    addScorch(x + hx * 4, y + hy * 3, rnd(46, 64), 1.3f, 7, 0.45f, sand);
                }
            }
            // Dust and grains while it travels (and churning where it ate).
            const float rate = travel ? 1.0f : k->churn > 0 ? 0.7f * std::min(1.0f, k->churn) : 0.0f;
            if (rate <= 0) continue;
            k->dustAcc += dt * 9 * rate * ld;
            while (k->dustAcc >= 1) {
                k->dustAcc -= 1;
                Particle p{};
                const float a = rnd(0, 2 * kPi), r = rnd(4, 12);
                p.x = x + std::cos(a) * r - hx * rnd(0, 10);
                p.y = y + std::sin(a) * r * 0.8f - hy * rnd(0, 8);
                p.z = rnd(0, 3);
                p.vx = std::cos(a) * rnd(4, 12) + kWindX * 0.6f;
                p.vy = std::sin(a) * rnd(3, 8);
                p.vz = rnd(4, 12);
                p.drag = 1.1f;
                p.life = rnd(1.1f, 1.9f);
                p.size0 = rnd(6, 9);
                p.size1 = rnd(20, 30);
                p.c0 = withAlpha(lerpColour(dust, light, rnd(0, 0.5f)), int(rnd(70, 110)));
                p.c1 = withAlpha(dust, 0);
                p.tex = kSmoke;
                p.blend = Blend::Alpha;
                p.rot = rnd(0, 2 * kPi);
                p.vrot = rnd(-0.7f, 0.7f);
                add(p);
            }
            k->sprayAcc += dt * 16 * rate * ld;
            while (k->sprayAcc >= 1) {
                k->sprayAcc -= 1;
                Particle g{};
                const float a = rnd(0, 2 * kPi), sp = rnd(10, 34);
                g.x = x + std::cos(a) * rnd(2, 9);
                g.y = y + std::sin(a) * rnd(2, 7);
                g.z = rnd(1, 4);
                g.vx = std::cos(a) * sp + hx * 8;
                g.vy = std::sin(a) * sp * 0.75f + hy * 6;
                g.vz = rnd(30, 75);
                g.gravity = -220;
                g.drag = 0.4f;
                g.life = rnd(0.45f, 0.8f);
                g.size0 = g.size1 = rnd(1.1f, 2.1f);
                g.c0 = g.c1 = withAlpha(rnd() < 0.5f ? dark : light, 235);
                g.tex = kDebris;
                g.blend = Blend::Alpha;
                g.rot = rnd(0, 2 * kPi);
                g.vrot = rnd(-10, 10);
                g.fx = kFxLateFade;
                add(g);
            }
        }
    }
    for (std::size_t i = 0; i < worms_.size();) {
        if (!worms_[i].seen) {
            worms_[i] = worms_.back();
            worms_.pop_back();
        } else {
            ++i;
        }
    }
}

// The hump of sand over a travelling worm: a soft dark ring of disturbed sand, the shadow on the
// south-east, a long lit dome heaving with a slow swell, and a bow wave of sand ahead of it.
void Effects::wormMarks() {
    for (const WormLook& k : worms_) {
        if (!k.visible || k.moving < 0.02f || !k.sand) continue;
        const float m = k.moving;
        const float swell = 1 + 0.08f * std::sin(time_ * 5.3f + float(k.seed & 255));
        const float L = 15 * swell, W = 9 * swell;
        const float hx = std::cos(k.heading), hy = std::sin(k.heading);
        marks_.push_back({k.x, k.y, L * 1.6f, W * 1.7f, k.heading, kScorch, Blend::Alpha,
                          withAlpha(scaleRgb(k.sand, 0.7f), int(55 * m))});
        marks_.push_back({k.x + 3, k.y + 3, L * 1.1f, W * 1.05f, k.heading, kGlow, Blend::Alpha, rgba(70, 46, 24, int(80 * m))});
        const std::uint32_t hi = rgba(255, 242, 214, 255);  // the dome texture shades it: tint towards a warm highlight
        marks_.push_back({k.x, k.y, L, W, k.heading, kMound, Blend::Alpha, withAlpha(lerpColour(k.sand, hi, 0.55f), int(235 * m))});
        marks_.push_back({k.x - 3, k.y - 3, L * 0.45f, W * 0.4f, k.heading, kGlow, Blend::Alpha, withAlpha(lerpColour(k.sand, hi, 0.7f), int(70 * m))});
        // Bow wave: a crescent of sand piling up ahead.
        marks_.push_back({k.x + hx * L * 0.85f, k.y + hy * L * 0.7f, W * 1.1f, W * 0.8f, k.heading, kArc, Blend::Alpha,
                          withAlpha(lerpColour(k.sand, hi, 0.4f), int(120 * m))});
    }
}

// Wormsign (EX_WORMSN1..6, rand % 80 per tick while the worm travels): the sand over it bursts - a
// spray of sand and grains thrown up, a puff of dust, a ring of sand heaved out.
void Effects::spawnWormSign(const game::World& w, const TileAtlas& atlas, float x, float y) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y);
    const std::uint32_t light = lerpColour(sand, rgba(255, 242, 214, 255), 0.35f), dark = scaleRgb(sand, 0.82f);
    const float ld = load();
    for (int i = 0; i < int(10 * ld); ++i) {  // the spray
        Particle p{};
        const float a = rnd(0, 2 * kPi), sp = rnd(8, 30);
        p.x = x + std::cos(a) * 3;
        p.y = y + std::sin(a) * 2;
        p.z = rnd(0, 4);
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp * 0.7f;
        p.vz = rnd(60, 135);
        p.gravity = -230;
        p.drag = 0.8f;
        p.life = rnd(0.8f, 1.3f);
        p.size0 = rnd(4, 7);
        p.size1 = rnd(14, 22);
        p.c0 = withAlpha(lerpColour(sand, light, rnd()), int(rnd(190, 230)));
        p.c1 = withAlpha(sand, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-1.5f, 1.5f);
        add(p);
    }
    for (int i = 0; i < int(16 * ld); ++i) {  // grains
        Particle g{};
        const float a = rnd(0, 2 * kPi), sp = rnd(20, 70);
        g.x = x;
        g.y = y;
        g.z = rnd(2, 6);
        g.vx = std::cos(a) * sp;
        g.vy = std::sin(a) * sp * 0.75f;
        g.vz = rnd(60, 130);
        g.gravity = -300;
        g.drag = 0.3f;
        g.life = rnd(0.7f, 1.1f);
        g.size0 = g.size1 = rnd(1.3f, 2.6f);
        g.c0 = g.c1 = withAlpha(rnd() < 0.5f ? dark : light, 240);
        g.tex = kDebris;
        g.blend = Blend::Alpha;
        g.rot = rnd(0, 2 * kPi);
        g.vrot = rnd(-12, 12);
        g.fx = kFxLateFade;
        add(g);
    }
    for (int i = 0; i < 4; ++i) {  // dust drifting off
        Particle d{};
        d.x = x + rnd(-8, 8);
        d.y = y + rnd(-6, 6);
        d.z = rnd(1, 5);
        d.vx = rnd(-8, 8) + kWindX;
        d.vy = rnd(-5, 5);
        d.vz = rnd(4, 12);
        d.drag = 1;
        d.life = rnd(1.4f, 2.2f);
        d.size0 = rnd(9, 13);
        d.size1 = rnd(28, 38);
        d.c0 = withAlpha(lerpColour(sand, light, 0.3f), 110);
        d.c1 = withAlpha(sand, 0);
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.rot = rnd(0, 2 * kPi);
        d.vrot = rnd(-0.6f, 0.6f);
        add(d);
    }
    addScorch(x, y, 76, 0.9f, 7, 0.8f, sand);
}

// The swallow (EX_WORM at the victim's cell; the maw art plays over it): the sand erupts round the
// maw - a column of sand, clumps and the victim's broken metal flung out and raining down, a
// sand-coloured shockwave and a dust ring along the ground, a small kick of the camera, and a
// churned crater that the wind smooths over in ~12 s. No fire and no light: nothing burns.
void Effects::spawnWormStrike(const game::World& w, const TileAtlas& atlas, float x, float y, const Camera& cam, int viewW,
                              int viewH) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y);
    const std::uint32_t light = lerpColour(sand, rgba(255, 242, 214, 255), 0.35f), dark = scaleRgb(sand, 0.82f);
    const std::uint32_t dust = lerpColour(sand, rgba(250, 226, 184, 255), 0.4f);
    const float ld = load();
    for (int i = 0; i < int(28 * ld); ++i) {  // the column of sand round the maw
        Particle p{};
        const float a = rnd(0, 2 * kPi), r = rnd(8, 16), sp = rnd(6, 30);
        p.x = x + std::cos(a) * r;
        p.y = y + std::sin(a) * r * 0.8f;
        p.z = rnd(0, 6);
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp * 0.75f;
        p.vz = rnd(70, 200);
        p.gravity = -200;
        p.drag = 0.7f;
        p.age = -rnd(0, 0.15f);
        p.life = rnd(1.2f, 2.1f);
        p.size0 = rnd(6, 10);
        p.size1 = rnd(24, 38);
        p.c0 = withAlpha(lerpColour(dark, light, rnd()), int(rnd(200, 240)));
        p.c1 = withAlpha(dust, 0);
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        p.rot = rnd(0, 2 * kPi);
        p.vrot = rnd(-1.5f, 1.5f);
        add(p);
    }
    for (int i = 0; i < int(30 * ld); ++i) {  // clumps of sand flung out
        Particle c{};
        const float a = rnd(0, 2 * kPi), sp = rnd(30, 110);
        c.x = x + std::cos(a) * 8;
        c.y = y + std::sin(a) * 6;
        c.z = rnd(4, 14);
        c.vx = std::cos(a) * sp;
        c.vy = std::sin(a) * sp * 0.75f;
        c.vz = rnd(90, 200);
        c.gravity = -320;
        c.drag = 0.3f;
        c.life = rnd(1.1f, 1.9f);
        c.size0 = c.size1 = rnd(1.5f, 3.0f);
        c.c0 = c.c1 = rnd() < 0.5f ? dark : light;
        c.tex = kDebris;
        c.blend = Blend::Alpha;
        c.rot = rnd(0, 2 * kPi);
        c.vrot = rnd(-12, 12);
        c.fx = kFxLateFade | (rnd() < 0.3f ? kFxLandDust : 0);
        add(c);
    }
    for (int i = 0; i < 7; ++i) {  // the victim's metal, torn off and flung
        Particle m{};
        const float a = rnd(0, 2 * kPi), sp = rnd(25, 80);
        m.x = x;
        m.y = y;
        m.z = rnd(6, 16);
        m.vx = std::cos(a) * sp;
        m.vy = std::sin(a) * sp * 0.75f;
        m.vz = rnd(110, 210);
        m.gravity = -340;
        m.drag = 0.25f;
        m.life = rnd(1.4f, 2.2f);
        m.size0 = m.size1 = rnd(2.4f, 4.2f);
        m.c0 = m.c1 = rnd() < 0.5f ? rgba(70, 68, 66, 255) : rgba(104, 98, 90, 255);
        m.tex = kDebris;
        m.blend = Blend::Alpha;
        m.rot = rnd(0, 2 * kPi);
        m.vrot = rnd(-14, 14);
        m.fx = kFxLateFade | kFxLandDust;
        add(m);
    }
    for (int i = 0; i < 12; ++i) {  // the low dust cloud rolling out
        Particle d{};
        const float a = float(i) / 12 * 2 * kPi + rnd(-0.2f, 0.2f), sp = rnd(28, 55);
        d.x = x + std::cos(a) * 10;
        d.y = y + std::sin(a) * 8;
        d.z = rnd(0, 3);
        d.vx = std::cos(a) * sp + kWindX * 0.5f;
        d.vy = std::sin(a) * sp * 0.75f;
        d.vz = rnd(2, 8);
        d.drag = 1.4f;
        d.life = rnd(1.8f, 2.8f);
        d.size0 = rnd(10, 14);
        d.size1 = rnd(34, 48);
        d.c0 = withAlpha(dust, 85);
        d.c1 = withAlpha(dust, 0);
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.rot = rnd(0, 2 * kPi);
        d.vrot = rnd(-0.6f, 0.6f);
        add(d);
    }
    addScorch(x, y, 170, 0.95f, 7, 1.0f, sand);
    addScorch(x, y, 230, 1.5f, 3, 0.8f, lerpColour(sand, light, 0.4f));
    addScorch(x, y, 64, 12.0f, 6, 1.0f, sand);
    if (!scorch_.empty()) scorch_.back().rot = rnd(0, 2 * kPi);
    addTrauma(x, y, 0.22f, cam, viewW, viewH);
}

// EX_WORMDUST (with the maw): the dust of the swallow hanging over the spot and settling - slow
// billows of sand dust released over a second, fine grains drifting down.
void Effects::spawnWormDust(const game::World& w, const TileAtlas& atlas, float x, float y) {
    const std::uint32_t sand = terrainColour(w, atlas, x, y);
    const std::uint32_t light = scaleRgb(sand, 1.12f);
    const float ld = load();
    for (int i = 0; i < int(12 * ld); ++i) {
        Particle d{};
        const float a = rnd(0, 2 * kPi), r = rnd(4, 20);
        d.x = x + std::cos(a) * r;
        d.y = y + std::sin(a) * r * 0.8f;
        d.z = rnd(4, 18);
        d.vx = std::cos(a) * rnd(4, 14) + kWindX;
        d.vy = std::sin(a) * rnd(3, 9) + kWindY;
        d.vz = rnd(5, 16);
        d.drag = 0.9f;
        d.age = -rnd(0.1f, 1.0f);
        d.life = rnd(2.0f, 3.2f);
        d.size0 = rnd(12, 18);
        d.size1 = rnd(38, 56);
        d.c0 = withAlpha(lerpColour(sand, light, rnd(0, 0.6f)), int(rnd(80, 115)));
        d.c1 = withAlpha(sand, 0);
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.rot = rnd(0, 2 * kPi);
        d.vrot = rnd(-0.5f, 0.5f);
        add(d);
    }
    for (int i = 0; i < int(18 * ld); ++i) {
        Particle m{};
        m.x = x + rnd(-24, 24);
        m.y = y + rnd(-18, 18);
        m.z = rnd(20, 60);
        m.vx = kWindX + rnd(-4, 4);
        m.vy = rnd(-3, 3);
        m.vz = -rnd(6, 14);
        m.drag = 0.4f;
        m.age = -rnd(0.3f, 1.0f);
        m.life = rnd(2.0f, 3.2f);
        m.size0 = m.size1 = rnd(1.2f, 2.0f);
        m.c0 = withAlpha(light, 200);
        m.c1 = withAlpha(sand, 0);
        m.tex = kSpark;
        m.blend = Blend::Alpha;
        m.fx = kFxLateFade;
        add(m);
    }
}

}  // namespace d2k::render
