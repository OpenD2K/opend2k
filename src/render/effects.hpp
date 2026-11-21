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
#include "render/building_assemble.hpp"
#include "render/effect_anchors.hpp"
#include "render/fx_budget.hpp"
#include "render/smooth_drive.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Vertex;

namespace d2k::render {

class MuzzleTips;
class DropInSource;
class AssemblySource;

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
    // Barrel tips measured on the original sprites (render/muzzle_tips.hpp): where shots start for
    // the units the anchors above do not cover. nullptr = the old offsets along the aim.
    void setMuzzleTips(const MuzzleTips* m) { tips_ = m; }
    // "Vehicle animation: Modern" (Settings::vehicleAnimation): a puff of dust kicked up when a
    // vehicle sets off. Set before update().
    void setVehicleAnimation(bool on) { vehicleAnim_ = on; }
    // Smooth driving (render/smooth_drive.hpp; nullptr = off): ground vehicles' UnitVisual facings
    // are the drawn ones of the smoothed path instead of the eased 32 steps.
    void setDrive(const SmoothDrive* d) { drive_ = d; }
    // Drop-in build-up (render/building_dropin.hpp; Graphics: 3D with models, "Building placement:
    // Drop-in"): who says which buildings are dropping in this frame (nullptr = none). The landing
    // kicks up a dust ring and pebbles, a soft shadow grows under the falling building and the sand
    // under it stays dark for a moment (render/effects_dropin.cpp). Set before update().
    void setDropIn(const DropInSource* s) { dropIn_ = s; }
    // The built-in landing dust on / off: a later particle system (the Reimagined cinematic
    // particles) can take the burst over and read dropInImpacts() instead; the marks stay.
    void setDropInDust(bool on) { dropInDust_ = on; }
    // The landings of this update (visible ones only): footprint (world px), its cell, the sand colour.
    struct DropInImpact {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int tileX = 0, tileY = 0;
        std::uint32_t sand = 0;
        bool fromGround = false;  // an MCV unfolding where it stands (a lighter puff)
    };
    const std::vector<DropInImpact>& dropInImpacts() const { return dropInImpacts_; }
    // Assemble build-up (render/building_assemble.hpp; "Building placement: Assemble"): who says which
    // buildings are assembling this frame (nullptr = none): survey outline and dust on the footprint,
    // sparks along the rising cuts, a weld flash and dust where each part seats (render/effects_assemble.cpp).
    // Set before update().
    void setAssembly(const AssemblySource* s) { assembly_ = s; }
    // Particle density of the continuous emitters (building fires): 1 = High, less on lower Quality
    // presets (phones). Set before update().
    void setDetail(float d) { detail_ = std::clamp(d, 0.25f, 1.0f); }
    // "Effects intensity" (Settings::effectsIntensity; render/fx_budget.hpp): Cinematic = the full look,
    // Balanced = scaled down as the view gets busy, Clean = short effects, minimal smoke. Set before update().
    void setIntensity(FxIntensity i) { budget_.intensity = i; }
    FxIntensity intensity() const { return budget_.intensity; }
    const FxBudget& budget() const { return budget_; }
    float areaK() const { return areaK_; }  // reference view area / the view's area (FxBudget::areaK)
    // D2K_FX_HIDE=1 (measurements only): the particles are simulated as usual but not drawn (2D, plain 3D and
    // the cinematic particles) - the "no effects" frame the readability numbers compare against.
    static bool hideParticles();
    // The extra opacity factor of a particle from the intensity: the early fade-out of a capped puff x the
    // see-through over units (1 = as spawned).
    struct Particle;
    static float budgetAlpha(const Particle& p);

    // Screen shake for this frame, world pixels (add to the camera).
    float shakeX() const { return shakeX_; }
    float shakeY() const { return shakeY_; }

