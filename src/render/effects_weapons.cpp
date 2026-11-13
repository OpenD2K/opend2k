// Modern weapon effects (render/effects.hpp): what replaces the original projectile, muzzle
// flash and explosion sprites with Effects: Modern - muzzle flashes, tracers, projectiles in
// flight, impacts, layered explosions (flash + light, fireball, smoke column, sparks, debris,
// ground dust ring, scorch, shockwave), burning vehicle wrecks, and the timed lights and ground
// marks they leave. Render only: reads the World, never changes it (docs/enhanced-graphics.md
// "Modern weapon effects").

#include "render/effects.hpp"

#include "render/effects_internal.hpp"
#include "render/map_renderer.hpp"
#include "render/tileset.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

using namespace fx;

namespace {
constexpr std::size_t kMaxFlashes = 256;
constexpr std::size_t kMaxScorch = 420;
}  // namespace

float Effects::load() const {
    const std::size_t n = particles_.size();
    if (n > kMaxParticles * 2 / 3) return 0.45f;
    if (n > kMaxParticles * 2 / 5) return 0.7f;
    return 1.0f;
}

void Effects::addFlash(float x, float y, float z, float size, std::uint32_t c, float life, float attack) {
    if (flashes_.size() >= kMaxFlashes) {  // full: drop the oldest-looking one (cheap: the first)
        flashes_.erase(flashes_.begin());
    }
    flashes_.push_back({x, y, z, size, c, 0, std::max(0.02f, life), attack});
}

void Effects::addScorch(float x, float y, float size, float life, std::uint8_t kind, float strength, std::uint32_t c) {
    if (scorch_.size() >= kMaxScorch) {
        // Full: the oldest long-lived mark goes (marks are appended in time order).
        for (std::size_t i = 0; i < scorch_.size(); ++i)
            if (scorch_[i].kind <= 1) {
                scorch_.erase(scorch_.begin() + std::ptrdiff_t(i));
                break;
            }
        if (scorch_.size() >= kMaxScorch) return;
    }
    Scorch s{};
    s.x = x;
    s.y = y;
    s.size = size;
    s.rot = rnd(0, 2 * kPi);
    s.age = 0;
    s.life = life;
    s.kind = kind;
    s.seed = rng_ ^ hash32(std::uint32_t(x * 7 + y * 131));
    s.strength = strength;
    s.c = c;
    scorch_.push_back(s);
}

