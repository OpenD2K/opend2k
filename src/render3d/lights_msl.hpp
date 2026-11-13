#pragma once
// MSL code shared by the lit 3D shaders (render3d/lights.hpp): the dynamic point light block
// (fragment uniform buffer 1) and its evaluation. Spliced into the MSL sources by adjacent
// string literal concatenation; the HLSL twin is at the top of render3d/shaders.hlsl.

#define D2K_MSL_POINT_LIGHTS R"MSL(
struct PointLights {
    float4 info;            // x = count, y = specular multiplier, z = saturation of the sun / sky lit colour
    float4 sunColor;        // rgb: the sun's colour multiplier (time of day / Modern lighting)
    float4 ambColor;        // rgb: the ambient colour multiplier
    float4 spriteTint;      // rgb: billboards' light
    float4 posRadius[48];   // world xyz, radius
    float4 color[48];       // rgb * intensity
    float4 spot[48];        // xyz beam direction, w = cos half angle (< -1: point light)
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
// Sum of the point lights at world point p with normal n; wrap 0 = Lambert, 1 = no normal
// dependence (billboards). Spot lights fade out over the outer third of their cone.
float3 pointLights(constant PointLights& L, float3 p, float3 n, float wrap) {
    float3 sum = float3(0.0);
    const int cnt = int(L.info.x);
    for (int i = 0; i < cnt; ++i) {
        const float3 d = L.posRadius[i].xyz - p;
        const float r = L.posRadius[i].w;
        const float d2 = dot(d, d);
        if (d2 >= r * r) continue;
        float att = 1.0 - d2 / (r * r);
        att *= att;
        const float3 dir = d * rsqrt(max(d2, 1e-4));
        const float4 sp = L.spot[i];
        if (sp.w > -1.5) {
            const float c = dot(-dir, sp.xyz);
            att *= smoothstep(sp.w, sp.w + (1.0 - sp.w) * 0.45, c);
        }
        const float ndl = saturate(dot(n, dir) * (1.0 - wrap) + wrap);
        sum += L.color[i].rgb * (att * ndl);
    }
    return sum;
}
)MSL"
