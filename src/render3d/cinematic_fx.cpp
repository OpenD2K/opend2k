#include "render3d/cinematic_fx.hpp"

#include "game/world.hpp"
#include "render/effects.hpp"
#include "render3d/gpu.hpp"
#include "render3d/shader_blobs.hpp"
#include "render3d/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace d2k::render3d {

namespace {

constexpr int kNoiseSize = 64;  // the 3D noise texture: 64^3 RGBA8 (1 MB), tileable
constexpr int kFloats = 32;     // per instance (8 x float4: cinematic_msl.hpp FxIn)

struct FxVU {
    float viewProj[16];
    float right[4], up[4], eye[4];
};
struct FxFU {
    float sunDir[4], sunCol[4], ambCol[4], fogColor[4], fog[4], fog2[4];
};

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

// ---- The noise texture (CPU, once, on a worker thread): r = a Perlin-Worley value (billowy cells with
// fbm detail), gba = its gradient (for the puffs' normals). Periodic in all three axes.
std::uint32_t ihash(int x, int y, int z, int s) {
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u + std::uint32_t(z) * 2147483647u +
                      std::uint32_t(s) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
int wrap(int a, int n) { return ((a % n) + n) % n; }
float perlin(float x, float y, float z, int period, int seed) {
    static const float g[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                   {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z));
    const float fx = x - float(xi), fy = y - float(yi), fz = z - float(zi);
    auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const float ux = fade(fx), uy = fade(fy), uz = fade(fz);
    auto dotg = [&](int a, int b, int c, float dx, float dy, float dz) {
        const float* v = g[ihash(wrap(a, period), wrap(b, period), wrap(c, period), seed) % 12];
        return v[0] * dx + v[1] * dy + v[2] * dz;
    };
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerp(dotg(xi, yi, zi, fx, fy, fz), dotg(xi + 1, yi, zi, fx - 1, fy, fz), ux);
    const float x10 = lerp(dotg(xi, yi + 1, zi, fx, fy - 1, fz), dotg(xi + 1, yi + 1, zi, fx - 1, fy - 1, fz), ux);
    const float x01 = lerp(dotg(xi, yi, zi + 1, fx, fy, fz - 1), dotg(xi + 1, yi, zi + 1, fx - 1, fy, fz - 1), ux);
    const float x11 = lerp(dotg(xi, yi + 1, zi + 1, fx, fy - 1, fz - 1), dotg(xi + 1, yi + 1, zi + 1, fx - 1, fy - 1, fz - 1), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}
// Distance to the nearest feature point (one per cell), x in cell units; 0..~1.
float worley(float x, float y, float z, int cells, int seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z));
    float best = 9;
    for (int c = -1; c <= 1; ++c)
        for (int b = -1; b <= 1; ++b)
            for (int a = -1; a <= 1; ++a) {
                const int cx = xi + a, cy = yi + b, cz = zi + c;
                const std::uint32_t h = ihash(wrap(cx, cells), wrap(cy, cells), wrap(cz, cells), seed);
                const float px = float(cx) + float(h & 0x3FF) / 1023.0f, py = float(cy) + float((h >> 10) & 0x3FF) / 1023.0f,
                            pz = float(cz) + float((h >> 20) & 0x3FF) / 1023.0f;
                const float dx = px - x, dy = py - y, dz = pz - z;
                best = std::min(best, dx * dx + dy * dy + dz * dz);
            }
    return std::min(1.0f, std::sqrt(best));
}
std::vector<std::uint32_t> makeNoise() {
    constexpr int N = kNoiseSize;
    std::vector<float> v(std::size_t(N) * N * N);
    for (int z = 0; z < N; ++z)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = float(x) / N, w = float(y) / N, s = float(z) / N;
                const float p = perlin(u * 4, w * 4, s * 4, 4, 1) * 0.55f + perlin(u * 8, w * 8, s * 8, 8, 2) * 0.3f +
                                perlin(u * 16, w * 16, s * 16, 16, 3) * 0.15f;
                const float c = (1 - worley(u * 4, w * 4, s * 4, 4, 7)) * 0.6f + (1 - worley(u * 8, w * 8, s * 8, 8, 8)) * 0.28f +
                                (1 - worley(u * 16, w * 16, s * 16, 16, 9)) * 0.12f;
                v[(std::size_t(z) * N + std::size_t(y)) * N + std::size_t(x)] = (p * 0.5f + 0.5f) * 0.45f + c * 0.55f;
            }
    // Stretch to 0..1 (1 % / 99 % points).
    std::vector<float> sorted(v);
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 100), sorted.end());
    const float lo = sorted[sorted.size() / 100];
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() * 99 / 100), sorted.end());
    const float hi = sorted[sorted.size() * 99 / 100];
    for (float& f : v) f = std::clamp((f - lo) / std::max(1e-4f, hi - lo), 0.0f, 1.0f);
    auto at = [&](int x, int y, int z) { return v[(std::size_t(wrap(z, N)) * N + std::size_t(wrap(y, N))) * N + std::size_t(wrap(x, N))]; };
    // Gradient per texel, scaled so that ~2.5 x its RMS fills the -1..1 range.
    double sum = 0;
    for (int z = 0; z < N; z += 2)
        for (int y = 0; y < N; y += 2)
            for (int x = 0; x < N; x += 2) {
                const float gx = at(x + 1, y, z) - at(x - 1, y, z), gy = at(x, y + 1, z) - at(x, y - 1, z), gz = at(x, y, z + 1) - at(x, y, z - 1);
                sum += double(gx * gx + gy * gy + gz * gz);
            }
    const float rms = float(std::sqrt(sum / double((N / 2) * (N / 2) * (N / 2))));
    const float gs = 1.0f / std::max(1e-4f, 2.5f * rms);
    std::vector<std::uint32_t> out(std::size_t(N) * N * N);
    const auto b8 = [](float f) { return std::uint32_t(std::clamp(f, 0.0f, 1.0f) * 255 + 0.5f); };
    for (int z = 0; z < N; ++z)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float gx = (at(x + 1, y, z) - at(x - 1, y, z)) * gs, gy = (at(x, y + 1, z) - at(x, y - 1, z)) * gs,
                            gz = (at(x, y, z + 1) - at(x, y, z - 1)) * gs;
                out[(std::size_t(z) * N + std::size_t(y)) * N + std::size_t(x)] =
                    b8(at(x, y, z)) | (b8(gx * 0.5f + 0.5f) << 8) | (b8(gy * 0.5f + 0.5f) << 16) | (b8(gz * 0.5f + 0.5f) << 24);
            }
    return out;
}

}  // namespace

std::unique_ptr<CinematicFx> CinematicFx::create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth,
                                                 SDL_GPUSampleCount samples) {
    std::unique_ptr<CinematicFx> c(new CinematicFx(dev));
    if (!c->init(color, depth, samples)) return nullptr;
    return c;
}

CinematicFx::~CinematicFx() {
    if (texThread_.joinable()) texThread_.join();
    for (SDL_GPUGraphicsPipeline* p : {pipe_, halfPipe_, compPipe_})
        if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    for (SDL_GPUTexture* t : {tex_, halfColor_, halfDepth_})
        if (t) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUSampler* s : {samp_, linear_})
        if (s) SDL_ReleaseGPUSampler(dev_, s);
    if (vb_) SDL_ReleaseGPUBuffer(dev_, vb_);
    if (tb_) SDL_ReleaseGPUTransferBuffer(dev_, tb_);
}

namespace {
// The particles' pipeline for one target: premultiplied alpha, depth tested, not written.
SDL_GPUGraphicsPipeline* makeFxPipe(SDL_GPUDevice* dev, SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth,
                                    SDL_GPUSampleCount samples) {
    SDL_GPUShader* vs = createShader(dev, ShaderId::FxVs, 0, 1);
    SDL_GPUShader* fs = createShader(dev, ShaderId::FxFs, 1, 1);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(dev, vs);
        if (fs) SDL_ReleaseGPUShader(dev, fs);
        SDL_Log("3D cinematic effects: shaders failed");
        return nullptr;
    }
    SDL_GPUColorTargetDescription ctd{};
    ctd.format = color;
    ctd.blend_state.enable_blend = true;
    ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    SDL_GPUVertexBufferDescription vbd{0, kFloats * 4, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0};
    SDL_GPUVertexAttribute at[8];
    for (Uint32 i = 0; i < 8; ++i) at[i] = {i, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, i * 16};
    pi.vertex_input_state = {&vbd, 1, at, 8};
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
    pi.multisample_state.sample_count = samples;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = false;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = depth;
    pi.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    SDL_ReleaseGPUShader(dev, vs);
    SDL_ReleaseGPUShader(dev, fs);
    if (!p) SDL_Log("3D cinematic effects: pipeline failed: %s", SDL_GetError());
    return p;
}
}  // namespace

bool CinematicFx::init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples) {
    colorFmt_ = color;
    pipe_ = makeFxPipe(dev_, color, depth, samples);
    if (!pipe_) return false;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_3D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = kNoiseSize;
    ti.layer_count_or_depth = kNoiseSize;
    ti.num_levels = 1;
    tex_ = SDL_CreateGPUTexture(dev_, &ti);
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samp_ = SDL_CreateGPUSampler(dev_, &si);
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linear_ = SDL_CreateGPUSampler(dev_, &si);
    if (!tex_ || !samp_ || !linear_) {
        SDL_Log("3D cinematic effects: noise texture failed: %s", SDL_GetError());
        return false;
    }
    texThread_ = std::thread([this] {
        const Uint64 t0 = SDL_GetTicksNS();
        texCpu_ = makeNoise();
        SDL_Log("3D cinematic effects: noise ready (%.0f ms, worker thread)", double(SDL_GetTicksNS() - t0) / 1e6);
        texBuilt_.store(true, std::memory_order_release);
    });
    return true;
}

void CinematicFx::reset() {
    parts_.clear();
    burners_.clear();
    pending_.clear();
    glows_.clear();
    smokeAcc_.clear();
    lifeAcc_.clear();
    beaconLights_.clear();
    tracks_.clear();
    shells_.clear();
    seenBuildings_.clear();
    buildingsPrimed_ = false;
}

float CinematicFx::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

float CinematicFx::load() const {
    const float fill = float(parts_.size()) / float(kMaxParticles);
    return std::clamp((1.0f - fill) / 0.45f, 0.1f, 1.0f);
}

// Effects intensity (render/fx_budget.hpp): an event's particle as the budget wants it. Puffs (smoke, dust,
// fireball soot) are capped in time (the look up to the cap is the spawned one, then a fade: FxBudget::capAlpha)
// and, as the view gets busy, fewer, smaller, thinner and sandier (a haze rather than a brown wall); sparks and
// debris only get fewer. Flashes, the fireball's white-hot core, ground marks and lasting columns are left alone.
// Thinning hashes a counter: the RNG streams (and with them every calm scene) stay as they were.
bool CinematicFx::budgetSpawn(Part& p) {
    if (lasting_ || !budget_.on()) return true;
    const float level = budget_.level();
    const bool puff = p.mode == kPuff;
    const bool core = puff && p.er > 2.0f;  // the white-hot core of a blast
    if ((puff && !core) || p.mode == kSpark || p.mode == kDebris) {
        const float keep = budget_.countMul();
        if (keep < 0.999f && render::FxBudget::hash01(++thinN_ * 2654435761u) > keep) return false;
    }
    if (!puff) return true;
    const float cap = budget_.smokeCap();
    if (cap > 0 && p.life > cap) p.cut = cap;
    if (level > 0 && !core) {
        const float sm = budget_.sizeMul();
        p.size0 *= sm;
        p.size1 *= sm;
        p.a *= budget_.alphaMul();
        if (p.er <= 0) {  // cool smoke / dust: towards a pale sandy haze
            const float h = 0.5f * level;
            p.r0 += (0.66f - p.r0) * h, p.g0 += (0.58f - p.g0) * h, p.b0 += (0.47f - p.b0) * h;
            p.r1 += (0.70f - p.r1) * h, p.g1 += (0.63f - p.g1) * h, p.b1 += (0.52f - p.b1) * h;
            p.soot *= 1 - 0.7f * level;
        }
    }
    return true;
}

CinematicFx::Part& CinematicFx::add(const Part& in) {
    Part pb = in;
    if (!budgetSpawn(pb)) {
        scratch_ = pb;
        return scratch_;
    }
    const Part& p = pb;
    if (int(parts_.size()) >= kMaxParticles) {
        // Replace the particle nearest its end (a sample of them); ground marks last the longest anyway.
        std::size_t best = 0;
        float bestLeft = 1e9f;
        for (std::size_t i = std::size_t(frame_ % 7); i < parts_.size(); i += 7) {
            const float left = parts_[i].life - parts_[i].age;
            if (left < bestLeft) bestLeft = left, best = i;
        }
        parts_[best] = p;
        parts_[best].id = nextId_++;
        return parts_[best];
    }
    parts_.push_back(p);
    parts_.back().id = nextId_++;
    return parts_.back();
}

CinematicFx::Part CinematicFx::puff() {
    Part p;
    p.rot = rnd(0, 6.283f);
    p.vrot = rnd(-0.3f, 0.3f);
    p.seed = rnd();
    return p;
}

// A flat mark on the terrain: the corner heights of its (rotated) square.
void CinematicFx::groundMark(Part& p, const Terrain& terr) {
    const float h = 0.5f * p.size1, cs = std::cos(p.rot), sn = std::sin(p.rot);
    const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        const float wx = p.p.x + (cs * cx[i] - sn * cy[i]) * h, wz = p.p.z + (sn * cx[i] + cs * cy[i]) * h;
        p.ch[i] = terr.height(wx, wz) + 0.6f - p.p.y;
    }
}

int CinematicFx::emitGroundMark(const float* vals, const Terrain& terr) {
    constexpr int kMaxGrid = 8;
    const float x = vals[0], baseY = vals[1], z = vals[2], size = vals[3], rot = vals[7];
    const float h = 0.5f * size, cs = std::cos(rot), sn = std::sin(rot);
    // Grid cells about two terrain steps wide (the Reimagined mesh: 4 px), at most kMaxGrid a side.
    const float cell = std::max(8.0f, 2.0f * terr.step());
    // D2K_MARK_GRID=0: one quad per mark as before (a comparison switch for the slope fix).
    static const bool grid = !std::getenv("D2K_MARK_GRID") || std::string(std::getenv("D2K_MARK_GRID")) != "0";
    int n = grid ? std::clamp(int(std::ceil(size / cell)), 1, kMaxGrid) : 1;
    float g[kMaxGrid + 1][kMaxGrid + 1], fade[kMaxGrid + 1][kMaxGrid + 1];
    const float refY = terr.height(x, z);
    auto sample = [&](int nn) {
        for (int j = 0; j <= nn; ++j)
            for (int i = 0; i <= nn; ++i) {
                const float lx = -1 + 2 * float(i) / float(nn), ly = -1 + 2 * float(j) / float(nn);
                const float wx = x + (cs * lx - sn * ly) * h, wz = z + (sn * lx + cs * ly) * h;
                g[j][i] = terr.height(wx, wz) + 0.6f - baseY;
                fade[j][i] = grid ? terr.markFade(wx, wz, refY) : 1.0f;
            }
    };
    sample(n);
    if (n > 1) {
        // Flat enough for the one quad of its corners (within half a pixel everywhere)?
        float dev = 0;
        const float c00 = g[0][0], c10 = g[0][n], c11 = g[n][n], c01 = g[n][0];
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                const float u = float(i) / float(n), v = float(j) / float(n);
                const float p = (c00 * (1 - u) + c10 * u) * (1 - v) + (c01 * (1 - u) + c11 * u) * v;
                dev = std::max(dev, std::abs(g[j][i] - p));
            }
        // D2K_FX_MARK_LOG=1: how far the ground strays from the mark's corner plane (what one quad would show).
        static const bool markLog = std::getenv("D2K_FX_MARK_LOG") != nullptr;
        if (markLog && dev >= 2.0f)
            SDL_Log("fx ground mark at (%.0f, %.0f) size %.0f: ground off its corner plane by up to %.1f px -> %d x %d grid", x, z,
                    size, dev, n, n);
        bool faded = false;  // a fade anywhere inside needs the grid too
        for (int j = 0; j <= n && !faded; ++j)
            for (int i = 0; i <= n; ++i) faded = faded || fade[j][i] < 0.99f;
        if (dev < 0.5f && !faded) {
            const float c[4] = {c00, c10, c11, c01};
            n = 1;
            g[0][0] = c[0], g[0][1] = c[1], g[1][1] = c[2], g[1][0] = c[3];
            fade[0][0] = fade[0][1] = fade[1][1] = fade[1][0] = 1.0f;
        }
    }
    // A corner = its height to 1/4 px (floor(v) / 4) + its fade (fract(v) / 0.99): fx_vs unpacks both.
    auto pack = [](float hh, float a) { return std::round(hh * 4) + std::clamp(a, 0.0f, 1.0f) * 0.99f; };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const std::size_t at = inst_.size();
            inst_.insert(inst_.end(), vals, vals + kFloats);
            float* o = &inst_[at];
            o[17] = n > 1 ? float(n * 256 + j * 16 + i) : 0.0f;
            o[28] = pack(g[j][i], fade[j][i]), o[29] = pack(g[j][i + 1], fade[j][i + 1]);
            o[30] = pack(g[j + 1][i + 1], fade[j + 1][i + 1]), o[31] = pack(g[j + 1][i], fade[j + 1][i]);
        }
    return n * n;
}

