#pragma once
// MSL of the Reimagined models' fragment shader (docs/3d-models.md "Reimagined models"): model_pbr_fs, the
// model_fs of a model with PBR maps. Compiled appended to render3d/models_msl.hpp's kModel (its structs,
// skinning, walkPart, shadowPCF, the light block), see shader_blobs.cpp modelPbrMsl(). HLSL twin: the
// "Reimagined models" block of render3d/shaders.hlsl - change both together.
//
// Resources: textures / samplers 0 base colour (+ emission in alpha), 1 shroud, 2 sun shadow map, 3 PBR map
// (R occlusion, G roughness, B metallic, A house-colour mask), 4 tangent-space normal map, 5 the screen-space
// ambient occlusion (render3d/ssao.*); uniform buffers
// 0 FU (as model_fs), 1 PointLights, 2 PbrU; storage buffer 0 the light grid ([[buffer(3)]]).
// Shading: model_fs's light model (sky ambient + fill + sun, the scene's light scale) with the normal map
// (tangent frame from screen derivatives of position and uv), occlusion on the ambient, metals losing their
// diffuse, a GGX sun highlight and a sky / ground reflection (Schlick Fresnel) - highlights may exceed 1
// (HDR: bevels and wet-looking metal glint and bloom).

namespace d2k::render3d::msl {

inline constexpr const char* kModelPbr = R"MSL(
struct PbrU {
    float4 k;   // x normal map strength, y green sign (1 = glTF), z sun highlight, w sky reflection
    float4 k2;  // x occlusion strength
};
// The normal map's normal in world space: the tangent frame from the derivatives of the world position
// and the uv (no tangents in the vertex format). Green points towards decreasing glTF v (Blender's +v).
float3 pbrNormal(float3 n, float3 wpos, float2 uv, float3 tn, constant PbrU& q) {
    const float3 dp1 = dfdx(wpos), dp2 = dfdy(wpos);
    const float2 duv1 = dfdx(uv), duv2 = dfdy(uv);
    const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    const float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    const float m = max(dot(t, t), dot(b, b));
    if (m < 1e-20) return n;
    const float s = rsqrt(m);
    const float2 xy = tn.xy * q.k.x;
    return normalize(t * (s * xy.x) - b * (s * xy.y * q.k.y) + n * max(tn.z, 0.05));
}

fragment float4 model_pbr_fs(VOut in [[stage_in]], bool front [[front_facing]], constant FU& u [[buffer(0)]],
                             constant PointLights& pl [[buffer(1)]], constant PbrU& q [[buffer(2)]],
                             const device uint4* plg [[buffer(3)]],
                             texture2d<float> tex [[texture(0)]], texture2d<float> shroud [[texture(1)]],
                             depth2d<float> shadowMap [[texture(2)]], texture2d<float> pbrTex [[texture(3)]],
                             texture2d<float> nrmTex [[texture(4)]], texture2d<float> ssaoTex [[texture(5)]],
                             sampler texSamp [[sampler(0)]], sampler shroudSamp [[sampler(1)]],
                             sampler shadowSamp [[sampler(2)]], sampler pbrSamp [[sampler(3)]],
                             sampler nrmSamp [[sampler(4)]], sampler ssaoSamp [[sampler(5)]]) {
    if (in.params.w > 0.5 && !front) discard_fragment();
    if (u.walkMode.x > 0.5 && walkPart(in.wpos, u.walk) != (u.walkMode.x > 1.5)) discard_fragment();
    float4 c = in.color;
    float emit = 0.0;
    float4 orm = float4(1.0, 1.0 - in.pbr.x, in.pbr.y, 0.0);
    float3 tn = float3(0.0, 0.0, 1.0);
    if (in.params.z > 0.5) {
        const float4 t = tex.sample(texSamp, in.uv);
        if (t.a < 0.5) discard_fragment();
        c.rgb *= t.rgb;
        if (u.map.w > 0.0) emit = saturate((1.0 - t.a) * 2.0) * u.map.w;
        orm = pbrTex.sample(pbrSamp, in.uv);
        // x, y only (BC5 maps have no blue): z rebuilt (docs/memory.md)
        const float2 nxy = nrmTex.sample(nrmSamp, in.uv).xy * 2.0 - 1.0;
        tn = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
    }
    float band = 0.0;
    if (u.walkMode.z > 0.0) {
        const float d = u.walkMode.y - in.wpos.y;
        if (d < 0.0) {
            if (fract((in.wpos.y - u.walkMode.w) / 3.0) > 0.25) discard_fragment();
            return float4(0.12, 0.6, 1.0, 0.4 * u.sunDir.w);
        }
        band = saturate(1.0 - d / u.walkMode.z);
    }
    if (in.pbr.z > 0.5) band = max(band, 0.55 * saturate(1.0 - (in.pbr.z - 1.0) / 2.5));  // Assemble: cut edge, weld seams
    band = max(band, in.pbr.w);
    const float3 glow = c.rgb / max(max(c.r, max(c.g, c.b)), 0.05);
    // Realistic grade (docs/3d-mode.md): the albedo darker; the house colour keeps its strength (an accent).
    const float gk = pl.grade.x > 0.0 ? pl.grade.x : 1.0;
    c.rgb *= gk;
    // House colour: the map's mask (dirt over the paint lowers it), or a part's own vertex house value.
    float hm = 1.0;
    if (in.params.x > 1.5) {
        c.rgb = u.house.rgb;
    } else {
        hm = max(in.params.x, orm.a);
        const float l = dot(c.rgb, float3(0.299, 0.587, 0.114)) / gk;
        c.rgb = mix(c.rgb, clamp(u.house.rgb * l * 1.8, 0.0, 1.0), hm);
    }
    float ember = 0.0;  // vehicle wrecks: a hulk is burnt black-brown, embers glow in its seams (the baked occlusion)
    if (u.dust.z > 0.0) {
        const float4 wk = wreckChar(c.rgb, u.dust.zw, in.params.z > 0.5 ? in.uv * 55.0 : in.wpos.xz * 0.9 + in.wpos.y * 0.7,
                                    saturate((0.5 - orm.r) * 3.0));
        if (wk.a < 0.0) discard_fragment();
        c.rgb = wk.rgb;
        ember = wk.a;
    }
    if (in.params.y > 0.5) {
        float3 n0 = normalize(in.nrm);
        if (!front) n0 = -n0;
        const float3 n = pbrNormal(n0, in.wpos, in.uv, tn, q);
        const float3 L = u.sunDir.xyz;
        const float ndl = max(dot(n, L), 0.0);
        float sh = 1.0;
        if (u.shadow.x > 0.5) sh = shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(in.wpos, 1.0)).xyz, u.shadow);
        const float lit = ndl * mix(u.shadow.z, 1.0, sh);
        const float ao = mix(1.0, orm.r, saturate(q.k2.x));
        float rough = clamp(orm.g, 0.06, 1.0), metal = saturate(orm.b);
        // Realistic grade: fine sand dust on the up-facing surfaces and over the model's foot (matt, not metal).
        const float dk = sandDustK(pl, in.uv * 61.0, n0.y, dot(u.gplane.xyz, in.wpos) - u.gplane.w, u.dust) * (1.0 - 0.5 * saturate(hm));
        if (dk > 0.0) {
            c.rgb = mix(c.rgb, pl.dustColor.rgb, dk);
            rough = mix(rough, 0.95, dk);
            metal *= 1.0 - dk;
        }
        if (u.dust.z > 0.0) {  // a hulk: sooty, matt
            rough = mix(rough, 0.92, u.dust.z);
            metal *= 1.0 - 0.9 * u.dust.z;
        }
        const float3 base = c.rgb;
        c.rgb = satur(pl, c.rgb);
        const float3 fillDir = normalize(float3(-L.x, 0.25, -L.z));
        const float amb = u.light.x * (0.72 + 0.18 * n.y) + u.light.x * 0.4 * max(dot(n, fillDir), 0.0);
        // (Ambient occlusion, docs/3d-mode.md: the screen-space term with the baked one, on the sky light only.)
        c.rgb *= sunAmb(pl, amb * ao * ssaoAt(pl, ssaoTex, ssaoSamp, in.pos.xy), u.light.y * lit) * u.light.z * (1.0 - 0.75 * metal);
        // GGX sun highlight + sky / ground reflection.
        const float3 V = normalize(u.fog.xyz - in.wpos);
        const float3 H = normalize(L + V);
        const float ndv = max(dot(n, V), 1e-3), ndh = max(dot(n, H), 0.0), vdh = max(dot(V, H), 0.0);
        const float a = rough * rough, a2 = a * a;
        const float dd = ndh * ndh * (a2 - 1.0) + 1.0;
        const float D = a2 / (3.14159 * dd * dd);
        const float k = a * 0.5 + 0.0001;
        const float vis = 1.0 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k) * 4.0);
        const float3 f0 = mix(float3(0.04), base, metal);
        const float3 F = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
        c.rgb += (D * vis * 3.14159) * F * (lit * u.light.y * u.light.z * q.k.z * pl.info.y) * pl.sunColor.rgb;
        const float3 R = reflect(-V, n);
        const float3 sky = pl.ambColor.rgb * mix(float3(0.62, 0.55, 0.46), float3(0.80, 0.86, 0.95), saturate(R.y * 0.6 + 0.5));
        const float3 fe = f0 + (max(float3(1.0 - rough), f0) - f0) * pow(1.0 - ndv, 5.0);
        c.rgb += sky * fe * (ao * u.light.x * u.light.z * q.k.w * (1.0 - 0.6 * rough));
        c.rgb += base * (1.0 - 0.75 * metal) * pointLights(pl, plg, in.wpos, n, 0.25);
    } else if (in.params.y > 0.2) {
        c.rgb = satur(pl, c.rgb) * pl.spriteTint.rgb + c.rgb * pointLights(pl, plg, in.wpos, float3(0.0, 1.0, 0.0), 0.8);
    }
    c.rgb *= dustHaze(pl, in.wpos);  // Reimagined: drifting dust veils
    if (u.house.a > 0.0) c.rgb = min(c.rgb + u.house.a, 1.0);  // the attack flash
    c.rgb += glow * emit;
    if (ember > 0.0) c.rgb += float3(2.6, 0.7, 0.12) * ember;
    c.rgb += float3(1.0, 0.55, 0.2) * (band * band * 1.6) + float3(1.0, 0.95, 0.85) * pow(band, 12.0);
    if (u.light.w > 0.5) c.rgb *= 1.0 - shroudAt(pl, shroud, shroudSamp, in.wpos.xz, u.map.xy);
    const float fo = smoothstep(u.fogColor.a, u.fog.w, distance(in.wpos, u.fog.xyz)) * aerialK(pl);
    c.rgb = mix(c.rgb, u.fogColor.rgb, fo);
    return float4(c.rgb, u.sunDir.w);
}
)MSL";

}  // namespace d2k::render3d::msl
