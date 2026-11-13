#pragma once
// Modern effects for the Enhanced graphics mode (Settings::effects = Modern): a render-side
// particle system (fireballs, smoke plumes, sparks, debris, dust trails, sand spray,
// rocket smoke, sonic pressure waves, deviator gas in the firer's house colour, devastator
// plasma), modern weapon effects that REPLACE the original projectile / explosion sprites
// (muzzle flashes, tracers, rockets, shells, impacts, layered explosions, scorch marks,
// shockwaves, burning wrecks; replacesExplosion / replacesBullet), dynamic lights (the light
// list, see Light below), tread marks, screen shake, smooth visual turning, plus a light vignette.
//
// Purely presentational: it only reads the World (new explosions / bullets are detected by
// comparing the object slots with the previous frame), keeps its own state and its own RNG, and
// never touches the simulation or the game RNG. Time is "effect seconds" driven by the game clock
// (game ticks / 20, i.e. real seconds at the default game speed), so pausing freezes it and the
// game speed setting speeds it up together with the sprite animations.

#include "formats/bintables.hpp"
#include "formats/templates.hpp"
#include "game/world.hpp"
#include "render/effect_anchors.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Vertex;

namespace d2k::render {

struct Camera;
class TileAtlas;

class Effects {
public:
    Effects(SDL_Renderer* r, const formats::Templates& templates);
    ~Effects();
    Effects(const Effects&) = delete;
    Effects& operator=(const Effects&) = delete;

    void reset();
    // House colour ramps (COLOURS.BIN) for house-tinted effects (the deviator's gas).
    void setHouseColours(const formats::HouseColours& c);
    // Observe the world and advance by dt effect seconds (0 = paused: nothing moves, nothing new
    // is spawned). The world must hold the positions that are drawn this frame.
    void update(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                bool showShroud);

    // Layers (rendering order in MapRenderer::draw):
    void drawGround(const Camera& cam);  // tread marks + low dust, after terrain/decals
    void drawAir(const Camera& cam);     // particles and light glows, after projectiles, before shroud
    void drawPost(int viewW, int viewH); // vignette / grade, after shroud
    // Time of day (render/lighting.hpp): the 2D grade the battlefield got, applied to the alpha-
    // blended particles (smoke, dust) drawn after it; nullptr = none. Light / additive ones glow.
    void setGrade(const float* rgb) {
        graded_ = rgb != nullptr;
        if (rgb) for (int i = 0; i < 3; ++i) grade_[i] = rgb[i];
    }

    // 3D: the tilt (radians from vertical) of the camera the building cards face, for effects
    // anchored on building art (the refinery chimneys); < 0 = the 2D view. Set before update().
    void setCardTilt(float tilt) { cardTilt_ = tilt; }
    // 3D with models: the models' named anchors (render/effect_anchors.hpp) place effects that
    // leave a part of an object (the refinery's flue tops: "fx_flue*"); nullptr = the 2D offsets.
    // Set before update().
    void setAnchors(const EffectAnchors* a) { anchors_ = a; }

    // Screen shake for this frame, world pixels (add to the camera).
    float shakeX() const { return shakeX_; }
    float shakeY() const { return shakeY_; }

    // Smoothed presentation of a ground vehicle (nullptr if not tracked).
    struct UnitVisual {
        std::int32_t key = -1;          // type + 1; -1 = slot not seen
        float facing = 0, turret = 0;   // 0..32 (32-way facing units, eased)
        float x = 0, y = 0;             // last drawn position (world px)
        float treadDist = 0, dustDist = 0;
        float smokeT = 0;
        bool moving = false;
    };
    const UnitVisual* unitVisual(int side, int index) const;

