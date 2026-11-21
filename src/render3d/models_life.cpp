// Building life (docs/3d-mode.md "Building life"; Reimagined + Modern effects): the moving parts of building models,
// found by their node names, and the idle scan of building turrets. Render-only: the World is read, never changed;
// no game RNG (the per-building phase is a hash of the building's slot).

#include "render3d/models.hpp"

#include "game/world.hpp"
#include "render/lighting.hpp"
#include "render3d/cinematic_fx.hpp"
#include "render3d/model_anim.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace d2k::render3d {

namespace {

constexpr float kTwoPi = 6.28318530717959f;

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Name convention -> kind and rate (rad/s at full speed). Rates stay well below a half blade spacing per frame at
// 60 fps so the blades never strobe backwards.
bool lifeKind(const std::string& nm, ModelRenderer::LifeKind& kind, float& rate) {
    using M = ModelRenderer;
    if (starts(nm, "turbine")) return kind = M::kLifePower, rate = 5.0f, true;
    if (starts(nm, "fan")) return kind = M::kLifePower, rate = 8.0f, true;
    if (starts(nm, "rotor") || starts(nm, "propeller")) return kind = M::kLifePower, rate = 7.0f, true;
    if (starts(nm, "anemometer")) return kind = M::kLifeWind, rate = 4.5f, true;
    if (starts(nm, "vane")) return kind = M::kLifeVane, rate = 0.0f, true;
    if (starts(nm, "dish") || starts(nm, "radar")) return kind = M::kLifeDish, rate = 0.9f, true;
    return false;
}

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Rotation about node local axis `axis` by `a` (column-major).
void axisRot(int axis, float a, float out[16]) {
    const float c = std::cos(a), s = std::sin(a);
    for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;  // u -> v by +a
    out[u * 4 + u] = c;
    out[u * 4 + v] = s;
    out[v * 4 + u] = -s;
    out[v * 4 + v] = c;
}

float ease(float cur, float target, float dt, float tau) { return cur + (target - cur) * (1.0f - std::exp(-dt / tau)); }

}  // namespace

