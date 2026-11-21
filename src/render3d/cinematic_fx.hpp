#pragma once
// "Cinematic" particles of the Reimagined 3D look (docs/3d-mode.md "Reimagined"; Settings::reimagined with
// Modern effects). Render-only: reads the World and the Modern effects' event log (render::Effects::events,
// smokeSources, dropInImpacts, lights), keeps its own particles and its own RNG, never touches the simulation
// or the game RNG.
//
// What it adds on top of the Modern effects (round 2, "effects v2"):
//   explosions  - a flash, a white-hot core and several overlapping fireball puffs that expand, rise and cool
//                 (temperature ramp in the shader) into dark smoke; spark streaks with gravity, some landing
//                 as glowing embers; lit debris chunks (big ones trail smoke); a shock dust ring and rolling
//                 dust hugging the ground; a scorch mark that fades over ~30 s; a smoke column that lingers
//                 (longer for big blasts); a short point light while the fireball burns (lights());
//   buildings   - death = a multi-stage explosion (several blasts over ~1.3 s) and a long smoke column;
//   artillery   - howitzer shells / grenades: a fountain of earth, clods, a brief flash, a drifting dust
//                 cloud, a shock ring and a crater (flash, sparks and dark smoke on armour) - no fireball;
//   impacts     - a sand burst on sand, grit + a few sparks on rock, sparks and a flash on armour;
//   weapons     - HDR muzzle stars, machine-gun tracers, glowing shell streaks, rocket smoke trails;
//   smoke       - burning wrecks / ruins / building fires send up big lit columns; damage smoke off vehicles and
//                 buildings (FxEvent::Smoke). Lit by sun, sky and fires, brown-black ageing to warm grey, the
//                 ground's dust in its foot (docs/3d-mode.md "Lit smoke"); the Modern effects' own smoke sprites
//                 of these are not drawn while the cinematic particles run (kFxBlast);
//   drop-in     - landing buildings raise a rolling ground dust ring (Effects::dropInImpacts);
//   vehicles    - dust trails behind moving ground vehicles: two plumes from the rear (one per track side),
//                 bigger / denser with speed, faint on rock, none on concrete (Settings "Ground dust");
//   placement   - a short dust ring billowing out of a new building's footprint edges when it appears
//                 (placed, MCV deployed; seen render-side by its build-up start - Settings "Ground dust");
//   wrecks      - a destroyed vehicle's hulk (render3d/wrecks.hpp, Settings "Vehicle wrecks"): a fireball half as
//                 big again that keeps cooking off, hot debris arcs, small flames and a thin sunlit smoke
//                 column on the hulk, a smoke trail behind its blown-off turret, scattered debris left behind.
//                 A shot-down flyer (aircraft crashes): a burst on the hull, fire and a black-to-grey smoke
//                 trail down to the ground, the impact's explosion, dust ring, debris and scorch, then the hulk.
// Drawn as GPU-instanced quads (one draw call, premultiplied alpha, ground marks first, the rest back to front),
// shaded procedurally from one tileable 3D noise texture (cinematic_msl.hpp). Budget: kMaxParticles, spawns thin
// out as it fills, the nearest-to-dying are replaced.

#include "render/fx_budget.hpp"
#include "render3d/camera3d.hpp"
#include "render3d/math3d.hpp"
#include "render3d/wrecks.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace d2k::game {
class World;
}
namespace d2k::render {
class Effects;
}

namespace d2k::render3d {

class Terrain;

class CinematicFx {
public:
    static constexpr int kMaxParticles = 7000;
    // The wind (world px/s, x east, z south): smoke and dust drift with it; the Dust haze veils move along it.
    static constexpr float kWindX = 13.0f, kWindZ = -6.0f;
    static std::unique_ptr<CinematicFx> create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth,
                                               SDL_GPUSampleCount samples);
    ~CinematicFx();
    CinematicFx(const CinematicFx&) = delete;
    CinematicFx& operator=(const CinematicFx&) = delete;

