// HLSL twin of the 3D mode's MSL shaders (src/render3d/shaders_msl.hpp + models_msl.hpp).
// Keep the two in step: same entry points, same uniform blocks, same maths.
//
// Built OFFLINE, the results are embedded (docs/3d-mode.md "Shaders", docs/building.md):
//   - SPIR-V (Vulkan: Windows / Linux, also MoltenVK): tools/compile_shaders.sh with glslangValidator
//     (--auto-sampled-textures turns each Texture + SamplerState pair into the combined image
//     sampler SDL_gpu's Vulkan backend binds) -> src/render3d/shaders_spirv.inc (checked in);
//   - DXIL (Direct3D 12, Windows): cmake/CompileShadersDXIL.cmake runs dxc from the Windows SDK at
//     build time -> <build>/generated/shaders_dxil.inc (D2K_HAVE_DXIL). Without dxc the Windows
//     build asks SDL for a Vulkan device instead (render3d/gpu.cpp).
// Renderer3D / ModelRenderer pick the blob matching SDL_GetGPUShaderFormats() (render3d/shader_blobs.*).
//
// Resource layout follows SDL_CreateGPUShader's rules:
//   vertex   shaders: uniforms b[n] space1                         (SPIR-V: set 1)
//                    storage buffers t[n] space0                  (SPIR-V: set 0)
//   fragment shaders: textures t[n] + samplers s[n] space2 (pairs), uniforms b[n] space3
//                                                                  (SPIR-V: set 2 / set 3)
// Vertex attributes: TEXCOORD<location> (SDL's D3D12 backend binds attribute n to TEXCOORDn;
// SPIR-V locations follow declaration order).
// Matrices: the C++ side uploads column-major float[16] and the MSL computes M * v; HLSL cbuffer
// matrices are column_major by default, so mul(M, v) is the same product.


// Sun shadow map (docs/3d-mode.md "Sun shadows"): LP = position in the light's clip space,
// SH = (on, texel size in uv, -, depth bias). 3x3 PCF of hardware 2x2 compares: soft edges.
// (A macro: glslang without legalisation does not like textures as function arguments.)
#define SHADOW_PCF(SM, SS, LP, SH, OUT) {                                                        \
        const float3 slp_ = (LP);                                                              \
        const float2 suv_ = float2(slp_.x * 0.5 + 0.5, 0.5 - slp_.y * 0.5);                      \
        OUT = 1.0;                                                                             \
        if ((SH).x > 0.5 && all(suv_ > float2(0.0, 0.0)) && all(suv_ < float2(1.0, 1.0)) && slp_.z < 1.0) { \
            float s_ = 0.0;                                                                    \
            [unroll] for (int j_ = -1; j_ <= 1; ++j_)                                          \
                [unroll] for (int i_ = -1; i_ <= 1; ++i_)                                      \
                    s_ += SM.SampleCmpLevelZero(SS, suv_ + float2(i_, j_) * (SH).y * 1.25, slp_.z - (SH).w); \
            OUT = s_ / 9.0;                                                                    \
        }                                                                                      \
    }

// ================================================================================== point lights
// Dynamic lights (render3d/lights.hpp): fragment uniform buffer 1 of every lit shader (the frame's
// sun / sky colours + the light grid's placement) and the clustered light grid, fragment storage
// buffer 0 (SDL: t<sampler count>, space2 - so one declaration per sampler count; the lookup is
// a macro instantiated per buffer, as glslang does not take resources as function arguments).
cbuffer PointLightsFU : register(b1, space3) {
    float4 plInfo;            // x = light count, y = specular multiplier, z = saturation of the sun / sky lit colour,
                              // w = Aerial haze: 1 - the fog's cap (0 = off)
    float4 plSunColor;        // rgb: the sun's colour multiplier (time of day / Modern lighting)
    float4 plAmbColor;        // rgb: the ambient colour multiplier
    float4 plSpriteTint;      // rgb: billboards' light
    float4 plGrid;            // xy = grid origin (world x, z), z = 1 / cell size, w = cells across
    float4 plGrid2;           // x = cells down, y = cell table start, z = index list start (uint4 units)
    float4 plHaze;            // Dust haze: xy wind direction (world x, z), z px drifted, w strength (0 = off)
    float4 plGrade;           // Realistic grade (docs/3d-mode.md): x the models' albedo scale (0 = off), y sand dust on models
    float4 plDustColor;       // rgb: that dust's colour (the ground's sand)
    float4 plSsao;            // Ambient occlusion (docs/3d-mode.md): xy = 1 / the scene target's size (px -> uv), z = strength (0 = off)
    float4 plEdge;            // World beyond the map (docs/3d-mode.md "Shroud and the map edge"): x = 1 on (0 = off), y = the
                              // land beyond's fixed fog darkness, z = its vignette, w = its distance haze share
};
// The shroud darkness at world point P (xz px; MS = the map in px) from the shroud mask T (clamp sampler S); MSL twin
// shroudAt in lights_msl.hpp (inside the map / surround off: the mask; beyond it the edge's darkness blurred along the
// edge, blended over ~3 cells into the fixed fog level, then a vignette). A macro: glslang does not take resources as
// function arguments.
#define SHROUD_AT(T, S, P, MS, OUT) {                                                                       \
        const float2 shp_ = (P), shm_ = (MS);                                                               \
        const float2 sho_ = max(max(-shp_, shp_ - shm_), float2(0.0, 0.0));                                 \
        if (plEdge.x < 0.5 || sho_.x + sho_.y <= 0.0) OUT = T.Sample(S, shp_ / shm_).a;                      \
        else {                                                                                              \
            const float shd_ = length(sho_);                                                                \
            const float2 she_ = clamp(shp_, float2(0.0, 0.0), shm_);                                        \
            const float shr_ = 80.0 * smoothstep(0.0, 96.0, shd_);                                          \
            const float shw_ = sho_.x / (sho_.x + sho_.y);                                                  \
            float sha_ = 0.0, shs_ = 0.0;                                                                   \
            [unroll] for (int shi_ = -3; shi_ <= 3; ++shi_) {                                               \
                const float shk_ = exp(-float(shi_ * shi_) / 5.0), sof_ = float(shi_) * (shr_ / 3.0);       \
                float shv_ = 0.0;                                                                           \
                if (shw_ > 0.0) shv_ += shw_ * T.SampleLevel(S, float2(she_.x, clamp(she_.y + sof_, 0.0, shm_.y)) / shm_, 0.0).a; \
                if (shw_ < 1.0) shv_ += (1.0 - shw_) * T.SampleLevel(S, float2(clamp(she_.x + sof_, 0.0, shm_.x), she_.y) / shm_, 0.0).a; \
                sha_ += shk_ * shv_;                                                                        \
                shs_ += shk_;                                                                               \
            }                                                                                               \
            const bool shn_ = plEdge.x < 1.5;                                                               \
            const float shq_ = shn_ ? sha_ / shs_ : lerp(sha_ / shs_, plEdge.y, smoothstep(0.0, 92.0, shd_)); \
            OUT = 1.0 - (1.0 - shq_) * (1.0 - (shn_ ? smoothstep(96.0, 176.0, shd_) : plEdge.z * smoothstep(120.0, 1100.0, shd_))); \
        }                                                                                                   \
    }
// Beyond the map: the share of the distance haze the land fades into with its distance from the map (MSL twin edgeHazeK).
float edgeHazeK(float2 p, float2 mapSize) {
    if (plEdge.x < 0.5) return 0.0;
    return plEdge.w * smoothstep(96.0, 1400.0, length(max(max(-p, p - mapSize), float2(0.0, 0.0))));
}
// Screen-space ambient occlusion (render3d/ssao.*; MSL twin ssaoAt in lights_msl.hpp): the share of the sky light that
// reaches the pixel at XY (1 when off). A macro: glslang does not take resources as function arguments.
#define SSAO_AT(T, S, XY, OUT) {                                                                  \
        OUT = 1.0;                                                                                \
        if (plSsao.z > 0.0) OUT = lerp(1.0, T.SampleLevel(S, (XY) * plSsao.xy, 0.0).r, plSsao.z); \
    }
// Reimagined "Dust haze" (docs/3d-mode.md "Dust haze"): drifting wind-blown dust veils; the light multiplier at
// world point p (1 = clear). MSL twin in lights_msl.hpp.
float hzHash(float2 p) {
    float3 q = frac(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float hzNoise(float2 p) {
    const float2 i = floor(p), f = frac(p), w = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hzHash(i), hzHash(i + float2(1.0, 0.0)), w.x), lerp(hzHash(i + float2(0.0, 1.0)), hzHash(i + float2(1.0, 1.0)), w.x), w.y);
}
// Reimagined "Realistic grade": the share of fine sand dust settled on a model (MSL twin sandDustK in lights_msl.hpp).
float sandDustK(float2 q, float ny, float h, float4 dust) {
    if (plGrade.y <= 0.0 || dust.y <= 0.0) return 0.0;
    const float nz = hzNoise(q) * 0.6 + hzNoise(q * 3.7 + 7.0) * 0.4;
    const float low = dust.x > 0.0 ? 1.0 - smoothstep(0.0, 1.0, (h + (nz - 0.5) * 2.0) * dust.x) : 0.0;
    const float up = smoothstep(0.5, 0.95, ny) * (0.14 + 0.3 * smoothstep(0.35, 0.75, nz));
    return saturate((low * 0.62 + up) * dust.y * plGrade.y);
}
// Vehicle wrecks (docs/3d-mode.md "Vehicle wrecks"): a destroyed vehicle's hulk - the burnt albedo (rgb) and the
// ember glow (a; < 0 = crumbled away: discard). MSL twin wreckChar in lights_msl.hpp.
float4 wreckChar(float3 albedo, float2 wk, float2 q, float cav) {
    const float n1 = hzNoise(q), n2 = hzNoise(q * 3.1 + 17.0), n3 = hzNoise(q * 0.37 + 5.0);
    const float gone = n2 * 0.65 + n1 * 0.35 - (-wk.y * 1.08 - 0.04);
    if (wk.y < 0.0 && gone < 0.0) return float4(0.0, 0.0, 0.0, -1.0);
    const float3 burnt = float3(0.060, 0.044, 0.034) * (0.45 + 1.1 * n1) + float3(0.055, 0.024, 0.009) * smoothstep(0.55, 0.9, n2);
    const float3 c = lerp(albedo, burnt, saturate(wk.x * (0.84 + 0.14 * n3)));
    const float vein = 1.0 - smoothstep(0.0, 0.05, abs(n2 - 0.5));
    float e = max(wk.y, 0.0) * (vein * smoothstep(0.5, 0.78, n3) + 0.5 * cav * smoothstep(0.35, 0.7, n1));
    if (wk.y < 0.0) e = 1.0 - smoothstep(0.0, 0.09, gone);  // the crumbling edge glows
    return float4(c, e);
}
// Reimagined "Aerial haze": the share of the fog term kept (MSL twin aerialK in lights_msl.hpp).
float aerialK() {
    return 1.0 - plInfo.w;
}
float3 dustHaze(float3 p) {
    if (plHaze.w <= 0.0) return float3(1.0, 1.0, 1.0);
    const float2 wd = plHaze.xy;
    const float a = dot(p.xz, wd) - plHaze.z;
    float b = dot(p.xz, float2(-wd.y, wd.x));
    b += (hzNoise(float2(a / 800.0, b / 560.0) + 11.0) - 0.5) * 200.0;
    const float v = hzNoise(float2(a / 950.0, b / 250.0)) * 0.6 + hzNoise(float2(a / 420.0, b / 120.0) + 5.3) * 0.28 +
                    hzNoise(float2(a / 170.0, b / 64.0) + 9.1) * 0.12;
    const float k = smoothstep(0.47, 0.7, v) * plHaze.w;
    return lerp(float3(1.0, 1.0, 1.0), float3(0.92, 0.86, 0.75), k);
}
// The frame's sun + sky light: ambient and diffuse terms (scalars of the old light) tinted.
float3 sunAmb(float amb, float dif) { return amb * plAmbColor.rgb + dif * plSunColor.rgb; }
// The time of day's saturation (night: towards grey, then the blue moonlight tints it); 1 = as is.
float3 satur(float3 c) {
    if (abs(plInfo.z - 1.0) < 0.001) return c;
    const float l = dot(c, float3(0.299, 0.587, 0.114));
    return max(lerp(float3(l, l, l), c, plInfo.z), 0.0);
}
// Sum of the lights at world point p with normal n; wrap 0 = Lambert, 1 = no normal dependence
// (billboards). Only the lights of p's grid cell are visited. Spot lights fade out over the outer
// 45 % of their cone.
#define D2K_POINT_LIGHTS_FN(NAME, G)                                                            \
float3 NAME(float3 p, float3 n, float wrap) {                                                   \
    float3 sum = float3(0.0, 0.0, 0.0);                                                         \
    if (plInfo.x < 0.5) return sum;                                                             \
    const float2 g = (p.xz - plGrid.xy) * plGrid.z;                                             \
    if (g.x < 0.0 || g.y < 0.0 || g.x >= plGrid.w || g.y >= plGrid2.x) return sum;              \
    const uint c = uint(g.y) * uint(plGrid.w) + uint(g.x);                                      \
    const uint e = G[uint(plGrid2.y) + (c >> 2)][c & 3u];                                       \
    const uint cnt = e & 0xFFu;                                                                 \
    const uint first = uint(plGrid2.z) * 4u + (e >> 8);                                         \
    [loop] for (uint k = 0; k < cnt; ++k) {                                                     \
        const uint w = first + k;                                                               \
        const uint li = G[w >> 2][w & 3u] * 3u;                                                 \
        const float4 pr = asfloat(G[li]);                                                       \
        const float3 d = pr.xyz - p;                                                            \
        const float r2 = pr.w * pr.w;                                                           \
        const float d2 = dot(d, d);                                                             \
        if (d2 >= r2) continue;                                                                 \
        float att = 1.0 - d2 / r2;                                                              \
        att *= att;                                                                             \
        const float3 dir = d * rsqrt(max(d2, 1e-4));                                            \
        const float4 sp = asfloat(G[li + 2u]);                                                  \
        if (sp.w > -1.5) {                                                                      \
            const float cs = dot(-dir, sp.xyz);                                                 \
            att *= smoothstep(sp.w, sp.w + (1.0 - sp.w) * 0.45, cs);                            \
        }                                                                                       \
        const float ndl = saturate(dot(n, dir) * (1.0 - wrap) + wrap);                          \
        sum += asfloat(G[li + 1u]).rgb * (att * ndl);                                           \
    }                                                                                           \
    return sum;                                                                                 \
}
// The grid at t<n>, space2 for the lit shaders' sampler counts: 3 (sprite_fs, clutter_fs), 4 (terrain_fs, model_fs,
// rock_fs), 6 (model_pbr_fs), 7 (terrain2_fs), 13 (terrain3_fs). (9: unused since the photo terrain.)
StructuredBuffer<uint4> PlGrid3 : register(t3, space2);
StructuredBuffer<uint4> PlGrid4 : register(t4, space2);
StructuredBuffer<uint4> PlGrid7 : register(t7, space2);
StructuredBuffer<uint4> PlGrid9 : register(t9, space2);
StructuredBuffer<uint4> PlGrid6 : register(t6, space2);
StructuredBuffer<uint4> PlGrid13 : register(t13, space2);
D2K_POINT_LIGHTS_FN(pointLights3, PlGrid3)
D2K_POINT_LIGHTS_FN(pointLights4, PlGrid4)
D2K_POINT_LIGHTS_FN(pointLights7, PlGrid7)
D2K_POINT_LIGHTS_FN(pointLights9, PlGrid9)
D2K_POINT_LIGHTS_FN(pointLights6, PlGrid6)
D2K_POINT_LIGHTS_FN(pointLights13, PlGrid13)

// ================================================================================== terrain
struct TVIn {
    float3 pos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float sun : TEXCOORD2;
};
struct TVOut {
    float4 pos : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float sun : TEXCOORD2;
};
cbuffer TerrainVU : register(b0, space1) {
    float4x4 tViewProj;
};

TVOut terrain_vs(TVIn i) {
    TVOut o;
    o.pos = mul(tViewProj, float4(i.pos, 1.0));
    o.wpos = i.pos;
    o.nrm = i.nrm;
    o.sun = i.sun;
    return o;
}

Texture2DArray<float4> Tiles : register(t0, space2);
Texture2D<float> TileMap : register(t1, space2);  // tile indices, exact integers in R32_FLOAT
Texture2D<float4> TerrainShroud : register(t2, space2);
SamplerState TileSamp : register(s0, space2);
SamplerState MapSamp : register(s1, space2);
SamplerState TerrainShroudSamp : register(s2, space2);
Texture2D<float> TerrainShadowMap : register(t3, space2);
SamplerComparisonState TerrainShadowSamp : register(s3, space2);
cbuffer TerrainFU : register(b0, space3) {
    float4 tSunDir;    // xyz towards the sun
    float4 tLight;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 tFogColor;  // rgb, a = unused
    float4 tFog;       // eye xyz, fog start
    float4 tFog2;      // fog end, map width px, map height px, shroud on
    float4x4 tLightVP; // sun shadow map
    float4 tShadow;    // on, texel (uv), -, depth bias
};

float4 terrain_fs(TVOut i) : SV_Target0 {
    const float2 g = i.wpos.xz / 32.0;
    uint mw, mh;
    TileMap.GetDimensions(mw, mh);
    const uint2 cell = uint2(clamp(floor(g), float2(0.0, 0.0), float2(mw, mh) - 1.0));
    const uint tile = uint(TileMap.Load(int3(int2(cell), 0)) + 0.5);
    const float2 uv = saturate(g - float2(cell));
    // Gradients from the continuous coordinate: no mip seams at the tile borders.
    const float4 c = Tiles.SampleGrad(TileSamp, float3(uv, float(tile)), ddx(g), ddy(g));
    const float3 n = normalize(i.nrm);
    const float ndl = max(dot(n, tSunDir.xyz), 0.0);
    float sm = i.sun;  // the shadow map, else the baked soft heightfield shadow
    if (tShadow.x > 0.5) SHADOW_PCF(TerrainShadowMap, TerrainShadowSamp, mul(tLightVP, float4(i.wpos, 1.0)).xyz, tShadow, sm);
    const float vis = lerp(tLight.w, 1.0, sm);
    const float3 shade = sunAmb(tLight.x, tLight.y * ndl * vis) * tLight.z;
    float3 rgb = satur(c.rgb) * shade + c.rgb * pointLights4(i.wpos, n, 0.3);
    rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils (0 strength = off)
    // Darkness: the shroud mask, and a thin fade into the black beyond the map edge. The haze
    // is darkened too, so shrouded ground stays black at any distance.
    float s = 0.0;
    if (tFog2.w > 0.5) s = TerrainShroud.Sample(TerrainShroudSamp, i.wpos.xz / tFog2.yz).a;
    const float2 e = min(i.wpos.xz, tFog2.yz - i.wpos.xz);
    s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(i.wpos, tFog.xyz);
    const float f = smoothstep(tFog.w, tFog2.x, d) * aerialK();
    rgb = lerp(rgb, tFogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}

// ================================================================================== sprites
struct SVIn {
    float3 pos : TEXCOORD0;     // corner, world
    float3 anchor : TEXCOORD1;  // depth anchor, world (billboards: the whole quad gets its depth)
    float2 uv : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 params : TEXCOORD4;  // x: 1 = billboard depth, y: flash (added grey), zw: ground xz (shroud)
    float3 spos : TEXCOORD5;    // world point whose sun shadow applies (billboards: their ground point)
};
struct SVOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float flash : TEXCOORD2;
    float2 ground : TEXCOORD3;
    float fogF : TEXCOORD4;
    float3 lpos : TEXCOORD5;
    float3 wpos : TEXCOORD6;
};
cbuffer SpriteVU : register(b0, space1) {
    float4x4 sViewProj;
    float4 sEye;  // xyz, w = fog start
    float4 sFog;  // x = fog end
    float4x4 sLightVP;
};

SVOut sprite_vs(SVIn i) {
    SVOut o;
    float4 p = mul(sViewProj, float4(i.pos, 1.0));
    if (i.params.x > 0.5) {
        const float4 a = mul(sViewProj, float4(i.anchor, 1.0));
        if (a.w > 1e-4) p.z = saturate(a.z / a.w) * p.w;
    }
    o.pos = p;
    o.uv = i.uv;
    o.color = i.color;
    o.flash = i.params.y;
    o.ground = i.params.zw;
    o.fogF = smoothstep(sEye.w, sFog.x, distance(i.anchor, sEye.xyz));
    o.lpos = mul(sLightVP, float4(i.spos, 1.0)).xyz;
    o.wpos = i.pos;
    return o;
}

Texture2D<float4> SpriteTex : register(t0, space2);
Texture2D<float4> SpriteShroud : register(t1, space2);
SamplerState SpriteSamp : register(s0, space2);
SamplerState SpriteShroudSamp : register(s1, space2);
Texture2D<float> SpriteShadowMap : register(t2, space2);
SamplerComparisonState SpriteShadowSamp : register(s2, space2);
cbuffer SpriteFU : register(b0, space3) {
    float4 sMode;      // x: 0 alpha, 1 additive / subtractive, 2 multiply; y: shroud on; zw: map size px
    float4 sFogColor;
    float4 sShadow;    // on, texel (uv), brightness in full shadow, depth bias
};

