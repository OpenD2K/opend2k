#pragma once
// MSL fragment shader of the faithful designed terrain ("Designed (faithful)", Terrain v3;
// render3d/terrain_v2.* with TerrainStyle::Faithful, docs/3d-mode.md "Terrain v3"). HLSL twin:
// terrain3_fs in render3d/shaders.hlsl - change both together.
//
// terrain3_fs (with terrain_vs) is terrain2_fs with the LAYOUT taken from the guide texture
// (render3d/terrain_guide.*: rock coverage, relief height, tone, ruggedness - read from the
// original tiles at run time) instead of the blurred per-cell splat:
//   - sand <-> rock on the original's own outline (a crisp ramp, height-blended);
//   - cliffs as shaded RELIEF: crag rock (material layer 7) in the original's palette (five tone
//     levels measured on the map's tiles) by the relief's tone - a face darker than its top, by
//     the side it looks to; a lit rim; a darker foot; the original's fine light and dark; crisp
//     edges along the tone contours;
//   - rock that is not plain in the original (pale crust, dark lumps) drawn broken in its own
//     tone levels, so only buildable rock looks calm;
//   - concrete as the original's 32 x 16 slabs (pale seam + dark line, used and dusty), paler
//     under buildings.
// Everything before the fragment function (structs, the uniform block, noise, anti-tiled
// sampling, shadows, the organic spice) is shared with terrain2_fs: the source is the head of
// msl::kTerrain2 + kTerrain3Body (render3d/shader_blobs.cpp), so the Designed shader's own
// fragment function stays as it is.