void Effects::spawnExplosion(ExClass c, float x, float y, float z, float age, const Camera& cam, int viewW, int viewH,
                             const TileAtlas& atlas, const game::World& w, bool crowded, int house, float sizeMul,
                             float shakeMul) {
    const std::size_t first = particles_.size();
    auto P = [&](Tex tex, Blend bl) {
        Particle p{};
        p.x = x;
        p.y = y;
        p.z = z;
        p.tex = tex;
        p.blend = bl;
        p.rot = rnd(0, 2 * kPi);
        return p;
    };
    const bool ground = z < 8;
    const float ld = load();
    float s = 1.0f, shake = 0;
    switch (c) {
    case ExClass::Huge: s = 1.5f; shake = 0.5f; break;
    case ExClass::Big: s = 1.15f; shake = 0.22f; break;
    case ExClass::Medium: s = 0.62f; shake = 0.04f; break;
    default: break;
    }
    s *= sizeMul;
    shake *= shakeMul * std::min(1.6f, sizeMul);
    if (c == ExClass::Huge || c == ExClass::Big || c == ExClass::Medium) {
        // Crowded: another big burst started next to this one in the same frame (a building's
        // death spawns 2 explosions per solid tile). Stacking every flash would white out the
        // spot, so only the first one flashes and the rest are thinner.
        const float n = (crowded ? 0.55f : 1.0f) * ld;
        // Big blasts get more pieces rather than ever bigger ones (a few huge soft particles
        // average into a blurry ball): sizes grow ~s^0.6 above 1, counts and reach with s.
        const float ss = s <= 1 ? s : std::pow(s, 0.6f);
        const float cs = s <= 1 ? 1.0f : s;
        const float rs = std::sqrt(s);
        // 1. Flash: a white-hot core for a few frames and a light on the surroundings.
        if (!crowded) {
            Particle q = P(kGlow, Blend::Add);
            q.life = 0.08f + 0.04f * s;
            q.size0 = 14 * ss + 6;
            q.size1 = 40 * ss + 8;
            q.c0 = rgba(255, 246, 220, 210);
            q.c1 = rgba(255, 170, 90, 0);
            add(q);
            addFlash(x, y - z, z, 120 * s + 40, rgba(255, 150, 70, std::min(255, int(170 + 40 * s))), 0.25f + 0.25f * s);
        }
        // 2. Fireball: billowing puffs thrown out and rising - white-yellow at the core, orange
        // flames (alpha: they read on bright sand), soot already forming at the edges; hot
        // additive puffs carry the glow. Each cools to a dark brown as it fades.
        const int flames = int((9 + 12 * s) * cs * n) + 2;
        for (int i = 0; i < flames; ++i) {
            const int kind = i % 4;  // 0 hot (add), 1 core, 2 flame, 3 soot
            Particle p = P(kFire, kind <= 1 ? Blend::Add : Blend::Alpha);
            const float a = rnd(0, 2 * kPi);
            const float reach = kind == 1 ? 0.5f : 1.0f;
            const float sp = rnd(12, 52) * s * reach;
            const float off = rnd(0, 4) * s * reach;
            p.x += std::cos(a) * off;
            p.y += std::sin(a) * off * 0.75f;
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.72f;
            p.vz = rnd(8, 34) * rs * (kind == 3 ? 1.4f : 1.0f) + (ground ? 0.0f : rnd(-12, 12));
            p.drag = 3.0f;
            p.life = rnd(0.45f, 0.85f) * (0.85f + 0.2f * rs);
            // Part of the fire keeps rolling up from the spot for a while (the original vehicle
            // death animations run ~1-3 s): later puffs start lower and rise.
            if (c != ExClass::Medium && i % 3 == 2 && i > 3) {
                p.age = -rnd(0.12f, 0.6f) * rs;
                p.vx *= 0.35f;
                p.vy *= 0.35f;
            }
            p.size0 = rnd(8, 13) * ss;
            p.size1 = rnd(20, 32) * ss;
            switch (kind) {
            case 0:
                p.c0 = rgba(255, 196, 105, 160);
                p.c1 = rgba(255, 70, 15, 0);
                p.life *= 0.7f;
                break;
            case 1:  // white-hot core (additive: blooms on any ground)
                p.c0 = rgba(255, 232, 180, 170);
                p.c1 = rgba(255, 90, 20, 0);
                p.life *= 0.7f;
                p.size1 *= 0.8f;
                break;
            case 2:  // flame body: deep orange, separates from the sand
                p.c0 = rgba(255, 128, 36, 240);
                p.c1 = rgba(70, 22, 10, 0);
                break;
            default:  // soot
                p.age = -rnd(0.03f, 0.1f);
                p.c0 = rgba(96, 52, 30, 230);
                p.c1 = rgba(22, 18, 16, 0);
                p.life *= 1.35f;
                break;
            }
            if (kind != 3) p.fx = kFxHot;
            p.vrot = rnd(-1.6f, 1.6f);
            add(p);
        }
        // Big blasts: secondary bursts popping around the first one.
        if (s > 1.3f) {
            const int pops = int((s - 1) * 4 * n);
            for (int i = 0; i < pops; ++i) {
                const float a = rnd(0, 2 * kPi), r = rnd(14, 36) * s;
                const float delay = rnd(0.08f, 0.45f);
                for (int k = 0; k < 4; ++k) {
                    Particle p = P(kFire, k == 0 ? Blend::Add : Blend::Alpha);
                    p.x += std::cos(a) * r + rnd(-5, 5);
                    p.y += std::sin(a) * r * 0.75f + rnd(-4, 4);
                    p.z += rnd(0, 10);
                    p.age = -delay;
                    p.vx = std::cos(a) * rnd(5, 20);
                    p.vy = std::sin(a) * rnd(4, 14);
                    p.vz = rnd(10, 30);
                    p.drag = 2.5f;
                    p.life = rnd(0.35f, 0.6f);
                    p.size0 = rnd(8, 12) * ss * 0.7f;
                    p.size1 = rnd(20, 30) * ss * 0.7f;
                    p.c0 = k == 0 ? rgba(255, 200, 110, 150) : k == 1 ? rgba(255, 230, 160, 245) : rgba(240, 140, 60, 235);
                    p.c1 = k == 0 ? rgba(255, 70, 15, 0) : rgba(60, 26, 14, 0);
                    p.fx = kFxHot;
                    p.vrot = rnd(-1.5f, 1.5f);
                    add(p);
                }
                addFlash(x + std::cos(a) * r, y + std::sin(a) * r * 0.75f - z, z, 70 * ss, rgba(255, 150, 70, 160),
                         0.25f + delay, delay);
            }
        }
        // 3. Smoke column: rises from the fire as it dies down - puffs released over a while,
        // the first lit orange from below, then dark grey, drifting with the wind.
        const int smokes = int((6 + 8 * s) * cs * n) + 1;
        for (int i = 0; i < smokes; ++i) {
            Particle p = P(kSmoke, Blend::Alpha);
            const float rel = float(i) / float(std::max(1, smokes - 1));  // release order
            p.age = -(0.1f + rel * (0.45f + 0.35f * rs) + rnd(0, 0.08f));
            p.x += rnd(-6, 6) * ss;
            p.y += rnd(-4, 4) * ss;
            p.z += rnd(2, 8) * ss;
            p.vx = rnd(-7, 7) + kWindX;
            p.vy = rnd(-4, 4) + kWindY;
            p.vz = rnd(22, 40) * rs;
            p.drag = 0.35f;
            p.life = rnd(2.0f, 3.4f) * rs * (c == ExClass::Huge ? 1.3f : 1.0f);
            p.size0 = rnd(9, 14) * ss;
            p.size1 = rnd(34, 50) * ss;
            const int g = int(rnd(38, 60));
            if (rel < 0.3f) p.c0 = rgba(124, 76, 44, 185);
            else p.c0 = rgba(g, g - 3, g - 6, 170);
            p.c1 = rgba(g + 70, g + 66, g + 62, 0);
            p.vrot = rnd(-0.6f, 0.6f);
            add(p);
        }
        // 4. A few sparks (no glitter: few, short, falling).
        const int sparks = int((3 + 4 * s) * n);
        for (int i = 0; i < sparks; ++i) {
            Particle p = P(kSpark, Blend::Add);
            const float a = rnd(0, 2 * kPi), sp = rnd(70, 200) * rs;
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(30, 120);
            p.gravity = -320;
            p.drag = 1.3f;
            p.life = rnd(0.25f, 0.6f);
            p.size0 = rnd(2.2f, 3.0f);
            p.size1 = 1.1f;
            p.c0 = rgba(255, 236, 180, 255);
            p.c1 = rgba(255, 110, 30, 0);
            p.streak = true;
            add(p);
        }
        // 5. Debris chunks (vehicle deaths and bigger), some trailing smoke.
        if (c != ExClass::Medium || s > 0.75f) {
            const int chunks = int((2 + 4 * s) * n) + 1;
            for (int i = 0; i < chunks; ++i) {
                Particle p = P(kDebris, Blend::Alpha);
                const float a = rnd(0, 2 * kPi), sp = rnd(35, 120) * rs;
                p.vx = std::cos(a) * sp;
                p.vy = std::sin(a) * sp * 0.7f;
                p.vz = rnd(70, 180);
                p.gravity = -420;
                p.drag = 0.4f;
                p.life = rnd(1.4f, 2.4f);
                p.size0 = p.size1 = rnd(2.6f, 5.0f);
                const int g = int(rnd(70, 100));
                p.c0 = p.c1 = rgba(g, g - 8, g - 14, 255);
                p.size0 = p.size1 = rnd(2.2f, 4.2f);
                p.vrot = rnd(-12, 12);
                p.fx = kFxLandDust | kFxLateFade;
                if (rnd() < 0.35f) p.fx |= kFxTrail;
                add(p);
            }
        }
        // 6. On the ground: a ring of dust pushed out, a scorch mark, a dust / shock ring.
        if (ground) {
            const std::uint32_t sand = terrainColour(w, atlas, x, y);
            if (!crowded) {
                const int puffs = int((5 + 4 * s) * ld);
                for (int i = 0; i < puffs; ++i) {
                    Particle p = P(kSmoke, Blend::Alpha);
                    const float a = (float(i) + rnd(0, 0.6f)) / float(std::max(1, puffs)) * 2 * kPi, sp = rnd(30, 70) * s;
                    p.age = -0.03f;
                    p.vx = std::cos(a) * sp;
                    p.vy = std::sin(a) * sp * 0.75f;
                    p.z = 1;
                    p.vz = rnd(2, 8);
                    p.drag = 2.2f;
                    p.life = rnd(0.9f, 1.6f) * rs;
                    p.size0 = 8 * ss + 2;
                    p.size1 = rnd(26, 40) * ss;
                    p.c0 = withAlpha(scaleRgb(sand, rnd(0.9f, 1.0f)), 105);
                    p.c1 = withAlpha(sand, 0);
                    add(p);
                }
                if (s >= 0.9f) addScorch(x, y, 50 + 70 * s, 0.55f + 0.15f * s, 3, 1, scaleRgb(sand, 0.85f));
                if (c == ExClass::Huge) addScorch(x, y, 80 + 90 * s, 0.75f, 2);
            }
            addScorch(x, y, 16 + 22 * ss, 22 + 14 * s, 0, crowded ? 0.5f : std::min(1.0f, 0.6f + 0.3f * s));
        }
    } else if (c == ExClass::Small) {
        spawnImpact(w, atlas, x, y, z, 1);
    } else if (c == ExClass::Whiten) {
        // A vehicle died here (the death flash under its explosion): a burning wreck - a charred
        // hulk mark and a fire with a smoke column that burns for a while.
        if (ground && fires_.size() < 160) {
            Fire f{x + rnd(-2, 2), y + rnd(-2, 2), 0, rnd(9, 14), rnd(0.55f, 0.72f), 0};
            f.smoke = 0.6f;
            fires_.push_back(f);
            addScorch(x, y, 30, 45, 1);
        }
    } else if (c == ExClass::DhmTrail) {
        // The Death Hand missile's exhaust (one per flight tick): a thick smoke trail, the motor
        // flame and its light.
        Particle p = P(kSmoke, Blend::Alpha);
        p.vx = kWindX * 0.5f + rnd(-4, 4);
        p.vy = kWindY * 0.5f + rnd(-4, 4);
        p.vz = rnd(-2, 4);
        p.drag = 0.8f;
        p.life = rnd(2.4f, 3.8f);
        p.size0 = rnd(7, 10);
        p.size1 = rnd(28, 40);
        p.c0 = rgba(222, 214, 204, 185);
        p.c1 = rgba(176, 170, 164, 0);
        p.vrot = rnd(-0.8f, 0.8f);
        add(p);
        Particle f = P(kFire, Blend::Add);
        f.life = 0.14f;
        f.size0 = 15;
        f.size1 = 6;
        f.c0 = rgba(255, 215, 140, 235);
        f.c1 = rgba(255, 110, 30, 0);
        add(f);
        Particle g = P(kGlow, Blend::Add);
        g.life = 0.1f;
        g.size0 = g.size1 = 22;
        g.c0 = rgba(255, 190, 110, 150);
        g.c1 = rgba(255, 120, 40, 0);
        add(g);
        addFlash(x, y - z, z, 90, rgba(255, 170, 80, 170), 0.12f);
    } else if (c == ExClass::Dust) {  // (thumper puffs; the worm's sand: effects_events.cpp)
        const std::uint32_t sand = terrainColour(w, atlas, x, y);
        const int spray = 0;
        for (int i = 0; i < spray; ++i) {
            Particle p = P(kSmoke, Blend::Alpha);
            const float a = rnd(0, 2 * kPi), sp = rnd(15, 60);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.6f;
            p.vz = rnd(50, 120);
            p.gravity = -240;
            p.drag = 0.8f;
            p.life = rnd(0.6f, 1.0f);
            p.size0 = rnd(3, 6);
            p.size1 = rnd(9, 13);
            p.c0 = withAlpha(scaleRgb(sand, rnd(0.8f, 1.05f)), 220);
            p.c1 = withAlpha(sand, 0);
            add(p);
        }
        const int clouds = 2;
        for (int i = 0; i < clouds; ++i) {
            Particle p = P(kSmoke, Blend::Alpha);
            p.x += rnd(-8, 8);
            p.y += rnd(-6, 6);
            p.vx = rnd(-8, 8) + kWindX;
            p.vy = rnd(-5, 5);
            p.vz = rnd(3, 10);
            p.drag = 1;
            p.life = rnd(1.0f, 1.8f);
            p.size0 = rnd(8, 12);
            p.size1 = rnd(24, 34);
            p.c0 = withAlpha(scaleRgb(sand, 1.05f), 120);
            p.c1 = withAlpha(sand, 0);
            add(p);
        }
    } else if (c == ExClass::Flame) {
        spawnFlame(w, x, y, z);  // building damage flames (BUILEXP.BIN points; effects_events.cpp)
    } else if (c == ExClass::Gas) {
        // Deviator hit: a swirling cloud of the firer's gas, a soft glow, motes (the conversion
        // itself is the simulation's).
        const std::uint32_t body = gasBody(house), glow = gasGlow(house);
        Particle f = P(kGlow, Blend::Add);
        f.life = 0.5f;
        f.size0 = 30;
        f.size1 = 64;
        f.c0 = withAlpha(glow, 60);
        f.c1 = withAlpha(body, 0);
        add(f);
        addFlash(x, y - z, z, 90, withAlpha(body, 90), 0.9f, 0.08f);
        for (int i = 0; i < 18; ++i) {
            Particle p = P(kGas, Blend::Alpha);
            const float a = rnd(0, 2 * kPi), sp = rnd(12, 46);
            p.age = -rnd(0, 0.15f);
            p.vx = std::cos(a) * sp + kWindX * 0.3f;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(4, 14);
            p.drag = 1.5f;
            p.life = rnd(1.8f, 3.0f);
            p.size0 = rnd(8, 14);
            p.size1 = rnd(40, 58);
            p.c0 = withAlpha(i % 3 == 0 ? scaleRgb(body, 0.72f) : body, i % 3 == 0 ? 130 : 110);
            p.c1 = withAlpha(scaleRgb(body, 1.15f), 0);
            p.vrot = rnd(-1.4f, 1.4f);
            add(p);
        }
        for (int i = 0; i < 5; ++i) {
            Particle p = P(kGas, Blend::Add);
            const float a = rnd(0, 2 * kPi), sp = rnd(6, 20);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(2, 8);
            p.drag = 1.2f;
            p.life = rnd(0.7f, 1.2f);
            p.size0 = rnd(10, 16);
            p.size1 = rnd(26, 36);
            p.c0 = withAlpha(glow, 70);
            p.c1 = withAlpha(body, 0);
            p.vrot = rnd(-2, 2);
            add(p);
        }
        for (int i = 0; i < 8; ++i) {
            Particle p = P(kSpark, Blend::Add);
            const float a = rnd(0, 2 * kPi), sp = rnd(10, 40);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(8, 30);
            p.drag = 1.0f;
            p.life = rnd(0.8f, 1.6f);
            p.size0 = rnd(2.4f, 3.4f);
            p.size1 = 1.2f;
            p.c0 = withAlpha(glow, 230);
            p.c1 = withAlpha(body, 0);
            add(p);
        }
    } else if (c == ExClass::Plasma) {
        // Devastator hit (EX_DEVASTATOR): the bullet is a PAIR of plasma bolts (the original's DEVBULLET
        // frames show two orbs 8 px apart, MUZ_DEVAS two flashes), so the burst pulses twice: a white-hot
        // flash, then a second magenta pulse a beat later; shock rings, arcing motes, violet haze, dark
        // smoke and a scorch, no flames.
        addFlash(x, y - z, z, 175, rgba(225, 110, 255, 245), 0.55f);
        addFlash(x, y - z, z, 120, rgba(255, 220, 255, 200), 0.18f);
        for (int k = 0; k < 2; ++k) {  // the two pulses
            Particle g = P(kGlow, Blend::Add);
            g.x += rnd(-3, 3);
            g.y += rnd(-3, 3);
            g.age = -0.09f * k;
            g.life = 0.26f;
            g.size0 = 30 - 6 * k;
            g.size1 = 72 - 12 * k;
            g.c0 = k ? rgba(240, 140, 255, 150) : rgba(255, 235, 255, 170);
            g.c1 = rgba(200, 80, 255, 0);
            add(g);
            Particle h = P(kGlow, Blend::Add);  // hot core
            h.age = g.age;
            h.life = 0.16f;
            h.size0 = 18;
            h.size1 = 8;
            h.c0 = rgba(255, 255, 255, 230);
            h.c1 = rgba(255, 190, 255, 0);
            add(h);
        }
        for (int k = 0; k < 3; ++k) {
            Particle r = P(kWave, Blend::Add);
            r.age = -0.07f * k;
            r.life = 0.5f;
            r.size0 = 12;
            r.size1 = 96 - 18 * k;
            r.c0 = rgba(245, 160, 255, 150);
            r.c1 = rgba(150, 50, 255, 0);
            add(r);
            r.blend = Blend::Alpha;  // the colour on bright sand
            r.c0 = rgba(170, 40, 230, 140);
            r.c1 = rgba(120, 30, 200, 0);
            add(r);
        }
        const int sparks = std::max(8, int(24 * ld));
        for (int i = 0; i < sparks; ++i) {
            Particle p = P(kSpark, Blend::Add);
            const float a = rnd(0, 2 * kPi), sp = rnd(70, 240);
            p.age = i % 2 ? -0.09f : 0;  // half of them with the second pulse
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(10, 90);
            p.gravity = -120;
            p.drag = 2.5f;
            p.life = rnd(0.3f, 0.7f);
            p.size0 = rnd(2.6f, 3.8f);
            p.size1 = 1;
            p.c0 = i % 3 ? rgba(255, 170, 255, 255) : rgba(255, 240, 255, 255);
            p.c1 = rgba(160, 60, 255, 0);
            p.streak = true;
            add(p);
        }
        for (int i = 0; i < 6; ++i) {
            Particle p = P(kGlow, Blend::Alpha);
            const float a = rnd(0, 2 * kPi), sp = rnd(10, 40);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(5, 20);
            p.drag = 2;
            p.life = rnd(0.3f, 0.55f);
            p.size0 = rnd(12, 18);
            p.size1 = 4;
            p.c0 = rgba(190, 60, 240, 120);
            p.c1 = rgba(120, 30, 220, 0);
            add(p);
        }
        for (int i = 0; i < 4; ++i) {
            Particle p = P(kSmoke, Blend::Alpha);
            p.age = -rnd(0.1f, 0.25f);
            p.vx = rnd(-8, 8) + kWindX;
            p.vy = rnd(-5, 5) + kWindY;
            p.vz = rnd(8, 18);
            p.drag = 0.6f;
            p.life = rnd(1.2f, 2.0f);
            p.size0 = rnd(8, 12);
            p.size1 = rnd(26, 36);
            p.c0 = rgba(55, 40, 70, 150);
            p.c1 = rgba(110, 95, 125, 0);
            p.vrot = rnd(-0.6f, 0.6f);
            add(p);
        }
        if (ground) addScorch(x, y, 36, 28, 0, 0.9f);
        shake = 0.06f * shakeMul;  // a light kick
    } else if (c == ExClass::Spark) {
        for (int i = 0; i < 4; ++i) {
            Particle p = P(kSpark, Blend::Add);
            const float a = rnd(0, 2 * kPi), sp = rnd(30, 90);
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(10, 50);
            p.gravity = -150;
            p.life = rnd(0.2f, 0.45f);
            p.size0 = 2.6f;
            p.size1 = 1;
            p.c0 = rgba(190, 255, 200, 255);
            p.c1 = rgba(60, 220, 120, 0);
            p.streak = true;
            add(p);
        }
        addFlash(x, y - z, z, 40, rgba(120, 255, 150, 120), 0.2f);
    }
    // (Muzzle / Trail: the bullets draw their own flash and trail.)
    // Screen shake, fading with the distance from the view centre.
    if (shake > 0) addTrauma(x, y, shake, cam, viewW, viewH);
    // Pre-aged spawn (the first frame of a view: the explosion was already running).
    preAge(first, age);
}