float4 sprite_fs(SVOut i) : SV_Target0 {
    float4 c = SpriteTex.Sample(SpriteSamp, i.uv);
    const float fogA = i.fogF * aerialK();  // the fog at the anchor (Aerial haze: capped)
    if (sMode.x > 2.5) {
        // Relief decal (track marks; the MSL twin explains it): normal (rg) lit by the sun in the
        // vertex colour, relative to flat ground, times the tone (b); coverage in a; blend dst * 2 * src.
        const float2 nxy = c.rg * 2.0 - 1.0;
        const float3 n = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
        const float3 L = normalize(i.color.rgb * 2.0 - 1.0);
        float sunVis = 1.0;
        if (sShadow.x > 0.5) SHADOW_PCF(SpriteShadowMap, SpriteShadowSamp, i.lpos, sShadow, sunVis);
        const float amb = 0.62, dif = 0.55 * sunVis;
        const float lit = (amb * (0.55 + 0.45 * n.z) + dif * max(dot(n, L), 0.0)) / (amb + dif * max(L.z, 0.0));
        float dark = 0.0;
        if (sMode.y > 0.5) SHROUD_AT(SpriteShroud, SpriteShroudSamp, i.ground, sMode.zw, dark);
        const float k = c.a * i.color.a * (1.0 - dark) * (1.0 - fogA);
        const float f = lerp(1.0, clamp(lit * c.b * 2.0, 0.0, 2.0), k);
        return float4(0.5 * f, 0.5 * f, 0.5 * f, 1.0);
    }
    c *= i.color;
    c.rgb = min(c.rgb + i.flash * c.a, 1.0);
    // Darkness of the map point this pixel covers in the 2D view (decals: the ground under it);
    // nothing outside the map is ever visible.
    float dark = 0.0;
    if (sMode.y > 0.5) SHROUD_AT(SpriteShroud, SpriteShroudSamp, i.ground, sMode.zw, dark);
    if (any(i.ground < float2(0.0, 0.0)) || any(i.ground >= sMode.zw)) dark = 1.0;
    if (sMode.x < 0.5) {
        if (c.a < 0.004 || dark > 0.996) discard;
        const float3 alb = c.rgb;
        if (sShadow.x > 0.5) {  // sprites receive the sun shadow at their ground point
            float sm = 1.0;
            SHADOW_PCF(SpriteShadowMap, SpriteShadowSamp, i.lpos, sShadow, sm);
            c.rgb *= lerp(sShadow.z, 1.0, sm);
        }
        c.rgb = satur(c.rgb) * plSpriteTint.rgb;  // the light of the time of day
        // Dynamic lights (explosions, muzzle flashes, fires, lamps) on units, buildings and smoke.
        c.rgb += alb * pointLights3(i.wpos, float3(0.0, 1.0, 0.0), 0.8);
        c.rgb *= dustHaze(float3(i.ground.x, 0.0, i.ground.y));  // Reimagined: drifting dust veils
        c.rgb = lerp(c.rgb, sFogColor.rgb, fogA) * (1.0 - dark);
    } else if (sMode.x < 1.5) {
        c.rgb *= (1.0 - dark) * (1.0 - fogA);
    } else {
        c.rgb = lerp(c.rgb, float3(1.0, 1.0, 1.0), max(dark, fogA));
    }
    return c;
}

// =================================================================================== models
// model_vs/model_fs: lit, textured meshes (house colour, shroud, fog, flash, alpha);
// shadow_fs: the flat shadow polygons as a darkening of what is below; model_shadow_vs: a glTF
// mesh flattened onto its ground plane along the sun (projected shadow, drawn with shadow_fs).
struct MVIn {
    float3 pos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 params : TEXCOORD4;  // x house (0..1 tint, 2 solid), y lit, z textured, w one-sided
    float2 pbr : TEXCOORD5;     // x gloss (1 - roughness; > 0 marks glTF shading), y metallic
    float4 joints : TEXCOORD6;  // skinning: palette entries / 255 (UBYTE4_NORM)
    float4 weights : TEXCOORD7; // their weights (sum 1; all 0 = rigid)
    float4 apart : TEXCOORD8;   // Assemble part (render3d/model_assembly.hpp): foot xyz, param
    float4 atime : TEXCOORD9;   // t0, dur, motion * 32 / 255, (1 + direction 0..254) / 255 (0 = no part)
};
struct MVOut {
    float4 pos : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 params : TEXCOORD4;
    float4 pbr : TEXCOORD5;
};
// model_vs and model_shadow_vs are separate shader objects with the same block at b0.
cbuffer ModelVU : register(b0, space1) {
    float4x4 mViewProj;
    float4x4 mModel;
    float4 mSun;    // model_shadow_vs only: xyz towards the sun
    float4 mPlane;  // model_shadow_vs only: ground plane, xyz normal, w = dot(normal, point on it)
    float4 mSkin;   // x = first palette entry of this draw (matrices), < 0 = not skinned;
                    // y = drop-in unfold 1 + t (0 = off; render/building_dropin.hpp), z = the model's height (px),
                    // w = Assemble progress 1 + p (0 = off; render/building_assemble.hpp)
    float4 mDrive;  // running gear (render3d/model_drive.hpp): x = 1 on, y = odometer (px), z = spin (radians, + clockwise)
};
// GPU skinning (docs/3d-models.md "Animation"): vertex storage buffer 0 (SPIR-V set 0, DXIL
// t0 space0) = the joint palette, column-major matrices as 4 float4 columns each; a vertex = the
// weighted sum of its (up to) four joints' matrices applied to it (v.w = 1 points, 0 directions).
StructuredBuffer<float4> ModelPalette : register(t0, space0);
float4 Skinned(float4 j, float4 w, float4 v) {
    if (mSkin.x < 0.0 || w.x + w.y + w.z + w.w < 0.001) return v;
    const int base = int(mSkin.x + 0.5);
    float4 r = float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 4; ++k) {
        const int i = (base + int(j[k] * 255.0 + 0.5)) * 4;
        if (w[k] > 0.0)
            r += w[k] * (ModelPalette[i] * v.x + ModelPalette[i + 1] * v.y + ModelPalette[i + 2] * v.z + ModelPalette[i + 3] * v.w);
    }
    return r;
}

// Drop-in unfold (render/building_dropin.hpp; the MSL twin: unfoldY in render3d/models_msl.hpp): four
// height bands, folded to 28 % each, extending in turn from the lowest (ease-out-back). k = the band's scale.
float UnfoldY(float y, out float k) {
    k = 1.0;
    if (mSkin.y < 0.5 || y <= 0.0) return y;
    const float t = mSkin.y - 1.0;
    const float bh = max(mSkin.z, 1.0) * 0.25;
    float acc = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        const float e = clamp((t - float(i) * 0.16) / 0.52, 0.0, 1.0) - 1.0;
        const float eb = 1.0 + 2.3 * e * e * e + 1.3 * e * e;
        const float ki = lerp(0.28, 1.0, eb);
        const float lo = float(i) * bh;
        if (y < lo + bh || i == 3) {
            k = ki;
            return acc + (y - lo) * ki;
        }
        acc += bh * ki;
    }
    return y;
}

// Assemble build-up (render/building_assemble.hpp; the MSL twin: assemble in render3d/models_msl.hpp): the
// parts arrive one after another; before its window a part is collapsed onto its foot, after it it stands.
// edge = 1 + px below a glowing cut (0 = none), seam = the weld glow at the foot of a part that just seated.
struct Assembled {
    float3 p;
    float3 n;
    float edge;
    float seam;
};
float EaseBack(float t, float s) {
    const float u = t - 1.0;
    return 1.0 + (s + 1.0) * u * u * u + s * u * u;
}
Assembled Assemble(float3 p, float3 n, float4 pa, float4 pt, float prog) {
    Assembled o;
    o.p = p;
    o.n = n;
    o.edge = 0.0;
    o.seam = 0.0;
    if (pt.w <= 0.0) return o;
    const float s = clamp((prog - pt.x) / max(pt.y, 0.004), 0.0, 1.0);
    const float3 foot = pa.xyz;
    if (s >= 1.0) {
        const float after = (prog - pt.x - pt.y) / 0.05;
        if (after < 1.0) o.seam = 0.85 * (1.0 - max(after, 0.0)) * saturate(1.0 - (p.y - foot.y) / 3.0);
        return o;
    }
    if (s <= 0.0) {
        o.p = foot;
        return o;
    }
    const int motion = int(pt.z * 255.0 / 32.0 + 0.5);
    const float a = (pt.w * 255.0 - 1.0) / 254.0 * 6.2831853;
    const float2 dir = float2(cos(a), sin(a));
    if (motion == 0) {
        const float e = 1.0 - (1.0 - s) * (1.0 - s);
        const float cut = foot.y + pa.w * e;
        o.p.y = min(p.y, cut);
        o.edge = 1.0 + (cut - o.p.y);
    } else if (motion == 1) {
        const float g = smoothstep(0.0, 0.35, s);
        o.p = foot + (p - foot) * lerp(0.6, 1.0, g);
        o.p.y += pa.w * pow(1.0 - s, 2.2) + pa.w * 0.07 * sin(clamp((s - 0.7) / 0.3, 0.0, 1.0) * 3.14159265);
    } else if (motion == 2) {
        const float e = EaseBack(s, 1.2);
        o.p.xz -= dir * (pa.w * (1.0 - e));
        o.p = foot + (o.p - foot) * lerp(0.7, 1.0, min(s * 2.5, 1.0));
    } else if (motion == 3) {
        const float ang = (1.0 - EaseBack(s, 0.9)) * 1.5707963;
        const float c = cos(ang), sn = sin(ang);
        const float3 d = p - foot;
        const float r = dot(d.xz, dir);
        const float r2 = r * c + d.y * sn, y2 = -r * sn + d.y * c;
        o.p = float3(foot.x + d.x + dir.x * (r2 - r), foot.y + y2, foot.z + d.z + dir.y * (r2 - r));
        const float nr = dot(n.xz, dir);
        const float nr2 = nr * c + n.y * sn, ny2 = -nr * sn + n.y * c;
        o.n = float3(n.x + dir.x * (nr2 - nr), ny2, n.z + dir.y * (nr2 - nr));
    } else {
        const float e = EaseBack(s, 0.8);
        o.p.y = max(p.y - pa.w * (1.0 - e), foot.y);
        o.edge = 1.0 + (o.p.y - foot.y);
    }
    return o;
}

// Running gear (render3d/model_drive.hpp; the MSL twin: driveGear in render3d/models_msl.hpp).
void DriveGear(inout float3 p, inout float3 n, float4 a, float4 t) {
    if (mDrive.x < 0.5 || t.w > 0.0) return;
    const int kind = int(t.x * 255.0 + 0.5);
    if (kind < 1 || kind > 3) return;
    const float x = (t.y * 255.0 * 256.0 + t.z * 255.0) / 256.0 - 128.0;
    const float dist = mDrive.y - mDrive.z * x;
    if (kind == 3) {
        p.yz += frac(dist / a.w) * a.xy;
        return;
    }
    const float ang = kind == 1 ? -dist / max(a.z, 0.01) : frac(dist / a.w) * a.z;
    const float c = cos(ang), s = sin(ang);
    const float2 d = p.yz - a.xy;
    p.yz = a.xy + float2(c * d.x - s * d.y, s * d.x + c * d.y);
    n.yz = float2(c * n.y - s * n.z, s * n.y + c * n.z);
}

MVOut model_vs(MVIn i) {
    MVOut o;
    o.pbr = float4(i.pbr, 0.0, 0.0);
    if (mSkin.w > 0.5) {  // Assemble (pbr.z = cut edge, pbr.w = seam glow for the fragment shaders)
        const float4 m = Skinned(i.joints, i.weights, float4(i.pos, 1.0));
        const float4 mn = Skinned(i.joints, i.weights, float4(i.nrm, 0.0));
        const Assembled as = Assemble(m.xyz, mn.xyz, i.apart, i.atime, mSkin.w - 1.0);
        const float4 w = mul(mModel, float4(as.p, 1.0));
        o.pos = mul(mViewProj, w);
        o.wpos = w.xyz;
        o.nrm = mul(mModel, float4(as.n, 0.0)).xyz;
        o.pbr.zw = float2(as.edge, as.seam);
    } else {
        float3 p = i.pos, gn = i.nrm;
        DriveGear(p, gn, i.apart, i.atime);
        float4 m = Skinned(i.joints, i.weights, float4(p, 1.0));
        float k;
        m.y = UnfoldY(m.y, k);
        const float4 w = mul(mModel, m);
        o.pos = mul(mViewProj, w);
        o.wpos = w.xyz;
        float4 mn = Skinned(i.joints, i.weights, float4(gn, 0.0));
        mn.y /= k;
        o.nrm = mul(mModel, mn).xyz;
    }
    o.uv = i.uv;
    o.color = i.color;
    o.params = i.params;
    return o;
}

MVOut model_shadow_vs(MVIn i) {
    MVOut o;
    float3 p = i.pos, gn = i.nrm;
    DriveGear(p, gn, i.apart, i.atime);
    float4 m = Skinned(i.joints, i.weights, float4(p, 1.0));
    float k;
    m.y = UnfoldY(m.y, k);
    if (mSkin.w > 0.5) m.xyz = Assemble(m.xyz, float3(0.0, 1.0, 0.0), i.apart, i.atime, mSkin.w - 1.0).p;
    float3 w = mul(mModel, m).xyz;
    const float3 n = mPlane.xyz;
    const float above = max(dot(n, w) - mPlane.w, 0.0);
    w -= mSun.xyz * (above / max(dot(n, mSun.xyz), 0.2));
    w = w - n * (dot(n, w) - mPlane.w) + n * 0.35;  // onto the plane, just above it
    o.pos = mul(mViewProj, float4(w, 1.0));
    o.wpos = w;
    o.nrm = n;
    o.uv = i.uv;
    o.color = i.color;
    o.params = i.params;
    o.pbr = float4(i.pbr, 0.0, 0.0);
    return o;
}

Texture2D<float4> ModelTex : register(t0, space2);
Texture2D<float4> ModelShroud : register(t1, space2);
SamplerState ModelSamp : register(s0, space2);
SamplerState ModelShroudSamp : register(s1, space2);
Texture2D<float> ModelShadowMap : register(t2, space2);
SamplerComparisonState ModelShadowSamp : register(s2, space2);
Texture2D<float4> ModelSsao : register(t3, space2);  // model_fs: the screen-space ambient occlusion (render3d/ssao.*)
SamplerState ModelSsaoSamp : register(s3, space2);
cbuffer ModelFU : register(b0, space3) {
    float4 mHouse;     // rgb house colour, a = flash
    float4 mSunDir;    // xyz towards the sun, w = alpha
    float4 mLight;     // ambient, diffuse, 1 / flat-ground light, shroud on
    float4 mFogColor;  // rgb, a = fog start
    float4 mFog;       // eye xyz, fog end
    float4 mMap;       // map width, height (px), shadow alpha, emission strength (0: alpha is no emission)
    float4x4 mLightVP; // sun shadow map
    float4 mShadow;    // on, texel (uv), diffuse kept in full shadow, depth bias
    float4 mWalk;      // walkable cells: x0, z0 of the footprint's top-left cell (world px), cell mask (bit y*4+x), min height
    float4 mWalkMode;  // x: 0 everything, 1 all but the walkable cells' parts, 2 only them (docs/3d-models.md "Walkable cells");
                       // build-up (render/buildup_fx.hpp): y = cut height (world), z = glow band (px, 0 = none), w = scan drift
    float4 mGPlane;    // the model's ground plane: xyz normal, w = dot(normal, a point on it) (Realistic grade: sand dust)
    float4 mDust;      // sand dust (docs/3d-mode.md "Realistic grade"): x = 1 / the foot band's height (px; 0 = none), y = share;
                       // zw: vehicle wrecks (wreckChar; 0, 0 = a live model)
};
// A building's part above one of its walkable cells (higher than its floor)?
bool WalkPart(float3 wpos) {
    const float2 c = floor((wpos.xz - mWalk.xy) / 32.0);
    if (any(c < float2(0.0, 0.0)) || any(c > float2(3.0, 3.0)) || wpos.y <= mWalk.w) return false;
    return ((uint(mWalk.z) >> (uint(c.y) * 4u + uint(c.x))) & 1u) != 0u;
}

float4 model_fs(MVOut i, bool front : SV_IsFrontFace) : SV_Target0 {
    if (i.params.w > 0.5 && !front) discard;  // PS1 back curtains: only from behind
    if (mWalkMode.x > 0.5 && WalkPart(i.wpos) != (mWalkMode.x > 1.5)) discard;
    float4 c = i.color;
    float emit = 0.0;  // glTF emission folded into the texture alpha (docs/3d-models.md "Emission")
    if (i.params.z > 0.5) {
        const float4 t = ModelTex.Sample(ModelSamp, i.uv);
        if (t.a < 0.5) discard;
        c.rgb *= t.rgb;
        if (mMap.w > 0.0) emit = saturate((1.0 - t.a) * 2.0) * mMap.w;
    }
    // Build-up / selling: the part above the cut is a hologram of cyan scan lines, a warm band glows below it.
    float band = 0.0;
    if (mWalkMode.z > 0.0) {
        const float d = mWalkMode.y - i.wpos.y;
        if (d < 0.0) {
            if (frac((i.wpos.y - mWalkMode.w) / 3.0) > 0.25) discard;
            return float4(0.12, 0.6, 1.0, 0.4 * mSunDir.w);
        }
        band = saturate(1.0 - d / mWalkMode.z);
    }
    if (i.pbr.z > 0.5) band = max(band, 0.55 * saturate(1.0 - (i.pbr.z - 1.0) / 2.5));  // Assemble: the cut edge ...
    band = max(band, i.pbr.w);                                                  // ... and the weld seams
    const float3 glow = c.rgb / max(max(c.r, max(c.g, c.b)), 0.05);  // the albedo's hue at full brightness
    // Realistic grade (docs/3d-mode.md): the albedo darker; the house colour keeps its strength (an accent).
    const float gk = plGrade.x > 0.0 ? plGrade.x : 1.0;
    c.rgb *= gk;
    if (i.params.x > 1.5) {
        c.rgb = mHouse.rgb;
    } else if (i.params.x > 0.0) {
        const float l = dot(c.rgb, float3(0.299, 0.587, 0.114)) / gk;
        c.rgb = lerp(c.rgb, saturate(mHouse.rgb * l * 1.8), i.params.x);
    }
    float ember = 0.0;  // vehicle wrecks: a hulk is burnt black-brown, embers glow in it
    if (mDust.z > 0.0) {
        const float4 wk = wreckChar(c.rgb, mDust.zw, i.params.z > 0.5 ? i.uv * 55.0 : i.wpos.xz * 0.9 + i.wpos.y * 0.7, 0.0);
        if (wk.a < 0.0) discard;
        c.rgb = wk.rgb;
        ember = wk.a;
    }
    if (i.params.y > 0.5) {
        float3 n = normalize(i.nrm);
        if (!front) n = -n;  // two-sided: the inside of a face is lit as its outside
        // ... and fine sand dust settled on the up-facing surfaces and over the model's foot.
        const float dk = sandDustK(i.uv * 61.0, n.y, dot(mGPlane.xyz, i.wpos) - mGPlane.w, mDust) *
                         (1.0 - 0.5 * saturate(i.params.x));
        if (dk > 0.0) c.rgb = lerp(c.rgb, plDustColor.rgb, dk);
        float ndl = max(dot(n, mSunDir.xyz), 0.0);
        if (mShadow.x > 0.5) {
            float sm = 1.0;
            SHADOW_PCF(ModelShadowMap, ModelShadowSamp, mul(mLightVP, float4(i.wpos, 1.0)).xyz, mShadow, sm);
            ndl *= lerp(mShadow.z, 1.0, sm);
        }
        const float3 base = c.rgb;
        c.rgb = satur(c.rgb);  // the time of day's saturation (night: towards grey) before the light
        float sao;  // ambient occlusion: the sky light only
        SSAO_AT(ModelSsao, ModelSsaoSamp, i.pos.xy, sao);
        if (i.pbr.x > 0.0) {
            // glTF models: sky ambient (brighter facing up) + a soft fill light from the side
            // opposite the sun, so faces turned away from the sun keep their shape.
            const float3 fillDir = normalize(float3(-mSunDir.x, 0.25, -mSunDir.z));
            const float amb = mLight.x * (0.72 + 0.18 * n.y) + mLight.x * 0.4 * max(dot(n, fillDir), 0.0);
            c.rgb *= sunAmb(amb * sao, mLight.y * ndl) * mLight.z * (1.0 - 0.2 * i.pbr.y);
        } else {
            c.rgb *= sunAmb(mLight.x * sao, mLight.y * ndl) * mLight.z;
        }
        if (i.pbr.x > 0.0) {  // glTF materials: a sun highlight (Blinn-Phong), tinted on metals
            const float3 h = normalize(mSunDir.xyz + normalize(mFog.xyz - i.wpos));
            const float sp = pow(max(dot(n, h), 0.0), 6.0 + 90.0 * i.pbr.x * i.pbr.x) * i.pbr.x * step(0.0, ndl);
            c.rgb += sp * (0.25 + 0.45 * i.pbr.y) * lerp(float3(1.0, 1.0, 1.0), base, i.pbr.y) * plInfo.y * plSunColor.rgb;
        }
        c.rgb += base * pointLights4(i.wpos, n, 0.25);  // dynamic lights
    } else if (i.params.y > 0.2) {  // flat-shaded parts: the time of day's light, like the billboards
        c.rgb = satur(c.rgb) * plSpriteTint.rgb + c.rgb * pointLights4(i.wpos, float3(0.0, 1.0, 0.0), 0.8);
    }
    c.rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils
    c.rgb = min(c.rgb + mHouse.a, 1.0);
    c.rgb += glow * emit;  // may exceed 1 (HDR: blooms)
    if (ember > 0.0) c.rgb += float3(2.6, 0.7, 0.12) * ember;
    c.rgb += float3(1.0, 0.55, 0.2) * (band * band * 1.6) + float3(1.0, 0.95, 0.85) * pow(band, 12.0);
    if (mLight.w > 0.5) {
        float shroudK;
        SHROUD_AT(ModelShroud, ModelShroudSamp, i.wpos.xz, mMap.xy, shroudK);
        c.rgb *= 1.0 - shroudK;
    }
    const float f = smoothstep(mFogColor.a, mFog.w, distance(i.wpos, mFog.xyz)) * aerialK();
    c.rgb = lerp(c.rgb, mFogColor.rgb, f);
    return float4(c.rgb, mSunDir.w);
}

float4 shadow_fs(MVOut i) : SV_Target0 {
    return float4(0.0, 0.0, 0.0, mMap.z);
}