    // ---- 3D bridge (render3d::Renderer3D draws these in world space; read-only) ----
    // kWave: a thin bright rim with a faint interior (sonic pressure waves); kGas: a soft,
    // billowing puff with a feathered edge (deviator gas).
    // kArc: a crescent (bright outer rim fading inward) on the +x side, for wavefronts; kLine: a
    // soft line across v, solid along u (beam strands, joined end to end).
    // kFire: a billowing fireball puff (hot core); kMuzzle: a muzzle flash star pointing along +x
    // (bright at the muzzle end, -x); kStreak: a tracer (bright head at +x, fading tail);
    // kCapsule: a rounded body along u (rockets, shells, bombs); kScorch: an irregular burnt
    // blotch; kShock: a thin sharp ring (shockwaves); kMound: a grainy dome lit from the north-west
    // (spice bloom mounds).
    enum Tex : std::uint8_t {
        kGlow, kSmoke, kSpark, kDebris, kFlame, kRing, kSoft, kGlowPM, kWave, kGas, kArc, kLine,
        kFire, kMuzzle, kStreak, kCapsule, kScorch, kShock, kMound, kTexCount
    };
    // Alpha: normal blending; Add: additive (hot cores, sparks); Light: dst * (1 + src), i.e. the
    // light brightens what it falls on instead of washing bright sand out to white (kGlowPM).
    enum class Blend : std::uint8_t { Alpha, Add, Light };
    struct Particle {
        float x, y, z;        // world px; z = height (drawn y - z)
        float vx, vy, vz;     // px / s
        float age, life;      // s; age < 0 = delayed start
        float size0, size1;   // world px (diameter)
        std::uint32_t c0, c1; // RGBA (0xAABBGGRR) at birth / death
        float gravity;        // px / s^2 on vz
        float drag;           // 1/s velocity damping
        float rot, vrot;      // radians
        Tex tex;
        Blend blend;
        bool streak;          // stretched along the screen velocity (sparks)
        bool aligned;         // rot is a direction in the ground plane (3D projects it)
        float len;            // > 0: a strip len world px long along rot, size wide
        std::uint8_t fx;      // kFx* behaviour flags (debris landing, smoke trails, late fade)
        float acc;            // kFxTrail: distance to the next trail puff
    };
    enum : std::uint8_t {
        kFxLandDust = 1,   // first ground contact: a dust puff
        kFxLandFire = 2,   // first ground contact: a small fire burns there for a while
        kFxTrail = 4,      // while airborne: smoke and a little flame behind it (burning debris)
        kFxLanded = 8,     // (internal) the landing effects ran
        kFxLateFade = 16,  // keeps c0 and fades out over the last quarter of its life (lying debris)
        kFxHot = 32,       // fire: the colour holds near c0 and cools late (colour lerp by t^2)
    };
    struct Tread {
        float x, y, angle, age;
    };
    // ---- Light list (dynamic lights; docs/enhanced-graphics.md "Light list").
    // Rebuilt every update(): every light source of this frame - explosions and their flashes,
    // muzzle flashes, projectiles in flight, fires and burning wrecks. Each light is a point at
    // the drawn position (x, y) = (ground x, ground y - z) with its height z above the ground,
    // a reach `size` (world px; the 2D glow's diameter, the 3D point light's radius is size / 2)
    // and a colour whose alpha is the intensity (255 = 1.0). 2D draws each as an additive-ish
    // glow (Light blend, dst * (1 + c)) after the objects, so it brightens the ground, the units
    // and the buildings under it; 3D uploads the strongest ones as point lights to the terrain /
    // model / sprite shaders (render3d/lights.hpp). Future sources (time of day, vehicle and
    // building lights) add their records here (or to the 3D list) the same way.
    struct Light {
        float x, y, size;
        std::uint32_t c;  // RGBA; alpha = intensity
        float z = 0;      // height of the source above the ground (world px)
    };
    const std::vector<Particle>& particles() const { return particles_; }
    const std::vector<Tread>& treads() const { return treads_; }
    const std::vector<Light>& lights() const { return lights_; }  // ground glows (x, y = drawn point)
    // Beam strips rebuilt every update (sonic ribbons): drawn like particles (age 0 = c0).
    const std::vector<Particle>& beams() const { return beams_; }
    SDL_Texture* atlasTexture() const { return atlasTex_; }
    // ---- Modern craters (Effects: Modern + Designed terrain; render/effects_craters.cpp): the
    // simulation's terrain damage per cell (cell.damage > 0x10 on rock / sand, exactly where the
    // original draws its crater sprite) as soft lit dents. Each decal is two layers of
    // craterTexture(): the ground layer (u0..v1) tinted `tint`, then the soot layer (u0, sv0,
    // u1, sv1) in white at alpha `soot`; centred on (x, y), size x size world px, not rotated
    // (the shading is lit from the north-west, like the terrain).
    struct CraterDecal {
        float x = 0, y = 0, size = 0;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0, sv0 = 0, sv1 = 0;
        std::uint32_t tint = 0;
        float soot = 1;
    };
    void craterDecals(const game::World& w, bool showShroud, int x0, int y0, int x1, int y1,
                      std::vector<CraterDecal>& out) const;
    SDL_Texture* craterTexture() const { return craterTex_; }
    // 2D: the craters of the cell range (tiles) over the baked terrain, instead of the sprites.
    void drawCraters(const game::World& w, const TileAtlas& atlas, const Camera& cam, int x0, int y0, int x1, int y1,
                     bool showShroud);
    // UV rectangle of a procedural texture cell in atlasTexture().
    static void texRect(Tex t, float& u0, float& v0, float& u1, float& v1);
    // The look drawAir gives a particle: diameter (world px) and RGBA (Light: premultiplied,
    // alpha 255). False while it has not started yet.
    static bool particleLook(const Particle& p, float& size, std::uint32_t& rgba);
    // Tread mark opacity (0 = gone) and colour; light colour as drawAir (premultiplied).
    static float treadAlpha(const Tread& t);
    static std::uint32_t treadColour();
    static std::uint32_t lightColour(const Light& l);