// Smoke colours (docs/3d-mode.md "Lit smoke"; albedo before the plume lighting of the shader). Soot is never black:
// fresh from a fire it is a dark brown, and it ages to a warm grey as it thins out and takes up air.
void CinematicFx::sootColour(Part& p, float darkness) {
    const float d = std::clamp(darkness, 0.0f, 1.0f);
    p.r0 = 0.33f - 0.15f * d, p.g0 = 0.3f - 0.16f * d, p.b0 = 0.275f - 0.17f * d;   // 1: (0.18, 0.14, 0.105)
    p.r1 = 0.62f - 0.08f * d, p.g1 = 0.6f - 0.1f * d, p.b1 = 0.57f - 0.12f * d;     // 1: (0.54, 0.5, 0.45)
}

// The dust the ground raises there: dustTint with a world, else the sand / rock pair.
CinematicFx::DustTint CinematicFx::groundDust(float x, float y, bool sand) const {
    if (world_) return dustTint(*world_, x, y);
    return sand ? DustTint{0.95f, 0.84f, 0.65f} : DustTint{0.74f, 0.68f, 0.6f};
}

// Smoke born at the ground drags its dust along: the puff takes the ground's colour by `share` while it is low
// (prepare(): fading out by ~60 px up), so a column's foot is brown-grey dust and its top smoke.
void CinematicFx::mixGround(Part& p, float share) const {
    const DustTint t = groundDust(p.p.x, p.p.z, true);
    p.gmix = share;
    p.gr = t.r * 0.8f, p.gg = t.g * 0.8f, p.gb = t.b * 0.8f;
}

// Column smoke: dense and dark at the base, thinning, paling and spreading as it rises, leaning with the wind;
// k = size (1: a burning wreck's column), darkness 0..1, lifeMul stretches how long it lingers.
CinematicFx::Part& CinematicFx::smokePuff(const Terrain& terr, float x, float y, float z, float k, float darkness, float lifeMul,
                                          Smoke kind) {
    Part s = puff();
    s.p = {x + rnd(-4, 4) * k, terr.height(x, y) + z, y + rnd(-4, 4) * k};
    s.v = {rnd(-5, 5), rnd(16, 28) * (0.7f + 0.3f * k), rnd(-5, 5)};
    s.life = rnd(9, 13) * (0.8f + 0.2f * k) * lifeMul;
    s.size0 = rnd(14, 20) * k;
    s.size1 = rnd(72, 105) * k;
    s.vrot = rnd(-0.15f, 0.15f);
    s.drag = 0.35f;
    s.wind = 1;
    sootColour(s, darkness);
    s.a = 0.6f;
    s.fadeIn = 0.04f;
    s.fadeOut = 0.62f;
    s.erode0 = 0.16f;
    s.erode1 = 0.72f;
    s.scale = 1.05f;
    s.soft = 0.25f;
    s.soot = darkness;
    switch (kind) {
    case kSoot: mixGround(s, 0.3f); break;
    case kBuildingFire:  // a dense brown-grey column
        s.r0 = 0.27f, s.g0 = 0.205f, s.b0 = 0.15f;
        s.r1 = 0.5f, s.g1 = 0.45f, s.b1 = 0.39f;
        s.a = 0.7f;
        s.soot = 0.8f;
        break;
    case kWreckSmoke: s.a = 0.5f; break;
    case kStackWisp:  // building life: a thin light-grey wisp off a working stack, leaning downwind
        s.p = {x + rnd(-0.8f, 0.8f), terr.height(x, y) + z, y + rnd(-0.8f, 0.8f)};
        s.v = {rnd(-1.5f, 1.5f), rnd(9, 14), rnd(-1.5f, 1.5f)};
        s.life = rnd(4.0f, 5.5f) * lifeMul;
        s.size0 = rnd(3.0f, 4.0f) * k;
        s.size1 = rnd(36, 48) * k;
        s.drag = 0.45f;
        s.r0 = 0.5f, s.g0 = 0.49f, s.b0 = 0.47f;
        s.r1 = 0.74f, s.g1 = 0.72f, s.b1 = 0.69f;
        s.a = 0.55f;
        s.fadeIn = 0.06f;
        s.fadeOut = 0.7f;
        s.erode0 = 0.26f;
        s.erode1 = 0.82f;
        s.scale = 1.35f;
        s.soft = 0.1f;
        s.soot = 0.2f;
        break;
    case kSteam:  // building life: a power plant's faint white steam, gone within a couple of seconds
        s.p = {x + rnd(-0.6f, 0.6f), terr.height(x, y) + z, y + rnd(-0.6f, 0.6f)};
        s.v = {rnd(-1.5f, 1.5f), rnd(12, 18), rnd(-1.5f, 1.5f)};
        s.life = rnd(1.8f, 2.6f) * lifeMul;
        s.size0 = rnd(3.0f, 4.0f) * k;
        s.size1 = rnd(34, 44) * k;
        s.drag = 0.6f;
        s.r0 = 0.86f, s.g0 = 0.87f, s.b0 = 0.88f;
        s.r1 = 0.93f, s.g1 = 0.93f, s.b1 = 0.93f;
        s.a = 0.5f;
        s.fadeIn = 0.08f;
        s.fadeOut = 0.75f;
        s.erode0 = 0.3f;
        s.erode1 = 0.86f;
        s.scale = 1.45f;
        s.soft = 0.1f;
        s.soot = 0;
        break;
    case kVentHeat:  // building life: a vent's warm air - barely there, a faint shimmer of pale haze
        s.p = {x + rnd(-1.5f, 1.5f) * k, terr.height(x, y) + z, y + rnd(-1.5f, 1.5f) * k};
        s.v = {rnd(-1, 1), rnd(7, 11), rnd(-1, 1)};
        s.life = rnd(1.0f, 1.5f) * lifeMul;
        s.size0 = rnd(4, 5) * k;
        s.size1 = rnd(11, 15) * k;
        s.drag = 0.8f;
        s.wind = 0.6f;
        s.r0 = s.r1 = 0.9f, s.g0 = s.g1 = 0.86f, s.b0 = s.b1 = 0.8f;
        s.a = 0.07f;
        s.fadeIn = 0.2f;
        s.fadeOut = 0.6f;
        s.erode0 = 0.35f;
        s.erode1 = 0.85f;
        s.scale = 1.6f;
        s.soft = 0.1f;
        s.soot = 0;
        break;
    case kDamageSmoke:  // thin and grey: a wisp off a damaged hull, gone in a couple of seconds
        s.v = {rnd(-3, 3), rnd(14, 24), rnd(-3, 3)};
        s.life = rnd(1.6f, 2.4f) * lifeMul;
        s.size0 = rnd(5, 7) * k;
        s.size1 = rnd(22, 30) * k;
        s.drag = 0.4f;
        s.a = 0.34f;
        s.erode0 = 0.24f;
        s.erode1 = 0.76f;
        s.scale = 1.3f;
        s.soft = 0.15f;
        break;
    }
    return add(s);
}

void CinematicFx::dustPuff(const Terrain& terr, float x, float y, float k, float r, float g, float b, float alpha) {
    Part d = puff();
    d.p = {x, terr.height(x, y) + 2, y};
    d.v = {rnd(-5, 5), rnd(3, 9), rnd(-5, 5)};
    d.life = rnd(1.8f, 3.0f);
    d.size0 = rnd(8, 12) * k;
    d.size1 = rnd(30, 44) * k;
    d.vrot = rnd(-0.4f, 0.4f);
    d.drag = 1.2f;
    d.wind = 0.6f;
    d.r0 = d.r1 = r;
    d.g0 = d.g1 = g;
    d.b0 = d.b1 = b;
    d.a = alpha;
    d.fadeIn = 0.12f;
    d.fadeOut = 0.45f;
    d.erode0 = 0.25f;
    d.erode1 = 0.7f;
    d.scale = 1.35f;
    add(d);
}

// A smoke puff left behind by a flying chunk / rocket.
void CinematicFx::trailPuff(const Vec3& p, float k, bool rocket) {
    Part s = puff();
    s.p = p + Vec3{rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)};
    s.v = {rnd(-3, 3), rnd(2, 8), rnd(-3, 3)};
    s.life = rocket ? rnd(1.4f, 2.2f) : rnd(1.0f, 1.8f);
    s.size0 = rocket ? 3.5f : 2.0f * k;
    s.size1 = rocket ? rnd(14, 20) : rnd(7, 11) * k;
    s.drag = 1.5f;
    s.buoy = 4;
    s.wind = 0.7f;
    const float g = rocket ? 0.62f : 0.34f;
    s.r0 = g * 1.03f, s.g0 = g, s.b0 = g * 0.95f;
    s.r1 = rocket ? 0.55f : 0.46f, s.g1 = rocket ? 0.53f : 0.44f, s.b1 = rocket ? 0.5f : 0.41f;
    s.a = rocket ? 0.42f : 0.28f;
    s.fadeIn = 0.03f;
    s.fadeOut = 0.6f;
    s.erode0 = 0.2f;
    s.erode1 = 0.7f;
    s.scale = 1.2f;
    s.soft = 0.15f;
    if (rocket) {  // glowing for an instant right behind the motor
        s.er = s.eg = s.eb = 0.9f;
        s.heatLife = 0.09f;
    }
    add(s);
}

// The fireball radius (world px) of an explosion of size k (Effects::FxEvent::scale): ~0.5 small bursts of a
// building's death chain, 1 a tank shell / rocket (a fireball well larger than the tank: ~50 px across, a tank is
// ~32), 2 a vehicle's death / a howitzer shell, 2.7 a building, 5 the Devastator, 7.2 the Death Hand.
float CinematicFx::fireballRadius(float k) { return 24.0f * std::pow(std::clamp(k, 0.25f, 8.0f), 0.78f); }

// A burnt crater mark; a fresh one close to an existing one (a bombarded spot) renews and widens that one
// instead of stacking another (overlapping marks cost pixels and read as one dark blot anyway).
void CinematicFx::scorchMark(const Terrain& terr, float x, float y, float size, float k, bool sand, float embers) {
    const float hot = embers >= 0 ? std::max(0.05f, embers) : 2.0f + 1.5f * std::sqrt(k);
    const float gy = terr.height(x, y);
    for (Part& o : parts_) {
        if (o.mode != kScorch) continue;
        const float dx = o.p.x - x, dz = o.p.z - y;
        const float near = 0.3f * std::max(o.size1, size);
        if (dx * dx + dz * dz > near * near) continue;
        o.age = std::min(o.age, 1.0f);
        if (size > o.size1) {
            o.size0 = o.size1 = std::min(size * 1.1f, o.size1 * 1.25f);
            groundMark(o, terr);
        }
        o.heatLife = std::max(o.heatLife, o.age + hot);
        return;
    }
    Part s;
    s.mode = kScorch;
    s.p = {x, gy, y};
    s.rot = rnd(0, 6.283f);
    s.life = rnd(50, 70);
    s.size0 = s.size1 = size;
    s.r0 = s.r1 = 0.05f, s.g0 = s.g1 = 0.042f, s.b0 = s.b1 = 0.036f;
    // The ejecta rim: the ground's own dust colour (carried in v: ground marks never move).
    s.v = sand ? Vec3{0.78f, 0.66f, 0.5f} : Vec3{0.58f, 0.54f, 0.48f};
    s.a = 0.9f;
    s.fadeIn = 0.003f;
    s.fadeOut = 0.45f;
    s.heatLife = hot;
    s.seed = rnd();
    groundMark(s, terr);
    add(s);
}

// k: Effects::FxEvent::scale (see fireballRadius). Fewer, bigger puffs than round 2: a rolling fireball well
// larger than the vehicles, cooling into dense black soot that rises into a lingering column.
void CinematicFx::explosion(const Terrain& terr, float x, float y, float z, float k, bool sand, bool ground, bool scorch) {
    const float ld = load();
    const float R = fireballRadius(k);
    const float sk = std::sqrt(R / 24.0f), rk = std::sqrt(std::max(0.25f, k));
    const float gy = terr.height(x, y);
    const Vec3 c{x, gy + z + 0.25f * R, y};
    // A hulk's blast (vehicle wrecks): the fireball's soot thins out sooner - the hulk is to be seen in it.
    const float sootLife = hulkBlast_ ? 0.6f : 1.0f;
    // Flash: an instant of white-hot light (a soft glow, drawn at half resolution).
    {
        Part f;
        f.mode = kFlash;
        f.p = c + Vec3{0, 0.3f * R, 0};
        f.life = 0.08f + 0.035f * rk;
        f.size0 = 1.7f * R;
        f.size1 = 2.7f * R;
        f.er = 7, f.eg = 4.8f, f.eb = 2.6f;
        f.fadeIn = 0;
        f.fadeOut = 0.7f;
        f.seed = rnd();
        add(f);
    }
    glows_.push_back({x, y, z + 0.7f * R, 24 + 2.4f * R, 1.5f, 0.7f, 0.26f, 0, 0.45f + 0.12f * k});
    // The white-hot core: one or two big puffs that burn out fast.
    const int nCore = k >= 1.6f ? 2 : 1;
    for (int i = 0; i < nCore; ++i) {
        Part p = puff();
        p.p = c + Vec3{rnd(-0.1f, 0.1f) * R, rnd(0.1f, 0.3f) * R, rnd(-0.1f, 0.1f) * R};
        p.v = {rnd(-0.3f, 0.3f) * R, rnd(0.6f, 1.1f) * R * sk, rnd(-0.3f, 0.3f) * R};
        p.life = rnd(1.4f, 1.8f) * (0.85f + 0.15f * rk) * sootLife;
        p.size0 = rnd(0.75f, 0.9f) * R;
        p.size1 = rnd(1.5f, 1.8f) * R;
        p.vrot = rnd(-1.0f, 1.0f);
        p.drag = 3;
        p.buoy = 20 * sk;
        p.wind = 0.3f;
        sootColour(p, 1);
        p.a = 1;
        p.fadeIn = 0.0f;
        p.fadeOut = 0.5f;
        p.er = p.eg = p.eb = 2.3f;
        p.heatLife = rnd(0.6f, 0.8f) * (0.85f + 0.15f * rk);
        p.erode0 = 0.0f;
        p.erode1 = 0.5f;
        p.scale = 0.75f;
        p.soft = 0.12f;
        p.soot = 1;
        add(p);
    }
    // Fireball: overlapping hot puffs thrown out and up, rolling, rising, cooling into black soot.
    const int nFire = std::max(2, int((4 + 3 * rk) * ld));
    for (int i = 0; i < nFire; ++i) {
        Part p = puff();
        const float a = rnd(0, 6.283f), el = rnd(0.1f, 1.0f);
        const Vec3 d{std::cos(a) * std::cos(el), std::sin(el), std::sin(a) * std::cos(el)};
        p.p = c + d * (rnd(0.15f, 0.45f) * R);
        p.v = d * (rnd(1.8f, 3.2f) * R) + Vec3{0, rnd(0.3f, 0.9f) * R, 0};
        p.age = -rnd(0.0f, 0.08f) * sk;  // staggered: the blast keeps blooming for a moment
        p.life = rnd(2.2f, 3.2f) * (0.85f + 0.15f * rk) * sootLife;
        p.size0 = rnd(0.7f, 0.85f) * R;
        p.size1 = rnd(1.4f, 1.8f) * R;
        p.vrot = rnd(-1.0f, 1.0f);
        p.drag = 3.2f;
        p.buoy = 24 * sk;
        p.wind = 0.4f;
        sootColour(p, 1);
        if (ground && !onVehicle_) mixGround(p, 0.25f);
        p.a = 0.85f;
        p.fadeIn = 0.02f;
        p.fadeOut = 0.6f;
        const float hot = rnd(1.3f, 1.9f);
        p.er = p.eg = p.eb = hot;
        p.heatLife = std::min(1.9f, rnd(0.8f, 1.2f) * (0.8f + 0.2f * rk));
        p.erode0 = 0.02f;
        p.erode1 = 0.64f;
        p.scale = 1.05f;
        p.soft = 0.15f;
        p.soot = 1;
        add(p);
    }
    // Sparks: hot streaks with gravity; a third land and glow on the ground for a few seconds.
    const int nSpark = int((8 + 8 * rk) * ld);
    for (int i = 0; i < nSpark; ++i) {
        Part s;
        s.mode = kSpark;
        const float a = rnd(0, 6.283f), el = rnd(0.15f, 1.35f), sp = rnd(110, 300) * sk;
        s.p = c + Vec3{0, rnd(0, 0.2f) * R, 0};
        s.v = {std::cos(a) * std::cos(el) * sp, std::sin(el) * sp, std::sin(a) * std::cos(el) * sp};
        s.ember = rnd() < 0.33f;
        s.life = s.ember ? rnd(2.0f, 4.0f) : rnd(0.35f, 0.9f);
        s.heatLife = s.ember ? s.life : 0;
        s.size0 = s.size1 = rnd(1.3f, 2.2f);
        s.gravity = 320;
        s.drag = 1.2f;
        s.stretch = 0.045f;
        const float h = rnd(0.8f, 1.25f);
        s.er = 14 * h, s.eg = 7.5f * h, s.eb = 2.6f * h;
        s.fadeIn = 0;
        s.fadeOut = s.ember ? 0.35f : 0.5f;
        s.seed = rnd();
        add(s);
    }
    // Debris: dark chunks thrown up, falling, bouncing once; big blasts' chunks trail smoke.
    const int nDeb = int((3 + 3 * rk) * ld);
    for (int i = 0; i < nDeb; ++i) {
        Part d;
        d.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(40, 130) * sk;
        d.p = c;
        d.v = {std::cos(a) * sp, rnd(80, 180) * sk, std::sin(a) * sp};
        d.life = rnd(1.8f, 2.8f);
        d.size0 = d.size1 = rnd(2.4f, 4.2f) * std::sqrt(sk);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-9, 9);
        d.gravity = 320;
        d.drag = 0.3f;
        const float g = rnd(0.16f, 0.3f);
        d.r0 = d.r1 = g * 1.08f, d.g0 = d.g1 = g, d.b0 = d.b1 = g * 0.88f;
        d.fadeIn = 0;
        d.fadeOut = 0.25f;
        d.er = 3.5f, d.eg = 1.3f, d.eb = 0.3f;
        d.heatLife = 0.7f;
        d.seed = rnd();
        d.scale = 1;
        if (k >= 1.6f && rnd() < 0.35f) d.trail = 3.0f;
        add(d);
    }
    const DustTint gd = groundDust(x, y, sand);  // dust from the ground it hits, in the ground's colour
    const float dr = gd.r * 0.92f, dg = gd.g * 0.92f, db = gd.b * 0.92f;
    if (ground) {
        // Scorch / crater: a burnt mark with an ejecta rim that stays about a minute.
        if (scorch && k >= 0.8f) scorchMark(terr, x, y, rnd(2.3f, 2.8f) * R, k, sand);
        // Shock ring (big hits): dust racing out along the ground.
        if (k >= 1.4f) {
            Part r;
            r.mode = kRing;
            r.p = {x, gy, y};
            r.rot = rnd(0, 6.283f);
            r.life = 0.55f + 0.1f * k;
            r.size0 = 0.5f * R;
            r.size1 = 5.0f * R;
            r.r0 = r.r1 = dr * 1.05f, r.g0 = r.g1 = dg * 1.05f, r.b0 = r.b1 = db * 1.05f;
            r.a = 0.65f;
            r.fadeIn = 0.02f;
            r.fadeOut = 0.75f;
            r.seed = rnd();
            groundMark(r, terr);
            add(r);
        }
        // Rolling dust hugging the ground.
        const int nRing = int((5 + 3 * rk) * ld);
        for (int i = 0; i < nRing; ++i) {
            Part d = puff();
            const float a = 6.283f * (float(i) + rnd(0, 0.6f)) / float(std::max(1, nRing)), sp = rnd(3.0f, 5.0f) * R;
            d.p = {x + std::cos(a) * 0.3f * R, gy + 3, y + std::sin(a) * 0.3f * R};
            d.v = {std::cos(a) * sp, rnd(2, 9), std::sin(a) * sp};
            d.life = rnd(2.5f, 4.0f) * (0.85f + 0.15f * rk);
            d.size0 = rnd(0.4f, 0.55f) * R;
            d.size1 = rnd(1.4f, 1.9f) * R;
            d.vrot = rnd(-0.5f, 0.5f);
            d.drag = 2.6f;
            d.wind = 0.5f;
            d.r0 = d.r1 = dr, d.g0 = d.g1 = dg, d.b0 = d.b1 = db;
            d.a = 0.55f;
            d.fadeIn = 0.04f;
            d.fadeOut = 0.55f;
            d.erode0 = 0.25f;
            d.erode1 = 0.72f;
            d.scale = 1.25f;
            d.soft = 0.2f;
            add(d);
        }
        // Sand thrown up: pale billows above the blast.
        if (sand) {
            const int nUp = int((2 + rk) * ld);
            for (int i = 0; i < nUp; ++i) {
                Part d = puff();
                const float a = rnd(0, 6.283f), sp = rnd(0.6f, 1.6f) * R;
                d.p = {x + rnd(-0.2f, 0.2f) * R, gy + 4, y + rnd(-0.2f, 0.2f) * R};
                d.v = {std::cos(a) * sp, rnd(2.0f, 3.6f) * R, std::sin(a) * sp};
                d.life = rnd(3.0f, 5.0f);
                d.size0 = rnd(0.5f, 0.65f) * R;
                d.size1 = rnd(1.6f, 2.1f) * R;
                d.drag = 1.8f;
                d.gravity = 18;
                d.wind = 0.8f;
                d.r0 = d.r1 = dr * 1.04f, d.g0 = d.g1 = dg * 1.04f, d.b0 = d.b1 = db * 1.04f;
                d.a = 0.5f;
                d.fadeIn = 0.04f;
                d.fadeOut = 0.6f;
                d.erode0 = 0.22f;
                d.erode1 = 0.7f;
                d.scale = 1.2f;
                add(d);
            }
        }
    }
    // A dark smoke column that lingers (longer and bigger after big blasts), rising out of the cooling fireball.
    // On a vehicle (a shell on a tank) it starts above the hull and downwind of it, a little greyer: seen from
    // above, a column standing on the hull covered it for ~10 s with a dark blob.
    const int nSmoke = hulkBlast_ ? 0 : int((2 + 1.5f * rk) * ld);
    const float wl = std::max(1e-3f, std::hypot(wind_.x, wind_.z));
    for (int i = 0; i < nSmoke; ++i) {
        float sx = x + rnd(-0.3f, 0.3f) * R, sy = y + rnd(-0.3f, 0.3f) * R, sz = z + 0.4f * R, dark = 1.0f;
        if (onVehicle_) {
            const float off = rnd(0.6f, 0.9f) * R;
            sx = x + wind_.x / wl * off + rnd(-0.15f, 0.15f) * R;
            sy = y + wind_.z / wl * off + rnd(-0.15f, 0.15f) * R;
            sz = z + 18;
            dark = 0.7f;
        }
        smokePuff(terr, sx, sy, sz, R / 34.0f, dark, 0.75f + 0.2f * rk).age = -rnd(0.4f, 1.2f);
    }
}