// ================================================================================== Reimagined models (PBR)
// model_pbr_fs (MSL twin: render3d/model_pbr_msl.hpp; docs/3d-models.md "Reimagined models"): model_fs for a
// model with PBR maps - t3 R occlusion / G roughness / B metallic / A house mask, t4 tangent-space normals;
// t5 the screen-space ambient occlusion; uniforms b0 ModelFU, b1 PointLightsFU, b2 PbrFU; the light grid at t6
// (6 samplers).
Texture2D<float4> ModelPbrMap : register(t3, space2);
SamplerState ModelPbrSamp : register(s3, space2);
Texture2D<float4> ModelNrmMap : register(t4, space2);
SamplerState ModelNrmSamp : register(s4, space2);
cbuffer PbrFU : register(b2, space3) {
    float4 pK;   // x normal map strength, y green sign (1 = glTF), z sun highlight, w sky reflection
    float4 pK2;  // x occlusion strength
};
Texture2D<float4> ModelPbrSsao : register(t5, space2);
SamplerState ModelPbrSsaoSamp : register(s5, space2);
float3 PbrNormal(float3 n, float3 wpos, float2 uv, float3 tn) {
    const float3 dp1 = ddx(wpos), dp2 = ddy(wpos);
    const float2 duv1 = ddx(uv), duv2 = ddy(uv);
    const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    const float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    const float m = max(dot(t, t), dot(b, b));
    if (m < 1e-20) return n;
    const float s = rsqrt(m);
    const float2 xy = tn.xy * pK.x;
    return normalize(t * (s * xy.x) - b * (s * xy.y * pK.y) + n * max(tn.z, 0.05));
}

float4 model_pbr_fs(MVOut i, bool front : SV_IsFrontFace) : SV_Target0 {
    if (i.params.w > 0.5 && !front) discard;
    if (mWalkMode.x > 0.5 && WalkPart(i.wpos) != (mWalkMode.x > 1.5)) discard;
    float4 c = i.color;
    float emit = 0.0;
    float4 orm = float4(1.0, 1.0 - i.pbr.x, i.pbr.y, 0.0);
    float3 tn = float3(0.0, 0.0, 1.0);
    if (i.params.z > 0.5) {
        const float4 t = ModelTex.Sample(ModelSamp, i.uv);
        if (t.a < 0.5) discard;
        c.rgb *= t.rgb;
        if (mMap.w > 0.0) emit = saturate((1.0 - t.a) * 2.0) * mMap.w;
        orm = ModelPbrMap.Sample(ModelPbrSamp, i.uv);
        // x, y only (BC5 maps have no blue): z rebuilt (docs/memory.md)
        const float2 nxy = ModelNrmMap.Sample(ModelNrmSamp, i.uv).xy * 2.0 - 1.0;
        tn = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
    }
    float band = 0.0;
    if (mWalkMode.z > 0.0) {
        const float d = mWalkMode.y - i.wpos.y;
        if (d < 0.0) {
            if (frac((i.wpos.y - mWalkMode.w) / 3.0) > 0.25) discard;
            return float4(0.12, 0.6, 1.0, 0.4 * mSunDir.w);
        }
        band = saturate(1.0 - d / mWalkMode.z);
    }
    if (i.pbr.z > 0.5) band = max(band, 0.55 * saturate(1.0 - (i.pbr.z - 1.0) / 2.5));  // Assemble: cut edge, weld seams
    band = max(band, i.pbr.w);
    const float3 glow = c.rgb / max(max(c.r, max(c.g, c.b)), 0.05);
    // Realistic grade (docs/3d-mode.md): the albedo darker; the house colour keeps its strength (an accent).
    const float gk = plGrade.x > 0.0 ? plGrade.x : 1.0;
    c.rgb *= gk;
    float hm = 1.0;
    if (i.params.x > 1.5) {
        c.rgb = mHouse.rgb;
    } else {
        hm = max(i.params.x, orm.a);
        const float l = dot(c.rgb, float3(0.299, 0.587, 0.114)) / gk;
        c.rgb = lerp(c.rgb, saturate(mHouse.rgb * l * 1.8), hm);
    }
    float ember = 0.0;  // vehicle wrecks: a hulk is burnt black-brown, embers glow in its seams (the baked occlusion)
    if (mDust.z > 0.0) {
        const float4 wk = wreckChar(c.rgb, mDust.zw, i.params.z > 0.5 ? i.uv * 55.0 : i.wpos.xz * 0.9 + i.wpos.y * 0.7,
                                    saturate((0.5 - orm.r) * 3.0));
        if (wk.a < 0.0) discard;
        c.rgb = wk.rgb;
        ember = wk.a;
    }
    if (i.params.y > 0.5) {
        float3 n0 = normalize(i.nrm);
        if (!front) n0 = -n0;
        const float3 n = PbrNormal(n0, i.wpos, i.uv, tn);
        const float3 L = mSunDir.xyz;
        const float ndl = max(dot(n, L), 0.0);
        float sh = 1.0;
        if (mShadow.x > 0.5) SHADOW_PCF(ModelShadowMap, ModelShadowSamp, mul(mLightVP, float4(i.wpos, 1.0)).xyz, mShadow, sh);
        const float lit = ndl * lerp(mShadow.z, 1.0, sh);
        const float ao = lerp(1.0, orm.r, saturate(pK2.x));
        float rough = clamp(orm.g, 0.06, 1.0), metal = saturate(orm.b);
        // Realistic grade: fine sand dust on the up-facing surfaces and over the model's foot (matt, not metal).
        const float dk = sandDustK(i.uv * 61.0, n0.y, dot(mGPlane.xyz, i.wpos) - mGPlane.w, mDust) * (1.0 - 0.5 * saturate(hm));
        if (dk > 0.0) {
            c.rgb = lerp(c.rgb, plDustColor.rgb, dk);
            rough = lerp(rough, 0.95, dk);
            metal *= 1.0 - dk;
        }
        if (mDust.z > 0.0) {  // a hulk: sooty, matt
            rough = lerp(rough, 0.92, mDust.z);
            metal *= 1.0 - 0.9 * mDust.z;
        }
        const float3 base = c.rgb;
        c.rgb = satur(c.rgb);
        const float3 fillDir = normalize(float3(-L.x, 0.25, -L.z));
        const float amb = mLight.x * (0.72 + 0.18 * n.y) + mLight.x * 0.4 * max(dot(n, fillDir), 0.0);
        float sao;  // (ambient occlusion: the screen-space term with the baked one, on the sky light only)
        SSAO_AT(ModelPbrSsao, ModelPbrSsaoSamp, i.pos.xy, sao);
        c.rgb *= sunAmb(amb * ao * sao, mLight.y * lit) * mLight.z * (1.0 - 0.75 * metal);
        const float3 V = normalize(mFog.xyz - i.wpos);
        const float3 H = normalize(L + V);
        const float ndv = max(dot(n, V), 1e-3), ndh = max(dot(n, H), 0.0), vdh = max(dot(V, H), 0.0);
        const float a = rough * rough, a2 = a * a;
        const float dd = ndh * ndh * (a2 - 1.0) + 1.0;
        const float D = a2 / (3.14159 * dd * dd);
        const float k = a * 0.5 + 0.0001;
        const float vis = 1.0 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k) * 4.0);
        const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metal);
        const float3 F = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
        c.rgb += (D * vis * 3.14159) * F * (lit * mLight.y * mLight.z * pK.z * plInfo.y) * plSunColor.rgb;
        const float3 R = reflect(-V, n);
        const float3 sky = plAmbColor.rgb * lerp(float3(0.62, 0.55, 0.46), float3(0.80, 0.86, 0.95), saturate(R.y * 0.6 + 0.5));
        const float3 fe = f0 + (max(float3(1.0 - rough, 1.0 - rough, 1.0 - rough), f0) - f0) * pow(1.0 - ndv, 5.0);
        c.rgb += sky * fe * (ao * mLight.x * mLight.z * pK.w * (1.0 - 0.6 * rough));
        c.rgb += base * (1.0 - 0.75 * metal) * pointLights6(i.wpos, n, 0.25);
    } else if (i.params.y > 0.2) {
        c.rgb = satur(c.rgb) * plSpriteTint.rgb + c.rgb * pointLights6(i.wpos, float3(0.0, 1.0, 0.0), 0.8);
    }
    c.rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils
    if (mHouse.a > 0.0) c.rgb = min(c.rgb + mHouse.a, 1.0);
    c.rgb += glow * emit;
    if (ember > 0.0) c.rgb += float3(2.6, 0.7, 0.12) * ember;
    c.rgb += float3(1.0, 0.55, 0.2) * (band * band * 1.6) + float3(1.0, 0.95, 0.85) * pow(band, 12.0);
    if (mLight.w > 0.5) {
        float shroudK;
        SHROUD_AT(ModelShroud, ModelShroudSamp, i.wpos.xz, mMap.xy, shroudK);
        c.rgb *= 1.0 - shroudK;
    }
    const float fo = smoothstep(mFogColor.a, mFog.w, distance(i.wpos, mFog.xyz)) * aerialK();
    c.rgb = lerp(c.rgb, mFogColor.rgb, fo);
    return float4(c.rgb, mSunDir.w);
}

// ================================================================================== designed terrain
// HLSL twin of render3d/terrain_v2_msl.hpp (docs/3d-mode.md "Terrain v2"): terrain2_fs (with
// terrain_vs) and rock_vs / rock_fs.
Texture2DArray<float4> T2Alb : register(t0, space2);
Texture2DArray<float4> T2Nor : register(t1, space2);
Texture2D<float4> T2SplatA : register(t2, space2);
Texture2D<float4> T2SplatB : register(t3, space2);
Texture2D<float4> T2Spice : register(t4, space2);
Texture2D<float4> T2Shroud : register(t5, space2);
SamplerState T2MatSamp : register(s0, space2);
SamplerState T2MatSamp2 : register(s1, space2);
SamplerState T2SplatSamp : register(s2, space2);
SamplerState T2SplatSamp2 : register(s3, space2);
SamplerState T2SpiceSamp : register(s4, space2);
SamplerState T2ShroudSamp : register(s5, space2);
Texture2D<float> T2ShadowMap : register(t6, space2);
SamplerComparisonState T2ShadowSamp : register(s6, space2);
cbuffer Terrain2FU : register(b0, space3) {
    float4 t2SunDir;    // xyz towards the sun
    float4 t2Light;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 t2FogColor;  // rgb, a = unused
    float4 t2Fog;       // eye xyz, fog start
    float4 t2Fog2;      // fog end, map width px, map height px, shroud on
    float4 t2Tint[8];   // per material: rgb colour lock (per channel), a = world px per texture repeat
    float4 t2Lum[8];    // per material: rgb colour lock by luminance (target / mean luminance), a = normal strength
    float4 t2Params;    // x unused, y height-blend depth, z macro variation, w luminance share of the lock
    float4x4 t2LightVP; // sun shadow map
    float4 t2Shadow;    // on, texel (uv), -, depth bias
};

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}
float vnoise(float2 p) {
    const float2 i = floor(p), f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash12(i), hash12(i + float2(1, 0)), u.x), lerp(hash12(i + float2(0, 1)), hash12(i + float2(1, 1)), u.x), u.y);
}

struct MatS {
    float4 c;
    float3 n;
};

MatS sampleTiled(float2 uv, uint layer, float2 gx, float2 gy, float k) {
    const float l = k * 8.0;
    const float i = floor(l), f = l - i;
    const float2 oa = sin(float2(3.0, 7.0) * i), ob = sin(float2(3.0, 7.0) * (i + 1.0));
    const float4 ca = T2Alb.SampleGrad(T2MatSamp, float3(uv + oa, float(layer)), gx, gy);
    const float4 cb = T2Alb.SampleGrad(T2MatSamp, float3(uv + ob, float(layer)), gx, gy);
    const float4 na = T2Nor.SampleGrad(T2MatSamp2, float3(uv + oa, float(layer)), gx, gy);
    const float4 nb = T2Nor.SampleGrad(T2MatSamp2, float3(uv + ob, float(layer)), gx, gy);
    const float b = smoothstep(0.2, 0.8, f - 0.1 * dot(ca.rgb - cb.rgb, float3(1.0, 1.0, 1.0)));
    MatS m;
    m.c = lerp(ca, cb, b);
    m.n = lerp(na.xyz, nb.xyz, b) * 2.0 - 1.0;
    return m;
}

// Spice at p, ORGANIC (no cell grid): x = presence (0 none .. 1 thin spice or more), y = richness
// (0 thin .. 1 the core of thick spice). The texture holds per cell the presence and the richness
// blurred over two cells; both are read through a three-scale noise warp, the presence with a
// round nine-tap kernel, and the richness is broken into clumps - soft irregular patches with
// denser cores; still the simulated amount (a harvested cell thins out and clears).
float2 spiceField(float2 p, float2 mapSize) {
    const float2 q = p + (float2(vnoise(p / 45.0), vnoise(p / 45.0 + 31.7)) - 0.5) * 26.0 +
                     (float2(vnoise(p / 17.0 + 5.3), vnoise(p / 17.0 + 71.1)) - 0.5) * 14.0 +
                     (float2(vnoise(p / 6.5 + 11.7), vnoise(p / 6.5 + 3.9)) - 0.5) * 5.0;
    const float o = 18.0, od = 13.0;
    const float2 c = T2Spice.Sample(T2SpiceSamp, q / mapSize).rg;
    const float presence = saturate((c.r * 0.28 +
                                     (T2Spice.Sample(T2SpiceSamp, (q + float2(o, 0.0)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q - float2(o, 0.0)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q + float2(0.0, o)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q - float2(0.0, o)) / mapSize).r) * 0.12 +
                                     (T2Spice.Sample(T2SpiceSamp, (q + float2(od, od)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q - float2(od, od)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q + float2(od, -od)) / mapSize).r +
                                      T2Spice.Sample(T2SpiceSamp, (q - float2(od, -od)) / mapSize).r) * 0.06) * 1.004);
    const float clump = vnoise(p / 21.0 + 9.1) * 0.55 + vnoise(p / 47.0 + 2.7) * 0.3 + vnoise(p / 9.0 + 5.5) * 0.15;
    const float rich = saturate(c.g * 2.0 * presence * (0.4 + 1.9 * clump) - 0.12);
    return float2(presence, rich);
}

// Round spots (the original's darker spice mounds): 1 inside a spot .. 0 outside. One jittered
// point per cell of `size` world px, the lattice turned off the map's axes; grow scales the
// spots' radius (0 = none). (A thresholded value noise here read as square blocks.)
float spots(float2 p, float size, float grow) {
    const float2 q = float2(p.x * 0.866 - p.y * 0.5, p.x * 0.5 + p.y * 0.866) / size;
    const float2 i = floor(q), f = q - i;
    float best = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x) {
            const float2 c = i + float2(float(x), float(y));
            const float2 d = float2(float(x), float(y)) + 0.5 + (float2(hash12(c), hash12(c + 37.3)) - 0.5) * 0.9 - f;
            const float r = (0.2 + 0.27 * hash12(c * 1.7 + 11.1)) * grow;
            const float dist = length(d * float2(1.0, 1.2 + 0.5 * hash12(c + 5.1)));
            best = max(best, 1.0 - smoothstep(r * 0.5, r, dist));
        }
    return best;
}

// Concrete foundation slabs, one per cell (docs/enhanced-graphics.md "Concrete"): crisp seams
// with a bevel that catches / loses the sun, a shade per slab, panel detail by slab (expansion
// joints, bolt plates, drainage slots), faded paint markings, weathering (dirt, oil stains,
// chipped corners) and sand drifted into the seams. look0 = dust colour rgb, a = style; look1 =
// panel detail, weathering, markings, sand drift. px = one screen pixel in cells (anti-aliasing).
float3 concreteSlab(float2 p, float3 base, inout float3 nrm, float4 look0, float4 look1, float px) {
    const float2 cell = floor(p / 32.0);
    const float2 f = p / 32.0 - cell;
    const float h1 = hash12(cell + 0.37), h2 = hash12(cell * 1.7 + 4.1), h3 = hash12(cell * 2.3 + 9.7),
                h4 = hash12(cell * 0.7 + 17.3);
    const float2 q = abs(f - 0.5);
    const float e = 0.5 - max(q.x, q.y);  // distance to the slab edge (cells)
    const float aa = px + 0.004;
    float3 col = base * (0.9 + 0.2 * h4) * float3(1.0 + 0.03 * (h2 - 0.5), 1.0, 1.0 - 0.03 * (h2 - 0.5));
    const float seam = 1.0 - smoothstep(0.012, 0.012 + aa, e);
    const float bev = 1.0 - smoothstep(0.016, 0.075, e);
    const float2 dir = q.x > q.y ? float2(sign(f.x - 0.5), 0.0) : float2(0.0, sign(f.y - 0.5));
    nrm += float3(dir.x, 0.0, dir.y) * bev * 0.85;
    float groove = 0.0, plate = 0.0;
    const float detail = look1.x;
    if (h1 < 0.26 * detail) {  // an expansion joint across the slab
        const float t = h2 < 0.5 ? abs(f.x - 0.5) : abs(f.y - 0.5);
        groove = 1.0 - smoothstep(0.005, 0.005 + aa, t);
    } else if (h1 < 0.5 * detail) {  // bolt plates in the corners
        const float2 cq = abs(q - 0.34);
        const float pq = max(cq.x, cq.y);
        plate = 1.0 - smoothstep(0.05, 0.05 + aa, pq);
        const float rim = plate * smoothstep(0.03, 0.05, pq);  // the plate's raised edge
        nrm += float3(sign(f.x - 0.5) * (cq.x > cq.y ? 1.0 : 0.0), 0.0, sign(f.y - 0.5) * (cq.x > cq.y ? 0.0 : 1.0)) * rim * 0.4;
        groove = max(groove, (1.0 - smoothstep(0.012, 0.012 + aa, length(q - 0.34))) * 0.8);  // the bolt
    } else if (h1 < 0.68 * detail) {  // drainage slots along one edge
        const float along = h2 < 0.5 ? f.x : f.y, across = h2 < 0.5 ? f.y : f.x;
        const float side = h3 < 0.5 ? across : 1.0 - across;
        groove = (1.0 - smoothstep(0.014, 0.014 + aa, abs(side - 0.13))) * step(frac(along * 7.0), 0.55) *
                 step(0.15, along) * step(along, 0.85);
    }
    if (h3 > 1.0 - look1.z) {  // paint: a stripe along one edge, sometimes hazard-striped
        const float t = h4 < 0.5 ? f.y : f.x;
        float band = smoothstep(0.08, 0.08 + aa, t) * (1.0 - smoothstep(0.15, 0.15 + aa, t));
        if (h2 > 0.55) band *= step(0.5, frac((f.x + f.y) * 4.0));
        const float worn = smoothstep(0.2, 0.62, vnoise(p / 2.5) * 0.6 + vnoise(p / 9.0) * 0.4);
        const float3 paint = h1 < 0.8 ? float3(0.74, 0.58, 0.2) : float3(0.8, 0.8, 0.78);
        col = lerp(col, paint * (0.9 + 0.2 * h4), band * worn * 0.75);
    }
    col *= 1.0 - 0.14 * plate;
    // Weathering: grime, oil / scorch stains, chipped corners.
    const float weather = look1.y;
    const float dirt = vnoise(p / 11.0) * 0.6 + vnoise(p / 3.5) * 0.4;
    col *= 1.0 - weather * 0.2 * smoothstep(0.4, 0.85, dirt);
    const float stain = smoothstep(0.6, 0.85, vnoise(p / 6.0 + h1 * 37.0) * 0.75 + vnoise(p / 2.2) * 0.25) *
                        step(h4, 0.12 + 0.25 * weather);
    col = lerp(col, col * float3(0.5, 0.48, 0.46), stain * 0.5);
    const float2 cq2 = 0.5 - q;  // corner chips
    const float chip = step(cq2.x + cq2.y, 0.05 + 0.05 * weather * vnoise(p / 1.5 + h3 * 11.0)) * step(0.5, h2 + 0.3 * weather);
    // Sand: in the seams and grooves, drifting onto the slab edges in patches.
    const float drift = look1.w * smoothstep(0.3, 0.78, vnoise(p / 7.0 + 3.1) * 0.7 + vnoise(p / 2.0) * 0.3);
    const float gap = max(max(seam, groove), chip);
    col *= 1.0 - 0.55 * gap;
    const float sand = saturate(gap * drift * 0.8 + bev * drift * 0.35 + chip * 0.3);
    return lerp(col, look0.rgb * (0.92 + 0.1 * dirt), sand);
}

