#pragma once
// MSL of the "Cinematic" particles (render3d/cinematic_fx.*, docs/3d-mode.md "Reimagined"). HLSL twin:
// fx_vs / fx_fs in render3d/shaders.hlsl - change both together (and run tools/compile_shaders.sh).
//
// fx_vs: one quad per instance (6 vertices from vertex_id, no vertex buffer of its own):
//   puffs / debris / flashes face the camera, turned by their rotation; sparks are stretched along their
//   velocity as seen on the screen (the head at the particle); ground marks (scorch, shock ring) lie on the
//   terrain (the four corner heights come with the instance) pulled a little towards the eye.
// fx_fs: premultiplied alpha (ONE, ONE_MINUS_SRC_ALPHA). Everything is procedural, from one tileable 3D noise
//   texture (r = Perlin-Worley value, gba = its gradient):
//   - puffs (smoke, dust, fireballs): a sphere whose outline the noise pushes about and erodes (wisps, more as it
//     ages), a normal from that shape + the noise gradient, one plume lighting (docs/3d-mode.md "Lit smoke"): the
//     sun (wrapped, self-shadowed by the density towards the sun; soot swallows more), scattering in the thin edges
//     (a bright rim against the sun), the sky from above, the ground's bounce and the fires around from below;
//     fireballs add a temperature ramp (white-yellow core -> orange -> dark red -> soot) in HDR that cools with
//     `heat`;
//   - sparks / flashes / muzzle stars: light only (alpha 0 = additive);
//   - debris: a hard noisy chunk, lit, glowing while hot;
//   - scorch: a dark burnt radial mark with noise, embers while fresh; shock ring: a dust ring rolling out.
// Instance layout (8 x float4, CinematicFx::kFloats):
//   posSize (world xyz, size px) | velRot (velocity xyz, rotation) | albedo (rgb, alpha) | emit (rgb, mode) |
//   extra (ground height, streak length, soft fade px, heat 0..1) | light (nearby light rgb, age 0..1) |
//   aux (seed, noise scale, erosion, rays; puffs: w = soot 0 dust .. 1 black smoke) | corners (ground marks: corner
//   heights relative to posSize.y; puffs: x = ground dust 0..1 - no longer read by the shader: every puff has the
//   plume lighting; prepare() gives those puffs a ground shadow)
// Modes: 0 puff, 1 spark, 2 flash, 3 debris, 4 scorch, 5 ring, 6 ground dust shadow (a soft dark blob).

