#pragma once
// MSL code shared by the lit 3D shaders (render3d/lights.hpp): the light block (fragment uniform buffer 1)
// and the clustered light grid (fragment storage buffer 0 = [[buffer(<uniform count>)]]) and their
// evaluation. Spliced into the MSL sources by adjacent string literal concatenation; the HLSL twin is at
// the top of render3d/shaders.hlsl.

#define D2K_MSL_POINT_LIGHTS R"MSL(
struct PointLights {
    float4 info;            // x = light count, y = specular multiplier, z = saturation of the sun / sky lit colour
    float4 sunColor;        // rgb: the sun's colour multiplier (time of day / Modern lighting)
    float4 ambColor;        // rgb: the ambient colour multiplier
    float4 spriteTint;      // rgb: billboards' light
    float4 grid;            // xy = grid origin (world x, z), z = 1 / cell size, w = cells across
    float4 grid2;           // x = cells down, y = cell table start, z = index list start (uint4 units)
};
// The frame's sun + sky light: ambient and diffuse terms (scalars of the old light) tinted.
float3 sunAmb(constant PointLights& L, float amb, float dif) {
    return amb * L.ambColor.rgb + dif * L.sunColor.rgb;
}
// The time of day's saturation (night: towards grey, then the blue moonlight tints it); 1 = as is.
float3 satur(constant PointLights& L, float3 c) {
    if (abs(L.info.z - 1.0) < 0.001) return c;
    const float l = dot(c, float3(0.299, 0.587, 0.114));
    return max(mix(float3(l), c, L.info.z), 0.0);
}
// Sum of the lights at world point p with normal n; wrap 0 = Lambert, 1 = no normal dependence
// (billboards). Only the lights of p's grid cell are visited. Spot lights fade out over the outer
// 45 % of their cone.
float3 pointLights(constant PointLights& L, const device uint4* G, float3 p, float3 n, float wrap) {
    float3 sum = float3(0.0);
    if (L.info.x < 0.5) return sum;
    const float2 g = (p.xz - L.grid.xy) * L.grid.z;
    if (g.x < 0.0 || g.y < 0.0 || g.x >= L.grid.w || g.y >= L.grid2.x) return sum;
    const uint c = uint(g.y) * uint(L.grid.w) + uint(g.x);
    const uint e = G[uint(L.grid2.y) + (c >> 2)][c & 3u];
    const uint cnt = e & 0xFFu;
    const uint first = uint(L.grid2.z) * 4u + (e >> 8);
    for (uint k = 0; k < cnt; ++k) {
        const uint w = first + k;
        const uint li = G[w >> 2][w & 3u] * 3u;
        const float4 pr = as_type<float4>(G[li]);
        const float3 d = pr.xyz - p;
        const float r2 = pr.w * pr.w;
        const float d2 = dot(d, d);
        if (d2 >= r2) continue;
        float att = 1.0 - d2 / r2;
        att *= att;
        const float3 dir = d * rsqrt(max(d2, 1e-4));
        const float4 sp = as_type<float4>(G[li + 2u]);
        if (sp.w > -1.5) {
            const float cs = dot(-dir, sp.xyz);
            att *= smoothstep(sp.w, sp.w + (1.0 - sp.w) * 0.45, cs);
        }
        const float ndl = saturate(dot(n, dir) * (1.0 - wrap) + wrap);
        sum += as_type<float4>(G[li + 1u]).rgb * (att * ndl);
    }
    return sum;
}
)MSL"