float4 terrain2_fs(TVOut i) : SV_Target0 {
    const float2 p = i.wpos.xz;
    const float2 mapSize = t2Fog2.yz;
    const float3 dpx = ddx(i.wpos), dpy = ddy(i.wpos);
    const float3 gn = normalize(i.nrm);

    float w[7];
    const float4 a = T2SplatA.Sample(T2SplatSamp, p / mapSize);
    const float4 b = T2SplatB.Sample(T2SplatSamp2, p / mapSize);
    w[0] = a.x; w[1] = a.y; w[2] = a.z; w[3] = a.w; w[4] = b.x; w[6] = 0.0; w[5] = 0.0;
    // Concrete: a binary per-cell channel, thresholded at 0.5 (exactly the cell edges) with a
    // one-pixel ramp: crisp slab borders, blended over the other materials afterwards.
    const float cfw = max(fwidth(b.y), 1e-3);
    const float cw = smoothstep(0.5 - cfw, 0.5 + cfw, b.y);
    const float pxCells = max(length(dpx.xz), length(dpy.xz)) / 32.0;
    const float2 sf = spiceField(p, mapSize);
    const float amount = (sf.x + sf.y) * (2.0 / 7.0);
    const float grains = vnoise(p / 5.0) * 0.6 + vnoise(p / 15.0) * 0.4;
    // The edge of a field dissolves: the presence against a noise threshold (lobes and specks).
    const float edgeN = vnoise(p / 11.0 + 4.0) * 0.5 + vnoise(p / 4.0 + 8.0) * 0.3 + vnoise(p / 1.7) * 0.2;
    const float cover = smoothstep(-0.12, 0.2, sf.x - (0.15 + 0.6 * edgeN));
    float spice = lerp(cover * 0.78, 1.0, sf.y) * smoothstep(1.0, 0.55, grains - amount * 0.3) * 0.92;
    spice *= saturate((w[0] + w[1]) * 1.6);
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    const bool steep = gn.y < 0.82;
    const float3 tw = pow(abs(gn), float3(4.0, 4.0, 4.0));
    const float3 tri = tw / (tw.x + tw.y + tw.z);
    float4 c[7];
    float3 nn[7];
    float hmax = -1.0;
    [unroll] for (int m = 0; m < 7; ++m) {
        c[m] = float4(0.0, 0.0, 0.0, 0.0);
        nn[m] = float3(0.0, 0.0, 0.0);
        if (w[m] >= 0.004) {
            const float sc = 1.0 / t2Tint[m].a;
            MatS s = sampleTiled(p * sc, uint(m), dpx.xz * sc, dpy.xz * sc, k);
            float3 n = gn + float3(s.n.x, 0.0, -s.n.y) * t2Lum[m].a;
            if (steep && (m == 2 || m == 3 || m == 4)) {
                const MatS mx = sampleTiled(float2(i.wpos.z, -i.wpos.y) * sc, uint(m), float2(dpx.z, -dpx.y) * sc,
                                            float2(dpy.z, -dpy.y) * sc, k);
                const MatS mz = sampleTiled(float2(i.wpos.x, -i.wpos.y) * sc, uint(m), float2(dpx.x, -dpx.y) * sc,
                                            float2(dpy.x, -dpy.y) * sc, k);
                s.c = s.c * tri.y + mx.c * tri.x + mz.c * tri.z;
                n = gn + (float3(s.n.x, 0.0, -s.n.y) * tri.y + float3(0.0, mx.n.y, mx.n.x) * tri.x +
                          float3(mz.n.x, mz.n.y, 0.0) * tri.z) * t2Lum[m].a;
            }
            c[m] = s.c;
            nn[m] = n;
            hmax = max(hmax, s.c.a + w[m]);
        }
    }
    const float depth = t2Params.y;
    float3 albedo = float3(0.0, 0.0, 0.0), n = float3(0.0, 0.0, 0.0);
    float sum = 0.0;
    [unroll] for (int j = 0; j < 7; ++j) {
        if (w[j] >= 0.004) {
            const float bw = max(c[j].a + w[j] - hmax + depth, 0.0);
            const float L = dot(c[j].rgb, float3(0.299, 0.587, 0.114));
            albedo += lerp(c[j].rgb * t2Tint[j].rgb, L * t2Lum[j].rgb, t2Params.w) * bw;
            n += nn[j] * bw;
            sum += bw;
        }
    }
    albedo /= max(sum, 1e-4);
    n = normalize(sum > 0.0 ? n : gn);
    if (cw > 0.004) {  // concrete slabs over the rest
        const float sc = 1.0 / t2Tint[6].a;
        MatS s = sampleTiled(p * sc, 6u, dpx.xz * sc, dpy.xz * sc, k);
        float3 cn = gn + float3(s.n.x, 0.0, -s.n.y) * t2Lum[6].a;
        const float L = dot(s.c.rgb, float3(0.299, 0.587, 0.114));
        float3 cc = lerp(s.c.rgb * t2Tint[6].rgb, L * t2Lum[6].rgb, t2Params.w);
        cc = concreteSlab(p, cc, cn, t2Tint[7], t2Lum[7], pxCells);
        const float t = sum > 0.0 ? cw : 1.0;
        albedo = lerp(albedo, cc, t);
        n = normalize(lerp(n, normalize(cn), t));
    }
    // Darker spice mounds (the original's spots), more of them in thick fields.
    const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
    albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
    albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick
    const float macro = vnoise(p / 700.0) * 0.6 + vnoise(p / 230.0 + 5.0) * 0.4;
    albedo *= 1.0 + t2Params.z * (macro - 0.5);

    const float ndl = max(dot(n, t2SunDir.xyz), 0.0);
    float sm = i.sun;
    if (t2Shadow.x > 0.5) SHADOW_PCF(T2ShadowMap, T2ShadowSamp, mul(t2LightVP, float4(i.wpos, 1.0)).xyz, t2Shadow, sm);
    const float vis = lerp(t2Light.w, 1.0, sm);
    const float3 shade = sunAmb(t2Light.x, t2Light.y * ndl * vis) * t2Light.z;
    float3 rgb = satur(albedo) * shade + albedo * pointLights7(i.wpos, n, 0.3);
    rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils (0 strength = off)
    float s2 = 0.0;
    // fog2.w: 0 no shroud, 1 shroud, 2 baked for the Enhanced 2D view (no shroud, no edge fade).
    if (t2Fog2.w > 0.5 && t2Fog2.w < 1.5) s2 = T2Shroud.Sample(T2ShroudSamp, p / mapSize).a;
    const float2 e = min(p, mapSize - p);
    if (t2Fog2.w < 1.5) s2 = max(s2, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(i.wpos, t2Fog.xyz);
    const float f = smoothstep(t2Fog.w, t2Fog2.x, d) * aerialK();
    rgb = lerp(rgb, t2FogColor.rgb, f) * (1.0 - s2);
    return float4(rgb, 1.0);
}

// ================================================================================== faithful designed terrain
// HLSL twin of render3d/terrain_v3_msl.hpp ("Designed (faithful)", docs/3d-mode.md "Terrain v3"):
// terrain3_fs (with terrain_vs) = terrain2_fs with the layout taken from the guide texture
// (render3d/terrain_guide.*: rock coverage, macro relief, tone, ruggedness - read from the
// original tiles at run time). Uses the designed terrain's textures, uniform block and helpers
// above, plus the guide (t7) and its own block (b2). Material layer 7 = the crag rock.
Texture2D<float4> T3Guide : register(t7, space2);
SamplerState T3GuideSamp : register(s7, space2);
// The world beyond the map (docs/3d-mode.md): the far texture (render3d/surround.*).
Texture2D<float4> T3Far : register(t8, space2);
SamplerState T3FarSamp : register(s8, space2);
// Photo terrain (docs/3d-mode.md "Photo terrain"; render3d/photo_terrain.*): the scans' colour, normal (x, y) and
// occlusion / roughness / height arrays; then the screen-space ambient occlusion (render3d/ssao.*).
Texture2DArray<float4> T3PhAlb : register(t9, space2);
SamplerState T3PhSamp : register(s9, space2);
Texture2DArray<float4> T3PhNor : register(t10, space2);
SamplerState T3PhSamp2 : register(s10, space2);
Texture2DArray<float4> T3PhOrh : register(t11, space2);
SamplerState T3PhSamp3 : register(s11, space2);
Texture2D<float4> T3Ssao : register(t12, space2);
SamplerState T3SsaoSamp : register(s12, space2);
cbuffer Terrain3FU : register(b2, space3) {
    float4 t3Guide;        // x cliff height (world px), y guide texels per world px, z 1 = baked from above, w tone relief (px)
    float4 t3Level[5];     // rgb colour of the broken rock's tone level i; a = its upper tone edge (level 4: contour raggedness)
    float4 t3CragTex;      // x 1 / the crag texture's mean luminance, y contrast (gamma), z normal strength, w world px per repeat
    float4 t3RoughTex;     // x 1 / the rough texture's mean luminance, y contrast (gamma), z, w unused
    float4 t3Shade;        // a face's tone: x against its top, y + the part by the side it looks to (towards the sun
                           // lighter); z the pale rim's strength, w the share of the original's fine light and dark
    float4 t3Face;         // x gullies (strength), y ledge line, z darker foot (tone units), w contact shadow on the sand
    float4 t3Slab;         // rgb concrete slab colour, a speckle
    float4 t3SeamLight;    // rgb pale seam colour, a unused
    float4 t3SeamDark;     // rgb dark seam line colour, a unused
    float4 t3DetailSun;    // xyz the sun of the level ground's fine detail, w 1 / its flat-ground light
    float4 t3ReliefLight;  // ambient, diffuse, 1 / flat-ground light of the broken rock (lit by sunDir), w unused
    float4 t3Reim[4];      // "Reimagined" (docs/3d-mode.md): [0] x on, y spice glow, z sand ripples, w gravel;
                           // [1] xy wind direction, z cliff strata, w cavity occlusion; [2] x time, y the world beyond
                           // the map's margin (px), z shadow blue, w world beyond the map: 0 off, 1 the map with it on,
                           // 2 the land beyond;
                           // [3] x world px per shadow texel, y penumbra per px of distance, z sandstone cliffs
    float4 t3Photo[9];     // "Photo terrain": [0] x on, y the scans' share in the colour, z normal strength, w sun sheen;
                           // [1] unused; [2 + layer] rgb = 1 / the scan's mean colour, a = world px per repeat
                           // (layers: 0 sand, 1 ripples, 2 gravel, 3 rock, 4 rough rock, 5 cliff, 6 concrete)
};

// Reimagined: the macro relief's height (kCliffHeight units) for the guide's height gg (Terrain::reliefHeight).
float macroH(float gg) {
    if (t3Reim[0].x < 0.5) return gg;
    const float t = saturate((gg - 0.1333) / 0.8667);
    return gg * (1.6 + 0.8 * t * t * (3.0 - 2.0 * t));
}

// The relief (world px above the ground): the guide's macro height, and on broken rock the
// original's own light and dark as relief (pale = proud, dark = a pocket).
float faithHeight(float4 g, bool macro) {
    return (macro ? macroH(g.g) * t3Guide.x : 0.0) + g.a * (g.b * 2.0 - 1.0) * t3Guide.w;
}

// The guide at world point p, smoothly upsampled (zoomed in, a bilinear tap's kinks at every
// texel border showed the original's tile pixels as blocks and stair-steps): while a guide texel
// covers more than about a pixel a Catmull-Rom bicubic (5 bilinear taps, clamped), minified the
// plain trilinear tap (blended over lod 0.5 .. 1). texPerPx = guide texels per world px.
float4 guideAt(float2 p, float2 mapSize, float texPerPx, float lod) {
    const float4 lin = T3Guide.SampleLevel(T3GuideSamp, p / mapSize, lod);
    if (lod >= 1.0) return lin;
    const float2 size = mapSize * texPerPx;
    const float2 sp = p * texPerPx;
    const float2 t1 = floor(sp - 0.5) + 0.5;
    const float2 f = sp - t1;
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2;
    const float2 a0 = (t1 - 1.0) / size, a12 = (t1 + w2 / w12) / size, a3 = (t1 + 2.0) / size;
    const float c = w12.x * w12.y, n = w12.x * w0.y, so = w12.x * w3.y, we = w0.x * w12.y, ea = w3.x * w12.y;
    float4 r = T3Guide.SampleLevel(T3GuideSamp, a12, lod) * c + T3Guide.SampleLevel(T3GuideSamp, float2(a12.x, a0.y), lod) * n +
               T3Guide.SampleLevel(T3GuideSamp, float2(a12.x, a3.y), lod) * so +
               T3Guide.SampleLevel(T3GuideSamp, float2(a0.x, a12.y), lod) * we +
               T3Guide.SampleLevel(T3GuideSamp, float2(a3.x, a12.y), lod) * ea;
    r = saturate(r / (c + n + so + we + ea));
    return lerp(r, lin, smoothstep(0.5, 1.0, lod));
}

// The guide for the world beyond the map (docs/3d-mode.md "World beyond the map"; sk = the margin in
// world px, the mode: 2 = the land beyond): the map's own guide, mirrored across the edge by its sampler,
// blended by the far texture's weight (b) into the procedural desert it describes (r rock coverage, g relief
// height; sk.zw = its tone and ruggedness there). Every other draw: the guide as it is.
float4 guideX(float2 p, float2 mapSize, float texPerPx, float lod, float4 sk) {
    if (sk.y < 1.5) return guideAt(p, mapSize, texPerPx, lod);
    const float4 f = T3Far.SampleLevel(T3FarSamp, (p + sk.x + 4.0) / (mapSize + 2.0 * sk.x + 8.0), 0.0);
    const float4 fg = float4(f.r, f.g, sk.z, sk.w);
    if (f.b > 0.996) return fg;
    return lerp(guideAt(p, mapSize, texPerPx, lod), fg, f.b);
}

// A column of a cliff face (the rock between two gullies) at coordinate u (world px) across the
// face: x = the position inside the column 0..1, y = a random value per column. Jittered widths.
float2 column(float u, float width, float seed) {
    const float t = u / width;
    const float i = floor(t);
    float bl = i + (hash12(float2(i, seed)) - 0.5) * 0.6;
    float br = i + 1.0 + (hash12(float2(i + 1.0, seed)) - 0.5) * 0.6;
    float idx = i;
    if (t < bl) {
        br = bl;
        bl = i - 1.0 + (hash12(float2(i - 1.0, seed)) - 0.5) * 0.6;
        idx = i - 1.0;
    } else if (t >= br) {
        bl = br;
        br = i + 2.0 + (hash12(float2(i + 2.0, seed)) - 0.5) * 0.6;
        idx = i + 1.0;
    }
    return float2((t - bl) / max(br - bl, 1e-3), hash12(float2(idx + 0.5, seed + 7.0)));
}

// The colour of broken rock of tone t: the original's tone levels (crevices, shaded rock, mid
// rock, lit facets, pale rims) with crisp edges along the tone's contours (aa = half an edge's
// width in tone units) and a little of the continuous tone inside each level.
float3 toneRamp(float t, float aa) {
    const float s0 = smoothstep(t3Level[0].a - aa, t3Level[0].a + aa, t);
    const float s1 = smoothstep(t3Level[1].a - aa, t3Level[1].a + aa, t);
    const float s2 = smoothstep(t3Level[2].a - aa, t3Level[2].a + aa, t);
    const float s3 = smoothstep(t3Level[3].a - aa, t3Level[3].a + aa, t);
    float3 c = lerp(t3Level[0].rgb, t3Level[1].rgb, s0);
    c = lerp(c, t3Level[2].rgb, s1);
    c = lerp(c, t3Level[3].rgb, s2);
    c = lerp(c, t3Level[4].rgb, s3);
    const float centre = -0.75 + s0 * 0.31 + s1 * 0.255 + s2 * 0.25 + s3 * 0.295;
    return c * (1.0 + 0.35 * clamp(t - centre, -0.3, 0.3));
}

// The original's concrete: two slabs per cell (32 x 16 px, a plain grid), a pale seam along
// each slab's south and west edge and a dark line along its north and east edge, the slab colour
// and fine speckle of the original's tiles - and USED, like the Designed concrete: a shade per
// slab, grime in soft blotches, dark stains, hairline cracks, chipped corners, dirt along the
// seams and sand drifted into them. The same with or without a building on it. grainRel = the
// scanned concrete's luminance / its mean; sand = the colour of drifted sand; px = one screen
// pixel in world px.
float3 concreteSlab3(float2 p, float grainRel, inout float3 nrm, float3 sand, float px) {
    const float2 s = float2(p.x / 32.0, p.y / 16.0);
    const float2 cell = floor(s);
    const float2 f = (s - cell) * float2(32.0, 16.0);
    const float h1 = hash12(cell + 0.37), h2 = hash12(cell * 1.7 + 4.1), h3 = hash12(cell * 2.9 + 11.3),
                h4 = hash12(cell * 0.7 + 17.3);
    const float aa = px * 0.75 + 0.05;
    float3 col = t3Slab.rgb * (0.95 + 0.2 * h1) * float3(1.0 + 0.04 * (h2 - 0.5), 1.0, 1.0 - 0.06 * (h2 - 0.5));
    // Speckle (the original's: light specks paler, dark ones red-brown), then the scan's grain.
    const float grain = vnoise(p * 1.9) * 0.55 + vnoise(p * 4.3 + 9.0) * 0.45;
    const float speck = t3Slab.a * (grain - 0.5);
    col *= float3(1.0 + speck, 1.0 + 1.5 * speck, 1.0 + 2.4 * speck) * (1.0 + 0.2 * (grainRel - 1.0));
    // Wear: grime in soft blotches; dark stains on some slabs; dirt gathered along the seams.
    const float dirtN = vnoise(p / 11.0) * 0.6 + vnoise(p / 3.5) * 0.4;
    col *= 1.0 - 0.17 * smoothstep(0.38, 0.85, dirtN);
    const float stain = smoothstep(0.56, 0.85, vnoise(p / 6.0 + h1 * 37.0) * 0.75 + vnoise(p / 2.2) * 0.25) * step(h3, 0.36);
    col = lerp(col, col * float3(0.55, 0.5, 0.46), stain * 0.55);
    const float seamDist = min(min(f.x, 32.0 - f.x), min(f.y, 16.0 - f.y));
    const float edgeBand = 1.0 - smoothstep(0.5, 3.5, seamDist);
    col *= 1.0 - 0.2 * edgeBand * (0.4 + 0.6 * vnoise(p / 4.0 + 5.0));
    // A hairline crack wandering across some slabs.
    const float wander = (vnoise(float2(p.x / 6.0, cell.y * 7.3 + 1.0)) - 0.5) * 9.0;
    const float crack = step(0.84, h4) * (1.0 - smoothstep(0.2, 0.55 + aa, abs(f.y - 8.0 - wander))) *
                        smoothstep(0.0, 4.0, f.x - 28.0 * h2 + 4.0) * 0.8;
    col *= 1.0 - 0.36 * crack;
    // Seams: pale along the south and west edges, dark along the north and east.
    const float south = smoothstep(14.5 - aa, 14.5 + aa, f.y);
    const float west = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, f.x);
    const float north = 1.0 - smoothstep(1.6 - aa, 1.6 + aa, f.y);
    const float east = smoothstep(30.5 - aa, 30.5 + aa, f.x);
    const float worn = 0.75 + 0.25 * vnoise(p / 3.0 + 11.0);
    col = lerp(col, t3SeamDark.rgb, saturate(max(north, east * 0.9)) * (0.85 + 0.15 * worn));
    col = lerp(col, t3SeamLight.rgb, saturate(max(south, west)) * (0.55 + 0.3 * worn));
    // Chipped corners (some), and sand drifted into the seams and the chips in patches.
    const float2 cq = float2(min(f.x, 32.0 - f.x), min(f.y, 16.0 - f.y));
    const float cornerId = hash12(cell * 3.1 + float2(step(16.0, f.x), step(8.0, f.y)) * 5.7 + 2.9);
    const float chip = step(cq.x + cq.y, 2.2 + 3.0 * vnoise(p / 1.5 + h3 * 11.0)) * step(cornerId, 0.38);
    col *= 1.0 - 0.35 * chip;
    const float drift = smoothstep(0.32, 0.78, vnoise(p / 7.0 + 3.1) * 0.7 + vnoise(p / 2.0) * 0.3);
    col = lerp(col, sand * (0.9 + 0.12 * dirtN), saturate(edgeBand * drift * 0.22 + chip * 0.55));
    nrm = lerp(nrm, float3(0.0, 1.0, 0.0), saturate(max(max(south, west), max(north, east))) * 0.7);
    return col;
}

// ---- Reimagined (docs/3d-mode.md "Reimagined"; MSL twin in terrain_v3_msl.hpp) ----
float4 pebbles(float2 p, float size, float density) {
    const float2 q = p / size;
    const float2 ii = floor(q), f = q - ii;
    float4 best = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            const float2 c = ii + float2(float(x), float(y));
            if (hash12(c + 0.71) > density) continue;
            const float2 ctr = float2(float(x), float(y)) + 0.5 + (float2(hash12(c + 3.3), hash12(c + 8.9)) - 0.5) * 0.7;
            const float r = 0.2 + 0.24 * hash12(c + 5.1);
            const float2 d = (f - ctr) / r;
            const float dist = length(d * float2(1.0, 0.8 + 0.5 * hash12(c + 2.2)));
            if (dist < 1.0 && 1.0 - dist > best.x) best = float4(1.0 - dist, d * 0.9, hash12(c + 9.7));
        }
    return best;
}

// ---- Photo terrain (docs/3d-mode.md "Photo terrain"; MSL twin in terrain_v3_msl.hpp) ----
struct PhS {
    float3 d;    // the scan's colour / its mean colour
    float2 n;    // tangent-space normal, x and y
    float3 orh;  // ambient occlusion, roughness, height
};
PhS photoTap(float2 uv, uint layer, float3 inv, float2 gx, float2 gy, float k) {
    const float l = k * 8.0;
    const float i = floor(l);
    const float b = smoothstep(0.25, 0.75, l - i);
    gx *= 0.8;  // (a little sharper than the footprint asks for)
    gy *= 0.8;
    PhS r;
    r.d = float3(0.0, 0.0, 0.0);
    r.n = float2(0.0, 0.0);
    r.orh = float3(0.0, 0.0, 0.0);
    if (b < 0.999) {
        const float3 q = float3(uv + sin(float2(3.0, 7.0) * i), float(layer));
        r.d += T3PhAlb.SampleGrad(T3PhSamp, q, gx, gy).rgb * (1.0 - b);
        r.n += T3PhNor.SampleGrad(T3PhSamp2, q, gx, gy).rg * (1.0 - b);
        r.orh += T3PhOrh.SampleGrad(T3PhSamp3, q, gx, gy).rgb * (1.0 - b);
    }
    if (b > 0.001) {
        const float3 q = float3(uv + sin(float2(3.0, 7.0) * (i + 1.0)), float(layer));
        r.d += T3PhAlb.SampleGrad(T3PhSamp, q, gx, gy).rgb * b;
        r.n += T3PhNor.SampleGrad(T3PhSamp2, q, gx, gy).rg * b;
        r.orh += T3PhOrh.SampleGrad(T3PhSamp3, q, gx, gy).rgb * b;
    }
    const float2 um = float2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8) * 0.29 + 0.37;
    const float2 mx = float2(gx.x * 0.8 - gx.y * 0.6, gx.x * 0.6 + gx.y * 0.8) * 0.29, my = float2(gy.x * 0.8 - gy.y * 0.6, gy.x * 0.6 + gy.y * 0.8) * 0.29;
    const float m = dot(T3PhAlb.SampleGrad(T3PhSamp, float3(um, float(layer)), mx, my).rgb * inv, float3(0.299, 0.587, 0.114));
    r.d *= inv * lerp(1.0, m, 0.7);
    r.n = r.n * 2.0 - 1.0;
    return r;
}
void photoAdd(PhS t, float w, float nk, inout float3 d, inout float2 n, inout float3 orh, inout float sum) {
    const float bw = w * lerp(1.0, 0.25 + 1.5 * t.orh.b, 0.7 * saturate(4.0 * w * (1.0 - w)));
    d += t.d * bw;
    n += t.n * (nk * bw);
    orh += t.orh * bw;
    sum += bw;
}