// A burning wreck / ruin's smoke column; one already burning right there (a spot hit again and again) is
// stoked instead of a second column starting on top of it.
void CinematicFx::addBurner(float x, float y, float z, float k, float life) {
    const float near = budget_.mergeRadius();  // ~1.5 tiles (Cinematic: 20 px, as before)
    for (Burner& b : burners_)
        if (std::abs(b.x - x) < near && std::abs(b.y - y) < near && (near <= 20 || std::hypot(b.x - x, b.y - y) < near)) {
            b.k = std::max(b.k, k);
            b.life = std::max(b.life, b.age + life * 0.6f);
            return;
        }
    if (burners_.size() < 64) burners_.push_back({x, y, z, k, 0, life, 0});
}

void CinematicFx::impact(const Terrain& terr, float x, float y, float z, float s, bool sand, bool armour) {
    const float ld = load();
    const float gy = terr.height(x, y);
    auto sparks = [&](int n, float h0, float speed) {
        for (int i = 0; i < n; ++i) {
            Part p;
            p.mode = kSpark;
            const float a = rnd(0, 6.283f), el = rnd(0.2f, 1.25f), sp = rnd(0.6f, 1.0f) * speed;
            p.p = {x, gy + z + h0, y};
            p.v = {std::cos(a) * std::cos(el) * sp, std::sin(el) * sp, std::sin(a) * std::cos(el) * sp};
            p.life = rnd(0.2f, 0.5f);
            p.size0 = p.size1 = rnd(1.1f, 1.6f);
            p.gravity = 320;
            p.drag = 1.5f;
            p.stretch = 0.045f;
            p.er = 15, p.eg = 9, p.eb = 3.5f;
            p.fadeIn = 0;
            p.seed = rnd();
            add(p);
        }
    };
    if (armour) {
        sparks(int(7 * ld) + 2, 6, 200);
        Part f;
        f.mode = kFlash;
        f.p = {x, gy + z + 7, y};
        f.life = 0.07f;
        f.size0 = 14 * s;
        f.size1 = 22 * s;
        f.er = 12, f.eg = 8, f.eb = 4;
        f.fadeIn = 0;
        f.fadeOut = 0.7f;
        f.seed = rnd();
        add(f);
        return;
    }
    if (!sand) sparks(int(3 * ld) + 1, 2, 150);
    // A burst of dust (sand) or grit (rock) thrown up in a cone, and a few grains.
    const DustTint gd = groundDust(x, y, sand);
    const float dr = gd.r * 0.92f, dg = gd.g * 0.92f, db = gd.b * 0.92f;
    const int n = int((3 + 2 * s) * ld) + 1;
    for (int i = 0; i < n; ++i) {
        Part d = puff();
        const float a = rnd(0, 6.283f), sp = rnd(10, 32);
        d.p = {x + rnd(-2, 2), gy + 2, y + rnd(-2, 2)};
        d.v = {std::cos(a) * sp, rnd(35, 70), std::sin(a) * sp};
        d.life = rnd(1.4f, 2.3f);
        d.size0 = rnd(5, 8) * s;
        d.size1 = rnd(20, 30) * s;
        d.vrot = rnd(-0.6f, 0.6f);
        d.drag = 2.2f;
        d.gravity = 25;
        d.wind = 0.5f;
        d.r0 = d.r1 = dr, d.g0 = d.g1 = dg, d.b0 = d.b1 = db;
        d.a = 0.62f;
        d.fadeIn = 0.03f;
        d.fadeOut = 0.5f;
        d.erode0 = 0.2f;
        d.erode1 = 0.7f;
        d.scale = 1.35f;
        d.soft = 0.2f;
        add(d);
    }
    for (int i = 0; i < int(3 * ld) + 1; ++i) {
        Part d;
        d.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(20, 60);
        d.p = {x, gy + 2, y};
        d.v = {std::cos(a) * sp, rnd(60, 120), std::sin(a) * sp};
        d.life = rnd(0.8f, 1.3f);
        d.size0 = d.size1 = rnd(1.2f, 2.0f);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-9, 9);
        d.gravity = 320;
        d.r0 = d.r1 = dr * 0.6f, d.g0 = d.g1 = dg * 0.6f, d.b0 = d.b1 = db * 0.6f;
        d.fadeIn = 0;
        d.fadeOut = 0.3f;
        d.seed = rnd();
        add(d);
    }
}

// An artillery impact (Effects::FxEvent::Artillery: the siege tank's howitzer k = 1, a grenade ~0.6): no
// fireball. On the ground a fountain of sand and earth thrown up steeply (~100 px for the howitzer) that slows,
// spreads and settles, a spray of grit and clods of earth falling back, a white flash for a few frames, rolling
// dust and a shock ring along the ground, a brown-grey dust / smoke cloud that drifts off with the wind and the
// crater. On armour: a flash, sparks and a small burst of dark smoke.
void CinematicFx::artillery(const Terrain& terr, float x, float y, float z, float k, bool sand, bool armour) {
    const float ld = load();
    const float R = fireballRadius(k);  // the size unit: 24 px for the howitzer, ~16 for a grenade
    const float sk = std::sqrt(k);
    const float gy = terr.height(x, y);
    const Vec3 c{x, gy + z, y};
    {  // the flash (half resolution) and its light
        Part f;
        f.mode = kFlash;
        f.p = c + Vec3{0, 0.35f * R, 0};
        f.life = 0.06f + 0.03f * sk;
        f.size0 = 1.5f * R;
        f.size1 = 2.4f * R;
        f.er = 9, f.eg = 6.5f, f.eb = 3.6f;
        f.fadeIn = 0;
        f.fadeOut = 0.7f;
        f.seed = rnd();
        add(f);
        glows_.push_back({x, y, z + 0.5f * R, 20 + 2.0f * R, 1.5f, 0.95f, 0.55f, 0, 0.2f + 0.05f * k});
    }
    if (armour) {
        impact(terr, x, y, z, 1.4f + 0.6f * k, sand, true);
        const int n = int((6 + 6 * k) * ld) + 1;
        for (int i = 0; i < n; ++i) {
            Part p;
            p.mode = kSpark;
            const float a = rnd(0, 6.283f), el = rnd(0.2f, 1.3f), sp = rnd(120, 260) * sk;
            p.p = c + Vec3{0, 7, 0};
            p.v = {std::cos(a) * std::cos(el) * sp, std::sin(el) * sp, std::sin(a) * std::cos(el) * sp};
            p.ember = rnd() < 0.2f;
            p.life = p.ember ? rnd(1.2f, 2.4f) : rnd(0.25f, 0.6f);
            p.heatLife = p.ember ? p.life : 0;
            p.size0 = p.size1 = rnd(1.2f, 1.9f);
            p.gravity = 320;
            p.drag = 1.3f;
            p.stretch = 0.045f;
            p.er = 15, p.eg = 8.5f, p.eb = 3;
            p.fadeIn = 0;
            p.fadeOut = 0.5f;
            p.seed = rnd();
            add(p);
        }
        const int nSmoke = int((3 + 2 * k) * ld) + 1;  // a small burst of dark smoke
        for (int i = 0; i < nSmoke; ++i) {
            Part s = puff();
            const float a = rnd(0, 6.283f);
            s.p = c + Vec3{std::cos(a) * 0.2f * R, 6 + rnd(0, 0.3f) * R, std::sin(a) * 0.2f * R};
            s.v = {std::cos(a) * rnd(0.4f, 1.0f) * R, rnd(0.8f, 1.5f) * R, std::sin(a) * rnd(0.4f, 1.0f) * R};
            s.age = -rnd(0, 0.06f);
            s.life = rnd(1.6f, 2.6f);
            s.size0 = rnd(0.35f, 0.5f) * R;
            s.size1 = rnd(1.2f, 1.6f) * R;
            s.drag = 2.0f;
            s.buoy = 10;
            s.wind = 0.6f;
            sootColour(s, 0.8f);
            s.a = 0.55f;
            s.fadeIn = 0.03f;
            s.fadeOut = 0.6f;
            s.erode0 = 0.15f;
            s.erode1 = 0.68f;
            s.scale = 1.1f;
            s.soft = 0.2f;
            s.soot = 0.8f;
            add(s);
        }
        return;
    }
    // Sand / soil colours: the dust as the other effects use it, and darker earth from below the surface.
    const DustTint gd = groundDust(x, y, sand);
    const float dr = gd.r * 0.92f, dg = gd.g * 0.92f, db = gd.b * 0.92f;
    const float er = dr * 0.42f, eg = dg * 0.37f, eb = db * 0.33f;
    // 1. The fountain: puffs thrown up steeply, the later ones faster (the column grows upwards); drag and
    // gravity stop them at ~30..110 px, then they spread and sink as they thin out.
    const int nCol = int((9 + 6 * k) * ld) + 3;
    for (int i = 0; i < nCol; ++i) {
        Part d = puff();
        const float u = float(i) / float(std::max(1, nCol - 1));
        d.p = {x + rnd(-0.2f, 0.2f) * R, gy + 3 + rnd(0, 0.2f) * R, y + rnd(-0.2f, 0.2f) * R};
        const float V = (90 + 170 * u) * sk * rnd(0.9f, 1.08f);
        const float side = (1.3f - 0.7f * u) * R;  // the low puffs spread wide: a cone, not a pipe
        d.v = {rnd(-1, 1) * side, V, rnd(-1, 1) * side};
        d.age = -0.012f * float(i % 4);
        d.life = rnd(1.8f, 2.6f) * (0.8f + 0.2f * sk);
        d.size0 = rnd(0.45f, 0.6f) * R;
        d.size1 = rnd(1.6f, 2.1f) * R * (0.85f + 0.3f * u);
        d.vrot = rnd(-0.6f, 0.6f);
        d.drag = 1.0f;
        d.gravity = 140;
        d.wind = 0.3f;
        const float m = rnd(0.35f, 0.9f);  // earth mixed in: darker in the core of the column
        d.r0 = dr + (er - dr) * m, d.g0 = dg + (eg - dg) * m, d.b0 = db + (eb - db) * m;
        d.r1 = dr * 0.9f, d.g1 = dg * 0.9f, d.b1 = db * 0.9f;
        d.a = 0.88f;
        d.fadeIn = 0.06f;
        d.fadeOut = 0.55f;
        d.erode0 = 0.1f;
        d.erode1 = 0.7f;
        d.scale = 1.3f;
        d.soft = 0.2f;
        add(d);
    }
    // 2. Grit: a spray of small dark grains up the column and out, falling back.
    const int nGrit = int((10 + 10 * k) * ld) + 2;
    for (int i = 0; i < nGrit; ++i) {
        Part g;
        g.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(10, 70) * sk;
        g.p = c + Vec3{0, 2, 0};
        g.v = {std::cos(a) * sp, rnd(130, 260) * sk, std::sin(a) * sp};
        g.life = rnd(0.9f, 1.5f);
        g.size0 = g.size1 = rnd(0.9f, 1.5f);
        g.rot = rnd(0, 6.283f);
        g.vrot = rnd(-9, 9);
        g.gravity = 320;
        g.drag = 0.5f;
        g.r0 = g.r1 = er * 0.8f, g.g0 = g.g1 = eg * 0.8f, g.b0 = g.b1 = eb * 0.8f;
        g.fadeIn = 0;
        g.fadeOut = 0.3f;
        g.seed = rnd();
        add(g);
    }
    // 3. Clods of earth thrown out, falling back round the hole (bouncing once).
    const int nClod = int((4 + 5 * k) * ld) + 2;
    for (int i = 0; i < nClod; ++i) {
        Part d;
        d.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(30, 95) * sk;
        d.p = c + Vec3{0, 2, 0};
        d.v = {std::cos(a) * sp, rnd(100, 200) * sk, std::sin(a) * sp};
        d.life = rnd(1.5f, 2.4f);
        d.size0 = d.size1 = rnd(2.0f, 3.6f) * (0.7f + 0.3f * sk);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-9, 9);
        d.gravity = 320;
        d.drag = 0.3f;
        const float sh = rnd(0.6f, 0.9f);
        d.r0 = d.r1 = er * sh, d.g0 = d.g1 = eg * sh, d.b0 = d.b1 = eb * sh;
        d.fadeIn = 0;
        d.fadeOut = 0.25f;
        d.seed = rnd();
        d.scale = 1;
        add(d);
    }
    // 4. Rolling dust hugging the ground and a shock ring racing out.
    const int nRing = int((5 + 3 * k) * ld) + 1;
    for (int i = 0; i < nRing; ++i) {
        Part d = puff();
        const float a = 6.283f * (float(i) + rnd(0, 0.6f)) / float(nRing), sp = rnd(2.6f, 4.2f) * R;
        d.p = {x + std::cos(a) * 0.3f * R, gy + 3, y + std::sin(a) * 0.3f * R};
        d.v = {std::cos(a) * sp, rnd(2, 8), std::sin(a) * sp};
        d.life = rnd(2.0f, 3.2f);
        d.size0 = rnd(0.4f, 0.55f) * R;
        d.size1 = rnd(1.3f, 1.7f) * R;
        d.vrot = rnd(-0.5f, 0.5f);
        d.drag = 2.6f;
        d.wind = 0.5f;
        d.r0 = d.r1 = dr, d.g0 = d.g1 = dg, d.b0 = d.b1 = db;
        d.a = 0.55f;
        d.fadeIn = 0.04f;
        d.fadeOut = 0.55f;
        d.erode0 = 0.25f;
        d.erode1 = 0.72f;
        d.scale = 1.25f;
        d.soft = 0.2f;
        add(d);
    }
    {
        Part r;
        r.mode = kRing;
        r.p = {x, gy, y};
        r.rot = rnd(0, 6.283f);
        r.life = 0.5f + 0.1f * k;
        r.size0 = 0.5f * R;
        r.size1 = 4.5f * R;
        r.r0 = r.r1 = dr * 1.05f, r.g0 = r.g1 = dg * 1.05f, r.b0 = r.b1 = db * 1.05f;
        r.a = 0.6f;
        r.fadeIn = 0.02f;
        r.fadeOut = 0.75f;
        r.seed = rnd();
        groundMark(r, terr);
        add(r);
    }
    // 5. The cloud left hanging: brown-grey dust and smoke, drifting off with the wind for a few seconds.
    const int nCloud = int((2 + 2 * k) * ld) + 1;
    for (int i = 0; i < nCloud; ++i) {
        Part s = puff();
        s.p = {x + rnd(-0.4f, 0.4f) * R, gy + rnd(0.6f, 1.8f) * R, y + rnd(-0.4f, 0.4f) * R};
        s.v = {rnd(-4, 4), rnd(6, 14) * sk, rnd(-4, 4)};
        s.age = -rnd(0.25f, 0.55f);
        s.life = rnd(4.0f, 6.0f) * (0.8f + 0.2f * sk);
        s.size0 = rnd(1.0f, 1.3f) * R;
        s.size1 = rnd(2.8f, 3.4f) * R;
        s.vrot = rnd(-0.15f, 0.15f);
        s.drag = 0.5f;
        s.wind = 1;
        s.r0 = 0.25f, s.g0 = 0.215f, s.b0 = 0.18f;
        s.r1 = (dr + 0.42f) * 0.5f, s.g1 = (dg + 0.4f) * 0.5f, s.b1 = (db + 0.37f) * 0.5f;
        s.a = 0.58f;
        s.fadeIn = 0.08f;
        s.fadeOut = 0.6f;
        s.erode0 = 0.2f;
        s.erode1 = 0.7f;
        s.scale = 1.15f;
        s.soft = 0.25f;
        s.soot = 0.3f;
        add(s);
    }
    // 6. The crater.
    scorchMark(terr, x, y, rnd(2.0f, 2.4f) * R, k, sand, 0.35f);
}