// A small-arms hit: sparks on armour (a vehicle or building cell, or in the air), a puff of
// dust and flying grains on the ground (terrain colour), a chip and a spark on rock.
void Effects::spawnImpact(const game::World& w, const TileAtlas& atlas, float x, float y, float z, float scale) {
    const int tx = int(x) >> 5, ty = int(y) >> 5;
    const std::uint32_t cf = w.inBounds(tx, ty) ? w.cell(tx, ty).flags : 0;
    const bool armour = (cf & (game::cell_flags::kVehicle | game::cell_flags::kBuilding)) || z > 4;
    const bool sand = (cf & game::cell_flags::kSand) != 0;
    const float ld = load();
    Particle b{};
    b.x = x;
    b.y = y;
    b.z = z;
    if (armour) {
        const int n = ld < 1 ? 2 : 3 + int(rnd() * 2);
        for (int i = 0; i < n; ++i) {
            Particle p = b;
            p.tex = kSpark;
            p.blend = Blend::Add;
            const float a = rnd(0, 2 * kPi), sp = rnd(60, 160) * scale;
            p.z += 4;
            p.vx = std::cos(a) * sp;
            p.vy = std::sin(a) * sp * 0.7f;
            p.vz = rnd(10, 70);
            p.gravity = -300;
            p.life = rnd(0.08f, 0.2f);
            p.size0 = 2.2f;
            p.size1 = 1.0f;
            p.c0 = rgba(255, 244, 205, 255);
            p.c1 = rgba(255, 150, 60, 0);
            p.streak = true;
            add(p);
        }
        Particle g = b;
        g.z += 4;
        g.tex = kGlow;
        g.blend = Blend::Add;
        g.life = 0.06f;
        g.size0 = 12 * scale;
        g.size1 = 16 * scale;
        g.c0 = rgba(255, 228, 170, 200);
        g.c1 = rgba(255, 160, 80, 0);
        add(g);
        addFlash(x, y - z - 4, z + 4, 30 * scale, rgba(255, 205, 140, 150), 0.07f);
        Particle s = b;  // a wisp of grey smoke
        s.z += 5;
        s.tex = kSmoke;
        s.blend = Blend::Alpha;
        s.vx = kWindX * 0.5f;
        s.vz = 8;
        s.drag = 1.5f;
        s.life = rnd(0.45f, 0.7f);
        s.size0 = 4;
        s.size1 = 12;
        s.c0 = rgba(120, 116, 110, 80);
        s.c1 = rgba(150, 146, 140, 0);
        s.rot = rnd(0, 6.28f);
        add(s);
        return;
    }
    const std::uint32_t ground = terrainColour(w, atlas, x, y);
    for (int i = 0; i < 2; ++i) {
        Particle d = b;
        d.tex = kSmoke;
        d.blend = Blend::Alpha;
        d.x += rnd(-2, 2);
        d.vx = rnd(-6, 6) + kWindX * 0.4f;
        d.vy = rnd(-4, 4);
        d.vz = rnd(6, 16);
        d.drag = 1.8f;
        d.life = rnd(0.5f, 0.9f);
        d.size0 = (4 + 2 * i) * scale;
        d.size1 = (13 + 5 * i) * scale;
        d.c0 = withAlpha(scaleRgb(ground, sand ? 1.04f : 0.95f), i ? 100 : 140);
        d.c1 = withAlpha(ground, 0);
        d.rot = rnd(0, 6.28f);
        d.vrot = rnd(-1, 1);
        add(d);
    }
    const int grains = ld < 1 ? 1 : 3;
    for (int i = 0; i < grains; ++i) {  // grains / chips thrown up
        Particle p = b;
        p.tex = kDebris;
        p.blend = Blend::Alpha;
        const float a = rnd(0, 2 * kPi), sp = rnd(15, 45);
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp * 0.7f;
        p.vz = rnd(40, 90);
        p.gravity = -320;
        p.life = rnd(0.35f, 0.6f);
        p.size0 = p.size1 = rnd(1.3f, 2.1f);
        p.c0 = withAlpha(scaleRgb(ground, 0.78f), 255);
        p.c1 = withAlpha(p.c0, 0);
        p.rot = rnd(0, 6.28f);
        p.vrot = rnd(-10, 10);
        add(p);
    }
    if (!sand && rnd() < 0.6f) {  // a spark off the rock
        Particle p = b;
        p.tex = kSpark;
        p.blend = Blend::Add;
        const float a = rnd(0, 2 * kPi), sp = rnd(50, 120);
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp * 0.7f;
        p.vz = rnd(20, 60);
        p.gravity = -300;
        p.life = rnd(0.08f, 0.16f);
        p.size0 = 2.0f;
        p.size1 = 1.0f;
        p.c0 = rgba(255, 238, 200, 255);
        p.c1 = rgba(255, 150, 60, 0);
        p.streak = true;
        add(p);
    }
}