// ---- Reimagined round 2: sandstone cliffs, soft cast shadows (MSL twin in terrain_v3_msl.hpp) ----
float3 sandstone(float2 uv, float px, out float2 nt, out float occ) {
    const float fine = 1.0 - smoothstep(0.4, 1.6, px), mid = 1.0 - smoothstep(1.2, 4.0, px);
    const float sw = (vnoise(float2(uv.x / 90.0, uv.y / 60.0)) - 0.5) * 1.4 + (vnoise(uv / 17.0 + 5.0) - 0.5) * 0.12;
    const float s = uv.y / 14.0 + sw;
    const float id = floor(s), f = s - id;
    const float h1 = hash12(float2(id, 17.0)), h2 = hash12(float2(id, 41.0));
    float3 col = lerp(float3(0.60, 0.38, 0.24), float3(0.74, 0.54, 0.35), smoothstep(0.2, 0.6, h1));
    col = lerp(col, float3(0.8, 0.65, 0.47), smoothstep(0.88, 0.98, h1));
    col *= 0.92 + 0.16 * vnoise(float2(uv.x / 40.0, id * 3.1));
    const float lam = vnoise(float2(uv.x / 30.0, uv.y * 0.9 + id));
    col *= 1.0 + (lam - 0.5) * 0.12 * mid;
    const float grp = floor(s / 3.0);
    const float wid = 14.0 + 20.0 * hash12(float2(grp, 5.0));
    const float2 c = column(uv.x + (vnoise(float2(uv.y / 11.0, grp)) - 0.5) * 4.0 + grp * 37.0, wid, grp * 3.7 + 1.0);
    const float e = min(c.x, 1.0 - c.x) * wid;
    const float crackOn = smoothstep(0.45, 0.75, vnoise(float2(uv.x / 9.0, uv.y / 13.0) + 7.0));
    const float joint = (1.0 - smoothstep(0.0, 0.6 + px * 0.6, e)) * mid * crackOn;
    col *= (0.9 + 0.2 * c.y) * (1.0 - 0.32 * joint);
    const float bevel = (1.0 - smoothstep(0.0, 3.0, c.x * wid)) - (1.0 - smoothstep(0.0, 3.0, (1.0 - c.x) * wid));
    const float2 fl = column(uv.x + (vnoise(float2(uv.y / 7.0, grp)) - 0.5) * 2.5, 5.0 + 5.0 * hash12(float2(grp, 9.0)), grp * 1.3 + 4.0);
    const float flute = smoothstep(0.25, 0.65, vnoise(float2(uv.x / 40.0, uv.y / 30.0) + 11.0)) * mid;
    col *= 1.0 - 0.18 * flute * pow(abs(fl.x * 2.0 - 1.0), 3.0);
    nt.x = bevel * 0.55 * mid * crackOn + (fl.x - 0.5) * 1.5 * flute + (vnoise(uv / 5.0 + 2.0) - 0.5) * 0.5 * fine;
    const float lipW = 0.6 + 0.4 * h2;
    const float lip = smoothstep(0.78, 0.98, f) * lipW, under = (1.0 - smoothstep(0.0, 0.12, f)) * lipW;
    nt.y = (lip * 0.9 - under * 0.7) * mid + (vnoise(uv / 4.0 + 8.0) - 0.5) * 0.4 * fine;
    col *= 1.0 + 0.1 * lip * mid;
    occ = (1.0 - 0.4 * under * mid) * (1.0 - 0.35 * joint);
    col *= 1.0 + (vnoise(uv * 1.1) - 0.5) * 0.16 * fine + (vnoise(uv * 0.33 + 9.0) - 0.5) * 0.12 * mid;
    col *= 1.0 - 0.18 * smoothstep(0.6, 0.9, vnoise(float2(uv.x / 3.5, uv.y / 70.0) + 3.0)) * mid;
    return col;
}

void reimCliff(float3 wpos, float3 gn, float px, float w, bool ng, inout float3 albedo, inout float3 n, inout float ao,
               float3 dpx, float3 dpy) {
    if (w < 0.004) return;
    float2 bw = pow(abs(gn.xz) + 1e-4, float2(4.0, 4.0));
    bw = saturate(bw / (bw.x + bw.y) * 1.25 - 0.125);  // (one projection only on most faces)
    const float sx = gn.x >= 0.0 ? 1.0 : -1.0, sz = gn.z >= 0.0 ? 1.0 : -1.0;
    float2 ntX = float2(0.0, 0.0), ntZ = float2(0.0, 0.0);
    float oX = 1.0, oZ = 1.0;
    float3 col = float3(0.0, 0.0, 0.0);
    if (t3Photo[0].x > 0.5) {
        // Photo terrain: the layered sandstone scan on the two vertical projections, in the sandstone's own ochre.
        const float sc = 1.0 / t3Photo[7].a;
        const float kk = vnoise(wpos.xz / 83.0 + wpos.y / 61.0 + 3.0);
        const float bed = vnoise(float2((wpos.x + wpos.z) / 150.0, wpos.y / 16.0 + 4.0));
        const float3 base = lerp(float3(0.68, 0.46, 0.30), float3(0.85, 0.64, 0.43), smoothstep(0.25, 0.7, bed));
        if (bw.x > 0.01) {
            const PhS t = photoTap(float2(-wpos.z * sx, -wpos.y) * sc, 5u, t3Photo[7].rgb,
                                   float2(-dpx.z * sx, -dpx.y) * sc, float2(-dpy.z * sx, -dpy.y) * sc, kk);
            const float tl = dot(t.d, float3(0.299, 0.587, 0.114));
            col += base * lerp(float3(tl, tl, tl), t.d, 0.6) * bw.x;
            ntX = t.n * (1.3 * t3Photo[0].z);
            oX = lerp(1.0, t.orh.r, 0.65);
        }
        if (bw.y > 0.01) {
            const PhS t = photoTap(float2(wpos.x * sz, -wpos.y) * sc + 0.43, 5u, t3Photo[7].rgb,
                                   float2(dpx.x * sz, -dpx.y) * sc, float2(dpy.x * sz, -dpy.y) * sc, kk);
            const float tl = dot(t.d, float3(0.299, 0.587, 0.114));
            col += base * lerp(float3(tl, tl, tl), t.d, 0.6) * bw.y;
            ntZ = t.n * (1.3 * t3Photo[0].z);
            oZ = lerp(1.0, t.orh.r, 0.65);
        }
    } else {
        if (bw.x > 0.01) col += sandstone(float2(-wpos.z * sx, wpos.y), px, ntX, oX) * bw.x;
        if (bw.y > 0.01) col += sandstone(float2(wpos.x * sz, wpos.y) + 311.0, px, ntZ, oZ) * bw.y;
    }
    col *= lerp(float3(1.06, 0.97, 0.9), float3(0.94, 1.0, 1.06), vnoise(wpos.xz / 260.0 + wpos.y / 400.0));
    const float3 tX = float3(0.0, 0.0, -sx), tZ = float3(sz, 0.0, 0.0);
    const float3 fn = normalize(gn + (tX * ntX.x + float3(0.0, ntX.y, 0.0)) * bw.x + (tZ * ntZ.x + float3(0.0, ntZ.y, 0.0)) * bw.y);
    const float foot = 1.0 - smoothstep(2.0, 22.0, wpos.y);
    col *= 1.0 - 0.25 * foot;
    const float scree = (1.0 - smoothstep(7.0, 15.0, wpos.y + (vnoise(wpos.xz / 6.0) - 0.5) * 8.0)) * (1.0 - smoothstep(0.6, 2.5, px));
    float3 fnS = fn;
    if (scree > 0.01) {
        const float4 pb = pebbles(float2(wpos.x + wpos.z, wpos.y * 1.4), 3.6, 0.8);
        const float on = smoothstep(0.0, 0.25, pb.x);
        col = lerp(col, lerp(col * 0.55, float3(0.55, 0.42, 0.31) * (0.75 + 0.5 * pb.w), on), scree);
        fnS = normalize(fn + float3(pb.y, pb.x * 0.6, pb.z) * scree);
    }
    // (Realistic grade: the sandstone calmer - the same family as the paler ground, not an orange wall.)
    if (ng) {
        const float cl = dot(col, float3(0.299, 0.587, 0.114));
        col = lerp(float3(cl, cl, cl), col, 0.6) * float3(1.0, 0.985, 0.94);
    }
    albedo = lerp(albedo, col, w);
    n = normalize(lerp(n, fnS, w));
    ao = lerp(ao, ao * (oX * bw.x + oZ * bw.y) * (1.0 - 0.3 * foot), w);
}

// Soft cast shadows (contact hardening from compares only; see the MSL twin). Reads the designed
// terrain's shadow map directly (glslang does not like textures as function arguments).
static const float2 kDisk[12] = {float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
                                 float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
                                 float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
float shadowSoft(float3 lp, float2 scr) {
    const float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (t2Shadow.x < 0.5 || any(uv <= float2(0.0, 0.0)) || any(uv >= float2(1.0, 1.0)) || lp.z >= 1.0) return 1.0;
    const float ref = lp.z - t2Shadow.w;
    const float a = 6.2831853 * frac(52.9829189 * frac(dot(scr, float2(0.06711056, 0.00583715))));
    const float ca = cos(a), sa = sin(a);
    const float uvPerPx = t2Shadow.y / max(t3Reim[3].x, 1e-4);
    // Fast path: the pixel and four taps the widest penumbra away agree (open ground, a shadow's core).
    float q = T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, uv, ref);
    [unroll] for (int j = 0; j < 4; ++j) {
        const float2 d = float2(j == 0 ? 1.0 : j == 1 ? -1.0 : 0.0, j == 2 ? 1.0 : j == 3 ? -1.0 : 0.0);
        q += T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * (7.0 * uvPerPx), ref);
    }
    if (q > 4.995) return 1.0;
    if (q < 0.005) return 0.0;
    float o0 = 0.0, o1 = 0.0, o2 = 0.0;
    [unroll] for (int k = 0; k < 4; ++k) {
        const float2 d = kDisk[k * 3];
        const float2 q = uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * (9.0 * uvPerPx);
        o0 += 1.0 - T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, q, ref);
        o1 += 1.0 - T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, q, ref - 40.0 * t2Shadow.w);
        o2 += 1.0 - T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, q, ref - 120.0 * t2Shadow.w);
    }
    if (o0 < 0.01) return 1.0;
    const float dist = 12.0 + 70.0 * (o1 / o0) + 130.0 * (o2 / o0);
    const float r = clamp(0.6 + dist * t3Reim[3].y, 0.6, 7.0) * uvPerPx;
    float s = 0.0;
    [unroll] for (int m = 0; m < 8; ++m) {
        const float2 d = kDisk[m];
        s += T2ShadowMap.SampleCmpLevelZero(T2ShadowSamp, uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * r, ref);
    }
    return s / 8.0;
}

void reimagine(float2 p, float3 wpos, float rockW, float faceW, float spice, float rich, float cw, float px, float4 g,
               float4 gb, float3 eye, inout float3 albedo, inout float3 n, inout float ao, inout float glow,
               float2 gx, float2 gy, float dunes, float crag, inout float gloss) {
    const float3 lumW = float3(0.299, 0.587, 0.114);
    const float sandW = (1.0 - rockW) * (1.0 - cw);
    const float rockOnly = rockW * (1.0 - cw);
    const float fine = 1.0 - smoothstep(0.5, 1.8, px);
    const float mid = 1.0 - smoothstep(1.5, 5.0, px);
    const float L = dot(albedo, lumW);
    const float3 tanTint = float3(1.05, 0.98, 0.86);
    // Realistic grade (docs/3d-mode.md "Realistic grade"; MSL twin): pale ochre / khaki sand, the buildable rock a
    // greyer brown with 40 % less contrast, the spice a dark rust / cinnamon. Off: the vivid round-2 palette.
    const bool ng = t3Reim[3].w > 0.5;
    const float rockLift = rockOnly * (1.0 - spice);
    const float Lr = lerp(L, 0.36, 0.45 * faceW);  // calmer faces: the original's near-black gullies lifted
    if (ng) {
        albedo = lerp(float3(L, L, L), albedo, lerp(0.32, 0.5, spice)) * lerp(float3(1.0, 0.95, 0.845), float3(1.08, 0.98, 0.83), spice);
        albedo = lerp(albedo, float3(L, L, L) * float3(0.9, 0.89, 0.88), 0.55 * cw);  // the slabs: a cooler grey, man-made
        const float Lm = dot(lerp(t3Level[2].rgb, t3Level[3].rgb, 0.6), lumW);  // the plain rock's mean tone
        const float Lc = lerp(lerp(L, Lm, 0.4), Lr, faceW);
        albedo *= lerp(1.0, Lc / max(L, 0.03), rockLift);
        albedo = lerp(albedo, float3(Lc, Lc, Lc) * float3(1.16, 0.98, 0.78) * (1.12 + 0.3 * faceW), rockLift * lerp(0.7, 0.9, faceW));
    } else {
        // Palette (round 2): warm Dune ochre - the sand a golden ochre, the rock a darker rust-brown.
        albedo = lerp(float3(L, L, L), albedo, lerp(0.42, 0.52, spice)) * float3(1.08, 0.98, 0.83);
        albedo = lerp(albedo, float3(Lr, Lr, Lr) * float3(1.2, 0.96, 0.76) * (1.05 + 0.35 * faceW), rockLift * lerp(0.6, 0.9, faceW));
    }
    albedo *= 1.0 + (vnoise(p / 300.0 + 11.0) - 0.5) * 0.14 * sandW;
    const float2 wind = t3Reim[1].xy;
    const float warp = vnoise(p / 41.0) * 16.0 + vnoise(p / 9.0 + 4.0) * 3.0;
    const float ph = (dot(p, wind) + warp) * (6.2831853 / 7.0);
    const float ripA = sandW * (1.0 - spice) * mid * t3Reim[0].z * smoothstep(0.3, 0.75, vnoise(p / 63.0 + 3.0)) *
                       (1.0 - saturate(g.g * 6.0));
    const bool photo = t3Photo[0].x > 0.5;
    if (!photo) {
        n += float3(wind.x, 0.0, wind.y) * (-0.28 * cos(ph) * ripA);
        albedo *= 1.0 + 0.03 * sin(ph) * ripA;
        const float4 pb = pebbles(p, 3.4, 0.5);
        const float grav = sandW * (1.0 - spice) * fine * t3Reim[0].w;
        n += float3(pb.y, 0.0, pb.z) * (pb.x * 0.9 * grav);
        albedo *= lerp(1.0, 0.8 + 0.4 * pb.w, smoothstep(0.0, 0.3, pb.x) * grav);
        albedo *= 1.0 + (vnoise(p * 1.7) - 0.5) * 0.12 * fine * sandW;
    }
    if (spice > 0.0) {
        albedo *= lerp(float3(1.0, 1.0, 1.0), lerp(float3(0.95, 0.78, 0.66), float3(0.8, 0.58, 0.48), rich), saturate(spice * 1.4));
        if (ng) {  // rust / cinnamon, not orange: darker, thick spice darkest
            const float Ls = dot(albedo, lumW);
            albedo = lerp(albedo, float3(Ls, Ls, Ls) * lerp(float3(1.42, 0.84, 0.52), float3(1.42, 0.78, 0.46), rich) * lerp(0.9, 0.72, rich),
                          0.85 * saturate(spice * 1.4));
        }
        const float mott = vnoise(p / 6.0 + 13.0) * 0.6 + lerp(0.5, vnoise(p / 2.2 + 5.0), mid) * 0.4;
        albedo *= lerp(1.0, 0.8 + 0.4 * mott, saturate(spice * 1.4));
        const float4 sg = pebbles(p + 41.0, 2.6, 0.25 + 0.7 * saturate(spice * (0.6 + 0.6 * rich)));
        const float on = smoothstep(0.0, 0.25, sg.x) * saturate(spice * 1.6) * mid;
        const float3 rust = (ng ? lerp(float3(0.62, 0.3, 0.15), float3(0.45, 0.18, 0.08), rich)
                                : lerp(float3(0.78, 0.38, 0.17), float3(0.58, 0.2, 0.08), rich)) * (0.8 + 0.4 * sg.w);
        albedo = lerp(albedo, rust, on);
        n += float3(sg.y, 0.0, sg.z) * (sg.x * 1.1 * on);
        ao *= 1.0 - 0.18 * saturate(spice * 1.6) * (1.0 - smoothstep(0.0, 0.2, sg.x)) * mid;
        const float2 cell = floor(p * 1.25);
        const float tw = hash12(cell + 11.1);  // fixed to the ground: a view-dependent twinkle flickered while scrolling (owner, 2026-11-18)
        const float sparkle = step(0.965, hash12(cell + 0.37)) * smoothstep(0.55, 1.0, tw);
        glow += spice * (t3Reim[0].y * (0.35 + 0.65 * vnoise(p / 9.0)) + sparkle * fine * 0.35);
    }
    const float edge = smoothstep(0.04, 0.3, gb.r) * (1.0 - rockW) * (1.0 - cw) * (1.0 - spice);
    if (edge > 0.001) {
        const float4 rb = pebbles(p + 17.0, 7.5, 0.25 + 0.55 * edge);
        const float l2 = dot(t3Level[2].rgb, lumW);
        const float3 rockC = lerp(float3(l2, l2, l2), t3Level[2].rgb, 0.5) * tanTint;
        const float on = smoothstep(0.0, 0.2, rb.x) * edge;
        albedo = lerp(albedo, rockC * (0.75 + 0.45 * rb.w), on);
        n += float3(rb.y, 0.0, rb.z) * (rb.x * 1.3 * edge);
        ao *= 1.0 - 0.25 * edge * (1.0 - smoothstep(0.0, 0.15, rb.x));
    }
    const float c1 = abs(vnoise(p / 11.0 + 3.0) - 0.5), c2 = abs(vnoise(p / 4.3 + 7.0) - 0.5);
    const float crack = ((1.0 - smoothstep(0.0, 0.016 + px * 0.006, c1)) * smoothstep(0.55, 0.75, vnoise(p / 37.0 + 1.0)) * 0.8 +
                         0.5 * (1.0 - smoothstep(0.0, 0.02, c2)) * smoothstep(0.45, 0.7, vnoise(p / 13.0 + 5.0)) * fine) * rockOnly * mid;
    if (!photo) {
        albedo *= 1.0 - (ng ? 0.27 : 0.45) * saturate(crack);  // (Realistic grade: the cracks mostly in the cavity light)
        ao *= 1.0 - 0.3 * saturate(crack);
    }
    const float face = photo ? 0.0 : faceW * rockOnly * t3Reim[1].z;
    if (face > 0.001) {
        const float st = wpos.y / 12.0 + vnoise(p / 25.0) * 1.1 + vnoise(p / 6.0) * 0.25;
        const float band = frac(st), id = floor(st);
        const float ledge = 1.0 - smoothstep(0.0, 0.14, band);
        albedo *= lerp(1.0, (0.9 + 0.2 * hash12(float2(id, 3.7))) * (1.0 - 0.18 * ledge), face);
        n.y += ledge * face * 0.5;
    }
    ao *= 1.0 - t3Reim[1].w * saturate((gb.g - g.g) * 3.0);
    if (photo) {
        // Photo terrain: the scans' detail over the palette's colour (MSL twin).
        const float kk = vnoise(p / 97.0 + 5.0);
        const float patch = smoothstep(0.5, 0.72, vnoise(p / 120.0 + 7.0) * 0.65 + vnoise(p / 29.0 + 2.0) * 0.35);
        // (The stony scan shows by its own height: its tallest stones strewn over the fine sand everywhere.)
        PhS tg;
        tg.d = float3(1.0, 1.0, 1.0);
        tg.n = float2(0.0, 0.0);
        tg.orh = float3(1.0, 1.0, 1.0);
        float wGrav = 0.0;
        if (sandW >= 0.02) {
            const float gsc = 1.0 / t3Photo[4].a;
            tg = photoTap(p * gsc, 2u, t3Photo[4].rgb, gx * gsc, gy * gsc, kk);
            tg.d = max(1.0 + (tg.d - 1.0) * 1.15, float3(0.62, 0.62, 0.62));  // (its darkest debris lifted)
            const float cover = lerp(lerp(0.27, 0.92, saturate(max(patch, edge * 1.5))), 1.0, saturate(spice * 1.5));
            wGrav = sandW * smoothstep(-0.12, 0.12, tg.orh.b - (1.0 - cover));
        }
        const float wFine = sandW - wGrav;
        const float wRough = rockOnly * saturate(max(crag, faceW));
        float wl[7];
        wl[0] = wFine * (1.0 - dunes);
        wl[1] = wFine * dunes;
        wl[2] = wGrav;
        wl[3] = rockOnly - wRough;
        wl[4] = wRough;
        wl[5] = 0.0;
        wl[6] = cw;
        float3 d = float3(0.0, 0.0, 0.0), orh = float3(0.0, 0.0, 0.0);
        float2 nn = float2(0.0, 0.0);
        float sum = 0.0;
        for (int li = 0; li < 7; ++li) {
            if (wl[li] >= 0.02 && li == 2) {
                photoAdd(tg, wl[li], 1.0, d, nn, orh, sum);
            } else if (wl[li] >= 0.02) {
                const float sc = 1.0 / t3Photo[2 + li].a;
                PhS t = photoTap(p * sc, uint(li), t3Photo[2 + li].rgb, gx * sc, gy * sc, kk);
                t.d = 1.0 + (t.d - 1.0) * (li == 0 ? 1.7 : li == 1 ? 1.4 : li <= 4 ? 1.15 : 0.8);
                if (li == 6) t.d = max(t.d, float3(0.72, 0.72, 0.72));  // (the slabs' own stains are drawn by concreteSlab3)
                photoAdd(t, wl[li], li == 1 ? 1.25 : li >= 3 && li <= 4 ? 1.25 : li == 6 ? 0.6 : 1.0, d, nn, orh, sum);
            }
        }
        if (sum > 0.0) {
            d /= sum;
            nn /= sum;
            orh /= sum;
            const float dl = dot(d, lumW);
            const float3 dc = lerp(float3(dl, dl, dl), d, 0.55 * (1.0 - saturate(spice * 1.5)));
            albedo *= lerp(float3(1.0, 1.0, 1.0), dc, t3Photo[0].y) * (1.0 + float3(0.2, 0.2, 0.18) * rockOnly + (0.05 + 0.05 * dunes) * sandW + 0.22 * cw);
            albedo *= lerp(float3(1.0, 1.0, 1.0), lerp(float3(1.07, 0.99, 0.9), float3(0.94, 1.0, 1.05), vnoise(p / 230.0 + 17.0)) *
                                                  (0.93 + 0.14 * vnoise(p / 95.0 + 41.0)), rockOnly);
            n += float3(nn.x, 0.0, -nn.y) * t3Photo[0].z;
            ao *= lerp(1.0, orh.r, 0.75);
            gloss = (1.0 - orh.g) * (1.0 - spice);
        }
    }
}

