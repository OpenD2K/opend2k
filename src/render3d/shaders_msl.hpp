#pragma once
// Metal Shading Language sources of the 3D mode, compiled at runtime by SDL_gpu's Metal backend
// (SDL_GPU_SHADERFORMAT_MSL). Resource slots follow SDL_CreateGPUShader's MSL rules: uniform
// buffers at [[buffer(0..)]], sampled textures at [[texture(0..)]] with the matching
// [[sampler(0..)]]; vertex buffers come in through [[stage_in]].
//
// Vulkan / D3D12: src/render3d/shaders.hlsl holds the same shaders in HLSL, compiled offline to
// SPIR-V (tools/compile_shaders.sh -> shaders_spirv.inc) and on Windows to DXIL (dxc at build
// time); render3d/shader_blobs.* picks the format per device. Change both files together.

#include "render3d/lights_msl.hpp"

namespace d2k::render3d::msl {

inline constexpr const char* kTerrain = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float sun [[attribute(2)]];
};
struct VOut {
    float4 pos [[position]];
    float3 wpos;
    float3 nrm;
    float sun;
};
struct VU {
    float4x4 viewProj;
};
vertex VOut terrain_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]]) {
    VOut o;
    o.pos = u.viewProj * float4(in.pos, 1.0);
    o.wpos = in.pos;
    o.nrm = in.nrm;
    o.sun = in.sun;
    return o;
}

struct FU {
    float4 sunDir;    // xyz towards the sun
    float4 light;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 fogColor;  // rgb, a = unused
    float4 fog;       // eye xyz, fog start
    float4 fog2;      // fog end, map width px, map height px, shroud on
    float4x4 lightVP; // sun shadow map
    float4 shadow;    // on, texel (uv), -, depth bias
};
// Sun shadow map (docs/3d-mode.md "Sun shadows"): lp = position in the light's clip space,
// sh = (on, texel size in uv, -, depth bias). 3x3 PCF of hardware 2x2 compares: soft edges.
float shadowPCF(depth2d<float> sm, sampler ss, float3 lp, float4 sh) {
    const float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (sh.x < 0.5 || any(uv <= float2(0.0)) || any(uv >= float2(1.0)) || lp.z >= 1.0) return 1.0;
    const float ref = lp.z - sh.w;
    float s = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
            s += sm.sample_compare(ss, uv + float2(float(i), float(j)) * sh.y * 1.25, ref, level(0));
    return s / 9.0;
}

fragment float4 terrain_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                           texture2d_array<float> tiles [[texture(0)]],
                           texture2d<float> tileMap [[texture(1)]],
                           texture2d<float> shroud [[texture(2)]],
                           depth2d<float> shadowMap [[texture(3)]],
                           sampler tileSamp [[sampler(0)]], sampler mapSamp [[sampler(1)]],
                           sampler shroudSamp [[sampler(2)]], sampler shadowSamp [[sampler(3)]]) {
    const float2 g = in.wpos.xz / 32.0;
    const uint2 msize = uint2(tileMap.get_width(), tileMap.get_height());
    const uint2 cell = uint2(clamp(floor(g), float2(0.0), float2(msize) - 1.0));
    const uint tile = uint(tileMap.read(cell).r + 0.5);  // exact integer in R32_FLOAT
    const float2 uv = clamp(g - float2(cell), 0.0, 1.0);
    // Gradients from the continuous coordinate: no mip seams at the tile borders.
    float4 c = tiles.sample(tileSamp, uv, tile, gradient2d(dfdx(g), dfdy(g)));
    const float3 n = normalize(in.nrm);
    const float ndl = max(dot(n, u.sunDir.xyz), 0.0);
    // Sun: the shadow map, else the baked soft heightfield shadow.
    const float sm = u.shadow.x > 0.5 ? shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow) : in.sun;
    const float vis = mix(u.light.w, 1.0, sm);
    const float3 shade = sunAmb(pl, u.light.x, u.light.y * ndl * vis) * u.light.z;
    float3 rgb = satur(pl, c.rgb) * shade + c.rgb * pointLights(pl, in.wpos, n, 0.3);
    // Darkness: the shroud mask, and a thin fade into the black beyond the map edge. The haze
    // is darkened too, so shrouded ground stays black at any distance.
    float s = 0.0;
    if (u.fog2.w > 0.5) s = shroud.sample(shroudSamp, in.wpos.xz / u.fog2.yz).a;
    const float2 e = min(in.wpos.xz, u.fog2.yz - in.wpos.xz);
    s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(in.wpos, u.fog.xyz);
    const float f = smoothstep(u.fog.w, u.fog2.x, d);
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

