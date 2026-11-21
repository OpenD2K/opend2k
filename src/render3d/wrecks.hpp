#pragma once
// Vehicle wrecks of the Reimagined 3D look (docs/3d-mode.md "Vehicle wrecks"; Settings::vehicleWrecks, with
// Modern effects and 3D models). The original removes a destroyed vehicle from the world and frees its cell; that
// stays exactly so. This is the picture only: where a ground vehicle died, its own model stays for a few seconds
// as a charred, burning hulk (render3d/models.* draws it, render3d/cinematic_fx.* burns it), the turret blown
// off, then it sinks and crumbles away. Units drive through it: a hulk a live unit overlaps is gone at once.
//
// Render-only: reads the World and the Modern effects' event log, keeps its own state, never touches the
// simulation or the game RNG. Everything is timed in real seconds (the frame time), so it looks the same at
// any GameSpeed. A fixed pool (kMax), the oldest goes when it is full; nothing is allocated per frame.
//
// What tells a death from the other ways a unit leaves the map: DestroyUnit 0x49cde0 alone starts EX_WHITEN1
// (the white flash under the death explosion) at the unit's own pixel, and leaves the unit in state Dead for its
// dead-state timer (10 ticks). The Modern effects log that flash as FxEvent::VehicleDeath; a hulk is made only
// for a ground vehicle in state Dead standing on that pixel. A worm's meal, a deviated / captured unit's old
// slot and a delivery carryall leaving are state Dead without the flash; a harvester in a refinery, a unit on a
// repair pad or under a carryall never reach state Dead; a load / restart starts a new world (reset).
//
// Aircraft crashes (docs/3d-mode.md "Aircraft crashes"; the same setting). The original removes a destroyed flyer
// in mid-air (it flies on for its dead-state timer inside its explosion, then is gone). The picture instead: the
// flyer is hit (a burst on the hull), loses control and falls for 0.7-1.6 s along its heading, burning and
// trailing smoke, hits the ground 1-2 tiles on (never off the map, not on a building when there is room) and
// blows up there, leaving a short-lived hulk. What a carryall carried falls with it and gets a hulk of its own
// when it is a vehicle. The same test tells the death: DestroyUnit's flash (it is started on the ground under a
// flyer too) at the pixel of a flyer in state Dead. A delivery carryall or a frigate flying off the map, a
// carryall putting its cargo down and the Death Hand's end never start that flash.

#include "render3d/math3d.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace d2k::render3d {
class Terrain;
}
namespace d2k::game {
class World;
struct Unit;
}
namespace d2k::render {
class Effects;
}