// Reimagined spice relief, fine part (MSL twin spiceRelH; docs/3d-mode.md "Spice fields in 3D").
float spiceRelH(float2 p, float2 wind) {
    const float warp = vnoise(p / 23.0 + 7.0) * 9.0 + vnoise(p / 6.0 + 2.0) * 2.2;
    const float t = frac((dot(p, wind) + warp) / 10.0);
    const float r = t < 0.78 ? smoothstep(0.0, 0.78, t) : 1.0 - smoothstep(0.78, 1.0, t);
    const float m = vnoise(p / 3.6 + 13.0);
    return r * (0.35 + 0.65 * vnoise(p / 29.0 + 5.0)) + 1.1 * m * m;
}

float4 terrain3_fs(TVOut i) : SV_Target0 {
    const float2 p = i.wpos.xz;
    const float2 mapSize = t2Fog2.yz;
    // Shroud / the map's edge (fog2.w: 0 no shroud, 1 shroud, 2 baked: no shroud, no edge fade).
    // (The MSL twin leaves early where a whole SIMD group is black; Vulkan 1.0 / SM 6.0 here keep
    // the full path - the same picture.)
    // The world beyond the map (docs/3d-mode.md): sk = its margin (px), the mode (2 = this draw is the land
    // beyond: the map's textures mirrored across the edge by their samplers, blended into the far texture's
    // procedural desert by its weight wF). fog2.w 3 = shroud without the edge fade (2: neither).
    float4 sk = float4(t3Reim[2].yw, 0.5, 0.0);
    const bool ring = sk.y > 1.5;
    // (The procedural rock's tone and ruggedness: mottled like the map's own broken rock, in patches.)
    if (ring) sk.zw = float2(0.5 + (vnoise(i.wpos.xz / 15.0 + 3.0) * 0.6 + vnoise(i.wpos.xz / 4.5 + 9.0) * 0.4 - 0.5) * 0.55,
                             smoothstep(0.38, 0.7, vnoise(i.wpos.xz / 70.0 + 17.0)) * 0.75);
    const float2 outXZ = max(max(-p, p - mapSize), float2(0.0, 0.0));
    const float dOut = length(outXZ);
    float4 fr = float4(0.0, 0.0, 0.0, 0.0);
    if (ring) fr = T3Far.SampleLevel(T3FarSamp, (p + sk.x + 4.0) / (mapSize + 2.0 * sk.x + 8.0), 0.0);
    const float wF = fr.b;
    float s2 = 0.0;
    if (t2Fog2.w > 0.5 && t2Fog2.w < 1.5) s2 = T2Shroud.Sample(T2ShroudSamp, p / mapSize).a;
    if (t2Fog2.w > 2.5) SHROUD_AT(T2Shroud, T2ShroudSamp, p, mapSize, s2);  // (beyond the map: the fixed fog level)
    const float2 ed = min(p, mapSize - p);
    if (t2Fog2.w < 1.5) s2 = max(s2, 1.0 - smoothstep(0.0, 8.0, min(ed.x, ed.y)));
    // Per cell (DesignedTerrain::buildSpiceTex): b > 0 = spice within reach of spiceField, a > 0 =
    // relief within reach of the slope / rim taps. Without them those reads are all zeros: skipped.
    const float4 near = T2Spice.Sample(T2SpiceSamp, p / mapSize);
    const float3 dpx = ddx(i.wpos), dpy = ddy(i.wpos);
    const float3 gn = normalize(i.nrm);
    const bool bake = t3Guide.z > 0.5;

    float4 a = T2SplatA.Sample(T2SplatSamp, p / mapSize);
    float4 b = T2SplatB.Sample(T2SplatSamp2, p / mapSize);
    if (ring) {  // beyond the map: dunes and the far texture's cliff mask further out; no concrete
        a = lerp(a, float4(0.0, 1.0, 0.0, fr.a), wF);
        b.y = 0.0;
    }
    const float cfw = max(fwidth(b.y), 1e-3);
    const float cw = smoothstep(0.5 - cfw, 0.5 + cfw, b.y);
    const float pxWorld = max(length(dpx.xz), length(dpy.xz));
    // The cells no vehicle can use (splat A.a: binary per cell): the only place for the raised
    // cliff look. Rock vehicles can use is always drawn low and light.
    const float cliffM = smoothstep(0.3, 0.7, a.w);

    // ---- the guide: rock outline, relief, tone
    const float lod = max(0.0, log2(pxWorld * t3Guide.y));
    const float tpp = t3Guide.y;
    // A slight warp (under half a guide texel, at about 3 texels) bends the original's pixel
    // diagonals into organic curves; every read below goes through it.
    const float2 pg = p + (float2(vnoise(p * (tpp / 2.9) + 31.0), vnoise(p * (tpp / 2.9) + 57.0)) - 0.5) * (0.9 / tpp);
    const float2 guv = pg / mapSize;
    const float4 g = guideX(pg, mapSize, tpp, lod, sk);
    const float e = 1.5;
    const bool relief = near.a > 0.0 || lod >= 2.0;
    float hE = g.g, hW = g.g, hS = g.g, hN = g.g;
    if (relief) {
        hE = guideX(pg + float2(e, 0.0), mapSize, tpp, lod, sk).g;
        hW = guideX(pg - float2(e, 0.0), mapSize, tpp, lod, sk).g;
        hS = guideX(pg + float2(0.0, e), mapSize, tpp, lod, sk).g;
        hN = guideX(pg - float2(0.0, e), mapSize, tpp, lod, sk).g;
    }
    const float2 mg = float2(macroH(hE) - macroH(hW), macroH(hS) - macroH(hN)) * (t3Guide.x / (2.0 * e));
    const float ml = length(mg);
    const float steep = smoothstep(0.45, 1.05, ml);
    const float tone = g.b * 2.0 - 1.0 + (vnoise(p / 1.3 + 3.0) - 0.5) * t3Level[4].a;
    const float aaT = max(fwidth(tone) * 0.7, 0.012);
    // ---- cliff faces, as the original tiles show their relief (tone units). A face = where the
    // relief climbs (only across the dark faces the original paints on cells no vehicle can
    // use): darker than the top by the side it looks to, the original's big light and dark, tiers
    // of blocky columns with gullies and ledge lines, a dark foot; a pale rim at its top edge.
    const float2 dir = ml > 1e-4 ? mg / ml : float2(0.0, 0.0);
    const float slopeW = smoothstep(0.25, 0.9, ml);
    const float2 sunXZ = normalize(t2SunDir.xz);
    const float facing = dot(-dir, sunXZ);
    float4 gb = T3Guide.SampleLevel(T3GuideSamp, guv, lod + 3.0);  // the surroundings (blurred)
    if (ring) gb = lerp(gb, float4(fr.r, fr.g, 0.5, sk.w), wF);
    const float hF = saturate((g.g - 0.1333) / 0.8667);                   // 0 at a face's foot .. 1 at the top
    const float hL = hF * 2.6 + (vnoise(p / 13.0 + 21.0) - 0.5) * 0.8;
    const float tier = floor(hL), tf = hL - tier;
    const bool ns = dir.y * dir.y + (vnoise(p / 17.0 + 3.0) - 0.5) * 0.5 > 0.5;
    const float uc = (ns ? p.x : p.y) + (vnoise(p / 11.0 + 7.0) - 0.5) * 6.0 + tier * 37.7;
    float2 c1 = column(uc, 8.5, 3.0 + tier), c2 = column(uc + 2.3, 3.4, 71.0 + tier);
    if (ns ? sunXZ.x > 0.0 : sunXZ.y > 0.0) {
        c1.x = 1.0 - c1.x;
        c2.x = 1.0 - c2.x;
    }
    const float gully = lerp(0.15, -0.32, smoothstep(0.42, 0.78, c1.x)) * (0.45 + 1.1 * c1.y) +
                        lerp(0.06, -0.16, smoothstep(0.45, 0.8, c2.x)) * (0.4 + c2.y) + (c1.y - 0.5) * 0.16;
    const float ledge = (1.0 - smoothstep(0.0, 0.16, tf)) * step(0.5, tier) * smoothstep(0.1, 0.3, hF);
    const float tierLight = 0.05 * (tier - 1.0) + 0.07 * smoothstep(0.75, 1.0, tf);
    const float foot = 1.0 - smoothstep(0.03, 0.16, hF);
    // The rim: the height still climbs from below (2 px down the slope) but no longer above.
    float gUp = g.g, gDn = g.g;
    if (relief) {
        gUp = guideX(pg + dir * 2.0, mapSize, tpp, lod, sk).g;
        gDn = guideX(pg - dir * 2.0, mapSize, tpp, lod, sk).g;
    }
    const float climbed = g.g - gDn, climbing = gUp - g.g;
    const float rim = smoothstep(0.35, 0.6, (climbed - climbing) / (climbed + abs(climbing) + 0.004)) *
                      smoothstep(0.012, 0.03, climbed) * smoothstep(0.3, 0.5, hF) *
                      smoothstep(0.3, 0.5, vnoise(p / 5.5 + 19.0)) * saturate(0.25 + 0.75 * facing);
    const float toneDir = t3Shade.x + t3Shade.y * facing;
    const float toneBig = clamp(gb.b * 2.0 - 1.0, -0.35, 0.35);  // the original's big light and dark
    const float toneC = toneDir + 0.5 * toneBig + t3Face.x * gully * (0.5 + 0.5 * saturate(facing + 0.6)) -
                        t3Face.y * ledge - t3Face.z * foot + tierLight + (g.b - gb.b) * 2.0 * t3Shade.w;
    const float aaC = max(fwidth(toneC) * 0.7, 0.012);
    const float toneMacro = slopeW * (-0.2 + 0.15 * facing) * lerp(0.3, 1.0, cliffM);  // plain rock on a slope: the side only
    const float toneP = lerp(max(tone, -0.18), tone, smoothstep(0.1, 0.9, a.w));  // passable broken rock: dark specks held back

    const float rockW = saturate((g.r - 0.5) * 2.6 + 0.5 + (vnoise(p / 1.6) - 0.5) * 0.25);
    const float sandAll = a.x + a.y;
    const float dunes = sandAll > 1e-3 ? a.y / sandAll : 0.0;

    // Relief normal: the height function at three points.
    const float d = clamp(pxWorld, 0.35, 1.5);
    float3 rn = float3(0.0, 1.0, 0.0);
    if (g.r > 0.02 || g.g > 0.004) {
        const float4 g1 = guideX(pg + float2(d, 0.0), mapSize, tpp, lod, sk);
        const float4 g2 = guideX(pg + float2(0.0, d), mapSize, tpp, lod, sk);
        // Baked from above the macro shape is shaded by the cliff shading above, not by this normal.
        const float h0 = faithHeight(g, !bake);
        float2 grad = float2(faithHeight(g1, !bake) - h0, faithHeight(g2, !bake) - h0) / d;
        // (Photo terrain: the guide's 8-bit height steps would show as contour lines on the calm rock; MSL twin.)
        if (t3Reim[0].x > 0.5 && t3Photo[0].x > 0.5) grad *= smoothstep(0.2, 0.6, length(grad));
        rn = normalize(float3(-grad.x, 1.0, -grad.y));
        if (ring) rn = normalize(lerp(rn, gn, wF));  // (the far texture is coarse: the mesh's own slope there)
    }
    // (Baked: sand on the ramp the mesh makes of a steep face is lit as level ground.)
    float3 gnL = gn;
    if (bake) gnL = normalize(lerp(float3(0.0, 1.0, 0.0), gn, smoothstep(0.8, 0.96, gn.y)));
    const float3 base = normalize(lerp(gnL, rn, rockW));

    // ---- material weights: crag rock (layer 7) on cliffs, rough rock on level broken rock
    const float faceW = steep * saturate(g.g * 4.0);
    const float crag = saturate(g.a * 1.4);
    const float cliffW = faceW * cliffM;
    float w[7];
    w[0] = (1.0 - rockW) * (1.0 - dunes);
    w[1] = (1.0 - rockW) * dunes;
    w[3] = rockW * cliffW;
    w[4] = rockW * (1.0 - cliffW) * crag;
    w[2] = max(rockW - w[3] - w[4], 0.0);
    w[5] = 0.0;
    w[6] = 0.0;
    float2 sf = float2(0.0, 0.0);
    float spice = 0.0;  // (no spice field within reach: no spice)
    // (Beyond the map: a little spice at the very edge at most.)
    const float spiceOut = 1.0 - smoothstep(6.0, 36.0, dOut);
    if (near.b > 0.0 && spiceOut > 0.0) {
        sf = spiceField(p, mapSize) * spiceOut;
        const float amount = (sf.x + sf.y) * (2.0 / 7.0);
        const float grains = vnoise(p / 5.0) * 0.6 + vnoise(p / 15.0) * 0.4;
        // The edge of a field dissolves: the presence against a noise threshold (lobes and specks).
        const float edgeN = vnoise(p / 11.0 + 4.0) * 0.5 + vnoise(p / 4.0 + 8.0) * 0.3 + vnoise(p / 1.7) * 0.2;
        const float cover = smoothstep(-0.12, 0.2, sf.x - (0.15 + 0.6 * edgeN));
        spice = lerp(cover * 0.78, 1.0, sf.y) * smoothstep(1.0, 0.55, grains - amount * 0.3) * 0.92;
        spice *= saturate((1.0 - rockW) * 1.6);
        if (t3Reim[0].x > 0.5) {
            // Reimagined: strays just beyond the field's edge (MSL twin).
            const float halo = smoothstep(0.03, 0.25, sf.x) * (1.0 - cover) * smoothstep(0.35, 0.7, vnoise(p / 13.0 + 3.0));
            if (halo > 0.0) spice = max(spice, halo * spots(p + 7.3, 7.0, 0.9) * 0.8 * saturate((1.0 - rockW) * 1.6));
        }
    }
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    // (Photo terrain: the old scans give the palette and the height blend only; MSL twin.)
    const bool calm = t3Reim[0].x > 0.5 && t3Photo[0].x > 0.5;
    const bool reimFace = t3Reim[0].x > 0.5 && t3Reim[3].z > 0.5 && gn.y < 0.42 && cw < 0.004;
    const bool steepGeo = !bake && gn.y < 0.82 && !reimFace;  // (baked from above: the mesh only casts shadows)
    const float3 tw = pow(abs(gn), float3(4.0, 4.0, 4.0));
    const float3 tri = tw / (tw.x + tw.y + tw.z);
    float4 c[7];
    float3 nn[7];
    float hmax = -1.0;
    [unroll] for (int m = 0; m < 7; ++m) {
        c[m] = float4(0.0, 0.0, 0.0, 0.0);
        nn[m] = float3(0.0, 0.0, 0.0);
        if (w[m] >= 0.004) {
            // Slot 3 (cliff) is the crag rock of layer 7 here, drawn out north-south.
            const bool isCrag = m == 3;
            const uint layer = isCrag ? 7u : uint(m);
            const float2 sc = isCrag ? float2(1.0, 0.62) / t3CragTex.w : float2(1.0, 1.0) / t2Tint[m].a;
            const float ns = isCrag ? t3CragTex.z : t2Lum[m].a;
            MatS sm = sampleTiled(p * sc, layer, dpx.xz * sc, dpy.xz * sc, k);
            float3 n = base + float3(sm.n.x, 0.0, -sm.n.y) * ns;
            if (steepGeo && (m == 2 || m == 3 || m == 4)) {
                const MatS mx = sampleTiled(float2(i.wpos.z, -i.wpos.y) * sc.x, layer, float2(dpx.z, -dpx.y) * sc.x,
                                            float2(dpy.z, -dpy.y) * sc.x, k);
                const MatS mz = sampleTiled(float2(i.wpos.x, -i.wpos.y) * sc.x, layer, float2(dpx.x, -dpx.y) * sc.x,
                                            float2(dpy.x, -dpy.y) * sc.x, k);
                sm.c = sm.c * tri.y + mx.c * tri.x + mz.c * tri.z;
                n = base + (float3(sm.n.x, 0.0, -sm.n.y) * tri.y + float3(0.0, mx.n.y, mx.n.x) * tri.x +
                            float3(mz.n.x, mz.n.y, 0.0) * tri.z) * ns;
            }
            if (calm) {  // (the scan's mean colour: its smallest mip)
                sm.c.rgb = lerp(T2Alb.SampleLevel(T2MatSamp, float3(0.5, 0.5, float(layer)), 16.0).rgb, sm.c.rgb, 0.3);
                n = lerp(base, n, 0.25);
            }
            c[m] = sm.c;
            nn[m] = n;
            hmax = max(hmax, sm.c.a + w[m]);
        }
    }
    // Height blend. The broken materials (crag, rough) give only their light and dark (det):
    // their colour is the original's, by tone.
    const float depth = t2Params.y;
    float3 albedo = float3(0.0, 0.0, 0.0), n = float3(0.0, 0.0, 0.0);
    float sum = 0.0, detC = 0.0, detR = 0.0, brokenSum = 0.0;
    [unroll] for (int j = 0; j < 7; ++j) {
        if (w[j] >= 0.004) {
            const float bw = max(c[j].a + w[j] - hmax + depth, 0.0);
            const float L = dot(c[j].rgb, float3(0.299, 0.587, 0.114));
            if (j == 3) {
                detC += lerp(1.0, pow(L * t3CragTex.x, t3CragTex.y), 0.45) * bw;
                brokenSum += bw;
            } else if (j == 4) {
                detR += pow(L * t3RoughTex.x, t3RoughTex.y) * bw;
                brokenSum += bw;
            } else {
                albedo += lerp(c[j].rgb * t2Tint[j].rgb, L * t2Lum[j].rgb, t2Params.w) * (j == 2 ? 1.0 + toneMacro : 1.0) * bw;
            }
            n += nn[j] * bw;
            sum += bw;
        }
    }
    // Crags: the original's palette by the relief's tone; level broken rock: its own tone.
    albedo = (albedo + toneRamp(toneC, aaC) * detC + toneRamp(toneP + toneMacro * 0.6, aaT) * detR) / max(sum, 1e-4);
    n = normalize(sum > 0.0 ? n : base);
    float reliefW = brokenSum / max(sum, 1e-4);
    // The rim: a pale line on whatever rock lies at the top edge of a face; a soft contact
    // shadow on the ground around the foot of raised rock.
    albedo = lerp(albedo, t3Level[4].rgb * (0.92 + 0.16 * vnoise(p / 1.7 + 5.0)), rim * t3Shade.z * rockW * cliffM);
    albedo *= 1.0 - t3Face.w * saturate((gb.g - g.g) * 5.0) * (1.0 - rockW);
    // Large-scale variation of the open ground (not of the broken rock nor of the concrete).
    const float macro = vnoise(p / 700.0) * 0.6 + vnoise(p / 230.0 + 5.0) * 0.4;
    albedo *= 1.0 + t2Params.z * (macro - 0.5) * (1.0 - reliefW);

    if (cw > 0.004) {  // concrete slabs over the rest
        const float sc = 1.0 / t2Tint[6].a;
        const MatS sm = sampleTiled(p * sc, 6u, dpx.xz * sc, dpy.xz * sc, k);
        float3 cn = base + float3(sm.n.x, 0.0, -sm.n.y) * t2Lum[6].a;
        const float grainRel = dot(sm.c.rgb, float3(0.299, 0.587, 0.114)) * t2Lum[6].r / max(t3Slab.r, 0.01);
        const float3 cc = concreteSlab3(p, grainRel, cn, t3Level[4].rgb, pxWorld);
        const float t = sum > 0.0 ? cw : 1.0;
        albedo = lerp(albedo, cc, t);
        n = normalize(lerp(n, normalize(cn), t));
        reliefW *= 1.0 - t;
    }
    float cwR = cw;  // the concrete left bare by the sand (Realistic grade)
    if (t3Reim[0].x > 0.5 && t3Reim[3].w > 0.5 && b.a > 0.03 && !ring) {
        // Realistic grade: sand lies over the slabs' edge, ragged, a thin veil further in, a little drifted sand
        // on the ground along the slabs (MSL twin).
        const float nl = vnoise(p / 17.0 + 23.0), nm = vnoise(p / 5.3 + 41.0), nf = vnoise(p / 2.1 + 7.0);
        const float din = (b.a - 0.5) * 24.0;
        const float reach = 0.6 + 13.0 * smoothstep(0.38, 0.8, nl) + 3.0 * nm * nm;
        const float over = 1.0 - smoothstep(reach * 0.25, reach, din + (nf - 0.5) * 0.7 * reach);
        const float veil = (1.0 - smoothstep(2.0, 13.0, din)) * smoothstep(0.4, 0.7, nm * 0.6 + nf * 0.4) * 0.3;
        const float apron = smoothstep(-8.0, -0.5, din) * (0.2 + 0.5 * nl) * (0.6 + 0.4 * nf);
        const float sd = lerp(apron, max(over, veil), cw);
        albedo = lerp(albedo, t3Level[4].rgb * (0.9 + 0.14 * nm), sd);
        cwR = cw * (1.0 - max(over, veil));
        n = normalize(lerp(n, base, sd * 0.8));
    }
    if (spice > 0.0) {
        const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
        albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
        albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick
    }

    float ao = 1.0, glow = 0.0, gloss = 0.0;
    const bool reim = t3Reim[0].x > 0.5;
    if (reim) {
        reimagine(p, i.wpos, rockW, faceW, spice, sf.y, cwR, pxWorld, g, gb, t2Fog.xyz, albedo, n, ao, glow, dpx.xz, dpy.xz,
                  dunes, crag, gloss);
        // The spice's fine relief: its normal and its self-shadow towards the low sun (MSL twin).
        const float spA = spice * (0.55 + 0.45 * sf.y) * (1.0 - smoothstep(1.2, 3.5, pxWorld)) * (1.0 - cwR);
        if (spA > 0.01) {
            const float2 wind = t3Reim[1].xy;
            const float h0 = spiceRelH(p, wind);
            const float hx = spiceRelH(p + float2(0.8, 0.0), wind), hz = spiceRelH(p + float2(0.0, 0.8), wind);
            n = normalize(n) - float3(hx - h0, 0.0, hz - h0) * (spA / 0.8);
            const float2 sd = normalize(t2SunDir.xz);
            const float tanE = t2SunDir.y / max(length(t2SunDir.xz), 1e-3);
            const float o1 = (spiceRelH(p + sd * 1.6, wind) - h0) * spA - 1.6 * tanE;
            const float o2 = (spiceRelH(p + sd * 3.6, wind) - h0) * spA - 3.6 * tanE;
            ao *= 1.0 - 0.55 * saturate(max(o1, o2) * 2.5) * saturate(spice * 1.5);
        }
        n = normalize(n);
        // Round 2: the mesh's steep faces in world-space sandstone, lit by their own relief.
        const float cliffGeo = (1.0 - smoothstep(0.42, 0.78, gn.y)) * (1.0 - cw) * t3Reim[3].z;
        reimCliff(i.wpos, gn, pxWorld, cliffGeo, t3Reim[3].w > 0.5, albedo, n, ao, dpx, dpy);
        reliefW = lerp(reliefW, 1.0, cliffGeo);
    }

    // Light: level ground under detailSun + the sun shadow map; broken rock under sunDir (baked
    // from above without cast shadow: the original's shading of the rock is in its tone levels).
    // (Baked: sand under the ramp the mesh makes of a steep lee face is looked up at ground level.)
    float3 sp = i.wpos;
    if (bake && rockW < 0.5 && gn.y < 0.85 && dot(gn.xz, t2SunDir.xz) < 0.0) sp.y = 0.0;
    float sv = i.sun;
    if (t2Shadow.x > 0.5) {
        const float3 lp = mul(t2LightVP, float4(sp, 1.0)).xyz;
        if (reim && t3Reim[3].y > 0.0) sv = shadowSoft(lp, i.pos.xy);
        else SHADOW_PCF(T2ShadowMap, T2ShadowSamp, lp, t2Shadow, sv);
    }
    const float visAll = lerp(t2Light.w, 1.0, sv);
    const float vis = bake ? lerp(visAll, 1.0, slopeW * 0.7 * rockW) : visAll;
    // Realistic grade: ground occlusion - the sky light cut round the foot of buildings and walls (splat B.b; MSL twin).
    const float aoG = reim && t3Reim[3].w > 0.5 && !ring ? 1.0 - 0.62 * smoothstep(0.04, 0.72, b.b) : 1.0;
    const float aoS = 1.0 - 0.2 * (1.0 - aoG);  // (a little of the sun too: a contact shadow)
    // Ambient occlusion (docs/3d-mode.md): the screen-space term on the sky light only - the darker of it and the
    // buildings' ground occlusion (the same crease: not both).
    float sao;
    SSAO_AT(T3Ssao, T3SsaoSamp, i.pos.xy, sao);
    const float aoA = min(aoG, sao);
    const float3 shadeFlat = sunAmb(t2Light.x * aoA, t2Light.y * max(dot(n, t3DetailSun.xyz), 0.0) * vis * aoS) * t3DetailSun.w;
    const float3 shadeRelief = sunAmb(t3ReliefLight.x * aoA, t3ReliefLight.y * max(dot(n, t2SunDir.xyz), 0.0) * vis * aoS) * t3ReliefLight.z;
    const float3 shade = lerp(shadeFlat, shadeRelief, reliefW);
    float3 rgb = satur(albedo) * shade + albedo * pointLights13(i.wpos, n, 0.3);
    if (reim) {
        // (Round 2: faces turned away from the sun take the sky's blue too.)
        const float lit = sv * saturate(dot(n, t2SunDir.xyz) * 4.0 + 0.15);
        rgb *= lerp(float3(1.0 - 0.13 * t3Reim[2].z, 1.0 - 0.07 * t3Reim[2].z, 1.0 + 0.06 * t3Reim[2].z), float3(1.0, 1.0, 1.0), lit) * ao;
        rgb += float3(1.0, 0.42, 0.1) * glow;
        if (gloss > 0.0) {  // Photo terrain: a faint sheen of the low sun on the smoother surfaces (the scans' roughness)
            const float3 hv = normalize(t2SunDir.xyz + normalize(t2Fog.xyz - i.wpos));
            rgb += plSunColor.rgb * (pow(max(dot(n, hv), 0.0), 18.0) * gloss * gloss * t3Photo[0].w * sv * visAll);
        }
    }
    rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils (0 strength = off)
    if (sk.y > 0.5) {
        // The world beyond the map: the land out there a little darker and greyer (clearly not the
        // battlefield), and a faint pale line along the map's edge.
        const float outW = smoothstep(0.0, 20.0, dOut);
        const float grey = dot(rgb, float3(0.299, 0.587, 0.114));
        const float2 bt = plEdge.x > 0.5 ? float2(0.3, 0.12) : float2(0.22, 0.15);  // (with the fixed fog: MSL twin)
        rgb = lerp(rgb, float3(grey, grey, grey), bt.x * outW) * (1.0 - bt.y * outW);
        const float dl = ring ? dOut : max(min(ed.x, ed.y), 0.0);
        const float lw = max(2.2, pxWorld * 1.3);
        rgb += float3(1.0, 0.88, 0.66) * (0.055 * exp(-dl * dl / (lw * lw)) * min(1.0, 2.2 / lw * 1.6));
    }
    const float dist = distance(i.wpos, t2Fog.xyz);
    float f = smoothstep(t2Fog.w, t2Fog2.x, dist) * aerialK();
    if (sk.y > 0.5) f = max(f, edgeHazeK(p, mapSize));  // (beyond the map: into the haze with distance from it)
    rgb = lerp(rgb, t2FogColor.rgb, f) * (1.0 - s2);
    return float4(rgb, 1.0);
}