// Muzzle flashes by weapon: a directional flash star (kMuzzle along the shot), a hot core, a
// short light on the ground and the firer, smoke for cannons / artillery / rocket backblast.
void Effects::spawnMuzzle(WeaponClass c, float x, float y, float z, float dx, float dy, float age, int house) {
    (void)age;
    const float len = std::hypot(dx, dy);
    const float ux = len > 0 ? dx / len : 0, uy = len > 0 ? dy / len : 1;
    const float ang = std::atan2(uy, ux);
    std::uint32_t col = rgba(255, 226, 150, 255);
    float L = 0, W = 0, glow = 0, light = 0, lightLife = 0.08f;
    int smoke = 0, lightA = 160;
    switch (c) {
    case WeaponClass::Gun: L = 13; W = 8; glow = 15; light = 58; lightA = 170; lightLife = 0.08f; break;
    case WeaponClass::Cannon: L = 24; W = 14; glow = 26; light = 92; lightA = 205; lightLife = 0.12f; smoke = 3; break;
    case WeaponClass::Artillery: L = 30; W = 18; glow = 32; light = 112; lightA = 220; lightLife = 0.14f; smoke = 4; break;
    case WeaponClass::Rocket:
        L = 13; W = 10; glow = 20; light = 64; lightA = 165; lightLife = 0.1f; smoke = 3; col = rgba(255, 196, 120, 255);
        break;
    case WeaponClass::Plasma: L = 18; W = 15; glow = 30; light = 90; lightA = 210; col = rgba(236, 130, 255, 255); break;
    case WeaponClass::Sonic: glow = 30; light = 70; lightA = 150; col = rgba(150, 215, 255, 200); break;
    case WeaponClass::Gas: glow = 26; light = 60; lightA = 110; col = withAlpha(gasGlow(house), 120); break;
    default: return;
    }
    const float var = rnd(0.8f, 1.2f);
    if (L > 0) {  // the flash star, its muzzle end at the barrel
        Particle f{};
        f.x = x + ux * L * var * 0.42f;
        f.y = y + uy * L * var * 0.42f;
        f.z = z;
        f.tex = kMuzzle;
        f.blend = Blend::Add;
        f.aligned = true;
        f.rot = ang;
        f.len = L * var;
        f.life = c == WeaponClass::Gun ? 0.07f : 0.075f;
        f.size0 = f.size1 = W * var;
        f.c0 = col;
        f.c1 = withAlpha(col, 60);
        add(f);
    }
    Particle g{};
    g.x = x + ux * 3;
    g.y = y + uy * 3;
    g.z = z;
    g.tex = kGlow;
    g.blend = Blend::Add;
    g.life = c == WeaponClass::Gun ? 0.08f : 0.1f;
    g.size0 = glow;
    g.size1 = glow * 1.3f;
    g.c0 = withAlpha(col, c == WeaponClass::Gas ? 90 : 210);
    g.c1 = withAlpha(col, 0);
    add(g);
    addFlash(x + ux * 6, y + uy * 6 - z, z, light, withAlpha(col == rgba(255, 226, 150, 255) ? rgba(255, 200, 120, 255) : col, lightA),
             lightLife);
    if (c == WeaponClass::Sonic) {  // a pressure ring leaving the emitter
        for (int k = 0; k < 2; ++k) {
            Particle r = g;
            r.tex = kWave;
            r.blend = k ? Blend::Alpha : Blend::Add;
            r.age = -0.05f * k;
            r.life = 0.4f;
            r.size0 = 8;
            r.size1 = 40;
            r.c0 = k ? rgba(90, 165, 255, 110) : rgba(190, 235, 255, 170);
            r.c1 = withAlpha(r.c0, 0);
            add(r);
        }
    } else if (c == WeaponClass::Gas) {  // a puff of the house's gas
        for (int k = 0; k < 4; ++k) {
            Particle p = g;
            p.tex = kGas;
            p.blend = Blend::Alpha;
            p.vx = ux * rnd(15, 40) + rnd(-8, 8);
            p.vy = uy * rnd(15, 40) + rnd(-8, 8);
            p.vz = rnd(2, 8);
            p.drag = 2;
            p.life = rnd(0.8f, 1.4f);
            p.size0 = 5;
            p.size1 = rnd(18, 26);
            p.c0 = withAlpha(gasBody(house), 150);
            p.c1 = withAlpha(gasBody(house), 0);
            p.rot = rnd(0, 6.28f);
            p.vrot = rnd(-1.5f, 1.5f);
            add(p);
        }
    }
    // Smoke: cannons and artillery blow it forward and to the sides, rockets backwards.
    const float dir = c == WeaponClass::Rocket ? -1.0f : 1.0f;
    for (int k = 0; k < smoke; ++k) {
        Particle p = g;
        p.tex = kSmoke;
        p.blend = Blend::Alpha;
        const float spread = rnd(-0.7f, 0.7f) * (k == 0 ? 0.3f : 1.0f);
        const float ca = std::cos(spread), sa = std::sin(spread);
        const float sx = ux * ca - uy * sa, sy = ux * sa + uy * ca;
        const float sp = rnd(18, 42) * (c == WeaponClass::Artillery ? 1.3f : 1.0f);
        p.x = x + ux * (dir > 0 ? 6.0f : -4.0f);
        p.y = y + uy * (dir > 0 ? 6.0f : -4.0f);
        p.vx = sx * sp * dir + kWindX * 0.4f;
        p.vy = sy * sp * dir;
        p.vz = rnd(4, 10);
        p.drag = 2.2f;
        p.life = rnd(0.6f, 1.1f) * (c == WeaponClass::Artillery ? 1.3f : 1.0f);
        p.size0 = 5;
        p.size1 = rnd(16, 24) * (c == WeaponClass::Artillery ? 1.25f : 1.0f);
        p.c0 = rgba(204, 198, 188, 120);
        p.c1 = rgba(170, 166, 160, 0);
        p.rot = rnd(0, 6.28f);
        p.vrot = rnd(-1, 1);
        add(p);
    }
}