void CinematicFx::muzzle(const Terrain& terr, float x, float y, float z, float dx, float dy, int weapon) {
    using W = render::Effects::WeaponClass;
    const W c = W(weapon);
    float size = 0, rays = 0, e = 0;
    int smoke = 0;
    switch (c) {
    case W::Gun: size = 13, rays = 4, e = 1.0f; break;
    case W::Cannon: size = 26, rays = 5, e = 1.3f, smoke = 2; break;
    case W::Artillery: size = 34, rays = 6, e = 1.5f, smoke = 3; break;
    case W::Rocket: size = 20, rays = 0, e = 1.1f, smoke = 2; break;
    default: return;
    }
    const float gy = terr.height(x, y);
    const Vec3 dir = normalize(Vec3{dx, 0, dy});
    Part f;
    f.mode = kFlash;
    f.p = Vec3{x, gy + z, y} + dir * (size * 0.18f);
    f.dir = dir;
    f.life = c == W::Gun ? 0.05f : 0.08f;
    f.size0 = size;
    f.size1 = size * 1.2f;
    f.er = 16 * e, f.eg = 10.5f * e, f.eb = 4.5f * e;
    f.fadeIn = 0;
    f.fadeOut = 0.8f;
    f.rays = rays;
    f.seed = rnd();
    add(f);
    const float ld = load();
    for (int i = 0; i < int(float(smoke) * ld + 0.5f); ++i) {
        Part s = puff();
        const float back = c == W::Rocket ? -1.0f : 1.0f;
        s.p = Vec3{x, gy + z, y} + dir * (4 * back);
        s.v = dir * (rnd(20, 45) * back) + Vec3{rnd(-8, 8), rnd(4, 12), rnd(-8, 8)};
        s.life = rnd(1.0f, 1.8f);
        s.size0 = 4;
        s.size1 = rnd(16, 26) * (c == W::Artillery ? 1.3f : 1.0f);
        s.drag = 2.2f;
        s.buoy = 4;
        s.wind = 0.6f;
        s.r0 = s.r1 = 0.6f, s.g0 = s.g1 = 0.58f, s.b0 = s.b1 = 0.55f;
        s.a = 0.42f;
        s.fadeIn = 0.04f;
        s.fadeOut = 0.6f;
        s.erode0 = 0.25f;
        s.erode1 = 0.72f;
        s.scale = 1.2f;
        s.soft = 0.15f;
        add(s);
    }
}

// A machine-gun round: a bright HDR streak flying from the muzzle to where it hits.
void CinematicFx::tracer(const Terrain& terr, float x, float y, float z, float tx, float ty, float tz) {
    const Vec3 a{x, terr.height(x, y) + z, y}, b{tx, terr.height(tx, ty) + tz + 2, ty};
    const Vec3 d = b - a;
    const float dist = length(d);
    if (dist < 8) return;
    constexpr float kSpeed = 1500;
    Part s;
    s.mode = kSpark;
    s.p = a;
    s.v = d * (kSpeed / dist);
    s.life = std::max(0.04f, dist / kSpeed);
    s.size0 = s.size1 = 1.6f;
    s.stretch = std::min(30.0f, dist * 0.5f) / kSpeed;
    s.er = 18, s.eg = 13, s.eb = 7;
    s.fadeIn = 0;
    s.fadeOut = 0.15f;
    s.seed = rnd();
    s.landed = true;  // flies straight: no ground test
    add(s);
}

// A building lands (Drop-in build-up): a big rolling dust ring out of the footprint, billows at the corners,
// a shock ring on the ground and a few pebbles.
void CinematicFx::dropIn(const Terrain& terr, float x0, float y0, float x1, float y1, std::uint32_t sand, bool fromGround) {
    const float W = x1 - x0, H = y1 - y0, cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float big = fromGround ? 0.55f : 1.0f;
    const float k = std::clamp((W + H) / 192.0f, 0.5f, 1.4f);
    // Dust in the air reads paler than the ground it comes from.
    const float sr = float(sand & 0xFF) / 255.0f, sg = float((sand >> 8) & 0xFF) / 255.0f, sb = float((sand >> 16) & 0xFF) / 255.0f;
    const float dr = sr + (0.96f - sr) * 0.4f, dg = sg + (0.87f - sg) * 0.4f, db = sb + (0.72f - sb) * 0.4f;
    const float ld = load();
    auto edge = [&](float u, float& x, float& y, float& nx, float& ny) {
        const float per = 2 * (W + H), s = u * per;
        if (s < W) x = x0 + s, y = y0, nx = 0, ny = -1;
        else if (s < W + H) x = x1, y = y0 + (s - W), nx = 1, ny = 0;
        else if (s < 2 * W + H) x = x1 - (s - W - H), y = y1, nx = 0, ny = 1;
        else x = x0, y = y1 - (s - 2 * W - H), nx = -1, ny = 0;
        const float ox = x - cx, oy = y - cy, l = std::sqrt(ox * ox + oy * oy) + 1e-3f;
        nx = 0.55f * nx + 0.45f * ox / l;
        ny = 0.55f * ny + 0.45f * oy / l;
        const float nl = std::sqrt(nx * nx + ny * ny) + 1e-3f;
        nx /= nl, ny /= nl;
    };
    const int ring = int(std::clamp((W + H) / 6.0f, 18.0f, 48.0f) * big * ld);
    for (int j = 0; j < ring; ++j) {
        float x, y, nx, ny;
        edge((float(j) + rnd(0, 0.8f)) / float(std::max(1, ring)), x, y, nx, ny);
        Part d = puff();
        const float sp = rnd(80, 150) * (0.7f + 0.3f * big) * k;
        d.p = {x + nx * rnd(0, 4), terr.height(x, y) + 3, y + ny * rnd(0, 4)};
        d.v = {nx * sp, rnd(3, 10), ny * sp};
        d.life = rnd(2.6f, 4.2f);
        d.size0 = rnd(12, 18) * k;
        d.size1 = rnd(50, 72) * k * (0.7f + 0.3f * big);
        d.vrot = rnd(-0.5f, 0.5f);
        d.drag = 2.3f;
        d.wind = 0.5f;
        d.r0 = d.r1 = dr, d.g0 = d.g1 = dg, d.b0 = d.b1 = db;
        d.a = 0.6f;
        d.fadeIn = 0.03f;
        d.fadeOut = 0.55f;
        d.erode0 = 0.22f;
        d.erode1 = 0.72f;
        d.scale = 1.35f;
        d.soft = 0.2f;
        add(d);
    }
    // Billows: slower, taller puffs rising at the corners and sides.
    const int billows = int(8 * big * ld) + 1;
    for (int j = 0; j < billows; ++j) {
        float x, y, nx, ny;
        edge(rnd(), x, y, nx, ny);
        Part d = puff();
        d.p = {x, terr.height(x, y) + 4, y};
        d.v = {nx * rnd(20, 45), rnd(18, 38) * big, ny * rnd(20, 45)};
        d.life = rnd(3.5f, 5.5f);
        d.size0 = rnd(16, 22) * k;
        d.size1 = rnd(60, 85) * k * big;
        d.drag = 1.6f;
        d.gravity = 6;
        d.wind = 0.8f;
        d.r0 = d.r1 = dr, d.g0 = d.g1 = dg, d.b0 = d.b1 = db;
        d.a = 0.5f;
        d.fadeIn = 0.05f;
        d.fadeOut = 0.6f;
        d.erode0 = 0.22f;
        d.erode1 = 0.72f;
        d.scale = 1.3f;
        add(d);
    }
    Part r;
    r.mode = kRing;
    r.p = {cx, terr.height(cx, cy), cy};
    r.rot = rnd(0, 6.283f);
    r.life = 0.9f;
    r.size0 = (W + H) * 0.5f;
    r.size1 = (W + H) * 1.5f * (0.6f + 0.4f * big);
    r.r0 = r.r1 = dr, r.g0 = r.g1 = dg, r.b0 = r.b1 = db;
    r.a = 0.55f * big;
    r.fadeIn = 0.02f;
    r.fadeOut = 0.75f;
    r.seed = rnd();
    groundMark(r, terr);
    add(r);
    for (int j = 0; j < int(10 * big * ld); ++j) {
        float x, y, nx, ny;
        edge(rnd(), x, y, nx, ny);
        Part d;
        d.mode = kDebris;
        const float sp = rnd(40, 90);
        d.p = {x, terr.height(x, y) + 3, y};
        d.v = {nx * sp, rnd(50, 110), ny * sp};
        d.life = rnd(1.0f, 1.6f);
        d.size0 = d.size1 = rnd(1.4f, 2.4f);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-9, 9);
        d.gravity = 320;
        d.r0 = d.r1 = dr * 0.55f, d.g0 = d.g1 = dg * 0.55f, d.b0 = d.b1 = db * 0.55f;
        d.fadeIn = 0;
        d.fadeOut = 0.3f;
        d.seed = rnd();
        add(d);
    }
}

// ---- Ground dust (docs/3d-mode.md "Vehicle dust trails", "Placement dust ring") ----------------------------------

namespace {
// How soft the ground is at (x, y): sand / dunes / spice 1, rock / concrete 0, bilinear between the cell centres
// (as the track marks: render/effects.cpp groundSoftness), so a plume thins out over a sand edge.
float softGround(const game::World& w, float x, float y) {
    const float fx = x / 32 - 0.5f, fy = y / 32 - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float ax = fx - float(x0), ay = fy - float(y0);
    auto soft = [&](int cx, int cy) {
        cx = std::clamp(cx, 0, std::max(0, w.width() - 1));
        cy = std::clamp(cy, 0, std::max(0, w.height() - 1));
        const std::uint32_t f = w.cell(cx, cy).flags;
        return (f & game::cell_flags::kSand) && !(f & game::cell_flags::kConcrete) ? 1.0f : 0.0f;
    };
    const float a = soft(x0, y0) + (soft(x0 + 1, y0) - soft(x0, y0)) * ax;
    const float b = soft(x0, y0 + 1) + (soft(x0 + 1, y0 + 1) - soft(x0, y0 + 1)) * ax;
    return a + (b - a) * ay;
}
}  // namespace

// A ground vehicle (not infantry, not in the air) stands within ~half a tile of (x, y) (world px).
bool CinematicFx::groundVehicleAt(const game::World& w, float x, float y) {
    for (const game::Side& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const game::Unit& u = side.units[i];
            if (!u.alive || u.type < 0 || (u.flags & 0x200000) || w.unitTemplate(u).isInfantry) continue;
            if (std::abs(float(u.posX) / 65536.0f - x) < 16 && std::abs(float(u.posY) / 65536.0f - y) < 16 && u.posZ == 0) return true;
        }
    return false;
}

// Effects intensity scales the ground dust: Clean 0.6, Balanced 0.8, Cinematic 1 (0 = Settings "Ground dust" off).
float CinematicFx::groundDustK() const {
    static const bool off = std::getenv("D2K_GROUND_DUST_OFF") != nullptr;
    if (!groundDust_ || off) return 0;
    switch (budget_.intensity) {
    case render::FxIntensity::Clean: return 0.6f;
    case render::FxIntensity::Balanced: return 0.8f;
    default: return 1.0f;
    }
}

// Airborne dust by ground (albedo before the plume lighting: the sunlit side comes out lighter than the ground, the
// shaded underside about as dark): spice and spice fields orange-rust, sand / dunes warm sand, rock (and concrete
// edges) a grey-brown grit. Spice weighs in by its amount (thin spice: part sand). Bilinear over the cell centres.
CinematicFx::DustTint CinematicFx::dustTint(const game::World& w, float x, float y) {
    constexpr DustTint kSandDust{0.95f, 0.84f, 0.65f}, kSpiceDust{0.94f, 0.6f, 0.36f}, kRockDust{0.74f, 0.68f, 0.6f};
    const float fx = x / 32 - 0.5f, fy = y / 32 - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float ax = fx - float(x0), ay = fy - float(y0);
    DustTint out{0, 0, 0};
    for (int j = 0; j < 4; ++j) {
        const int cx = std::clamp(x0 + (j & 1), 0, std::max(0, w.width() - 1));
        const int cy = std::clamp(y0 + (j >> 1), 0, std::max(0, w.height() - 1));
        const float wt = ((j & 1) ? ax : 1 - ax) * ((j >> 1) ? ay : 1 - ay);
        const game::Cell& c = w.cell(cx, cy);
        DustTint t = kRockDust;
        if ((c.flags & game::cell_flags::kSand) && !(c.flags & game::cell_flags::kConcrete)) {
            const float sp = std::min(1.0f, float(c.spice()) / 2.0f);
            t = {kSandDust.r + (kSpiceDust.r - kSandDust.r) * sp, kSandDust.g + (kSpiceDust.g - kSandDust.g) * sp,
                 kSandDust.b + (kSpiceDust.b - kSandDust.b) * sp};
        }
        out.r += t.r * wt;
        out.g += t.g * wt;
        out.b += t.b * wt;
    }
    return out;
}

