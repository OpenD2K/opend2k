#pragma once
// MSL shaders of the 3D model pass (render3d/models.*), slot rules as render3d/shaders_msl.hpp.
// HLSL twin (SPIR-V / DXIL): the "models" part of render3d/shaders.hlsl - change both together.
// model_vs/model_fs: lit, textured meshes (house colour, shroud, fog, flash, alpha);
// shadow_fs: the flat shadow polygons as a darkening of what is below; model_shadow_vs: a glTF
// mesh flattened onto its ground plane along the sun (projected shadow, drawn with shadow_fs).
// Both vertex shaders skin animated models from the joint palette (storage buffer 0 = [[buffer(1)]]).

#include "render3d/lights_msl.hpp"

namespace d2k::render3d::msl {

inline constexpr const char* kModel = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" D2K_MSL_POINT_LIGHTS R"MSL(

struct VIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 color [[attribute(3)]];
    float4 params [[attribute(4)]];  // x house (0..1 tint, 2 solid), y lit, z textured, w one-sided
    float2 pbr [[attribute(5)]];     // x gloss (1 - roughness; > 0 marks glTF shading), y metallic
    float4 joints [[attribute(6)]];  // skinning: palette entries / 255 (UBYTE4_NORM)
    float4 weights [[attribute(7)]]; // their weights (sum 1; all 0 = rigid)
    float4 apart [[attribute(8)]];   // Assemble part (render3d/model_assembly.hpp): foot xyz, param
    float4 atime [[attribute(9)]];   // t0, dur, motion * 32 / 255, (1 + direction 0..254) / 255 (0 = no part)
};
struct VOut {
    float4 pos [[position]];
    float3 wpos;
    float3 nrm;
    float2 uv;
    float4 color;
    float4 params;
    float4 pbr;
};
// One block for model_vs and model_shadow_vs (the HLSL twin shares one cbuffer).
struct VU {
    float4x4 viewProj;
    float4x4 model;
    float4 sun;    // model_shadow_vs: xyz towards the sun
    float4 plane;  // model_shadow_vs: ground plane, xyz normal, w = dot(normal, point on it)
    float4 skin;   // x = first palette entry of this draw (matrices), < 0 = not skinned;
                   // y = drop-in unfold 1 + t (0 = off; render/building_dropin.hpp), z = the model's height (px),
                   // w = Assemble progress 1 + p (0 = off; render/building_assemble.hpp)
    float4 drive;  // running gear (render3d/model_drive.hpp): x = 1 on, y = odometer (px), z = spin (radians, + clockwise)
};
// GPU skinning (docs/3d-models.md "Animation"): the palette is a storage buffer of column-major
// matrices (4 float4 columns each); a vertex = the weighted sum of its (up to) four joints' matrices
// applied to it (v.w = 1 points, 0 directions).
float4 skinned(const device float4* pal, float4 skin, float4 j, float4 w, float4 v) {
    if (skin.x < 0.0 || w.x + w.y + w.z + w.w < 0.001) return v;
    const int base = int(skin.x + 0.5);
    float4 r = float4(0.0);
    for (int k = 0; k < 4; ++k) {
        if (w[k] <= 0.0) continue;
        const int i = (base + int(j[k] * 255.0 + 0.5)) * 4;
        r += w[k] * (pal[i] * v.x + pal[i + 1] * v.y + pal[i + 2] * v.z + pal[i + 3] * v.w);
    }
    return r;
}
// Drop-in unfold (render/building_dropin.hpp, docs/3d-mode.md "Drop-in build-up"): the model is cut
// into four height bands; folded, each keeps 28 % of its height, and they extend in turn, the lowest
// first (eased, with a small overshoot) - a telescoping unfold. y = model-space height above the
// ground; k = the vertical scale of the vertex's band (normals: y / k).
float unfoldY(float y, float4 skin, thread float& k) {
    k = 1.0;
    if (skin.y < 0.5 || y <= 0.0) return y;
    const float t = skin.y - 1.0;
    const float bh = max(skin.z, 1.0) * 0.25;
    float acc = 0.0;
    for (int i = 0; i < 4; ++i) {
        const float e = clamp((t - float(i) * 0.16) / 0.52, 0.0, 1.0) - 1.0;
        const float eb = 1.0 + 2.3 * e * e * e + 1.3 * e * e;  // ease-out-back
        const float ki = mix(0.28, 1.0, eb);
        const float lo = float(i) * bh;
        if (y < lo + bh || i == 3) {
            k = ki;
            return acc + (y - lo) * ki;
        }
        acc += bh * ki;
    }
    return y;
}
// Assemble build-up (render/building_assemble.hpp, docs/3d-mode.md "Assemble build-up"): the model's
// parts arrive one after another (render3d/model_assembly.*: per vertex its part's foot, a parameter,
// its window [t0, t0 + dur] of the progress, how it moves, an outward direction). Before its window a
// part is collapsed onto its foot (not drawn), after it it stands. edge = 1 + px below a glowing cut
// (0 = none), seam = the weld glow at the foot of a part that just seated.
struct Assembled {
    float3 p, n;
    float edge, seam;
};
float easeBack(float t, float s) {
    const float u = t - 1.0;
    return 1.0 + (s + 1.0) * u * u * u + s * u * u;
}
Assembled assemble(float3 p, float3 n, float4 pa, float4 pt, float prog) {
    Assembled o;
    o.p = p;
    o.n = n;
    o.edge = 0.0;
    o.seam = 0.0;
    if (pt.w <= 0.0) return o;  // not a part (units, models without a plan)
    const float s = clamp((prog - pt.x) / max(pt.y, 0.004), 0.0, 1.0);
    const float3 foot = pa.xyz;
    if (s >= 1.0) {  // seated: a short weld glow round its foot
        const float after = (prog - pt.x - pt.y) / 0.05;
        if (after < 1.0) o.seam = 0.85 * (1.0 - max(after, 0.0)) * saturate(1.0 - (p.y - foot.y) / 3.0);
        return o;
    }
    if (s <= 0.0) {  // waiting: collapsed onto its foot
        o.p = foot;
        return o;
    }
    const int motion = int(pt.z * 255.0 / 32.0 + 0.5);
    const float a = (pt.w * 255.0 - 1.0) / 254.0 * 6.2831853;
    const float2 dir = float2(cos(a), sin(a));
    if (motion == 0) {  // extrude out of its foot behind a glowing cut
        const float e = 1.0 - (1.0 - s) * (1.0 - s);
        const float cut = foot.y + pa.w * e;
        o.p.y = min(p.y, cut);
        o.edge = 1.0 + (cut - o.p.y);
    } else if (motion == 1) {  // lowered into place, a small bounce as it sets down
        const float g = smoothstep(0.0, 0.35, s);
        o.p = foot + (p - foot) * mix(0.6, 1.0, g);
        o.p.y += pa.w * pow(1.0 - s, 2.2) + pa.w * 0.07 * sin(clamp((s - 0.7) / 0.3, 0.0, 1.0) * 3.14159265);
    } else if (motion == 2) {  // slides out of the core towards its place
        const float e = easeBack(s, 1.2);
        o.p.xz -= dir * (pa.w * (1.0 - e));
        o.p = foot + (o.p - foot) * mix(0.7, 1.0, min(s * 2.5, 1.0));
    } else if (motion == 3) {  // swings up on a hinge at its foot, from lying outward
        const float ang = (1.0 - easeBack(s, 0.9)) * 1.5707963;
        const float c = cos(ang), sn = sin(ang);
        const float3 d = p - foot;
        const float r = dot(d.xz, dir);
        const float r2 = r * c + d.y * sn, y2 = -r * sn + d.y * c;
        o.p = float3(foot.x + d.x + dir.x * (r2 - r), foot.y + y2, foot.z + d.z + dir.y * (r2 - r));
        const float nr = dot(n.xz, dir);
        const float nr2 = nr * c + n.y * sn, ny2 = -nr * sn + n.y * c;
        o.n = float3(n.x + dir.x * (nr2 - nr), ny2, n.z + dir.y * (nr2 - nr));
    } else {  // telescopes out of the surface it stands on, a hot ring where it comes out
        const float e = easeBack(s, 0.8);
        o.p.y = max(p.y - pa.w * (1.0 - e), foot.y);
        o.edge = 1.0 + (o.p.y - foot.y);
    }
    return o;
}

// Running gear (render3d/model_drive.hpp, docs/3d-models.md "Wheels and tracks"): per vertex its wheel /
// track link (apart; atime = kind, lateral position hi / lo bytes, 0). The distance this side of the hull
// drove = odometer - spin * x; a wheel turns about its axle by -distance / radius, a track link moves
// fract(distance / pitch) of the way to the next link (rotation about a pivot, or a shift).
void driveGear(thread float3& p, thread float3& n, float4 a, float4 t, float4 drive) {
    if (drive.x < 0.5 || t.w > 0.0) return;
    const int kind = int(t.x * 255.0 + 0.5);
    if (kind < 1 || kind > 3) return;
    const float x = (t.y * 255.0 * 256.0 + t.z * 255.0) / 256.0 - 128.0;
    const float dist = drive.y - drive.z * x;
    if (kind == 3) {
        p.yz += fract(dist / a.w) * a.xy;
        return;
    }
    const float ang = kind == 1 ? -dist / max(a.z, 0.01) : fract(dist / a.w) * a.z;
    const float c = cos(ang), s = sin(ang);
    const float2 d = p.yz - a.xy;
    p.yz = a.xy + float2(c * d.x - s * d.y, s * d.x + c * d.y);
    n.yz = float2(c * n.y - s * n.z, s * n.y + c * n.z);
}

)MSL" R"MSL(vertex VOut model_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]], const device float4* pal [[buffer(1)]]) {
    VOut o;
    o.pbr = float4(in.pbr, 0.0, 0.0);
    if (u.skin.w > 0.5) {  // Assemble: the parts move (pbr.z = cut edge, pbr.w = seam glow for the fragment shaders)
        const float4 m = skinned(pal, u.skin, in.joints, in.weights, float4(in.pos, 1.0));
        const float4 mn = skinned(pal, u.skin, in.joints, in.weights, float4(in.nrm, 0.0));
        const Assembled as = assemble(m.xyz, mn.xyz, in.apart, in.atime, u.skin.w - 1.0);
        const float4 w = u.model * float4(as.p, 1.0);
        o.pos = u.viewProj * w;
        o.wpos = w.xyz;
        o.nrm = (u.model * float4(as.n, 0.0)).xyz;
        o.pbr.zw = float2(as.edge, as.seam);
    } else if (u.skin.y > 0.5) {  // drop-in unfold (the plain path below stays exactly as before)
        float4 m = skinned(pal, u.skin, in.joints, in.weights, float4(in.pos, 1.0));
        float k;
        m.y = unfoldY(m.y, u.skin, k);
        const float4 w = u.model * m;
        o.pos = u.viewProj * w;
        o.wpos = w.xyz;
        float4 mn = skinned(pal, u.skin, in.joints, in.weights, float4(in.nrm, 0.0));
        mn.y /= k;
        o.nrm = (u.model * mn).xyz;
    } else {
        float3 p = in.pos, n = in.nrm;
        driveGear(p, n, in.apart, in.atime, u.drive);
        const float4 w = u.model * skinned(pal, u.skin, in.joints, in.weights, float4(p, 1.0));
        o.pos = u.viewProj * w;
        o.wpos = w.xyz;
        o.nrm = (u.model * skinned(pal, u.skin, in.joints, in.weights, float4(n, 0.0))).xyz;
    }
    o.uv = in.uv;
    o.color = in.color;
    o.params = in.params;
    return o;
}