inline constexpr const char* kSprite = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VIn {
    float3 pos [[attribute(0)]];     // corner, world
    float3 anchor [[attribute(1)]];  // depth anchor, world (billboards: the whole quad gets its depth)
    float2 uv [[attribute(2)]];
    float4 color [[attribute(3)]];
    float4 params [[attribute(4)]];  // x: 1 = billboard depth, y: flash (added grey), zw: ground xz (shroud)
    float3 spos [[attribute(5)]];    // world point whose sun shadow applies (billboards: their ground point)
};
struct VOut {
    float4 pos [[position]];
    float2 uv;
    float4 color;
    float flash;
    float2 ground;
    float fog;
    float3 lpos;
    float3 wpos;
};
struct VU {
    float4x4 viewProj;
    float4 eye;   // xyz, w = fog start
    float4 fog;   // x = fog end
    float4x4 lightVP;
};
vertex VOut sprite_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]]) {
    VOut o;
    float4 p = u.viewProj * float4(in.pos, 1.0);
    if (in.params.x > 0.5) {
        const float4 a = u.viewProj * float4(in.anchor, 1.0);
        if (a.w > 1e-4) p.z = clamp(a.z / a.w, 0.0, 1.0) * p.w;
    }
    o.pos = p;
    o.uv = in.uv;
    o.color = in.color;
    o.flash = in.params.y;
    o.ground = in.params.zw;
    o.fog = smoothstep(u.eye.w, u.fog.x, distance(in.anchor, u.eye.xyz));
    o.lpos = (u.lightVP * float4(in.spos, 1.0)).xyz;
    o.wpos = in.pos;
    return o;
}

struct FU {
    float4 mode;      // x: 0 alpha, 1 additive / subtractive, 2 multiply; y: shroud on; zw: map size px
    float4 fogColor;
    float4 shadow;    // on, texel (uv), brightness in full shadow, depth bias
};
// Sun shadow map (docs/3d-mode.md "Sun shadows"): lp = position in the light's clip space,
// sh = (on, texel size in uv, -, depth bias). 3x3 PCF of hardware 2x2 compares: soft edges.
float shadowPCF(depth2d<float> sm, sampler ss, float3 lp, float4 sh) {
    const float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (sh.x < 0.5 || any(uv <= float2(0.0)) || any(uv >= float2(1.0)) || lp.z >= 1.0) return 1.0;
    const float ref = lp.z - sh.w;
    float s = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
            s += sm.sample_compare(ss, uv + float2(float(i), float(j)) * sh.y * 1.25, ref, level(0));
    return s / 9.0;
}