    void reset();
    // Settings "Ground dust" (docs/3d-mode.md "Vehicle dust trails", "Placement dust ring"): the dust trails and the
    // placement ring on / off; placementRing false while Drop-in raises its own landing dust (dropInImpacts).
    void setGroundDust(bool on, bool placementRing) {
        groundDust_ = on;
        placementRing_ = placementRing;
    }
    // Vehicle wrecks (render3d/wrecks.hpp; nullptr = off: a vehicle's death is the blast and the burning column
    // it always was).
    void setWrecks(Wrecks* w) { wrecks_ = w; }
    // Advance by dt effect seconds (0 = paused) and spawn from this frame's events. hidden(x, y): the ground
    // point is under the shroud / off the map (nothing is spawned or drawn there).
    using Hidden = std::function<bool(float, float)>;
    void update(const render::Effects& fx, const game::World& w, const Terrain& terr, float dt, const Hidden& hidden);
    // Units (and the player's selected buildings) this frame, for the see-through of smoke over them (effects
    // intensity, render/fx_budget.hpp): a point (world, y up) with a radius; sel = the player's selected one.
    struct UnitPin {
        Vec3 p;
        float r;
        bool sel;
    };
    std::vector<UnitPin>& unitPins() { return pins_; }

    // Building life (docs/3d-mode.md "Building life"): this frame's working stacks, vents and beacons of the
    // building models (Renderer3D finds them by their anchors and the buildings' state), spawned with this update's
    // effect time (call after update()). lamps = the time of day's lamp strength (the beacons light the ground at
    // night). Budgeted by the effects intensity like the other transient smoke.
    enum LifeKind : std::uint8_t { kLifeStack, kLifeSteam, kLifeVent, kLifeBeacon };
    struct LifeSource {
        float x, y, z;      // ground point (world px, x east, y south) and height above the terrain there
        float k;            // the anchor's size x the building's strength (health)
        std::uint32_t key;  // stable per anchor (spawn accumulators, beacon phase)
        LifeKind kind;
    };
    void buildingLife(const Terrain& terr, const LifeSource* src, int n, float lamps);

    // Point lights of this frame (burning fireballs, building deaths): ground point (x, y), height z above
    // it, radius, linear colour x intensity. For the renderer's light grid (Renderer3D::buildPointLights).
    struct Light {
        float x, y, z, radius, r, g, b;
    };
    void lights(std::vector<Light>& out) const;

    struct Frame {
        Mat4 viewProj;
        Vec3 eye, right, up, forward, sun;
        float sunCol[3] = {1, 1, 1}, ambCol[3] = {1, 1, 1};
        float fogColor[3] = {}, fogStart = 0, fogEnd = 0;
        float hazeCut = 0;    // Aerial haze: 1 - the fog's cap (0 = the fog as always; docs/3d-mode.md "Aerial haze")
        float projScale = 0;  // screen px per world px x view depth (D2K_FX_STATS)
    };
    // Sort and upload (its own copy pass on cmd; before the render pass), then draw inside it.
    // Two sets: the full-resolution one (ground marks, sparks, debris, muzzle stars: drawn in the scene's own
    // MSAA pass by draw()) and the half-resolution one (smoke / dust / fire puffs and big glows: most of the
    // pixels). The half set goes to its own pass (drawHalf: a half-size colour + depth target; the renderer
    // first fills that depth with the terrain, rocks and models) and is then blended over the resolved scene
    // (composite). Without that pass (halfOk false) draw() draws both sets in the scene pass.
    void prepare(SDL_GPUCommandBuffer* cmd, const Frame& f, const render::Effects& fx, const Terrain& terr,
                 const Hidden& hidden);
    int draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f);
    int count() const { return int(parts_.size()); }
    // The half-resolution pass: wanted this frame (prepare() found puffs); its targets for a w x h scene with
    // the depth format of the renderer's depth-only pipelines (false: not available - draw() draws the puffs).
    bool wantsHalf() const { return halfCount_ > 0 && texUploaded_ && vb_; }
    bool ensureHalf(int w, int h, SDL_GPUTextureFormat depthFmt);
    SDL_GPUTexture* halfColor() const { return halfColor_; }
    SDL_GPUTexture* halfDepth() const { return halfDepth_; }
    void setHalfActive(bool on) { halfActive_ = on; }  // the renderer runs the half pass this frame
    bool halfActiveThisFrame() const { return halfActive_; }
    const float* halfBounds() const { return halfBounds_; }  // world x0, z0, x1, z1 of the half set's puffs
    int drawHalf(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f);
    // Blends the half-resolution particles over `target` (the resolved scene; rw x rh used).
    void composite(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, int rw, int rh);