vertex VOut model_shadow_vs(VIn in [[stage_in]], constant VU& u [[buffer(0)]], const device float4* pal [[buffer(1)]]) {
    VOut o;
    float3 p = in.pos, gn = in.nrm;
    driveGear(p, gn, in.apart, in.atime, u.drive);
    float4 m = skinned(pal, u.skin, in.joints, in.weights, float4(p, 1.0));
    float k;
    m.y = unfoldY(m.y, u.skin, k);
    if (u.skin.w > 0.5) m.xyz = assemble(m.xyz, float3(0.0, 1.0, 0.0), in.apart, in.atime, u.skin.w - 1.0).p;
    float3 w = (u.model * m).xyz;
    const float3 n = u.plane.xyz;
    const float above = max(dot(n, w) - u.plane.w, 0.0);
    w -= u.sun.xyz * (above / max(dot(n, u.sun.xyz), 0.2));
    w = w - n * (dot(n, w) - u.plane.w) + n * 0.35;  // onto the plane, just above it
    o.pos = u.viewProj * float4(w, 1.0);
    o.wpos = w;
    o.nrm = n;
    o.uv = in.uv;
    o.color = in.color;
    o.params = in.params;
    o.pbr = float4(in.pbr, 0.0, 0.0);
    return o;
}