// One puff of a vehicle's dust plume: kicked up behind a track (side -1 / +1 across the heading), thrown back a
// little, then it spreads, lifts and drifts with the wind over ~1-2 s. sk = speed factor (0.25 .. 1.4), amount =
// how loose the ground is (sand 1, rock ~0.2).
void CinematicFx::trailDust(const Terrain& terr, float x, float y, float dirX, float dirY, float side, float sk,
                            float amount, bool sand, const DustTint& tint) {
    const float px = -dirY, py = dirX;  // across the heading
    const float rear = 6 + 2 * sk, across = side * rnd(3.5f, 6.0f);
    const float bx = x - dirX * (rear + rnd(0, 3)) + px * across, by = y - dirY * (rear + rnd(0, 3)) + py * across;
    Part d = puff();
    d.p = {bx, terr.height(bx, by) + 1.5f, by};
    const float back = rnd(4, 9) * sk, out = rnd(1, 5) * side;
    d.v = {-dirX * back + px * out + rnd(-1.5f, 1.5f), rnd(2.5f, 6.5f) * (0.6f + 0.4f * sk),
           -dirY * back + py * out + rnd(-1.5f, 1.5f)};
    // Settling: the plume hangs and drifts for ~2.5-4 s, spreading slowly and thinning out over most of its life
    // (the fade and the erosion), so the trail dissolves instead of popping.
    d.life = rnd(2.5f, 3.5f) * (0.9f + 0.1f * std::min(sk, 1.4f));
    d.size0 = rnd(5, 7) * (0.7f + 0.3f * sk);
    d.size1 = rnd(26, 34) * (0.65f + 0.5f * sk) * (sand ? 1.0f : 0.75f);
    d.vrot = rnd(-0.35f, 0.35f);
    d.drag = 1.6f;
    d.wind = 0.85f;
    // The ground's dust (dustTint), a little paler as it spreads and thins.
    d.r0 = tint.r, d.g0 = tint.g, d.b0 = tint.b;
    d.r1 = tint.r + (1 - tint.r) * 0.12f, d.g1 = tint.g + (1 - tint.g) * 0.12f, d.b1 = tint.b + (1 - tint.b) * 0.12f;
    d.a = (0.3f + 0.26f * std::min(sk, 1.2f)) * amount;
    d.fadeIn = 0.05f;
    d.fadeOut = 0.78f;
    d.erode0 = 0.16f;
    d.erode1 = 0.78f;
    d.scale = 1.45f;
    d.soft = 0.3f;
    d.dust = 1;
    add(d);
}

// Dust behind every moving ground vehicle (Effects::unitVisual = the tracked ground vehicles, drawn positions:
// Smooth driving's curves and the tick interpolation included). Render-side randomness only.
void CinematicFx::vehicleDust(const render::Effects& fx, const game::World& w, const Terrain& terr, float dt,
                              const Hidden& hidden) {
    const float k = groundDustK();
    if (k <= 0) {
        if (!tracks_.empty()) tracks_.clear();
        return;
    }
    const float ld = load();
    const float ease = 1.0f - std::exp(-dt * 6.0f);
    lasting_ = true;  // scaled by groundDustK (Effects intensity) itself, not thinned again by the busy budget
    const auto& sides = w.sides();
    for (int si = 0; si < int(sides.size()); ++si) {
        const game::Side& side = sides[std::size_t(si)];
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const game::Unit& u = side.units[i];
            if (!u.alive) continue;
            const render::Effects::UnitVisual* vis = fx.unitVisual(si, i);
            if (!vis || vis->key < 0) continue;  // not a tracked ground vehicle
            const float x = vis->x, y = vis->y;
            const std::uint32_t key = std::uint32_t(si) << 16 | std::uint32_t(i);
            auto it = tracks_.find(key);
            if (it == tracks_.end()) {
                Track t{x, y, rnd(), frame_};
                tracks_.emplace(key, t);
                continue;
            }
            Track& t = it->second;
            const float dx = x - t.x, dy = y - t.y, d = std::hypot(dx, dy);
            t.frame = frame_;
            t.x = x;
            t.y = y;
            if (d > 48 || hidden(x, y)) {  // teleport / reused slot / under the shroud
                t.speed = 0;
                continue;
            }
            t.speed += (d / dt - t.speed) * ease;
            if (d > 0.01f) {  // the heading of the motion (eased: no flicker on corners)
                const float e2 = std::min(1.0f, ease * 2.0f);
                const float hx = t.dirX + (dx / d - t.dirX) * e2, hy = t.dirY + (dy / d - t.dirY) * e2;
                const float hl = std::hypot(hx, hy);
                if (hl > 1e-3f) t.dirX = hx / hl, t.dirY = hy / hl;
            }
            if (t.speed < 3.0f || d < 0.01f) continue;
            const int tx = int(x) >> 5, ty = int(y) >> 5;
            const std::uint32_t cf = w.inBounds(tx, ty) ? w.cell(tx, ty).flags : 0;
            if (cf & game::cell_flags::kConcrete) continue;  // none on concrete slabs
            const float soft = softGround(w, x, y);
            const float amount = soft + (1.0f - soft) * 0.2f;  // rock: a faint grit
            const float sk = std::clamp(t.speed / 40.0f, 0.25f, 1.4f);
            const DustTint tint = dustTint(w, x - t.dirX * 8, y - t.dirY * 8);  // the ground under the rear
            // Puffs per px driven: more when faster (so the plume thickens with speed, not just stretches).
            t.acc += d * (0.12f + 0.14f * sk) * (0.4f + 0.6f * amount) * k * ld;
            int guard = 0;
            while (t.acc >= 1 && guard++ < 6) {
                t.acc -= 1;
                const float sd = (t.n++ & 1) ? 1.0f : -1.0f;
                trailDust(terr, x, y, t.dirX, t.dirY, sd, sk, amount * (0.75f + 0.25f * k), soft > 0.5f, tint);
            }
            if (t.acc > 1) t.acc = 0;
        }
    }
    lasting_ = false;
    if ((frame_ & 255) == 0)
        for (auto it = tracks_.begin(); it != tracks_.end();)
            it = frame_ - it->second.frame > 120 ? tracks_.erase(it) : std::next(it);
}

// Buildings that appear (placed, an MCV deploying, a reinforcement building): a building slot first seen with
// its build-up starting raises a placement ring. Read from the World only (render-side; nothing in src/game).
void CinematicFx::placementRings(const game::World& w, const Terrain& terr, const Hidden& hidden) {
    const bool on = groundDustK() > 0 && placementRing_;
    // Check of the look: D2K_RING_DEMO=<s> rings every building in view every <s> effect seconds.
    static const float demo = std::getenv("D2K_RING_DEMO") ? float(std::atof(std::getenv("D2K_RING_DEMO"))) : 0.0f;
    bool demoNow = false;
    if (demo > 0 && on) {
        ringDemoT_ += dt_;
        if (ringDemoT_ >= demo) ringDemoT_ -= demo, demoNow = true;
    }
    const auto& sides = w.sides();
    for (int si = 0; si < int(sides.size()); ++si) {
        const game::Side& side = sides[std::size_t(si)];
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const game::Building& b = side.buildings[i];
            if (!b.alive) continue;
            const std::uint32_t key = std::uint32_t(si) << 16 | std::uint32_t(i), stamp = b.timeAdded + 1;
            auto [it, fresh] = seenBuildings_.try_emplace(key, stamp);
            if (!fresh && !demoNow) {
                if (it->second == stamp) continue;
                it->second = stamp;  // the slot holds a new building
            }
            if (!demoNow) {
                if (!buildingsPrimed_ || !on) continue;
                if (!(b.flags & game::building_flags::kBuildup) || b.buildupCounter > 12) continue;
            }
            const float x0 = float(b.tileX) * 32, y0 = float(b.tileY) * 32;
            const float x1 = float(b.maxX + 1) * 32, y1 = float(b.maxY + 1) * 32;
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            if (hidden(cx, cy)) continue;
            placementRing(w, terr, x0, y0, x1, y1, softGround(w, cx, cy) > 0.3f);
        }
    }
    buildingsPrimed_ = true;
    if (seenBuildings_.size() > 4096) seenBuildings_.clear(), buildingsPrimed_ = false;
}

// The placement ring: low dust rolling out of the footprint edges, a few slower billows at the corners and a
// faint ring on the ground; it billows for ~1-1.5 s and settles (smaller than Drop-in's landing).
void CinematicFx::placementRing(const game::World& w, const Terrain& terr, float x0, float y0, float x1, float y1,
                                bool sand) {
    const float k = groundDustK();
    const float W = x1 - x0, H = y1 - y0, cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float sz = std::clamp((W + H) / 128.0f, 0.5f, 1.4f);
    // The ground's dust (dustTint) round the footprint; each puff takes the ground at its own edge point.
    const DustTint mid = dustTint(w, cx, cy);
    const float dr = mid.r, dg = mid.g, db = mid.b;
    const float ld = load();
    lasting_ = true;  // not a battle event: never thinned by the busy budget
    auto edge = [&](float u, float& x, float& y, float& nx, float& ny) {
        const float per = 2 * (W + H), s = u * per;
        if (s < W) x = x0 + s, y = y0, nx = 0, ny = -1;
        else if (s < W + H) x = x1, y = y0 + (s - W), nx = 1, ny = 0;
        else if (s < 2 * W + H) x = x1 - (s - W - H), y = y1, nx = 0, ny = 1;
        else x = x0, y = y1 - (s - 2 * W - H), nx = -1, ny = 0;
        const float ox = x - cx, oy = y - cy, l = std::sqrt(ox * ox + oy * oy) + 1e-3f;
        nx = 0.5f * nx + 0.5f * ox / l;
        ny = 0.5f * ny + 0.5f * oy / l;
        const float nl = std::sqrt(nx * nx + ny * ny) + 1e-3f;
        nx /= nl, ny /= nl;
    };
    const int ring = int(std::clamp((W + H) / 7.0f, 10.0f, 36.0f) * (0.5f + 0.5f * k) * ld) + 4;
    for (int j = 0; j < ring; ++j) {
        float x, y, nx, ny;
        edge((float(j) + rnd(0, 0.8f)) / float(ring), x, y, nx, ny);
        Part d = puff();
        const float sp = rnd(55, 95) * sz;
        d.p = {x + nx * rnd(0, 3), terr.height(x, y) + 2, y + ny * rnd(0, 3)};
        d.v = {nx * sp, rnd(3, 8), ny * sp};
        d.life = rnd(1.8f, 2.8f);  // rolls out in ~1 s, then hangs and settles
        d.size0 = rnd(8, 12) * sz;
        d.size1 = rnd(34, 48) * sz;
        d.vrot = rnd(-0.6f, 0.6f);
        d.drag = 3.2f;
        d.wind = 0.7f;
        const DustTint t = dustTint(w, x + nx * 12, y + ny * 12);
        d.r0 = t.r, d.g0 = t.g, d.b0 = t.b;
        d.r1 = t.r + (1 - t.r) * 0.12f, d.g1 = t.g + (1 - t.g) * 0.12f, d.b1 = t.b + (1 - t.b) * 0.12f;
        d.a = (0.3f + 0.16f * k) * (sand ? 1.0f : 0.7f);
        d.fadeIn = 0.04f;
        d.fadeOut = 0.75f;
        d.erode0 = 0.22f;
        d.erode1 = 0.8f;
        d.scale = 1.4f;
        d.soft = 0.22f;
        d.dust = 1;
        add(d);
    }
    const int billows = int(4 * k * ld) + 2;
    for (int j = 0; j < billows; ++j) {
        float x, y, nx, ny;
        edge((float(j) + rnd()) / float(billows), x, y, nx, ny);
        Part d = puff();
        d.p = {x, terr.height(x, y) + 3, y};
        d.v = {nx * rnd(15, 30), rnd(12, 22), ny * rnd(15, 30)};
        d.life = rnd(2.4f, 3.4f);
        d.size0 = rnd(12, 16) * sz;
        d.size1 = rnd(44, 60) * sz;
        d.drag = 2.0f;
        d.gravity = 5;
        d.wind = 0.85f;
        const DustTint t = dustTint(w, x + nx * 12, y + ny * 12);
        d.r0 = t.r, d.g0 = t.g, d.b0 = t.b;
        d.r1 = t.r + (1 - t.r) * 0.12f, d.g1 = t.g + (1 - t.g) * 0.12f, d.b1 = t.b + (1 - t.b) * 0.12f;
        d.a = (0.26f + 0.12f * k) * (sand ? 1.0f : 0.7f);
        d.fadeIn = 0.06f;
        d.fadeOut = 0.75f;
        d.erode0 = 0.22f;
        d.erode1 = 0.8f;
        d.scale = 1.35f;
        d.dust = 1;
        add(d);
    }
    Part r;
    r.mode = kRing;
    r.p = {cx, terr.height(cx, cy), cy};
    r.rot = rnd(0, 6.283f);
    r.life = 0.8f;
    r.size0 = (W + H) * 0.5f;
    r.size1 = (W + H) * 1.1f;
    r.r0 = r.r1 = dr, r.g0 = r.g1 = dg, r.b0 = r.b1 = db;
    r.a = 0.35f * k * (sand ? 1.0f : 0.6f);
    r.fadeIn = 0.02f;
    r.fadeOut = 0.75f;
    r.seed = rnd();
    groundMark(r, terr);
    add(r);
    lasting_ = false;
}

