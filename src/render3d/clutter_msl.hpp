#pragma once
// MSL of the Reimagined ground clutter (render3d/ground_clutter.*; docs/3d-mode.md "Ground clutter"). Slot rules
// as render3d/shaders_msl.hpp. HLSL twin: the "ground clutter" part of render3d/shaders.hlsl - change both together.
//
// clutter_vs: an instanced piece (pebble, gravel patch, slab, tuft, boulder, rubble, shrub, dead wood, scrap):
//   yaw + scale, sheared onto the ground's slope (the class's share of it), normals with the inverse transpose.
//   Also the vertex shader of the clutter's sun shadow map pass (with depth_fs).
// clutter_fs: a texture-array layer per scan / procedural texture, colour-locked to the map's Reimagined palette
//   (rock layers) or as authored (plants, rust); sand dust on top faces; lit like the rocks (sun shadow map,
//   sky light brighter on top, contact darkening near the ground, the sky's blue in the shadows); shrouded,
//   faded at the map edge and hazed as the terrain. Two-sided (thin blades, plates).

#include "render3d/lights_msl.hpp"

namespace d2k::render3d::msl {

inline constexpr const char* kClutter = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 i0 [[attribute(3)]];  // instance: position xyz (ground under the centre), yaw (radians)
    float4 i1 [[attribute(4)]];  // scale xyz (world px), texture layer
    float4 i2 [[attribute(5)]];  // sun visibility (baked), height above the ground (px), dust, brightness
    float4 i3 [[attribute(6)]];  // ground slope dh/dx, dh/dz (times the class's conform share), hue shift, -
};
struct VOut {
    float4 pos [[position]];
    float3 wpos;
    float3 nrm;
    float2 uv;
    float layer;
    float sun;
    float ao;
    float bright;
    float dust;
    float hue;
};
struct VU {
    float4x4 viewProj;
};
vertex VOut clutter_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]]) {
    const float c = cos(in.i0.w), s = sin(in.i0.w);
    const float3 q = in.pos * in.i1.xyz;
    const float2 r = float2(c * q.x - s * q.z, s * q.x + c * q.z);
    const float3 w = float3(r.x, q.y + in.i3.x * r.x + in.i3.y * r.y, r.y) + in.i0.xyz;
    const float3 m = in.nrm / in.i1.xyz;  // inverse-transpose of the scale
    const float3 nr = float3(c * m.x - s * m.z, m.y, s * m.x + c * m.z);
    VOut o;
    o.pos = u.viewProj * float4(w, 1.0);
    o.wpos = w;
    o.nrm = float3(nr.x - in.i3.x * nr.y, nr.y, nr.z - in.i3.y * nr.y);  // ... and of the shear
    o.uv = in.uv;
    o.layer = in.i1.w;
    o.sun = in.i2.x;
    o.ao = saturate(q.y / max(in.i2.y, 0.5));
    o.bright = in.i2.w;
    o.dust = in.i2.z;
    o.hue = in.i3.z;
    return o;
}

struct FU {
    float4 sunDir;
    float4 light;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 fogColor;
    float4 fog;       // eye xyz, fog start
    float4 fog2;      // fog end, map width px, map height px, shroud on
    float4 tint[12];  // per layer: rgb = colour lock (target / mean luminance), a = mean luminance; a < 0: as authored x rgb
    float4 look[12];  // per layer: x = share of the texture's own hue, y = contrast kept, z = dust share, w unused
    float4 dust;      // rgb = the sand's colour (dust on top faces), a = the sky's blue in the shadows (0 = none)
    float4x4 lightVP; // sun shadow map
    float4 shadow;    // on, texel (uv), -, depth bias
};
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

fragment float4 clutter_fs(VOut in [[stage_in]], bool front [[front_facing]], constant FU& u [[buffer(0)]],
                           constant PointLights& pl [[buffer(1)]], const device uint4* plg [[buffer(2)]],
                           texture2d_array<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                           depth2d<float> shadowMap [[texture(2)]],
                           sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]],
                           sampler shadowSamp [[sampler(2)]]) {
    const uint layer = min(uint(in.layer + 0.5), 11u);
    const float3 t = tex.sample(texSamp, in.uv, layer).rgb;
    const float L = dot(t, float3(0.299, 0.587, 0.114));
    const float4 ti = u.tint[layer], lk = u.look[layer];
    float3 albedo;
    if (ti.a > 0.0) albedo = ti.rgb * mix(ti.a, L, lk.y) * mix(float3(1.0), t / max(L, 0.02), lk.x);
    else albedo = t * ti.rgb;
    albedo *= in.bright * float3(1.0 + 0.07 * in.hue, 1.0, 1.0 - 0.07 * in.hue);
    float3 n = normalize(in.nrm);
    if (!front) n = -n;
    // Sand blown onto the upward faces.
    albedo = mix(albedo, u.dust.rgb, in.dust * lk.z * smoothstep(0.35, 0.9, n.y));
    const float ndl = max(dot(n, u.sunDir.xyz), 0.0);
    const float sm = u.shadow.x > 0.5 ? shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow) : in.sun;
    const float vis = mix(u.light.w, 1.0, sm);
    // Sky light brighter on top faces, contact darkening near the ground.
    const float amb = u.light.x * (0.74 + 0.26 * max(n.y, 0.0)) * mix(0.55, 1.0, smoothstep(0.0, 0.6, in.ao));
    float3 rgb = satur(pl, albedo) * sunAmb(pl, amb, u.light.y * ndl * vis) * u.light.z + albedo * pointLights(pl, plg, in.wpos, n, 0.25);
    // The sky's blue in what the sun does not reach (as the Reimagined terrain).
    const float k = u.dust.a;
    rgb *= mix(float3(1.0 - 0.13 * k, 1.0 - 0.07 * k, 1.0 + 0.06 * k), float3(1.0), sm * saturate(ndl * 4.0 + 0.15));
    const float2 p = in.wpos.xz, mapSize = u.fog2.yz;
    float s = 0.0;
    if (u.fog2.w > 0.5 && u.fog2.w < 1.5) s = shroud.sample(shroudSamp, p / mapSize).a;
    if (u.fog2.w > 2.5) s = shroudAt(pl, shroud, shroudSamp, p, mapSize);  // (3: no edge fade; beyond the map the fixed fog)
    const float2 e = min(p, mapSize - p);
    if (u.fog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    if (s > 0.996) discard_fragment();
    rgb *= dustHaze(pl, in.wpos);  // Reimagined: drifting dust veils
    // (The world beyond the map: its pieces darker and greyer, as its ground - terrain3_fs.)
    const float outW = smoothstep(0.0, 20.0, length(max(max(-p, p - mapSize), float2(0.0))));
    const float2 bt = pl.edge.x > 0.5 ? float2(0.3, 0.12) : float2(0.22, 0.15);
    if (outW > 0.0) rgb = mix(rgb, float3(dot(rgb, float3(0.299, 0.587, 0.114))), bt.x * outW) * (1.0 - bt.y * outW);
    float f = smoothstep(u.fog.w, u.fog2.x, distance(in.wpos, u.fog.xyz)) * aerialK(pl);
    if (outW > 0.0) f = max(f, edgeHazeK(pl, p, mapSize));
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

}  // namespace d2k::render3d::msl
