#pragma once
// Time of day and the Modern lighting of the Enhanced / 3D views (docs/enhanced-graphics.md
// "Lighting"): the frame's global light - the sun's direction relative to the view's base sun,
// the sun (diffuse) and sky (ambient) colours as multipliers of the old light, the strength of the
// vehicle / building lamps (render/scene_lights.*), bloom and the 3D specular boost.
//
// Render only and pure (no SDL): the simulation never sees any of it. Classic lighting at Day is
// the identity - exactly the look from before this existed (the struct defaults, so one-frame /
// screenshot runs without --time-of-day / --lighting draw as before). Original graphics never
// use it.

#include <cstdint>

namespace d2k::render {

enum class TimeOfDay : std::uint8_t { Day, Dawn, Dusk, Night, Cycle };
// Classic = the old light (only the time of day changes it); Modern = the daylight look: warm
// directional sun against a cooler sky (more lit / shade contrast), bloom on highlights, specular
// glints on 3D metal, a warm grade.
enum class LightingStyle : std::uint8_t { Classic, Modern };

struct SceneLight {
    // Sun: azimuth offset (compass degrees, clockwise) and elevation factor against the view's
    // base sun (3D tiles / Designed: north-west 40 degrees; Faithful: west-south-west 48).
    float dAz = 0, elK = 1;
    float sun[3] = {1, 1, 1};  // multiplies the sun's diffuse term
    float amb[3] = {1, 1, 1};  // multiplies the ambient (sky) term
    float fog[3] = {1, 1, 1};  // 3D haze tint
    float lamps = 0;           // vehicle / building lamps, 0 (off) .. 1 (full night strength)
    float bloom = 0;           // bloom strength (0 = off)
    float bloomThreshold = 0.8f;
    float spec = 1;            // 3D: sun highlight on glTF materials
    float sat = 1;             // saturation of the sun / sky lit colour (night: towards grey)
    bool identity = true;      // Classic Day: nothing to do anywhere
    bool modern = false;       // LightingStyle::Modern (the sprites' shadows follow the sun, 3D glare)

    // The colour flat sunlit ground gets relative to the old light, for a shader whose light is
    // (ambient + diffuse * N.L) normalised by flat ground (ambient + diffuse * sunY): the 2D grade
    // over sprites and the 3D billboards' tint.
    void grade(float ambient, float diffuse, float sunY, float out[3]) const {
        const float n = ambient + diffuse * sunY;
        for (int i = 0; i < 3; ++i) out[i] = n > 0 ? (ambient * amb[i] + diffuse * sunY * sun[i]) / n : 1.0f;
    }
    // The sun's direction (unit, towards the sun; x east, y up, z south) from the base sun.
    void sunDirection(const float base[3], float out[3]) const;
};

// How the sprites' baked shadows (art-pack .shadow.png layers, the original index-1 pixels) follow
// the sun in Enhanced 2D with Modern lighting (docs/enhanced-graphics.md "Sun-following sprite
// shadows"): the shadow direction on the screen at the base sun (dx0, dy0; the art is lit from the
// north-west: towards the south-east) and now (dx, dy), the length factor cot(el) / cot(el0) and the
// mask strength (1 = as drawn; lower = fainter: long shadows fade a little, and the terrain's
// lit / shade ratio of the time of day). active = false: draw them as before (Day, Classic lighting).
struct SpriteShadowWarp {
    bool active = false;
    float dx0 = 0.70710678f, dy0 = 0.70710678f, dx = 0.70710678f, dy = 0.70710678f;
    float k = 1, strength = 1;
    // The length factor for art-pack frames rendered from 3D models (tools/render_sprites: their sun
    // stands 58 degrees high, so their shadows are shorter; the same elevation factor of that sun).
    float kPack = 1;
};
SpriteShadowWarp spriteShadowWarp(const SceneLight& l);

// The light for a setting; cycleSeconds: effect seconds (game ticks / 20) for TimeOfDay::Cycle
// (one day lasts cycleLength() seconds). lite: the iPad lite profile (no bloom, no lamps).
SceneLight sceneLight(TimeOfDay t, LightingStyle s, double cycleSeconds = 0, bool lite = false);
// Seconds per Cycle day: 960 (16 minutes of game time at the default speed); D2K_CYCLE_SECONDS
// overrides it (recordings).
double cycleLength();

const char* timeOfDayName(TimeOfDay t);  // day, dawn, dusk, night, cycle
TimeOfDay timeOfDayFrom(const char* v);
const char* lightingStyleName(LightingStyle s);  // classic, modern
LightingStyle lightingStyleFrom(const char* v);

}  // namespace d2k::render
