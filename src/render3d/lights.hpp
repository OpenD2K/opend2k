#pragma once
// Dynamic point lights of the 3D mode (docs/enhanced-graphics.md "Light list", docs/3d-mode.md
// "Dynamic lights"): a small fixed-size list per frame, uploaded as fragment uniform buffer 1 of
// every lit shader (terrain_fs, terrain2_fs, rock_fs, model_fs, sprite_fs; MSL [[buffer(1)]],
// HLSL b1 space3). Each light adds colour * (1 - d^2 / r^2)^2 * (wrapped N.L) on top of the
// sun-lit albedo, before fog and shroud (so a light under the shroud stays hidden).
//
// Sources: the Modern effects' light list (render::Effects::lights(): explosions and their
// flashes, muzzle flashes, projectiles, fires) and the lamps of the time of day
// (render::SceneLights: headlight cones = spot lights, tail lights, work lamps, building lamps),
// picked by Renderer3D::buildPointLights.

#include <cstring>

namespace d2k::render3d {

inline constexpr int kMaxPointLights = 48;

// Fragment uniform block, std140-compatible (float4 arrays). The head carries the frame's global
// light (time of day / Modern lighting, render/lighting.hpp): every lit shader multiplies its
// ambient term by ambColor and its sun term by sunColor (1 = the old light), billboards take
// spriteTint; info.y scales the glTF materials' sun highlight.
struct PointLightsU {
    float info[4];                        // x = light count, y = specular multiplier, z = saturation (1 = as is), w unused
    float sunColor[4];                    // rgb: the sun's (diffuse) colour multiplier
    float ambColor[4];                    // rgb: the ambient (sky) colour multiplier
    float spriteTint[4];                  // rgb: billboards' light (flat sunlit ground vs the old light)
    float posRadius[kMaxPointLights][4];  // world position (x, height, y) and radius (world px)
    float color[kMaxPointLights][4];      // rgb = colour * intensity (1 = doubles a sunlit albedo), a unused
    float spot[kMaxPointLights][4];       // xyz = beam direction (unit), w = cos of its half angle; w < -1 = point light
    PointLightsU() { clear(); }
    // No lights, the old light (colours 1).
    void clear() {
        std::memset(this, 0, sizeof *this);
        info[1] = 1;
        info[2] = 1;
        for (int i = 0; i < 3; ++i) sunColor[i] = ambColor[i] = spriteTint[i] = 1;
        for (auto& s : spot) s[3] = -2;
    }
    void setScene(const float sun[3], const float amb[3], const float tint[3], float specular, float saturation = 1) {
        info[2] = saturation;
        for (int i = 0; i < 3; ++i) {
            sunColor[i] = sun[i];
            ambColor[i] = amb[i];
            spriteTint[i] = tint[i];
        }
        info[1] = specular;
    }
};
static_assert(sizeof(PointLightsU) == 64 + kMaxPointLights * 48, "PointLightsU layout");

}  // namespace d2k::render3d