// One shot from the world's shot log: the muzzle flash at the end of the gun (the bullet starts
// at the firer's position + the template's ShootOffset along the aim, 2-5 px: inside the body;
// the flash goes to where the barrel ends: infantry 6 px, vehicles 2 x offset + 4 px - trike /
// raider / quad 10, tanks 14, devastator 18 - turrets 12 px from their centre), plus a tracer
// gun -> aim point for machine guns (the bullet itself lands within the tick).
void Effects::spawnShot(const game::World& w, const game::ShotEvent& e, WeaponClass c) {
    if (c == WeaponClass::None || c == WeaponClass::Bomb || c == WeaponClass::Debris || c == WeaponClass::Spice) return;
    const auto& side = w.sides()[std::size_t(e.side)];
    float x = float(e.srcX), y = float(e.srcY);
    const float dx = float(e.dstX) - x, dy = float(e.dstY) - y, dist = std::hypot(dx, dy);
    const float ux = dist > 0 ? dx / dist : 0, uy = dist > 0 ? dy / dist : 1;
    float extra = 0, z = 6;
    if (e.firer >= 0 && e.firer < int(side.links.size())) {
        const auto kind = side.links[std::size_t(e.firer)].kind;
        if (kind == game::SlotKind::Unit) {
            const auto& u = side.units[std::size_t(e.firer)];
            if (u.type >= 0 && u.type < formats::kMaxUnitTypes) {
                const auto& ut = t_.bin.units[u.type];
                const float off = float(std::max<int>(0, ut.primaryWeaponShootOffset));
                extra = ut.isInfantry ? std::max(0.0f, 6 - off) : off + 4;
                if (ut.isInfantry) z = 5;
            }
            z += float(u.posZ) / 65536.0f;
        } else if (kind == game::SlotKind::Building) {
            extra = 12;
            z = 10;
        }
    }
    extra = std::min(extra, dist * 0.5f);
    x += ux * extra;
    y += uy * extra;
    if (c == WeaponClass::Plasma) {
        // The devastator's twin plasma cannons: two flashes 8 px apart across the aim (MUZ_DEVAS).
        for (int k = -1; k <= 1; k += 2)
            spawnMuzzle(c, x - uy * kPlasmaTwinHalf * float(k), y + ux * kPlasmaTwinHalf * float(k), z, dx, dy, 0, side.house);
    } else {
        spawnMuzzle(c, x, y, z, dx, dy, 0, side.house);
    }
    if (c == WeaponClass::Gun) spawnTracer(x, y, z, float(e.dstX), float(e.dstY), 0);
}

