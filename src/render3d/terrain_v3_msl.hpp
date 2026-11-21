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
    float4 reim[4];     // "Reimagined" (docs/3d-mode.md): [0] x on, y spice glow, z sand ripples, w gravel;
                        // [1] xy wind direction (ripples), z cliff strata, w cavity occlusion;
                        // [2] x time (s), y the world beyond the map's margin (world px; 0 = the map's own draw),
                        //     z blue of the shadows, w world beyond the map (docs/3d-mode.md): 0 off, 1 the map
                        //     with it on, 2 the land beyond;
                        // [3] x world px per shadow-map texel, y penumbra per px of distance to the caster,
                        //     z sandstone cliffs (0 = the round-1 look), w the "Realistic grade" (docs/3d-mode.md):
                        //     1 = the calmer palette, rust spice, ground occlusion, sand over the slabs' edges
    float4 photo[9];    // "Photo terrain" (docs/3d-mode.md): [0] x on, y the scans' share in the colour, z normal strength,
                        // w sun sheen; [1] unused; [2 + layer] rgb = 1 / the scan's mean colour, a = world px per repeat
                        // (layers: 0 sand, 1 ripples, 2 gravel, 3 rock, 4 rough rock, 5 cliff, 6 concrete)
};

// Reimagined: the macro relief's height (in kCliffHeight units) for the guide's height gg - taller
// cliffs (Terrain::reliefHeight is the twin). Off: gg.
float macroH(float gg, constant F3U& u3) {
    if (u3.reim[0].x < 0.5) return gg;
    const float t = saturate((gg - 0.1333) / 0.8667);
    return gg * (1.6 + 0.8 * t * t * (3.0 - 2.0 * t));
}