void CinematicFx::update(const render::Effects& fx, const game::World& w, const Terrain& terr, float frameDt,
                         const Hidden& hidden) {
    ++frame_;
    if (w.gameTicks() < lastTick_) {  // a new world (restart, load): look at its buildings afresh
        seenBuildings_.clear();
        buildingsPrimed_ = false;
        tracks_.clear();
    }
    // Effect time follows the game: frames without a new game tick for a while = paused.
    float dt = std::clamp(frameDt, 0.0f, 0.1f);
    if (w.gameTicks() == lastTick_) {
        stall_ += dt;
        if (stall_ > 0.2f) dt = 0;
    } else {
        stall_ = 0;
        lastTick_ = w.gameTicks();
    }
    dt_ = dt;
    world_ = &w;
    struct Unbind {
        const game::World*& w;
        ~Unbind() { w = nullptr; }
    } unbind{world_};
    budget_ = fx.budget();  // effects intensity: the Modern effects' busy level (the same events)
    areaK_ = fx.areaK();
    lasting_ = false;
    // Events of this frame (Effects::recordEvents; cleared by its update).
    using K = render::Effects::FxEvent;
    using WC = render::Effects::WeaponClass;
    // Vehicle wrecks: the deaths that became hulks this frame (render3d/wrecks.hpp) go off as a hulk's blast - a
    // bigger fireball - instead of their death explosion and the burning column of a vehicle's death (taken[i]:
    // event i belongs to a hulk).
    static thread_local std::vector<std::uint8_t> taken;
    taken.assign(fx.events().size(), 0);
    if (wrecks_ && wrecks_->any())
        for (Wrecks::Hulk& h : wrecks_->hulks()) {
            if (!h.used || !h.fresh) continue;
            h.fresh = false;
            float k = h.size == Wrecks::kSmall ? 1.0f : h.size == Wrecks::kMedium ? 2.0f : 2.6f;
            bool sand = false;
            for (std::size_t i = 0; i < fx.events().size(); ++i) {
                const K& e = fx.events()[i];
                if (taken[i] || (e.kind != K::Explosion && e.kind != K::VehicleDeath)) continue;
                // (A flyer's death explosion is up where it flew; the flash is on the ground under it.
                // Its events drift along its heading, each at its own pace: a wider net.)
                const float near = h.crash ? 12.0f : 3.0f;
                if (std::abs(e.x - h.sx) > near || std::abs(e.y - h.sy) > near || (e.z >= 8 && !h.crash)) continue;
                taken[i] = 1;
                sand = e.sand;
                if (e.kind == K::Explosion) k = std::clamp(e.scale, 0.6f, 8.0f);
            }
            if (!h.crash) wreckBlast(terr, h, k, sand);
            else if (!h.cargo && !hidden(h.sx, h.sy)) crashHit(terr, h);
        }
    std::size_t evIndex = 0;
    for (const K& e : fx.events()) {
        if (taken[evIndex++]) continue;
        if (hidden(e.x, e.y)) continue;
        switch (e.kind) {
        case K::Explosion:
            onVehicle_ = groundVehicleAt(w, e.x, e.y);
            explosion(terr, e.x, e.y, e.z, std::clamp(e.scale, 0.3f, 8.0f), e.sand, e.z < 8);
            onVehicle_ = false;
            break;
        case K::Impact: impact(terr, e.x, e.y, e.z, std::clamp(e.scale, 0.5f, 2.0f), e.sand, e.armour); break;
        case K::Artillery: artillery(terr, e.x, e.y, e.z, std::clamp(e.scale, 0.3f, 3.0f), e.sand, e.armour); break;
        case K::VehicleDeath: addBurner(e.x, e.y, 4, 1.0f, rnd(16, 22)); break;
        case K::Smoke: {
            // Damage smoke (the original's EX_SMOKE3): a thin grey wisp off a damaged vehicle, darker and thicker
            // the worse it is hit; a building's smoke point: a small brown-grey column.
            const float dmg = std::clamp(1.0f - e.scale * 2.0f, 0.0f, 1.0f);  // 0 at half health .. 1 at none
            lasting_ = true;
            if (e.armour) smokePuff(terr, e.x, e.y, e.z, 0.4f + 0.2f * dmg, 0.9f, 0.3f * budget_.columnLife(), kBuildingFire);
            else if (budget_.columnRate() >= 1 || render::FxBudget::hash01(++thinN_ * 2654435761u) < budget_.columnRate())
                smokePuff(terr, e.x, e.y, e.z, 0.9f + 0.3f * dmg, 0.25f + 0.4f * dmg, 1, kDamageSmoke);
            lasting_ = false;
            break;
        }
        case K::BuildingDeath: {
            const float big = std::max(0.0f, e.scale);
            if (e.scale > 0) addBurner(e.x, e.y, 8, 1.3f + 0.9f * big, rnd(26, 34));
            explosion(terr, e.x, e.y, 0, 2.5f + 1.5f * big, e.sand, true);
            // More blasts over the next ~1.3 s around the footprint (fuel, ammunition, the structure).
            const int n = 3 + int(3 * big);
            const float rad = 18 + 26 * big;
            for (int i = 0; i < n && pending_.size() < 96; ++i) {
                const float a = rnd(0, 6.283f), r = rnd(0.3f, 1.0f) * rad;
                pending_.push_back({rnd(0.15f, 1.3f), e.x + std::cos(a) * r, e.y + std::sin(a) * r, rnd(0, 14),
                                    rnd(1.3f, 2.2f) + big, e.sand});
            }
            glows_.push_back({e.x, e.y, 30, 130 + 90 * big, 1.6f, 0.76f, 0.28f, 0, 1.6f});
            break;
        }
        case K::Muzzle: muzzle(terr, e.x, e.y, e.z, e.tx, e.ty, int(e.weapon)); break;
        case K::Tracer: tracer(terr, e.x, e.y, e.z, e.tx, e.ty, e.tz); break;
        case K::Projectile: {
            const Vec3 p{e.x, terr.height(e.x, e.y) + e.z, e.y};
            auto it = shells_.find(e.key);
            if (it == shells_.end() || length(p - it->second.p) > 64) {
                shells_[e.key] = {p, 0, frame_};
                break;
            }
            Shell& s = it->second;
            const Vec3 d = p - s.p;
            const float dist = length(d);
            s.frame = frame_;
            if (dt <= 0 || dist < 0.01f) break;
            if (e.weapon == WC::Rocket) {
                s.acc += dist;
                const int n = std::min(12, int(s.acc / 4.0f));
                for (int i = 0; i < n; ++i) trailPuff(s.p + d * (float(i + 1) / float(n)), 1, true);
                s.acc -= float(n) * 4.0f;
                Part g;  // the motor's glow
                g.mode = kFlash;
                g.p = p;
                g.life = 0.05f;
                g.size0 = g.size1 = 12;
                g.er = 12, g.eg = 6.5f, g.eb = 2.4f;
                g.fadeIn = 0;
                g.fadeOut = 1;
                add(g);
            } else if (e.weapon == WC::Cannon || e.weapon == WC::Artillery) {
                Part g;  // a glowing shell streak
                g.mode = kSpark;
                g.p = p;
                g.v = d * (1.0f / dt);
                g.life = 0.06f;
                g.size0 = g.size1 = e.weapon == WC::Artillery ? 2.2f : 1.8f;
                g.stretch = std::min(16.0f, dist * 2.5f) / std::max(1.0f, dist / dt);
                g.er = 14, g.eg = 9, g.eb = 4;
                g.fadeIn = 0;
                g.fadeOut = 1;
                g.landed = true;
                add(g);
            }
            s.p = p;
            break;
        }
        }
    }
    lasting_ = true;  // (landing dust: not a battle event)
    for (const auto& im : fx.dropInImpacts()) dropIn(terr, im.x0, im.y0, im.x1, im.y1, im.sand, im.fromGround);
    lasting_ = false;
    placementRings(w, terr, hidden);  // new buildings: a short dust ring (Settings "Ground dust")
    if ((frame_ & 63) == 0)
        for (auto it = shells_.begin(); it != shells_.end();)
            it = frame_ - it->second.frame > 30 ? shells_.erase(it) : std::next(it);
    if (dt <= 0) return;
    const float ld = load();
    // Building deaths' later blasts.
    for (std::size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        p.delay -= dt;
        if (p.delay > 0) {
            ++i;
            continue;
        }
        if (!hidden(p.x, p.y)) explosion(terr, p.x, p.y, p.z, p.k, p.sand, p.z < 8, false);
        pending_[i] = pending_.back();
        pending_.pop_back();
    }
    for (std::size_t i = 0; i < glows_.size();) {
        glows_[i].age += dt;
        if (glows_[i].age >= glows_[i].life) {
            glows_[i] = glows_.back();
            glows_.pop_back();
        } else {
            ++i;
        }
    }
    if (glows_.size() > 96) glows_.erase(glows_.begin(), glows_.begin() + std::ptrdiff_t(glows_.size() - 96));
    // Effects intensity: only so many columns burn at once per screen area; past the cap the oldest die down
    // over a few seconds (their emission fades with the remaining life).
    {
        static thread_local std::vector<int> vis;
        vis.clear();
        for (int i = 0; i < int(burners_.size()); ++i)
            if (!hidden(burners_[std::size_t(i)].x, burners_[std::size_t(i)].y)) vis.push_back(i);
        const int cap = budget_.columnCap(areaK_);
        if (int(vis.size()) > cap) {
            std::sort(vis.begin(), vis.end(), [&](int a, int b) { return burners_[std::size_t(a)].age < burners_[std::size_t(b)].age; });
            for (std::size_t k = std::size_t(cap); k < vis.size(); ++k) {
                Burner& b = burners_[std::size_t(vis[k])];
                b.life = std::min(b.life, b.age + 4.0f);
            }
        }
    }
    lasting_ = true;  // columns of wrecks / ruins / burning buildings: kept, only rate / life budgeted
    const float colRate = budget_.columnRate(), colLife = budget_.columnLife();
    // Burning wrecks and ruins: big smoke columns that thin out.
    for (std::size_t i = 0; i < burners_.size();) {
        Burner& b = burners_[i];
        b.age += dt;
        if (b.age >= b.life) {
            burners_[i] = burners_.back();
            burners_.pop_back();
            continue;
        }
        ++i;
        if (hidden(b.x, b.y)) continue;
        const float f = 1.0f - smooth01(b.age / b.life);
        b.acc += dt * (1.6f + 1.0f * b.k) * f * ld * colRate;
        while (b.acc >= 1) {
            b.acc -= 1;
            smokePuff(terr, b.x + rnd(-6, 6) * b.k, b.y + rnd(-6, 6) * b.k, b.z, b.k * (0.6f + 0.4f * f), 0.95f, 1.25f * colLife);
        }
    }
    // Burning buildings' fire points.
    static thread_local std::vector<render::Effects::SmokeSource> src;
    fx.smokeSources(src);
    // (Effects intensity: fire points within ~1.5 tiles of a stronger one feed its column instead of their own.)
    static thread_local std::vector<int> srcOrder;
    static thread_local std::vector<std::pair<float, float>> lit;
    srcOrder.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) srcOrder[i] = int(i);
    if (budget_.on())
        std::stable_sort(srcOrder.begin(), srcOrder.end(),
                         [&](int a, int b) { return src[std::size_t(a)].strength > src[std::size_t(b)].strength; });
    lit.clear();
    for (int si : srcOrder) {
        const auto& s = src[std::size_t(si)];
        if (hidden(s.x, s.y)) continue;
        if (budget_.on()) {
            bool merged = false;
            for (const auto& [lx, ly] : lit)
                if (std::hypot(lx - s.x, ly - s.y) < budget_.mergeRadius()) merged = true;
            if (merged) continue;
            lit.emplace_back(s.x, s.y);
        }
        float& acc = smokeAcc_[s.key];
        acc += dt * 2.6f * s.strength * ld * colRate;  // (the whole column: the Modern sprites of it are not drawn)
        while (acc >= 1) {
            acc -= 1;
            smokePuff(terr, s.x, s.y, s.z + 4, 0.6f + 0.5f * s.strength, 1.0f, 1.2f * colLife, kBuildingFire);
        }
    }
    if (smokeAcc_.size() > 512) smokeAcc_.clear();
    if (wrecks_ && wrecks_->any()) burnWrecks(terr, dt, ld, hidden);
    lasting_ = false;
    // Dust behind moving ground vehicles (Settings "Ground dust").
    vehicleDust(fx, w, terr, dt, hidden);
    // Step.
    const std::size_t n0 = parts_.size();
    for (std::size_t i = 0; i < parts_.size() && i < n0;) {
        Part& p = parts_[i];
        p.age += dt;
        if (p.age >= p.life || (p.cut > 0 && p.age >= p.cut)) {
            parts_[i] = parts_.back();
            parts_.pop_back();
            continue;
        }
        ++i;
        if (p.age < 0 || p.mode == kScorch || p.mode == kRing) continue;
        if (p.landed && p.mode == kSpark && p.ember) continue;  // an ember glowing on the ground
        const float drag = std::exp(-p.drag * dt);
        // Smoke leans further downwind the higher it has risen (calm near the ground).
        float gh = 0;
        if (p.mode == kPuff) gh = terr.height(p.p.x, p.p.z);
        const float lean = p.mode == kPuff && p.soot > 0 ? std::clamp(0.35f + (p.p.y - gh) / 45.0f, 0.35f, 2.4f) : 1.0f;
        const Vec3 wv = wind_ * (p.wind * lean);
        const Vec3 prev = p.p;
        p.v = wv + (p.v - wv) * drag;
        p.v.y += (p.buoy - p.gravity) * dt;
        p.p += p.v * dt;
        p.rot += p.vrot * dt;
        if (p.mode == kDebris || (p.mode == kSpark && !p.landed)) {
            const float gh = terr.height(p.p.x, p.p.z);
            if (p.p.y < gh + 1) {
                p.p.y = gh + 1;
                if (p.mode == kSpark && p.ember) {
                    p.landed = true;  // lies there glowing, cooling
                    p.v = {0, 0, 0};
                } else if (!p.bounced && p.v.y < -20) {
                    p.v = {p.v.x * 0.35f, -p.v.y * 0.28f, p.v.z * 0.35f};
                    p.vrot *= 0.4f;
                    p.bounced = true;
                } else {
                    p.v = {0, 0, 0};
                    p.vrot = 0;
                    p.trail = 0;
                }
            }
            if (p.trail > 0 && p.age < 1.2f) {
                p.trailAcc += length(p.p - prev);
                int n = 0;
                while (p.trailAcc >= p.trail) p.trailAcc -= p.trail, ++n;
                const Vec3 at = p.p;
                const float k = p.size0 * 0.3f;
                for (int j = 0; j < std::min(n, 3); ++j) trailPuff(at, k, false);  // may reallocate parts_: p is dead now
            }
        } else if (p.mode == kPuff) {
            if (p.p.y < gh - 4) p.p.y = gh - 4;
        }
    }
}

// Effects intensity: every puff eases its `clear` towards how much it covers a unit in front of which it is drawn
// (pins_, set by the renderer): measured across the view (camera right / up), the closer the unit is to the puff's
// middle the more see-through it gets, down to FxBudget::seeThrough (lower over the player's selected units).
void CinematicFx::seeThrough(const Frame& f) {
    const float ease = 1 - std::exp(-dt_ * 10.0f);
    if (ease <= 0) return;
    if (pins_.empty()) {
        for (Part& p : parts_)
            if (p.clear > 0) p.clear -= p.clear * ease;
        return;
    }
    struct Pin {
        float u, v, depth, r, keep;
    };
    static thread_local std::vector<Pin> pins;
    static thread_local std::vector<int> head, next;
    pins.clear();
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
    for (const UnitPin& q : pins_) {
        const Pin pin{dot(q.p, f.right), dot(q.p, f.up), dot(q.p - f.eye, f.forward), q.r, budget_.seeThrough(q.sel)};
        pins.push_back(pin);
        u0 = std::min(u0, pin.u - pin.r), v0 = std::min(v0, pin.v - pin.r);
        u1 = std::max(u1, pin.u + pin.r), v1 = std::max(v1, pin.v + pin.r);
    }
    constexpr float kCell = 64;
    const int gw = std::clamp(int((u1 - u0) / kCell) + 1, 1, 512), gh = std::clamp(int((v1 - v0) / kCell) + 1, 1, 512);
    head.assign(std::size_t(gw * gh), -1);
    next.assign(pins.size(), -1);
    auto cellOf = [&](float u, float v, int& cx, int& cy) {
        cx = std::clamp(int((u - u0) / kCell), 0, gw - 1);
        cy = std::clamp(int((v - v0) / kCell), 0, gh - 1);
    };
    for (int i = 0; i < int(pins.size()); ++i) {
        int cx, cy;
        cellOf(pins[std::size_t(i)].u, pins[std::size_t(i)].v, cx, cy);
        next[std::size_t(i)] = head[std::size_t(cy * gw + cx)];
        head[std::size_t(cy * gw + cx)] = i;
    }
    for (Part& p : parts_) {
        if (p.mode != kPuff || p.age < 0) continue;
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float r = 0.5f * (p.size0 + (p.size1 - p.size0) * (1 - (1 - t) * (1 - t)));
        const float pu = dot(p.p, f.right), pv = dot(p.p, f.up), pd = dot(p.p - f.eye, f.forward);
        float target = 0;
        if (pu + r + 40 >= u0 && pu - r - 40 <= u1 && pv + r + 40 >= v0 && pv - r - 40 <= v1) {
            int ca, cb, cc, cd;
            cellOf(pu - r - 40, pv - r - 40, ca, cb);
            cellOf(pu + r + 40, pv + r + 40, cc, cd);
            for (int cy = cb; cy <= cd; ++cy)
                for (int cx = ca; cx <= cc; ++cx)
                    for (int i = head[std::size_t(cy * gw + cx)]; i >= 0; i = next[std::size_t(i)]) {
                        const Pin& q = pins[std::size_t(i)];
                        if (pd > q.depth + r) continue;  // behind the unit: the depth test hides it there anyway
                        const float reach = 0.85f * r + q.r;
                        const float d = std::hypot(q.u - pu, q.v - pv);
                        if (d >= reach) continue;
                        const float cover = std::min(1.0f, (1 - d / reach) * 1.6f);
                        target = std::max(target, cover * (1 - q.keep));
                    }
        }
        p.clear += (target - p.clear) * ease;
        if (p.clear < 1e-3f) p.clear = 0;
    }
}

// Building life (docs/3d-mode.md "Building life"): the stacks / vents of working buildings and their beacons. Not
// `lasting_`: budgeted (fewer, smaller, paler puffs as the view gets busy) like the other transient smoke; the rates
// follow the columns' rate (effects intensity).
void CinematicFx::buildingLife(const Terrain& terr, const LifeSource* src, int n, float lamps) {
    beaconLights_.clear();
    const float dt = dt_;
    lifeClock_ += double(dt);
    const float rate = std::max(0.5f, budget_.columnRate()) * load();
    for (int i = 0; i < n; ++i) {
        const LifeSource& s = src[i];
        if (s.kind == kLifeBeacon) {
            // A slow red blink (1.9 s, a 0.16 s flash), each beacon in its own phase: a small glowing point and, at
            // night, a little red light on what is round it.
            constexpr double kPeriod = 1.9, kOn = 0.16;
            const double ph = std::fmod(lifeClock_ + double(s.key % 997u) * 0.0131, kPeriod);
            const bool lit = ph < kOn;
            const float on = lit ? float(std::sin(ph / kOn * 3.14159265)) : 0.0f;
            float& was = lifeAcc_[s.key];
            if (lit && was == 0 && dt > 0) {
                Part f;
                f.mode = kFlash;
                f.p = Vec3{s.x, terr.height(s.x, s.y) + s.z, s.y};
                f.life = 0.18f;
                f.size0 = 5.0f * s.k;
                f.size1 = 6.0f * s.k;
                f.er = 9.0f, f.eg = 0.9f, f.eb = 0.5f;
                f.fadeIn = 0.25f;
                f.fadeOut = 0.6f;
                f.rays = 0;
                f.seed = rnd();
                lasting_ = true;  // (a light, not smoke: never thinned out)
                add(f);
                lasting_ = false;
            }
            was = lit ? 1.0f : 0.0f;
            if (on > 0.02f && lamps > 0.05f)
                beaconLights_.push_back({s.x, s.y, s.z, 11.0f + 8.0f * s.k, 0.8f * on * lamps, 0.06f * on * lamps, 0.03f * on * lamps});
            continue;
        }
        if (dt <= 0) continue;
        float& acc = lifeAcc_[s.key];
        const float k = std::clamp(s.k, 0.2f, 1.6f);
        const float per = s.kind == kLifeStack ? 6.0f : s.kind == kLifeSteam ? 5.0f : 2.5f;  // puffs/s at k = 1 (a wisp is a stream)
        acc += dt * per * std::sqrt(k) * rate;
        while (acc >= 1) {
            acc -= 1;
            const Smoke kind = s.kind == kLifeStack ? kStackWisp : s.kind == kLifeSteam ? kSteam : kVentHeat;
            lasting_ = true;  // (rate-budgeted above; a wisp cut at the transient smoke cap would never rise)
            smokePuff(terr, s.x, s.y, s.z, std::max(0.65f, k), 0, 1, kind);
            lasting_ = false;
        }
    }
    if (lifeAcc_.size() > 1024) lifeAcc_.clear();
}