namespace d2k::render3d::msl {

inline constexpr const char* kCinematic = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FxIn {
    float4 posSize [[attribute(0)]];
    float4 velRot [[attribute(1)]];
    float4 albedo [[attribute(2)]];
    float4 emit [[attribute(3)]];
    float4 extra [[attribute(4)]];
    float4 light [[attribute(5)]];
    float4 aux [[attribute(6)]];
    float4 corners [[attribute(7)]];
};
struct FxOut {
    float4 pos [[position]];
    float2 q;
    float3 wpos;
    float3 ax;
    float3 ay;
    float3 vel;  // ground marks: the scorch rim's dust colour
    float4 albedo;
    float4 emit;
    float4 extra;
    float4 light;
    float4 aux;
    float dust;  // puffs: ground dust (corners.x); unused since every puff has the plume lighting
};
struct FxVU {
    float4x4 viewProj;
    float4 right;  // camera right (world)
    float4 up;     // camera up (world)
    float4 eye;
};
vertex FxOut fx_vs(FxIn in [[stage_in]], uint vid [[vertex_id]], constant FxVU& u [[buffer(0)]]) {
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 c = corners[vid % 6];
    const float mode = in.emit.w;
    const float3 R = u.right.xyz, U = u.up.xyz;
    float3 ax, ay, p;
    float2 q = c;
    float fade = 1.0;
    if (mode > 0.5 && mode < 1.5) {
        // spark: along the on-screen velocity, the head at the particle
        float2 v = float2(dot(in.velRot.xyz, R), dot(in.velRot.xyz, U));
        const float l = length(v);
        v = l > 1e-4 ? v / l : float2(1.0, 0.0);
        ax = R * v.x + U * v.y;
        ay = R * -v.y + U * v.x;
        const float len = max(in.extra.y, in.posSize.w);
        p = in.posSize.xyz + ax * ((c.x - 1.0) * 0.5 * len) + ay * (c.y * 0.5 * in.posSize.w);
    } else if (mode > 3.5) {
        // ground mark: on the terrain, its corners at their own heights; one cell of the mark's n x n grid
        // (extra.y = n * 256 + row * 16 + column, 0 = the whole mark) so it follows slopes and cliff edges
        const float cs = cos(in.velRot.w), sn = sin(in.velRot.w);
        ax = float3(cs, 0.0, sn);
        ay = float3(-sn, 0.0, cs);
        // a corner: its height in 1/4 px (floor) + its fade (fract / 0.99: none on steep faces / up walls)
        const float hv = c.x < 0.0 ? (c.y < 0.0 ? in.corners.x : in.corners.w) : (c.y < 0.0 ? in.corners.y : in.corners.z);
        const float h = floor(hv) * 0.25;
        fade = saturate((hv - floor(hv)) / 0.99);
        const float g = in.extra.y;
        const float n = max(1.0, floor(g / 256.0 + 0.001));
        const float cell = g - floor(g / 256.0 + 0.001) * 256.0;
        const float2 ij = float2(cell - floor(cell / 16.0 + 0.001) * 16.0, floor(cell / 16.0 + 0.001));
        q = -1.0 + (ij + (c * 0.5 + 0.5)) * (2.0 / n);
        p = in.posSize.xyz + (ax * q.x + ay * q.y) * (0.5 * in.posSize.w);
        p.y = in.posSize.y + h;
        p += normalize(u.eye.xyz - p) * 1.5;
    } else {
        const float cs = cos(in.velRot.w), sn = sin(in.velRot.w);
        ax = R * cs + U * sn;
        ay = U * cs - R * sn;
        p = in.posSize.xyz + (ax * c.x + ay * c.y) * (0.5 * in.posSize.w);
    }
    FxOut o;
    o.pos = u.viewProj * float4(p, 1.0);
    o.q = q;
    o.wpos = p;
    o.ax = ax;
    o.ay = ay;
    o.vel = in.velRot.xyz;
    o.albedo = in.albedo;
    o.albedo.a *= fade;
    o.emit = in.emit;
    o.extra = in.extra;
    o.light = in.light;
    o.aux = in.aux;
    o.dust = in.corners.x;
    return o;
}

struct FxFU {
    float4 sunDir;    // xyz towards the sun
    float4 sunCol;    // rgb sun colour (x strength), a = ambient share of the puffs' light
    float4 ambCol;    // rgb sky colour
    float4 fogColor;  // rgb, a = unused
    float4 fog;       // eye xyz, fog start
    float4 fog2;      // fog end, Aerial haze: 1 - the fog's cap (0 = off), -, -
};
// Black body-ish: soot -> dark red -> orange -> yellow -> white, brighter as it heats (HDR, bloom).
float3 fxFireRamp(float T) {
    float3 c = float3(0.0);
    c = mix(c, float3(0.42, 0.035, 0.0), smoothstep(0.03, 0.25, T));
    c = mix(c, float3(1.0, 0.28, 0.035), smoothstep(0.2, 0.5, T));
    c = mix(c, float3(1.0, 0.62, 0.17), smoothstep(0.45, 0.76, T));
    c = mix(c, float3(1.0, 0.93, 0.78), smoothstep(0.74, 1.0, T));
    return c * (0.5 + T * T * 9.0);
}
fragment float4 fx_fs(FxOut in [[stage_in]], constant FxFU& u [[buffer(0)]], texture3d<float> noise [[texture(0)]],
                      sampler ns [[sampler(0)]]) {
    const float mode = in.emit.w;
    const float2 q = in.q;
    const float r2 = dot(q, q);
    const float f = smoothstep(u.fog.w, u.fog2.x, distance(in.wpos, u.fog.xyz)) * (1.0 - u.fog2.y);
    const float seed = in.aux.x;
    const float t = in.light.w;
    if (mode > 0.5 && mode < 2.5) {
        // spark (a hot streak, brightest at the head) / flash (a radial glow, a star of rays for muzzles)
        float k;
        if (mode < 1.5) {
            const float along = q.x * 0.5 + 0.5;  // 0 tail .. 1 head
            k = exp(-q.y * q.y * 6.0) * along * along * smoothstep(1.0, 0.85, q.x);
        } else {
            const float r = sqrt(r2);
            k = exp(-r2 * 5.0) * 0.7 + exp(-r2 * 24.0);
            if (in.aux.w > 0.5) {
                const float ang = atan2(q.y, q.x);
                const float ray = pow(abs(cos(ang * in.aux.w * 0.5 + seed * 6.0)), 10.0);
                k += ray * exp(-r * 3.5) * 0.9;
                k += exp(-q.y * q.y * 40.0) * saturate(q.x) * exp(-q.x * 1.5) * 1.4;  // the forward tongue
            }
            k *= saturate(1.0 - r2);
        }
        return float4(in.emit.rgb * (k * in.albedo.a * (1.0 - f)), 0.0);
    }
    if (r2 >= 1.0) discard_fragment();
    const float r = sqrt(r2);
    if (mode > 3.5) {
        const float3 P = float3(q * 0.55, seed * 7.0);
        const float4 n1 = noise.sample(ns, P);
        const float4 n2 = noise.sample(ns, P * 2.7 + float3(0.31, 0.17, 0.0));
        const float n = n1.r * 0.6 + n2.r * 0.4;
        if (mode > 5.5) {
            // ground dust shadow: a soft, ragged dark blob under a dust puff (premultiplied black: darkens the ground)
            const float d = exp(-r2 * 2.6) * saturate(0.6 + (n - 0.5) * 1.6) * smoothstep(1.0, 0.6, r);
            const float a = d * in.albedo.a * (1.0 - f);
            if (a < 0.002) discard_fragment();
            return float4(0.0, 0.0, 0.0, a);
        }
        if (mode < 4.5) {
            // scorch / crater: a dark burnt bowl, ragged and streaked outwards, a rim of thrown-up ground around
            // it (shaded from a radial normal: the bowl's far wall and the rim's sunward side lit), embers while fresh
            const float ang = atan2(q.y, q.x);
            const float streak = noise.sample(ns, float3(cos(ang) * 0.45, sin(ang) * 0.45, seed * 3.0 + r * 0.08)).r;
            float d = saturate((0.78 - r) * 2.2 + (n - 0.5) * 1.3 + (streak - 0.5) * 1.2 * r - 0.15);
            d = d * d * (3.0 - 2.0 * d);
            const float rn = r + (n - 0.5) * 0.18;
            const float rim = smoothstep(0.3, 0.5, rn) * (1.0 - smoothstep(0.55, 0.9, rn));
            const float bowl = 1.0 - smoothstep(0.28, 0.5, rn);
            const float3 radial = (in.ax * q.x + in.ay * q.y) / max(r, 1e-3);
            const float slope = rim * 0.6 - bowl * smoothstep(0.05, 0.4, r) * 0.8;
            const float3 nW = normalize(float3(0.0, 1.0, 0.0) + radial * slope);
            const float shade = saturate(dot(nW, u.sunDir.xyz) * 0.8 + 0.35) / max(0.2, saturate(u.sunDir.y * 0.8 + 0.35));
            const float3 light = u.ambCol.rgb * u.sunCol.a + u.sunCol.rgb * saturate(u.sunDir.y) * shade + in.light.rgb;
            const float ab = d * (0.75 + 0.25 * bowl);
            const float ae = rim * (0.35 + 0.4 * n2.r);
            const float aa = (ab + ae * (1.0 - ab)) * in.albedo.a * smoothstep(1.0, 0.88, r);
            if (aa < 0.003) discard_fragment();
            const float3 cb = in.albedo.rgb * (0.65 + 0.7 * n2.r) * light;
            const float3 ce = in.vel * (0.8 + 0.35 * n1.r) * light;
            float3 lit = (cb * ab + ce * ae * (1.0 - ab)) / max(1e-3, ab + ae * (1.0 - ab));
            lit = mix(lit, u.fogColor.rgb, f);
            const float ember = smoothstep(0.6, 0.82, n2.r) * smoothstep(0.6, 0.05, r) * in.extra.w;
            const float3 e = float3(1.0, 0.32, 0.05) * ember * 4.0 * (1.0 - f);
            return float4((lit + e) * aa, aa);
        }
        // shock ring: a band of dust rolling out along the ground
        const float rr = (r - 0.7) / 0.17;
        float d = exp(-rr * rr) * (0.45 + 1.1 * (n - 0.35)) + exp(-r2 * 3.0) * 0.3 * (1.0 - t) * n;
        d = saturate(d) * smoothstep(1.0, 0.86, r);
        const float a = d * in.albedo.a;
        if (a < 0.003) discard_fragment();
        const float sunT = saturate(u.sunDir.y * 0.7 + 0.3);
        float3 lit = in.albedo.rgb * (u.ambCol.rgb * u.sunCol.a + u.sunCol.rgb * sunT + in.light.rgb) * (0.8 + 0.4 * n2.r);
        lit = mix(lit, u.fogColor.rgb, f);
        return float4(lit * a, a);
    }
    // Puffs and debris: a sphere broken by animated 3D noise.
    const float sph = sqrt(1.0 - r2);
    const float3 toEye = normalize(u.fog.xyz - in.wpos);
    const float sc = in.aux.y;
    const float3 P = float3(q * 0.42 * sc, sph * 0.3) + float3(seed * 13.7, seed * 5.3, seed * 9.1) + float3(0.0, -t * 0.32, t * 0.22);
    const float4 n1 = noise.sample(ns, P);
    const float4 n2 = noise.sample(ns, P * 2.6 + (n1.gba - 0.5) * 0.2);
    const float n = n1.r * 0.62 + n2.r * 0.38;
    const float3 g = (n1.gba - 0.5) * 2.0 * 0.9 + (n2.gba - 0.5) * 2.0 * 0.5;
    if (mode > 2.5) {
        // debris: a hard chunk
        if (r > 0.5 + (n - 0.5) * 0.9) discard_fragment();
        const float3 nq = normalize(float3(q, sph) - g * 0.8);
        const float3 nW = normalize(in.ax * nq.x + in.ay * nq.y + toEye * nq.z);
        const float sunT = saturate(dot(nW, u.sunDir.xyz) * 0.8 + 0.2);
        float3 lit = in.albedo.rgb * (u.ambCol.rgb * u.sunCol.a * (0.7 + 0.3 * nW.y) + u.sunCol.rgb * sunT + in.light.rgb);
        lit = mix(lit, u.fogColor.rgb, f);
        const float3 hot = in.emit.rgb * in.extra.w * (0.4 + 0.6 * n2.r) * (1.0 - f);
        return float4((lit + hot) * in.albedo.a, in.albedo.a);
    }
    // Wisps, not discs: the noise pushes the outline about (more as the puff ages), then erodes it.
    const float2 qw = q * (1.12 + (n1.r - 0.45) * 0.8) + (n1.gb - 0.5) * (0.5 + 0.5 * t);
    const float sphW = sqrt(saturate(1.0 - dot(qw, qw)));
    const float erode = in.aux.z;
    const float soot = in.aux.w;  // 0 dust .. 1 black smoke: how much of the light the puff swallows
    float dens = saturate((sphW * 1.15 + (n - 0.5) * (1.5 + 0.5 * t) - erode) * (2.2 - 0.7 * soot)) * smoothstep(1.0, 0.72, r);
    float a = dens * in.albedo.a;
    // Soft against the ground under it.
    if (in.extra.z > 0.0) a *= smoothstep(0.0, in.extra.z, in.wpos.y - in.extra.x);
    if (a < 0.003) discard_fragment();
    const float3 nq = normalize(float3(qw, sphW * 1.0 + 0.12) - g * 0.7);
    const float3 nW = normalize(in.ax * nq.x + in.ay * nq.y + toEye * nq.z);
    // Self-shadow: how much of the cloud lies between this point and the sun (soot swallows more).
    const float3 sq = float3(dot(u.sunDir.xyz, in.ax), dot(u.sunDir.xyz, in.ay), dot(u.sunDir.xyz, toEye));
    const float2 qs = qw + sq.xy * 0.5;
    const float sphS = sqrt(saturate(1.0 - dot(qs, qs)));
    const float nS = noise.sample(ns, P + float3(sq.xy * 0.21 * sc, 0.0)).r;
    const float occ = saturate(sphS * 1.15 + (nS * 0.62 + n2.r * 0.38 - 0.5) * 1.5 - erode);
    const float selfShadow = exp(-occ * (1.2 + 0.5 * soot) * in.albedo.a);
    const float ndl = dot(nW, u.sunDir.xyz);
    const float thin = 1.0 - dens;  // thin parts let the light through (no dark rims)
    const float hb = saturate((in.wpos.y - in.extra.x) / 80.0);  // height above the ground
    // One plume lighting for smoke and dust: the sun side and top bright and warm, the side away from the sun and
    // the underside in the cloud's own shade, cooler (sky light, never black); the thin edges scatter the sun -
    // a little on the sun side, a bright rim when the sun stands behind the smoke.
    const float sunD = pow(saturate(ndl * 0.6 + 0.4), 1.5) * mix(1.0, selfShadow, 0.85 * dens) +
                       thin * (0.08 + 0.22 * soot * saturate(ndl * 0.5 + 0.5) + pow(saturate(-sq.z), 3.0) * (0.35 + 0.5 * soot));
    const float up = saturate(nW.y * 0.5 + 0.5);
    const float under = 1.0 - up;
    // A smoke column stands in its own shade at its base and in its dense core.
    const float deep = 1.0 - soot * (0.3 * (1.0 - hb) + 0.2 * dens * dens * under);
    const float skyD = (0.5 + 0.5 * up + thin * 0.1) * deep;
    // The sunlit ground under it throws warm light back up.
    const float3 bounce = u.sunCol.rgb * float3(1.0, 0.84, 0.62) * (saturate(u.sunDir.y) * under * 0.1);
    const float3 fire = in.light.rgb * (0.4 + 0.6 * under);  // fires light it from below
    const float3 alb = in.albedo.rgb * (1.0 + 0.3 * soot * thin);  // wisps are paler than the dense body
    float3 lit = alb * (u.ambCol.rgb * float3(0.94, 0.97, 1.04) * skyD * u.sunCol.a +
                        u.sunCol.rgb * float3(1.08, 1.0, 0.88) * sunD * mix(1.0, deep, 0.5) + bounce + fire);
    lit += in.light.rgb * (soot * dens * (1.0 - hb) * (0.12 + 0.25 * under));  // firelight caught in the base
    // Heat: the dense core stays hot longest (fire cools from the outside in).
    const float heat = in.extra.w;
    float3 em = float3(0.0);
    if (heat > 0.001) {
        const float core = sphW * 0.85 + (n - 0.5) * 1.6 + 0.2;
        const float T = saturate(core * heat * 0.98 - (1.0 - heat) * 0.55);
        em = fxFireRamp(T) * in.emit.rgb;
        lit *= 1.0 - 0.5 * saturate(heat * 2.0 - 0.6);  // only while it still glows
        a = min(1.0, a * (1.0 + heat * 0.6));
    }
    lit = mix(lit, u.fogColor.rgb, f);
    return float4((lit + em * (1.0 - f)) * a, a);
}

// The half-resolution particles blended over the resolved scene (premultiplied): one full-screen triangle,
// bilinear from the half-size target (pixel coordinates, so the same in every backend).
struct FxcOut {
    float4 pos [[position]];
};
vertex FxcOut fxc_vs(uint vid [[vertex_id]]) {
    const float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    FxcOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
struct FxcFU {
    float4 scale;  // xy: full-resolution pixel -> half-resolution uv
};
fragment float4 fxc_fs(FxcOut in [[stage_in]], constant FxcFU& u [[buffer(0)]], texture2d<float> halfTex [[texture(0)]],
                       sampler hs [[sampler(0)]]) {
    return halfTex.sample(hs, in.pos.xy * u.scale.xy);
}
)MSL";

}  // namespace d2k::render3d::msl
