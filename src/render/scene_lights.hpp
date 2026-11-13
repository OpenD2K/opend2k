#pragma once
// Lamps on units and buildings for the time of day (docs/enhanced-graphics.md "Lighting"):
// vehicle headlight cones and red tail lights, harvester work lamps and beacons, navigation lights
// on flyers, building lamps (beacons, lit doors / bays, refinery glow, starport landing lights,
// repair pad lights, base floodlights). Strong at dusk / night, faint at day (SceneLight::lamps).
//
// Render only: rebuilt every frame from the World (read, never written), timed by the effect
// clock (game ticks / 20), so screenshots stay reproducible. Lamps on fully shrouded cells, of
// cloaked units and of dying buildings are left out. A building model's "fx_light*" anchors
// (3D with models; docs/3d-models.md "Anchors") place its lamps; otherwise per-type points on the
// footprint (the original sprites and rendered packs).
//
// 2D draws them itself (pools / cones with the Light blend dst * (1 + c), then the lamps' own
// flares additively); 3D turns the pools / cones into point / spot lights (render3d/lights.hpp)
// and the flares into additive billboards (Renderer3D).

#include "formats/templates.hpp"
#include "render/lighting.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Vertex;

namespace d2k::game {
class World;
}

namespace d2k::render {

struct Camera;
class Effects;
class EffectAnchors;

struct Lamp {
    enum Kind : std::uint8_t { Pool, Cone };
    Kind kind = Pool;
    float x = 0, y = 0;        // ground point under the source (world px)
    float z = 0;               // height of the source above the ground
    float reach = 0;           // pool: diameter on the ground; cone: length (world px)
    float dirX = 0, dirY = 1;  // cone: direction on the ground (unit)
    float spread = 0.45f;      // cone: half angle (radians)
    float r = 1, g = 1, b = 1, a = 0;  // colour, a = intensity
    float priority = 1;        // 3D point-light slots: higher first (then nearer the view centre)
};

// The visible lamp itself: a small hot glow at (x, y - z) in 2D, a billboard in 3D.
struct Flare {
    float x = 0, y = 0, z = 0;  // ground point + height
    float size = 4;             // world px
    float r = 1, g = 1, b = 1, a = 0;
    float pull = 14;            // 3D: px the billboard is pulled towards the eye (lamps on a model's surface: less)
};

// Emission strength of a building's glowing surfaces (docs/3d-models.md "Emission": the redesigned models'
// lamps, cyan louvres, lit windows; 3D model_fs, 2D the frames' .glow.png): 0 without lamps (Day + Classic:
// unchanged), about 0.35 by day with Modern lighting, 1.2 at night; off while building up, being sold or
// destroyed, flickering below half health; off without power and flickering dim on low power, except the
// wind traps, whose cores pulse with the energy they make. time: effect seconds.
// dayPulse (3D models): the wind trap's pulse also reads by day - it follows the rhythm of the original's
// working animation (the building's animation counter) and adds up to ~0.5 on its peaks when the lamps
// are low (Day + Modern: 0.2 .. 0.84; night 0.66 .. 1.2); off while the side has no power at all.
float buildingGlow(const game::World& w, const formats::Templates& t, int side, int building, float lamps, double time,
                   bool dayPulse = false);

class SceneLights {
public:
    SceneLights(SDL_Renderer* r, const formats::Templates& t);
    ~SceneLights();
    SceneLights(const SceneLights&) = delete;
    SceneLights& operator=(const SceneLights&) = delete;

    // Rebuilds the lamps. time: effect seconds; view: the ground rectangle to keep lamps for
    // (x0, y0, x1, y1 world px); fx: the Modern effects' eased vehicle facing (nullptr: the model
    // facing); anchors: 3D models' fx_light* points (nullptr: per-type points).
    // redesigned: per building type, non-zero = drawn from a redesigned art (a 3D model, or a 2D frame rendered
    // from one): its lamps are the model's own (fx_light* anchors, emissive surfaces), not the per-type points
    // measured on the original sprites (those floated beside / in front of the new buildings). nullptr = none.
    // redesignedUnits: the same per unit type: a vehicle / flyer drawn from a redesigned art gets only the lamps
    // its model has as fx_light_head* / _tail* / _beacon* / _work* / _port* / _starboard* / _strobe* anchors
    // (3D: the model's; 2D: the rendered pack's models.ini, PackAnchors), none at the original sprite's offsets.
    void update(const game::World& w, const SceneLight& light, double time, const float view[4], bool showShroud,
                const Effects* fx, const EffectAnchors* anchors, const std::vector<std::uint8_t>* redesigned = nullptr,
                const std::vector<std::uint8_t>* redesignedUnits = nullptr);
    const std::vector<Lamp>& lamps() const { return lamps_; }
    const std::vector<Flare>& flares() const { return flares_; }
    bool empty() const { return lamps_.empty() && flares_.empty(); }

    // 2D: the pools and cones (Light blend), then the flares (additive), in the view. grade: the
    // time of day's 2D grade the map got (MapRenderer::lightingGrade); the lamps make up for it.
    void setGrade(const float grade[3]) {
        for (int i = 0; i < 3; ++i) boost_[i] = 1.0f / std::max(0.25f, grade[i]);
    }
    void draw2D(const Camera& cam, int viewW, int viewH);
    // Textures for the 3D billboards: a soft round glow (premultiplied white).
    SDL_Texture* flareTexture() const { return flareTex_; }

private:
    void buildTextures();
    void addVehicle(const game::World& w, int side, int index, float facing, float lampK, double time);
    void addFlyer(const game::World& w, int side, int index, float lampK, double time);
    // A unit drawn from a redesigned art: its model's fx_light* anchors (nothing without them).
    void addModelUnit(const game::World& w, int side, int index, float facing, float lampK, double time,
                      const EffectAnchors* anchors);
    void addBuilding(const game::World& w, int side, int index, float lampK, double time, const EffectAnchors* anchors,
                     int redesigned, const SceneLight& light);  // redesigned: 1 = art-pack frame, 2 = 3D model
    void pool(float x, float y, float z, float reach, std::uint32_t rgb, float a, float prio = 1);
    void cone(float x, float y, float z, float len, float dx, float dy, float spread, std::uint32_t rgb, float a,
              float prio = 2);
    void flare(float x, float y, float z, float size, std::uint32_t rgb, float a, float pull = 14);

    SDL_Renderer* r_;
    const formats::Templates& t_;
    SDL_Texture* poolTex_ = nullptr;   // round, premultiplied (Light blend)
    SDL_Texture* coneTex_ = nullptr;   // apex at the left middle, opening to the right
    SDL_Texture* flareTex_ = nullptr;  // hot core + soft halo (additive)
    bool lightBlendOk_ = true;
    float boost_[3] = {1, 1, 1};
    std::vector<Lamp> lamps_;
    std::vector<Flare> flares_;
    std::vector<SDL_Vertex> vPool_, vCone_, vFlare_;
    // Per unit / building type: 0 none, else a kind (scene_lights.cpp).
    std::vector<std::uint8_t> unitKind_, buildingKind_;
    float view_[4] = {};
    bool showShroud_ = true;
    const game::World* world_ = nullptr;
    bool hidden(const game::World& w, float x, float y) const;
};

}  // namespace d2k::render
