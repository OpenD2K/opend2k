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
// Dynamic point lights (render3d/lights.hpp): fragment uniform buffer 1 of every lit shader.
cbuffer PointLightsFU : register(b1, space3) {
    float4 plInfo;            // x = count, y = specular multiplier, z = saturation of the sun / sky lit colour
    float4 plSunColor;        // rgb: the sun's colour multiplier (time of day / Modern lighting)
    float4 plAmbColor;        // rgb: the ambient colour multiplier
    float4 plSpriteTint;      // rgb: billboards' light
    float4 plPosRadius[48];   // world xyz, radius
    float4 plColor[48];       // rgb * intensity
    float4 plSpot[48];        // xyz beam direction, w = cos half angle (< -1: point light)
};
// The frame's sun + sky light: ambient and diffuse terms (scalars of the old light) tinted.
float3 sunAmb(float amb, float dif) { return amb * plAmbColor.rgb + dif * plSunColor.rgb; }
// The time of day's saturation (night: towards grey, then the blue moonlight tints it); 1 = as is.
float3 satur(float3 c) {
    if (abs(plInfo.z - 1.0) < 0.001) return c;
    const float l = dot(c, float3(0.299, 0.587, 0.114));
    return max(lerp(float3(l, l, l), c, plInfo.z), 0.0);
}
// Sum of the point lights at world point p with normal n; wrap 0 = Lambert, 1 = no normal
// dependence (billboards). Spot lights fade out over the outer third of their cone.
float3 pointLights(float3 p, float3 n, float wrap) {
    float3 sum = float3(0.0, 0.0, 0.0);
    const int cnt = int(plInfo.x);
    [loop] for (int i = 0; i < cnt; ++i) {
        const float3 d = plPosRadius[i].xyz - p;
        const float r = plPosRadius[i].w;
        const float d2 = dot(d, d);
        if (d2 >= r * r) continue;
        float att = 1.0 - d2 / (r * r);
        att *= att;
        const float3 dir = d * rsqrt(max(d2, 1e-4));
        const float4 sp = plSpot[i];
        if (sp.w > -1.5) {
            const float c = dot(-dir, sp.xyz);
            att *= smoothstep(sp.w, sp.w + (1.0 - sp.w) * 0.45, c);
        }
        const float ndl = saturate(dot(n, dir) * (1.0 - wrap) + wrap);
        sum += plColor[i].rgb * (att * ndl);
    }
    return sum;
}

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
    float3 rgb = satur(c.rgb) * shade + c.rgb * pointLights(i.wpos, n, 0.3);
    // Darkness: the shroud mask, and a thin fade into the black beyond the map edge. The haze
    // is darkened too, so shrouded ground stays black at any distance.
    float s = 0.0;
    if (tFog2.w > 0.5) s = TerrainShroud.Sample(TerrainShroudSamp, i.wpos.xz / tFog2.yz).a;
    const float2 e = min(i.wpos.xz, tFog2.yz - i.wpos.xz);
    s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(i.wpos, tFog.xyz);
    const float f = smoothstep(tFog.w, tFog2.x, d);
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
    c *= i.color;
    c.rgb = min(c.rgb + i.flash * c.a, 1.0);
    // Darkness of the map point this pixel covers in the 2D view (decals: the ground under it);
    // nothing outside the map is ever visible.
    float dark = 0.0;
    if (sMode.y > 0.5) dark = SpriteShroud.Sample(SpriteShroudSamp, i.ground / sMode.zw).a;
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
        c.rgb += alb * pointLights(i.wpos, float3(0.0, 1.0, 0.0), 0.8);
        c.rgb = lerp(c.rgb, sFogColor.rgb, i.fogF) * (1.0 - dark);
    } else if (sMode.x < 1.5) {
        c.rgb *= (1.0 - dark) * (1.0 - i.fogF);
    } else {
        c.rgb = lerp(c.rgb, float3(1.0, 1.0, 1.0), max(dark, i.fogF));
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
    float4 mSkin;   // x = first palette entry of this draw (matrices), < 0 = not skinned
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

MVOut model_vs(MVIn i) {
    MVOut o;
    const float4 w = mul(mModel, Skinned(i.joints, i.weights, float4(i.pos, 1.0)));
    o.pos = mul(mViewProj, w);
    o.wpos = w.xyz;
    o.nrm = mul(mModel, Skinned(i.joints, i.weights, float4(i.nrm, 0.0))).xyz;
    o.uv = i.uv;
    o.color = i.color;
    o.params = i.params;
    o.pbr = float4(i.pbr, 0.0, 0.0);
    return o;
}

MVOut model_shadow_vs(MVIn i) {
    MVOut o;
    float3 w = mul(mModel, Skinned(i.joints, i.weights, float4(i.pos, 1.0))).xyz;
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
    const float3 glow = c.rgb / max(max(c.r, max(c.g, c.b)), 0.05);  // the albedo's hue at full brightness
    if (i.params.x > 1.5) {
        c.rgb = mHouse.rgb;
    } else if (i.params.x > 0.0) {
        const float l = dot(c.rgb, float3(0.299, 0.587, 0.114));
        c.rgb = lerp(c.rgb, saturate(mHouse.rgb * l * 1.8), i.params.x);
    }
    if (i.params.y > 0.5) {
        float3 n = normalize(i.nrm);
        if (!front) n = -n;  // two-sided: the inside of a face is lit as its outside
        float ndl = max(dot(n, mSunDir.xyz), 0.0);
        if (mShadow.x > 0.5) {
            float sm = 1.0;
            SHADOW_PCF(ModelShadowMap, ModelShadowSamp, mul(mLightVP, float4(i.wpos, 1.0)).xyz, mShadow, sm);
            ndl *= lerp(mShadow.z, 1.0, sm);
        }
        const float3 base = c.rgb;
        c.rgb = satur(c.rgb);  // the time of day's saturation (night: towards grey) before the light
        if (i.pbr.x > 0.0) {
            // glTF models: sky ambient (brighter facing up) + a soft fill light from the side
            // opposite the sun, so faces turned away from the sun keep their shape.
            const float3 fillDir = normalize(float3(-mSunDir.x, 0.25, -mSunDir.z));
            const float amb = mLight.x * (0.72 + 0.18 * n.y) + mLight.x * 0.4 * max(dot(n, fillDir), 0.0);
            c.rgb *= sunAmb(amb, mLight.y * ndl) * mLight.z * (1.0 - 0.2 * i.pbr.y);
        } else {
            c.rgb *= sunAmb(mLight.x, mLight.y * ndl) * mLight.z;
        }
        if (i.pbr.x > 0.0) {  // glTF materials: a sun highlight (Blinn-Phong), tinted on metals
            const float3 h = normalize(mSunDir.xyz + normalize(mFog.xyz - i.wpos));
            const float sp = pow(max(dot(n, h), 0.0), 6.0 + 90.0 * i.pbr.x * i.pbr.x) * i.pbr.x * step(0.0, ndl);
            c.rgb += sp * (0.25 + 0.45 * i.pbr.y) * lerp(float3(1.0, 1.0, 1.0), base, i.pbr.y) * plInfo.y * plSunColor.rgb;
        }
        c.rgb += base * pointLights(i.wpos, n, 0.25);  // dynamic lights
    } else if (i.params.y > 0.2) {  // flat-shaded parts: the time of day's light, like the billboards
        c.rgb = satur(c.rgb) * plSpriteTint.rgb + c.rgb * pointLights(i.wpos, float3(0.0, 1.0, 0.0), 0.8);
    }
    c.rgb = min(c.rgb + mHouse.a, 1.0);
    c.rgb += glow * emit;  // may exceed 1 (HDR: blooms)
    c.rgb += float3(1.0, 0.55, 0.2) * (band * band * 1.6) + float3(1.0, 0.95, 0.85) * pow(band, 12.0);
    if (mLight.w > 0.5) c.rgb *= 1.0 - ModelShroud.Sample(ModelShroudSamp, i.wpos.xz / mMap.xy).a;
    const float f = smoothstep(mFogColor.a, mFog.w, distance(i.wpos, mFog.xyz));
    c.rgb = lerp(c.rgb, mFogColor.rgb, f);
    return float4(c.rgb, mSunDir.w);
}

float4 shadow_fs(MVOut i) : SV_Target0 {
    return float4(0.0, 0.0, 0.0, mMap.z);
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
    float3 rgb = satur(albedo) * shade + albedo * pointLights(i.wpos, n, 0.3);
    float s2 = 0.0;
    // fog2.w: 0 no shroud, 1 shroud, 2 baked for the Enhanced 2D view (no shroud, no edge fade).
    if (t2Fog2.w > 0.5 && t2Fog2.w < 1.5) s2 = T2Shroud.Sample(T2ShroudSamp, p / mapSize).a;
    const float2 e = min(p, mapSize - p);
    if (t2Fog2.w < 1.5) s2 = max(s2, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(i.wpos, t2Fog.xyz);
    const float f = smoothstep(t2Fog.w, t2Fog2.x, d);
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
};

// The relief (world px above the ground): the guide's macro height, and on broken rock the
// original's own light and dark as relief (pale = proud, dark = a pocket).
float faithHeight(float4 g, bool macro) {
    return (macro ? g.g * t3Guide.x : 0.0) + g.a * (g.b * 2.0 - 1.0) * t3Guide.w;
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

float4 terrain3_fs(TVOut i) : SV_Target0 {
    const float2 p = i.wpos.xz;
    const float2 mapSize = t2Fog2.yz;
    const float3 dpx = ddx(i.wpos), dpy = ddy(i.wpos);
    const float3 gn = normalize(i.nrm);
    const bool bake = t3Guide.z > 0.5;

    const float4 a = T2SplatA.Sample(T2SplatSamp, p / mapSize);
    const float4 b = T2SplatB.Sample(T2SplatSamp2, p / mapSize);
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
    const float4 g = guideAt(pg, mapSize, tpp, lod);
    const float e = 1.5;
    const float hE = guideAt(pg + float2(e, 0.0), mapSize, tpp, lod).g;
    const float hW = guideAt(pg - float2(e, 0.0), mapSize, tpp, lod).g;
    const float hS = guideAt(pg + float2(0.0, e), mapSize, tpp, lod).g;
    const float hN = guideAt(pg - float2(0.0, e), mapSize, tpp, lod).g;
    const float2 mg = float2(hE - hW, hS - hN) * (t3Guide.x / (2.0 * e));
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
    const float4 gb = T3Guide.SampleLevel(T3GuideSamp, guv, lod + 3.0);  // the surroundings (blurred)
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
    const float gUp = guideAt(pg + dir * 2.0, mapSize, tpp, lod).g;
    const float gDn = guideAt(pg - dir * 2.0, mapSize, tpp, lod).g;
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
        const float4 g1 = guideAt(pg + float2(d, 0.0), mapSize, tpp, lod);
        const float4 g2 = guideAt(pg + float2(0.0, d), mapSize, tpp, lod);
        // Baked from above the macro shape is shaded by the cliff shading above, not by this normal.
        const float h0 = faithHeight(g, !bake);
        const float2 grad = float2(faithHeight(g1, !bake) - h0, faithHeight(g2, !bake) - h0) / d;
        rn = normalize(float3(-grad.x, 1.0, -grad.y));
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
    const float2 sf = spiceField(p, mapSize);
    const float amount = (sf.x + sf.y) * (2.0 / 7.0);
    const float grains = vnoise(p / 5.0) * 0.6 + vnoise(p / 15.0) * 0.4;
    // The edge of a field dissolves: the presence against a noise threshold (lobes and specks).
    const float edgeN = vnoise(p / 11.0 + 4.0) * 0.5 + vnoise(p / 4.0 + 8.0) * 0.3 + vnoise(p / 1.7) * 0.2;
    const float cover = smoothstep(-0.12, 0.2, sf.x - (0.15 + 0.6 * edgeN));
    float spice = lerp(cover * 0.78, 1.0, sf.y) * smoothstep(1.0, 0.55, grains - amount * 0.3) * 0.92;
    spice *= saturate((1.0 - rockW) * 1.6);
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    const bool steepGeo = !bake && gn.y < 0.82;  // (baked from above: the mesh only casts shadows)
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
    const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
    albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
    albedo *= lerp(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick

    // Light: level ground under detailSun + the sun shadow map; broken rock under sunDir (baked
    // from above without cast shadow: the original's shading of the rock is in its tone levels).
    // (Baked: sand under the ramp the mesh makes of a steep lee face is looked up at ground level.)
    float3 sp = i.wpos;
    if (bake && rockW < 0.5 && gn.y < 0.85 && dot(gn.xz, t2SunDir.xz) < 0.0) sp.y = 0.0;
    float sv = i.sun;
    if (t2Shadow.x > 0.5) SHADOW_PCF(T2ShadowMap, T2ShadowSamp, mul(t2LightVP, float4(sp, 1.0)).xyz, t2Shadow, sv);
    const float visAll = lerp(t2Light.w, 1.0, sv);
    const float vis = bake ? lerp(visAll, 1.0, slopeW * 0.7 * rockW) : visAll;
    const float3 shadeFlat = sunAmb(t2Light.x, t2Light.y * max(dot(n, t3DetailSun.xyz), 0.0) * vis) * t3DetailSun.w;
    const float3 shadeRelief = sunAmb(t3ReliefLight.x, t3ReliefLight.y * max(dot(n, t2SunDir.xyz), 0.0) * vis) * t3ReliefLight.z;
    const float3 shade = lerp(shadeFlat, shadeRelief, reliefW);
    float3 rgb = satur(albedo) * shade + albedo * pointLights(i.wpos, n, 0.3);
    float s2 = 0.0;
    if (t2Fog2.w > 0.5 && t2Fog2.w < 1.5) s2 = T2Shroud.Sample(T2ShroudSamp, p / mapSize).a;
    const float2 ed = min(p, mapSize - p);
    if (t2Fog2.w < 1.5) s2 = max(s2, 1.0 - smoothstep(0.0, 8.0, min(ed.x, ed.y)));
    const float dist = distance(i.wpos, t2Fog.xyz);
    const float f = smoothstep(t2Fog.w, t2Fog2.x, dist);
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
    const float amb = rLight.x * (0.78 + 0.22 * n.y) * lerp(0.62, 1.0, smoothstep(0.0, 0.55, i.ao));
    float3 rgb = satur(albedo) * sunAmb(amb, rLight.y * ndl * vis) * rLight.z + albedo * pointLights(i.wpos, n, 0.25);
    const float2 p = i.wpos.xz, mapSize = rFog2.yz;
    float s = 0.0;
    if (rFog2.w > 0.5 && rFog2.w < 1.5) s = RockShroud.Sample(RockShroudSamp, p / mapSize).a;
    const float2 e = min(p, mapSize - p);
    if (rFog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    if (s > 0.996) discard;
    const float f = smoothstep(rFog.w, rFog2.x, distance(i.wpos, rFog.xyz));
    rgb = lerp(rgb, rFogColor.rgb, f) * (1.0 - s);
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
    float4 tmC;  // highlight desaturation
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
            c = lerp(t, float3(mm, mm, mm), w);
        }
    }
    return float4(tmB.w > 0.5 ? c : tmToSrgb(c), 1.0);
}