struct RVIn {
    float3 pos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 i0 : TEXCOORD3;  // instance: position xyz, yaw (radians)
    float4 i1 : TEXCOORD4;  // scale xyz, texture layer
    float4 i2 : TEXCOORD5;  // sun visibility, ground y, top y, brightness
};
struct RVOut {
    float4 pos : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float layer : TEXCOORD3;
    float sun : TEXCOORD4;
    float ao : TEXCOORD5;
    float bright : TEXCOORD6;
};
cbuffer RockVU : register(b0, space1) {
    float4x4 rViewProj;
};
RVOut rock_vs(RVIn i) {
    const float c = cos(i.i0.w), s = sin(i.i0.w);
    const float3 q = i.pos * i.i1.xyz;
    const float3 w = float3(c * q.x - s * q.z, q.y, s * q.x + c * q.z) + i.i0.xyz;
    const float3 m = i.nrm / i.i1.xyz;
    RVOut o;
    o.pos = mul(rViewProj, float4(w, 1.0));
    o.wpos = w;
    o.nrm = float3(c * m.x - s * m.z, m.y, s * m.x + c * m.z);
    o.uv = i.uv;
    o.layer = i.i1.w;
    o.sun = i.i2.x;
    o.ao = saturate((w.y - i.i2.y) / max(i.i2.z - i.i2.y, 1.0));
    o.bright = i.i2.w;
    return o;
}