    int particleCount() const { return static_cast<int>(particles_.size()); }

    // Sandworms: Modern draws the moving worm's sand (render/effects_events.cpp), so the renderer skips the
    // original's one-pixel sand particles (World::particles).
    static constexpr bool replacesWormParticles() { return true; }

    // Modern effects draw their own version of these, so the renderer skips the original
    // sprite (explosion art index / weapon index). Explosions that are not weapon effects
    // (harvester unload art, refinery, spice bloom, crates, sandworm, harvester dust) and
    // unknown names keep their original sprites.
    bool replacesExplosion(int type) const {
        return type >= 0 && type < int(exReplace_.size()) && exReplace_[std::size_t(type)] != 0;
    }
    bool replacesBullet(int weapon) const {
        return weapon >= 0 && weapon < int(weaponClass_.size()) && weaponClass_[std::size_t(weapon)] != WeaponClass::None;
    }
    // Spice blooms (crate records of type 7..10, World::crates()): Modern draws its own mound
    // (growing with the stage, pulsing; render/effects_events.cpp), so the renderer skips the
    // original bloom sprite. Ordinary crates keep theirs.
    static bool replacesCrate(int type) { return type >= 7 && type <= 10; }

    // ---- Destroyed buildings (render only): the collapse during the 30-tick death state 17,
    // ground marks (shockwave, scorch, rubble, embers) that outlive the building for a while.
    // How a dying building's art is drawn: N vertical pieces (u range of the image) that sink
    // into the ground (the part below the base line is cut off), lean outwards and char.
    struct CollapseLook {
        struct Piece { float u0, u1, sink, lean; };  // sink: fraction of the height; lean: px at the top
        int pieces = 0;
        Piece piece[8] = {};
        float shade = 1;       // rgb multiplier (charring)
        float dx = 0, dy = 0;  // tremble, world px
        float sink = 0;        // average sink (barrels, shadows)
    };
    bool collapseLook(int side, int index, CollapseLook& out) const;
    // Flat marks on the ground (rebuilt every update), drawn after the building bases, under
    // the buildings and units: shockwave rings, scorch, rubble, glowing embers.
    struct GroundMark {
        float x, y, hw, hh, rot;  // centre, half sizes (world px), angle (rad)
        Tex tex;
        Blend blend;
        std::uint32_t c;  // RGBA (Light: premultiplied)
    };
    const std::vector<GroundMark>& groundMarks() const { return marks_; }
    void drawMarks(const Camera& cam);
    // ---- Sonic heat haze: the battlefield under and around a sonic beam, bent in screen space.
    bool hasHaze() const { return !sonic_.empty(); }
    // Builds a refraction mesh over every beam: vertex positions are the undisplaced points
    // (toScreen: ground point in world px, drawn y -> target px), texture coordinates the same
    // points displaced a little (animated), divided by the captured frame's size. coreKeep:
    // keep the displacement small on the beam's own line (3D: the beam is in the captured frame).
    using ToScreen = std::function<bool(float wx, float wy, float& sx, float& sy)>;
    void buildHaze(std::vector<SDL_Vertex>& v, std::vector<int>& idx, const ToScreen& toScreen, float texW, float texH,
                   bool coreKeep) const;

private:
    // Per-object-kind slot memory for detecting new objects.
    struct SlotSeen {
        std::int32_t key = -1;
        std::int32_t a = 0;   // explosions: frame, bullets: moveSteps
        bool done = false;    // bullets: muzzle effect spawned
        float lx = 0, ly = 0, acc = 0;  // bullets: last position, trail distance accumulator
        float lz = 0;                   // bullets: last height
        bool visible = false;           // bullets: in view and unshrouded last frame
        int beam = -1;                  // bullets: SonicBeam id
    };
    enum class ExClass : std::uint8_t {
        None, Big, Huge, Medium, Small, Muzzle, Trail, Dust, Flame, Spark, Gas, Plasma, Whiten, DhmTrail,
        WormMaw,    // EX_WORM: a sandworm swallows a vehicle (its maw art stays: the worm itself)
        WormDust,   // EX_WORMDUST: the dust of the swallow (replaced)
        WormSign,   // EX_WORMSN1..6: wormsign, the sand bursting over a moving worm (replaced)
        HarvDust,   // EX_HARVDUST0..7: a harvester's load step (direction in exDir_)
        Smoke,      // EX_SMOKE3: the original's damage smoke (units: smoke timer, buildings: BUILEXP.BIN points)
        BloomGrow,  // EX_SPICEBLOOM: a spice bloom grows a stage
        Refinery    // EX_REF: a refinery processes one unloaded spice blob
    };
    enum class WeaponClass : std::uint8_t { Gun, Cannon, Rocket, Artillery, Sonic, Plasma, Gas, Bomb, Debris, Spice, None };