void CinematicFx::lights(std::vector<Light>& out) const {
    out.clear();
    out.insert(out.end(), beaconLights_.begin(), beaconLights_.end());  // building life (buildingLife)
    for (const Glow& g : glows_) {
        const float t = g.age / g.life;
        // A quick rise, then fading as the fireball cools; a little flicker.
        float k = std::min(1.0f, t * 12.0f) * (1 - t) * (1 - t);
        k *= 0.85f + 0.15f * std::sin(g.age * 37.0f + g.x);
        if (k < 0.02f) continue;
        out.push_back({g.x, g.y, g.z, g.radius, g.r * k, g.g * k, g.b * k});
    }
    // Burning wrecks and ruins: a low flickering fire glow while they burn hard (lights the ground and the
    // base of their smoke column from below).
    for (const Burner& b : burners_) {
        const float f = 1.0f - smooth01(b.age / (b.life * 0.7f));
        if (f < 0.05f) continue;
        const float fl = 0.75f + 0.25f * std::sin(b.age * 13.0f + b.x * 0.1f) * std::sin(b.age * 7.3f + b.y * 0.1f);
        const float k = 0.55f * f * fl * std::min(1.0f, b.age * 3.0f);
        out.push_back({b.x, b.y, 10 * b.k, 34 + 26 * b.k, 1.5f * k, 0.62f * k, 0.2f * k});
    }
    // Burning hulks (vehicle wrecks): a small flickering fire glow on the ground round them.
    if (wrecks_ && wrecks_->any())
        for (const Wrecks::Hulk& h : wrecks_->hulks()) {
            if (!h.used || h.life <= 0) continue;
            const Wrecks::Pose p = Wrecks::pose(h);
            const float fl = 0.75f + 0.25f * std::sin(h.age * 13.0f + h.x * 0.1f) * std::sin(h.age * 7.3f + h.y * 0.1f);
            const float k = 0.5f * p.fire * (1.0f - p.dissolve) * fl * std::min(1.0f, h.age * 3.0f);
            if (k < 0.03f) continue;
            out.push_back({h.x, h.y, 9.0f + (h.crash ? h.agl : 0.0f), 30.0f + 9.0f * float(h.size), 1.5f * k, 0.62f * k, 0.2f * k});
        }
}

// A vehicle's death with a hulk left behind (render3d/wrecks.hpp): the death explosion half as big again
// (k -> the fireball radius x 1.5), cooking off for a moment (later, smaller bursts in it), hot debris thrown in
// arcs, and a few dark pieces that stay lying round the wreck after it is gone. The scorch mark keeps the size it
// always had.
void CinematicFx::wreckBlast(const Terrain& terr, const Wrecks::Hulk& h, float k, bool sand) {
    const float x = h.x, y = h.y;
    const float kBig = std::min(8.0f, k * 1.68f);  // (R ~ k^0.78: x 1.5)
    const int sz = int(h.size);
    hulkBlast_ = true;  // (no lingering column of the blast's own: the hulk smokes)
    explosion(terr, x, y, 0, kBig, sand, true, false);
    hulkBlast_ = false;
    scorchMark(terr, x, y, rnd(2.0f, 2.4f) * fireballRadius(k), k, sand);
    const int nCook = 2 + (sz == Wrecks::kLarge ? 1 : 0);
    for (int i = 0; i < nCook && pending_.size() < 96; ++i)
        pending_.push_back({rnd(0.16f, 0.3f) * float(i + 1), x + rnd(-7, 7), y + rnd(-7, 7), rnd(9, 15), rnd(0.45f, 0.7f) * (0.8f + 0.2f * float(sz)),
                            sand});
    wreckDebris(Vec3{x, terr.height(x, y) + 8, y}, sz, Vec3{}, 5 + 3 * sz, 4 + 2 * sz);
}

// A wreck's debris thrown from c (v0 = what it flew off was moving at): `hot` glowing pieces thrown high, trailing
// smoke, and `left` small dark ones that stay lying about after the hulk is gone.
void CinematicFx::wreckDebris(const Vec3& c, int sz, const Vec3& v0, int hot, int left) {
    const float ld = load();
    // Hot debris: glowing pieces thrown high, trailing smoke.
    const int nHot = int(float(hot) * ld);
    for (int i = 0; i < nHot; ++i) {
        Part d;
        d.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(45, 135);
        d.p = c;
        d.v = v0 + Vec3{std::cos(a) * sp, rnd(150, 250), std::sin(a) * sp};
        d.life = rnd(2.2f, 3.2f);
        d.size0 = d.size1 = rnd(2.4f, 3.8f) + 0.4f * float(sz);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-10, 10);
        d.gravity = 320;
        d.drag = 0.25f;
        const float g = rnd(0.14f, 0.24f);
        d.r0 = d.r1 = g * 1.08f, d.g0 = d.g1 = g, d.b0 = d.b1 = g * 0.88f;
        d.fadeIn = 0;
        d.fadeOut = 0.25f;
        d.er = 7.0f, d.eg = 2.6f, d.eb = 0.5f;
        d.heatLife = rnd(1.1f, 1.7f);
        d.seed = rnd();
        d.scale = 1;
        d.trail = 2.6f;
        add(d);
    }
    // What is left lying about: small dark pieces that fade well after the hulk.
    lasting_ = true;
    const int nLeft = left;
    for (int i = 0; i < nLeft; ++i) {
        Part d;
        d.mode = kDebris;
        const float a = rnd(0, 6.283f), sp = rnd(28, 80);
        d.p = c;
        d.v = v0 + Vec3{std::cos(a) * sp, rnd(70, 150), std::sin(a) * sp};
        d.life = rnd(16, 24);
        d.size0 = d.size1 = rnd(1.8f, 3.2f);
        d.rot = rnd(0, 6.283f);
        d.vrot = rnd(-8, 8);
        d.gravity = 320;
        d.drag = 0.3f;
        const float g = rnd(0.07f, 0.14f);
        d.r0 = d.r1 = g * 1.1f, d.g0 = d.g1 = g, d.b0 = d.b1 = g * 0.86f;
        d.fadeIn = 0;
        d.fadeOut = 0.3f;
        d.er = 3.0f, d.eg = 1.1f, d.eb = 0.25f;
        d.heatLife = 0.9f;
        d.seed = rnd();
        d.scale = 1;
        add(d);
    }
    lasting_ = false;
}

// The hulks' fire while they last (called with lasting_ set: columns are budgeted by rate / life / how many
// smoke at once, like the burners): small flames licking over the hull, a thin smoke column off its top - greyer
// than a ruin's and small, so the sun lights it and the hulk stays in view - and the smoke trail of the turret
// in flight with a puff of dust where it lands.
void CinematicFx::burnWrecks(const Terrain& terr, float dt, float ld, const Hidden& hidden) {
    const float colRate = budget_.columnRate(), colLife = budget_.columnLife();
    const int cap = budget_.columnCap(areaK_);
    for (Wrecks::Hulk& h : wrecks_->hulks()) {
        if (!h.used || h.life <= 0 || hidden(h.x, h.y)) continue;
        const Wrecks::Pose p = Wrecks::pose(h);
        if (h.crash) {  // a shot-down flyer: burning on the way down, then the impact; the hulk burns like any
            if (p.air) {
                crashTrail(h, dt, ld);
                continue;
            }
            if (!h.impactFx) {
                h.impactFx = true;
                lasting_ = false;
                crashImpact(terr, h);
                lasting_ = true;
            }
            if (!h.rests) continue;
        }
        const float fire = p.fire * (1.0f - p.dissolve);
        const float gy = terr.height(h.x, h.y);
        const float sz = float(h.size);
        // Flames: short-lived hot puffs.
        h.flameAcc += dt * (10.0f + 4.0f * sz) * fire * ld * std::min(1.0f, h.age * 2.0f);
        while (h.flameAcc >= 1) {
            h.flameAcc -= 1;
            Part q = puff();
            const float r = 4.0f + 2.5f * sz, s = 0.85f + 0.2f * sz;
            q.p = {h.x + rnd(-r, r), gy + p.lift + h.top * rnd(0.45f, 0.8f), h.y + rnd(-r, r)};
            q.v = {rnd(-4, 4), rnd(14, 26), rnd(-4, 4)};
            q.life = rnd(0.45f, 0.8f);
            q.size0 = rnd(3.0f, 4.5f) * s;
            q.size1 = rnd(5.5f, 8.0f) * s;
            q.vrot = rnd(-1.5f, 1.5f);
            q.drag = 1.5f;
            q.buoy = 40;
            q.wind = 0.5f;
            sootColour(q, 1);
            q.a = 0.8f;
            q.fadeIn = 0.06f;
            q.fadeOut = 0.5f;
            q.er = q.eg = q.eb = rnd(0.85f, 1.2f);
            q.heatLife = q.life * rnd(0.75f, 0.95f);
            q.erode0 = 0.1f;
            q.erode1 = 0.7f;
            q.scale = 1.7f;
            q.soft = 0.1f;
            q.soot = 1;
            add(q);
        }
        // Smoke: only the newest hulks' (the column cap), thinner as the fire dies down.
        int newer = 0;
        for (const Wrecks::Hulk& o : wrecks_->hulks()) newer += o.used && o.serial > h.serial && !hidden(o.x, o.y);
        if (newer < cap) {
            h.smokeAcc += dt * (1.0f + 0.3f * sz) * fire * ld * colRate * std::min(1.0f, h.age);
            while (h.smokeAcc >= 1) {
                h.smokeAcc -= 1;
                smokePuff(terr, h.x + rnd(-3, 3), h.y + rnd(-3, 3), p.lift + h.top * 0.85f, (0.4f + 0.13f * sz) * (0.65f + 0.35f * fire), 0.45f,
                          0.85f * colLife, kWreckSmoke);
            }
        }
        // The turret in flight trails smoke; where it lands it raises a little dust.
        if (h.turret && h.posed) {
            const Vec3 tp{h.pivot.x + p.turretOff.x, h.pivot.y + p.turretOff.y, h.pivot.z + p.turretOff.z};
            if (p.turretAir && h.age > Wrecks::kTurretDelay) {
                h.trailAcc += dt * 26.0f * ld;
                while (h.trailAcc >= 1) {
                    h.trailAcc -= 1;
                    trailPuff(tp + Vec3{rnd(-2, 2), rnd(-2, 2), rnd(-2, 2)}, 1.1f, false);
                }
            } else if (!h.landFx) {
                h.landFx = true;
                {
                    const DustTint gd = groundDust(tp.x, tp.z, true);
                    for (int i = 0; i < 3; ++i)
                        dustPuff(terr, tp.x + rnd(-5, 5), tp.z + rnd(-5, 5), 0.5f, gd.r * 0.9f, gd.g * 0.9f, gd.b * 0.9f, 0.4f);
                }
            }
        }
    }
}

// Aircraft crashes (docs/3d-mode.md "Aircraft crashes"). The hit: a fire burst on the hull up where it flew (no
// ground dust, no scorch, soot that clears fast), sparks, and a few glowing parts flying off with its way on.
void CinematicFx::crashHit(const Terrain& terr, const Wrecks::Hulk& h) {
    const float z = std::max(0.0f, h.from.y - terr.height(h.from.x, h.from.z));
    hulkBlast_ = true;
    explosion(terr, h.from.x, h.from.z, z - 4.0f, h.heavy ? 1.1f : 0.8f, false, false, false);
    hulkBlast_ = false;
    const float T = std::max(0.3f, h.fallT);
    const Vec3 v0{(h.to.x - h.from.x) / T, 0.0f, (h.to.z - h.from.z) / T};
    wreckDebris(h.from + Vec3{0.0f, 0.4f * h.top, 0.0f}, 0, v0 * 0.8f + Vec3{0.0f, -110.0f, 0.0f}, h.heavy ? 5 : 3, 0);
}

// The fall: flames on the hull and a trail of fire and black smoke greying as it thins, left along the path.
void CinematicFx::crashTrail(Wrecks::Hulk& h, float dt, float ld) {
    const float s = h.cargo ? 0.6f : h.heavy ? 1.25f : 0.9f;
    const Vec3 c{h.x, h.wy + 0.5f * h.top, h.y};
    h.flameAcc += dt * (h.cargo ? 16.0f : 34.0f) * ld;
    while (h.flameAcc >= 1) {
        h.flameAcc -= 1;
        Part q = puff();
        q.p = c + Vec3{rnd(-3, 3) * s, rnd(-2, 3), rnd(-3, 3) * s};
        q.v = {rnd(-5, 5), rnd(4, 16), rnd(-5, 5)};
        q.life = rnd(0.4f, 0.75f);
        q.size0 = rnd(4.0f, 6.0f) * s;
        q.size1 = rnd(8.0f, 12.0f) * s;
        q.vrot = rnd(-1.5f, 1.5f);
        q.drag = 1.5f;
        q.buoy = 30;
        q.wind = 0.4f;
        sootColour(q, 1);
        q.a = 0.85f;
        q.fadeIn = 0.04f;
        q.fadeOut = 0.5f;
        q.er = q.eg = q.eb = rnd(1.0f, 1.5f);
        q.heatLife = q.life * rnd(0.75f, 0.95f);
        q.erode0 = 0.1f;
        q.erode1 = 0.7f;
        q.scale = 1.6f;
        q.soft = 0.1f;
        q.soot = 1;
        add(q);
    }
    if (h.cargo) return;  // (one trail: the carrier's)
    h.smokeAcc += dt * 24.0f * ld;
    while (h.smokeAcc >= 1) {
        h.smokeAcc -= 1;
        Part q = puff();
        q.p = c + Vec3{rnd(-2, 2), rnd(0, 4), rnd(-2, 2)};
        q.v = {rnd(-4, 4), rnd(3, 10), rnd(-4, 4)};
        q.life = rnd(2.0f, 3.2f);
        q.size0 = rnd(5.0f, 7.0f) * s;
        q.size1 = rnd(20.0f, 30.0f) * s;
        q.vrot = rnd(-0.5f, 0.5f);
        q.drag = 1.4f;
        q.buoy = 7;
        q.wind = 0.8f;
        sootColour(q, 0.95f);  // brown-black, thinning to a warm grey
        q.a = 0.55f;
        q.fadeIn = 0.05f;
        q.fadeOut = 0.6f;
        q.erode0 = 0.14f;
        q.erode1 = 0.68f;
        q.scale = 1.15f;
        q.soft = 0.15f;
        q.soot = 0.85f;
        add(q);
    }
    h.trailAcc += dt * 14.0f * ld;
    while (h.trailAcc >= 1) {  // sparks shed on the way
        h.trailAcc -= 1;
        Part k;
        k.mode = kSpark;
        k.p = c;
        k.v = {rnd(-40, 40), rnd(-20, 50), rnd(-40, 40)};
        k.life = rnd(0.3f, 0.7f);
        k.size0 = k.size1 = rnd(1.1f, 1.8f);
        k.gravity = 320;
        k.drag = 1.2f;
        k.stretch = 0.045f;
        const float hot = rnd(0.8f, 1.2f);
        k.er = 14 * hot, k.eg = 7.5f * hot, k.eb = 2.6f * hot;
        k.fadeIn = 0;
        k.fadeOut = 0.5f;
        k.seed = rnd();
        add(k);
    }
}

// The impact: an explosion about a vehicle's death in size (with its shock ring, rolling dust, thrown sand,
// sparks and debris), a scorch mark, a ring of dust kicked out along the ground, glowing parts thrown on along its
// way; what a carryall carried hits with a smaller burst of its own.
void CinematicFx::crashImpact(const Terrain& terr, const Wrecks::Hulk& h) {
    const float x = h.to.x, y = h.to.z;
    const float k = h.cargo ? 0.9f + 0.25f * float(h.size) : h.heavy ? 2.4f : 1.8f;
    hulkBlast_ = h.rests;  // (the hulk smokes by itself; the fireball's soot clears to show it)
    explosion(terr, x, y, 0, k, h.sand, true, false);
    hulkBlast_ = false;
    scorchMark(terr, x, y, rnd(2.0f, 2.4f) * fireballRadius(k * 0.7f), k * 0.7f, h.sand);
    const float ld = load();
    const DustTint gd = groundDust(x, y, h.sand);
    const float dr = gd.r * 0.92f, dg = gd.g * 0.92f, db = gd.b * 0.92f;
    const int nDust = int((h.cargo ? 4.0f : 8.0f) * ld);
    for (int i = 0; i < nDust; ++i) {
        const float a = 6.283f * (float(i) + rnd(0, 0.7f)) / float(std::max(1, nDust)), r = rnd(10, 22) * (h.cargo ? 0.7f : 1.0f);
        dustPuff(terr, x + std::cos(a) * r, y + std::sin(a) * r, h.cargo ? 0.6f : 0.9f, dr, dg, db, 0.42f);
    }
    if (h.cargo) return;
    if (h.rests && pending_.size() < 96) pending_.push_back({rnd(0.25f, 0.4f), x + rnd(-6, 6), y + rnd(-6, 6), rnd(6, 10), rnd(0.45f, 0.65f), h.sand});
    const float T = std::max(0.3f, h.fallT);
    const Vec3 v0{(h.to.x - h.from.x) / T * 0.5f, 0.0f, (h.to.z - h.from.z) / T * 0.5f};
    wreckDebris(Vec3{x, terr.height(x, y) + 6, y}, h.heavy ? 1 : 0, v0, h.heavy ? 7 : 5, h.rests ? (h.heavy ? 5 : 3) : 0);
}