Texture2DArray<float4> RockTex : register(t0, space2);
Texture2D<float4> RockShroud : register(t1, space2);
SamplerState RockSamp : register(s0, space2);
SamplerState RockShroudSamp : register(s1, space2);
Texture2D<float> RockShadowMap : register(t2, space2);
SamplerComparisonState RockShadowSamp : register(s2, space2);
Texture2D<float4> RockSsao : register(t3, space2);  // the screen-space ambient occlusion (render3d/ssao.*)
SamplerState RockSsaoSamp : register(s3, space2);
cbuffer RockFU : register(b0, space3) {
    float4 rSunDir;
    float4 rLight;
    float4 rFogColor;
    float4 rFog;
    float4 rFog2;
    float4 rTint[8];
    float4x4 rLightVP;  // sun shadow map
    float4 rShadow;     // on, texel (uv), -, depth bias
};
float4 rock_fs(RVOut i) : SV_Target0 {
    const uint layer = uint(i.layer + 0.5);
    const float3 t = RockTex.Sample(RockSamp, float3(i.uv, float(layer))).rgb;
    const float L = dot(t, float3(0.299, 0.587, 0.114));
    const float Lc = lerp(rTint[layer].a, L, 0.8);
    const float3 albedo = rTint[layer].rgb * Lc * lerp(float3(1.0, 1.0, 1.0), t / max(L, 0.02), 0.1) * i.bright;
    const float3 n = normalize(i.nrm);
    const float ndl = max(dot(n, rSunDir.xyz), 0.0);
    float sm = i.sun;
    if (rShadow.x > 0.5) SHADOW_PCF(RockShadowMap, RockShadowSamp, mul(rLightVP, float4(i.wpos, 1.0)).xyz, rShadow, sm);
    const float vis = lerp(rLight.w, 1.0, sm);
    float sao;  // (ambient occlusion: the darker of the rock's own and the screen-space term)
    SSAO_AT(RockSsao, RockSsaoSamp, i.pos.xy, sao);
    const float amb = rLight.x * (0.78 + 0.22 * n.y) * min(lerp(0.62, 1.0, smoothstep(0.0, 0.55, i.ao)), sao);
    float3 rgb = satur(albedo) * sunAmb(amb, rLight.y * ndl * vis) * rLight.z + albedo * pointLights4(i.wpos, n, 0.25);
    const float2 p = i.wpos.xz, mapSize = rFog2.yz;
    float s = 0.0;
    if (rFog2.w > 0.5 && rFog2.w < 1.5) s = RockShroud.Sample(RockShroudSamp, p / mapSize).a;
    if (rFog2.w > 2.5) SHROUD_AT(RockShroud, RockShroudSamp, p, mapSize, s);  // (3: no edge fade; beyond the map the fixed fog)
    const float2 e = min(p, mapSize - p);
    if (rFog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    if (s > 0.996) discard;
    rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils
    const float f = smoothstep(rFog.w, rFog2.x, distance(i.wpos, rFog.xyz)) * aerialK();
    rgb = lerp(rgb, rFogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}

// ================================================================================== ground clutter
// Reimagined ground clutter (render3d/ground_clutter.*; MSL twin render3d/clutter_msl.hpp): instanced pieces
// sheared onto the ground's slope; clutter_vs is also the vertex shader of their shadow map pass.
struct CVIn {
    float3 pos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 i0 : TEXCOORD3;  // instance: position xyz (ground under the centre), yaw (radians)
    float4 i1 : TEXCOORD4;  // scale xyz (world px), texture layer
    float4 i2 : TEXCOORD5;  // sun visibility (baked), height above the ground (px), dust, brightness
    float4 i3 : TEXCOORD6;  // ground slope dh/dx, dh/dz (times the class's conform share), hue shift, -
};
struct CVOut {
    float4 pos : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float layer : TEXCOORD3;
    float sun : TEXCOORD4;
    float ao : TEXCOORD5;
    float bright : TEXCOORD6;
    float dust : TEXCOORD7;
    float hue : TEXCOORD8;
};
cbuffer ClutterVU : register(b0, space1) {
    float4x4 cViewProj;
};
CVOut clutter_vs(CVIn i) {
    const float c = cos(i.i0.w), s = sin(i.i0.w);
    const float3 q = i.pos * i.i1.xyz;
    const float2 r = float2(c * q.x - s * q.z, s * q.x + c * q.z);
    const float3 w = float3(r.x, q.y + i.i3.x * r.x + i.i3.y * r.y, r.y) + i.i0.xyz;
    const float3 m = i.nrm / i.i1.xyz;
    const float3 nr = float3(c * m.x - s * m.z, m.y, s * m.x + c * m.z);
    CVOut o;
    o.pos = mul(cViewProj, float4(w, 1.0));
    o.wpos = w;
    o.nrm = float3(nr.x - i.i3.x * nr.y, nr.y, nr.z - i.i3.y * nr.y);
    o.uv = i.uv;
    o.layer = i.i1.w;
    o.sun = i.i2.x;
    o.ao = saturate(q.y / max(i.i2.y, 0.5));
    o.bright = i.i2.w;
    o.dust = i.i2.z;
    o.hue = i.i3.z;
    return o;
}

Texture2DArray<float4> ClutterTex : register(t0, space2);
Texture2D<float4> ClutterShroud : register(t1, space2);
SamplerState ClutterSamp : register(s0, space2);
SamplerState ClutterShroudSamp : register(s1, space2);
Texture2D<float> ClutterShadowMap : register(t2, space2);
SamplerComparisonState ClutterShadowSamp : register(s2, space2);
cbuffer ClutterFU : register(b0, space3) {
    float4 cSunDir;
    float4 cLight;      // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 cFogColor;
    float4 cFog;        // eye xyz, fog start
    float4 cFog2;       // fog end, map width px, map height px, shroud on
    float4 cTint[12];   // per layer: rgb = colour lock, a = mean luminance; a < 0: as authored x rgb
    float4 cLook[12];   // per layer: hue kept, contrast kept, dust share, -
    float4 cDust;       // rgb = sand colour, a = the sky's blue in the shadows
    float4x4 cLightVP;  // sun shadow map
    float4 cShadow;     // on, texel (uv), -, depth bias
};
float4 clutter_fs(CVOut i, bool front : SV_IsFrontFace) : SV_Target0 {
    const uint layer = min(uint(i.layer + 0.5), 11u);
    const float3 t = ClutterTex.Sample(ClutterSamp, float3(i.uv, float(layer))).rgb;
    const float L = dot(t, float3(0.299, 0.587, 0.114));
    const float4 ti = cTint[layer];
    const float4 lk = cLook[layer];
    float3 albedo;
    if (ti.a > 0.0) albedo = ti.rgb * lerp(ti.a, L, lk.y) * lerp(float3(1.0, 1.0, 1.0), t / max(L, 0.02), lk.x);
    else albedo = t * ti.rgb;
    albedo *= i.bright * float3(1.0 + 0.07 * i.hue, 1.0, 1.0 - 0.07 * i.hue);
    float3 n = normalize(i.nrm);
    if (!front) n = -n;
    albedo = lerp(albedo, cDust.rgb, i.dust * lk.z * smoothstep(0.35, 0.9, n.y));
    const float ndl = max(dot(n, cSunDir.xyz), 0.0);
    float sm = i.sun;
    if (cShadow.x > 0.5) SHADOW_PCF(ClutterShadowMap, ClutterShadowSamp, mul(cLightVP, float4(i.wpos, 1.0)).xyz, cShadow, sm);
    const float vis = lerp(cLight.w, 1.0, sm);
    const float amb = cLight.x * (0.74 + 0.26 * max(n.y, 0.0)) * lerp(0.55, 1.0, smoothstep(0.0, 0.6, i.ao));
    float3 rgb = satur(albedo) * sunAmb(amb, cLight.y * ndl * vis) * cLight.z + albedo * pointLights3(i.wpos, n, 0.25);
    const float k = cDust.a;
    rgb *= lerp(float3(1.0 - 0.13 * k, 1.0 - 0.07 * k, 1.0 + 0.06 * k), float3(1.0, 1.0, 1.0), sm * saturate(ndl * 4.0 + 0.15));
    const float2 p = i.wpos.xz, mapSize = cFog2.yz;
    float s = 0.0;
    if (cFog2.w > 0.5 && cFog2.w < 1.5) s = ClutterShroud.Sample(ClutterShroudSamp, p / mapSize).a;
    if (cFog2.w > 2.5) SHROUD_AT(ClutterShroud, ClutterShroudSamp, p, mapSize, s);  // (3: no edge fade; beyond the map the fixed fog)
    const float2 e = min(p, mapSize - p);
    if (cFog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    if (s > 0.996) discard;
    rgb *= dustHaze(i.wpos);  // Reimagined: drifting dust veils
    // (The world beyond the map: its pieces darker and greyer, as its ground - terrain3_fs.)
    const float outW = smoothstep(0.0, 20.0, length(max(max(-p, p - mapSize), float2(0.0, 0.0))));
    if (outW > 0.0) {
        const float grey = dot(rgb, float3(0.299, 0.587, 0.114));
        const float2 bt = plEdge.x > 0.5 ? float2(0.3, 0.12) : float2(0.22, 0.15);
        rgb = lerp(rgb, float3(grey, grey, grey), bt.x * outW) * (1.0 - bt.y * outW);
    }
    float f = smoothstep(cFog.w, cFog2.x, distance(i.wpos, cFog.xyz)) * aerialK();
    if (outW > 0.0) f = max(f, edgeHazeK(p, mapSize));
    rgb = lerp(rgb, cFogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}

// ================================================================================== shadow map
// Depth-only pass of the sun shadow map (terrain, rocks, models): no colour output.
void depth_fs() {}

// ================================================================================== HDR tone map
// render/hdr.*: a custom fragment shader of SDL's GPU renderer (SDL_GPURenderState). The vertex
// stage is SDL's own: colour at location 0, uv at location 1. Slot 0 = the drawn texture (the HDR
// scene), slot 1 = the bloom; uniforms b0. Twin of kTonemap in shaders_msl.hpp.
Texture2D tmScene : register(t0, space2);
SamplerState tmSceneSamp : register(s0, space2);
Texture2D tmBloom : register(t1, space2);
SamplerState tmBloomSamp : register(s1, space2);
cbuffer TmU : register(b0, space3) {
    float4 tmA;  // exposure, peak, knee, bloom strength
    float4 tmB;  // bloom uv scale x, y, input linear, output linear
    float4 tmC;  // highlight desaturation, display adjustments on
    float4 tmD;  // display adjustments: brightness, contrast, gamma, saturation (render/hdr.hpp DisplayAdjust)
    float4 tmE;  // the Reimagined "Realistic grade" (render/hdr.hpp Grade): desaturation (0 = off), toe, cooler shadows, highlight lift
};
struct TmIn {  // the semantics of SDL's own GPU-renderer shaders (D3D12 matches by name)
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
float3 tmToLin(float3 c) {
    c = max(c, 0.0);
    return lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045));
}
float3 tmToSrgb(float3 c) {
    c = max(c, 0.0);
    return lerp(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, step(c, 0.0031308));
}
// Display adjustments (twin of displayAdjust in shaders_msl.hpp): saturation on linear light, then
// gamma / contrast / brightness on the sRGB-coded value; 0 and 1 stay fixed, EDR values above 1 kept.
float3 tmDisplayAdjust(float3 c, float4 d) {
    const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = max(y + (c - y) * (1.0 + d.w), 0.0);
    const float3 over = max(c - 1.0, 0.0);
    float3 e = tmToSrgb(min(c, 1.0));
    e = pow(e, 1.0 / d.z);
    const float p = d.y >= 0.0 ? 1.0 + 2.0 * d.y : 1.0 / (1.0 - 2.0 * d.y);
    const float3 lo = 0.5 * pow(2.0 * e, p), hi = 1.0 - 0.5 * pow(max(2.0 * (1.0 - e), 0.0), p);
    e = lerp(hi, lo, step(e, 0.4999999));
    e = d.x >= 0.0 ? e + d.x * (1.0 - e) : e * (1.0 + d.x);
    return tmToLin(e) + over;
}
// The Reimagined "Realistic grade" (twin of naturalGrade in shaders_msl.hpp): less saturation, a filmic toe,
// cooler shadows, lifted highlights - on the tone-mapped linear light.
float3 tmNaturalGrade(float3 c, float4 g) {
    const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = max(y + (c - y) * (1.0 - g.x), 0.0);
    const float yt = y * (y + 0.35 * g.y) / (y + g.y) * (1.0 + g.w * smoothstep(0.2, 0.85, y));
    c *= yt / max(y, 1e-5);
    const float sh = (1.0 - smoothstep(0.0, 0.2, y)) * g.z;
    return c * float3(1.0 - 0.07 * sh, 1.0 - 0.015 * sh, 1.0 + 0.07 * sh);
}
float4 tonemap_fs(TmIn i) : SV_Target {
    const float3 s = tmScene.Sample(tmSceneSamp, i.uv).rgb * i.color.rgb;
    const float3 bl = max(tmBloom.Sample(tmBloomSamp, i.uv * tmB.xy).rgb, 0.0) * tmA.w;
    float3 c = tmB.z > 0.5 ? max(s, 0.0) + bl : tmToLin(s + tmToSrgb(bl));
    c *= tmA.x;
    const float m = max(c.r, max(c.g, c.b));
    const float knee = tmA.z, peak = tmA.y, room = peak - knee;
    if (m > knee) {
        if (room <= 1e-4) {
            c = min(c, peak);
        } else {
            const float mm = knee + room * (1.0 - exp(-(m - knee) / room));
            const float3 t = c * (mm / m);
            const float w = saturate((m - peak) / (3.0 * peak)) * tmC.x;
            // towards the per-channel shoulder (orange -> yellow -> white, not through pink; see the MSL twin)
            const float3 pc = min(c, knee) + room * (1.0 - exp(-max(c - knee, 0.0) / room));
            c = lerp(t, pc, w);
        }
    }
    if (tmE.x > 0.0) c = tmNaturalGrade(c, tmE);
    if (tmC.y > 0.5) c = tmDisplayAdjust(c, tmD);
    return float4(tmB.w > 0.5 ? c : tmToSrgb(c), 1.0);
}

// ================================================================================== cinematic particles
// HLSL twin of render3d/cinematic_msl.hpp (render3d/cinematic_fx.*, docs/3d-mode.md "Reimagined"): the
// instance layout, modes and the look are described there.
struct FxIn {
    float4 posSize : TEXCOORD0;  // world xyz, size (px)
    float4 velRot : TEXCOORD1;   // world velocity (sparks), rotation (radians)
    float4 albedo : TEXCOORD2;   // rgb, alpha
    float4 emit : TEXCOORD3;     // emissive rgb, mode
    float4 extra : TEXCOORD4;    // ground height, streak length, soft fade, heat
    float4 light : TEXCOORD5;    // nearby light rgb, age 0..1
    float4 aux : TEXCOORD6;      // seed, noise scale, erosion, rays
    float4 corners : TEXCOORD7;  // ground marks: corner heights
};
struct FxOut {
    float4 pos : SV_Position;
    float2 q : TEXCOORD0;
    float3 wpos : TEXCOORD1;
    float3 ax : TEXCOORD2;
    float3 ay : TEXCOORD3;
    float4 albedo : TEXCOORD4;
    float4 emit : TEXCOORD5;
    float4 extra : TEXCOORD6;
    float4 light : TEXCOORD7;
    float4 aux : TEXCOORD8;
    float3 vel : TEXCOORD9;  // ground marks: the scorch rim's dust colour
    float dust : TEXCOORD10; // puffs: ground dust (corners.x); unused since every puff has the plume lighting
};
cbuffer FxVU : register(b0, space1) {
    float4x4 fxViewProj;
    float4 fxRight;
    float4 fxUp;
    float4 fxEye;
};
static const float2 kFxCorners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
FxOut fx_vs(FxIn i, uint vid : SV_VertexID) {
    const float2 c = kFxCorners[vid % 6];
    const float mode = i.emit.w;
    const float3 R = fxRight.xyz, U = fxUp.xyz;
    float3 ax, ay, p;
    float2 q = c;
    float fade = 1.0;
    if (mode > 0.5 && mode < 1.5) {
        float2 v = float2(dot(i.velRot.xyz, R), dot(i.velRot.xyz, U));
        const float l = length(v);
        v = l > 1e-4 ? v / l : float2(1.0, 0.0);
        ax = R * v.x + U * v.y;
        ay = R * -v.y + U * v.x;
        const float len = max(i.extra.y, i.posSize.w);
        p = i.posSize.xyz + ax * ((c.x - 1.0) * 0.5 * len) + ay * (c.y * 0.5 * i.posSize.w);
    } else if (mode > 3.5) {
        const float cs = cos(i.velRot.w), sn = sin(i.velRot.w);
        ax = float3(cs, 0.0, sn);
        ay = float3(-sn, 0.0, cs);
        // a corner: its height in 1/4 px (floor) + its fade (fract / 0.99: none on steep faces / up walls)
        const float hv = c.x < 0.0 ? (c.y < 0.0 ? i.corners.x : i.corners.w) : (c.y < 0.0 ? i.corners.y : i.corners.z);
        const float h = floor(hv) * 0.25;
        fade = saturate((hv - floor(hv)) / 0.99);
        // one cell of the mark's n x n grid (extra.y = n * 256 + row * 16 + column, 0 = the whole mark)
        const float g = i.extra.y;
        const float n = max(1.0, floor(g / 256.0 + 0.001));
        const float cell = g - floor(g / 256.0 + 0.001) * 256.0;
        const float2 ij = float2(cell - floor(cell / 16.0 + 0.001) * 16.0, floor(cell / 16.0 + 0.001));
        q = -1.0 + (ij + (c * 0.5 + 0.5)) * (2.0 / n);
        p = i.posSize.xyz + (ax * q.x + ay * q.y) * (0.5 * i.posSize.w);
        p.y = i.posSize.y + h;
        p += normalize(fxEye.xyz - p) * 1.5;
    } else {
        const float cs = cos(i.velRot.w), sn = sin(i.velRot.w);
        ax = R * cs + U * sn;
        ay = U * cs - R * sn;
        p = i.posSize.xyz + (ax * c.x + ay * c.y) * (0.5 * i.posSize.w);
    }
    FxOut o;
    o.pos = mul(fxViewProj, float4(p, 1.0));
    o.q = q;
    o.wpos = p;
    o.ax = ax;
    o.ay = ay;
    o.vel = i.velRot.xyz;
    o.albedo = i.albedo;
    o.albedo.a *= fade;
    o.emit = i.emit;
    o.extra = i.extra;
    o.light = i.light;
    o.aux = i.aux;
    o.dust = i.corners.x;
    return o;
}

Texture3D<float4> FxNoise : register(t0, space2);
SamplerState FxNoiseSamp : register(s0, space2);
cbuffer FxFU : register(b0, space3) {
    float4 fxSunDir;
    float4 fxSunCol;
    float4 fxAmbCol;
    float4 fxFogColor;
    float4 fxFog;
    float4 fxFog2;
};
float3 fxFireRamp(float T) {
    float3 c = float3(0.0, 0.0, 0.0);
    c = lerp(c, float3(0.42, 0.035, 0.0), smoothstep(0.03, 0.25, T));
    c = lerp(c, float3(1.0, 0.28, 0.035), smoothstep(0.2, 0.5, T));
    c = lerp(c, float3(1.0, 0.62, 0.17), smoothstep(0.45, 0.76, T));
    c = lerp(c, float3(1.0, 0.93, 0.78), smoothstep(0.74, 1.0, T));
    return c * (0.5 + T * T * 9.0);
}
float4 fx_fs(FxOut i) : SV_Target0 {
    const float mode = i.emit.w;
    const float2 q = i.q;
    const float r2 = dot(q, q);
    const float f = smoothstep(fxFog.w, fxFog2.x, distance(i.wpos, fxFog.xyz)) * (1.0 - fxFog2.y);
    const float seed = i.aux.x;
    const float t = i.light.w;
    if (mode > 0.5 && mode < 2.5) {
        float k;
        if (mode < 1.5) {
            const float along = q.x * 0.5 + 0.5;
            k = exp(-q.y * q.y * 6.0) * along * along * smoothstep(1.0, 0.85, q.x);
        } else {
            const float rf = sqrt(r2);
            k = exp(-r2 * 5.0) * 0.7 + exp(-r2 * 24.0);
            if (i.aux.w > 0.5) {
                const float ang = atan2(q.y, q.x);
                const float ray = pow(abs(cos(ang * i.aux.w * 0.5 + seed * 6.0)), 10.0);
                k += ray * exp(-rf * 3.5) * 0.9;
                k += exp(-q.y * q.y * 40.0) * saturate(q.x) * exp(-q.x * 1.5) * 1.4;
            }
            k *= saturate(1.0 - r2);
        }
        return float4(i.emit.rgb * (k * i.albedo.a * (1.0 - f)), 0.0);
    }
    if (r2 >= 1.0) discard;
    const float r = sqrt(r2);
    if (mode > 3.5) {
        const float3 P = float3(q * 0.55, seed * 7.0);
        const float4 n1 = FxNoise.Sample(FxNoiseSamp, P);
        const float4 n2 = FxNoise.Sample(FxNoiseSamp, P * 2.7 + float3(0.31, 0.17, 0.0));
        const float n = n1.r * 0.6 + n2.r * 0.4;
        if (mode > 5.5) {
            // ground dust shadow (MSL twin: cinematic_msl.hpp)
            const float d = exp(-r2 * 2.6) * saturate(0.6 + (n - 0.5) * 1.6) * smoothstep(1.0, 0.6, r);
            const float a = d * i.albedo.a * (1.0 - f);
            if (a < 0.002) discard;
            return float4(0.0, 0.0, 0.0, a);
        }
        if (mode < 4.5) {
            const float ang = atan2(q.y, q.x);
            const float streak = FxNoise.Sample(FxNoiseSamp, float3(cos(ang) * 0.45, sin(ang) * 0.45, seed * 3.0 + r * 0.08)).r;
            float d = saturate((0.78 - r) * 2.2 + (n - 0.5) * 1.3 + (streak - 0.5) * 1.2 * r - 0.15);
            d = d * d * (3.0 - 2.0 * d);
            const float rn = r + (n - 0.5) * 0.18;
            const float rim = smoothstep(0.3, 0.5, rn) * (1.0 - smoothstep(0.55, 0.9, rn));
            const float bowl = 1.0 - smoothstep(0.28, 0.5, rn);
            const float3 radial = (i.ax * q.x + i.ay * q.y) / max(r, 1e-3);
            const float slope = rim * 0.6 - bowl * smoothstep(0.05, 0.4, r) * 0.8;
            const float3 nW = normalize(float3(0.0, 1.0, 0.0) + radial * slope);
            const float shade = saturate(dot(nW, fxSunDir.xyz) * 0.8 + 0.35) / max(0.2, saturate(fxSunDir.y * 0.8 + 0.35));
            const float3 light = fxAmbCol.rgb * fxSunCol.a + fxSunCol.rgb * saturate(fxSunDir.y) * shade + i.light.rgb;
            const float ab = d * (0.75 + 0.25 * bowl);
            const float ae = rim * (0.35 + 0.4 * n2.r);
            const float aa = (ab + ae * (1.0 - ab)) * i.albedo.a * smoothstep(1.0, 0.88, r);
            if (aa < 0.003) discard;
            const float3 cb = i.albedo.rgb * (0.65 + 0.7 * n2.r) * light;
            const float3 ce = i.vel * (0.8 + 0.35 * n1.r) * light;
            float3 lit = (cb * ab + ce * ae * (1.0 - ab)) / max(1e-3, ab + ae * (1.0 - ab));
            lit = lerp(lit, fxFogColor.rgb, f);
            const float ember = smoothstep(0.6, 0.82, n2.r) * smoothstep(0.6, 0.05, r) * i.extra.w;
            const float3 e = float3(1.0, 0.32, 0.05) * ember * 4.0 * (1.0 - f);
            return float4((lit + e) * aa, aa);
        }
        const float rr = (r - 0.7) / 0.17;
        float d = exp(-rr * rr) * (0.45 + 1.1 * (n - 0.35)) + exp(-r2 * 3.0) * 0.3 * (1.0 - t) * n;
        d = saturate(d) * smoothstep(1.0, 0.86, r);
        const float a = d * i.albedo.a;
        if (a < 0.003) discard;
        const float sunT = saturate(fxSunDir.y * 0.7 + 0.3);
        float3 lit = i.albedo.rgb * (fxAmbCol.rgb * fxSunCol.a + fxSunCol.rgb * sunT + i.light.rgb) * (0.8 + 0.4 * n2.r);
        lit = lerp(lit, fxFogColor.rgb, f);
        return float4(lit * a, a);
    }
    const float sph = sqrt(1.0 - r2);
    const float3 toEye = normalize(fxFog.xyz - i.wpos);
    const float sc = i.aux.y;
    const float3 P = float3(q * 0.42 * sc, sph * 0.3) + float3(seed * 13.7, seed * 5.3, seed * 9.1) + float3(0.0, -t * 0.32, t * 0.22);
    const float4 n1 = FxNoise.Sample(FxNoiseSamp, P);
    const float4 n2 = FxNoise.Sample(FxNoiseSamp, P * 2.6 + (n1.gba - 0.5) * 0.2);
    const float n = n1.r * 0.62 + n2.r * 0.38;
    const float3 g = (n1.gba - 0.5) * 2.0 * 0.9 + (n2.gba - 0.5) * 2.0 * 0.5;
    if (mode > 2.5) {
        if (r > 0.5 + (n - 0.5) * 0.9) discard;
        const float3 nq = normalize(float3(q, sph) - g * 0.8);
        const float3 nW = normalize(i.ax * nq.x + i.ay * nq.y + toEye * nq.z);
        const float sunT = saturate(dot(nW, fxSunDir.xyz) * 0.8 + 0.2);
        float3 lit = i.albedo.rgb * (fxAmbCol.rgb * fxSunCol.a * (0.7 + 0.3 * nW.y) + fxSunCol.rgb * sunT + i.light.rgb);
        lit = lerp(lit, fxFogColor.rgb, f);
        const float3 hot = i.emit.rgb * i.extra.w * (0.4 + 0.6 * n2.r) * (1.0 - f);
        return float4((lit + hot) * i.albedo.a, i.albedo.a);
    }
    // Wisps, not discs: the noise pushes the outline about (more as the puff ages), then erodes it.
    const float2 qw = q * (1.12 + (n1.r - 0.45) * 0.8) + (n1.gb - 0.5) * (0.5 + 0.5 * t);
    const float sphW = sqrt(saturate(1.0 - dot(qw, qw)));
    const float erode = i.aux.z;
    const float soot = i.aux.w;  // 0 dust .. 1 black smoke: how much of the light the puff swallows
    float dens = saturate((sphW * 1.15 + (n - 0.5) * (1.5 + 0.5 * t) - erode) * (2.2 - 0.7 * soot)) * smoothstep(1.0, 0.72, r);
    float a = dens * i.albedo.a;
    // Soft against the ground under it.
    if (i.extra.z > 0.0) a *= smoothstep(0.0, i.extra.z, i.wpos.y - i.extra.x);
    if (a < 0.003) discard;
    const float3 nq = normalize(float3(qw, sphW * 1.0 + 0.12) - g * 0.7);
    const float3 nW = normalize(i.ax * nq.x + i.ay * nq.y + toEye * nq.z);
    // Self-shadow: how much of the cloud lies between this point and the sun (soot swallows more).
    const float3 sq = float3(dot(fxSunDir.xyz, i.ax), dot(fxSunDir.xyz, i.ay), dot(fxSunDir.xyz, toEye));
    const float2 qs = qw + sq.xy * 0.5;
    const float sphS = sqrt(saturate(1.0 - dot(qs, qs)));
    const float nS = FxNoise.Sample(FxNoiseSamp, P + float3(sq.xy * 0.21 * sc, 0.0)).r;
    const float occ = saturate(sphS * 1.15 + (nS * 0.62 + n2.r * 0.38 - 0.5) * 1.5 - erode);
    const float selfShadow = exp(-occ * (1.2 + 0.5 * soot) * i.albedo.a);
    const float ndl = dot(nW, fxSunDir.xyz);
    const float thin = 1.0 - dens;  // thin parts let the light through (no dark rims)
    const float hb = saturate((i.wpos.y - i.extra.x) / 80.0);  // height above the ground
    // One plume lighting for smoke and dust: the sun side and top bright and warm, the side away from the sun and
    // the underside in the cloud's own shade, cooler (sky light, never black); the thin edges scatter the sun -
    // a little on the sun side, a bright rim when the sun stands behind the smoke.
    const float sunD = pow(saturate(ndl * 0.6 + 0.4), 1.5) * lerp(1.0, selfShadow, 0.85 * dens) +
                       thin * (0.08 + 0.22 * soot * saturate(ndl * 0.5 + 0.5) + pow(saturate(-sq.z), 3.0) * (0.35 + 0.5 * soot));
    const float up = saturate(nW.y * 0.5 + 0.5);
    const float under = 1.0 - up;
    // A smoke column stands in its own shade at its base and in its dense core.
    const float deep = 1.0 - soot * (0.3 * (1.0 - hb) + 0.2 * dens * dens * under);
    const float skyD = (0.5 + 0.5 * up + thin * 0.1) * deep;
    // The sunlit ground under it throws warm light back up.
    const float3 bounce = fxSunCol.rgb * float3(1.0, 0.84, 0.62) * (saturate(fxSunDir.y) * under * 0.1);
    const float3 fire = i.light.rgb * (0.4 + 0.6 * under);  // fires light it from below
    const float3 alb = i.albedo.rgb * (1.0 + 0.3 * soot * thin);  // wisps are paler than the dense body
    float3 lit = alb * (fxAmbCol.rgb * float3(0.94, 0.97, 1.04) * skyD * fxSunCol.a +
                        fxSunCol.rgb * float3(1.08, 1.0, 0.88) * sunD * lerp(1.0, deep, 0.5) + bounce + fire);
    lit += i.light.rgb * (soot * dens * (1.0 - hb) * (0.12 + 0.25 * under));  // firelight caught in the base
    // Heat: the dense core stays hot longest (fire cools from the outside in).
    const float heat = i.extra.w;
    float3 em = float3(0.0, 0.0, 0.0);
    if (heat > 0.001) {
        const float core = sphW * 0.85 + (n - 0.5) * 1.6 + 0.2;
        const float T = saturate(core * heat * 0.98 - (1.0 - heat) * 0.55);
        em = fxFireRamp(T) * i.emit.rgb;
        lit *= 1.0 - 0.5 * saturate(heat * 2.0 - 0.6);  // only while it still glows
        a = min(1.0, a * (1.0 + heat * 0.6));
    }
    lit = lerp(lit, fxFogColor.rgb, f);
    return float4((lit + em * (1.0 - f)) * a, a);
}

// The half-resolution particles blended over the resolved scene (cinematic_msl.hpp fxc_vs / fxc_fs).
float4 fxc_vs(uint vid : SV_VertexID) : SV_Position {
    const float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
Texture2D<float4> FxcHalf : register(t0, space2);
SamplerState FxcHalfSamp : register(s0, space2);
cbuffer FxcFU : register(b0, space3) {
    float4 fxcScale;
};
float4 fxc_fs(float4 pos : SV_Position) : SV_Target0 {
    return FxcHalf.Sample(FxcHalfSamp, pos.xy * fxcScale.xy);
}

// ================================================================================== ambient occlusion
// Screen-space ambient occlusion (render3d/ssao.*, docs/3d-mode.md "Ambient occlusion"; MSL twin
// render3d/ssao_msl.hpp): ssao_fs from the half-size scene depth, ssaoblur_fs its depth-aware 5 x 5 blur. Both
// full-screen (vertex shader fxc_vs).
Texture2D<float4> SsDepth : register(t0, space2);
SamplerState SsDepthSamp : register(s0, space2);
cbuffer SsaoFU : register(b0, space3) {
    float4 ssProj;  // x, y = tan of half the horizontal / vertical field of view, z = near plane, w = far plane
    float4 ssTex;   // xy = 1 / the depth texture's size, zw = the half-resolution viewport (px)
    float4 ssK;     // x radius on screen (world px), y strength, z angle bias, w reach (world px: an occluder's distance)
};
float ssDist(float z) { return ssProj.z * ssProj.w / (ssProj.w - z * (ssProj.w - ssProj.z)); }
float3 ssPos(float2 px) {
    const float dist = ssDist(SsDepth.SampleLevel(SsDepthSamp, px * ssTex.xy, 0.0).r);
    const float2 ndc = float2(px.x / ssTex.z * 2.0 - 1.0, 1.0 - px.y / ssTex.w * 2.0);
    return float3(ndc * ssProj.xy * dist, dist);
}
float4 ssao_fs(float4 pos : SV_Position) : SV_Target0 {
    const float2 px = pos.xy;
    if (SsDepth.SampleLevel(SsDepthSamp, px * ssTex.xy, 0.0).r >= 0.99999) return float4(1.0, 1.0, 1.0, 1.0);
    const float3 P = ssPos(px);
    const float3 Pl = ssPos(px - float2(1.0, 0.0)), Pr = ssPos(px + float2(1.0, 0.0));
    const float3 Pt = ssPos(px - float2(0.0, 1.0)), Pb = ssPos(px + float2(0.0, 1.0));
    const float3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
    const float3 dy = abs(Pb.z - P.z) < abs(P.z - Pt.z) ? Pb - P : P - Pt;
    float3 n = normalize(cross(dy, dx));
    if (dot(n, P) > 0.0) n = -n;  // towards the eye
    const float rpx = min(ssK.x / (P.z * ssProj.x) * ssTex.z * 0.5, 40.0);
    const float rot = 6.2831853 * frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    const float r2 = ssK.w * ssK.w;
    float occ = 0.0;
    [unroll] for (int i = 0; i < 10; ++i) {
        const float t = (float(i) + 0.5) / 10.0;
        const float a = rot + float(i) * 2.399963;
        const float2 q = floor(px + float2(cos(a), sin(a)) * (rpx * pow(t, 0.75) + 0.8)) + 0.5;
        const float3 v = ssPos(q) - P;
        const float vv = dot(v, v);
        occ += max(dot(v, n) * rsqrt(vv + 1e-3) - ssK.z, 0.0) * saturate(1.0 - vv / r2);
    }
    const float on = smoothstep(0.6, 1.6, rpx);
    const float ao = lerp(1.0, saturate(1.0 - occ * (ssK.y / 10.0)), on);
    return float4(ao, ao, ao, 1.0);
}
// ssaoblur_fs: t0 the raw occlusion, t1 the depth.
Texture2D<float4> SsbAo : register(t0, space2);
SamplerState SsbAoSamp : register(s0, space2);
Texture2D<float4> SsbDepth : register(t1, space2);
SamplerState SsbDepthSamp : register(s1, space2);
float4 ssaoblur_fs(float4 pos : SV_Position) : SV_Target0 {
    const float2 px = pos.xy;
    const float z0 = SsbDepth.SampleLevel(SsbDepthSamp, px * ssTex.xy, 0.0).r;
    if (z0 >= 0.99999) return float4(1.0, 1.0, 1.0, 1.0);
    const float d0 = ssDist(z0);
    const float wpp = d0 * ssProj.y * 2.0 / ssTex.w;  // world px per half-resolution pixel at this depth
    float sum = 0.0, wsum = 0.0;
    [unroll] for (int j = -2; j <= 2; ++j)
        [unroll] for (int i = -2; i <= 2; ++i) {
            const float2 o = float2(float(i), float(j));
            const float2 uv = (px + o) * ssTex.xy;
            const float d = ssDist(SsbDepth.SampleLevel(SsbDepthSamp, uv, 0.0).r);
            const float w = saturate(1.0 - abs(d - d0) / ((1.5 + 1.5 * length(o)) * wpp + 0.5)) * (1.0 - 0.06 * dot(o, o));
            sum += SsbAo.SampleLevel(SsbAoSamp, uv, 0.0).r * w;
            wsum += w;
        }
    const float ao = sum / max(wsum, 1e-4);
    return float4(ao, ao, ao, 1.0);
}