// Once per model at the upload (before MeshPart::slim drops the vertices).
void ModelRenderer::buildLife(int mi) {
    if (life_.size() < models_.size()) life_.resize(models_.size());
    Life& L = life_[std::size_t(mi)];
    if (L.built) return;
    L.built = true;
    const formats::Model3D& m = models_[std::size_t(mi)];
    if (m.skeleton.empty()) return;
    bool isBuilding = false;
    for (int bm : buildingModel_) isBuilding = isBuilding || bm == mi;
    if (!isBuilding) return;
    const std::size_t n = std::min<std::size_t>(m.skeleton.nodes.size(), 255);
    std::vector<float> world;
    restWorld(m.skeleton, world);
    std::vector<int> spinOf(n, -1);  // node -> spinner index
    for (std::size_t i = 0; i < n; ++i) {
        std::string nm = m.skeleton.nodes[i].name;
        for (char& c : nm) c = char(std::tolower(static_cast<unsigned char>(c)));
        Spinner s;
        if (!lifeKind(nm, s.kind, s.rate)) continue;
        mat4Mul(m.skeleton.place, &world[i * 16], s.rest);
        if (!mat4Inverse(s.rest, s.restInv)) continue;
        // The vertices this node moves (itself; children are handled through `owner`), in node space.
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        double cx = 0, cz = 0, area = 0;  // the vane: area-weighted centre in the node's horizontal plane
        int count = 0;
        for (const formats::MeshPart* part : {&m.body, &m.house}) {
            const auto& tris = part->tris;
            for (std::size_t k = 0; k + 2 < tris.size(); k += 3) {
                float q[3][3];
                bool mine = true;
                for (int c = 0; c < 3; ++c) {
                    const auto& v = tris[k + std::size_t(c)];
                    if (v.joint[0] != i || v.weight[0] < 128) mine = false;
                    const float* r = s.restInv;
                    for (int a = 0; a < 3; ++a)
                        q[c][a] = r[a] * v.pos[0] + r[4 + a] * v.pos[1] + r[8 + a] * v.pos[2] + r[12 + a];
                }
                if (!mine) continue;
                for (int c = 0; c < 3; ++c)
                    for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], q[c][a]), hi[a] = std::max(hi[a], q[c][a]);
                const double ar = std::abs(double((q[1][0] - q[0][0]) * (q[2][2] - q[0][2]) - (q[2][0] - q[0][0]) * (q[1][2] - q[0][2]))) * 0.5;
                cx += ar * double(q[0][0] + q[1][0] + q[2][0]) / 3.0;
                cz += ar * double(q[0][2] + q[1][2] + q[2][2]) / 3.0;
                area += ar;
                ++count;
            }
        }
        if (count == 0) continue;
        const float ext[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
        if (s.kind == kLifePower) {
            // The convention: the spin axis is the node's local +Y. A part modelled otherwise (thin along another
            // local axis, a fan disc) spins about its thinnest axis. Its vertices must lie round the axis (the
            // origin on it), else it is no rotor (a pipe run named "turbine") and stays still.
            int ax = 1;
            for (int a : {0, 2})
                if (ext[a] < 0.5f * ext[1] && ext[a] < ext[ax]) ax = a;
            const int u = (ax + 1) % 3, v = (ax + 2) % 3;
            const float rad = 0.5f * std::max(ext[u], ext[v]);
            const float off = std::hypot(0.5f * (hi[u] + lo[u]), 0.5f * (hi[v] + lo[v]));
            if (rad < 1e-4f || off > 0.3f * rad) continue;  // (node space: glTF units)
            s.axis = ax;
        }
        if (s.kind == kLifeVane) {
            // Turned so its tail (the bigger area: the fin) points downwind. The rest angle of the tail in the
            // node's x-z plane and the wind's, the rotation by a about +Y takes angle phi to phi - a.
            const float tx = area > 0 ? float(cx / area) : 0.0f, tz = area > 0 ? float(cz / area) : 0.0f;
            const float* r = s.restInv;
            const float wx = CinematicFx::kWindX, wz = CinematicFx::kWindZ;  // model space = world (buildings do not turn)
            const float lx = r[0] * wx + r[8] * wz, lz = r[2] * wx + r[10] * wz;
            s.phase = (std::hypot(tx, tz) > 1e-4f ? std::atan2(tz, tx) : 0.0f) - std::atan2(lz, lx);
        } else {
            s.phase = float(hash32(std::uint32_t(i) * 2654435761u + std::uint32_t(mi)) & 0xFFFF) / 65536.0f * kTwoPi;
        }
        s.node = int(i);
        spinOf[i] = int(L.spin.size());
        L.dish = L.dish || s.kind == kLifeDish;
        L.spin.push_back(s);
    }
    if (L.spin.empty()) return;
    // Outer spinners first; per node the nearest spinner up the hierarchy (itself included); the outer of each.
    auto depth = [&](const Spinner& s) {
        int d = 0;
        for (int p = m.skeleton.nodes[std::size_t(s.node)].parent; p >= 0 && p < int(n); p = m.skeleton.nodes[std::size_t(p)].parent)
            d += spinOf[std::size_t(p)] >= 0 ? 1 : 0;
        return d;
    };
    std::stable_sort(L.spin.begin(), L.spin.end(), [&](const Spinner& a, const Spinner& b) { return depth(a) < depth(b); });
    for (std::size_t k = 0; k < L.spin.size(); ++k) spinOf[std::size_t(L.spin[k].node)] = int(k);
    L.owner.assign(n, -1);
    for (std::size_t i = 0; i < n; ++i)
        for (int p = int(i); p >= 0 && p < int(n); p = m.skeleton.nodes[std::size_t(p)].parent)
            if (spinOf[std::size_t(p)] >= 0) {
                L.owner[i] = spinOf[std::size_t(p)];
                break;
            }
    for (Spinner& s : L.spin)
        for (int p = m.skeleton.nodes[std::size_t(s.node)].parent; p >= 0 && p < int(n); p = m.skeleton.nodes[std::size_t(p)].parent)
            if (spinOf[std::size_t(p)] >= 0) {
                s.outer = spinOf[std::size_t(p)];
                break;
            }
    std::string names;
    for (const Spinner& s : L.spin) names += (names.empty() ? "" : ", ") + m.skeleton.nodes[std::size_t(s.node)].name;
    SDL_Log("3D models: %s: building life: %s", m.name.c_str(), names.c_str());
}