// A machine-gun tracer: a short bright streak flying from the muzzle to the target (the gun
// bullets reach their target within one tick, so the tracer carries the shot).
void Effects::spawnTracer(float x, float y, float z, float tx, float ty, float tz) {
    const float dx = tx - x, dy = ty - y, dz = tz - z, dist = std::hypot(dx, dy);
    if (dist < 6) return;
    constexpr float kSpeed = 1100;  // px / s
    const float life = std::clamp(dist / kSpeed, 0.05f, 0.16f);
    const float ux = dx / dist, uy = dy / dist;
    const float L = std::min(24.0f, dist * 0.6f);
    Particle p{};
    p.x = x + ux * L * 0.5f;
    p.y = y + uy * L * 0.5f;
    p.z = z;
    p.vx = (dx - ux * L * 0.5f) / life;
    p.vy = (dy - uy * L * 0.5f) / life;
    p.vz = dz / life;
    p.life = life;
    p.tex = kStreak;
    p.blend = Blend::Add;
    p.aligned = true;
    p.rot = std::atan2(uy, ux);
    p.len = L;
    p.size0 = p.size1 = 3.0f;
    p.c0 = rgba(255, 244, 205, 255);
    p.c1 = rgba(255, 214, 150, 210);
    add(p);
    // A faint hot line along the whole path for an instant (reads at any frame rate).
    Particle l{};
    l.x = x + dx * 0.5f;
    l.y = y + dy * 0.5f;
    l.z = z + dz * 0.5f;
    l.life = 0.06f;
    l.tex = kLine;
    l.blend = Blend::Add;
    l.aligned = true;
    l.rot = p.rot;
    l.len = dist;
    l.size0 = 2.2f;
    l.size1 = 1.2f;
    l.c0 = rgba(255, 220, 160, 90);
    l.c1 = rgba(255, 200, 140, 0);
    add(l);
}