namespace d2k::render3d::msl {

// The first line of terrain2_fs in kTerrain2: everything before it is the shared head.
inline constexpr const char* kTerrain2Main = "fragment float4 terrain2_fs(";

inline constexpr const char* kTerrain3Body = R"MSL(
// Fragment uniform buffer 2: the faithful look (DesignedTerrain::FaithU).
struct F3U {
    float4 guide;       // x cliff height (world px), y guide texels per world px, z 1 = baked from above, w tone relief (px)
    float4 level[5];    // rgb colour of the broken rock's tone level i; a = its upper tone edge (level 4: contour raggedness)
    float4 cragTex;     // x 1 / the crag texture's mean luminance, y contrast (gamma), z normal strength, w world px per repeat
    float4 roughTex;    // x 1 / the rough texture's mean luminance, y contrast (gamma), z, w unused
    float4 shade;       // a face's tone: x against its top, y + the part by the side it looks to (towards the sun
                        // lighter); z the pale rim's strength, w the share of the original's fine light and dark
    float4 face;        // x gullies (strength), y ledge line, z darker foot (tone units), w contact shadow on the sand
    float4 slab;        // rgb concrete slab colour, a speckle
    float4 seamLight;   // rgb pale seam colour, a unused
    float4 seamDark;    // rgb dark seam line colour, a unused
    float4 detailSun;   // xyz the sun of the level ground's fine detail, w 1 / its flat-ground light
    float4 reliefLight; // ambient, diffuse, 1 / flat-ground light of the broken rock (lit by sunDir), w unused
};

// The relief (world px above the ground): the guide's macro height (macro) and, on broken rock,
// the original's own light and dark as relief (pale = proud, dark = a pocket). g = the guide (r
// rock, g macro height, b tone, a rugged).
float faithHeight(float4 g, constant F3U& u3, bool macro) {
    return (macro ? g.g * u3.guide.x : 0.0) + g.a * (g.b * 2.0 - 1.0) * u3.guide.w;
}

// The guide at world point p, smoothly upsampled (owner, 2026-11-11: zoomed in, the original's
// tile pixels showed through as square blocks and stair-steps - a bilinear tap has a kink at
// every texel border, and the tone levels and the rock outline cut its contours sharp). While a
// guide texel covers more than about a pixel: a Catmull-Rom bicubic (C1, interpolating - the
// original's single-pixel light and dark keep their strength; 5 bilinear taps, the 4 corner taps
// of the 3 x 3 dropped), clamped against its overshoot; minified, the plain trilinear tap (blended
// over lod 0.5 .. 1). texPerPx = guide texels per world px. (The 2D bake always reads lod 0.)
float4 guideAt(texture2d<float> t, sampler s, float2 p, float2 mapSize, float texPerPx, float lod) {
    const float4 lin = t.sample(s, p / mapSize, level(lod));
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
    float4 r = t.sample(s, a12, level(lod)) * c + t.sample(s, float2(a12.x, a0.y), level(lod)) * n +
               t.sample(s, float2(a12.x, a3.y), level(lod)) * so + t.sample(s, float2(a0.x, a12.y), level(lod)) * we +
               t.sample(s, float2(a3.x, a12.y), level(lod)) * ea;
    r = saturate(r / (c + n + so + we + ea));
    return mix(r, lin, smoothstep(0.5, 1.0, lod));
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

// The colour of broken rock of tone t. The original paints its crags in a few tones (crevices,
// shaded rock, mid rock, lit facets, pale rims): the same levels here, with crisp edges along the
// tone's contours (aa = half an edge's width in tone units) and a little of the continuous tone
// inside each level - the original's light and dark, redrawn sharp at any resolution.
float3 toneRamp(float t, float aa, constant F3U& u3) {
    const float s0 = smoothstep(u3.level[0].a - aa, u3.level[0].a + aa, t);
    const float s1 = smoothstep(u3.level[1].a - aa, u3.level[1].a + aa, t);
    const float s2 = smoothstep(u3.level[2].a - aa, u3.level[2].a + aa, t);
    const float s3 = smoothstep(u3.level[3].a - aa, u3.level[3].a + aa, t);
    float3 c = mix(u3.level[0].rgb, u3.level[1].rgb, s0);
    c = mix(c, u3.level[2].rgb, s1);
    c = mix(c, u3.level[3].rgb, s2);
    c = mix(c, u3.level[4].rgb, s3);
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
float3 concreteSlab3(float2 p, float grainRel, thread float3& nrm, float3 sand, constant F3U& u3, float px) {
    const float2 s = float2(p.x / 32.0, p.y / 16.0);
    const float2 cell = floor(s);
    const float2 f = (s - cell) * float2(32.0, 16.0);  // px inside the slab: x 0..32, y 0..16
    const float h1 = hash12(cell + 0.37), h2 = hash12(cell * 1.7 + 4.1), h3 = hash12(cell * 2.9 + 11.3),
                h4 = hash12(cell * 0.7 + 17.3);
    const float aa = px * 0.75 + 0.05;
    float3 col = u3.slab.rgb * (0.95 + 0.2 * h1) * float3(1.0 + 0.04 * (h2 - 0.5), 1.0, 1.0 - 0.06 * (h2 - 0.5));
    // Speckle: the fine aggregate of the original slabs (its specks differ by the same amount in
    // every channel: the light ones are paler, the dark ones a more saturated red-brown), then
    // the scanned concrete's own grain.
    const float grain = vnoise(p * 1.9) * 0.55 + vnoise(p * 4.3 + 9.0) * 0.45;
    const float speck = u3.slab.a * (grain - 0.5);
    col *= float3(1.0 + speck, 1.0 + 1.5 * speck, 1.0 + 2.4 * speck) * (1.0 + 0.2 * (grainRel - 1.0));
    // Wear. Grime in soft blotches; dark stains on some slabs; dirt gathered along the seams.
    const float dirtN = vnoise(p / 11.0) * 0.6 + vnoise(p / 3.5) * 0.4;
    col *= 1.0 - 0.17 * smoothstep(0.38, 0.85, dirtN);
    const float stain = smoothstep(0.56, 0.85, vnoise(p / 6.0 + h1 * 37.0) * 0.75 + vnoise(p / 2.2) * 0.25) * step(h3, 0.36);
    col = mix(col, col * float3(0.55, 0.5, 0.46), stain * 0.55);
    const float seamDist = min(min(f.x, 32.0 - f.x), min(f.y, 16.0 - f.y));
    const float edgeBand = 1.0 - smoothstep(0.5, 3.5, seamDist);
    col *= 1.0 - 0.2 * edgeBand * (0.4 + 0.6 * vnoise(p / 4.0 + 5.0));
    // A hairline crack wandering across some slabs.
    const float wander = (vnoise(float2(p.x / 6.0, cell.y * 7.3 + 1.0)) - 0.5) * 9.0;
    const float crack = step(0.84, h4) * (1.0 - smoothstep(0.2, 0.55 + aa, abs(f.y - 8.0 - wander))) *
                        smoothstep(0.0, 4.0, f.x - 28.0 * h2 + 4.0) * 0.8;
    col *= 1.0 - 0.36 * crack;
    // Seams: pale along the south (2 px) and west (1 px) edges, dark along the north and east.
    const float south = smoothstep(14.5 - aa, 14.5 + aa, f.y);
    const float west = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, f.x);
    const float north = 1.0 - smoothstep(1.6 - aa, 1.6 + aa, f.y);
    const float east = smoothstep(30.5 - aa, 30.5 + aa, f.x);
    const float worn = 0.75 + 0.25 * vnoise(p / 3.0 + 11.0);
    col = mix(col, u3.seamDark.rgb, saturate(max(north, east * 0.9)) * (0.85 + 0.15 * worn));
    col = mix(col, u3.seamLight.rgb, saturate(max(south, west)) * (0.55 + 0.3 * worn));
    // Chipped corners (some), and sand drifted into the seams and the chips in patches.
    const float2 cq = float2(min(f.x, 32.0 - f.x), min(f.y, 16.0 - f.y));
    const float cornerId = hash12(cell * 3.1 + float2(step(16.0, f.x), step(8.0, f.y)) * 5.7 + 2.9);
    const float chip = step(cq.x + cq.y, 2.2 + 3.0 * vnoise(p / 1.5 + h3 * 11.0)) * step(cornerId, 0.38);
    col *= 1.0 - 0.35 * chip;
    const float drift = smoothstep(0.32, 0.78, vnoise(p / 7.0 + 3.1) * 0.7 + vnoise(p / 2.0) * 0.3);
    col = mix(col, sand * (0.9 + 0.12 * dirtN), saturate(edgeBand * drift * 0.22 + chip * 0.55));
    // The seams are painted (as in the original), not lit: keep the slab's normal calm there.
    nrm = mix(nrm, float3(0.0, 1.0, 0.0), saturate(max(max(south, west), max(north, east))) * 0.7);
    return col;
}

fragment float4 terrain3_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                            constant F3U& u3 [[buffer(2)]],
                            texture2d_array<float> alb [[texture(0)]], texture2d_array<float> nor [[texture(1)]],
                            texture2d<float> splatA [[texture(2)]], texture2d<float> splatB [[texture(3)]],
                            texture2d<float> spiceMap [[texture(4)]], texture2d<float> shroud [[texture(5)]],
                            depth2d<float> shadowMap [[texture(6)]], texture2d<float> guide [[texture(7)]],
)MSL" R"MSL(                            sampler matSamp [[sampler(0)]], sampler matSamp2 [[sampler(1)]],
                            sampler splatSamp [[sampler(2)]], sampler splatSamp2 [[sampler(3)]],
                            sampler spiceSamp [[sampler(4)]], sampler shroudSamp [[sampler(5)]],
                            sampler shadowSamp [[sampler(6)]], sampler guideSamp [[sampler(7)]]) {
    const float2 p = in.wpos.xz;
    const float2 mapSize = u.fog2.yz;
    const float3 dpx = dfdx(in.wpos), dpy = dfdy(in.wpos);
    const float3 gn = normalize(in.nrm);
    const bool bake = u3.guide.z > 0.5;

    const float4 a = splatA.sample(splatSamp, p / mapSize);
    const float4 b = splatB.sample(splatSamp2, p / mapSize);
    const float cfw = max(fwidth(b.y), 1e-3);
    const float cw = smoothstep(0.5 - cfw, 0.5 + cfw, b.y);
    const float pxWorld = max(length(dpx.xz), length(dpy.xz));
    // The cells no vehicle can use (splat A.a: binary per cell, crisp at the cell borders): the
    // only place for the raised cliff look. Rock vehicles can use is always drawn low and light.
    const float cliffM = smoothstep(0.3, 0.7, a.w);

    // ---- the guide: rock outline, relief, tone ---------------------------------------------
    const float lod = max(0.0, log2(pxWorld * u3.guide.y));
    const float tpp = u3.guide.y;
    // A slight warp (under half a guide texel, at about 3 texels) bends the original's pixel
    // diagonals into organic curves; every read below goes through it.
    const float2 pg = p + (float2(vnoise(p * (tpp / 2.9) + 31.0), vnoise(p * (tpp / 2.9) + 57.0)) - 0.5) * (0.9 / tpp);
    const float2 guv = pg / mapSize;
    const float4 g = guideAt(guide, guideSamp, pg, mapSize, tpp, lod);
    // Macro slope (central differences 1.5 px apart).
    const float e = 1.5;
    const float hE = guideAt(guide, guideSamp, pg + float2(e, 0.0), mapSize, tpp, lod).g;
    const float hW = guideAt(guide, guideSamp, pg - float2(e, 0.0), mapSize, tpp, lod).g;
    const float hS = guideAt(guide, guideSamp, pg + float2(0.0, e), mapSize, tpp, lod).g;
    const float hN = guideAt(guide, guideSamp, pg - float2(0.0, e), mapSize, tpp, lod).g;
    const float2 mg = float2(hE - hW, hS - hN) * (u3.guide.x / (2.0 * e));  // px rise per px
    const float ml = length(mg);
    const float steep = smoothstep(0.45, 1.05, ml);
    // The tone, its contours a little ragged; half a screen pixel in tone units for their edges.
    const float tone = g.b * 2.0 - 1.0 + (vnoise(p / 1.3 + 3.0) - 0.5) * u3.level[4].a;
    const float aaT = max(fwidth(tone) * 0.7, 0.012);
    // ---- cliff faces, as the original tiles show their relief --------------------------------
    // A face = where the relief climbs (the guide lets it climb only across the dark faces the
    // original paints on cells no vehicle can use). In tone units (relative luminance; the
    // tones index the original's palette, toneRamp): darker than the top by the side it looks
    // to, the original's big light and dark, tiers of blocky columns with gullies and ledge
    // lines, a dark foot; a pale rim where it breaks into its top. Level broken rock (tops,
    // rubble, shores) is the original's own tone (toneP).
    const float2 dir = ml > 1e-4 ? mg / ml : float2(0.0);
    const float slopeW = smoothstep(0.25, 0.9, ml);
    const float2 sunXZ = normalize(u.sunDir.xz);
    const float facing = dot(-dir, sunXZ);
    const float4 gb = guide.sample(guideSamp, guv, level(lod + 3.0));  // the surroundings (blurred)
    // The position on a face: 0 at its foot .. 1 at the top.
    const float hF = saturate((g.g - 0.1333) / 0.8667);
    // Two or three tiers (strata) of columns: gullies run down each tier - columns across the
    // face, along x on faces looking north / south, along z on faces looking east / west (a
    // noise decides on diagonal faces: facets, not a plaid) - and every tier has its own
    // columns. Each column has a lit side (towards the sun) that falls off into a dark gully.
    const float hL = hF * 2.6 + (vnoise(p / 13.0 + 21.0) - 0.5) * 0.8;
    const float tier = floor(hL), tf = hL - tier;
    const bool ns = dir.y * dir.y + (vnoise(p / 17.0 + 3.0) - 0.5) * 0.5 > 0.5;
    const float uc = (ns ? p.x : p.y) + (vnoise(p / 11.0 + 7.0) - 0.5) * 6.0 + tier * 37.7;
    float2 c1 = column(uc, 8.5, 3.0 + tier), c2 = column(uc + 2.3, 3.4, 71.0 + tier);
    if (ns ? sunXZ.x > 0.0 : sunXZ.y > 0.0) {
        c1.x = 1.0 - c1.x;
        c2.x = 1.0 - c2.x;
    }
    const float gully = mix(0.15, -0.32, smoothstep(0.42, 0.78, c1.x)) * (0.45 + 1.1 * c1.y) +
                        mix(0.06, -0.16, smoothstep(0.45, 0.8, c2.x)) * (0.4 + c2.y) + (c1.y - 0.5) * 0.16;
    const float ledge = (1.0 - smoothstep(0.0, 0.16, tf)) * step(0.5, tier) * smoothstep(0.1, 0.3, hF);
    const float tierLight = 0.05 * (tier - 1.0) + 0.07 * smoothstep(0.75, 1.0, tf);
    const float foot = 1.0 - smoothstep(0.03, 0.16, hF);
    // The rim: the height still climbs from below (2 px down the slope) but no longer above
    // (2 px up) - a thin broken pale line, on the sides that look towards the sun.
    const float gUp = guideAt(guide, guideSamp, pg + dir * 2.0, mapSize, tpp, lod).g;
    const float gDn = guideAt(guide, guideSamp, pg - dir * 2.0, mapSize, tpp, lod).g;
    const float climbed = g.g - gDn, climbing = gUp - g.g;
    const float rim = smoothstep(0.35, 0.6, (climbed - climbing) / (climbed + abs(climbing) + 0.004)) *
                      smoothstep(0.012, 0.03, climbed) * smoothstep(0.3, 0.5, hF) *
                      smoothstep(0.3, 0.5, vnoise(p / 5.5 + 19.0)) * saturate(0.25 + 0.75 * facing);
    const float toneDir = u3.shade.x + u3.shade.y * facing;
    const float toneBig = clamp(gb.b * 2.0 - 1.0, -0.35, 0.35);  // the original's big light and dark
    // (The gullies stand out on the sunlit sides; a shaded face is a darker, calmer mass.)
    const float toneC = toneDir + 0.5 * toneBig + u3.face.x * gully * (0.5 + 0.5 * saturate(facing + 0.6)) -
                        u3.face.y * ledge - u3.face.z * foot + tierLight + (g.b - gb.b) * 2.0 * u3.shade.w;
    const float aaC = max(fwidth(toneC) * 0.7, 0.012);
    // Plain rock on a slope (the step along a plateau's shore): the side only, faintly on passable cells.
    const float toneMacro = slopeW * (-0.2 + 0.15 * facing) * mix(0.3, 1.0, cliffM);
    // Level broken rock: the original's own tone - on passable cells its pale crust with the dark
    // specks held back (a ramp over the cell border, no square edge).
    const float toneP = mix(max(tone, -0.18), tone, smoothstep(0.1, 0.9, a.w));

    // Rock coverage: a ramp across the original's outline; the materials' own heights decide
    // inside it (sand lies in the cracks).
    const float rockW = saturate((g.r - 0.5) * 2.6 + 0.5 + (vnoise(p / 1.6) - 0.5) * 0.25);
    const float sandAll = a.x + a.y;
    const float dunes = sandAll > 1e-3 ? a.y / sandAll : 0.0;

    // Relief normal: the height function at three points.
    const float d = clamp(pxWorld, 0.35, 1.5);
    float3 rn = float3(0.0, 1.0, 0.0);
    if (g.r > 0.02 || g.g > 0.004) {
        const float4 g1 = guideAt(guide, guideSamp, pg + float2(d, 0.0), mapSize, tpp, lod);
        const float4 g2 = guideAt(guide, guideSamp, pg + float2(0.0, d), mapSize, tpp, lod);
        // Baked from above the macro shape is shaded by the cliff shading above (the original's
        // look: tops lit, faces dark), not by this normal: only the small relief catches the sun.
        const float h0 = faithHeight(g, u3, !bake);
        const float2 grad = float2(faithHeight(g1, u3, !bake) - h0, faithHeight(g2, u3, !bake) - h0) / d;
        rn = normalize(float3(-grad.x, 1.0, -grad.y));
    }
    // (Baked: the mesh turns a steep face into a ramp that reaches over the sand at its foot;
    // the sand there is lit as level ground, not as a slope.)
    const float3 gnL = bake ? normalize(mix(float3(0.0, 1.0, 0.0), gn, smoothstep(0.8, 0.96, gn.y))) : gn;
    const float3 base = normalize(mix(gnL, rn, rockW));

    // ---- material weights -----------------------------------------------------------------
    // Rock the original draws broken is crag rock (layer 7) on the faces of cells no vehicle can
    // use, rough rock everywhere else (tops, rubble, shores, lumps); the rest is the plain rock.
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
    spice *= saturate((1.0 - rockW) * 1.6);
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    const bool steepGeo = !bake && gn.y < 0.82;  // (baked from above: the mesh only casts shadows)
    const float3 tw = pow(abs(gn), float3(4.0));
    const float3 tri = tw / (tw.x + tw.y + tw.z);
    float4 c[7];
    float3 nn[7];
    float hmax = -1.0;
    for (int i = 0; i < 7; ++i) {
        c[i] = float4(0.0);
        nn[i] = float3(0.0);
)MSL" R"MSL(        if (w[i] < 0.004) continue;
        // Slot 3 (cliff) is the crag rock of layer 7 here.
        const bool isCrag = i == 3;
        const uint layer = isCrag ? 7u : uint(i);
        // (The crag rock is drawn out north-south: the original's gullies, seen obliquely from
        // the south, all run up-down on the screen.)
        const float2 sc = isCrag ? float2(1.0, 0.62) / u3.cragTex.w : float2(1.0 / u.tint[i].a);
        const float ns = isCrag ? u3.cragTex.z : u.lum[i].a;
        MatS m = sampleTiled(alb, nor, matSamp, p * sc, layer, dpx.xz * sc, dpy.xz * sc, k);
        float3 n = base + float3(m.n.x, 0.0, -m.n.y) * ns;
        if (steepGeo && (i == 2 || i == 3 || i == 4)) {
            const MatS mx = sampleTiled(alb, nor, matSamp, float2(in.wpos.z, -in.wpos.y) * sc.x, layer,
                                        float2(dpx.z, -dpx.y) * sc.x, float2(dpy.z, -dpy.y) * sc.x, k);
            const MatS mz = sampleTiled(alb, nor, matSamp, float2(in.wpos.x, -in.wpos.y) * sc.x, layer,
                                        float2(dpx.x, -dpx.y) * sc.x, float2(dpy.x, -dpy.y) * sc.x, k);
            m.c = m.c * tri.y + mx.c * tri.x + mz.c * tri.z;
            n = base + (float3(m.n.x, 0.0, -m.n.y) * tri.y + float3(0.0, mx.n.y, mx.n.x) * tri.x +
                        float3(mz.n.x, mz.n.y, 0.0) * tri.z) * ns;
        }
        c[i] = m.c;
        nn[i] = n;
        hmax = max(hmax, m.c.a + w[i]);
    }
    // Height blend. The broken materials (crag, rough) give only their light and dark (detC,
    // detR): their colour is the original's, by tone. Plain rock takes the slope shading.
    const float depth = u.params.y;
    float3 albedo = float3(0.0), n = float3(0.0);
    float sum = 0.0, detC = 0.0, detR = 0.0, brokenSum = 0.0;
    for (int i = 0; i < 7; ++i) {
        if (w[i] < 0.004) continue;
        const float bw = max(c[i].a + w[i] - hmax + depth, 0.0);
        const float L = dot(c[i].rgb, float3(0.299, 0.587, 0.114));
        if (i == 3) {
            detC += mix(1.0, pow(L * u3.cragTex.x, u3.cragTex.y), 0.45) * bw;
            brokenSum += bw;
        } else if (i == 4) {
            detR += pow(L * u3.roughTex.x, u3.roughTex.y) * bw;
            brokenSum += bw;
        } else {
            albedo += mix(c[i].rgb * u.tint[i].rgb, L * u.lum[i].rgb, u.params.w) * (i == 2 ? 1.0 + toneMacro : 1.0) * bw;
        }
        n += nn[i] * bw;
        sum += bw;
    }
    // Crags: the original's palette by the relief's tone (toneC). Level broken rock (shores,
    // lumps): the original's own tone.
    albedo = (albedo + toneRamp(toneC, aaC, u3) * detC + toneRamp(toneP + toneMacro * 0.6, aaT, u3) * detR) / max(sum, 1e-4);
    n = normalize(sum > 0.0 ? n : base);
    // How much of this pixel is broken rock (its own light) rather than level ground.
    float reliefW = brokenSum / max(sum, 1e-4);
    // The rim: a pale line on whatever rock lies at the top edge of a face.
    albedo = mix(albedo, u3.level[4].rgb * (0.92 + 0.16 * vnoise(p / 1.7 + 5.0)), rim * u3.shade.z * rockW * cliffM);
    // A soft contact shadow on the ground around the foot of raised rock.
    albedo *= 1.0 - u3.face.w * saturate((gb.g - g.g) * 5.0) * (1.0 - rockW);
    // Large-scale variation of the open ground (not of the broken rock, whose tones are the
    // original's, nor of the concrete).
    const float macro = vnoise(p / 700.0) * 0.6 + vnoise(p / 230.0 + 5.0) * 0.4;
    albedo *= 1.0 + u.params.z * (macro - 0.5) * (1.0 - reliefW);

    if (cw > 0.004) {  // concrete slabs over the rest
        const float sc = 1.0 / u.tint[6].a;
        const MatS m = sampleTiled(alb, nor, matSamp, p * sc, 6u, dpx.xz * sc, dpy.xz * sc, k);
        // (On the rock's normal, not the mesh's: baked from above the gentle tilt of a plateau
        // behind a cliff must not light the slabs.)
        float3 cn = base + float3(m.n.x, 0.0, -m.n.y) * u.lum[6].a;
        const float grainRel = dot(m.c.rgb, float3(0.299, 0.587, 0.114)) * u.lum[6].r / max(u3.slab.r, 0.01);
        const float3 cc = concreteSlab3(p, grainRel, cn, u3.level[4].rgb, u3, pxWorld);
        const float t = sum > 0.0 ? cw : 1.0;
        albedo = mix(albedo, cc, t);
        n = normalize(mix(n, normalize(cn), t));
        reliefW *= 1.0 - t;
    }
    const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
    albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
    albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick

    // Light. Level ground: the fine detail under detailSun (sand and plain rock exactly as the
    // Designed terrain shows them), darkened by the sun shadow map. Broken rock: its small relief
    // under sunDir. Baked from above, a face is shaded by the slope shading above, so the shadow
    // map darkens it only a little (it is for the shadows cast onto the ground and the tops).
    // (Baked: the mesh turns a steep face into a ramp that reaches a few px over the sand at its
    // foot. On the lee side that sand is looked up at ground level, under the ramp: the cast
    // shadow then starts at the rock, not a ramp's width away from it.)
    float3 sp = in.wpos;
    if (bake && rockW < 0.5 && gn.y < 0.85 && dot(gn.xz, u.sunDir.xz) < 0.0) sp.y = 0.0;
    const float sm = u.shadow.x > 0.5 ? shadowPCF(shadowMap, shadowSamp, (u.lightVP * float4(sp, 1.0)).xyz, u.shadow) : in.sun;
    const float visAll = mix(u.light.w, 1.0, sm);
    const float vis = bake ? mix(visAll, 1.0, slopeW * 0.7 * rockW) : visAll;
    const float3 shadeFlat = sunAmb(pl, u.light.x, u.light.y * max(dot(n, u3.detailSun.xyz), 0.0) * vis) * u3.detailSun.w;
    const float3 shadeRelief = sunAmb(pl, u3.reliefLight.x, u3.reliefLight.y * max(dot(n, u.sunDir.xyz), 0.0) * vis) * u3.reliefLight.z;
    const float3 shade = mix(shadeFlat, shadeRelief, reliefW);
    float3 rgb = satur(pl, albedo) * shade + albedo * pointLights(pl, in.wpos, n, 0.3);
    float s = 0.0;
    if (u.fog2.w > 0.5 && u.fog2.w < 1.5) s = shroud.sample(shroudSamp, p / mapSize).a;
    const float2 ed = min(p, mapSize - p);
    if (u.fog2.w < 1.5) s = max(s, 1.0 - smoothstep(0.0, 8.0, min(ed.x, ed.y)));
    const float dist = distance(in.wpos, u.fog.xyz);
    const float f = smoothstep(u.fog.w, u.fog2.x, dist);
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

}  // namespace d2k::render3d::msl