    SDL_Renderer* r_;
    const formats::Templates& t_;
    SDL_Texture* atlasTex_ = nullptr;
    SDL_Texture* vignette_ = nullptr;
    SDL_Texture* craterTex_ = nullptr;
    const TileAtlas* craterAtlas_ = nullptr;  // the tile atlas of the last update (crater colours)
    std::vector<CraterDecal> craterScratch_;
    void buildCraterTexture();
    std::vector<Particle> particles_;
    std::vector<Tread> treads_;
    std::vector<Light> lights_;  // rebuilt every update
    std::vector<Particle> beams_;  // rebuilt every update
    // A sonic shot: origin, head, and a fade after the bullet ended.
    struct SonicBeam {
        int id = 0;
        float ox = 0, oy = 0, hx = 0, hy = 0;  // world px (y = drawn y)
        float dying = -1;                      // s since the bullet ended (-1 = flying)
        float seed = 0, age = 0, arcAcc = 0;
        bool seen = false;                     // updated this frame
    };
    std::vector<SonicBeam> sonic_;
    // A destroyed building: tracked from its death (state 17) until its ruin has faded.
    struct Wreck {
        int side = 0, index = 0, type = 0, tileX = 0, tileY = 0;
        std::uint32_t mask = 0;   // solid cells (4x4 bits)
        int cells = 1, fw = 1, fh = 1;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // solid footprint (world px)
        float artH = 32;          // art height (px above the footprint's south edge)
        float artX0 = 0, artW = 32, artY = 0;  // the art box: left, width, bottom (base line)
        std::uint32_t sand = 0;   // terrain colour for the dust
        float big = 0;            // 0 small (turret, wall) .. 1 large (construction yard, palace)
        float start = 0, last = -1;  // effect time at death; elapsed time processed so far
        float smokeAcc = 0, dustAcc = 0, baseAcc = 0, sparkAcc = 0;
        std::uint32_t seed = 0;
        bool present = true;      // the building is still in the world (state 17)
        bool seen = false;
        bool lite = false;        // walls and other non-repairable small structures: no fire, no smoke column
    };
    std::vector<Wreck> wrecks_;
    struct Fire {
        float x, y, age, life, size, acc;
        float smoke = 0.18f;  // share of flame puffs that also send up smoke (burning wrecks: more)
        float smokeAcc = 0;
    };
    std::vector<Fire> fires_;  // small fires (landed debris, ruins, burning vehicle wrecks)
    // Timed lights (explosion flashes, muzzle flashes): an intensity envelope over their life,
    // added to lights_ every update.
    struct Flash {
        float x, y, z, size;
        std::uint32_t c;
        float age, life;
        float attack;  // s to full intensity (0: instant)
    };
    std::vector<Flash> flashes_;
    void addFlash(float x, float y, float z, float size, std::uint32_t c, float life, float attack = 0);
    // Ground marks with a life of their own: explosion scorch, vehicle wreck hulks, shockwave
    // and dust rings (added to marks_ by buildMarks).
    struct Scorch {
        float x, y, size, rot, age, life;
        std::uint8_t kind;  // 0 scorch, 1 wreck hulk (scorch + charred chunks), 2 shockwave, 3 dust ring, 4 spice stain,
                            // 5 sand furrow (a worm's trail), 6 churned sand (where a worm ate), 7 sand shockwave
        std::uint32_t seed;
        float strength;
        std::uint32_t c;    // dust ring colour
    };
    std::vector<Scorch> scorch_;
    void addScorch(float x, float y, float size, float life, std::uint8_t kind, float strength = 1,
                   std::uint32_t c = 0);
    float load() const;  // 1 normally, less when the particle budget is filling up
    std::vector<GroundMark> marks_;
    std::vector<Particle> pending_;  // spawned while stepping (landings, trails)
    int nextBeamId_ = 1;
    std::size_t treadHead_ = 0;
    std::array<std::vector<SlotSeen>, game::kMaxSides> seenEx_, seenBullet_;
    std::array<std::vector<UnitVisual>, game::kMaxSides> units_;
    // ---- Original non-weapon events (render/effects_events.cpp): harvest dust, damage smoke, spice
    // blooms (mound, growth, burst, spice shells and their splashes), building capture.
    struct BloomSeen { bool active = false; int type = 0, x = 0, y = 0; };
    std::array<BloomSeen, game::kMaxCrates> bloomSeen_{};
    struct BloomLook { float x, y; int stage; std::uint32_t timer, seed; };
    std::vector<BloomLook> blooms_;  // visible blooms of this frame (marks)
    struct SpiceTarget { int key; float x, y, until; };
    std::vector<SpiceTarget> spiceTargets_;  // where spice shells in flight (and just landed) come down
    struct CaptureSeen { int side, index, type, tx, ty; float t; };
    std::vector<CaptureSeen> captures_;
    int spiceWeapon_ = -1;  // groupIds[50] (SPICEEXPL)
    std::vector<std::int8_t> exDir_;  // EX_HARVDUST0..7: the 8-way direction
    void spawnHarvestDust(const game::World& w, const TileAtlas& atlas, float x, float y, int dir, bool burst);
    void spawnDamageSmoke(const game::World& w, float x, float y, float z);
    void spawnFlame(const game::World& w, float x, float y, float z);
    // EX_REF: a refinery's chimneys vent (one burst per unloaded blob) and smoke while it works.
    static constexpr int kMaxVents = 8;  // chimney tops per refinery
    struct RefineryVent {
        float x = 0, y = 0;     // the EX_REF point (world px)
        float heat = 0;         // 0..1.4: rises with every event, holds while unloading, then cools
        float since = 0;        // s since the last event
        float smokeAcc[kMaxVents] = {}, lickAcc[kMaxVents] = {};
        std::uint32_t seed = 0;
    };
    std::vector<RefineryVent> vents_;
    // A chimney top as a particle point (ground x / y, height z) with its strength.
    struct VentPoint { float x, y, z, power; };
    // The vent's chimney tops this frame: the model's "fx_flue*" anchors (3D with models), else the
    // two measured on the original art. Returns the count (<= kMaxVents).
    int ventPoints(const game::World& w, const RefineryVent& v, VentPoint* out) const;
    void spawnRefineryVent(const game::World& w, float x, float y);
    void updateRefineryVents(const game::World& w, float dt, const Camera& cam, int viewW, int viewH, bool showShroud);
    // Weld sparks at the fx_weld anchors of working redesigned buildings (render/weld_fx.hpp).
    void updateWeldSparks(const game::World& w, float dt, const Camera& cam, int viewW, int viewH, bool showShroud);
    void updateBlooms(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                      bool showShroud, bool first);
    void spawnBloomGrow(const game::World& w, const TileAtlas& atlas, float x, float y);
    void spawnBloomBurst(const game::World& w, const TileAtlas& atlas, float x, float y);
    bool nearSpiceTarget(float x, float y) const;
    void spawnSpiceSplash(const game::World& w, const TileAtlas& atlas, float x, float y);
    void spawnCaptureFlash(const game::World& w, int tileX, int tileY, int type, int fromSide);
    // Sandworms (render/effects_events.cpp): the moving worm's sand displacement (instead of the original's
    // one-pixel sand particles, World::particles), wormsign bursts, the swallow's eruption and dust.
    struct WormLook {
        int side = 0, index = 0;
        float x = 0, y = 0, heading = 0;  // world px; heading radians (0 = east, ground plane)
        float trailAcc = 0, rippleAcc = 0, dustAcc = 0, sprayAcc = 0;
        float moving = 0;                 // 0..1 eased: a hump while it travels under the sand
        float churn = 0;                  // s left of churning after a swallow
        std::uint32_t sand = 0, seed = 0;
        bool seen = false, visible = false;
    };
    std::vector<WormLook> worms_;
    void updateWorms(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                     bool showShroud);
    void wormMarks();
    void spawnWormSign(const game::World& w, const TileAtlas& atlas, float x, float y);
    void spawnWormStrike(const game::World& w, const TileAtlas& atlas, float x, float y, const Camera& cam, int viewW,
                         int viewH);
    void spawnWormDust(const game::World& w, const TileAtlas& atlas, float x, float y);
    void bloomMarks();
    std::vector<ExClass> exClass_;
    std::vector<float> exScale_;          // fireball size per explosion type (classify)
    std::vector<std::uint8_t> exReplace_; // the original sprite is replaced (replacesExplosion)
    std::vector<WeaponClass> weaponClass_;
    std::vector<std::uint8_t> wheeled_;
    std::array<std::uint32_t, formats::kHouseCount> gasBody_{}, gasGlow_{};  // per house (setHouseColours)
    bool primed_ = false;
    std::uint32_t rng_ = 0x9E3779B9u;
    float time_ = 0;
    float cardTilt_ = -1;  // setCardTilt
    const EffectAnchors* anchors_ = nullptr;  // setAnchors
    mutable std::vector<EffectAnchor> anchorTmp_;
    float trauma_ = 0, shakeX_ = 0, shakeY_ = 0;
    // Frame vertex batches.
    std::vector<SDL_Vertex> vAlpha_, vAdd_, vLight_, vGround_;
    bool lightBlendOk_ = true;
    bool graded_ = false;
    float grade_[3] = {1, 1, 1};
    std::vector<int> idx_;