struct FU {
    float4 house;     // rgb house colour, a = flash
    float4 sunDir;    // xyz towards the sun, w = alpha
    float4 light;     // ambient, diffuse, 1 / flat-ground light, shroud on
    float4 fogColor;  // rgb, a = fog start
    float4 fog;       // eye xyz, fog end
    float4 map;       // map width, height (px), shadow alpha, emission strength (0: alpha is no emission)
    float4x4 lightVP; // sun shadow map
    float4 shadow;    // on, texel (uv), diffuse kept in full shadow, depth bias
    float4 walk;      // walkable cells: x0, z0 of the footprint's top-left cell (world px), cell mask (bit y*4+x), min height
    float4 walkMode;  // x: 0 everything, 1 all but the walkable cells' parts, 2 only them (docs/3d-models.md "Walkable cells");
                      // build-up (render/buildup_fx.hpp): y = cut height (world), z = glow band (px, 0 = none), w = scan drift
    float4 gplane;    // the model's ground plane: xyz normal, w = dot(normal, a point on it) (Realistic grade: sand dust)
    float4 dust;      // sand dust (docs/3d-mode.md "Realistic grade"): x = 1 / the foot band's height (px; 0 = none), y = share;
                      // zw: vehicle wrecks (lights_msl.hpp wreckChar; 0, 0 = a live model)
};
// A building's part above one of its walkable cells (higher than its floor)?
bool walkPart(float3 wpos, float4 walk) {
    const float2 c = floor((wpos.xz - walk.xy) / 32.0);
    if (any(c < float2(0.0)) || any(c > float2(3.0)) || wpos.y <= walk.w) return false;
    return ((uint(walk.z) >> (uint(c.y) * 4u + uint(c.x))) & 1u) != 0u;
}
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