// The relief (world px above the ground): the guide's macro height (macro) and, on broken rock,
// the original's own light and dark as relief (pale = proud, dark = a pocket). g = the guide (r
// rock, g macro height, b tone, a rugged).
float faithHeight(float4 g, constant F3U& u3, bool macro) {
    return (macro ? macroH(g.g, u3) * u3.guide.x : 0.0) + g.a * (g.b * 2.0 - 1.0) * u3.guide.w;
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

// The guide for the world beyond the map (docs/3d-mode.md "World beyond the map"; sk = the margin in
// world px, the mode: 2 = the land beyond): the map's own guide, mirrored across the edge by its sampler,
// blended by the far texture's weight (b) into the procedural desert it describes (r rock coverage, g relief
// height; sk.zw = its tone and ruggedness there). Every other draw: the guide as it is.
float4 guideX(texture2d<float> t, sampler s, texture2d<float> far, sampler fs, float2 p, float2 mapSize, float texPerPx,
              float lod, float4 sk) {
    if (sk.y < 1.5) return guideAt(t, s, p, mapSize, texPerPx, lod);
    const float4 f = far.sample(fs, (p + sk.x + 4.0) / (mapSize + 2.0 * sk.x + 8.0), level(0.0));
    const float4 fg = float4(f.r, f.g, sk.z, sk.w);
    if (f.b > 0.996) return fg;
    return mix(guideAt(t, s, p, mapSize, texPerPx, lod), fg, f.b);
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

)MSL" R"MSL(
// ---- Reimagined (docs/3d-mode.md "Reimagined"): procedural detail over the faithful materials ----
// Round stones on a jittered lattice of `size` world px (a share `density` of the lattice points
// holds one): x = 1 at a stone's middle .. 0 at its edge (0 outside), yz = the outward direction
// (the stone's slope: a normal offset), w = a random shade.
float4 pebbles(float2 p, float size, float density) {
    const float2 q = p / size;
    const float2 i = floor(q), f = q - i;
    float4 best = float4(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            const float2 c = i + float2(float(x), float(y));
            if (hash12(c + 0.71) > density) continue;
            const float2 ctr = float2(float(x), float(y)) + 0.5 + (float2(hash12(c + 3.3), hash12(c + 8.9)) - 0.5) * 0.7;
            const float r = 0.2 + 0.24 * hash12(c + 5.1);
            const float2 d = (f - ctr) / r;
            const float dist = length(d * float2(1.0, 0.8 + 0.5 * hash12(c + 2.2)));
            if (dist < 1.0 && 1.0 - dist > best.x) best = float4(1.0 - dist, d * 0.9, hash12(c + 9.7));
        }
    return best;
}

)MSL" R"MSL(
// ---- Photo terrain (docs/3d-mode.md "Photo terrain"): photographed ground instead of the procedural detail ----
// One tap of a scan (texture arrays: colour BC1, normal x / y BC5, r occlusion - g roughness - b height BC1).
struct PhS {
    float3 d;    // the scan's colour / its mean colour (1 = the mean: the palette's colour stays, the detail is real)
    float2 n;    // tangent-space normal, x and y
    float3 orh;  // ambient occlusion, roughness, height
};
// Without visible repetition (as sampleTiled: a noise value k picks between offset copies of the scan; the two
// copies are blended only inside the transition, so most pixels take one tap of each texture).
PhS photoTap(texture2d_array<float> pa, texture2d_array<float> pn, texture2d_array<float> po, sampler s, float2 uv, uint layer,
             float3 inv, float2 gx, float2 gy, float k) {
    const float l = k * 8.0;
    const float i = floor(l);
    const float b = smoothstep(0.25, 0.75, l - i);
    gx *= 0.8;  // (a little sharper than the footprint asks for: crisp grain, the anisotropic filter keeps it calm)
    gy *= 0.8;
    PhS r;
    r.d = float3(0.0);
    r.n = float2(0.0);
    r.orh = float3(0.0);
    if (b < 0.999) {
        const float2 q = uv + sin(float2(3.0, 7.0) * i);
        r.d += pa.sample(s, q, layer, gradient2d(gx, gy)).rgb * (1.0 - b);
        r.n += pn.sample(s, q, layer, gradient2d(gx, gy)).rg * (1.0 - b);
        r.orh += po.sample(s, q, layer, gradient2d(gx, gy)).rgb * (1.0 - b);
    }
    if (b > 0.001) {
        const float2 q = uv + sin(float2(3.0, 7.0) * (i + 1.0));
        r.d += pa.sample(s, q, layer, gradient2d(gx, gy)).rgb * b;
        r.n += pn.sample(s, q, layer, gradient2d(gx, gy)).rg * b;
        r.orh += po.sample(s, q, layer, gradient2d(gx, gy)).rgb * b;
    }
    // A second, 3.4 x larger copy of the colour (turned, so the two never line up): detail that survives zooming out.
    const float2 um = float2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8) * 0.29 + 0.37;
    const float2 mx = float2(gx.x * 0.8 - gx.y * 0.6, gx.x * 0.6 + gx.y * 0.8) * 0.29, my = float2(gy.x * 0.8 - gy.y * 0.6, gy.x * 0.6 + gy.y * 0.8) * 0.29;
    const float m = dot(pa.sample(s, um, layer, gradient2d(mx, my)).rgb * inv, float3(0.299, 0.587, 0.114));
    r.d *= inv * mix(1.0, m, 0.7);
    r.n = r.n * 2.0 - 1.0;
    return r;
}
// Adds a layer's tap to the blend: where two materials meet, the one standing higher shows (sand in the cracks).
void photoAdd(PhS t, float w, float nk, thread float3& d, thread float2& n, thread float3& orh, thread float& sum) {
    const float bw = w * mix(1.0, 0.25 + 1.5 * t.orh.b, 0.7 * saturate(4.0 * w * (1.0 - w)));
    d += t.d * bw;
    n += t.n * (nk * bw);
    orh += t.orh * bw;
    sum += bw;
}

)MSL" R"MSL(
// ---- Reimagined round 2 (docs/3d-mode.md "Reimagined"): sandstone cliffs, soft cast shadows ----
// One projection of the sandstone (u across the face, v = height, world px): strata ~10 px thick
// (each its own tone: rust, ochre, pale buff; gently wavy), every stratum broken into blocks by
// vertical joints (offset from the stratum below), a rounded lit lip at the top of each stratum
// and a dark undercut below it, fine grain and faint dark streaks running down. Returns the
// colour; nt = the normal's tilt (x along u, y up), occ = cavity light.
float3 sandstone(float2 uv, float px, thread float2& nt, thread float& occ) {
    const float fine = 1.0 - smoothstep(0.4, 1.6, px), mid = 1.0 - smoothstep(1.2, 4.0, px);
    // Beds: 9-20 px thick, gently wavy, each its own tone; thin laminae inside.
    const float sw = (vnoise(float2(uv.x / 90.0, uv.y / 60.0)) - 0.5) * 1.4 + (vnoise(uv / 17.0 + 5.0) - 0.5) * 0.12;
    const float s = uv.y / 14.0 + sw;
    const float id = floor(s), f = s - id;
    const float h1 = hash12(float2(id, 17.0)), h2 = hash12(float2(id, 41.0));
    // Mostly warm ochre, some rust beds, a few pale buff ones.
    float3 col = mix(float3(0.60, 0.38, 0.24), float3(0.74, 0.54, 0.35), smoothstep(0.2, 0.6, h1));
    col = mix(col, float3(0.8, 0.65, 0.47), smoothstep(0.88, 0.98, h1));
    col *= 0.92 + 0.16 * vnoise(float2(uv.x / 40.0, id * 3.1));
    const float lam = vnoise(float2(uv.x / 30.0, uv.y * 0.9 + id));
    col *= 1.0 + (lam - 0.5) * 0.12 * mid;
    // Vertical fractures: 14-34 px apart, running through several beds, fading in and out.
    const float grp = floor(s / 3.0);
    const float wid = 14.0 + 20.0 * hash12(float2(grp, 5.0));
    const float2 c = column(uv.x + (vnoise(float2(uv.y / 11.0, grp)) - 0.5) * 4.0 + grp * 37.0, wid, grp * 3.7 + 1.0);
    const float e = min(c.x, 1.0 - c.x) * wid;  // px to the nearest fracture
    const float crackOn = smoothstep(0.45, 0.75, vnoise(float2(uv.x / 9.0, uv.y / 13.0) + 7.0));
    const float joint = (1.0 - smoothstep(0.0, 0.6 + px * 0.6, e)) * mid * crackOn;
    col *= (0.9 + 0.2 * c.y) * (1.0 - 0.32 * joint);
    // Columns rounded towards the fractures; each bed's rounded lip and its undercut.
    const float bevel = (1.0 - smoothstep(0.0, 3.0, c.x * wid)) - (1.0 - smoothstep(0.0, 3.0, (1.0 - c.x) * wid));
    // Fluting: rounded vertical columns 5-10 px wide (the weathered sandstone's organ pipes), in patches.
    const float2 fl = column(uv.x + (vnoise(float2(uv.y / 7.0, grp)) - 0.5) * 2.5, 5.0 + 5.0 * hash12(float2(grp, 9.0)), grp * 1.3 + 4.0);
    const float flute = smoothstep(0.25, 0.65, vnoise(float2(uv.x / 40.0, uv.y / 30.0) + 11.0)) * mid;
    col *= 1.0 - 0.18 * flute * pow(abs(fl.x * 2.0 - 1.0), 3.0);
    nt.x = bevel * 0.55 * mid * crackOn + (fl.x - 0.5) * 1.5 * flute + (vnoise(uv / 5.0 + 2.0) - 0.5) * 0.5 * fine;
    const float lipW = 0.6 + 0.4 * h2;
    const float lip = smoothstep(0.78, 0.98, f) * lipW, under = (1.0 - smoothstep(0.0, 0.12, f)) * lipW;
    nt.y = (lip * 0.9 - under * 0.7) * mid + (vnoise(uv / 4.0 + 8.0) - 0.5) * 0.4 * fine;
    col *= 1.0 + 0.1 * lip * mid;
    occ = (1.0 - 0.4 * under * mid) * (1.0 - 0.35 * joint);
    // Grain, pits and the dark streaks of desert varnish running down.
    col *= 1.0 + (vnoise(uv * 1.1) - 0.5) * 0.16 * fine + (vnoise(uv * 0.33 + 9.0) - 0.5) * 0.12 * mid;
    col *= 1.0 - 0.18 * smoothstep(0.6, 0.9, vnoise(float2(uv.x / 3.5, uv.y / 70.0) + 3.0)) * mid;
    return col;
}

// The cliff faces of the reimagined relief (the mesh's own steep faces, Terrain::reimaginedRelief) in
// sandstone, projected in world space on the two vertical axes (nothing stretches down a face):
// replaces the albedo / normal / cavity light by weight w.
void reimCliff(float3 wpos, float3 gn, float px, float w, bool ng, thread float3& albedo, thread float3& n, thread float& ao,
               constant F3U& u3, texture2d_array<float> pa, texture2d_array<float> pn, texture2d_array<float> po, sampler ps,
               float3 dpx, float3 dpy) {
    if (w < 0.004) return;
    float2 bw = pow(abs(gn.xz) + 1e-4, float2(4.0));
    bw = saturate(bw / (bw.x + bw.y) * 1.25 - 0.125);  // (one projection only on most faces)
    const float sx = gn.x >= 0.0 ? 1.0 : -1.0, sz = gn.z >= 0.0 ? 1.0 : -1.0;
    float2 ntX = float2(0.0), ntZ = float2(0.0);
    float oX = 1.0, oZ = 1.0;
    float3 col = float3(0.0);
    if (u3.photo[0].x > 0.5) {
        // Photo terrain: the layered sandstone scan on the two vertical projections (u along the face, v down it),
        // in the procedural sandstone's own ochre (broad beds of rust and buff across the whole cliff).
        const float sc = 1.0 / u3.photo[7].a;
        const float kk = vnoise(wpos.xz / 83.0 + wpos.y / 61.0 + 3.0);
        const float bed = vnoise(float2((wpos.x + wpos.z) / 150.0, wpos.y / 16.0 + 4.0));
        const float3 base = mix(float3(0.68, 0.46, 0.30), float3(0.85, 0.64, 0.43), smoothstep(0.25, 0.7, bed));
        if (bw.x > 0.01) {
            const PhS t = photoTap(pa, pn, po, ps, float2(-wpos.z * sx, -wpos.y) * sc, 5u, u3.photo[7].rgb,
                                   float2(-dpx.z * sx, -dpx.y) * sc, float2(-dpy.z * sx, -dpy.y) * sc, kk);
            col += base * mix(float3(dot(t.d, float3(0.299, 0.587, 0.114))), t.d, 0.6) * bw.x;
            ntX = t.n * (1.3 * u3.photo[0].z);
            oX = mix(1.0, t.orh.r, 0.65);
        }
        if (bw.y > 0.01) {
            const PhS t = photoTap(pa, pn, po, ps, float2(wpos.x * sz, -wpos.y) * sc + 0.43, 5u, u3.photo[7].rgb,
                                   float2(dpx.x * sz, -dpx.y) * sc, float2(dpy.x * sz, -dpy.y) * sc, kk);
            col += base * mix(float3(dot(t.d, float3(0.299, 0.587, 0.114))), t.d, 0.6) * bw.y;
            ntZ = t.n * (1.3 * u3.photo[0].z);
            oZ = mix(1.0, t.orh.r, 0.65);
        }
    } else {
        if (bw.x > 0.01) col += sandstone(float2(-wpos.z * sx, wpos.y), px, ntX, oX) * bw.x;
        if (bw.y > 0.01) col += sandstone(float2(wpos.x * sz, wpos.y) + 311.0, px, ntZ, oZ) * bw.y;
    }
    // Large patches over the whole cliff (sun-bleached / iron-stained rock).
    col *= mix(float3(1.06, 0.97, 0.9), float3(0.94, 1.0, 1.06), vnoise(wpos.xz / 260.0 + wpos.y / 400.0));
    // Normal: the face's own + the tilts (along the face's horizontal tangent, and up).
    const float3 tX = float3(0.0, 0.0, -sx), tZ = float3(sz, 0.0, 0.0);
    const float3 fn = normalize(gn + (tX * ntX.x + float3(0.0, ntX.y, 0.0)) * bw.x + (tZ * ntZ.x + float3(0.0, ntZ.y, 0.0)) * bw.y);
    // A darker foot; the talus skirt (Terrain::reimaginedRelief keeps 6-14 px of the unsculpted
    // profile there) as fallen blocks and scree.
    const float foot = 1.0 - smoothstep(2.0, 22.0, wpos.y);
    col *= 1.0 - 0.25 * foot;
    const float scree = (1.0 - smoothstep(7.0, 15.0, wpos.y + (vnoise(wpos.xz / 6.0) - 0.5) * 8.0)) * (1.0 - smoothstep(0.6, 2.5, px));
    float3 fnS = fn;
    if (scree > 0.01) {
        const float4 pb = pebbles(float2(wpos.x + wpos.z, wpos.y * 1.4), 3.6, 0.8);
        const float on = smoothstep(0.0, 0.25, pb.x);
        col = mix(col, mix(col * 0.55, float3(0.55, 0.42, 0.31) * (0.75 + 0.5 * pb.w), on), scree);
        fnS = normalize(fn + float3(pb.y, pb.x * 0.6, pb.z) * scree);
    }
    // (Realistic grade: the sandstone calmer - the same family as the paler ground, not an orange wall.)
    if (ng) col = mix(float3(dot(col, float3(0.299, 0.587, 0.114))), col, 0.6) * float3(1.0, 0.985, 0.94);
    albedo = mix(albedo, col, w);
    n = normalize(mix(n, fnS, w));
    ao = mix(ao, ao * (oX * bw.x + oZ * bw.y) * (1.0 - 0.3 * foot), w);
}

// Soft cast shadows (the low sun's long cliff shadows): a contact-hardening filter from compares
// only. A fast path (the pixel + 4 taps 7 px away agree: open ground, a shadow core); else 4 taps in 9 world px estimate how far above the receiver the casters are (the share of
// blockers at least 0 / 40 / 120 px above); the penumbra grows with that distance (texW = world px per
// shadow texel, soft = penumbra px per px of distance, at most 7 px); then 8 rotated Poisson taps over it.
constant float2 kDisk[12] = {float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
                              float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
                              float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
float shadowSoft(depth2d<float> sm, sampler ss, float3 lp, float4 sh, float texW, float soft, float2 scr) {
    const float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (sh.x < 0.5 || any(uv <= float2(0.0)) || any(uv >= float2(1.0)) || lp.z >= 1.0) return 1.0;
    const float ref = lp.z - sh.w;
    const float a = 6.2831853 * fract(52.9829189 * fract(dot(scr, float2(0.06711056, 0.00583715))));
    const float ca = cos(a), sa = sin(a);
    const float uvPerPx = sh.y / max(texW, 1e-4);
    // Fast path: the pixel and four taps the widest penumbra away agree (open ground, a shadow's core).
    float q = sm.sample_compare(ss, uv, ref, level(0));
    for (int i = 0; i < 4; ++i) {
        const float2 d = float2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0);
        q += sm.sample_compare(ss, uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * (7.0 * uvPerPx), ref, level(0));
    }
    if (q > 4.995) return 1.0;
    if (q < 0.005) return 0.0;
    float o0 = 0.0, o1 = 0.0, o2 = 0.0;
    for (int i = 0; i < 4; ++i) {
        const float2 d = kDisk[i * 3];
        const float2 q = uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * (9.0 * uvPerPx);
        o0 += 1.0 - sm.sample_compare(ss, q, ref, level(0));
        o1 += 1.0 - sm.sample_compare(ss, q, ref - 40.0 * sh.w, level(0));
        o2 += 1.0 - sm.sample_compare(ss, q, ref - 120.0 * sh.w, level(0));
    }
    if (o0 < 0.01) return 1.0;
    const float dist = 12.0 + 70.0 * (o1 / o0) + 130.0 * (o2 / o0);
    const float r = clamp(0.6 + dist * soft, 0.6, 7.0) * uvPerPx;
    float s = 0.0;
    for (int i = 0; i < 8; ++i) {
        const float2 d = kDisk[i];
        s += sm.sample_compare(ss, uv + float2(d.x * ca - d.y * sa, d.x * sa + d.y * ca) * r, ref, level(0));
    }
    return s / 8.0;
}

)MSL" R"MSL(
// The reimagined surface over the faithful one: one warm, low-saturation palette (sand and rock told
// apart by height, texture and shadow, not by hue), wind ripples and gravel on the sand, rubble at the
// foot of rock, cracks in the rock, strata bands across the cliff faces, spice as scattered rust
// granules, cavity occlusion (ao) and the spice's glow (emissive, added after the light).
void reimagine(float2 p, float3 wpos, float rockW, float faceW, float spice, float rich, float cw, float px, float4 g,
               float4 gb, float3 eye, thread float3& albedo, thread float3& n, thread float& ao, thread float& glow,
               constant F3U& u3, texture2d_array<float> pa, texture2d_array<float> pn, texture2d_array<float> po, sampler ps,
               float2 gx, float2 gy, float dunes, float crag, thread float& gloss) {
    const float3 lumW = float3(0.299, 0.587, 0.114);
    const float sandW = (1.0 - rockW) * (1.0 - cw);
    const float rockOnly = rockW * (1.0 - cw);
    const float fine = 1.0 - smoothstep(0.5, 1.8, px);  // features of a few px: fade into their mean
    const float mid = 1.0 - smoothstep(1.5, 5.0, px);
    // Palette (round 2): warm Dune ochre - the sand a golden ochre, the rock a darker rust-brown; the
    // spice keeps its own colour.
    const float L = dot(albedo, lumW);
    const float3 tanTint = float3(1.05, 0.98, 0.86);
    // Realistic grade (docs/3d-mode.md "Realistic grade"): the calmer palette - pale ochre / khaki sand, the
    // buildable rock a greyer brown with 40 % less contrast (its detail stays in the normals), the spice a dark
    // rust / cinnamon. Off: the vivid round-2 palette below, unchanged.
    const bool ng = u3.reim[3].w > 0.5;
    const float rockLift = rockOnly * (1.0 - spice);
    const float Lr = mix(L, 0.36, 0.45 * faceW);  // calmer faces: the original's near-black gullies lifted
    if (ng) {
        albedo = mix(float3(L), albedo, mix(0.32, 0.5, spice)) * mix(float3(1.0, 0.95, 0.845), float3(1.08, 0.98, 0.83), spice);
        albedo = mix(albedo, float3(L) * float3(0.9, 0.89, 0.88), 0.55 * cw);  // the slabs: a cooler grey, man-made
        const float Lm = dot(mix(u3.level[2].rgb, u3.level[3].rgb, 0.6), lumW);  // the plain rock's mean tone
        const float Lc = mix(mix(L, Lm, 0.4), Lr, faceW);
        albedo *= mix(1.0, Lc / max(L, 0.03), rockLift);
        albedo = mix(albedo, float3(Lc) * float3(1.16, 0.98, 0.78) * (1.12 + 0.3 * faceW), rockLift * mix(0.7, 0.9, faceW));
    } else {
        albedo = mix(float3(L), albedo, mix(0.42, 0.52, spice)) * float3(1.08, 0.98, 0.83);
        // Rock: rust-brown, its dark original tones lifted a little (faces brightest).
        albedo = mix(albedo, float3(Lr) * float3(1.2, 0.96, 0.76) * (1.05 + 0.35 * faceW), rockLift * mix(0.6, 0.9, faceW));
    }
    // Large-scale tone variation of the open ground.
    albedo *= 1.0 + (vnoise(p / 300.0 + 11.0) - 0.5) * 0.14 * sandW;
    // Sand: wind ripples (crests across the wind, bent by noise), gravel, fine grain.
    const float2 wind = u3.reim[1].xy;
    const float warp = vnoise(p / 41.0) * 16.0 + vnoise(p / 9.0 + 4.0) * 3.0;
    const float ph = (dot(p, wind) + warp) * (6.2831853 / 7.0);
    const float ripA = sandW * (1.0 - spice) * mid * u3.reim[0].z * smoothstep(0.3, 0.75, vnoise(p / 63.0 + 3.0)) *
                       (1.0 - saturate(g.g * 6.0));
    const bool photo = u3.photo[0].x > 0.5;
    if (!photo) {
        n += float3(wind.x, 0.0, wind.y) * (-0.28 * cos(ph) * ripA);
        albedo *= 1.0 + 0.03 * sin(ph) * ripA;
        const float4 pb = pebbles(p, 3.4, 0.5);
        const float grav = sandW * (1.0 - spice) * fine * u3.reim[0].w;
        n += float3(pb.y, 0.0, pb.z) * (pb.x * 0.9 * grav);
        albedo *= mix(1.0, 0.8 + 0.4 * pb.w, smoothstep(0.0, 0.3, pb.x) * grav);
        albedo *= 1.0 + (vnoise(p * 1.7) - 0.5) * 0.12 * fine * sandW;
    }
    // Spice: thousands of small rust granules, denser and darker where it is thick, thinning out at
    // the edges, each lit as a little dome.
    if (spice > 0.0) {
        // (Round 2: the field itself a deeper orange-red, darker where thick.)
        albedo *= mix(float3(1.0), mix(float3(0.95, 0.78, 0.66), float3(0.8, 0.58, 0.48), rich), saturate(spice * 1.4));
        if (ng) {  // rust / cinnamon, not orange: darker, thick spice darkest
            const float Ls = dot(albedo, lumW);
            albedo = mix(albedo, float3(Ls) * mix(float3(1.42, 0.84, 0.52), float3(1.42, 0.78, 0.46), rich) * mix(0.9, 0.72, rich),
                         0.85 * saturate(spice * 1.4));
        }
        // Grainy at every zoom: mottled clumps (6 px) and grit (2 px, faded out when smaller than a pixel).
        const float mott = vnoise(p / 6.0 + 13.0) * 0.6 + mix(0.5, vnoise(p / 2.2 + 5.0), mid) * 0.4;
        albedo *= mix(1.0, 0.8 + 0.4 * mott, saturate(spice * 1.4));
        const float4 sg = pebbles(p + 41.0, 2.6, 0.25 + 0.7 * saturate(spice * (0.6 + 0.6 * rich)));
        const float on = smoothstep(0.0, 0.25, sg.x) * saturate(spice * 1.6) * mid;
        const float3 rust = (ng ? mix(float3(0.62, 0.3, 0.15), float3(0.45, 0.18, 0.08), rich)
                                : mix(float3(0.78, 0.38, 0.17), float3(0.58, 0.2, 0.08), rich)) * (0.8 + 0.4 * sg.w);
        albedo = mix(albedo, rust, on);
        n += float3(sg.y, 0.0, sg.z) * (sg.x * 1.1 * on);
        ao *= 1.0 - 0.18 * saturate(spice * 1.6) * (1.0 - smoothstep(0.0, 0.2, sg.x)) * mid;
        const float2 cell = floor(p * 1.25);
        const float tw = hash12(cell + 11.1);  // fixed to the ground: a view-dependent twinkle flickered while scrolling (owner, 2026-11-18)
        const float sparkle = step(0.965, hash12(cell + 0.37)) * smoothstep(0.55, 1.0, tw);
        glow += spice * (u3.reim[0].y * (0.35 + 0.65 * vnoise(p / 9.0)) + sparkle * fine * 0.35);
    }
    // Rubble on the sand along the foot of rock (gb.r: the rock coverage, blurred).
    const float edge = smoothstep(0.04, 0.3, gb.r) * (1.0 - rockW) * (1.0 - cw) * (1.0 - spice);
    if (edge > 0.001) {
        const float4 rb = pebbles(p + 17.0, 7.5, 0.25 + 0.55 * edge);
        const float3 rockC = mix(float3(dot(u3.level[2].rgb, lumW)), u3.level[2].rgb, 0.5) * tanTint;
        const float on = smoothstep(0.0, 0.2, rb.x) * edge;
        albedo = mix(albedo, rockC * (0.75 + 0.45 * rb.w), on);
        n += float3(rb.y, 0.0, rb.z) * (rb.x * 1.3 * edge);
        ao *= 1.0 - 0.25 * edge * (1.0 - smoothstep(0.0, 0.15, rb.x));
    }
    // Rock: cracks (thin dark lines along noise contours) in patches.
    const float c1 = abs(vnoise(p / 11.0 + 3.0) - 0.5), c2 = abs(vnoise(p / 4.3 + 7.0) - 0.5);
    const float crack = ((1.0 - smoothstep(0.0, 0.016 + px * 0.006, c1)) * smoothstep(0.55, 0.75, vnoise(p / 37.0 + 1.0)) * 0.8 +
                         0.5 * (1.0 - smoothstep(0.0, 0.02, c2)) * smoothstep(0.45, 0.7, vnoise(p / 13.0 + 5.0)) * fine) * rockOnly * mid;
    if (!photo) {
        albedo *= 1.0 - (ng ? 0.27 : 0.45) * saturate(crack);  // (Realistic grade: the cracks mostly in the cavity light)
        ao *= 1.0 - 0.3 * saturate(crack);
    }
    // Cliff faces: strata bands by height, a ledge line at the top of each band.
    const float face = photo ? 0.0 : faceW * rockOnly * u3.reim[1].z;
    if (face > 0.001) {
        const float st = wpos.y / 12.0 + vnoise(p / 25.0) * 1.1 + vnoise(p / 6.0) * 0.25;
        const float band = fract(st), id = floor(st);
        const float ledge = 1.0 - smoothstep(0.0, 0.14, band);
        albedo *= mix(1.0, (0.9 + 0.2 * hash12(float2(id, 3.7))) * (1.0 - 0.18 * ledge), face);
        n.y += ledge * face * 0.5;
    }
    // Cavities: ground lower than its surroundings (crevices, the foot of a cliff).
    ao *= 1.0 - u3.reim[1].w * saturate((gb.g - g.g) * 3.0);
    if (photo) {
        // Photo terrain: the scans' detail over the palette's colour. Fine sand on the open desert, the rippled
        // scan on dune cells, coarse stony sand in patches / along the foot of rock / under the spice; cracked
        // sandstone on plain rock, fractured rock where the original draws it broken; weathered concrete on slabs.
        const float kk = vnoise(p / 97.0 + 5.0);
        const float patch = smoothstep(0.5, 0.72, vnoise(p / 120.0 + 7.0) * 0.65 + vnoise(p / 29.0 + 2.0) * 0.35);
        // The stony scan shows by its own height: everywhere its tallest stones lie strewn over the fine sand, in
        // the patches / along the rock / under the spice most of it.
        PhS tg;
        tg.d = float3(1.0);
        tg.n = float2(0.0);
        tg.orh = float3(1.0);
        float wGrav = 0.0;
        if (sandW >= 0.02) {
            const float sc = 1.0 / u3.photo[4].a;
            tg = photoTap(pa, pn, po, ps, p * sc, 2u, u3.photo[4].rgb, gx * sc, gy * sc, kk);
            tg.d = max(1.0 + (tg.d - 1.0) * 1.15, float3(0.62));  // (its darkest debris lifted: stones, not dirt)
            const float cover = mix(mix(0.27, 0.92, saturate(max(patch, edge * 1.5))), 1.0, saturate(spice * 1.5));
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
        float3 d = float3(0.0), orh = float3(0.0);
        float2 nn = float2(0.0);
        float sum = 0.0;
        for (int i = 0; i < 7; ++i) {
            if (wl[i] < 0.02) continue;
            if (i == 2) {
                photoAdd(tg, wl[i], 1.0, d, nn, orh, sum);
                continue;
            }
            const float sc = 1.0 / u3.photo[2 + i].a;
            PhS t = photoTap(pa, pn, po, ps, p * sc, uint(i), u3.photo[2 + i].rgb, gx * sc, gy * sc, kk);
            // (The sand scans are pale and even: their grain a little stronger, so it reads at an RTS camera's
            // distance. Normal strength: the ripples and the rock carry their shape in the normal.)
            t.d = 1.0 + (t.d - 1.0) * (i == 0 ? 1.7 : i == 1 ? 1.4 : i <= 4 ? 1.15 : 0.8);
            if (i == 6) t.d = max(t.d, float3(0.72));  // (the slabs' own stains are drawn by concreteSlab3)
            photoAdd(t, wl[i], i == 1 ? 1.25 : i >= 3 && i <= 4 ? 1.25 : i == 6 ? 0.6 : 1.0, d, nn, orh, sum);
        }
        if (sum > 0.0) {
            d /= sum;
            nn /= sum;
            orh /= sum;
            // (Part of the scans' own hue differences kept; under the spice the grain only, the colour is the spice's.)
            const float3 dc = mix(float3(dot(d, lumW)), d, 0.55 * (1.0 - saturate(spice * 1.5)));
            // (The palette's brightness kept: the scans' cavities and rougher normals take some light away.)
            albedo *= mix(float3(1.0), dc, u3.photo[0].y) * (1.0 + float3(0.2, 0.2, 0.18) * rockOnly + (0.05 + 0.05 * dunes) * sandW + 0.22 * cw);
            // (Broad warm / cool patches over the rock: one scan over a whole plateau is otherwise one flat brown.)
            albedo *= mix(float3(1.0), mix(float3(1.07, 0.99, 0.9), float3(0.94, 1.0, 1.05), vnoise(p / 230.0 + 17.0)) *
                                           (0.93 + 0.14 * vnoise(p / 95.0 + 41.0)), rockOnly);
            n += float3(nn.x, 0.0, -nn.y) * u3.photo[0].z;
            ao *= mix(1.0, orh.r, 0.75);
            gloss = (1.0 - orh.g) * (1.0 - spice);
        }
    }
}
)MSL";

// The fragment function itself (a separate literal: MSVC's 64 KB limit; shader_blobs.cpp appends it to kTerrain3Body).
inline constexpr const char* kTerrain3Main = R"MSL(
// Reimagined spice relief, fine part (docs/3d-mode.md "Spice fields in 3D"; HLSL twin spiceRelH): small wind
// drifts across the wind (crests ~10 px apart, a long windward rise and a short lee slope, bent by noise) and
// crumbly lumps, 0..~1.6 (x the amplitude in px). The mesh carries the mounds (Terrain::spiceRelief); this is
// the detail under them, for the normals and the self-shadow at the low sun.
float spiceRelH(float2 p, float2 wind) {
    const float warp = vnoise(p / 23.0 + 7.0) * 9.0 + vnoise(p / 6.0 + 2.0) * 2.2;
    const float t = fract((dot(p, wind) + warp) / 10.0);
    const float r = t < 0.78 ? smoothstep(0.0, 0.78, t) : 1.0 - smoothstep(0.78, 1.0, t);
    const float m = vnoise(p / 3.6 + 13.0);
    return r * (0.35 + 0.65 * vnoise(p / 29.0 + 5.0)) + 1.1 * m * m;
}

fragment float4 terrain3_fs(VOut in [[stage_in]], constant FU& u [[buffer(0)]], constant PointLights& pl [[buffer(1)]],
                            constant F3U& u3 [[buffer(2)]], const device uint4* plg [[buffer(3)]],
                            texture2d_array<float> alb [[texture(0)]], texture2d_array<float> nor [[texture(1)]],
                            texture2d<float> splatA [[texture(2)]], texture2d<float> splatB [[texture(3)]],
                            texture2d<float> spiceMap [[texture(4)]], texture2d<float> shroud [[texture(5)]],
                            depth2d<float> shadowMap [[texture(6)]], texture2d<float> guide [[texture(7)]],
                            texture2d<float> farMap [[texture(8)]],
                            texture2d_array<float> phAlb [[texture(9)]], texture2d_array<float> phNor [[texture(10)]],
                            texture2d_array<float> phOrh [[texture(11)]], texture2d<float> ssaoTex [[texture(12)]],
)MSL" R"MSL(                            sampler matSamp [[sampler(0)]], sampler matSamp2 [[sampler(1)]],
                            sampler splatSamp [[sampler(2)]], sampler splatSamp2 [[sampler(3)]],
                            sampler spiceSamp [[sampler(4)]], sampler shroudSamp [[sampler(5)]],
                            sampler shadowSamp [[sampler(6)]], sampler guideSamp [[sampler(7)]],
                            sampler farSamp [[sampler(8)]], sampler phSamp [[sampler(9)]], sampler phSamp2 [[sampler(10)]],
                            sampler phSamp3 [[sampler(11)]], sampler ssaoSamp [[sampler(12)]]) {
    const float2 p = in.wpos.xz;
    const float2 mapSize = u.fog2.yz;
    // The world beyond the map (docs/3d-mode.md): sk = its margin (px), the mode (2 = this draw is the land
    // beyond: the map's textures mirrored across the edge by their samplers, blended into the far texture's
    // procedural desert by its weight wF). fog2.w 3 = shroud without the edge fade (2: neither).
    float4 sk = float4(u3.reim[2].yw, 0.5, 0.0);
    const bool ring = sk.y > 1.5;
    // (The procedural rock's tone and ruggedness: mottled like the map's own broken rock, in patches.)
    if (ring) sk.zw = float2(0.5 + (vnoise(p / 15.0 + 3.0) * 0.6 + vnoise(p / 4.5 + 9.0) * 0.4 - 0.5) * 0.55,
                             smoothstep(0.38, 0.7, vnoise(p / 70.0 + 17.0)) * 0.75);
    const float2 outXZ = max(max(-p, p - mapSize), float2(0.0));
    const float dOut = length(outXZ);
    float4 fr = float4(0.0);
    if (ring) fr = farMap.sample(farSamp, (p + sk.x + 4.0) / (mapSize + 2.0 * sk.x + 8.0), level(0.0));
    const float wF = fr.b;
    // Shroud / the map's edge (fog2.w: 0 no shroud, 1 shroud, 2 baked for the Enhanced 2D view: no
    // shroud, no edge fade). Black under full shroud: a whole SIMD group of such pixels leaves here
    // (the result is the same: the end multiplies by 1 - s; the others' derivatives stay intact).
    float s = 0.0;
    if (u.fog2.w > 0.5 && u.fog2.w < 1.5) s = shroud.sample(shroudSamp, p / mapSize).a;
    if (u.fog2.w > 2.5) s = shroudAt(pl, shroud, shroudSamp, p, mapSize);  // (beyond the map: the fixed fog level)
    if (u.fog2.w < 1.5) {
        const float2 ed = min(p, mapSize - p);
        s = max(s, 1.0 - smoothstep(0.0, 8.0, min(ed.x, ed.y)));
    }
    if (simd_all(s >= 1.0)) return float4(0.0, 0.0, 0.0, 1.0);
    // Per cell (DesignedTerrain::buildSpiceTex): b > 0 = spice within reach of spiceField, a > 0 =
    // relief (guide height) within reach of the slope / rim taps below. Without them those reads
    // are all zeros: they are skipped (the same result, a fraction of the work on open sand).
    const float4 near = spiceMap.sample(spiceSamp, p / mapSize);
    const float3 dpx = dfdx(in.wpos), dpy = dfdy(in.wpos);
    const float3 gn = normalize(in.nrm);
    const bool bake = u3.guide.z > 0.5;

    float4 a = splatA.sample(splatSamp, p / mapSize);
    float4 b = splatB.sample(splatSamp2, p / mapSize);
    if (ring) {  // beyond the map: dunes and the far texture's cliff mask further out; no concrete
        a = mix(a, float4(0.0, 1.0, 0.0, fr.a), wF);
        b.y = 0.0;
    }
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
    const float4 g = guideX(guide, guideSamp, farMap, farSamp, pg, mapSize, tpp, lod, sk);
    // Macro slope (central differences 1.5 px apart).
    const float e = 1.5;
    const bool relief = near.a > 0.0 || lod >= 2.0 || ring;
    float hE = g.g, hW = g.g, hS = g.g, hN = g.g;
    if (relief) {
        hE = guideX(guide, guideSamp, farMap, farSamp, pg + float2(e, 0.0), mapSize, tpp, lod, sk).g;
        hW = guideX(guide, guideSamp, farMap, farSamp, pg - float2(e, 0.0), mapSize, tpp, lod, sk).g;
        hS = guideX(guide, guideSamp, farMap, farSamp, pg + float2(0.0, e), mapSize, tpp, lod, sk).g;
        hN = guideX(guide, guideSamp, farMap, farSamp, pg - float2(0.0, e), mapSize, tpp, lod, sk).g;
    }
    const float2 mg = float2(macroH(hE, u3) - macroH(hW, u3), macroH(hS, u3) - macroH(hN, u3)) * (u3.guide.x / (2.0 * e));  // px rise per px
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
    float4 gb = guide.sample(guideSamp, guv, level(lod + 3.0));  // the surroundings (blurred)
    if (ring) gb = mix(gb, float4(fr.r, fr.g, 0.5, sk.w), wF);
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
    float gUp = g.g, gDn = g.g;
    if (relief) {
        gUp = guideX(guide, guideSamp, farMap, farSamp, pg + dir * 2.0, mapSize, tpp, lod, sk).g;
        gDn = guideX(guide, guideSamp, farMap, farSamp, pg - dir * 2.0, mapSize, tpp, lod, sk).g;
    }
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
        const float4 g1 = guideX(guide, guideSamp, farMap, farSamp, pg + float2(d, 0.0), mapSize, tpp, lod, sk);
        const float4 g2 = guideX(guide, guideSamp, farMap, farSamp, pg + float2(0.0, d), mapSize, tpp, lod, sk);
        // Baked from above the macro shape is shaded by the cliff shading above (the original's
        // look: tops lit, faces dark), not by this normal: only the small relief catches the sun.
        const float h0 = faithHeight(g, u3, !bake);
        float2 grad = float2(faithHeight(g1, u3, !bake) - h0, faithHeight(g2, u3, !bake) - h0) / d;
        // (Photo terrain: on the calm photographed rock the guide's 8-bit height steps would show as contour
        // lines under the low sun - slopes that small are left to the mesh.)
        if (u3.reim[0].x > 0.5 && u3.photo[0].x > 0.5) grad *= smoothstep(0.2, 0.6, length(grad));
        rn = normalize(float3(-grad.x, 1.0, -grad.y));
        if (ring) rn = normalize(mix(rn, gn, wF));  // (the far texture is coarse: the mesh's own slope there)
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
    float2 sf = float2(0.0);
    float spice = 0.0;  // (no spice field within reach: presence and richness 0, so no spice)
    // (Beyond the map: a little spice at the very edge at most.)
    const float spiceOut = 1.0 - smoothstep(6.0, 36.0, dOut);
    if (near.b > 0.0 && spiceOut > 0.0) {
        sf = spiceField(spiceMap, spiceSamp, p, mapSize) * spiceOut;
        const float amount = (sf.x + sf.y) * (2.0 / 7.0);
        const float grains = vnoise(p / 5.0) * 0.6 + vnoise(p / 15.0) * 0.4;
        // The edge of a field dissolves: the presence against a noise threshold - lobes and specks
        // thinning out into the sand, no line.
        const float edgeN = vnoise(p / 11.0 + 4.0) * 0.5 + vnoise(p / 4.0 + 8.0) * 0.3 + vnoise(p / 1.7) * 0.2;
        const float cover = smoothstep(-0.12, 0.2, sf.x - (0.15 + 0.6 * edgeN));
        spice = mix(cover * 0.78, 1.0, sf.y) * smoothstep(1.0, 0.55, grains - amount * 0.3) * 0.92;
        spice *= saturate((1.0 - rockW) * 1.6);
        if (u3.reim[0].x > 0.5) {
            // Reimagined: strays just beyond the field's edge - specks and small patches of spice on the sand.
            const float halo = smoothstep(0.03, 0.25, sf.x) * (1.0 - cover) * smoothstep(0.35, 0.7, vnoise(p / 13.0 + 3.0));
            if (halo > 0.0) spice = max(spice, halo * spots(p + 7.3, 7.0, 0.9) * 0.8 * saturate((1.0 - rockW) * 1.6));
        }
    }
    w[0] *= 1.0 - spice;
    w[1] *= 1.0 - spice;
    w[5] = spice;

    const float k = vnoise(p / 160.0);
    // (Photo terrain: the old scans give the palette and the height blend only - most of their own light and
    // dark and of their normals is taken out, the photographed detail comes on top in reimagine().)
    const bool calm = u3.reim[0].x > 0.5 && u3.photo[0].x > 0.5;
    // (Reimagined sandstone cliffs replace a steep face's material entirely: no triplanar taps there.)
    const bool reimFace = u3.reim[0].x > 0.5 && u3.reim[3].z > 0.5 && gn.y < 0.42 && cw < 0.004;
    const bool steepGeo = !bake && gn.y < 0.82 && !reimFace;  // (baked from above: the mesh only casts shadows)
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
        if (calm) {  // (the scan's mean colour: its smallest mip)
            m.c.rgb = mix(alb.sample(matSamp, float2(0.5), layer, level(16.0)).rgb, m.c.rgb, 0.3);
            n = mix(base, n, 0.25);
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
    float cwR = cw;  // the concrete left bare by the sand (Realistic grade)
    if (u3.reim[0].x > 0.5 && u3.reim[3].w > 0.5 && b.a > 0.03 && !ring) {
        // Realistic grade: no straight grid line where the slabs end - sand lies over their edge, ragged (in
        // places not at all, in places tongues of it reach 10-15 px in, breaking up into grains), a thin veil
        // of it further in, and a little drifted sand on the ground along the slabs. b.a = the concrete's
        // coverage blurred (DesignedTerrain::buildSplat): 0.5 on the border, ~1 / 24 per px into (+) and out
        // of (-) the slabs.
        const float nl = vnoise(p / 17.0 + 23.0), nm = vnoise(p / 5.3 + 41.0), nf = vnoise(p / 2.1 + 7.0);
        const float din = (b.a - 0.5) * 24.0;
        const float reach = 0.6 + 13.0 * smoothstep(0.38, 0.8, nl) + 3.0 * nm * nm;
        const float over = 1.0 - smoothstep(reach * 0.25, reach, din + (nf - 0.5) * 0.7 * reach);
        const float veil = (1.0 - smoothstep(2.0, 13.0, din)) * smoothstep(0.4, 0.7, nm * 0.6 + nf * 0.4) * 0.3;
        const float apron = smoothstep(-8.0, -0.5, din) * (0.2 + 0.5 * nl) * (0.6 + 0.4 * nf);
        const float sd = mix(apron, max(over, veil), cw);
        albedo = mix(albedo, u3.level[4].rgb * (0.9 + 0.14 * nm), sd);
        cwR = cw * (1.0 - max(over, veil));
        n = normalize(mix(n, base, sd * 0.8));
    }
)MSL" R"MSL(    if (spice > 0.0) {
        const float mound = spots(p + (float2(vnoise(p / 23.0 + 1.3), vnoise(p / 23.0 + 9.1)) - 0.5) * 12.0, 13.0, 0.3 + 0.7 * sf.y);
        albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.85, 0.61, 0.45), spice * mound);
        albedo *= mix(float3(1.0, 1.0, 1.0), float3(0.95, 0.84, 0.74), spice * smoothstep(0.1, 0.8, sf.y));  // richer, redder where thick
    }

    float ao = 1.0, glow = 0.0, gloss = 0.0;
    const bool reim = u3.reim[0].x > 0.5;
    if (reim) {
        reimagine(p, in.wpos, rockW, faceW, spice, sf.y, cwR, pxWorld, g, gb, u.fog.xyz, albedo, n, ao, glow, u3, phAlb, phNor,
                  phOrh, phSamp, dpx.xz, dpy.xz, dunes, crag, gloss);
        // The spice's fine relief (spiceRelH, amplitude up to ~1 px on thick spice): its normal and its self-shadow
        // towards the low sun (two steps along the sun; a step higher than the sun's ray shades this point).
        const float spA = spice * (0.55 + 0.45 * sf.y) * (1.0 - smoothstep(1.2, 3.5, pxWorld)) * (1.0 - cwR);
        if (spA > 0.01) {
            const float2 wind = u3.reim[1].xy;
            const float h0 = spiceRelH(p, wind);
            const float hx = spiceRelH(p + float2(0.8, 0.0), wind), hz = spiceRelH(p + float2(0.0, 0.8), wind);
            n = normalize(n) - float3(hx - h0, 0.0, hz - h0) * (spA / 0.8);
            const float2 sd = normalize(u.sunDir.xz);
            const float tanE = u.sunDir.y / max(length(u.sunDir.xz), 1e-3);
            const float o1 = (spiceRelH(p + sd * 1.6, wind) - h0) * spA - 1.6 * tanE;
            const float o2 = (spiceRelH(p + sd * 3.6, wind) - h0) * spA - 3.6 * tanE;
            ao *= 1.0 - 0.55 * saturate(max(o1, o2) * 2.5) * saturate(spice * 1.5);
        }
        n = normalize(n);
        // Round 2: the mesh's steep faces in world-space sandstone, lit by their own relief.
        const float cliffGeo = (1.0 - smoothstep(0.42, 0.78, gn.y)) * (1.0 - cw) * u3.reim[3].z;
        reimCliff(in.wpos, gn, pxWorld, cliffGeo, u3.reim[3].w > 0.5, albedo, n, ao, u3, phAlb, phNor, phOrh, phSamp, dpx, dpy);
        reliefW = mix(reliefW, 1.0, cliffGeo);
    }

    // Light. Level ground: the fine detail under detailSun (sand and plain rock exactly as the
    // Designed terrain shows them), darkened by the sun shadow map. Broken rock: its small relief
    // under sunDir. Baked from above, a face is shaded by the slope shading above, so the shadow
    // map darkens it only a little (it is for the shadows cast onto the ground and the tops).
    // (Baked: the mesh turns a steep face into a ramp that reaches a few px over the sand at its
    // foot. On the lee side that sand is looked up at ground level, under the ramp: the cast
    // shadow then starts at the rock, not a ramp's width away from it.)
    float3 sp = in.wpos;
    if (bake && rockW < 0.5 && gn.y < 0.85 && dot(gn.xz, u.sunDir.xz) < 0.0) sp.y = 0.0;
    float sm = in.sun;
    if (u.shadow.x > 0.5) {
        const float3 lp = (u.lightVP * float4(sp, 1.0)).xyz;
        sm = reim && u3.reim[3].y > 0.0 ? shadowSoft(shadowMap, shadowSamp, lp, u.shadow, u3.reim[3].x, u3.reim[3].y, in.pos.xy)
                                         : shadowPCF(shadowMap, shadowSamp, lp, u.shadow);
    }
    const float visAll = mix(u.light.w, 1.0, sm);
    const float vis = bake ? mix(visAll, 1.0, slopeW * 0.7 * rockW) : visAll;
    // Realistic grade: ground occlusion - the sky light is cut round the foot of buildings and walls (splat B.b:
    // their cells, blurred; DesignedTerrain::buildSplat), a soft dark band where a wall meets the ground. 1 = none.
    const float aoG = reim && u3.reim[3].w > 0.5 && !ring ? 1.0 - 0.62 * smoothstep(0.04, 0.72, b.b) : 1.0;
    const float aoS = 1.0 - 0.2 * (1.0 - aoG);  // (a little of the sun too: a contact shadow)
    // Ambient occlusion (docs/3d-mode.md): the screen-space term on the sky light only - the darker of it and the
    // buildings' ground occlusion (the same crease: not both).
    const float aoA = min(aoG, ssaoAt(pl, ssaoTex, ssaoSamp, in.pos.xy));
    const float3 shadeFlat = sunAmb(pl, u.light.x * aoA, u.light.y * max(dot(n, u3.detailSun.xyz), 0.0) * vis * aoS) * u3.detailSun.w;
    const float3 shadeRelief = sunAmb(pl, u3.reliefLight.x * aoA, u3.reliefLight.y * max(dot(n, u.sunDir.xyz), 0.0) * vis * aoS) * u3.reliefLight.z;
    const float3 shade = mix(shadeFlat, shadeRelief, reliefW);
    float3 rgb = satur(pl, albedo) * shade + albedo * pointLights(pl, plg, in.wpos, n, 0.3);
    if (reim) {
        // Shadows take the sky's blue (not black); cavities darken; the spice glows.
        // (Round 2: faces turned away from the sun take the sky's blue too.)
        const float lit = sm * saturate(dot(n, u.sunDir.xyz) * 4.0 + 0.15);
        rgb *= mix(float3(1.0 - 0.13 * u3.reim[2].z, 1.0 - 0.07 * u3.reim[2].z, 1.0 + 0.06 * u3.reim[2].z), float3(1.0), lit) * ao;
        rgb += float3(1.0, 0.42, 0.1) * glow;
        if (gloss > 0.0) {  // Photo terrain: a faint sheen of the low sun on the smoother surfaces (the scans' roughness)
            const float3 hv = normalize(u.sunDir.xyz + normalize(u.fog.xyz - in.wpos));
            rgb += pl.sunColor.rgb * (pow(max(dot(n, hv), 0.0), 18.0) * gloss * gloss * u3.photo[0].w * sm * visAll);
        }
    }
    rgb *= dustHaze(pl, in.wpos);  // Reimagined: drifting dust veils (0 strength = off)
    if (sk.y > 0.5) {
        // The world beyond the map: the land out there a little darker and greyer (clearly not the
        // battlefield), and a faint pale line along the map's edge.
        // (With the shroud's fixed fog out there - PointLights::edge - a little greyer and less dark: the two together
        // ~44 % of the lit land, not near-black.)
        const float outW = smoothstep(0.0, 20.0, dOut);
        const float2 bt = pl.edge.x > 0.5 ? float2(0.3, 0.12) : float2(0.22, 0.15);
        rgb = mix(rgb, float3(dot(rgb, float3(0.299, 0.587, 0.114))), bt.x * outW) * (1.0 - bt.y * outW);
        const float2 ei = min(p, mapSize - p);
        const float dl = ring ? dOut : max(min(ei.x, ei.y), 0.0);
        const float lw = max(2.2, pxWorld * 1.3);
        rgb += float3(1.0, 0.88, 0.66) * (0.055 * exp(-dl * dl / (lw * lw)) * min(1.0, 2.2 / lw * 1.6));
    }
    const float dist = distance(in.wpos, u.fog.xyz);
    float f = smoothstep(u.fog.w, u.fog2.x, dist) * aerialK(pl);
    if (sk.y > 0.5) f = max(f, edgeHazeK(pl, p, mapSize));  // (beyond the map: into the haze with distance from it)
    rgb = mix(rgb, u.fogColor.rgb, f) * (1.0 - s);
    return float4(rgb, 1.0);
}
)MSL";

}  // namespace d2k::render3d::msl