    // Smoothed presentation of a ground vehicle (nullptr if not tracked).
    struct UnitVisual {
        std::int32_t key = -1;          // type + 1; -1 = slot not seen
        float facing = 0, turret = 0;   // 0..32 (32-way facing units, eased)
        float x = 0, y = 0;             // last drawn position (world px)
        float treadDist = 0, dustDist = 0;
        // Track marks: this vehicle's trail (Tread nodes; 0 = none yet / broken), the px driven and
        // the heading turned along it, the heading of its last node; while on rock / concrete the
        // newest point is held back (pend*) and only laid when the trail reaches sand again.
        std::uint32_t trail = 0;
        float trailOdo = 0, trailTurn = 0, lastAngle = 0, nodeAngle = 0;
        float pendX = 0, pendY = 0, pendAngle = 0, pendOdo = 0, pendTurn = 0;
        bool pend = false, angleSet = false;
        float smokeT = 0;
        float still = 0;                // seconds standing (the dust kick-up on setting off)
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
        kFire, kMuzzle, kStreak, kCapsule, kScorch, kShock, kMound,
        kTongue, kTongue2,  // flame tongues (building fires): coloured (white-yellow core, orange, dark red tips),
                            // base at -u, tip at +u (drawn as strips: len along rot, size across)
        kTexCount
    };
    // Alpha: normal blending; Add: additive (hot cores, sparks); Light: dst * (1 + src), i.e. the
    // light brightens what it falls on instead of washing bright sand out to white (kGlowPM).
    // Glow: normal (over) blending of something that emits its own light (flames): not darkened by
    // the time of day (2D grade, 3D sprite lighting), and - unlike Add - it replaces the colour
    // under it, so orange stays orange over the blue night (Add on blue reads pink / purple).
    enum class Blend : std::uint8_t { Alpha, Add, Light, Glow };
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
        // Effects intensity (render/fx_budget.hpp; 0 = off): `cut` = the age (s) at which a transient puff has
        // faded out and goes (capped smoke clears early; the look before that is unchanged); `clear` = how
        // see-through it is over a unit (0 = not at all, eased per frame).
        float cut;
        float clear;
    };
    enum : std::uint8_t {
        kFxLandDust = 1,   // first ground contact: a dust puff
        kFxLandFire = 2,   // first ground contact: a small fire burns there for a while
        kFxTrail = 4,      // while airborne: smoke and a little flame behind it (burning debris)
        kFxLanded = 8,     // (internal) the landing effects ran
        kFxLateFade = 16,  // keeps c0 and fades out over the last quarter of its life (lying debris)
        kFxHot = 32,       // fire: the colour holds near c0 and cools late (colour lerp by t^2)
        kFxBlast = 64,     // part of a weapon blast the 3D cinematic particles redraw (Reimagined): the 3D
                           // renderer skips it while they run (its trail / landing puffs inherit the tag); also the
                           // dark smoke of fires, damage and wrecks, which they draw lit (docs/3d-mode.md "Lit smoke")
    };
    // Track marks (docs/3d-mode.md "Track marks"): the trail of a ground vehicle as nodes, each linked to
    // the one before it on the same trail, so the 3D view draws continuous imprint strips through the
    // turns (render3d/track_marks.hpp); 2D stamps its old tread marks at the `stamp` nodes.
    struct Tread {
        float x = 0, y = 0;         // the vehicle's drawn ground point (world px)
        float angle = 0;            // its drawn hull heading (radians, screen: 0 = east, + clockwise)
        float age = 0;              // s
        float odo = 0;              // px driven along the trail (the tread pattern's phase)
        float turn = 0;             // radians turned along the trail (unwrapped; the bands' own odometers)
        float depth = 0;            // how soft the ground is here: 1 sand / dunes / spice, 0 rock / concrete
        std::uint32_t serial = 0;   // node number (0 = empty slot)
        std::uint32_t prev = 0;     // the node before it on the same trail (0 = the trail starts here)
        std::int16_t type = -1;     // unit type
        bool spin = false;          // turned on the spot since the previous node (a scuffed arc)
        bool stamp = false;         // a 4 px step on sand (2D tread marks)
        bool wheels = false;        // a wheeled vehicle (2D: no marks)
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
        bool blast = false;  // an explosion's fireball: the 3D cinematic particles light it themselves
    };
    const std::vector<Particle>& particles() const { return particles_; }
    const std::vector<Tread>& treads() const { return treads_; }  // a ring (serial 0 = empty)
    const Tread* treadBySerial(std::uint32_t s) const {
        if (s == 0 || treads_.empty()) return nullptr;
        const Tread& t = treads_[s % treads_.size()];
        return t.serial == s ? &t : nullptr;
    }
    std::uint32_t treadSerial() const { return treadSerial_; }  // the newest node
    std::size_t treadCapacity() const { return treads_.size(); }
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

    // ---- Event log for the 3D "Cinematic" particles (render3d/cinematic_fx.*, docs/3d-mode.md "Reimagined"):
    // off by default (2D and the plain 3D never pay for it). When on, every update() starts a fresh list of
    // what happened this frame - explosions by size, impacts, vehicle / building deaths - at ground point
    // (x, y) and height z (world px).
    // Weapon kinds (FxEvent::weapon; the order is fixed: the cinematic particles switch on it).
    enum class WeaponClass : std::uint8_t { Gun, Cannon, Rocket, Artillery, Sonic, Plasma, Gas, Bomb, Debris, Spice, None };
    struct FxEvent {
        // Muzzle: a shot leaves the barrel (x, y ground, z height; tx, ty = aim direction on the ground).
        // Tracer: a machine-gun round flying from (x, y, z) to (tx, ty, tz). Projectile: a shell / rocket in
        // flight this frame (x, y ground, z height; key stable per bullet).
        // Artillery: a howitzer shell / grenade hit (scale 1 howitzer, ~0.6 grenade; armour: on a vehicle /
        // building) - thrown-up earth and a flash rather than a fireball.
        // Smoke: a puff of the original's damage smoke (EX_SMOKE3) off a vehicle (scale = its health 0..1) or, armour
        // set, off a building's smoke point that feeds no fire (scale = its health).
        enum Kind : std::uint8_t { Explosion, Impact, VehicleDeath, BuildingDeath, Muzzle, Tracer, Projectile, Artillery, Smoke };
        Kind kind = Explosion;
        float x = 0, y = 0, z = 0;
        float scale = 1;    // Explosion: 0 small .. 1 medium, 2 big, 3 huge (x the type's size); death: 0..1 size
        bool sand = false;  // the ground there is sand (dust) rather than rock / concrete
        bool armour = false;  // Impact: on a vehicle / building
        WeaponClass weapon = WeaponClass::None;  // Muzzle / Tracer / Projectile
        float tx = 0, ty = 0, tz = 0;            // Muzzle: aim direction; Tracer: end point
        std::uint32_t key = 0;                   // Projectile: side << 16 | pool index
    };
    // Aircraft crashes (render3d/wrecks.hpp, docs/3d-mode.md "Aircraft crashes"): on = a shot-down flyer falls and
    // burns where it comes down, so the death flash under a flyer (the one flash DestroyUnit starts with the
    // drift flag) or under what a carryall carried (a dead carried unit on the flash's pixel) starts no fire and
    // leaves no hulk mark on the ground under the point it was hit at.
    void airCrashes(bool on) { airCrashes_ = on; }
    void recordEvents(bool on) {
        recordEvents_ = on;
        if (!on) events_.clear();
    }
    const std::vector<FxEvent>& events() const { return events_; }
    // Burning buildings' fire points this frame (ground x / y, height z, strength 0..1): smoke columns.
    struct SmokeSource {
        float x, y, z, strength;
        std::uint32_t key;  // stable per point
    };
    void smokeSources(std::vector<SmokeSource>& out) const;

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
        // bullets: drawn offset from the simulated position to the barrel tip at launch (world px),
        // shrinking to 0 as the bullet nears its target (d0 = its distance to it at launch).
        float ox = 0, oy = 0, oz = 0, d0 = 0;
    };
    enum class ExClass : std::uint8_t {
        None, Big, Huge, Medium, Small, Muzzle, Trail, Dust, Flame, Spark, Gas, Plasma, Whiten, DhmTrail,
        WormMaw,    // EX_WORM: a sandworm swallows a vehicle (its maw art stays: the worm itself)
        WormDust,   // EX_WORMDUST: the dust of the swallow (replaced)
        WormSign,   // EX_WORMSN1..6: wormsign, the sand bursting over a moving worm (replaced)
        HarvDust,   // EX_HARVDUST0..7: a harvester's load step (direction in exDir_)
        Smoke,      // EX_SMOKE3: the original's damage smoke (units: smoke timer, buildings: BUILEXP.BIN points)
        BloomGrow,  // EX_SPICEBLOOM: a spice bloom grows a stage
        Refinery,   // EX_REF: a refinery processes one unloaded spice blob
        Shell,      // EX_TANKGUN: a tank shell's hit - a heavy bullet, not a bomb: dust / sparks, no fireball
        Artillery   // EX_155MMGUN: a howitzer shell / grenade - a fountain of earth, a flash, no rolling fireball
    };
    SDL_Renderer* r_;
    const formats::Templates& t_;
    SDL_Texture* atlasTex_ = nullptr;
    SDL_Texture* vignette_ = nullptr;
    SDL_Texture* craterTex_ = nullptr;
    const TileAtlas* craterAtlas_ = nullptr;  // the tile atlas of the last update (crater colours)
    std::vector<CraterDecal> craterScratch_;
    void buildCraterTexture();
    std::vector<Particle> particles_;
    bool recordEvents_ = false;
    bool airCrashes_ = false;  // airCrashes()
    bool flyerFlash_ = false;  // the explosion being spawned drifts (a flyer's death)
    std::vector<FxEvent> events_;
    void note(FxEvent::Kind k, const game::World& w, float x, float y, float z, float scale, bool armour = false);
    void noteEvent(const FxEvent& e) {
        if (recordEvents_ && events_.size() < kMaxEvents) events_.push_back(e);
    }
    static constexpr std::size_t kMaxEvents = 2048;
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
    // ---- Effects intensity (render/fx_budget.hpp).
    FxBudget budget_;
    float areaK_ = 1;           // reference view area / this view's area
    int eventDepth_ = 0;        // > 0 while an event (a hit, a blast) spawns: its smoke is transient
    std::uint32_t thinN_ = 0;   // thinning hash counter (not the effects' RNG)
    struct EventScope {         // marks the particles added in its scope as an event's (transient) ones
        explicit EventScope(Effects& e) : fx(e) { ++fx.eventDepth_; }
        ~EventScope() { --fx.eventDepth_; }
        Effects& fx;
    };
    // The weight of an event for the busy level (counted when it spawns in view).
    void countEvent(float w) { budget_.addEvent(w); }
    // Applies the budget to an event's particle; false = thinned out (not added).
    bool budgetSpawn(Particle& p);
    // Smoke over units: eases every alpha-blended puff's `clear` towards how much it covers a unit.
    void updateSeeThrough(const game::World& w, float dt);
    // Lasting columns (burning vehicle wrecks): merge nearby ones, cap how many smoke at once.
    void capColumns(float dt, const Camera& cam, int viewW, int viewH);
    std::vector<GroundMark> marks_;
    std::vector<Particle> pending_;  // spawned while stepping (landings, trails)
    int nextBeamId_ = 1;
    std::uint32_t treadSerial_ = 0;
    std::uint32_t addTread(Tread t);
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
    // Grenades share the howitzer's hit explosion (EX_155MMGUN): where grenades in flight come down tells
    // the two apart (a smaller artillery hit).
    int grenadeWeapon_ = -1;
    std::vector<SpiceTarget> grenadeTargets_;
    bool nearGrenadeTarget(float x, float y) const;
    void spawnArtillery(const game::World& w, const TileAtlas& atlas, float x, float y, float z, float k);
    std::vector<std::int8_t> exDir_;  // EX_HARVDUST0..7: the 8-way direction
    void spawnHarvestDust(const game::World& w, const TileAtlas& atlas, float x, float y, int dir, bool burst);
    void spawnDamageSmoke(const game::World& w, float x, float y, float z);
    void spawnFlame(const game::World& w, float x, float y, float z);
    // ---- Building fires (render/effects_fire.cpp): the original's damage events (EX_SMOKE3 / EX_FLAME
    // at the BUILEXP.BIN points of a building below half health) feed a fire record per building and
    // point; the record burns (layered flame tongues, embers, a smoke column lit from below, firelight,
    // soot on the ground) with the building's damage while the events keep coming, then dies down.
    struct BurnPoint {
        float ox = 0, oy = 0;     // the event's drawn point from the building's top-left tile corner (px)
        bool flame = false;       // an EX_FLAME point (else EX_SMOKE3)
        float heat = 0;           // 0..1: 1 while the events come, falls off after the last one
        float since = 99;         // s since the last event
        float lickAcc[4] = {}, smokeAcc = 0, emberAcc = 0, litAcc = 0;
        float tx[4] = {}, ty[4] = {};  // tongue offsets (px) around the point
        int tongues = 0;
        std::uint32_t seed = 0;
    };
    struct BurnSite {
        int side = 0, index = 0, type = 0, tileX = 0, tileY = 0;
        int points = 0;
        BurnPoint pt[8];
        float hp = 1;             // health fraction (this frame)
        float scorch = 0;         // 0..1 soot on the ground, builds up while it burns
        bool alive = true;        // the building still stands (not dying / gone)
        std::uint32_t seed = 0;
        // This frame's placement of each point: ground x / y, height z (valid while emitting).
        float gx[8] = {}, gy[8] = {}, gz[8] = {};
    };
    std::vector<BurnSite> burns_;
    float detail_ = 1;
    // An EX_SMOKE3 / EX_FLAME event at drawn point (x, y): feeds the burning building's record;
    // false when no damaged building holds the point (a vehicle's smoke).
    bool feedBurn(const game::World& w, float x, float y, bool flame);
    void updateBurns(const game::World& w, float dt, const Camera& cam, int viewW, int viewH, bool showShroud);
    void burnMarks();
    // Where point i of a site sits this frame (ground x / y, height): the model's fx_fire anchors, else
    // the event's point mapped onto the footprint (3D models: onto the model's roof, 3D cards: onto the
    // card, 2D: the same drawn point, split into ground + height inside the footprint).
    void burnPlace(const game::World& w, const BurnSite& s, int i, float& gx, float& gy, float& z) const;
    // One flame tongue leaving (x, y, z): s = size (1 = a building's medium fire).
    void flameLick(float x, float y, float z, float s, float lean);
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
    // Drop-in build-up (render/effects_dropin.cpp).
    struct DropInSeen {
        int side = 0, index = 0, type = 0, tileX = 0, tileY = 0;
        float lastP = 0;
        float impactT = -1e9f;  // time_ of the landing
        float shadow = 0;       // the blob shadow under it this frame (0 = none)
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::uint32_t sand = 0;
        bool fromGround = false, seen = false, visible = false;
    };
    std::vector<DropInSeen> dropIns_;
    std::vector<DropInImpact> dropInImpacts_;
    const DropInSource* dropIn_ = nullptr;  // setDropIn
    bool dropInDust_ = true;                // setDropInDust
    void updateDropIns(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                       bool showShroud);
    void dropInMarks();
    void spawnDropInDust(const DropInSeen& d);
    // Assemble build-up (render/effects_assemble.cpp).
    struct AssemblySeen {
        int side = 0, index = 0, type = 0, tileX = 0, tileY = 0;
        float lastP = -1;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the solid footprint (world px)
        float p = 0;                            // this frame's progress (marks)
        float dustAcc = 0;
        std::uint32_t sand = 0;
        bool seen = false, visible = false;
    };
    std::vector<AssemblySeen> assemblies_;
    std::vector<AssemblyPartWorld> assemblyParts_;  // scratch
    const AssemblySource* assembly_ = nullptr;      // setAssembly
    void updateAssemblies(const game::World& w, const TileAtlas& atlas, float dt, const Camera& cam, int viewW, int viewH,
                          bool showShroud);
    void assemblyMarks();
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
    const MuzzleTips* tips_ = nullptr;        // setMuzzleTips
    bool vehicleAnim_ = false;                // setVehicleAnimation
    const SmoothDrive* drive_ = nullptr;      // setDrive
    // Per unit slot: the shots fired (alternates between a unit's guns) and where the last one
    // left (the bullet that follows starts drawn there).
    struct LastShot { std::uint32_t tick = 0; std::uint16_t count = 0; bool valid = false; EffectAnchor at; };
    std::array<std::vector<LastShot>, game::kMaxSides> lastShot_;
    mutable std::vector<EffectAnchor> muzzleTmp_;
    // The barrel tips of unit `unit` of side `s` as drawn this frame (3D models' / rendered pack's
    // fx_muzzle anchors, else the measured original sprites, infantry: the rifle); false = unknown.
    bool muzzlePoints(const game::World& w, int s, int unit, std::vector<EffectAnchor>& out) const;
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
