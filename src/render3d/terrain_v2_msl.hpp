#pragma once
// MSL shaders of the designed terrain (Graphics: 3D, "3D terrain: Designed"; render3d/terrain_v2.*,
// docs/3d-mode.md "Terrain v2"). Slot rules as render3d/shaders_msl.hpp. HLSL twin: the
// "designed terrain" part of render3d/shaders.hlsl - change both together.
//
// terrain2_fs (with terrain_vs): splat-blended tiling CC0 materials. Per pixel: the material
//   weights (two RGBA8 splat textures, 8 texels per cell, blurred, with a noisy boundary) + spice
//   (a per-cell texture read as an organic field: spiceField) -> height-blended (each material's
//   displacement in its albedo alpha), anti-tiling (the pattern switches between offset copies by
//   a low-frequency noise), steep rock triplanar, normal-mapped, colour-locked to the map's own
//   tile colours (tint per material), a large-scale brightness variation; lit, shrouded and
//   hazed as terrain_fs.
// rock_vs / rock_fs: instanced rock-scan meshes along the cliffs (textured, colour-locked).

#include "render3d/lights_msl.hpp"

namespace d2k::render3d::msl {

inline constexpr const char* kTerrain2 = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VOut {
    float4 pos [[position]];
    float3 wpos;
    float3 nrm;
    float sun;
};

struct FU {
    float4 sunDir;    // xyz towards the sun
    float4 light;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 fogColor;  // rgb, a = unused
    float4 fog;       // eye xyz, fog start
    float4 fog2;      // fog end, map width px, map height px, shroud on
    float4 tint[8];   // per material: rgb colour lock (per channel), a = world px per texture repeat
    float4 lum[8];    // per material: rgb colour lock by luminance (target / mean luminance), a = normal strength
    float4 params;    // x unused, y height-blend depth, z macro variation, w luminance share of the lock
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


float hash12(float2 p) {
    float3 p3 = fract(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(float2 p) {
    const float2 i = floor(p), f = fract(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + float2(1, 0)), u.x), mix(hash12(i + float2(0, 1)), hash12(i + float2(1, 1)), u.x), u.y);
}

struct MatS {
    float4 c;   // albedo rgb, height a
    float3 n;   // tangent-space normal (x right, y up in the image, z out)
};

// One projection of one material without visible repetition (after Inigo Quilez, "texture
// repetition", technique 3): a noise value picks between 8 offset copies of the texture and the
// transition is blended. Gradients come from the caller (uniform control flow).
MatS sampleTiled(texture2d_array<float> alb, texture2d_array<float> nor, sampler s, float2 uv, uint layer,
                 float2 gx, float2 gy, float k) {
    const float l = k * 8.0;
    const float i = floor(l), f = l - i;
    const float2 oa = sin(float2(3.0, 7.0) * i), ob = sin(float2(3.0, 7.0) * (i + 1.0));
    const float4 ca = alb.sample(s, uv + oa, layer, gradient2d(gx, gy));
    const float4 cb = alb.sample(s, uv + ob, layer, gradient2d(gx, gy));
    const float4 na = nor.sample(s, uv + oa, layer, gradient2d(gx, gy));
    const float4 nb = nor.sample(s, uv + ob, layer, gradient2d(gx, gy));
    const float b = smoothstep(0.2, 0.8, f - 0.1 * dot(ca.rgb - cb.rgb, float3(1.0)));
    MatS m;
    m.c = mix(ca, cb, b);
    m.n = mix(na.xyz, nb.xyz, b) * 2.0 - 1.0;
    return m;
}

// Spice at p, ORGANIC (no cell grid): x = presence (0 none .. 1 thin spice or more), y = richness
// (0 thin .. 1 the core of thick spice). The texture holds per cell the presence (the amount up
// to thin) and the richness blurred over two cells (render3d/terrain_v2.cpp buildSpiceTex); both
// are read through a three-scale noise warp, the presence with a round nine-tap kernel, and the
// richness is broken into clumps - thick spice lies in soft irregular patches with denser cores
// fading out. It still is the simulated amount: a harvested cell thins out and clears. (Reach:
// presence under two cells, richness about three - the dirty margin of render3d/terrain_bake.cpp.)
float2 spiceField(texture2d<float> spiceMap, sampler spiceSamp, float2 p, float2 mapSize) {
    const float2 q = p + (float2(vnoise(p / 45.0), vnoise(p / 45.0 + 31.7)) - 0.5) * 26.0 +
                     (float2(vnoise(p / 17.0 + 5.3), vnoise(p / 17.0 + 71.1)) - 0.5) * 14.0 +
                     (float2(vnoise(p / 6.5 + 11.7), vnoise(p / 6.5 + 3.9)) - 0.5) * 5.0;
    const float o = 18.0, od = 13.0;
    const float2 c = spiceMap.sample(spiceSamp, q / mapSize).rg;
    const float presence = saturate((c.r * 0.28 +
                                     (spiceMap.sample(spiceSamp, (q + float2(o, 0.0)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q - float2(o, 0.0)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q + float2(0.0, o)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q - float2(0.0, o)) / mapSize).r) * 0.12 +
                                     (spiceMap.sample(spiceSamp, (q + float2(od, od)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q - float2(od, od)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q + float2(od, -od)) / mapSize).r +
                                      spiceMap.sample(spiceSamp, (q - float2(od, -od)) / mapSize).r) * 0.06) * 1.004);
    // Thick spice: the blurred richness (0.5 = thick everywhere) broken into clumps - dense
    // cores, thin spice between them.
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
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
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
float3 concreteSlab(float2 p, float3 base, thread float3& nrm, float4 look0, float4 look1, float px) {
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
        groove = (1.0 - smoothstep(0.014, 0.014 + aa, abs(side - 0.13))) * step(fract(along * 7.0), 0.55) *
                 step(0.15, along) * step(along, 0.85);
    }
    if (h3 > 1.0 - look1.z) {  // paint: a stripe along one edge, sometimes hazard-striped
        const float t = h4 < 0.5 ? f.y : f.x;
        float band = smoothstep(0.08, 0.08 + aa, t) * (1.0 - smoothstep(0.15, 0.15 + aa, t));
        if (h2 > 0.55) band *= step(0.5, fract((f.x + f.y) * 4.0));
        const float worn = smoothstep(0.2, 0.62, vnoise(p / 2.5) * 0.6 + vnoise(p / 9.0) * 0.4);
        const float3 paint = h1 < 0.8 ? float3(0.74, 0.58, 0.2) : float3(0.8, 0.8, 0.78);
        col = mix(col, paint * (0.9 + 0.2 * h4), band * worn * 0.75);
    }
    col *= 1.0 - 0.14 * plate;
    // Weathering: grime, oil / scorch stains, chipped corners.
    const float weather = look1.y;
    const float dirt = vnoise(p / 11.0) * 0.6 + vnoise(p / 3.5) * 0.4;
    col *= 1.0 - weather * 0.2 * smoothstep(0.4, 0.85, dirt);
    const float stain = smoothstep(0.6, 0.85, vnoise(p / 6.0 + h1 * 37.0) * 0.75 + vnoise(p / 2.2) * 0.25) *
                        step(h4, 0.12 + 0.25 * weather);
    col = mix(col, col * float3(0.5, 0.48, 0.46), stain * 0.5);
)MSL" R"MSL(    const float2 cq2 = 0.5 - q;  // corner chips
    const float chip = step(cq2.x + cq2.y, 0.05 + 0.05 * weather * vnoise(p / 1.5 + h3 * 11.0)) * step(0.5, h2 + 0.3 * weather);
    // Sand: in the seams and grooves, drifting onto the slab edges in patches.
    const float drift = look1.w * smoothstep(0.3, 0.78, vnoise(p / 7.0 + 3.1) * 0.7 + vnoise(p / 2.0) * 0.3);
    const float gap = max(max(seam, groove), chip);
    col *= 1.0 - 0.55 * gap;
    const float sand = saturate(gap * drift * 0.8 + bev * drift * 0.35 + chip * 0.3);
    return mix(col, look0.rgb * (0.92 + 0.1 * dirt), sand);
}

fragment float4 terrain2_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                            texture2d_array<float> alb [[texture(0)]], texture2d_array<float> nor [[texture(1)]],
                            texture2d<float> splatA [[texture(2)]], texture2d<float> splatB [[texture(3)]],
                            texture2d<float> spiceMap [[texture(4)]], texture2d<float> shroud [[texture(5)]],
                            depth2d<float> shadowMap [[texture(6)]],
                            sampler matSamp [[sampler(0)]], sampler matSamp2 [[sampler(1)]],
                            sampler splatSamp [[sampler(2)]], sampler splatSamp2 [[sampler(3)]],
                            sampler spiceSamp [[sampler(4)]], sampler shroudSamp [[sampler(5)]],
                            sampler shadowSamp [[sampler(6)]]) {
    const float2 p = in.wpos.xz;
    const float2 mapSize = u.fog2.yz;
    // Derivatives in uniform control flow (the material loop branches).
    const float3 dpx = dfdx(in.wpos), dpy = dfdy(in.wpos);
    const float3 gn = normalize(in.nrm);

    float w[7];
    const float4 a = splatA.sample(splatSamp, p / mapSize);
    const float4 b = splatB.sample(splatSamp2, p / mapSize);
    w[0] = a.x; w[1] = a.y; w[2] = a.z; w[3] = a.w; w[4] = b.x; w[6] = 0.0; w[5] = 0.0;
    // Concrete: a binary per-cell channel, thresholded at 0.5 (exactly the cell edges) with a
    // one-pixel ramp: crisp slab borders, blended over the other materials afterwards.
    const float cfw = max(fwidth(b.y), 1e-3);
    const float cw = smoothstep(0.5 - cfw, 0.5 + cfw, b.y);
    const float pxCells = max(length(dpx.xz), length(dpy.xz)) / 32.0;
    // Spice: the organic field (spiceField), broken into grains; thick spice denser and darker.
    // amount = the equivalent amount (thin 2 / 7, thick 4 / 7), as the per-cell texture gave it.
    const float2 sf = spiceField(spiceMap, spiceSamp, p, mapSize);
    const float amount = (sf.x + sf.y) * (2.0 / 7.0);
    const float grains = vnoise(p / 5.0) * 0.6 + vnoise(p / 15.0) * 0.4;
    // The edge of a field dissolves: the presence against a noise threshold - lobes and specks
    // thinning out into the sand, no line.
    const float edgeN = vnoise(p / 11.0 + 4.0) * 0.5 + vnoise(p / 4.0 + 8.0) * 0.3 + vnoise(p / 1.7) * 0.2;
    const float cover = smoothstep(-0.12, 0.2, sf.x - (0.15 + 0.6 * edgeN));
    float spice = mix(cover * 0.78, 1.0, sf.y) * smoothstep(1.0, 0.55, grains - amount * 0.3) * 0.92;
    spice *= saturate((w[0] + w[1]) * 1.6);
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    const bool steep = gn.y < 0.82;
    const float3 tw = pow(abs(gn), float3(4.0));
    const float3 tri = tw / (tw.x + tw.y + tw.z);
    float4 c[7];
    float3 nn[7];
    float hmax = -1.0;
    for (int i = 0; i < 7; ++i) {
        c[i] = float4(0.0);
        nn[i] = float3(0.0);
        if (w[i] < 0.004) continue;
        const float sc = 1.0 / u.tint[i].a;
        MatS m = sampleTiled(alb, nor, matSamp, p * sc, uint(i), dpx.xz * sc, dpy.xz * sc, k);
        float3 n = gn + float3(m.n.x, 0.0, -m.n.y) * u.lum[i].a;
        if (steep && (i == 2 || i == 3 || i == 4)) {
            // Rock on steep slopes: add the two side projections (triplanar).
            const MatS mx = sampleTiled(alb, nor, matSamp, float2(in.wpos.z, -in.wpos.y) * sc, uint(i),
                                        float2(dpx.z, -dpx.y) * sc, float2(dpy.z, -dpy.y) * sc, k);
            const MatS mz = sampleTiled(alb, nor, matSamp, float2(in.wpos.x, -in.wpos.y) * sc, uint(i),
                                        float2(dpx.x, -dpx.y) * sc, float2(dpy.x, -dpy.y) * sc, k);
            m.c = m.c * tri.y + mx.c * tri.x + mz.c * tri.z;
            n = gn + (float3(m.n.x, 0.0, -m.n.y) * tri.y + float3(0.0, mx.n.y, mx.n.x) * tri.x +
                      float3(mz.n.x, mz.n.y, 0.0) * tri.z) * u.lum[i].a;
        }
        c[i] = m.c;
        nn[i] = n;
        hmax = max(hmax, m.c.a + w[i]);
    }
    // Height blend: where two materials meet, the higher texels win (sand fills the cracks).
    const float depth = u.params.y;
    float3 albedo = float3(0.0), n = float3(0.0);
    float sum = 0.0;
    for (int i = 0; i < 7; ++i) {
        if (w[i] < 0.004) continue;
        const float bw = max(c[i].a + w[i] - hmax + depth, 0.0);
        const float L = dot(c[i].rgb, float3(0.299, 0.587, 0.114));
        albedo += mix(c[i].rgb * u.tint[i].rgb, L * u.lum[i].rgb, u.params.w) * bw;
        n += nn[i] * bw;
        sum += bw;
    }
    albedo /= max(sum, 1e-4);
    n = normalize(sum > 0.0 ? n : gn);
    if (cw > 0.004) {  // concrete slabs over the rest
        const float sc = 1.0 / u.tint[6].a;
        MatS m = sampleTiled(alb, nor, matSamp, p * sc, 6u, dpx.xz * sc, dpy.xz * sc, k);
        float3 cn = gn + float3(m.n.x, 0.0, -m.n.y) * u.lum[6].a;
        const float L = dot(m.c.rgb, float3(0.299, 0.587, 0.114));
        float3 cc = mix(m.c.rgb * u.tint[6].rgb, L * u.lum[6].rgb, u.params.w);
        cc = concreteSlab(p, cc, cn, u.tint[7], u.lum[7], pxCells);
        const float t = sum > 0.0 ? cw : 1.0;
        albedo = mix(albedo, cc, t);
        n = normalize(mix(n, normalize(cn), t));
    }
    // Darker spice mounds (the original's round spots), bigger and more of them in thick spice.
    const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
    albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
    albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick
    // Large-scale variation (no two dunes the same shade).
    const float macro = vnoise(p / 700.0) * 0.6 + vnoise(p / 230.0 + 5.0) * 0.4;
    albedo *= 1.0 + u.params.z * (macro - 0.5);

    const float ndl = max(dot(n, u.sunDir.xyz), 0.0);
    const float sm = u.shadow.x > 0.5 ? shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow) : in.sun;
    const float vis = mix(u.light.w, 1.0, sm);
    const float3 shade = sunAmb(pl, u.light.x, u.light.y * ndl * vis) * u.light.z;
    float3 rgb = satur(pl, albedo) * shade + albedo * pointLights(pl, in.wpos, n, 0.3);
    float s = 0.0;
    // fog2.w: 0 no shroud, 1 shroud, 2 baked for the Enhanced 2D view (no shroud, no edge fade).
    if (u.fog2.w > 0.5 && u.fog2.w < 1.5) s = shroud.sample(shroudSamp, p / mapSize).a;
    const float2 e = min(p, mapSize - p);
    if (u.fog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    const float d = distance(in.wpos, u.fog.xyz);
    const float f = smoothstep(u.fog.w, u.fog2.x, d);
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

inline constexpr const char* kRock = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 i0 [[attribute(3)]];  // instance: position xyz, yaw (radians)
    float4 i1 [[attribute(4)]];  // scale xyz, texture layer
    float4 i2 [[attribute(5)]];  // sun visibility, ground y, top y, brightness
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
};
struct VU {
    float4x4 viewProj;
};
vertex VOut rock_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]]) {
    const float c = cos(in.i0.w), s = sin(in.i0.w);
    const float3 q = in.pos * in.i1.xyz;
    const float3 w = float3(c * q.x - s * q.z, q.y, s * q.x + c * q.z) + in.i0.xyz;
    const float3 m = in.nrm / in.i1.xyz;  // inverse-transpose of the scale
    VOut o;
    o.pos = u.viewProj * float4(w, 1.0);
    o.wpos = w;
    o.nrm = float3(c * m.x - s * m.z, m.y, s * m.x + c * m.z);
    o.uv = in.uv;
    o.layer = in.i1.w;
    o.sun = in.i2.x;
    o.ao = saturate((w.y - in.i2.y) / max(in.i2.z - in.i2.y, 1.0));
    o.bright = in.i2.w;
    return o;
}

