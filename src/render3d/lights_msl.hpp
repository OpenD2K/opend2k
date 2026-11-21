#pragma once
// MSL code shared by the lit 3D shaders (render3d/lights.hpp): the light block (fragment uniform buffer 1)
// and the clustered light grid (fragment storage buffer 0 = [[buffer(<uniform count>)]]) and their
// evaluation. Spliced into the MSL sources by adjacent string literal concatenation; the HLSL twin is at
// the top of render3d/shaders.hlsl.

#define D2K_MSL_POINT_LIGHTS R"MSL(
struct PointLights {
    float4 info;            // x = light count, y = specular multiplier, z = saturation of the sun / sky lit colour,
                            // w = Aerial haze: 1 - the fog's cap (0 = off)
    float4 sunColor;        // rgb: the sun's colour multiplier (time of day / Modern lighting)
    float4 ambColor;        // rgb: the ambient colour multiplier
    float4 spriteTint;      // rgb: billboards' light
    float4 grid;            // xy = grid origin (world x, z), z = 1 / cell size, w = cells across
    float4 grid2;           // x = cells down, y = cell table start, z = index list start (uint4 units)
    float4 haze;            // Dust haze: xy wind direction (world x, z), z px drifted, w strength (0 = off)
    float4 grade;           // Realistic grade (docs/3d-mode.md): x the models' albedo scale (0 = off), y sand dust on models
    float4 dustColor;       // rgb: that dust's colour (the ground's sand)
    float4 ssao;            // Ambient occlusion (docs/3d-mode.md): xy = 1 / the scene target's size (px -> uv), z = strength (0 = off)
    float4 edge;            // World beyond the map (docs/3d-mode.md "Shroud and the map edge"): x = 1 band / 2 full landscape (0 = off), y = the
                            // land beyond's fixed fog darkness, z = its vignette (darkening far out), w = its distance haze share
};
// The shroud darkness at world point p (xz, px; mapSize = the map in px) from the shroud mask t (clamp sampler s). Inside
// the map, and always with the world beyond the map off, the mask itself. Beyond the map the land never depends on
// exploration: the map edge's darkness - blurred along the edge (more with distance: no per-cell stripes; at a corner
// the two edges weighted by direction) - blends over ~3 cells into the fixed fog level edge.y, then fades to full darkness between ~4.5 and ~11 cells out (only a band of the land beyond shows).
float shroudAt(constant PointLights& L, texture2d<float> t, sampler s, float2 p, float2 mapSize) {
    const float2 o = max(max(-p, p - mapSize), float2(0.0));
    if (L.edge.x < 0.5 || o.x + o.y <= 0.0) return t.sample(s, p / mapSize).a;
    const float d = length(o);
    const float2 e = clamp(p, float2(0.0), mapSize);
    const float r = 80.0 * smoothstep(0.0, 96.0, d);  // sideways reach of the blur (0 at the edge: the map's own value)
    const float wx = o.x / (o.x + o.y);               // share of the taps along a left / right edge (they vary y)
    float acc = 0.0, wsum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        const float k = exp(-float(i * i) / 5.0), off = float(i) * (r / 3.0);
        float v = 0.0;
        if (wx > 0.0) v += wx * t.sample(s, float2(e.x, clamp(e.y + off, 0.0, mapSize.y)) / mapSize, level(0.0)).a;
        if (wx < 1.0) v += (1.0 - wx) * t.sample(s, float2(clamp(e.x + off, 0.0, mapSize.x), e.y) / mapSize, level(0.0)).a;
        acc += k * v;
        wsum += k;
    }
    // edge.x 1 ("Near"): the explored edge opens the land beyond it - the edge's own darkness, blurred sideways - for
    // ~3 cells, fading to full darkness by ~5.5 cells (unexplored edges stay black); 2 ("Full"): the whole landscape at
    // the fixed fog level edge.y, a gentle vignette to the horizon.
    if (L.edge.x < 1.5) return 1.0 - (1.0 - acc / wsum) * (1.0 - smoothstep(96.0, 176.0, d));
    const float so = mix(acc / wsum, L.edge.y, smoothstep(0.0, 92.0, d));
    return 1.0 - (1.0 - so) * (1.0 - L.edge.z * smoothstep(120.0, 1100.0, d));
}
// Beyond the map: the share of the distance haze the land fades into with its distance from the map (0 inside / off).
float edgeHazeK(constant PointLights& L, float2 p, float2 mapSize) {
    if (L.edge.x < 0.5) return 0.0;
    return L.edge.w * smoothstep(96.0, 1400.0, length(max(max(-p, p - mapSize), float2(0.0))));
}
// Screen-space ambient occlusion (render3d/ssao.*, docs/3d-mode.md "Ambient occlusion"): the share of the sky light
// that reaches the pixel at fragXY (1 = all of it; always 1 when off). For the ambient term only.
float ssaoAt(constant PointLights& L, texture2d<float> t, sampler s, float2 fragXY) {
    if (L.ssao.z <= 0.0) return 1.0;
    return mix(1.0, t.sample(s, fragXY * L.ssao.xy, level(0.0)).r, L.ssao.z);
}
// Reimagined "Dust haze" (docs/3d-mode.md "Dust haze"; Arrakis has no clouds): wind-blown dust veils high up,
// drifting across the map with the wind - broad soft streaks, long along the wind and narrow across it, that dim
// the light by up to ~11 % and warm it towards sand. Returns the light multiplier at world point p (1 = clear).
float hzHash(float2 p) {
    float3 q = fract(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float hzNoise(float2 p) {
    const float2 i = floor(p), f = fract(p), w = f * f * (3.0 - 2.0 * f);
    return mix(mix(hzHash(i), hzHash(i + float2(1.0, 0.0)), w.x), mix(hzHash(i + float2(0.0, 1.0)), hzHash(i + float2(1.0, 1.0)), w.x), w.y);
}
float3 dustHaze(constant PointLights& L, float3 p) {
    if (L.haze.w <= 0.0) return float3(1.0);
    const float2 wd = L.haze.xy;
    const float a = dot(p.xz, wd) - L.haze.z;         // along the wind (drifting)
    float b = dot(p.xz, float2(-wd.y, wd.x));         // across it
    b += (hzNoise(float2(a / 800.0, b / 560.0) + 11.0) - 0.5) * 200.0;  // the streaks meander
    const float v = hzNoise(float2(a / 950.0, b / 250.0)) * 0.6 + hzNoise(float2(a / 420.0, b / 120.0) + 5.3) * 0.28 +
                    hzNoise(float2(a / 170.0, b / 64.0) + 9.1) * 0.12;
    const float k = smoothstep(0.47, 0.7, v) * L.haze.w;
    return mix(float3(1.0), float3(0.92, 0.86, 0.75), k);
}
// Reimagined "Realistic grade" (docs/3d-mode.md "Realistic grade"): fine sand dust settled on a model - on its
// up-facing surfaces (ny = the surface normal's y) and over its foot (h = px above the model's ground; dust.x =
// 1 / the foot band's height, 0 = no band; dust.y = this model's share). Light and patchy, not grime: q = the
// patches' coordinate (the model's uv, so they stay on a moving model). Returns the share of dust in the albedo
// (0 = none; always 0 with the grade off).
float sandDustK(constant PointLights& L, float2 q, float ny, float h, float4 dust) {
    if (L.grade.y <= 0.0 || dust.y <= 0.0) return 0.0;
    const float nz = hzNoise(q) * 0.6 + hzNoise(q * 3.7 + 7.0) * 0.4;
    const float low = dust.x > 0.0 ? 1.0 - smoothstep(0.0, 1.0, (h + (nz - 0.5) * 2.0) * dust.x) : 0.0;
    const float up = smoothstep(0.5, 0.95, ny) * (0.14 + 0.3 * smoothstep(0.35, 0.75, nz));
    return saturate((low * 0.62 + up) * dust.y * L.grade.y);
}
// Vehicle wrecks (docs/3d-mode.md "Vehicle wrecks"): a destroyed vehicle's hulk. wk = (how charred 0..1; the ember
// glow 0..1, or -(the share crumbled away)), q = the pattern's coordinate (the model's uv: it stays on the hull),
// cav = how deep in a seam the surface lies (0..1). Returns rgb = the burnt albedo, a = the ember glow there
// (emission); a < 0 = crumbled away (discard).
float4 wreckChar(float3 albedo, float2 wk, float2 q, float cav) {
    const float n1 = hzNoise(q), n2 = hzNoise(q * 3.1 + 17.0), n3 = hzNoise(q * 0.37 + 5.0);
    const float gone = n2 * 0.65 + n1 * 0.35 - (-wk.y * 1.08 - 0.04);
    if (wk.y < 0.0 && gone < 0.0) return float4(0.0, 0.0, 0.0, -1.0);
    // soot black-brown, patchy, with rusty scale where the paint blistered
    const float3 burnt = float3(0.060, 0.044, 0.034) * (0.45 + 1.1 * n1) + float3(0.055, 0.024, 0.009) * smoothstep(0.55, 0.9, n2);
    const float3 c = mix(albedo, burnt, saturate(wk.x * (0.84 + 0.14 * n3)));
    // embers: thin glowing veins in patches, and the seams
    const float vein = 1.0 - smoothstep(0.0, 0.05, abs(n2 - 0.5));
    float e = max(wk.y, 0.0) * (vein * smoothstep(0.5, 0.78, n3) + 0.5 * cav * smoothstep(0.35, 0.7, n1));
    if (wk.y < 0.0) e = 1.0 - smoothstep(0.0, 0.09, gone);  // the crumbling edge glows
    return float4(c, e);
}
// Reimagined "Aerial haze" (docs/3d-mode.md "Aerial haze"): the share of the fog term kept (info.w = 1 - its cap;
// 0 = the fog as it always was).
float aerialK(constant PointLights& L) {
    return 1.0 - L.info.w;
}
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