    float rnd();                       // 0..1
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    void buildTextures();
    void classify();
    void add(const Particle& p);
    void spawnExplosion(ExClass c, float x, float y, float z, float age, const Camera& cam, int viewW, int viewH,
                        const TileAtlas& atlas, const game::World& w, bool crowded = false, int house = -1,
                        float sizeMul = 1, float shakeMul = 1);
    void preAge(std::size_t first, float age);
    void addTrauma(float x, float y, float amount, const Camera& cam, int viewW, int viewH);
    void updateWrecks(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                      bool showShroud);
    void runWreck(Wreck& k, float from, float to, const game::World& w, const TileAtlas& atlas, const Camera& cam,
                  int viewW, int viewH, bool visible);
    void buildMarks();
    void landed(const Particle& p);
    void spawnMuzzle(WeaponClass c, float x, float y, float z, float dx, float dy, float age, int house);
    void spawnTracer(float x, float y, float z, float tx, float ty, float tz);
    void spawnShot(const game::World& w, const game::ShotEvent& e, WeaponClass c);
    std::uint32_t lastShotTick_ = 0;  // shot log processed up to this game tick
    void spawnImpact(const game::World& w, const TileAtlas& atlas, float x, float y, float z, float scale);
    void projectileLook(WeaponClass c, float x, float y, float z, float vx, float vy, float vz, int house, int seed);
    void spawnSonicRipple(float x, float y);
    void buildBeams();
    std::uint32_t gasBody(int house) const;
    std::uint32_t gasGlow(int house) const;
    void emitContinuous(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW,
                        int viewH, bool showShroud, bool prewarm);
    void step(float dt);
    void quad(std::vector<SDL_Vertex>& v, float cx, float cy, float hw, float hh, float angle, std::uint32_t rgba,
              float alphaMul, Tex tex);
    void flush(std::vector<SDL_Vertex>& v, Blend b, SDL_Texture* tex);
    std::uint32_t terrainColour(const game::World& w, const TileAtlas& atlas, float x, float y) const;
};

}  // namespace d2k::render