void CinematicFx::prepare(SDL_GPUCommandBuffer* cmd, const Frame& f, const render::Effects& fx, const Terrain& terr,
                          const Hidden& hidden) {
    const bool texReady = texUploaded_ || texBuilt_.load(std::memory_order_acquire);
    seeThrough(f);
    // Visible particles: the scene-pass set first (ground marks first, the rest back to front), then the
    // half-resolution set (puffs and big glows, back to front).
    order_.clear();
    mainCount_ = 0;
    halfBounds_[0] = halfBounds_[1] = 1e9f;
    halfBounds_[2] = halfBounds_[3] = -1e9f;
    for (int i = 0; i < int(parts_.size()); ++i) {
        const Part& p = parts_[std::size_t(i)];
        if (p.age < 0 || hidden(p.p.x, p.p.z)) continue;
        const float depth = dot(p.p - f.eye, f.forward);
        const bool half = p.mode == kPuff || (p.mode == kFlash && p.rays == 0 && p.size1 >= 20);
        if (p.dust > 0 && f.sun.y > 0.05f) {
            // Ground dust: its soft shadow on the ground (an instance of its own, drawn with the ground marks).
            order_.push_back({0, depth, p.id, -1 - i});
            ++mainCount_;
        }
        if (half) {
            order_.push_back({3, depth, p.id, i});
            const float r = 0.5f * std::max(p.size0, p.size1);
            halfBounds_[0] = std::min(halfBounds_[0], p.p.x - r);
            halfBounds_[1] = std::min(halfBounds_[1], p.p.z - r);
            halfBounds_[2] = std::max(halfBounds_[2], p.p.x + r);
            halfBounds_[3] = std::max(halfBounds_[3], p.p.z + r);
        } else {
            order_.push_back({p.mode == kScorch ? 0u : p.mode == kRing ? 1u : 2u, depth, p.id, i});
            ++mainCount_;
        }
    }
    // (The keys used to be one float - depth plus 1e9 / -1e8 per set - which rounded the depth to 8-128 px:
    // puffs within that distance tied and the unstable sort, fed a list reshuffled by swap-removal, swapped
    // them every few frames. Overlapping smoke flickered.)
    std::sort(order_.begin(), order_.end(), [](const Order& a, const Order& b) {
        if (a.set != b.set) return a.set < b.set;
        if (a.depth != b.depth) return a.depth > b.depth;
        return a.id < b.id;
    });
    halfCount_ = std::uint32_t(order_.size()) - mainCount_;
    // Nearby light per particle: the effects' light list (explosions, fires, muzzle flashes) at its source and
    // our fireball glows.
    struct L {
        float x, y, z, r2, r, g, b;
    };
    static thread_local std::vector<L> lights;
    static thread_local std::vector<Light> glow;
    lights.clear();
    for (const auto& l : fx.lights()) {
        const float a = float(l.c >> 24) / 255.0f;
        if (a < 0.05f || l.size < 24 || l.blast) continue;  // blasts: our own glows below
        const float gy = l.y + l.z;
        const float rad = l.size * 0.6f;
        lights.push_back({l.x, terr.height(l.x, gy) + l.z + 8, gy, rad * rad, float(l.c & 0xFF) / 255.0f * a * 1.6f,
                          float((l.c >> 8) & 0xFF) / 255.0f * a * 1.6f, float((l.c >> 16) & 0xFF) / 255.0f * a * 1.6f});
    }
    this->lights(glow);
    for (const Light& l : glow) {
        const float rad = l.radius * 0.8f;
        lights.push_back({l.x, terr.height(l.x, l.y) + l.z, l.y, rad * rad, l.r * 0.6f, l.g * 0.6f, l.b * 0.6f});
    }
    inst_.clear();
    inst_.reserve(order_.size() * kFloats);
    const std::uint32_t mainOrders = mainCount_;
    for (const Order& ord : order_) {
        const bool shadow = ord.idx < 0;
        const Part& p = parts_[std::size_t(shadow ? -1 - ord.idx : ord.idx)];
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float size = p.size0 + (p.size1 - p.size0) * (1 - (1 - t) * (1 - t));
        float a = p.a;
        if (p.fadeIn > 0) a *= smooth01(t / p.fadeIn);
        a *= 1 - smooth01((t - (1 - p.fadeOut)) / std::max(1e-3f, p.fadeOut));
        a *= render::FxBudget::capAlpha(p.age, p.cut) * (1 - p.clear);  // effects intensity
        if (shadow) {
            // The puff's shadow: where the sun ray through it meets the ground (cast away from the sun), a soft dark
            // blob a little wider than the puff, fainter the higher the puff hangs (softer, more spread light).
            // (From the puff's upper part, so the shadow peeks out on the side away from the sun instead of lying
            // hidden right under it.)
            const float gh0 = terr.height(p.p.x, p.p.z), above = std::max(0.0f, p.p.y - gh0);
            const float k = (above + 0.3f * size) / std::max(0.25f, f.sun.y);
            const float sx = p.p.x - f.sun.x * k, sz = p.p.z - f.sun.z * k;
            const float gy = terr.height(sx, sz);
            const float ss = size * (1.15f + above / 60.0f);
            const float sa = std::min(0.4f, a * p.dust * 0.6f / (1.0f + above / 60.0f));
            const float vals[kFloats] = {sx, gy, sz, ss,
                                         0, 0, 0, p.rot,
                                         0, 0, 0, sa,
                                         0, 0, 0, float(kShadow),
                                         gy, 0, 0, 0,
                                         0, 0, 0, t,
                                         p.seed, 1, 0, 0,
                                         0, 0, 0, 0};
            mainCount_ += std::uint32_t(emitGroundMark(vals, terr) - 1);
            continue;
        }
        float heat = p.heatLife > 0 ? std::max(0.0f, 1 - p.age / p.heatLife) : (p.mode == kPuff ? 0.0f : 1.0f);
        if (p.mode == kPuff) heat = std::pow(heat, 1.3f);
        else if (p.heatLife > 0) heat *= heat;
        float er = p.er, eg = p.eg, eb = p.eb;
        if (p.mode == kSpark || p.mode == kDebris) {
            er *= heat, eg *= heat, eb *= heat;
            if (p.ember && p.landed) {  // a glowing ember on the ground: dimmer, flickering
                const float fl = 0.32f + 0.12f * std::sin(p.age * 11.0f + p.seed * 40.0f);
                er *= fl, eg *= fl * 0.75f, eb *= fl * 0.5f;
            }
        }
        float lr = 0, lg = 0, lb = 0;
        if (p.mode == kPuff || p.mode == kDebris || p.mode >= kScorch)
            for (const L& l : lights) {
                const float dx = p.p.x - l.x, dy = p.p.y - l.y, dz = p.p.z - l.z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 >= l.r2) continue;
                const float k = 1 - d2 / l.r2;
                lr += l.r * k * k;
                lg += l.g * k * k;
                lb += l.b * k * k;
            }
        const float gh = terr.height(p.p.x, p.p.z);
        float rot = p.rot;
        if (p.mode == kFlash && (p.dir.x != 0 || p.dir.z != 0))  // muzzle star along the shot, as seen on screen
            rot = std::atan2(dot(p.dir, f.up), dot(p.dir, f.right));
        const float speed = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y + p.v.z * p.v.z);
        const float erode = p.erode0 + (p.erode1 - p.erode0) * t;
        // (Soot pales fastest while it is young: three quarters of the way to grey by half its life.)
        const float tc = p.mode == kPuff && p.soot > 0 ? 1 - (1 - t) * (1 - t) : t;
        float cr = p.r0 + (p.r1 - p.r0) * tc, cg = p.g0 + (p.g1 - p.g0) * tc, cb = p.b0 + (p.b1 - p.b0) * tc, soot = p.soot;
        float grain = p.scale;
        if (p.mode == kPuff) {
            if (p.gmix > 0) {  // the ground's dust in the foot of the smoke
                const float m = p.gmix * std::clamp(1.0f - (p.p.y - gh) / 60.0f, 0.0f, 1.0f);
                cr += (p.gr - cr) * m, cg += (p.gg - cg) * m, cb += (p.gb - cb) * m;
                soot *= 1 - 0.7f * m;
            }
            grain *= 0.86f + 0.3f * (p.seed * 7.31f - std::floor(p.seed * 7.31f));  // (no two puffs with the same grain)
        }
        const float vals[kFloats] = {p.p.x, p.p.y, p.p.z, size,
                                     p.v.x, p.v.y, p.v.z, rot,
                                     cr, cg, cb, a,
                                     er, eg, eb, float(p.mode),
                                     gh, speed * p.stretch, p.mode == kPuff ? size * p.soft : 0.0f, heat,
                                     lr, lg, lb, t,
                                     p.seed, grain, erode, p.mode == kPuff ? soot : p.rays,
                                     p.mode == kPuff ? p.dust : p.ch[0], p.ch[1], p.ch[2], p.ch[3]};
        // Ground marks: their corner heights at the size drawn now, on a grid where the ground is not flat
        // (a mark's own p.ch were measured at its final size; a ring grows into them).
        if (p.mode == kScorch || p.mode == kRing) mainCount_ += std::uint32_t(emitGroundMark(vals, terr) - 1);
        else inst_.insert(inst_.end(), vals, vals + kFloats);
    }
    drawn_ = texReady && !render::Effects::hideParticles() ? std::uint32_t(inst_.size() / kFloats) : 0;
    // D2K_FX_STATS=1: particles drawn and the screen pixels they cover (mean over 120 frames).
    static const bool stats = std::getenv("D2K_FX_STATS") != nullptr;
    if (stats) {
        static double px = 0, n = 0, pxHalf = 0, nHalf = 0;
        static int frames = 0;
        for (std::size_t k = 0; k < order_.size(); ++k) {
            const Part& p = parts_[std::size_t(order_[k].idx)];
            const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
            const float size = p.size0 + (p.size1 - p.size0) * (1 - (1 - t) * (1 - t));
            const float s = size * f.projScale / std::max(1.0f, dot(p.p - f.eye, f.forward));
            (k < mainOrders ? px : pxHalf) += double(s * s);
        }
        n += double(mainCount_);
        nHalf += double(halfCount_);
        if (++frames == 120) {
            SDL_Log("fx stats: %.0f + %.0f particles (scene pass + half), %.2f + %.2f Mpx quads (full-resolution pixels)", n / 120,
                    nHalf / 120, px / 120 / 1e6, pxHalf / 120 / 1e6);
            px = n = pxHalf = nHalf = 0;
            frames = 0;
        }
    }
    const bool needTex = !texUploaded_ && texReady;
    if (drawn_ == 0 && !needTex) return;
    if (drawn_ > cap_) {
        std::uint32_t cap = std::max<std::uint32_t>(1024, cap_);
        while (cap < drawn_) cap *= 2;
        if (vb_) SDL_ReleaseGPUBuffer(dev_, vb_);
        if (tb_) SDL_ReleaseGPUTransferBuffer(dev_, tb_);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = cap * kFloats * 4;
        vb_ = SDL_CreateGPUBuffer(dev_, &bi);
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = cap * kFloats * 4;
        tb_ = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        cap_ = (vb_ && tb_) ? cap : 0;
        if (!cap_) {
            drawn_ = 0;
            return;
        }
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBuffer* texTb = nullptr;
    if (needTex) {
        if (texThread_.joinable()) texThread_.join();
        texUploaded_ = true;
        const Uint32 bytes = Uint32(texCpu_.size() * 4);
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
        texTb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (texTb) {
            std::memcpy(SDL_MapGPUTransferBuffer(dev_, texTb, false), texCpu_.data(), bytes);
            SDL_UnmapGPUTransferBuffer(dev_, texTb);
            SDL_GPUTextureTransferInfo src{texTb, 0, kNoiseSize, kNoiseSize};
            SDL_GPUTextureRegion reg{};
            reg.texture = tex_;
            reg.w = reg.h = kNoiseSize;
            reg.d = kNoiseSize;
            SDL_UploadToGPUTexture(cp, &src, &reg, false);
        }
        texCpu_.clear();
        texCpu_.shrink_to_fit();
    }
    if (drawn_ > 0) {
        const Uint32 bytes = drawn_ * kFloats * 4;
        std::memcpy(SDL_MapGPUTransferBuffer(dev_, tb_, true), inst_.data(), bytes);
        SDL_UnmapGPUTransferBuffer(dev_, tb_);
        SDL_GPUTransferBufferLocation src{tb_, 0};
        SDL_GPUBufferRegion reg{vb_, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, true);
    }
    SDL_EndGPUCopyPass(cp);
    if (texTb) SDL_ReleaseGPUTransferBuffer(dev_, texTb);
}

namespace {
struct FxCompFU {
    float scale[4];  // full-resolution pixel -> half-resolution uv
};
}  // namespace

void CinematicFx::bindCommon(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f, SDL_GPUGraphicsPipeline* pipe) {
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    FxVU vu{};
    std::memcpy(vu.viewProj, f.viewProj.m, sizeof vu.viewProj);
    const float r[4] = {f.right.x, f.right.y, f.right.z, 0}, u[4] = {f.up.x, f.up.y, f.up.z, 0}, e[4] = {f.eye.x, f.eye.y, f.eye.z, 1};
    std::memcpy(vu.right, r, sizeof r);
    std::memcpy(vu.up, u, sizeof u);
    std::memcpy(vu.eye, e, sizeof e);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
    FxFU fu{{f.sun.x, f.sun.y, f.sun.z, 0},
            {f.sunCol[0], f.sunCol[1], f.sunCol[2], 1},
            {f.ambCol[0], f.ambCol[1], f.ambCol[2], 1},
            {f.fogColor[0], f.fogColor[1], f.fogColor[2], 1},
            {f.eye.x, f.eye.y, f.eye.z, f.fogStart},
            {f.fogEnd, f.hazeCut, 0, 0}};
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    SDL_GPUBufferBinding vb{vb_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUTextureSamplerBinding sb{tex_, samp_};
    SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
}

int CinematicFx::draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f) {
    if (drawn_ == 0 || !texUploaded_ || !vb_) return 0;
    // Without the half-resolution pass this frame both sets are drawn here.
    const std::uint32_t n = halfActive_ ? mainCount_ : drawn_;
    if (n == 0) return 0;
    bindCommon(pass, cmd, f, pipe_);
    SDL_DrawGPUPrimitives(pass, 6, n, 0, 0);
    return 1;
}

bool CinematicFx::ensureHalf(int w, int h, SDL_GPUTextureFormat depthFmt) {
    if (halfFailed_) return false;
    const int hw = std::max(1, (w + 1) / 2), hh = std::max(1, (h + 1) / 2);
    if (halfColor_ && hw == halfW_ && hh == halfH_ && depthFmt == halfDepthFmt_) return true;
    for (SDL_GPUTexture** t : {&halfColor_, &halfDepth_}) {
        if (*t) SDL_ReleaseGPUTexture(dev_, *t);
        *t = nullptr;
    }
    if (!halfPipe_ || depthFmt != halfDepthFmt_) {
        if (halfPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, halfPipe_);
        // Half-float colour (the fireballs are HDR) when the device renders it, else the scene's format.
        const SDL_GPUTextureUsageFlags use = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        halfFmt_ = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTURETYPE_2D, use)
                       ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
                       : colorFmt_;
        halfDepthFmt_ = depthFmt;
        halfPipe_ = makeFxPipe(dev_, halfFmt_, depthFmt, SDL_GPU_SAMPLECOUNT_1);
    }
    if (!compPipe_) {
        SDL_GPUShader* vs = createShader(dev_, ShaderId::FxcVs, 0, 0);
        SDL_GPUShader* fs = createShader(dev_, ShaderId::FxcFs, 1, 1);
        if (vs && fs) {
            SDL_GPUColorTargetDescription ctd{};
            ctd.format = colorFmt_;
            ctd.blend_state.enable_blend = true;
            ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;  // the scene keeps its alpha
            ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = vs;
            pi.fragment_shader = fs;
            pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pi.target_info.color_target_descriptions = &ctd;
            pi.target_info.num_color_targets = 1;
            compPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        }
        for (SDL_GPUShader* s : {vs, fs})
            if (s) SDL_ReleaseGPUShader(dev_, s);
    }
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = Uint32(hw);
    ci.height = Uint32(hh);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.format = halfFmt_;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    halfColor_ = halfPipe_ && compPipe_ ? SDL_CreateGPUTexture(dev_, &ci) : nullptr;
    ci.format = depthFmt;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    halfDepth_ = halfColor_ ? SDL_CreateGPUTexture(dev_, &ci) : nullptr;
    if (!halfColor_ || !halfDepth_) {
        SDL_Log("3D cinematic effects: half-resolution pass not available (%s) - particles drawn at full resolution",
                SDL_GetError());
        halfFailed_ = true;
        return false;
    }
    halfW_ = hw;
    halfH_ = hh;
    return true;
}

int CinematicFx::drawHalf(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Frame& f) {
    if (!halfActive_ || halfCount_ == 0 || !halfPipe_) return 0;
    bindCommon(pass, cmd, f, halfPipe_);
    SDL_DrawGPUPrimitives(pass, 6, halfCount_, 0, mainCount_);
    return 1;
}

void CinematicFx::composite(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, int rw, int rh) {
    if (!halfActive_ || !compPipe_ || !halfColor_) return;
    SDL_GPUColorTargetInfo cti{};
    cti.texture = target;
    cti.load_op = SDL_GPU_LOADOP_LOAD;
    cti.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &cti, 1, nullptr);
    if (!pass) return;
    const SDL_GPUViewport vpt{0, 0, float(rw), float(rh), 0, 1};
    SDL_SetGPUViewport(pass, &vpt);
    const SDL_Rect sc{0, 0, rw, rh};
    SDL_SetGPUScissor(pass, &sc);
    SDL_BindGPUGraphicsPipeline(pass, compPipe_);
    const FxCompFU u{{0.5f / float(halfW_), 0.5f / float(halfH_), 0, 0}};
    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
    SDL_GPUTextureSamplerBinding sb{halfColor_, linear_};
    SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

}  // namespace d2k::render3d