struct FU {
    float4 sunDir;
    float4 light;     // ambient, diffuse, 1 / flat-ground light, darkest shadow factor
    float4 fogColor;
    float4 fog;       // eye xyz, fog start
    float4 fog2;      // fog end, map width px, map height px, shroud on
    float4 tint[8];   // per rock layer: rgb colour lock
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

fragment float4 rock_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                        texture2d_array<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                        depth2d<float> shadowMap [[texture(2)]],
                        sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]],
                        sampler shadowSamp [[sampler(2)]]) {
    const uint layer = uint(in.layer + 0.5);
    // Recoloured by luminance to the map's cliff colour (tint.rgb = target / mean luminance,
    // a = mean luminance), contrast eased, a tenth of the scan's own (greyish) hue kept.
    const float3 t = tex.sample(texSamp, in.uv, layer).rgb;
    const float L = dot(t, float3(0.299, 0.587, 0.114));
    const float Lc = mix(u.tint[layer].a, L, 0.8);
    const float3 albedo = u.tint[layer].rgb * Lc * mix(float3(1.0), t / max(L, 0.02), 0.1) * in.bright;
    const float3 n = normalize(in.nrm);
    const float ndl = max(dot(n, u.sunDir.xyz), 0.0);
    const float sm = u.shadow.x > 0.5 ? shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow) : in.sun;
    const float vis = mix(u.light.w, 1.0, sm);
    // Sky light brighter on top faces, contact darkening near the ground.
    const float amb = u.light.x * (0.78 + 0.22 * n.y) * mix(0.62, 1.0, smoothstep(0.0, 0.55, in.ao));
    float3 rgb = satur(pl, albedo) * sunAmb(pl, amb, u.light.y * ndl * vis) * u.light.z + albedo * pointLights(pl, in.wpos, n, 0.25);
    const float2 p = in.wpos.xz, mapSize = u.fog2.yz;
    float s = 0.0;
    // fog2.w: 0 no shroud, 1 shroud, 2 baked for the Enhanced 2D view (no shroud, no edge fade).
    if (u.fog2.w > 0.5 && u.fog2.w < 1.5) s = shroud.sample(shroudSamp, p / mapSize).a;
    const float2 e = min(p, mapSize - p);
    if (u.fog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(e.x, e.y)));
    if (s > 0.996) discard_fragment();
    const float f = smoothstep(u.fog.w, u.fog2.x, distance(in.wpos, u.fog.xyz));
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

// Depth-only pass of the sun shadow map (terrain, rocks, models): no colour output.
inline constexpr const char* kDepth = R"MSL(
#include <metal_stdlib>
using namespace metal;
fragment void depth_fs() {}
)MSL";

}  // namespace d2k::render3d::msl