private:
    explicit CinematicFx(SDL_GPUDevice* dev) : dev_(dev) {}
    bool init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples);
    void bindCommon(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f, SDL_GPUGraphicsPipeline* pipe);

    // kShadow: never stored - the soft ground shadow of a ground-dust puff, an instance prepare() adds (Part::dust).
    enum Mode : std::uint8_t { kPuff = 0, kSpark = 1, kFlash = 2, kDebris = 3, kScorch = 4, kRing = 5, kShadow = 6 };
    struct Part {
        Vec3 p, v;
        Vec3 dir;  // flashes: the shot's direction (muzzle stars point along it)
        float age = 0, life = 1;
        float size0 = 8, size1 = 8;
        float rot = 0, vrot = 0;
        float drag = 0, gravity = 0, buoy = 0;  // px/s^2 (gravity down, buoyancy up)
        float wind = 0;                         // share of the wind it drifts with
        float r0 = 0.3f, g0 = 0.3f, b0 = 0.3f, r1 = 0.3f, g1 = 0.3f, b1 = 0.3f;  // albedo at birth / death
        float a = 1, fadeIn = 0.1f, fadeOut = 0.5f;  // peak alpha; fade-in / fade-out share of the life
        float er = 0, eg = 0, eb = 0;                // emission tint x intensity
        float heatLife = 0;                          // s: heat 1 -> 0 (fire temperature, glow); 0 = always 1
        float stretch = 0;                           // sparks: streak length per px/s
        float erode0 = 0.2f, erode1 = 0.55f;         // puffs: edge erosion at birth / death
        float scale = 1;                             // puffs: noise scale (finer for dust)
        float soft = 0.3f;                           // puffs: soft fade against the ground (x size)
        float seed = 0;
        float rays = 0;                              // flashes: muzzle star rays
        float soot = 0;                              // puffs: 0 dust .. 1 black smoke (darker at its base, lit by fire)
        float trail = 0, trailAcc = 0;               // debris: a smoke puff every `trail` px
        float ch[4] = {};                            // ground marks: corner heights relative to p.y
        float cut = 0;                               // effects intensity: transient puff fades out by this age (0 = no cap)
        float clear = 0;                             // effects intensity: see-through over a unit (eased)
        float dust = 0;                              // puffs: ground dust (Settings "Ground dust": trails, placement
                                                     // ring) - plume lighting + a soft ground shadow; 0 = other puffs
        float gmix = 0, gr = 0, gg = 0, gb = 0;      // smoke: the ground's dust mixed in near the ground (share, colour)
        std::uint32_t id = 0;                        // spawn number (add): the draw order's tie-break
        Mode mode = kPuff;
        bool bounced = false, ember = false, landed = false;
    };
    std::vector<Part> parts_;
    float rnd();
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    std::uint32_t rng_ = 0x2545F491u;
    std::uint32_t nextId_ = 0;
    float load() const;  // 1 .. 0 as the budget fills
    Part& add(const Part& p);  // (the stored particle: valid until the next add; a thinned-out one: a scratch copy)
    // Effects intensity (render/fx_budget.hpp; a copy of the Modern effects' budget, refreshed every update).
    render::FxBudget budget_;
    float areaK_ = 1;
    bool lasting_ = false;     // spawning a lasting column (burning wreck / building): not budgeted as an event
    bool onVehicle_ = false;   // the explosion being spawned hit a ground vehicle (its column rises off the hull)
    bool hulkBlast_ = false;   // ... is a hulk's (vehicle wrecks): its soot clears sooner, the hulk smokes by itself
    std::uint32_t thinN_ = 0;  // thinning hash counter (not the RNG)
    Part scratch_;             // add()'s answer for a thinned-out particle
    std::vector<UnitPin> pins_;
    bool budgetSpawn(Part& p);  // false = thinned out
    void seeThrough(const Frame& f);  // eases every puff's `clear` (pins_)
    Part puff();          // defaults of a puff (random seed / rotation)
    void groundMark(Part& p, const Terrain& terr);
    // Ground marks follow the terrain (docs/3d-mode.md "Ground marks on slopes"): one instance per cell of an
    // n x n grid over the mark's rotated square at its drawn size, each with its own corner heights (extra.y =
    // n * 256 + row * 16 + column tells the shader which part of the mark it is). n = 1 where the ground under the
    // mark is flat enough for one quad. Appends to inst_, returns the instances added. vals: the whole mark's
    // instance (posSize.y = baseY).
    int emitGroundMark(const float* vals, const Terrain& terr);
    void explosion(const Terrain& terr, float x, float y, float z, float k, bool sand, bool ground, bool scorch = true);
    static bool groundVehicleAt(const game::World& w, float x, float y);
    void impact(const Terrain& terr, float x, float y, float z, float s, bool sand, bool armour);
    void artillery(const Terrain& terr, float x, float y, float z, float k, bool sand, bool armour);
    void muzzle(const Terrain& terr, float x, float y, float z, float dx, float dy, int weapon);
    void tracer(const Terrain& terr, float x, float y, float z, float tx, float ty, float tz);
    void dropIn(const Terrain& terr, float x0, float y0, float x1, float y1, std::uint32_t sand, bool fromGround);
    // Smoke by kind (docs/3d-mode.md "Lit smoke"): soot of a blast / a burning ruin (brown-black, ageing to warm
    // grey), a burning building's dense brown-grey column, a hulk's thinner column, a damaged vehicle's thin grey wisp.
    // Building life: a working stack's thin light-grey wisp, a power plant's faint white steam, a vent's heat puff.
    enum Smoke : std::uint8_t { kSoot, kBuildingFire, kWreckSmoke, kDamageSmoke, kStackWisp, kSteam, kVentHeat };
    Part& smokePuff(const Terrain& terr, float x, float y, float z, float k, float darkness, float lifeMul = 1,
                    Smoke kind = kSoot);
    static void sootColour(Part& p, float darkness);  // albedo at birth / death of soot (1 = fresh black-brown)
    static float fireballRadius(float k);
    // embers: s the fresh crater glows (default 2 + 1.5 sqrt(k); artillery: a brief glow, no fire).
    void scorchMark(const Terrain& terr, float x, float y, float size, float k, bool sand, float embers = -1);
    void dustPuff(const Terrain& terr, float x, float y, float k, float r, float g, float b, float alpha);
    // Ground dust (Settings "Ground dust", scaled by Effects intensity; 0 = off).
    float groundDustK() const;
    void vehicleDust(const render::Effects& fx, const game::World& w, const Terrain& terr, float dt, const Hidden& hidden);
    // The colour of the dust the ground raises at (x, y) (spice orange-rust, sand / dunes warm sand, rock grey-brown
    // grit; bilinear over the cell centres), lighter and a little less saturated than the ground, as airborne dust.
    struct DustTint {
        float r, g, b;
    };
    static DustTint dustTint(const game::World& w, float x, float y);
    // The ground's dust at (x, y) (dustTint; the old sand / rock pair without a world) and a smoke puff taking it up.
    DustTint groundDust(float x, float y, bool sand) const;
    void mixGround(Part& p, float share) const;
    void trailDust(const Terrain& terr, float x, float y, float dirX, float dirY, float side, float sk, float amount, bool sand,
                   const DustTint& tint);
    void placementRings(const game::World& w, const Terrain& terr, const Hidden& hidden);
    void placementRing(const game::World& w, const Terrain& terr, float x0, float y0, float x1, float y1, bool sand);
    void trailPuff(const Vec3& p, float k, bool rocket);
    void addBurner(float x, float y, float z, float k, float life);
    // Vehicle wrecks: a hulk's blast (k = its death explosion's size), then its fire while it lasts.
    void wreckBlast(const Terrain& terr, const Wrecks::Hulk& h, float k, bool sand);
    void burnWrecks(const Terrain& terr, float dt, float ld, const Hidden& hidden);
    void wreckDebris(const Vec3& c, int sz, const Vec3& v0, int hot, int left);
    // Aircraft crashes (render3d/wrecks.hpp): the hit in the air, the fire and smoke it drags down, the impact.
    void crashHit(const Terrain& terr, const Wrecks::Hulk& h);
    void crashTrail(Wrecks::Hulk& h, float dt, float ld);
    void crashImpact(const Terrain& terr, const Wrecks::Hulk& h);
    Wrecks* wrecks_ = nullptr;
    const game::World* world_ = nullptr;  // the world of this update (dustTint of the spawns); never kept across frames

    // Burners: smoke columns of wrecks and ruins (deaths), with their own life.
    struct Burner {
        float x, y, z, k, age, life, acc;
    };
    std::vector<Burner> burners_;
    // Blasts still to come (a building's death goes off in stages).
    struct Pending {
        float delay, x, y, z, k;
        bool sand;
    };
    std::vector<Pending> pending_;
    // Fireball lights (lights()).
    struct Glow {
        float x, y, z, radius, r, g, b, age, life;
    };
    std::vector<Glow> glows_;
    std::unordered_map<std::uint32_t, float> smokeAcc_;   // building fire points (Effects::smokeSources)
    std::unordered_map<std::uint32_t, float> lifeAcc_;    // building life: spawn accumulators per anchor
    std::vector<Light> beaconLights_;                     // building life: the beacons lit this frame
    double lifeClock_ = 0;                                // building life: effect seconds (the beacons' blink)
    struct Track {
        float x, y, acc;
        std::uint32_t frame;
        float speed = 0, dirX = 1, dirY = 0;  // drawn speed (px/s, eased) and heading of the motion
        std::uint32_t n = 0;                  // puffs so far (alternates the two plumes)
    };
    std::unordered_map<std::uint32_t, Track> tracks_;     // ground vehicles: last position (dust trails)
    struct Shell {
        Vec3 p;
        float acc;
        std::uint32_t frame;
    };
    std::unordered_map<std::uint32_t, Shell> shells_;     // projectiles in flight: last position (trails)
    // Placement rings: the buildings seen (side << 16 | index -> timeAdded + 1); primed after the first look at a
    // world, so the buildings already standing at a mission start / load raise no dust.
    std::unordered_map<std::uint32_t, std::uint32_t> seenBuildings_;
    bool buildingsPrimed_ = false;
    float ringDemoT_ = 0;  // D2K_RING_DEMO
    bool groundDust_ = true, placementRing_ = true;
    std::uint32_t frame_ = 0;
    std::uint32_t lastTick_ = 0;
    float stall_ = 0;
    float dt_ = 0;  // this frame's effect time step
    Vec3 wind_{kWindX, 0.0f, kWindZ};

    // GPU.
    SDL_GPUDevice* dev_;
    SDL_GPUGraphicsPipeline* pipe_ = nullptr;      // scene pass (MSAA, the scene's formats)
    SDL_GPUGraphicsPipeline* halfPipe_ = nullptr;  // half-resolution pass (1x, halfFmt_ + halfDepthFmt_)
    SDL_GPUGraphicsPipeline* compPipe_ = nullptr;  // composite over the resolved scene
    SDL_GPUTextureFormat colorFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID, halfFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID,
                         halfDepthFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTexture* halfColor_ = nullptr;
    SDL_GPUTexture* halfDepth_ = nullptr;
    SDL_GPUSampler* linear_ = nullptr;
    int halfW_ = 0, halfH_ = 0;
    bool halfFailed_ = false, halfActive_ = false;
    std::uint32_t mainCount_ = 0, halfCount_ = 0;
    float halfBounds_[4] = {1, 1, 0, 0};  // instances: [0, mainCount_) scene pass, then the half set
    SDL_GPUTexture* tex_ = nullptr;  // 3D noise (kNoiseSize^3, RGBA8: value + gradient)
    SDL_GPUSampler* samp_ = nullptr;
    SDL_GPUBuffer* vb_ = nullptr;
    SDL_GPUTransferBuffer* tb_ = nullptr;
    std::uint32_t cap_ = 0, drawn_ = 0;
    // The noise is generated on a worker thread at start-up; nothing is drawn until it is uploaded.
    std::vector<std::uint32_t> texCpu_;
    std::thread texThread_;
    std::atomic<bool> texBuilt_{false};
    bool texUploaded_ = false;
    std::vector<float> inst_;
    // Draw order: set (0 scorch, 1 ring, 2 the rest of the scene pass, 3 the half-resolution set), then back to
    // front, then the spawn number (stable from frame to frame: overlapping puffs at the same depth never swap).
    struct Order {
        std::uint32_t set;
        float depth;
        std::uint32_t id;
        int idx;
    };
    std::vector<Order> order_;
};

}  // namespace d2k::render3d