fragment float4 sprite_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                          texture2d<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                          depth2d<float> shadowMap [[texture(2)]],
                          sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]],
                          sampler shadowSamp [[sampler(2)]]) {
    float4 c = tex.sample(texSamp, in.uv);
    c *= in.color;
    c.rgb = min(c.rgb + in.flash * c.a, 1.0);
    // Darkness of the map point this pixel covers in the 2D view (decals: the ground under it);
    // nothing outside the map is ever visible.
    float dark = 0.0;
    if (u.mode.y > 0.5) dark = shroud.sample(shroudSamp, in.ground / u.mode.zw).a;
    if (any(in.ground < float2(0.0)) || any(in.ground >= u.mode.zw)) dark = 1.0;
    if (u.mode.x < 0.5) {
        if (c.a < 0.004 || dark > 0.996) discard_fragment();
        // Sprites receive the sun shadow at their ground point (units in a cliff's shade), in the
        // light of the time of day.
        const float3 alb = c.rgb;
        if (u.shadow.x > 0.5) c.rgb *= mix(u.shadow.z, 1.0, shadowPCF(shadowMap, shadowSamp, in.lpos, u.shadow));
        c.rgb = satur(pl, c.rgb) * pl.spriteTint.rgb;
        // Dynamic lights (explosions, muzzle flashes, fires, lamps) on units, buildings and smoke.
        c.rgb += alb * pointLights(pl, in.wpos, float3(0.0, 1.0, 0.0), 0.8);
        c.rgb = mix(c.rgb, u.fogColor.rgb, in.fog) * (1.0 - dark);
    } else if (u.mode.x < 1.5) {
        c.rgb *= (1.0 - dark) * (1.0 - in.fog);
    } else {
        c.rgb = mix(c.rgb, float3(1.0), max(dark, in.fog));
    }
    return c;
}
)MSL";


// HDR tone map (render/hdr.*; docs/enhanced-graphics.md "Quality"): a custom fragment shader of
// SDL's GPU renderer (SDL_GPURenderState) - the vertex stage is SDL's own (colour at location 0,
// uv at location 1; the drawn texture in slot 0, our bloom texture in slot 1, uniforms in buffer 0).
// tonemap_fs in render3d/shaders.hlsl is the twin - change both together.
inline constexpr const char* kTonemap = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct TmIn {
    float4 color [[user(locn0)]];
    float2 uv [[user(locn1)]];
};
// a = (exposure, peak, knee, bloom strength), b = (bloom uv scale x, y, input linear, output linear),
// c = (highlight desaturation, -, -, -)
struct TmU { float4 a; float4 b; float4 c; };
static float3 toLin(float3 c) {
    c = max(c, float3(0.0));
    return select(pow((c + 0.055) / 1.055, float3(2.4)), c / 12.92, c <= float3(0.04045));
}
static float3 toSrgb(float3 c) {
    c = max(c, float3(0.0));
    return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c <= float3(0.0031308));
}
fragment float4 tonemap_fs(TmIn in [[stage_in]], constant TmU& u [[buffer(0)]], texture2d<float> scene [[texture(0)]],
                           sampler sceneSamp [[sampler(0)]], texture2d<float> bloom [[texture(1)]],
                           sampler bloomSamp [[sampler(1)]]) {
    const float3 s = scene.sample(sceneSamp, in.uv).rgb * in.color.rgb;
    const float3 bl = max(bloom.sample(bloomSamp, in.uv * u.b.xy).rgb, float3(0.0)) * u.a.w;
    // Gamma-encoded input (the usual sRGB renderer): the bloom is added as the 8-bit path adds it
    // (SDL encodes the linear bloom texture to sRGB), then everything goes to linear light.
    float3 c = u.b.z > 0.5 ? max(s, float3(0.0)) + bl : toLin(s + toSrgb(bl));
    c *= u.a.x;
    // Hue-preserving filmic shoulder: identity up to the knee, then an exponential roll-off that
    // approaches the peak (1 = SDR white; the display's headroom with EDR output). Very hot
    // pixels drift towards white (fire cores, flashes), as film does.
    const float m = max(c.r, max(c.g, c.b));
    const float knee = u.a.z, peak = u.a.y, room = peak - knee;
    if (m > knee) {
        if (room <= 1e-4) {
            c = min(c, float3(peak));
        } else {
            const float mm = knee + room * (1.0 - exp(-(m - knee) / room));
            const float3 t = c * (mm / m);
            const float w = clamp((m - peak) / (3.0 * peak), 0.0, 1.0) * u.c.x;
            c = mix(t, float3(mm), w);
        }
    }
    return float4(u.b.w > 0.5 ? c : toSrgb(c), 1.0);
}
)MSL";

}  // namespace d2k::render3d::msl