// The frame's clock: real seconds, frozen when no game tick came for a while (paused), as the cinematic particles.
void ModelRenderer::lifeFrame(const ModelFrame& f) {
    lifeDt_ = 0;
    if (!f.buildingLife || lifeOff_ || !f.world) return;
    const game::World& w = *f.world;
    float dt = std::clamp(f.frameDt, 0.0f, 0.1f);
    if (w.gameTicks() < lifeTick_) lifeState_.clear();  // a new world (restart, load)
    if (w.gameTicks() == lifeTick_) {
        lifeStall_ += dt;
        if (lifeStall_ > 0.2f) dt = 0;
    } else {
        lifeStall_ = 0;
        lifeTick_ = w.gameTicks();
    }
    lifeDt_ = dt;
    lifeClock_ += double(dt);
    const std::size_t want = w.sides().size() * std::size_t(kLifePool);
    if (lifeState_.size() != want) lifeState_.assign(want, LifeState{});
}

int ModelRenderer::buildingLife(const game::World& w, int si, int bi, const game::Building& b, int mi, bool building,
                                bool turret, float& turretOff) {
    turretOff = 0;
    const std::size_t slot = std::size_t(si) * std::size_t(kLifePool) + std::size_t(bi);
    if (slot >= lifeState_.size() || b.type < 0 || b.type >= formats::kMaxBuildingTypes) return -1;
    const bool hasSpin = std::size_t(mi) < life_.size() && !life_[std::size_t(mi)].spin.empty();
    if (!hasSpin && !turret) return -1;
    LifeState& st = lifeState_[slot];
    const bool fresh = st.type != b.type || st.timeAdded != b.timeAdded;
    if (fresh) {
        st = LifeState{};
        st.type = b.type;
        st.timeAdded = b.timeAdded;
        st.facing = b.facing;
    }
    const float dt = lifeDt_;
    const auto& t = t_.bin.buildings[b.type];
    const auto& sd = w.sides()[std::size_t(si)];
    const float hk = t.hitPoints > 0 ? std::clamp(float(b.health) / float(t.hitPoints), 0.0f, 1.0f) : 1.0f;
    const float power = sd.powerDemand > 0 ? std::clamp(float(sd.powerPercent) / 100.0f, 0.0f, 1.0f) : 1.0f;
    // Power spinners: a power plant runs with its health (its output, World::updatePower), the others' fans also
    // with the base's power; nothing turns while it is built up / sold / dropping in / assembling.
    const float target = building ? 0.0f : (0.3f + 0.7f * hk) * (t.powerConsumption < 0 ? 1.0f : power);
    const float targetW = building ? 0.0f : 1.0f;
    // The dish turns while the original animates it (the outpost's radar: the animation flags, at least half health).
    const bool dishOn = !building && (b.flags & 0x30) && (b.flags & 0x40) && b.health * 2 >= std::int32_t(t.hitPoints);
    const float targetD = dishOn ? 1.0f : 0.0f;
    if (fresh && !building) {
        st.speed = target, st.speedW = targetW, st.speedD = targetD;
    } else {
        st.speed = ease(st.speed, target, dt, target > st.speed ? 2.2f : 1.4f);
        st.speedW = ease(st.speedW, targetW, dt, 1.5f);
        st.speedD = ease(st.speedD, targetD, dt, 0.8f);
    }
    const float clock = float(std::fmod(lifeClock_, 3600.0));
    const float gust = 1.0f + 0.3f * std::sin(0.45f * clock + 0.7f * float(bi)) * std::sin(0.17f * clock + float(si));
    st.phi += double(st.speed * dt);
    st.phiW += double(st.speedW * gust * dt);
    st.phiD += double(st.speedD * dt);
    // Turret: a slow look-around once its game facing has not changed for a few seconds (no target, powered when
    // the turret needs power); any turn of the game facing hands the turret straight back to the game.
    if (turret) {
        if (b.facing != st.facing) st.facing = b.facing, st.idle = 0;
        else st.idle += dt;
        const bool powered = !t.requireEnoughPower || sd.powerPercent >= 100;
        const bool idle = !building && powered && b.state == 2 && st.idle > 3.0f;
        float tgt = 0;
        if (idle) {
            const float ph = 0.28f * clock + float(hash32(std::uint32_t(slot)) & 0xFF) * 0.0245f;
            const float sw = std::clamp(1.5f * std::sin(ph) + 0.25f * std::sin(2.3f * ph), -1.0f, 1.0f);  // dwells at the ends
            tgt = 0.5f * sw * std::clamp((st.idle - 3.0f) / 2.0f, 0.0f, 1.0f);
        }
        st.turret = ease(st.turret, tgt, dt, idle ? 0.9f : 0.08f);
        if (!idle && std::abs(st.turret) < 1e-3f) st.turret = 0;
        turretOff = st.turret;
    }
    // D2K_LIFE_LOG=1: the state of every animated building, every 6th frame (checks).
    static const bool logLife = [] {
        const char* e = std::getenv("D2K_LIFE_LOG");
        return e && e[0] == '1';
    }();
    if (logLife && w.gameTicks() % 2 == 0 && dt > 0)
        SDL_Log("life: side %d bld %d type %d clock %.3f dt %.4f speed %.2f phi %.3f wind %.3f dish %.3f turret %.3f idle %.1f",
                si, bi, b.type, lifeClock_, double(dt), double(st.speed), st.phi, st.phiW, st.phiD, double(st.turret),
                double(st.idle));
    if (!hasSpin || animOff_) return -1;
    const Life& L = life_[std::size_t(mi)];
    const formats::Model3D& m = models_[std::size_t(mi)];
    const std::size_t n = std::min<std::size_t>(m.skeleton.nodes.size(), 255);
    const int base = int(paletteData_.size() / 16);
    static const float kId[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (std::size_t i = 0; i < n; ++i) paletteData_.insert(paletteData_.end(), kId, kId + 16);
    constexpr std::size_t kMaxSpin = 32;
    float comp[kMaxSpin][16];
    const float slotPhase = float(hash32(std::uint32_t(slot) * 0x9E3779B9u) & 0xFFFF) / 65536.0f * kTwoPi;
    for (std::size_t k = 0; k < L.spin.size() && k < kMaxSpin; ++k) {
        const Spinner& s = L.spin[k];
        float a = 0;
        switch (s.kind) {
        case kLifePower: a = s.phase + slotPhase + float(std::fmod(double(s.rate) * st.phi, double(kTwoPi))); break;
        case kLifeWind: a = s.phase + slotPhase + float(std::fmod(double(s.rate) * st.phiW, double(kTwoPi))); break;
        case kLifeDish: a = s.phase + float(std::fmod(double(s.rate) * st.phiD, double(kTwoPi))); break;
        case kLifeVane:  // into the wind, swinging a little with the gusts
            a = s.phase + st.speedW * (0.16f * std::sin(0.9f * clock + slotPhase) + 0.07f * std::sin(2.6f * clock + 1.7f * slotPhase));
            break;
        }
        float r[16], t0[16], local[16];
        axisRot(s.axis, a, r);
        mat4Mul(s.rest, r, t0);
        mat4Mul(t0, s.restInv, local);
        if (s.outer >= 0 && std::size_t(s.outer) < k) mat4Mul(comp[s.outer], local, comp[k]);
        else std::copy(local, local + 16, comp[k]);
    }
    for (std::size_t i = 0; i < n && i < L.owner.size(); ++i) {
        const int o = L.owner[i];
        if (o < 0 || std::size_t(o) >= kMaxSpin) continue;
        std::copy(comp[o], comp[o] + 16, paletteData_.begin() + std::ptrdiff_t((std::size_t(base) + i) * 16));
    }
    return base;
}

// The effects of building life (CinematicFx::buildingLife): per visible, standing building whose model has life anchors -
//   fx_smoke* (else fx_flue*, but not on a refinery: its flues already flare and smoke while it unloads, the Modern
//     effects' refinery vent): a stack wisp while the building works (production holds the animation flag
//     permanent), a power plant's faint steam while it stands;
//   fx_vent*: a faint warm puff while it works / produces power;
//   fx_beacon*: a slow red blink (a small light at night).
void Renderer3D::buildingLifeFx(const game::World& w, const Options& o) {
    enum : std::int8_t { kSmoke = 1, kFlue = 2, kVent = 4, kBeacon = 8 };
    if (!models_ || !cine_ || !terrainBuilt_) return;
    if (lifeMaskOf_ != models_ || w.gameTicks() < lifeMaskTick_ || lifeMask_.size() != std::size_t(formats::kMaxBuildingTypes)) {
        lifeMask_.assign(std::size_t(formats::kMaxBuildingTypes), -1);
        lifeMaskOf_ = models_;
    }
    lifeMaskTick_ = w.gameTicks();
    lifeSrc_.clear();
    constexpr std::size_t kMaxSources = 48;  // anchors per frame (a big base on screen: the nearest ones first come)
    const float vw = float(cam_.viewW()), vh = float(cam_.viewH());
    for (std::size_t si = 0; si < w.sides().size(); ++si) {
        const auto& side = w.sides()[si];
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const game::Building& b = side.buildings[std::size_t(bi)];
            if (!b.alive || b.type < 0 || b.type >= formats::kMaxBuildingTypes || b.state == 17) continue;
            std::int8_t& mask = lifeMask_[std::size_t(b.type)];
            if (mask < 0 || (mask == 0 && w.gameTicks() % 32 == 0)) {  // (0: maybe read later - looked at again now and then)
                if (!models_->hasBuildingModel(b.type)) continue;  // (not read yet: looked at again later)
                mask = 0;
                if (models_->buildingAnchors(w, terrain_, int(si), bi, "fx_smoke", lifeAnchors_)) mask |= kSmoke;
                if (models_->buildingAnchors(w, terrain_, int(si), bi, "fx_flue", lifeAnchors_)) mask |= kFlue;
                if (models_->buildingAnchors(w, terrain_, int(si), bi, "fx_vent", lifeAnchors_)) mask |= kVent;
                if (models_->buildingAnchors(w, terrain_, int(si), bi, "fx_beacon", lifeAnchors_)) mask |= kBeacon;
            }
            if (mask == 0 || (b.flags & 0x6)) continue;  // (building up / selling: nothing runs yet)
            const auto& t = t_.bin.buildings[b.type];
            const float cx = float(b.tileX * 32) + float(t.artWidth) * 0.5f, cy = float(b.tileY * 32) + float(t.artHeight) * 0.5f;
            if (pointHidden(cx, cy)) continue;
            float sx, sy;
            if (!cam_.project(Vec3{cx, terrain_.height(cx, cy), cy}, sx, sy) || sx < -200 || sy < -200 || sx > vw + 200 || sy > vh + 300)
                continue;
            const float hk = t.hitPoints > 0 ? std::clamp(float(b.health) / float(t.hitPoints), 0.0f, 1.0f) : 1.0f;
            const bool producer = t.powerConsumption < 0;
            const bool working = (b.flags & game::building_flags::kAnimPermanent) != 0;
            const bool refinery = t.specialBehavior == game::building_behaviour::kRefinery;
            auto emit = [&](const char* prefix, CinematicFx::LifeKind kind, float k, std::uint32_t salt) {
                if (!models_->buildingAnchors(w, terrain_, int(si), bi, prefix, lifeAnchors_)) return;
                for (std::size_t i = 0; i < lifeAnchors_.size() && lifeSrc_.size() < kMaxSources; ++i) {
                    const auto& a = lifeAnchors_[i];
                    const std::uint32_t key = (std::uint32_t(si) << 26) ^ (std::uint32_t(bi) << 12) ^ (salt << 6) ^ std::uint32_t(i) ^
                                              (b.timeAdded * 2654435761u);
                    lifeSrc_.push_back({a.pos.x, a.pos.z, a.pos.y - terrain_.height(a.pos.x, a.pos.z), a.size * k, key, kind});
                }
            };
            if (producer || working) {
                const CinematicFx::LifeKind kind = producer ? CinematicFx::kLifeSteam : CinematicFx::kLifeStack;
                const float k = 0.5f + 0.5f * hk;
                if (mask & kSmoke) emit("fx_smoke", kind, k, 1);
                else if ((mask & kFlue) && !refinery) emit("fx_flue", kind, k, 2);
                if (mask & kVent) emit("fx_vent", CinematicFx::kLifeVent, k, 3);
            }
            if (mask & kBeacon) emit("fx_beacon", CinematicFx::kLifeBeacon, 1.0f, 4);
        }
    }
    const float lamps = o.light && !o.light->identity ? o.light->lamps : 0.0f;
    cine_->buildingLife(terrain_, lifeSrc_.data(), int(lifeSrc_.size()), lamps);
}

}  // namespace d2k::render3d