// What a projectile in flight looks like this frame (strips / sprites rebuilt every update in
// beams_): (x, y) ground point, z height, (vx, vy, vz) its velocity (any scale).
void Effects::projectileLook(WeaponClass c, float x, float y, float z, float vx, float vy, float vz, int house, int seed) {
    auto put = [&](Tex t, Blend b, float size, float len, float rot, bool aligned, std::uint32_t col, float px, float py,
                   float pz) {
        Particle p{};
        p.x = px;
        p.y = py;
        p.z = pz;
        p.life = 1;
        p.size0 = p.size1 = size;
        p.c0 = p.c1 = col;
        p.tex = t;
        p.blend = b;
        p.aligned = aligned;
        p.rot = rot;
        p.len = len;
        beams_.push_back(p);
    };
    const float gl = std::hypot(vx, vy);
    const float ux = gl > 1e-4f ? vx / gl : 1, uy = gl > 1e-4f ? vy / gl : 0;
    const float ang = std::atan2(uy, ux);
    const float sang = std::atan2(vy - vz, vx);  // on screen (2D: drawn y = y - z)
    const float flick = 0.85f + 0.15f * std::sin(time_ * 63 + float(seed) * 1.7f);
    switch (c) {
    case WeaponClass::Cannon:  // a hot slug with a short glowing streak
        put(kStreak, Blend::Add, 3.6f, 16, ang, true, rgba(255, 214, 140, 245), x - ux * 6, y - uy * 6, z);
        put(kGlow, Blend::Add, 6, 0, 0, false, rgba(255, 236, 190, 230), x, y, z);
        break;
    case WeaponClass::Artillery:
        put(kGlow, Blend::Alpha, 8, 0, 0, false, rgba(0, 0, 0, std::clamp(int(90 - z), 25, 80)), x, y, 0);  // shadow
        put(kGlow, Blend::Add, 9, 0, 0, false, rgba(255, 196, 120, 120), x, y, z);
        put(kCapsule, Blend::Alpha, 3.6f, 7, sang, false, rgba(78, 72, 64, 255), x, y, z);
        break;
    case WeaponClass::Rocket: {  // body, a flame jet behind it, a small hot glow at the nozzle
        put(kGlow, Blend::Add, 12 * flick, 0, 0, false, rgba(255, 150, 60, 120), x - ux * 6, y - uy * 6, z);
        put(kStreak, Blend::Add, 4.2f * flick, 11 * flick, ang, true, rgba(255, 190, 90, 235), x - ux * 9, y - uy * 9, z);
        put(kCapsule, Blend::Alpha, 3.2f, 10, ang, true, rgba(222, 216, 206, 255), x, y, z);
        put(kGlow, Blend::Add, 6, 0, 0, false, rgba(255, 248, 225, 240), x - ux * 5.5f, y - uy * 5.5f, z);
        break;
    }
    case WeaponClass::Plasma: {
        // Twin bolts side by side across the flight (the original DEVBULLET frames: two orbs 8 px apart),
        // each a violet haze, a magenta glow, a short hot streak behind and a white core; one wide soft
        // glow around the pair.
        put(kGlow, Blend::Add, 34 * flick, 0, 0, false, rgba(200, 80, 255, 90), x, y, z);
        for (int k = -1; k <= 1; k += 2) {
            const float bx = x - uy * kPlasmaTwinHalf * float(k), by = y + ux * kPlasmaTwinHalf * float(k);
            const float f2 = 0.85f + 0.15f * std::sin(time_ * 71 + float(seed + k) * 2.3f);
            put(kGas, Blend::Alpha, 12, 0, time_ * 4 + float(k), false, rgba(160, 50, 220, 95), bx, by, z);
            put(kGlow, Blend::Alpha, 15, 0, 0, false, rgba(140, 30, 215, 120), bx, by, z);  // the colour on bright sand
            put(kStreak, Blend::Add, 6 * f2, 14, ang, true, rgba(235, 130, 255, 200), bx - ux * 6, by - uy * 6, z);
            put(kGlow, Blend::Add, 17 * f2, 0, 0, false, rgba(235, 120, 255, 220), bx, by, z);
            put(kGlow, Blend::Add, 7.5f, 0, 0, false, rgba(255, 245, 255, 255), bx, by, z);
        }
        break;
    }
    case WeaponClass::Gas:
        put(kGas, Blend::Alpha, 11, 0, time_ * 3, false, withAlpha(gasBody(house), 150), x, y, z);
        put(kGlow, Blend::Add, 13 * flick, 0, 0, false, withAlpha(gasGlow(house), 150), x, y, z);
        break;
    case WeaponClass::Bomb:
        put(kGlow, Blend::Alpha, 9, 0, 0, false, rgba(0, 0, 0, std::clamp(int(100 - z), 30, 90)), x, y, 0);  // shadow
        put(kCapsule, Blend::Alpha, 4, 8.5f, kPi * 0.5f, false, rgba(66, 64, 60, 255), x, y, z);
        break;
    case WeaponClass::Spice: {  // a glob of spice tumbling through the air, its shadow on the ground
        put(kGlow, Blend::Alpha, 7, 0, 0, false, rgba(0, 0, 0, std::clamp(int(80 - z), 20, 70)), x, y, 0);
        put(kSmoke, Blend::Alpha, 7.5f, 0, time_ * 5 + float(seed), false, rgba(222, 142, 70, 170), x, y, z);
        put(kDebris, Blend::Alpha, 4.2f, 0, time_ * 8 + float(seed), false, rgba(176, 92, 40, 255), x, y, z);
        break;
    }
    case WeaponClass::Debris:
        put(kDebris, Blend::Alpha, 4.8f, 0, time_ * 9 + float(seed), false, rgba(58, 50, 44, 255), x, y, z);
        put(kGlow, Blend::Add, 8 * flick, 0, 0, false, rgba(255, 140, 50, 150), x, y, z);
        break;
    default: break;
    }
}

}  // namespace d2k::render