fragment float4 model_fs(VOut in [[stage_in]], bool front [[front_facing]], constant FU& u [[buffer(0)]],
                         constant PointLights& pl [[buffer(1)]], const device uint4* plg [[buffer(2)]],
                         texture2d<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                         depth2d<float> shadowMap [[texture(2)]], texture2d<float> ssaoTex [[texture(3)]],
                         sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]],
                         sampler shadowSamp [[sampler(2)]], sampler ssaoSamp [[sampler(3)]]) {
    if (in.params.w > 0.5 && !front) discard_fragment();  // PS1 back curtains: only from behind
    if (u.walkMode.x > 0.5 && walkPart(in.wpos, u.walk) != (u.walkMode.x > 1.5)) discard_fragment();
    float4 c = in.color;
    float emit = 0.0;  // glTF emission folded into the texture alpha (docs/3d-models.md "Emission")
    if (in.params.z > 0.5) {
        const float4 t = tex.sample(texSamp, in.uv);
        if (t.a < 0.5) discard_fragment();
        c.rgb *= t.rgb;
        if (u.map.w > 0.0) emit = saturate((1.0 - t.a) * 2.0) * u.map.w;
    }
    // Build-up / selling: the part above the cut is a hologram of cyan scan lines, a warm band glows below it.
    float band = 0.0;
    if (u.walkMode.z > 0.0) {
        const float d = u.walkMode.y - in.wpos.y;
        if (d < 0.0) {
            if (fract((in.wpos.y - u.walkMode.w) / 3.0) > 0.25) discard_fragment();
            return float4(0.12, 0.6, 1.0, 0.4 * u.sunDir.w);
        }
        band = saturate(1.0 - d / u.walkMode.z);
    }
    if (in.pbr.z > 0.5) band = max(band, 0.55 * saturate(1.0 - (in.pbr.z - 1.0) / 2.5));  // Assemble: the cut edge ...
    band = max(band, in.pbr.w);                                                   // ... and the weld seams
    const float3 glow = c.rgb / max(max(c.r, max(c.g, c.b)), 0.05);  // the albedo's hue at full brightness
    // Realistic grade (docs/3d-mode.md): the albedo darker; the house colour keeps its strength (an accent).
    const float gk = pl.grade.x > 0.0 ? pl.grade.x : 1.0;
    c.rgb *= gk;
    if (in.params.x > 1.5) {
        c.rgb = u.house.rgb;
    } else if (in.params.x > 0.0) {
        const float l = dot(c.rgb, float3(0.299, 0.587, 0.114)) / gk;
        c.rgb = mix(c.rgb, clamp(u.house.rgb * l * 1.8, 0.0, 1.0), in.params.x);
    }
    float ember = 0.0;  // vehicle wrecks: a hulk is burnt black-brown, embers glow in it
    if (u.dust.z > 0.0) {
        const float4 wk = wreckChar(c.rgb, u.dust.zw, in.params.z > 0.5 ? in.uv * 55.0 : in.wpos.xz * 0.9 + in.wpos.y * 0.7, 0.0);
        if (wk.a < 0.0) discard_fragment();
        c.rgb = wk.rgb;
        ember = wk.a;
    }
    if (in.params.y > 0.5) {
        float3 n = normalize(in.nrm);
        if (!front) n = -n;  // two-sided: the inside of a face is lit as its outside
        // ... and fine sand dust settled on the up-facing surfaces and over the model's foot.
        const float dk = sandDustK(pl, in.uv * 61.0, n.y, dot(u.gplane.xyz, in.wpos) - u.gplane.w, u.dust) *
                         (1.0 - 0.5 * saturate(in.params.x));
        if (dk > 0.0) c.rgb = mix(c.rgb, pl.dustColor.rgb, dk);
        float ndl = max(dot(n, u.sunDir.xyz), 0.0);
        if (u.shadow.x > 0.5)
            ndl *= mix(u.shadow.z, 1.0, shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow));
        const float3 base = c.rgb;
        c.rgb = satur(pl, c.rgb);  // the time of day's saturation (night: towards grey) before the light
        const float sao = ssaoAt(pl, ssaoTex, ssaoSamp, in.pos.xy);  // ambient occlusion: the sky light only
        if (in.pbr.x > 0.0) {
            // glTF models: sky ambient (brighter facing up) + a soft fill light from the side
            // opposite the sun, so faces turned away from the sun keep their shape (the PC art
            // is pre-rendered with fill lights too).
            const float3 fillDir = normalize(float3(-u.sunDir.x, 0.25, -u.sunDir.z));
            const float amb = u.light.x * (0.72 + 0.18 * n.y) + u.light.x * 0.4 * max(dot(n, fillDir), 0.0);
            c.rgb *= sunAmb(pl, amb * sao, u.light.y * ndl) * u.light.z * (1.0 - 0.2 * in.pbr.y);
        } else {
            c.rgb *= sunAmb(pl, u.light.x * sao, u.light.y * ndl) * u.light.z;
        }
        if (in.pbr.x > 0.0) {  // glTF materials (pbr.x > 0): a sun highlight (Blinn-Phong), tinted on metals
            const float3 h = normalize(u.sunDir.xyz + normalize(u.fog.xyz - in.wpos));
            const float sp = pow(max(dot(n, h), 0.0), 6.0 + 90.0 * in.pbr.x * in.pbr.x) * in.pbr.x * step(0.0, ndl);
            c.rgb += sp * (0.25 + 0.45 * in.pbr.y) * mix(float3(1.0), base, in.pbr.y) * pl.info.y * pl.sunColor.rgb;
        }
        c.rgb += base * pointLights(pl, plg, in.wpos, n, 0.25);  // dynamic lights
    } else if (in.params.y > 0.2) {  // flat-shaded parts: the time of day's light, like the billboards
        c.rgb = satur(pl, c.rgb) * pl.spriteTint.rgb + c.rgb * pointLights(pl, plg, in.wpos, float3(0.0, 1.0, 0.0), 0.8);
    }
    c.rgb *= dustHaze(pl, in.wpos);  // Reimagined: drifting dust veils
    c.rgb = min(c.rgb + u.house.a, 1.0);
    c.rgb += glow * emit;  // may exceed 1 (HDR: blooms)
    if (ember > 0.0) c.rgb += float3(2.6, 0.7, 0.12) * ember;
    c.rgb += float3(1.0, 0.55, 0.2) * (band * band * 1.6) + float3(1.0, 0.95, 0.85) * pow(band, 12.0);
    if (u.light.w > 0.5) c.rgb *= 1.0 - shroudAt(pl, shroud, shroudSamp, in.wpos.xz, u.map.xy);
    const float f = smoothstep(u.fogColor.a, u.fog.w, distance(in.wpos, u.fog.xyz)) * aerialK(pl);
    c.rgb = mix(c.rgb, u.fogColor.rgb, f);
    return float4(c.rgb, u.sunDir.w);
}

fragment float4 shadow_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]],
                          texture2d<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                          sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]]) {
    return float4(0.0, 0.0, 0.0, u.map.z);
}
)MSL";

}  // namespace d2k::render3d::msl