namespace d2k::render3d {

class Wrecks {
public:
    static constexpr int kMax = 24;
    // s after the blast: a cook-off blows the turret off (the first fireball has thinned enough to show it fly).
    static constexpr float kTurretDelay = 0.55f;
    enum Size : std::uint8_t { kSmall = 0, kMedium = 1, kLarge = 2 };  // trike / quad, tanks, harvester / MCV / Devastator
    struct Hulk {
        bool used = false;
        bool fresh = false;      // born this frame: the cinematic particles have not set it off yet
        bool posed = false;      // the model renderer has taken the dying unit's drawn pose
        bool turret = false;     // its model has a turret part (posed)
        bool landFx = false;     // the turret's landing dust is out
        std::uint32_t serial = 0;
        int side = 0, index = 0, type = 0;
        Size size = kMedium;
        float x = 0, y = 0;      // ground point (world px): where it died
        int facing = 0, turretFacing = 0;  // the simulation's (the pose when the unit was never drawn)
        float house[3] = {1, 1, 1};
        // The drawn pose at death (posed): body / turret model matrices, the turret pivot (world), the model's
        // height and its turret pivot's height above the model's origin (px).
        Mat4 body, turretM;
        Vec3 pivot;
        float top = 12, pivotY = 8;
        float age = 0, life = 8;   // s
        float cover = 0;           // 0..1: a live unit stands in it (eased; 1 = gone)
        float turretCover = 0;     // the same for the turret where it lies
        float seed[4] = {};
        float smokeAcc = 0, flameAcc = 0, trailAcc = 0;  // the particles' emission accumulators
        // Aircraft crashes: a shot-down flyer, or what a carryall carried (cargo). It falls from `from` (its
        // origin at death, world, y up) to `to` (on the ground) in fallT s, then lies there as a hulk.
        bool crash = false, cargo = false;
        bool heavy = false;      // a carryall / frigate: noses down and rolls slowly (else it spins and banks)
        bool rests = true;       // false: it came down on a building - the impact only, no hulk
        bool impactFx = false;   // the impact's effects are out
        bool sand = false;       // the ground where it comes down
        float sx = 0, sy = 0;    // where it died (world px; x, y move with a falling hulk)
        Vec3 from, to;
        float fallT = 0;
        float wy = 0, agl = 0;   // its origin's height now (world), and above the ground under it (update)
        Mat4 turretRel;          // the turret's pose relative to the body (it stays on)
        int poseAnim = -1, bodyPose = -1;  // the model's pose at death (the wings stop)
        float poseTime = 0;
    };
    // The pose of a hulk at its age (a pure function of the hulk: nothing is integrated per frame).
    struct Pose {
        float charK = 1;         // 0..1: how burnt the paint is
        float ember = 0;         // 0..1: the glow in the seams (flickers)
        float fire = 0;          // 0..1: how hard it burns (flames, smoke, light)
        float dissolve = 0;      // 0..1: how much of the hull has crumbled away (the end, or a unit in it)
        float turretDissolve = 0;
        float lift = 0;          // px: the hull's hop at the blast, then its sinking
        float roll = 0, pitch = 0;  // radians: how it came to rest
        Vec3 turretOff;          // the turret pivot's travel (world px)
        Vec3 turretAxis{1, 0, 0};  // it tumbles about this horizontal axis ...
        float turretAngle = 0;     // ... by this much (radians), spinning by turretSpin about the vertical
        float turretSpin = 0;
        bool turretAir = false;  // still flying
        // Aircraft crashes: still falling, its origin now (world), its spin about the vertical since the death,
        // and the height of the point it rolls / pitches about (the hull's middle in the air, its foot at rest).
        bool air = false;
        Vec3 pos;
        float spin = 0, centre = 0;
    };
    static Pose pose(const Hulk& h);

    void reset();
    bool any() const { return count_ > 0; }
    int count() const { return count_; }
    std::array<Hulk, kMax>& hulks() { return hulks_; }
    const std::array<Hulk, kMax>& hulks() const { return hulks_; }
    // The hulk made of unit (side, index) (the unit is still in the pool, in state Dead): its model is not
    // drawn a second time. nullptr = none.
    Hulk* owner(int side, int index);

    // Advance by dt real seconds (0 = paused) and make hulks of this frame's deaths. hidden(x, y): the ground
    // point is under the shroud / off the map.
    // modelled(type): the unit type has a 3D model (a flyer without one keeps its old death: nothing to fall).
    using Hidden = std::function<bool(float, float)>;
    using Modelled = std::function<bool(int)>;
    void update(const render::Effects& fx, const game::World& w, const Terrain& terr, float dt, const Hidden& hidden,
                const Modelled& modelled);

private:
    Hulk& take();
    void crashOf(Hulk& h, const game::World& w, const Terrain& terr, const game::Unit& u, const Hulk* carrier);
    std::array<Hulk, kMax> hulks_{};
    int count_ = 0;
    std::uint32_t serial_ = 0;
    std::uint32_t rng_ = 0x9E3779B9u;
    float rnd();
    std::uint32_t lastTick_ = 0;
    float stall_ = 0;
};

}  // namespace d2k::render3d
