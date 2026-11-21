#pragma once
// MSL of the screen-space ambient occlusion (render3d/ssao.*, docs/3d-mode.md "Ambient occlusion"). HLSL twin:
// the "ambient occlusion" block of render3d/shaders.hlsl - change both together. Both passes are full-screen
// triangles (the vertex shader is the particles' composite one, fxc_vs) over a half-resolution target.
//
// ssao_fs: from the half-size scene depth (terrain, rocks, models; drawn before the scene) the share of the sky
//   a pixel sees: its view-space position and normal (from the depth's own differences, the nearer neighbour on
//   each axis), then 10 taps on a rotated spiral inside a world-space radius - each tap counts by how high it
//   stands above the pixel's tangent plane, fading out with its distance (the reach: a roof two storeys above
//   the ground at a wall's foot still counts, nothing far in front or behind does - no halo round a unit).
// ssaoblur_fs: a 5 x 5 depth-aware blur of that (the rotation's noise gone, edges kept).

namespace d2k::render3d::msl {

inline constexpr const char* kSsao = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct SsIn {
    float4 pos [[position]];
};
struct SsaoU {
    float4 proj;  // x, y = tan of half the horizontal / vertical field of view, z = near plane, w = far plane
    float4 tex;   // xy = 1 / the depth texture's size, zw = the half-resolution viewport (px)
    float4 k;     // x radius on screen (world px), y strength, z angle bias, w reach (world px: an occluder's distance)
};
float ssDist(float z, constant SsaoU& u) { return u.proj.z * u.proj.w / (u.proj.w - z * (u.proj.w - u.proj.z)); }
// The view-space position (x right, y up, z = distance along the view) of the scene at pixel px.
float3 ssPos(depth2d<float> d, sampler s, float2 px, constant SsaoU& u) {
    const float dist = ssDist(d.sample(s, px * u.tex.xy, level(0)), u);
    const float2 ndc = float2(px.x / u.tex.z * 2.0 - 1.0, 1.0 - px.y / u.tex.w * 2.0);
    return float3(ndc * u.proj.xy * dist, dist);
}

fragment float4 ssao_fs(SsIn in [[stage_in]], constant SsaoU& u [[buffer(0)]], depth2d<float> depth [[texture(0)]],
                        sampler ds [[sampler(0)]]) {
    const float2 px = in.pos.xy;
    if (depth.sample(ds, px * u.tex.xy, level(0)) >= 0.99999) return float4(1.0);
    const float3 P = ssPos(depth, ds, px, u);
    const float3 Pl = ssPos(depth, ds, px - float2(1.0, 0.0), u), Pr = ssPos(depth, ds, px + float2(1.0, 0.0), u);
    const float3 Pt = ssPos(depth, ds, px - float2(0.0, 1.0), u), Pb = ssPos(depth, ds, px + float2(0.0, 1.0), u);
    const float3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
    const float3 dy = abs(Pb.z - P.z) < abs(P.z - Pt.z) ? Pb - P : P - Pt;
    float3 n = normalize(cross(dy, dx));
    if (dot(n, P) > 0.0) n = -n;  // towards the eye
    // The radius in half-resolution pixels (capped: zoomed in close the taps stay near each other).
    const float rpx = min(u.k.x / (P.z * u.proj.x) * u.tex.z * 0.5, 40.0);
    const float rot = 6.2831853 * fract(52.9829189 * fract(dot(px, float2(0.06711056, 0.00583715))));
    const float r2 = u.k.w * u.k.w;
    float occ = 0.0;
    for (int i = 0; i < 10; ++i) {
        const float t = (float(i) + 0.5) / 10.0;
        const float a = rot + float(i) * 2.399963;
        const float2 q = floor(px + float2(cos(a), sin(a)) * (rpx * pow(t, 0.75) + 0.8)) + 0.5;
        const float3 v = ssPos(depth, ds, q, u) - P;
        const float vv = dot(v, v);
        occ += max(dot(v, n) * rsqrt(vv + 1e-3) - u.k.z, 0.0) * saturate(1.0 - vv / r2);
    }
    // (Zoomed far out the radius is under a pixel: no occlusion rather than noise.)
    const float on = smoothstep(0.6, 1.6, rpx);
    const float ao = mix(1.0, saturate(1.0 - occ * (u.k.y / 10.0)), on);
    return float4(ao, ao, ao, 1.0);
}

fragment float4 ssaoblur_fs(SsIn in [[stage_in]], constant SsaoU& u [[buffer(0)]], texture2d<float> aoTex [[texture(0)]],
                            depth2d<float> depth [[texture(1)]], sampler as [[sampler(0)]], sampler ds [[sampler(1)]]) {
    const float2 px = in.pos.xy;
    const float z0 = depth.sample(ds, px * u.tex.xy, level(0));
    if (z0 >= 0.99999) return float4(1.0);
    const float d0 = ssDist(z0, u);
    const float wpp = d0 * u.proj.y * 2.0 / u.tex.w;  // world px per half-resolution pixel at this depth
    float sum = 0.0, wsum = 0.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) {
            const float2 o = float2(float(i), float(j));
            const float2 uv = (px + o) * u.tex.xy;
            const float d = ssDist(depth.sample(ds, uv, level(0)), u);
            // (A tilted ground plane's own depth slope across the kernel is not an edge.)
            const float w = saturate(1.0 - abs(d - d0) / ((1.5 + 1.5 * length(o)) * wpp + 0.5)) * (1.0 - 0.06 * dot(o, o));
            sum += aoTex.sample(as, uv, level(0)).r * w;
            wsum += w;
        }
    const float ao = sum / max(wsum, 1e-4);
    return float4(ao, ao, ao, 1.0);
}
)MSL";

}  // namespace d2k::render3d::msl
