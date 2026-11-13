#include "render3d/models.hpp"

#include "formats/ps1_disc.hpp"
#include "formats/ps1_models.hpp"
#include "game/world.hpp"
#include "render/art_pack.hpp"
#include "render/buildup_fx.hpp"
#include "render/scene_lights.hpp"
#include "render3d/gpu.hpp"
#include "render3d/model_anim.hpp"
#include "render3d/model_gltf.hpp"
#include "render3d/shader_blobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <map>
#include <thread>

namespace d2k::render3d {

namespace fs = std::filesystem;

namespace {

// pos3 nrm3 uv2 rgba4 params4 pbr2 + joints (4 bytes) + weights (4 bytes) in the last two floats
constexpr int kVertexFloats = 20;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kPoseFps = 24.0f;  // pose / frame-map times: frames at 24 fps (as Blender exports them)

// model_vs / model_shadow_vs (one block layout for both, as the HLSL twin's cbuffer).
struct ModelVU {
    float viewProj[16];
    float model[16];
    float sun[4] = {};    // model_shadow_vs: towards the sun
    float plane[4] = {};  // model_shadow_vs: ground plane
    float skin[4] = {-1, 0, 0, 0};  // x = first palette entry, < 0 = not skinned
};
using ShadowVU = ModelVU;

// The model vertex layout (both pipelines + the shadow depth pipeline).
struct VertexLayout {
    SDL_GPUVertexBufferDescription vbd{0, kVertexFloats * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    SDL_GPUVertexAttribute at[8] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
                                    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
                                    {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48},
                                    {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 64},
                                    {6, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 72},
                                    {7, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 76}};
    SDL_GPUVertexInputState state() const { return {&vbd, 1, at, 8}; }
};
struct ModelFU {
    float house[4];
    float sunDir[4];
    float light[4];
    float fogColor[4];
    float fog[4];
    float map[4];
    float lightVP[16];
    float shadow[4];
    float walk[4];      // walkable cells: x0, z0 of the footprint's top-left cell, cell mask, min height
    float walkMode[4];  // x: 0 everything, 1 all but the walkable cells' parts, 2 only them
};

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string underscored(std::string s) {
    for (char& c : s)
        if (c == ' ') c = '_';
    return s;
}

// models.ini: [NAME] scale= yaw= y= fit=contain|centre|off x= z= anchor.fx_NAME= x y z [size]
// (docs/3d-models.md "models.ini").
std::map<std::string, GltfAdjust> readAdjust(const fs::path& file) {
    std::map<std::string, GltfAdjust> out;
    std::ifstream in(file);
    std::string line, section;
    while (std::getline(in, line)) {
        if (const auto c = line.find_first_of(";#"); c != std::string::npos) line.erase(c);  // comments
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '[') {
            section = trim(line.substr(1, line.find(']') - 1));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        const float v = std::strtof(val.c_str(), nullptr);
        GltfAdjust& a = out[section];
        if (k == "scale") a.scale = v;
        else if (k == "yaw") a.yawDeg = v;
        else if (k == "y") a.y = v;
        else if (k == "x") a.x = v;
        else if (k == "z") a.z = v;
        else if (k == "shrink") a.shrink = v != 0;
        else if (k == "fit") {
            a.fit = val == "off" || val == "none" ? GltfAdjust::Fit::Off
                    : val == "centre" || val == "center" ? GltfAdjust::Fit::Centre
                                                         : GltfAdjust::Fit::Contain;
        } else if (k.rfind("anchor.", 0) == 0 && k.size() > 7) {
            GltfAdjust::Anchor an;
            an.name = k.substr(7);
            for (char& c : an.name) c = char(std::tolower(static_cast<unsigned char>(c)));
            const int n = std::sscanf(val.c_str(), "%f %f %f %f", &an.pos[0], &an.pos[1], &an.pos[2], &an.size);
            if (n >= 3) a.anchors.push_back(an);
            else SDL_Log("3D models: models.ini [%s] %s: wants x y z [size]", section.c_str(), k.c_str());
        }
    }
    return out;
}

// Ground plan of a model (x / z bounds of its body, turret and house parts), world px.
bool planBounds(const formats::Model3D& m, float& x0, float& z0, float& x1, float& z1) {
    x0 = z0 = 1e30f;
    x1 = z1 = -1e30f;
    for (const auto* part : {&m.body, &m.turret, &m.house})
        for (const auto& v : part->tris) {
            x0 = std::min(x0, v.pos[0]);
            x1 = std::max(x1, v.pos[0]);
            z0 = std::min(z0, v.pos[2]);
            z1 = std::max(z1, v.pos[2]);
        }
    return x1 >= x0;
}

// The centre of a building's occupied cells (tilesOccupiedAll): the glTF building models' origin,
// in px from the template's top-left; and the occupied cells' bounding box.
void footprintOrigin(const formats::BuildingTemplate& t, float& ox, float& oz, int& cx0, int& cy0, int& cx1, int& cy1) {
    const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
    int n = 0, sx = 0, sy = 0;
    cx0 = cy0 = 4;
    cx1 = cy1 = -1;
    for (int k = 0; k < 16; ++k)
        if (t.tilesOccupiedAll & (1u << k)) {
            ++n, sx += k % 4, sy += k / 4;
            cx0 = std::min(cx0, k % 4);
            cy0 = std::min(cy0, k / 4);
            cx1 = std::max(cx1, k % 4);
            cy1 = std::max(cy1, k / 4);
        }
    if (!n) {
        cx0 = cy0 = 0;
        cx1 = fw - 1;
        cy1 = fh - 1;
    }
    ox = n ? (float(sx) / float(n) + 0.5f) * 32 : float(fw) * 16;
    oz = n ? (float(sy) / float(n) + 0.5f) * 32 : float(fh) * 16;
}

// Moves a building model so that it stands on its template's occupied cells (docs/3d-models.md
// "Placement"): the redesigned models are authored so that their 45-degree 2D render lands on the
// original sprite, which can put parts of them outside the footprint in 3D (the refinery's drums
// stand ~0.4 tiles north of its cells). Contain / Centre also scale a model down that would still
// reach past the cells' box or into one of its unoccupied cells (GltfAdjust::shrink), so adjacent
// buildings never intersect. Returns the shift (px, of the plan centre) and the scale.
void fitToFootprint(formats::Model3D& m, const formats::BuildingTemplate& t, const GltfAdjust& a, float& dx, float& dz,
                    float& scale) {
    dx = a.x * 32;
    dz = a.z * 32;
    scale = 1;
    float cx = 0, cz = 0;  // the scale's centre (plan centre)
    float x0, z0, x1, z1;
    if (a.fit != GltfAdjust::Fit::Off && planBounds(m, x0, z0, x1, z1)) {
        float ox, oz;
        int cx0, cy0, cx1, cy1;
        footprintOrigin(t, ox, oz, cx0, cy0, cx1, cy1);
        // The cells' box relative to the origin.
        const float f[4] = {float(cx0 * 32) - ox, float((cx1 + 1) * 32) - ox, float(cy0 * 32) - oz, float((cy1 + 1) * 32) - oz};
        constexpr float kSlack = 0.5f;  // px: overhangs below half a pixel stay
        auto axis = [&](float lo, float hi, float f0, float f1) {
            if (a.fit == GltfAdjust::Fit::Centre || hi - lo > f1 - f0) return 0.5f * (f0 + f1) - 0.5f * (lo + hi);
            if (lo < f0 - kSlack) return f0 - lo;
            if (hi > f1 + kSlack) return f1 - hi;
            return 0.0f;
        };
        cx = 0.5f * (x0 + x1);
        cz = 0.5f * (z0 + z1);
        if (a.shrink) {
            // Parts more than kLow px above the ground must stay out of the unoccupied cells of the
            // box (deeper than kTol: they may touch the edge), and nothing may stick out of the box
            // by more than kTol. The largest scale (steps of 0.5 %) that achieves both.
            constexpr float kTol = 1.0f, kLow = 2.0f;
            std::uint32_t freeMask = 0;
            for (int y = cy0; y <= cy1; ++y)
                for (int x = cx0; x <= cx1; ++x)
                    if (!(t.tilesOccupiedAll & (1u << (y * 4 + x)))) freeMask |= 1u << (y * 4 + x);
            std::vector<float> tall;  // x, y, z of the parts that must keep out of free cells
            if (freeMask)
                for (const auto* part : {&m.body, &m.turret, &m.house})
                    for (const auto& v : part->tris)
                        if (v.pos[1] > kLow) tall.insert(tall.end(), {v.pos[0], v.pos[1], v.pos[2]});
            auto fits = [&](float s) {
                const float lx = cx + s * (x0 - cx), hx = cx + s * (x1 - cx), lz = cz + s * (z0 - cz), hz = cz + s * (z1 - cz);
                if (hx - lx > f[1] - f[0] + 2 * kTol || hz - lz > f[3] - f[2] + 2 * kTol) return false;
                const float sx = axis(lx, hx, f[0], f[1]), sz = axis(lz, hz, f[2], f[3]);
                for (std::size_t i = 0; i + 2 < tall.size(); i += 3) {
                    if (tall[i + 1] * s <= kLow) continue;
                    const float px = cx + s * (tall[i] - cx) + sx + ox, pz = cz + s * (tall[i + 2] - cz) + sz + oz;  // art box px
                    const int gx = int(std::floor(px / 32)), gz = int(std::floor(pz / 32));
                    if (gx < 0 || gz < 0 || gx > 3 || gz > 3 || !(freeMask & (1u << (gz * 4 + gx)))) continue;
                    const float ix = px - float(gx * 32), iz = pz - float(gz * 32);
                    if (std::min({ix, 32 - ix, iz, 32 - iz}) > kTol) return false;
                }
                return true;
            };
            while (scale > 0.6f && !fits(scale)) scale -= 0.005f;
            scale = std::max(scale, 0.6f);
        }
        dx += axis(cx + scale * (x0 - cx), cx + scale * (x1 - cx), f[0], f[1]);
        dz += axis(cz + scale * (z0 - cz), cz + scale * (z1 - cz), f[2], f[3]);
    }
    if (dx == 0 && dz == 0 && scale == 1) return;
    // p' = c + s (p - c) + d in plan, y' = s y: one similarity for the vertices, the poses, the
    // skeleton's place matrix (the palette follows the moved vertices), the turret pivot and anchors.
    const float s = scale, tx = cx * (1 - s) + dx, tz = cz * (1 - s) + dz;
    auto apply = [&](float* p) {
        p[0] = s * p[0] + tx;
        p[1] = s * p[1];
        p[2] = s * p[2] + tz;
    };
    for (auto* part : {&m.body, &m.turret, &m.shadow, &m.house})
        for (auto& v : part->tris) apply(v.pos);
    for (auto& part : m.bodyPoses)
        for (auto& v : part.tris) apply(v.pos);
    float* pl = m.skeleton.place;  // place = S * place (column-major: rows 0..2 of every column)
    for (int c = 0; c < 4; ++c) {
        pl[c * 4 + 0] = s * pl[c * 4 + 0] + tx * pl[c * 4 + 3];
        pl[c * 4 + 1] = s * pl[c * 4 + 1];
        pl[c * 4 + 2] = s * pl[c * 4 + 2] + tz * pl[c * 4 + 3];
    }
    apply(m.turretPivot);
    for (auto& an : m.anchors) apply(an.pos);
}

// D2K_MODEL_FIT_REPORT=1: per building model, how far its geometry reaches past its occupied cells
// (px, per side; "tall" = only parts more than 2 px above the ground) and the height of anything
// standing on a cell of the art box the building does not occupy.
void footprintReport(const formats::Model3D& m, const formats::BuildingTemplate& t, const std::string& key) {
    float ox, oz;
    int cx0, cy0, cx1, cy1;
    footprintOrigin(t, ox, oz, cx0, cy0, cx1, cy1);
    const float f[4] = {float(cx0 * 32) - ox, float((cx1 + 1) * 32) - ox, float(cy0 * 32) - oz, float((cy1 + 1) * 32) - oz};
    float all[4] = {0, 0, 0, 0}, tall[4] = {0, 0, 0, 0};  // W E N S
    float freeTop = 0;
    for (const auto* part : {&m.body, &m.turret, &m.house})
        for (const auto& v : part->tris) {
            const float o[4] = {f[0] - v.pos[0], v.pos[0] - f[1], f[2] - v.pos[2], v.pos[2] - f[3]};
            for (int k = 0; k < 4; ++k) {
                all[k] = std::max(all[k], o[k]);
                if (v.pos[1] > 2) tall[k] = std::max(tall[k], o[k]);
            }
            const int cx = int(std::floor((v.pos[0] + ox) / 32)), cy = int(std::floor((v.pos[2] + oz) / 32));
            if (cx >= cx0 && cy >= cy0 && cx <= cx1 && cy <= cy1 && !(t.tilesOccupiedAll & (1u << (cy * 4 + cx))) && v.pos[1] > 2) {
                const float ix = v.pos[0] + ox - float(cx * 32), iz = v.pos[2] + oz - float(cy * 32);
                freeTop = std::max(freeTop, std::min({ix, 32 - ix, iz, 32 - iz}));
            }
        }
    SDL_Log("3D models: fit %-28s cells %dx%d mask %04x solid %04x  over W/E/N/S %5.1f %5.1f %5.1f %5.1f  tall %5.1f %5.1f %5.1f %5.1f  free-cell depth %.1f",
            key.c_str(), cx1 - cx0 + 1, cy1 - cy0 + 1, unsigned(t.tilesOccupiedAll), unsigned(t.tilesOccupiedSolid), double(all[0]), double(all[1]), double(all[2]),
            double(all[3]), double(tall[0]), double(tall[1]), double(tall[2]), double(tall[3]), double(freeTop));
}

// Column-major helpers (math3d.hpp layout).
Mat4 translate(float x, float y, float z) {
    Mat4 m;
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}
// Turn by `a` radians clockwise as seen from above (x east, z south): north -> east.
Mat4 yawM(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 m;
    m.m[0] = c;   // x' = c x - s z
    m.m[2] = s;   // z' = s x + c z
    m.m[8] = -s;
    m.m[10] = c;
    return m;
}
Mat4 scaleY(float k) {
    Mat4 m;
    m.m[5] = k;
    return m;
}
// Squashed by k towards the height y0 (a building's shadow caster during its build-up).
Mat4 squashAbove(float y0, float k) {
    if (k >= 1) return Mat4{};
    return translate(0, y0, 0) * scaleY(k) * translate(0, -y0, 0);
}
// Rotation taking +y to the unit vector n (ground slope).
Mat4 tiltTo(const Vec3& n) {
    const Vec3 up{0, 1, 0};
    const Vec3 ax = cross(up, n);
    const float s = length(ax), c = n.y;
    Mat4 m;
    if (s < 1e-5f) return m;
    const Vec3 k = ax * (1.0f / s);
    const float t = 1 - c;
    // Rodrigues, column-major: m[col*4 + row].
    m.m[0] = c + k.x * k.x * t;
    m.m[1] = k.y * k.x * t + k.z * s;
    m.m[2] = k.z * k.x * t - k.y * s;
    m.m[4] = k.x * k.y * t - k.z * s;
    m.m[5] = c + k.y * k.y * t;
    m.m[6] = k.z * k.y * t + k.x * s;
    m.m[8] = k.x * k.z * t + k.y * s;
    m.m[9] = k.y * k.z * t - k.x * s;
    m.m[10] = c + k.z * k.z * t;
    return m;
}
float facingAngle(int facing) { return float(facing & 31) * (kTwoPi / 32); }

}  // namespace

ModelRenderer::ModelRenderer(const formats::Templates& t, const formats::HouseColours& colours)
    : t_(t), colours_(colours), unitModel_(formats::kMaxUnitTypes, -1), buildingModel_(formats::kMaxBuildingTypes, -1) {
    // "Modern animation" (not a setting yet): infantry poses blend between the original frames.
    // Off = the original's steps (one pose per 2D frame).
    if (const char* e = std::getenv("D2K_MODERN_ANIM")) smoothAnim_ = e[0] == '1';
    // D2K_ANIM_OFF=1: every model in its rest pose (A/B measurements of the skinning cost).
    if (const char* e = std::getenv("D2K_ANIM_OFF")) animOff_ = e[0] == '1';
}

ModelRenderer::~ModelRenderer() {
    releaseGpu();
    if (dev_) {
        if (meshPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, meshPipe_);
        if (shadowPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, shadowPipe_);
        if (projShadowPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, projShadowPipe_);
        if (depthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, depthPipe_);
        if (walkPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, walkPipe_);
        if (nearest_) SDL_ReleaseGPUSampler(dev_, nearest_);
        if (linear_) SDL_ReleaseGPUSampler(dev_, linear_);
        if (white_) SDL_ReleaseGPUTexture(dev_, white_);
    }
}

void ModelRenderer::releaseGpu() {
    if (!dev_) return;
    for (auto& g : gpu_)
        if (g.tex) SDL_ReleaseGPUTexture(dev_, g.tex);
    gpu_.clear();
    if (vb_) SDL_ReleaseGPUBuffer(dev_, vb_);
    vb_ = nullptr;
    if (palette_) SDL_ReleaseGPUBuffer(dev_, palette_);
    if (paletteTb_) SDL_ReleaseGPUTransferBuffer(dev_, paletteTb_);
    palette_ = nullptr;
    paletteTb_ = nullptr;
    paletteCap_ = 0;
    uploaded_ = false;
}

int ModelRenderer::unitModels() const {
    return int(std::count_if(unitModel_.begin(), unitModel_.end(), [](int i) { return i >= 0; }));
}
int ModelRenderer::buildingModels() const {
    return int(std::count_if(buildingModel_.begin(), buildingModel_.end(), [](int i) { return i >= 0; }));
}
bool ModelRenderer::hasUnitModel(int type) const {
    return active() && type >= 0 && type < int(unitModel_.size()) && unitModel_[std::size_t(type)] >= 0;
}
bool ModelRenderer::hasBuildingModel(int type) const {
    return active() && type >= 0 && type < int(buildingModel_.size()) && buildingModel_[std::size_t(type)] >= 0;
}

// The built-in model pack "new": assets/models (our own procedural models, tools/models/), found
// next to the executable / in the .app's Resources (SDL_GetBasePath and up to 5 parents) or under
// the working directory. Layout: unit/*.glb, building/*.glb, models.ini.
static std::string builtInModelsDir() {
    auto ok = [](const fs::path& p) {
        std::error_code ec;
        return fs::is_directory(p / "unit", ec) || fs::is_directory(p / "building", ec);
    };
    if (const char* base = SDL_GetBasePath()) {
        fs::path p(base);
        for (int up = 0; up < 6 && !p.empty(); ++up) {
            if (ok(p / "assets" / "models")) return (p / "assets" / "models").string();
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    std::error_code ec;
    const fs::path c = fs::current_path(ec) / "assets" / "models";
    return ok(c) ? c.string() : std::string();
}

float ModelRenderer::unitModelHeight(int type) const {
    if (!hasUnitModel(type)) return -1;
    const int m = unitModel_[std::size_t(type)];
    return m < int(modelTop_.size()) ? modelTop_[std::size_t(m)] : -1;
}
float ModelRenderer::buildingModelHeight(int type) const {
    if (!hasBuildingModel(type)) return -1;
    const int m = buildingModel_[std::size_t(type)];
    return m < int(modelTop_.size()) ? modelTop_[std::size_t(m)] : -1;
}

std::vector<std::string> ModelRenderer::availablePacks() {
    std::vector<std::string> names;
    const bool builtIn = !builtInModelsDir().empty();
    for (const auto& d : render::ArtPack::searchDirs()) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(d, ec))
            if (e.is_directory(ec) && fs::is_directory(e.path() / "models", ec)) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (builtIn && std::find(names.begin(), names.end(), kBuiltInPack) == names.end())
        names.insert(names.begin(), kBuiltInPack);
    return names;
}

std::string ModelRenderer::packPath(const std::string& name) {
    for (const auto& d : render::ArtPack::searchDirs()) {
        std::error_code ec;
        if (!name.empty() && fs::is_directory(fs::path(d) / name / "models", ec)) return (fs::path(d) / name).string();
    }
    if (name == kBuiltInPack) return builtInModelsDir();
    if (name.empty()) {  // the OpenD2K 3D art pack (models + redesigned frames) before the built-in models
        if (const std::string full = render::ArtPack::threeDPack(); !full.empty()) return packPath(full);
        const auto all = availablePacks();
        if (!all.empty()) return packPath(all.front());
    }
    return {};
}

bool ModelRenderer::load(Source s, const std::string& ps1Image, const std::string& packDir) {
    releaseGpu();
    models_.clear();
    modelTop_.clear();
    decks_.clear();
    anim_.clear();
    poseCache_.clear();
    frameSlots_.clear();
    std::fill(unitModel_.begin(), unitModel_.end(), -1);
    std::fill(buildingModel_.begin(), buildingModel_.end(), -1);
    source_ = s;
    if (s == Source::Sprites) return true;
    std::vector<std::string> unitKey(formats::kMaxUnitTypes), bldName(formats::kMaxBuildingTypes),
        bldGroup(formats::kMaxBuildingTypes);
    for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
        const int g = t_.bin.units[i].unitGroup;
        if (g >= 0 && g < formats::kMaxUnitTypes) unitKey[std::size_t(i)] = t_.bin.unitGroupNames[g];
    }
    for (int i = 0; i < formats::kMaxBuildingTypes; ++i) {
        bldName[std::size_t(i)] = t_.bin.buildingNames[i];
        const int g = t_.bin.buildings[i].buildingGroup;
        if (g >= 0 && g < formats::kMaxBuildingTypes) bldGroup[std::size_t(i)] = t_.bin.buildingGroupNames[g];
    }
    // A community campaign's Templates.bin (setModelKeys): the matching base type's name / group;
    // both empty = its own art, no model.
    std::vector<std::string> unitName(formats::kMaxUnitTypes);
    for (int i = 0; i < formats::kMaxUnitTypes; ++i) unitName[std::size_t(i)] = t_.bin.unitNames[i];
    int keyed = 0, own = 0;
    for (std::size_t i = 0; i < keyUnitName_.size() && i < keyUnitGroup_.size() && i < unitName.size(); ++i) {
        keyed += (keyUnitName_[i] != unitName[i] || keyUnitGroup_[i] != unitKey[i]) && i < t_.bin.unitCount;
        own += keyUnitName_[i].empty() && keyUnitGroup_[i].empty() && i < t_.bin.unitCount;
        unitName[i] = keyUnitName_[i];
        unitKey[i] = keyUnitGroup_[i];
    }
    for (std::size_t i = 0; i < keyBldName_.size() && i < keyBldGroup_.size() && i < bldName.size(); ++i) {
        keyed += (keyBldName_[i] != bldName[i] || keyBldGroup_[i] != bldGroup[i]) && i < t_.bin.buildingCount;
        own += keyBldName_[i].empty() && keyBldGroup_[i].empty() && i < t_.bin.buildingCount;
        bldName[i] = keyBldName_[i];
        bldGroup[i] = keyBldGroup_[i];
    }
    if (keyed)
        SDL_Log("3D models: campaign templates - %d types looked up by their base type, %d with the campaign's own art (no model)",
                keyed - own, own);
    if (s == Source::Ps1) {
        formats::ps1::Disc disc;
        std::string err;
        if (ps1Image.empty() || !disc.open(ps1Image)) {
            SDL_Log("3D models: no PS1 disc image (%s)", ps1Image.empty() ? "not set" : disc.error().c_str());
            return false;
        }
        if (!formats::ps1::loadModels(disc, models_, err)) {
            SDL_Log("3D models: PS1 disc: %s", err.c_str());
            models_.clear();
            return false;
        }
        auto find = [&](const std::string& n) {
            for (std::size_t i = 0; i < models_.size(); ++i)
                if (models_[i].name == n) return int(i);
            return -1;
        };
        for (int i = 0; i < formats::kMaxUnitTypes; ++i)
            if (!unitKey[std::size_t(i)].empty()) unitModel_[std::size_t(i)] = find(formats::ps1::modelForUnit(unitKey[std::size_t(i)]));
        for (int i = 0; i < formats::kMaxBuildingTypes; ++i)
            if (!bldGroup[std::size_t(i)].empty())
                buildingModel_[std::size_t(i)] = find(formats::ps1::modelForBuilding(bldName[std::size_t(i)], bldGroup[std::size_t(i)]));
    } else {
        std::error_code ec0;  // art packs: <pack>/models/...; the built-in pack: assets/models/...
        const fs::path root = fs::is_directory(fs::path(packDir) / "models", ec0) ? fs::path(packDir) / "models" : fs::path(packDir);
        const auto adjust = readAdjust(fs::path(packDir) / "models.ini");
        std::map<std::string, int> loaded;  // file -> model index
        // Reading a .glb (parse + PNG texture decode) is most of the load time (seconds for a full pack):
        // every file the lookups below can ask for is read up front on worker threads, then taken in
        // the same order as before, so the result is the same as reading them one by one.
        struct Read {
            bool ok = false;
            formats::Model3D m;
            std::string err;
        };
        std::map<std::string, Read> pre;
        {
            std::vector<std::pair<std::string, std::string>> want;  // (file, adjust key)
            auto add = [&](const char* kind, const std::string& key) {
                if (key.empty()) return;
                const fs::path f = root / kind / (key + ".glb");
                std::error_code ec;
                if (fs::exists(f, ec) && !pre.count(f.string())) {
                    pre[f.string()];
                    want.emplace_back(f.string(), key);
                }
            };
            for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
                add("unit", underscored(t_.bin.unitNames[i]));
                add("unit", unitKey[std::size_t(i)]);
            }
            for (int i = 0; i < formats::kMaxBuildingTypes; ++i) {
                add("building", underscored(bldName[std::size_t(i)]));
                add("building", bldGroup[std::size_t(i)]);
            }
            std::atomic<std::size_t> next{0};
            auto worker = [&] {
                for (std::size_t j; (j = next.fetch_add(1)) < want.size();) {
                    Read& r = pre[want[j].first];  // the map is not modified any more: lookups only
                    const auto a = adjust.find(want[j].second);
                    r.ok = loadGltfModel(want[j].first, a != adjust.end() ? a->second : GltfAdjust{}, r.m, r.err);
                }
            };
            const unsigned n = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
            std::vector<std::thread> pool;
            for (unsigned t = 1; t < n && t < want.size(); ++t) pool.emplace_back(worker);
            worker();
            for (auto& t : pool) t.join();
        }
        auto get = [&](const char* kind, const std::string& key) -> int {
            if (key.empty()) return -1;
            const fs::path f = root / kind / (key + ".glb");
            std::error_code ec;
            if (!fs::exists(f, ec)) return -1;
            if (auto it = loaded.find(f.string()); it != loaded.end()) return it->second;
            formats::Model3D m;
            std::string err;
            const auto a = adjust.find(key);
            bool ok = false;
            if (auto p = pre.find(f.string()); p != pre.end() && (p->second.ok || !p->second.err.empty())) {
                ok = p->second.ok;
                m = std::move(p->second.m);
                err = std::move(p->second.err);
                pre.erase(p);
            } else {
                ok = loadGltfModel(f.string(), a != adjust.end() ? a->second : GltfAdjust{}, m, err);
            }
            if (!ok) {
                SDL_Log("3D models: %s", err.c_str());
                return loaded[f.string()] = -1;
            }
            m.name = key;
            models_.push_back(std::move(m));
            const int mi = int(models_.size()) - 1;
            anim_.resize(models_.size());
            // Animated infantry: the frame map next to the glb (docs/art-pipeline.md "Animated models").
            const fs::path fm = root / kind / (key + ".anim.json");
            if (fs::exists(fm, ec) && !models_.back().skeleton.empty()) {
                AnimInfo& ai = anim_.back();
                if (loadFrameMap(fm.string(), ai.frameMap, err)) {
                    int missing = 0;
                    for (const auto& e : ai.frameMap.frames) {
                        const int ia = e.anim.empty() ? -1 : models_.back().skeleton.find(e.anim);
                        if (!e.anim.empty() && ia < 0) ++missing;
                        ai.frameAnim.push_back(ia);
                    }
                    SDL_Log("3D models: %s: frame map, %zu frames, %d glTF animations%s", key.c_str(), ai.frameMap.frames.size(),
                            int(models_.back().skeleton.animations.size()),
                            missing ? " (some frames name a missing animation: rest pose)" : "");
                } else {
                    SDL_Log("3D models: %s", err.c_str());
                }
            }
            loadVariants(mi, packDir, key, f.string(), a != adjust.end() ? a->second : GltfAdjust{});
            return loaded[f.string()] = mi;
        };
        for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
            int m = get("unit", underscored(unitName[std::size_t(i)]));
            if (m < 0) m = get("unit", unitKey[std::size_t(i)]);
            unitModel_[std::size_t(i)] = m;
        }
        std::vector<bool> fitted;
        for (int i = 0; i < formats::kMaxBuildingTypes; ++i) {
            std::string key = underscored(bldName[std::size_t(i)]);
            int m = get("building", key);
            if (m < 0) m = get("building", key = bldGroup[std::size_t(i)]);
            buildingModel_[std::size_t(i)] = m;
            // On its cells: fitted once per model, to the first template that uses it (house
            // variants share the footprint).
            if (m < 0) continue;
            if (fitted.size() <= std::size_t(m)) fitted.resize(std::size_t(m) + 1, false);
            if (fitted[std::size_t(m)]) continue;
            fitted[std::size_t(m)] = true;
            const auto ai = adjust.find(key);
            float dx = 0, dz = 0, sc = 1;
            fitToFootprint(models_[std::size_t(m)], t_.bin.buildings[i], ai != adjust.end() ? ai->second : GltfAdjust{}, dx, dz, sc);
            if (dx != 0 || dz != 0 || sc != 1)
                SDL_Log("3D models: %s placed on its cells: moved %.1f px east, %.1f px south, scaled %.3f", key.c_str(), double(dx),
                        double(dz), double(sc));
            if (const char* e = std::getenv("D2K_MODEL_FIT_REPORT"); e && e[0] == '1')
                footprintReport(models_[std::size_t(m)], t_.bin.buildings[i], key);
        }
    }
    anim_.resize(models_.size());
    // Docking harvesters: where the unloading pipe ends (anchor fx_nozzle, else the rear-most point
    // of the last pose: the pipe fully out).
    dockTip_.assign(models_.size(), DockTip{});
    for (int i = 0; i < formats::kMaxUnitTypes; ++i) {
        const int mi = unitModel_[std::size_t(i)];
        if (mi < 0 || t_.bin.units[i].specialBehavior != 1) continue;
        const formats::Model3D& m = models_[std::size_t(mi)];
        DockTip& d = dockTip_[std::size_t(mi)];
        for (const auto& an : m.anchors)
            if (an.name.rfind("fx_nozzle", 0) == 0) d = DockTip{true, an.pos[0], an.pos[1], an.pos[2]};
        if (d.valid) continue;
        for (std::size_t k = m.bodyPoses.size(); k-- > 0 && !d.valid;) {
            float best = -1e30f;
            for (const auto& v : m.bodyPoses[k].tris) best = std::max(best, v.pos[2]);
            double sx = 0, sy = 0;
            int n = 0;
            for (const auto& v : m.bodyPoses[k].tris)
                if (v.pos[2] > best - 0.5f) sx += v.pos[0], sy += v.pos[1], ++n;
            if (n) d = DockTip{true, float(sx / n), float(sy / n), best};  // the centre of the pipe's mouth
        }
    }
    decks_.clear();
    for (const int m : buildingModel_)
        if (m >= 0 && (std::size_t(m) >= decks_.size() || decks_[std::size_t(m)].w == 0)) buildDeck(m);
    modelTop_.clear();
    for (const auto& m : models_) {
        float top = 0;
        for (const auto* part : {&m.body, &m.turret, &m.house})
            for (const auto& v : part->tris) top = std::max(top, v.pos[1]);
        modelTop_.push_back(top);
    }
    SDL_Log("3D models: %s, %d models, %d unit types, %d building types", s == Source::Ps1 ? "PS1 disc" : packDir.c_str(),
            int(models_.size()), unitModels(), buildingModels());
    return !models_.empty();
}

// Pose variants of a model (docs/3d-models.md "Animation"): <pack>/variants/KEY_poseNN.glb (the
// building generators, tools/models/atreides_kit.py), KEY_unloadNN.glb (the harvester's unload
// poses, tools/models/harvester_v1.py) and KEY_rowNN.glb (unit art animation row NN, e.g. the
// ornithopter's wing beat, tools/models/atreides_ornithopter.py), NN = the pose. A variant whose
// nodes moved becomes key NN of the model's "pose" animation (STEP, NN / 24 s); one whose nodes are
// unchanged but whose meshes differ (the harvester: one merged body) becomes a body mesh variant
// (Model3D::bodyPoses). Optional <pack>/variants/KEY.spec.json or spec.json (with "key": KEY):
// which pose each building overlay frame / silo fill level / unit art row shows (else pose =
// frame - 1, frame 0 = rest; rows: pose = row).
void ModelRenderer::loadVariants(int mi, const std::string& packDir, const std::string& key, const std::string& glbPath,
                                 const GltfAdjust& adj) {
    formats::Model3D& m = models_[std::size_t(mi)];
    AnimInfo& ai = anim_[std::size_t(mi)];
    std::error_code ec;
    fs::path vdir = fs::path(packDir) / "variants";
    if (!fs::is_directory(vdir, ec)) vdir = fs::path(packDir) / "models" / "variants";
    if (!fs::is_directory(vdir, ec)) return;
    for (const char* spec : {"KEY.spec.json", "spec.json"}) {
        const std::string name = std::string(spec) == "spec.json" ? spec : key + ".spec.json";
        if (loadPoseSpec((vdir / name).string(), key, ai.animFrames, ai.levels, ai.rowPoses)) break;
    }
    std::map<int, fs::path> files;
    bool rowFiles = false;
    for (const auto& e : fs::directory_iterator(vdir, ec)) {
        const std::string n = e.path().filename().string();
        for (const char* tag : {"_pose", "_unload", "_row"}) {
            const std::string pre = key + tag;
            if (n.size() == pre.size() + 6 && n.compare(0, pre.size(), pre) == 0 && n.compare(n.size() - 4, 4, ".glb") == 0 &&
                std::isdigit(static_cast<unsigned char>(n[pre.size()])) && std::isdigit(static_cast<unsigned char>(n[pre.size() + 1]))) {
                files[std::atoi(n.c_str() + pre.size())] = e.path();
                rowFiles |= std::string_view(tag) == "_row";
            }
        }
    }
    if (files.empty()) return;
    ai.rows = rowFiles || !ai.rowPoses.empty();
    std::vector<std::string> names;
    if (!loadGltfNodeNames(glbPath, names) || names.size() != m.skeleton.nodes.size()) return;
    auto& sk = m.skeleton;
    const int count = files.rbegin()->first + 1;
    std::vector<std::vector<float>> nodePose(static_cast<std::size_t>(count));  // 10 floats per node, empty = rest
    std::vector<char> moved(sk.nodes.size(), 0);
    int nodeVariants = 0, meshVariants = 0;
    for (const auto& [k, path] : files) {
        std::vector<float> trs;
        std::string err;
        if (!loadGltfNodeTrs(path.string(), names, trs, err)) {
            SDL_Log("3D models: %s", err.c_str());
            continue;
        }
        bool any = false;
        for (std::size_t n = 0; n < sk.nodes.size(); ++n) {
            const auto& r = sk.nodes[n];
            const float rest[10] = {r.t[0], r.t[1], r.t[2], r.r[0], r.r[1], r.r[2], r.r[3], r.s[0], r.s[1], r.s[2]};
            for (int c = 0; c < 10; ++c)
                if (std::abs(trs[n * 10 + std::size_t(c)] - rest[c]) > 1e-5f) {
                    moved[n] = 1;
                    any = true;
                    break;
                }
        }
        if (any) {
            nodePose[std::size_t(k)] = std::move(trs);
            ++nodeVariants;
            continue;
        }
        GltfAdjust ga = adj;
        ga.geometryOnly = true;
        formats::Model3D v;
        if (!loadGltfModel(path.string(), ga, v, err)) {
            SDL_Log("3D models: %s", err.c_str());
            continue;
        }
        if (m.bodyPoses.size() < std::size_t(count)) m.bodyPoses.resize(std::size_t(count));
        m.bodyPoses[std::size_t(k)] = std::move(v.body);
        ++meshVariants;
    }
    if (nodeVariants) {
        formats::Model3D::Skeleton::Animation an;
        an.name = "pose";
        an.duration = float(count - 1) / kPoseFps;
        for (std::size_t n = 0; n < sk.nodes.size(); ++n) {
            if (!moved[n]) continue;
            const auto& r = sk.nodes[n];
            const float rest[10] = {r.t[0], r.t[1], r.t[2], r.r[0], r.r[1], r.r[2], r.r[3], r.s[0], r.s[1], r.s[2]};
            for (std::uint8_t path = 0; path < 3; ++path) {
                formats::Model3D::Skeleton::Channel ch;
                ch.node = int(n);
                ch.path = path;
                ch.step = true;
                const int off = path == 0 ? 0 : path == 1 ? 3 : 7, nc = path == 1 ? 4 : 3;
                for (int k = 0; k < count; ++k) {
                    ch.times.push_back(float(k) / kPoseFps);
                    const auto& p = nodePose[std::size_t(k)];
                    for (int c = 0; c < nc; ++c) ch.values.push_back(p.empty() ? rest[off + c] : p[n * 10 + std::size_t(off + c)]);
                }
                an.channels.push_back(std::move(ch));
            }
        }
        sk.animations.push_back(std::move(an));
        ai.poseAnim = int(sk.animations.size()) - 1;
    }
    ai.poseCount = count;
    SDL_Log("3D models: %s: %d pose variants (%d node poses, %d mesh poses)%s%s", key.c_str(), int(files.size()), nodeVariants,
            meshVariants, ai.rows ? ", unit art rows" : "",
            ai.animFrames.empty() && ai.levels.empty() && ai.rowPoses.empty() ? "" : ", pose spec");
}

// The pose of a unit's model, chosen from the same state as its 2D frame (MapRenderer::drawUnit):
// infantry: the frame map entry of art frame animFrame * dirs + direction column (the direction-
// less deaths / fidgets: column animType - 9); the harvester docking / unloading at a refinery
// (animFrame != 0): pose animFrame (its EX_HARV frame); a unit whose art has animation rows (the
// ornithopter's wing beat, World::flyUnit = FlyUnit 0x494fd0: rows 0-1-2 every 5 ticks, row 2
// held, back): the pose of row animFrame (2D frame = first + animFrame * dirs + direction,
// re/notes/rendering.md §2).
ModelRenderer::UnitPose ModelRenderer::unitPose(const game::Unit& u, int type, int mi) const {
    UnitPose p;
    if (mi < 0 || std::size_t(mi) >= anim_.size() || type < 0 || type >= formats::kMaxUnitTypes) return p;
    const AnimInfo& ai = anim_[std::size_t(mi)];
    const auto& tmpl = t_.bin.units[type];
    if (!ai.frameMap.frames.empty() && tmpl.unitArt >= 0 && tmpl.unitArt < formats::kMaxUnitArt) {
        const int dirs = std::max(1, int(t_.bin.unitArtDirectionFrames[tmpl.unitArt]));
        int dir = tmpl.directionFrames[u.facing & 31];
        if (u.animType > 8) dir = u.animType - 9;
        const int key = u.animFrame * dirs + dir;
        if (key >= 0 && std::size_t(key) < ai.frameMap.frames.size() && !ai.frameMap.frames[std::size_t(key)].anim.empty()) {
            const FrameMapEntry& e = ai.frameMap.frames[std::size_t(key)];
            p.framed = true;
            p.anim = ai.frameAnim[std::size_t(key)];
            float frame = e.frame;
            if (smoothAnim_) {  // towards the next original frame of the same strip (3 ticks per frame)
                const int next = (u.animFrame + 1) * dirs + dir;
                if (std::size_t(next) < ai.frameMap.frames.size() && ai.frameMap.frames[std::size_t(next)].anim == e.anim &&
                    ai.frameMap.frames[std::size_t(next)].frame == e.frame + 1)
                    frame += float(std::min<int>(u.animDelay, 2)) / 3.0f;
            }
            p.time = frame / kPoseFps;
            p.yaw = e.yaw;
            p.shiftE = e.shiftE;
            p.shiftN = e.shiftN;
            p.scale = e.scale;
            p.alpha = e.alpha;
        }
        return p;
    }
    auto pose = [&](int k) {
        if (k < 0 || k >= ai.poseCount) return;
        if (ai.poseAnim >= 0) {
            p.anim = ai.poseAnim;
            p.time = float(k) / kPoseFps;
        }
        const formats::Model3D& m = models_[std::size_t(mi)];
        if (std::size_t(k) < m.bodyPoses.size() && !m.bodyPoses[std::size_t(k)].empty()) p.bodyPose = k;
    };
    if (tmpl.specialBehavior == 1) {
        if (u.animFrame != 0) pose(u.animFrame);
    } else if (ai.rows) {
        const int row = u.animFrame;
        pose(ai.rowPoses.empty() ? row : (std::size_t(row) < ai.rowPoses.size() ? ai.rowPoses[std::size_t(row)] : -1));
    }
    return p;
}

// The pose of a building's model: the overlay frame its 2D drawing would show (MapRenderer::
// drawBuilding: flags HAS_ANIMATION + permanent / once, not damaged, not building up; frame =
// animCounter / animationSpeed) or a silo's fill level, through the pose spec (default frame - 1).
int ModelRenderer::buildingPose(const game::World& w, const game::Building& b, int mi) const {
    if (mi < 0 || std::size_t(mi) >= anim_.size()) return -1;
    const AnimInfo& ai = anim_[std::size_t(mi)];
    if (ai.poseCount <= 0 || (b.flags & 0x6)) return -1;
    const auto& t = t_.bin.buildings[b.type];
    auto pick = [&](const std::vector<int>& map, int k) {
        const int p = map.empty() ? k - 1 : (k >= 0 && std::size_t(k) < map.size() ? map[std::size_t(k)] : -1);
        return p >= 0 && p < ai.poseCount ? p : -1;
    };
    if ((b.flags & 0x30) && (b.flags & 0x40) && b.health * 2 >= std::int32_t(t.hitPoints)) {
        const int a = t.buildingAnimation;
        if (a >= 0 && a < formats::kMaxBuildingTypes) {
            const int k = b.animCounter / std::max(1, int(t_.bin.buildings[a].animationSpeed));
            if (k < t_.bin.buildingAnimationFrames[a]) return pick(ai.animFrames, k);
        }
        return -1;
    }
    if (t.specialBehavior == 14 || t.specialBehavior == 16) {
        // Walls and wall-linking turrets: the wall-link frame of MapRenderer::drawBuilding (the walls / linking
        // turrets on the four neighbouring cells), through the spec's "levels" (frame -> pose; the wall model
        // tools/models/wall.py: the arms shown, the turrets' wall stubs).
        auto links = [&](int x, int y) {
            if (!w.inBounds(x, y)) return false;
            const auto& c = w.cell(x, y);
            if (c.buildingSide < 0) return false;
            const auto& o = w.sides()[std::size_t(c.buildingSide)].buildings[std::size_t(c.buildingIndex)];
            if (o.tileX != x || o.tileY != y || o.type < 0 || o.type >= formats::kMaxBuildingTypes) return false;
            const int sb = t_.bin.buildings[o.type].specialBehavior;
            return sb == 14 || sb == 16;
        };
        static constexpr int kWallFrameMapping[16] = {0, 2, 3, 12, 1, 6, 11, 7, 4, 13, 5, 9, 15, 10, 8, 14};
        const int m = (links(b.tileX - 1, b.tileY) ? 1 : 0) | (links(b.tileX, b.tileY - 1) ? 2 : 0) |
                      (links(b.tileX + 1, b.tileY) ? 4 : 0) | (links(b.tileX, b.tileY + 1) ? 8 : 0);
        return ai.levels.empty() ? -1 : pick(ai.levels, kWallFrameMapping[m]);
    }
    if (t.specialBehavior == 5) {  // silo: min(spice * 4500 / capacity / 1500, N - 1), as the 2D main frame
        const auto& sd = w.sides()[std::size_t(b.side)];
        const int n = t.buildingArt >= 0 && t.buildingArt < formats::kMaxBuildingArt ? int(t_.bin.buildingArtDirections[t.buildingArt]) : 1;
        if (sd.maxStorage <= 0) return pick(ai.levels, 0);
        const auto lvl = std::uint64_t(std::uint32_t(sd.spice)) * 4500u / std::uint32_t(sd.maxStorage) / 1500u;
        return pick(ai.levels, int(std::min<std::uint64_t>(lvl, std::uint64_t(std::max(1, n) - 1))));
    }
    return -1;
}

int ModelRenderer::paletteFor(int mi, int anim, float time) {
    const formats::Model3D& m = models_[std::size_t(mi)];
    if (anim < 0 || m.skeleton.empty() || animOff_) return -1;
    const std::uint64_t key = (std::uint64_t(std::uint32_t(mi)) << 40) | (std::uint64_t(std::uint32_t(anim) & 0xFFFFF) << 20) |
                              std::uint64_t(std::uint32_t(std::lround(std::max(0.0f, time) * kPoseFps * 64.0f)) & 0xFFFFF);
    if (const auto it = frameSlots_.find(key); it != frameSlots_.end()) return it->second;
    auto it = poseCache_.find(key);
    if (it == poseCache_.end()) {
        std::vector<float> pal;
        posePalette(m.skeleton, anim, time, pal);
        it = poseCache_.emplace(key, std::move(pal)).first;
    }
    const int base = int(paletteData_.size() / 16);
    paletteData_.insert(paletteData_.end(), it->second.begin(), it->second.end());
    frameSlots_.emplace(key, base);
    return base;
}

// This frame's palette -> vertex storage buffer 0 (grown as needed; at least one matrix so the
// binding is always valid).
void ModelRenderer::uploadPalette(SDL_GPUCommandBuffer* cmd) {
    if (paletteData_.empty()) {
        static const float kId[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        paletteData_.assign(kId, kId + 16);
    }
    const std::uint32_t n = std::uint32_t(paletteData_.size() / 16);
    if (n > paletteCap_ || !palette_ || !paletteTb_) {
        if (palette_) SDL_ReleaseGPUBuffer(dev_, palette_);
        if (paletteTb_) SDL_ReleaseGPUTransferBuffer(dev_, paletteTb_);
        paletteCap_ = std::max<std::uint32_t>(256, n + n / 2);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bi.size = paletteCap_ * 64;
        palette_ = SDL_CreateGPUBuffer(dev_, &bi);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = paletteCap_ * 64;
        paletteTb_ = SDL_CreateGPUTransferBuffer(dev_, &ti);
        if (!palette_ || !paletteTb_) {
            SDL_Log("3D models: joint palette buffer failed: %s", SDL_GetError());
            paletteCap_ = 0;
            return;
        }
    }
    void* p = SDL_MapGPUTransferBuffer(dev_, paletteTb_, true);
    if (!p) return;
    std::memcpy(p, paletteData_.data(), std::size_t(n) * 64);
    SDL_UnmapGPUTransferBuffer(dev_, paletteTb_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{paletteTb_, 0};
    SDL_GPUBufferRegion reg{palette_, 0, n * 64};
    SDL_UploadToGPUBuffer(cp, &src, &reg, true);
    SDL_EndGPUCopyPass(cp);
}

bool ModelRenderer::ensurePipelines(SDL_GPUDevice* dev, const ModelFrame& f) {
    if (pipelineFailed_) return false;
    if (meshPipe_ && dev == dev_ && f.colorFormat == colorFmt_ && f.depthFormat == depthFmt_ && f.samples == samples_ &&
        f.aniso == aniso_)
        return true;
    if (dev_ && dev != dev_) return false;  // one device per app
    dev_ = dev;
    if (meshPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, meshPipe_);
    if (shadowPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, shadowPipe_);
    if (projShadowPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, projShadowPipe_);
    if (depthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, depthPipe_);
    if (walkPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, walkPipe_);
    meshPipe_ = shadowPipe_ = projShadowPipe_ = depthPipe_ = walkPipe_ = nullptr;
    depthPipeFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    colorFmt_ = f.colorFormat;
    depthFmt_ = f.depthFormat;
    samples_ = f.samples;
    SDL_GPUShader* vs = createShader(dev_, ShaderId::ModelVs, 0, 1, 1);
    SDL_GPUShader* fsM = createShader(dev_, ShaderId::ModelFs, 3, 2);
    SDL_GPUShader* fsS = createShader(dev_, ShaderId::ShadowFs, 3, 1);
    SDL_GPUShader* vsS = createShader(dev_, ShaderId::ModelShadowVs, 0, 1, 1);
    if (vs && fsM && fsS && vsS) {
        SDL_GPUColorTargetDescription ctd{};
        ctd.format = colorFmt_;
        ctd.blend_state.enable_blend = true;
        ctd.blend_state.color_blend_op = ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fsM;
        const VertexLayout layout;
        pi.vertex_input_state = layout.state();
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
        pi.multisample_state.sample_count = samples_;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depthFmt_;
        pi.target_info.has_depth_stencil_target = true;
        // Walkable cells (docs/3d-models.md "Walkable cells"): every mesh writes its stencil
        // reference where it is drawn (units on a building's walkable cells: that building's, all
        // else 0); walkPipe_ then draws those buildings' parts over the walkable cells except where
        // their own units are in front.
        // D2K_WALK_OFF=1: off (A/B screenshots).
        static const bool walkOff = [] {
            const char* e = std::getenv("D2K_WALK_OFF");
            return e && e[0] == '1';
        }();
        const bool stencil = !walkOff && (depthFmt_ == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ||
                                          depthFmt_ == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT);
        if (stencil) {
            SDL_GPUStencilOpState st{};
            st.fail_op = st.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
            st.pass_op = SDL_GPU_STENCILOP_REPLACE;
            st.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            pi.depth_stencil_state.enable_stencil_test = true;
            pi.depth_stencil_state.front_stencil_state = pi.depth_stencil_state.back_stencil_state = st;
            pi.depth_stencil_state.compare_mask = pi.depth_stencil_state.write_mask = 0xFF;
        }
        meshPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (stencil) {
            SDL_GPUStencilOpState st{};
            st.fail_op = st.depth_fail_op = st.pass_op = SDL_GPU_STENCILOP_KEEP;
            st.compare_op = SDL_GPU_COMPAREOP_NOT_EQUAL;
            pi.depth_stencil_state.front_stencil_state = pi.depth_stencil_state.back_stencil_state = st;
            pi.depth_stencil_state.write_mask = 0;
            walkPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
            if (!walkPipe_) SDL_Log("3D models: walkable-cell pipeline failed: %s", SDL_GetError());
            pi.depth_stencil_state.enable_stencil_test = false;
            pi.depth_stencil_state.compare_mask = pi.depth_stencil_state.write_mask = 0;
        }
        pi.fragment_shader = fsS;
        pi.depth_stencil_state.enable_depth_write = false;
        shadowPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        // Projected shadows: depth-written and LESS, so overlapping shadow triangles (same plane)
        // darken the ground once.
        pi.vertex_shader = vsS;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        projShadowPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!meshPipe_ || !shadowPipe_ || !projShadowPipe_) SDL_Log("3D models: pipeline failed: %s", SDL_GetError());
        SDL_Log("3D models: walkable cells %s", walkPipe_ ? "on (stencil)" : "off");
    }
    for (SDL_GPUShader* s : {vs, fsM, fsS, vsS})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (linear_ && f.aniso != aniso_) {  // a new anisotropy (Quality): a new sampler
        SDL_ReleaseGPUSampler(dev_, linear_);
        SDL_ReleaseGPUSampler(dev_, nearest_);
        linear_ = nearest_ = nullptr;
    }
    aniso_ = f.aniso;
    if (!nearest_) {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nearest_ = SDL_CreateGPUSampler(dev_, &si);
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.address_mode_u = si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.enable_anisotropy = gpuAnisotropy() && f.aniso > 1;  // Quality::aniso
        si.max_anisotropy = float(std::max(1, f.aniso));
        linear_ = SDL_CreateGPUSampler(dev_, &si);
    }
    if (!meshPipe_ || !shadowPipe_ || !projShadowPipe_ || !nearest_ || !linear_) {
        pipelineFailed_ = true;
        return false;
    }
    return true;
}

void ModelRenderer::prepare(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) {
    draws_.clear();
    if (!active() || !ensurePipelines(dev, f)) return;
    if (!uploaded_) {
        uploaded_ = true;
        std::vector<float> v;
        auto put = [&](const formats::MeshPart& p) {
            Range r{std::uint32_t(v.size() / kVertexFloats), std::uint32_t(p.tris.size())};
            for (const auto& x : p.tris) {
                float f16[kVertexFloats] = {x.pos[0], x.pos[1], x.pos[2], x.nrm[0], x.nrm[1], x.nrm[2], x.uv[0], x.uv[1],
                                            x.rgba[0], x.rgba[1], x.rgba[2], x.rgba[3], x.house, x.lit, x.textured, x.oneSided,
                                            x.gloss, x.metal, 0, 0};
                std::memcpy(&f16[18], x.joint, 4);   // UBYTE4_NORM joints
                std::memcpy(&f16[19], x.weight, 4);  // UBYTE4_NORM weights
                v.insert(v.end(), f16, f16 + kVertexFloats);
            }
            return r;
        };
        struct TexUp { SDL_GPUTexture* tex; const std::vector<std::uint32_t>* px; int w, h; };
        std::vector<TexUp> texUps;
        auto makeTex = [&](int w, int h) {
            SDL_GPUTextureCreateInfo ci{};
            ci.type = SDL_GPU_TEXTURETYPE_2D;
            ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ci.width = Uint32(w);
            ci.height = Uint32(h);
            ci.layer_count_or_depth = 1;
            ci.num_levels = 1;
            return SDL_CreateGPUTexture(dev_, &ci);
        };
        static const std::vector<std::uint32_t> kWhite{0xFFFFFFFFu};
        if (!white_) {
            white_ = makeTex(1, 1);
            if (white_) texUps.push_back({white_, &kWhite, 1, 1});
        }
        for (const auto& m : models_) {
            GpuModel g;
            g.anchor = m.anchor;
            g.body = put(m.body);
            g.turret = put(m.turret);
            g.shadow = put(m.shadow);
            g.house = put(m.house);
            for (const auto& bp : m.bodyPoses) g.bodyPoses.push_back(put(bp));
            std::memcpy(g.pivot, m.turretPivot, sizeof g.pivot);
            g.point = m.pointSample;
            g.houseOnTurret = m.houseOnTurret;
            g.projectShadow = m.shadow.empty();
            g.emissive = m.emissiveInAlpha;
            for (const formats::MeshPart* part : {&m.body, &m.turret, &m.house})
                for (const auto& vx : part->tris) g.top = std::max(g.top, vx.pos[1]);
            if (!m.texture.empty() && m.texW > 0 && m.texH > 0) {
                g.tex = makeTex(m.texW, m.texH);
                if (g.tex) texUps.push_back({g.tex, &m.texture, m.texW, m.texH});
            }
            gpu_.push_back(g);
        }
        const Uint32 vbytes = Uint32(v.size() * 4);
        if (vbytes > 0) {
            SDL_GPUBufferCreateInfo bi{};
            bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bi.size = vbytes;
            vb_ = SDL_CreateGPUBuffer(dev_, &bi);
        }
        // Staged in transfer buffers of at most kChunk bytes (a bigger texture gets its own): the
        // whole pack in one (~1.3 GB for opend2k-redesign) exceeds Direct3D 12's resource size
        // limit on cards with less VRAM. Texture offsets 512-aligned (D3D12 placement alignment).
        constexpr Uint32 kChunk = 64u << 20;
        struct Up { const std::uint8_t* data; Uint32 size; Uint32 vbOff; const TexUp* tex; };
        std::vector<Up> ups;
        if (vb_) {
            const auto* vp = reinterpret_cast<const std::uint8_t*>(v.data());
            for (Uint32 o = 0; o < vbytes; o += kChunk) ups.push_back({vp + o, std::min(kChunk, vbytes - o), o, nullptr});
            for (const auto& t : texUps)
                ups.push_back({reinterpret_cast<const std::uint8_t*>(t.px->data()), Uint32(t.w * t.h * 4), 0, &t});
        }
        auto align = [](Uint32 x) { return (x + 511u) & ~511u; };
        SDL_GPUCopyPass* cp = ups.empty() ? nullptr : SDL_BeginGPUCopyPass(cmd);
        for (std::size_t i = 0; i < ups.size();) {
            std::size_t j = i;
            Uint32 size = 0;
            while (j < ups.size() && (j == i || align(size) + ups[j].size <= kChunk)) size = align(size) + ups[j++].size;
            SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &ti);
            auto* p = tb ? static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, tb, false)) : nullptr;
            if (p) {
                Uint32 off = 0;
                for (std::size_t k = i; k < j; off += ups[k++].size) {
                    off = align(off);
                    std::memcpy(p + off, ups[k].data, ups[k].size);
                }
                SDL_UnmapGPUTransferBuffer(dev_, tb);
                off = 0;
                for (std::size_t k = i; k < j; off += ups[k++].size) {
                    off = align(off);
                    if (const TexUp* t = ups[k].tex) {
                        SDL_GPUTextureTransferInfo tsrc{tb, off, Uint32(t->w), Uint32(t->h)};
                        SDL_GPUTextureRegion treg{};
                        treg.texture = t->tex;
                        treg.w = Uint32(t->w);
                        treg.h = Uint32(t->h);
                        treg.d = 1;
                        SDL_UploadToGPUTexture(cp, &tsrc, &treg, false);
                    } else {
                        SDL_GPUTransferBufferLocation src{tb, off};
                        SDL_GPUBufferRegion reg{vb_, ups[k].vbOff, ups[k].size};
                        SDL_UploadToGPUBuffer(cp, &src, &reg, false);
                    }
                }
            } else {
                SDL_Log("3D models: upload buffer of %u bytes failed: %s", size, SDL_GetError());
            }
            if (tb) SDL_ReleaseGPUTransferBuffer(dev_, tb);
            i = j;
        }
        if (cp) SDL_EndGPUCopyPass(cp);
        if (!vb_) gpu_.clear();
    }
    if (!vb_) return;
    paletteData_.clear();
    frameSlots_.clear();
    const Uint64 t0 = SDL_GetTicksNS();
    const std::size_t cached = poseCache_.size();
    collect(f);
    uploadPalette(cmd);
    // D2K_ANIM_STATS=1: animation cost, logged every 120 frames (docs/3d-models.md "Animation").
    static const bool stats = [] {
        const char* e = std::getenv("D2K_ANIM_STATS");
        return e && e[0] == '1';
    }();
    if (stats) {
        static double ns = 0;
        static long frames = 0, posed = 0, drawsN = 0, mats = 0, misses = 0;
        ns += double(SDL_GetTicksNS() - t0);
        ++frames;
        drawsN += long(draws_.size());
        for (const Draw& d : draws_) posed += d.palette >= 0 || d.bodyPose >= 0;
        mats += long(paletteData_.size() / 16);
        misses += long(poseCache_.size() - cached);
        if (frames == 120) {
            SDL_Log("3D models: %.1f models drawn, %.1f posed, %.1f palette matrices (%.1f KB) per frame; %zu poses cached "
                    "(%.2f new per frame); collect + palette %.3f ms per frame",
                    double(drawsN) / 120, double(posed) / 120, double(mats) / 120, double(mats) / 120 * 64 / 1024, poseCache_.size(),
                    double(misses) / 120, ns / 120 / 1e6);
            ns = 0;
            frames = posed = drawsN = mats = misses = 0;
        }
    }
}

bool ModelRenderer::unitPlacement(const Terrain& terr, const game::World& w, const game::Unit& u, int type, float height,
                                  bool flying, Mat4& body, Mat4& turret, Mat4& tilt, Vec3& at) const {
    if (type < 0 || type >= int(unitModel_.size())) return false;
    const int mi = unitModel_[std::size_t(type)];
    if (mi < 0 || mi >= int(models_.size())) return false;
    float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
    // On a building's deck (a refinery's dock pad, a repair pad): on top of it, not inside it. A
    // docking harvester backs onto the refinery's inlet (dockOffset); the deck stays its dock cell's.
    const int cellX = int(std::floor(gx / 32)), cellY = int(std::floor(gy / 32));
    if (float ox, oz; !flying && dockOffset(w, u, mi, gx, gy, ox, oz)) gx += ox, gy += oz;
    const float ground = terr.height(gx, gy) + (flying || !active() ? 0.0f : deckAt(w, gx, gy, cellX, cellY));
    at = Vec3{gx, ground + height, gy};
    // Ground vehicles lean with the terrain slope.
    tilt = Mat4{};
    if (!flying) {
        const float e = 8;
        const Vec3 n = normalize(Vec3{terr.height(gx - e, gy) - terr.height(gx + e, gy), 2 * e,
                                      terr.height(gx, gy - e) - terr.height(gx, gy + e)});
        tilt = tiltTo(n);
    }
    body = translate(at.x, at.y, at.z) * tilt * yawM(facingAngle(u.facing));
    if (const UnitPose p = unitPose(u, type, mi); p.framed) {
        // A frame-map frame (infantry): its own turn, shift and size, as the 2D frame draws the
        // figure; upright (the original's soldiers do not lean with the ground).
        Mat4 s;
        s.m[0] = s.m[5] = s.m[10] = p.scale;
        tilt = Mat4{};
        body = translate(at.x + p.shiftE, at.y, at.z - p.shiftN) * yawM(radians(p.yaw)) * s;
    }
    const formats::Model3D& m = models_[std::size_t(mi)];
    const auto& tmpl = t_.bin.units[type];
    const int tf = tmpl.barrelArt >= 0 ? u.facingTurret : u.facing;
    turret = body * translate(m.turretPivot[0], m.turretPivot[1], m.turretPivot[2]) * yawM(facingAngle(tf - u.facing)) *
             translate(-m.turretPivot[0], -m.turretPivot[1], -m.turretPivot[2]);
    return true;
}

void ModelRenderer::addUnit(const ModelFrame& f, const game::World& w, int sideIdx, const game::Unit& u, int type,
                            float height, bool flying) {
    if (type < 0 || type >= int(unitModel_.size())) return;
    const int mi = unitModel_[std::size_t(type)];
    if (mi < 0 || mi >= int(gpu_.size())) return;
    Draw d;
    d.model = mi;
    d.alpha = 1;
    if (u.flags & 0x4) {
        if (u.side != f.playerSide) return;
        d.alpha = 0.5f;
    } else if (u.stealthTimer != 0) {
        d.alpha = 0.5f;
    }
    d.flash = (u.flicker & 2) ? 0.43f : 0.0f;
    Mat4 tilt;
    Vec3 at;
    if (!unitPlacement(*f.terrain, w, u, type, height, flying, d.bodyM, d.turretM, tilt, at)) return;
    const float ground = at.y - height, gx = at.x, gy = at.z;  // (a docking harvester: shifted)
    float sx, sy;
    if (!f.camera->project(at, sx, sy)) return;
    const float margin = 400;
    if (sx < -margin || sy < -margin || sx > f.camera->viewW() + margin || sy > f.camera->viewH() + margin) return;
    const GpuModel& g = gpu_[std::size_t(mi)];
    d.turret = g.turret.count > 0;
    if (const UnitPose p = unitPose(u, type, mi); p.anim >= 0 || p.bodyPose >= 0 || p.framed) {
        d.palette = paletteFor(mi, p.anim, p.time);
        if (p.bodyPose >= 0 && std::size_t(p.bodyPose) < g.bodyPoses.size() && g.bodyPoses[std::size_t(p.bodyPose)].count)
            d.bodyPose = p.bodyPose;
        d.alpha *= p.alpha;
    }
    d.shadowM = translate(gx, ground + 0.6f, gy) * tilt * yawM(facingAngle(u.facing));
    {  // projected shadow plane: the ground under the unit (sloped like the unit; flyers: level)
        Vec3 n{0, 1, 0};
        if (!flying) n = Vec3{tilt.m[4], tilt.m[5], tilt.m[6]};
        d.plane[0] = n.x;
        d.plane[1] = n.y;
        d.plane[2] = n.z;
        d.plane[3] = n.x * gx + n.y * ground + n.z * gy;
    }
    const int house = w.sides()[std::size_t(sideIdx)].house;
    const formats::Rgba c = colours_[std::size_t(std::clamp(house, 0, formats::kHouseCount - 1))][8];
    d.house[0] = float(c & 0xFF) / 255.0f;
    d.house[1] = float((c >> 8) & 0xFF) / 255.0f;
    d.house[2] = float((c >> 16) & 0xFF) / 255.0f;
    draws_.push_back(d);
}

// Where a building's model stands (docs/3d-models.md "Placement"): the origin by the model's
// anchor (glTF: the centre of the occupied cells, the model already moved onto them at load), on
// the levelled foundation of its cells (Terrain::updateFoundations; the highest ground of the art
// box before the terrain has levelled a new building), lifted 0.75 px against z-fighting of flat
// parts (build-up / selling: model_fs cuts it, render/buildup_fx.hpp).
bool ModelRenderer::buildingPlacement(const Terrain& terr, const game::Building& b, Mat4& body, Mat4& turret,
                                      float& groundY) const {
    if (!b.alive || b.type < 0 || b.type >= int(buildingModel_.size())) return false;
    const int mi = buildingModel_[std::size_t(b.type)];
    if (mi < 0 || mi >= int(models_.size())) return false;
    const formats::Model3D& m = models_[std::size_t(mi)];
    const auto& t = t_.bin.buildings[b.type];
    const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
    float ox, oz, cox, coz;
    int cx0, cy0, cx1, cy1;
    footprintOrigin(t, cox, coz, cx0, cy0, cx1, cy1);
    if (m.anchor == formats::Model3D::Anchor::ArtTopLeft) {
        ox = float(b.tileX * 32);
        oz = float(b.tileY * 32);
    } else if (m.anchor == formats::Model3D::Anchor::ArtBottomLeft) {
        ox = float(b.tileX * 32);
        oz = float((b.tileY + fh) * 32);
    } else {  // centre of the occupied cells
        ox = float(b.tileX * 32) + cox;
        oz = float(b.tileY * 32) + coz;
    }
    groundY = terr.foundationAt(b.tileX + cx0, b.tileY + cy0,
                                terr.maxHeight(b.tileX, b.tileY, b.tileX + fw - 1, b.tileY + fh - 1));
    // (build-up / selling: the whole model stands; the draw cuts it, render/buildup_fx.hpp)
    body = translate(ox, groundY + 0.75f, oz);
    const bool turns = t.barrelArt >= 0 || t.actLikeTurret;
    // PS1 turret heads rest facing north (PS1 +z), as facing 0.
    const float a = turns ? facingAngle(b.facing) : 0.0f;
    turret = body * translate(m.turretPivot[0], m.turretPivot[1], m.turretPivot[2]) * yawM(a) *
             translate(-m.turretPivot[0], -m.turretPivot[1], -m.turretPivot[2]);
    return true;
}

// The deck of a building model under (wx, wy): the highest of its surfaces at most Deck::kCap px
// above its ground near there (a pad, a dock, a platform), as extra height above the terrain (the
// levelled foundation under the building). 0 = no building model / no deck there.
float ModelRenderer::groundOffset(const game::World& w, float wx, float wy) const {
    if (!active()) return 0;
    return deckAt(w, wx, wy, int(std::floor(wx / 32)), int(std::floor(wy / 32)));
}

// Walkable cells (docs/3d-models.md "Walkable cells"): the footprint cells units drive over (the
// front row of most buildings, the refinery's dock and back corner, the repair pad's corners).
std::uint32_t ModelRenderer::walkMask(int type) const {
    if (type < 0 || type >= int(buildingModel_.size()) || buildingModel_[std::size_t(type)] < 0) return 0;
    const auto& bt = t_.bin.buildings[type];
    return bt.tilesOccupiedAll & ~bt.tilesOccupiedSolid & 0xFFFFu;
}

// A unit billboard (types without a model) on a walkable cell of a building model: pulled 14 px
// towards the eye, about the depth of the model's parts over the cell in front of it.
float ModelRenderer::walkableBias(const game::World& w, float wx, float wy) const {
    if (!active() || !walkPipe_) return 0;
    const int cx = int(std::floor(wx / 32)), cy = int(std::floor(wy / 32));
    for (const auto& side : w.sides())
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const game::Building& b = side.buildings[std::size_t(bi)];
            if (!b.alive) continue;
            const int dx = cx - b.tileX, dy = cy - b.tileY;
            if (dx < 0 || dy < 0 || dx > 3 || dy > 3) continue;
            if (walkMask(b.type) & (1u << (dy * 4 + dx))) return 14.0f;
        }
    return 0;
}

// Only where the original game has units standing on a building (its 2D drawing puts them over
// the building's base frame): the refinery's dock cell (exit point 1: World::refineryDock), every
// cell of a repair pad, the exit cell of a light / heavy factory (the cells spawn.cpp marks
// kExitTile). A unit merely crossing the walkable front row of another building, or infantry
// walking into a building to capture it, stays on the ground.
float ModelRenderer::deckAt(const game::World& w, float wx, float wy, int cx, int cy) const {
    for (const auto& side : w.sides())
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const game::Building& b = side.buildings[std::size_t(bi)];
            if (!b.alive || b.type < 0 || b.type >= int(buildingModel_.size())) continue;
            const int dx = cx - b.tileX, dy = cy - b.tileY;
            if (dx < 0 || dy < 0 || dx > 3 || dy > 3) continue;
            const auto& bt = t_.bin.buildings[b.type];
            if (!(bt.tilesOccupiedAll & (1u << (dy * 4 + dx)))) continue;
            namespace bb = game::building_behaviour;
            const bool deck = (bt.specialBehavior == bb::kRefinery && dx == bt.exitPoint1X && dy == bt.exitPoint1Y) ||
                              bt.specialBehavior == bb::kRepairPad ||
                              (bt.specialBehavior == bb::kHeavyFactory && dx == 0 && dy == 2) ||
                              (bt.specialBehavior == bb::kLightFactory && dx == 2 && dy == 1);
            if (!deck) return 0;
            const int mi = buildingModel_[std::size_t(b.type)];
            if (mi < 0 || mi >= int(decks_.size()) || decks_[std::size_t(mi)].y.empty()) return 0;
            const Deck& dk = decks_[std::size_t(mi)];
            Mat4 body, turret;
            float groundY = 0;
            static const Terrain kFlat;  // only the origin and the growth are needed here
            if (!buildingPlacement(kFlat, b, body, turret, groundY)) return 0;
            const float grow = body.m[5];
            const float mx = wx - body.m[12], mz = wy - body.m[14];
            float best = -1;
            for (const float o : {0.0f, -5.0f, 5.0f})
                for (const float p : {0.0f, -5.0f, 5.0f}) {
                    const int ix = int(std::floor((mx + o - dk.x0) / Deck::kRes));
                    const int iz = int(std::floor((mz + p - dk.z0) / Deck::kRes));
                    if (ix < 0 || iz < 0 || ix >= dk.w || iz >= dk.h) continue;
                    best = std::max(best, dk.y[std::size_t(iz) * std::size_t(dk.w) + std::size_t(ix)]);
                }
            return best > 0 ? 0.75f + best * grow : 0.0f;
        }
    return 0;
}

// A harvester docking / unloading at its refinery (states 14-16: animFrame 1..10 on the dock cell,
// facing 12; MapRenderer::drawUnit draws its EX_HARV frames at its own position): the redesigned
// refineries and the harvester were matched in the 45-degree 2D picture, where a point further
// north and lower lands on the same pixel as one further south and higher; in 3D the pipe of a
// harvester on the dock cell's centre ends ~15 px north of the inlet. So the model backs onto the
// inlet: the plan shift from the pipe's nozzle (final pose) to the refinery model's fx_inlet anchor,
// eased in over the first 7 docking frames (the pipe telescopes out in frames 8-10).
bool ModelRenderer::dockOffset(const game::World& w, const game::Unit& u, int mi, float gx, float gy, float& ox, float& oz) const {
    ox = oz = 0;
    if (u.animFrame == 0 || mi < 0 || std::size_t(mi) >= dockTip_.size() || !dockTip_[std::size_t(mi)].valid) return false;
    if (u.side >= w.sides().size()) return false;
    const auto& sd = w.sides()[u.side];
    if (u.refineryIndex < 0 || u.refineryIndex >= int(sd.buildings.size())) return false;
    const game::Building& b = sd.buildings[std::size_t(u.refineryIndex)];
    if (!b.alive || b.type < 0 || b.type >= int(buildingModel_.size())) return false;
    const auto& bt = t_.bin.buildings[b.type];
    if (bt.specialBehavior != game::building_behaviour::kRefinery) return false;
    if (u.blockFromX != b.tileX + bt.exitPoint1X || u.blockFromY != b.tileY + bt.exitPoint1Y) return false;
    const int bm = buildingModel_[std::size_t(b.type)];
    if (bm < 0) return false;
    const formats::Model3D* rm = &models_[std::size_t(bm)];
    const formats::Model3D::AnchorPoint* inlet = nullptr;
    for (const auto& an : rm->anchors)
        if (an.name.rfind("fx_inlet", 0) == 0) {
            inlet = &an;
            break;
        }
    if (!inlet) return false;
    Mat4 body, turret;
    float groundY = 0;
    static const Terrain kFlat;
    if (!buildingPlacement(kFlat, b, body, turret, groundY)) return false;
    const Vec4 target = body * Vec4{inlet->pos[0], inlet->pos[1], inlet->pos[2], 1};
    const DockTip& tip = dockTip_[std::size_t(mi)];
    const float a = facingAngle(u.facing), c = std::cos(a), s = std::sin(a);
    const float nx = gx + c * tip.x - s * tip.z, nz = gy + s * tip.x + c * tip.z;
    float dx = target.x - nx, dz = target.z - nz;
    const float len = std::sqrt(dx * dx + dz * dz);
    constexpr float kMax = 32;  // px: a refinery model without a matching inlet does not drag it away
    if (len > kMax) return false;
    const float k = std::min(1.0f, float(u.animFrame) / 7.0f);
    ox = dx * k;
    oz = dz * k;
    return true;
}

void ModelRenderer::buildDeck(int mi) {
    if (decks_.size() <= std::size_t(mi)) decks_.resize(std::size_t(mi) + 1);
    Deck& dk = decks_[std::size_t(mi)];
    dk = Deck{};
    const formats::Model3D& m = models_[std::size_t(mi)];
    float x0, z0, x1, z1;
    if (!planBounds(m, x0, z0, x1, z1)) return;
    dk.x0 = std::floor(x0);
    dk.z0 = std::floor(z0);
    dk.w = int(std::ceil((x1 - dk.x0) / Deck::kRes)) + 1;
    dk.h = int(std::ceil((z1 - dk.z0) / Deck::kRes)) + 1;
    if (dk.w <= 0 || dk.h <= 0 || dk.w * dk.h > (1 << 20)) {
        dk = Deck{};
        return;
    }
    dk.y.assign(std::size_t(dk.w) * std::size_t(dk.h), -1.0f);
    // Rasterise every triangle top-down; keep per cell the highest surface at most kCap.
    for (const auto* part : {&m.body, &m.house})
        for (std::size_t t = 0; t + 2 < part->tris.size(); t += 3) {
            const float* a = part->tris[t].pos;
            const float* b = part->tris[t + 1].pos;
            const float* c = part->tris[t + 2].pos;
            if (std::min({a[1], b[1], c[1]}) > Deck::kCap) continue;
            const float ax = (a[0] - dk.x0) / Deck::kRes, az = (a[2] - dk.z0) / Deck::kRes;
            const float bx = (b[0] - dk.x0) / Deck::kRes, bz = (b[2] - dk.z0) / Deck::kRes;
            const float cx = (c[0] - dk.x0) / Deck::kRes, cz = (c[2] - dk.z0) / Deck::kRes;
            const float den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (std::abs(den) < 1e-6f) continue;  // vertical (walls): no deck
            const int i0 = std::max(0, int(std::floor(std::min({ax, bx, cx}))));
            const int i1 = std::min(dk.w - 1, int(std::ceil(std::max({ax, bx, cx}))));
            const int j0 = std::max(0, int(std::floor(std::min({az, bz, cz}))));
            const int j1 = std::min(dk.h - 1, int(std::ceil(std::max({az, bz, cz}))));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    const float px = float(i) + 0.5f, pz = float(j) + 0.5f;
                    const float l0 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / den;
                    const float l1 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / den;
                    const float l2 = 1 - l0 - l1;
                    if (l0 < -1e-4f || l1 < -1e-4f || l2 < -1e-4f) continue;
                    const float y = l0 * a[1] + l1 * b[1] + l2 * c[1];
                    if (y > Deck::kCap) continue;
                    float& d = dk.y[std::size_t(j) * std::size_t(dk.w) + std::size_t(i)];
                    d = std::max(d, y);
                }
        }
}

// Anchor points in world space (docs/3d-models.md "Anchors"): the model's fx_* points through the
// object's body / turret transform this frame.
bool ModelRenderer::buildingAnchors(const game::World& w, const Terrain& terr, int side, int building,
                                    std::string_view prefix, std::vector<AnchorWorld>& out) const {
    out.clear();
    if (!active() || side < 0 || side >= int(w.sides().size())) return false;
    const auto& sd = w.sides()[std::size_t(side)];
    if (building < 0 || building >= int(sd.buildings.size())) return false;
    const game::Building& b = sd.buildings[std::size_t(building)];
    Mat4 body, turret;
    float groundY = 0;
    if (!buildingPlacement(terr, b, body, turret, groundY)) return false;
    const formats::Model3D& m = models_[std::size_t(buildingModel_[std::size_t(b.type)])];
    for (const auto& a : m.anchors)
        if (a.name.compare(0, prefix.size(), prefix) == 0) {
            const Vec4 p = (a.onTurret ? turret : body) * Vec4{a.pos[0], a.pos[1], a.pos[2], 1};
            out.push_back(AnchorWorld{Vec3{p.x, p.y, p.z}, a.size});
        }
    return !out.empty();
}

bool ModelRenderer::unitAnchors(const game::World& w, const Terrain& terr, int side, int unit, std::string_view prefix,
                                std::vector<AnchorWorld>& out) const {
    out.clear();
    if (!active() || side < 0 || side >= int(w.sides().size())) return false;
    const auto& sd = w.sides()[std::size_t(side)];
    if (unit < 0 || unit >= int(sd.units.size())) return false;
    const game::Unit& u = sd.units[std::size_t(unit)];
    if (!u.alive) return false;
    const bool flying = (u.flags & 0x140) != 0;
    const float h = flying ? float(game::fixToPixel(u.posZ) & 0xff) : float(game::fixToPixel(u.posZ));
    Mat4 body, turret, tilt;
    Vec3 at;
    if (!unitPlacement(terr, w, u, u.type, h, flying, body, turret, tilt, at)) return false;
    const formats::Model3D& m = models_[std::size_t(unitModel_[std::size_t(u.type)])];
    for (const auto& a : m.anchors)
        if (a.name.compare(0, prefix.size(), prefix) == 0) {
            const Vec4 p = (a.onTurret ? turret : body) * Vec4{a.pos[0], a.pos[1], a.pos[2], 1};
            out.push_back(AnchorWorld{Vec3{p.x, p.y, p.z}, a.size});
        }
    return !out.empty();
}

void ModelRenderer::collect(const ModelFrame& f) {
    const game::World& w = *f.world;
    auto hidden = [&](int cx, int cy) {
        if (!w.inBounds(cx, cy)) return true;
        return f.shroudOn && (w.cell(cx, cy).shroud & 0xF) == 1;
    };
    // Buildings with walkable cells (docs/3d-models.md "Walkable cells"): their draws, for the
    // units standing on those cells below.
    struct WalkBuilding {
        int tileX, tileY;
        std::uint32_t mask;
        std::size_t draw;
    };
    std::vector<WalkBuilding> walkers;
    // Buildings.
    for (std::size_t si = 0; si < w.sides().size(); ++si) {
        const auto& side = w.sides()[si];
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const auto& b = side.buildings[std::size_t(bi)];
            if (!b.alive || b.type < 0 || b.type >= int(buildingModel_.size())) continue;
            const int mi = buildingModel_[std::size_t(b.type)];
            if (mi < 0 || mi >= int(gpu_.size())) continue;
            const auto& t = t_.bin.buildings[b.type];
            const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
            bool vis = false;
            for (int y = 0; y < fh && !vis; ++y)
                for (int x = 0; x < fw && !vis; ++x) vis = !hidden(b.tileX + x, b.tileY + y);
            if (!vis) continue;
            const GpuModel& g = gpu_[std::size_t(mi)];
            Draw d;
            float groundY = 0;
            if (!buildingPlacement(*f.terrain, b, d.bodyM, d.turretM, groundY)) continue;
            float sxp, syp;
            if (!f.camera->project(Vec3{d.bodyM.m[12], groundY, d.bodyM.m[14]}, sxp, syp)) continue;
            const float margin = 600;
            if (sxp < -margin || syp < -margin || sxp > f.camera->viewW() + margin || syp > f.camera->viewH() + margin) continue;
            d.model = mi;
            d.flash = (b.flicker & 2) ? 0.43f : 0.0f;
            d.shadowM = translate(d.bodyM.m[12], groundY + 0.6f, d.bodyM.m[14]);
            d.plane[3] = groundY;
            d.turret = g.turret.count > 0;
            if (g.emissive) d.emit = 1.6f * render::buildingGlow(w, t_, int(si), bi, f.lamps, f.time, true);  // thin strips: a bit more
            // Build-up / selling: a plane rising from the foundation (the flat parts up to 1.5 px stand
            // from the start) to the top of the model.
            if (const float p = render::buildupProgress(t_, b, float(f.time * 20.0 - double(w.gameTicks()))); p >= 0) {
                d.build[0] = groundY + 0.75f + 1.5f + p * (std::max(g.top, 2.0f) - 1.5f + 0.5f);
                d.build[1] = 4.0f * std::clamp(std::min(p, 1.0f - p) * 12.0f, 0.0f, 1.0f) + 0.001f;
                d.build[2] = float(std::fmod(f.time * 6.0, 3.0));
                d.buildK = std::max(p, 0.02f);
            }
            if (const int pose = buildingPose(w, b, mi); pose >= 0) {  // animation frame / silo level
                d.palette = paletteFor(mi, anim_[std::size_t(mi)].poseAnim, float(pose) / kPoseFps);
                if (std::size_t(pose) < g.bodyPoses.size() && g.bodyPoses[std::size_t(pose)].count) d.bodyPose = pose;
            }
            const formats::Rgba c = colours_[std::size_t(std::clamp(side.house, 0, formats::kHouseCount - 1))][8];
            d.house[0] = float(c & 0xFF) / 255.0f;
            d.house[1] = float((c >> 8) & 0xFF) / 255.0f;
            d.house[2] = float((c >> 16) & 0xFF) / 255.0f;
            if (walkPipe_)
                if (const std::uint32_t m = walkMask(b.type); m) {
                    d.walk[0] = float(b.tileX * 32);
                    d.walk[1] = float(b.tileY * 32);
                    d.walk[2] = float(m);
                    d.walk[3] = groundY + 1.25f;  // its floor (lifted 0.75 px) stays an ordinary part
                    walkers.push_back({b.tileX, b.tileY, m, draws_.size()});
                }
            draws_.push_back(d);
        }
    }
    // A ground unit on (or reaching 8 px into) a walkable cell of a building model gets that
    // building's stencil reference (1..255, given in the order met); the building draws its parts
    // over its walkable cells after the units, except where such a unit of its own is in front.
    int nextRef = 1;
    auto walkOn = [&](std::size_t unitDraw, float gx, float gy) {
        if (walkers.empty() || unitDraw >= draws_.size()) return;
        constexpr float r = 8;
        for (const float ox : {0.0f, -r, r})
            for (const float oy : {0.0f, -r, r}) {
                const int cx = int(std::floor((gx + ox) / 32)), cy = int(std::floor((gy + oy) / 32));
                for (const WalkBuilding& wb : walkers) {
                    const int dx = cx - wb.tileX, dy = cy - wb.tileY;
                    if (dx < 0 || dy < 0 || dx > 3 || dy > 3 || !(wb.mask & (1u << (dy * 4 + dx)))) continue;
                    Draw& bd = draws_[wb.draw];
                    if (bd.walkRef == 0) {
                        if (nextRef > 255) return;
                        bd.walkRef = nextRef++;
                    }
                    draws_[unitDraw].walkRef = bd.walkRef;
                    return;
                }
            }
    };
    // Ground units, flyers (+ a delivery carryall's cargo) at their height (Renderer3D's rules).
    for (std::size_t si = 0; si < w.sides().size(); ++si) {
        const auto& side = w.sides()[si];
        for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui)) {
            const auto& u = side.units[std::size_t(ui)];
            if (!u.alive) continue;
            const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
            if (!(u.flags & 0x200140)) {
                if (hidden(int(std::floor(gx / 32)), int(std::floor(gy / 32)))) continue;
                const std::size_t n = draws_.size();
                addUnit(f, w, int(si), u, u.type, float(game::fixToPixel(u.posZ)), false);
                if (draws_.size() > n) walkOn(n, gx, gy);
            } else if ((u.flags & 0x140) && !(u.flags & 0x200000)) {
                const float h = float(game::fixToPixel(u.posZ) & 0xff);
                addUnit(f, w, int(si), u, u.type, h, true);
                if (u.flags & 0x20000000) {
                    const auto& dl = side.deliveries[std::size_t(u.lastMoved % 10)];
                    const int cargo = dl.units[0];
                    if (cargo < 0xfe) addUnit(f, w, int(si), u, cargo, float(game::fixToPixel(u.posZ - 0xa0000) & 0xff), true);
                }
            }
        }
    }
}

int ModelRenderer::draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) {
    {  // dynamic point lights (fragment uniform buffer 1 of model_fs)
        static PointLightsU none{};
        SDL_PushGPUFragmentUniformData(cmd, 1, f.lights ? f.lights : &none, sizeof(PointLightsU));
    }
    if (draws_.empty() || !vb_ || !meshPipe_ || !palette_) return 0;
    int calls = 0;
    ModelVU vu;
    std::memcpy(vu.viewProj, f.viewProj.m, sizeof vu.viewProj);
    ModelFU fu{};
    fu.sunDir[0] = f.sun.x;
    fu.sunDir[1] = f.sun.y;
    fu.sunDir[2] = f.sun.z;
    fu.light[0] = f.ambient;
    fu.light[1] = f.diffuse;
    fu.light[2] = 1.0f / (f.ambient + f.diffuse * f.sun.y);
    fu.light[3] = f.shroudOn ? 1.0f : 0.0f;
    fu.fogColor[0] = f.fogColor[0];
    fu.fogColor[1] = f.fogColor[1];
    fu.fogColor[2] = f.fogColor[2];
    fu.fogColor[3] = f.fogStart;
    fu.fog[0] = f.eye.x;
    fu.fog[1] = f.eye.y;
    fu.fog[2] = f.eye.z;
    fu.fog[3] = f.fogEnd;
    fu.map[0] = f.mapW;
    fu.map[1] = f.mapH;
    fu.map[2] = 0.45f;  // shadow darkness
    std::memcpy(fu.lightVP, f.lightViewProj.m, sizeof fu.lightVP);
    std::memcpy(fu.shadow, f.shadow, sizeof fu.shadow);
    // With the sun shadow map the models cast real shadows (drawShadowDepth): no PS1 shadow
    // polygons / projected glTF shadows on top.
    const bool shadowMap = f.shadow[0] > 0.5f && f.shadowMap;
    SDL_GPUBufferBinding vb{vb_, 0};
    auto bind = [&](const GpuModel& g) {
        SDL_GPUTextureSamplerBinding sb[3] = {{g.tex ? g.tex : white_, g.point ? nearest_ : linear_},
                                              {f.shroud ? f.shroud : white_, f.linearClamp ? f.linearClamp : linear_},
                                              {f.shadowMap, f.shadowSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0, sb, f.shadowMap ? 3 : 2);
    };
    auto drawRange = [&](const Range& r, const Mat4& m, int palette) {
        if (!r.count) return;
        std::memcpy(vu.model, m.m, sizeof vu.model);
        vu.skin[0] = float(palette);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        SDL_DrawGPUPrimitives(pass, r.count, 1, r.first, 0);
        ++calls;
    };
    // Vertex storage buffer 0 of every model vertex shader: the joint palette.
    auto bindPalette = [&] { SDL_BindGPUVertexStorageBuffers(pass, 0, &palette_, 1); };
    // Shadows first (darken the terrain, no depth write), then the meshes.
    SDL_BindGPUGraphicsPipeline(pass, shadowPipe_);
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    bindPalette();
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    for (const Draw& d : draws_) {
        const GpuModel& g = gpu_[std::size_t(d.model)];
        if (!g.shadow.count || d.alpha < 1 || shadowMap) continue;
        bind(g);
        drawRange(g.shadow, d.shadowM, -1);
    }
    // glTF models: the body + turret flattened onto the ground plane along the sun.
    bool projBound = false;
    for (const Draw& d : draws_) {
        const GpuModel& g = gpu_[std::size_t(d.model)];
        if (!g.projectShadow || d.alpha < 1 || shadowMap) continue;
        if (!projBound) {
            SDL_BindGPUGraphicsPipeline(pass, projShadowPipe_);
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            bindPalette();
            SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
            projBound = true;
        }
        bind(g);
        ShadowVU su;
        std::memcpy(su.viewProj, f.viewProj.m, sizeof su.viewProj);
        su.sun[0] = f.sun.x;
        su.sun[1] = f.sun.y;
        su.sun[2] = f.sun.z;
        su.sun[3] = 0;
        std::memcpy(su.plane, d.plane, sizeof su.plane);
        auto one = [&](const Range& r, const Mat4& m, int palette) {
            if (!r.count) return;
            std::memcpy(su.model, m.m, sizeof su.model);
            su.skin[0] = float(palette);
            SDL_PushGPUVertexUniformData(cmd, 0, &su, sizeof su);
            SDL_DrawGPUPrimitives(pass, r.count, 1, r.first, 0);
            ++calls;
        };
        const Mat4 sq = squashAbove(d.plane[3], d.buildK);
        one(d.bodyPose >= 0 ? g.bodyPoses[std::size_t(d.bodyPose)] : g.body, sq * d.bodyM, d.palette);
        if (d.turret) one(g.turret, sq * d.turretM, -1);
    }
    SDL_BindGPUGraphicsPipeline(pass, meshPipe_);
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    bindPalette();
    auto drawMesh = [&](const Draw& d, float walkMode) {
        const GpuModel& g = gpu_[std::size_t(d.model)];
        fu.house[0] = d.house[0];
        fu.house[1] = d.house[1];
        fu.house[2] = d.house[2];
        fu.house[3] = d.flash;
        fu.sunDir[3] = d.alpha;
        fu.map[3] = d.emit;
        std::memcpy(fu.walk, d.walk, sizeof fu.walk);
        fu.walkMode[0] = walkMode;
        std::memcpy(fu.walkMode + 1, d.build, sizeof d.build);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        bind(g);
        drawRange(d.bodyPose >= 0 ? g.bodyPoses[std::size_t(d.bodyPose)] : g.body, d.bodyM, d.palette);
        drawRange(g.house, g.houseOnTurret && d.turret ? d.turretM : d.bodyM, g.houseOnTurret && d.turret ? -1 : d.palette);
        if (d.turret) drawRange(g.turret, d.turretM, -1);
    };
    // Walkable cells (docs/3d-models.md "Walkable cells"; the original draws units over the building
    // sprite): a building with units on its walkable cells (walkRef > 0, walk[2] = its cell mask)
    // leaves out its parts above those cells here; every mesh writes its stencil reference (such
    // units: their building's, the rest 0), so the stencil ends up naming the front-most unit.
    bool walking = false;
    int ref = -1;
    auto stencilRef = [&](int r) {
        if (walkPipe_ && r != ref) SDL_SetGPUStencilReference(pass, Uint8(r));
        ref = r;
    };
    // Opaque first, translucent (stealth) last.
    for (int pass2 = 0; pass2 < 2; ++pass2)
        for (const Draw& d : draws_) {
            if ((d.alpha < 1) != (pass2 == 1)) continue;
            const bool walkBuilding = d.walkRef > 0 && d.walk[2] > 0;
            walking |= walkBuilding;
            stencilRef(d.walkRef > 0 && !walkBuilding ? d.walkRef : 0);
            drawMesh(d, walkBuilding ? 1.0f : 0.0f);
        }
    // Then those parts, depth-tested as usual, except over the pixels where one of the building's
    // own units is the front-most surface: the unit stays drawn over them, from any camera angle,
    // while the building's solid cells, other buildings and the terrain still hide it.
    static const bool walkDebug = std::getenv("D2K_WALK_DEBUG") != nullptr;  // leave the walkable parts out
    if (walking && walkPipe_ && !walkDebug) {
        SDL_BindGPUGraphicsPipeline(pass, walkPipe_);
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        bindPalette();
        for (const Draw& d : draws_) {
            if (d.walkRef <= 0 || d.walk[2] <= 0) continue;
            stencilRef(d.walkRef);
            drawMesh(d, 2.0f);
        }
        stencilRef(0);
    }
    return calls;
}

// The meshes as casters of the sun shadow map (docs/3d-mode.md "Sun shadows"): depth only,
// opaque models only (stealthed ones cast nothing).
int ModelRenderer::drawShadowDepth(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) {
    if (draws_.empty() || !vb_ || !meshPipe_ || !dev_ || !palette_) return 0;
    if (!depthPipe_ || depthPipeFmt_ != f.shadowFormat) {
        if (depthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, depthPipe_);
        depthPipe_ = nullptr;
        depthPipeFmt_ = f.shadowFormat;
        SDL_GPUShader* vs = createShader(dev_, ShaderId::ModelVs, 0, 1, 1);
        SDL_GPUShader* fs = createShader(dev_, ShaderId::DepthFs, 0, 0);
        if (vs && fs) {
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = vs;
            pi.fragment_shader = fs;
            const VertexLayout layout;
            pi.vertex_input_state = layout.state();
            pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
            pi.rasterizer_state.enable_depth_bias = true;
            pi.rasterizer_state.depth_bias_constant_factor = 2.0f;
            pi.rasterizer_state.depth_bias_slope_factor = 2.5f;
            pi.depth_stencil_state.enable_depth_test = true;
            pi.depth_stencil_state.enable_depth_write = true;
            pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            pi.target_info.num_color_targets = 0;
            pi.target_info.depth_stencil_format = f.shadowFormat;
            pi.target_info.has_depth_stencil_target = true;
            depthPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
            if (!depthPipe_) SDL_Log("3D models: shadow pipeline failed: %s", SDL_GetError());
        }
        for (SDL_GPUShader* s2 : {vs, fs})
            if (s2) SDL_ReleaseGPUShader(dev_, s2);
    }
    if (!depthPipe_) return 0;
    SDL_BindGPUGraphicsPipeline(pass, depthPipe_);
    SDL_GPUBufferBinding vb{vb_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &palette_, 1);
    ModelVU vu;
    std::memcpy(vu.viewProj, f.lightViewProj.m, sizeof vu.viewProj);
    int calls = 0;
    auto drawRange = [&](const Range& r, const Mat4& m, int palette) {
        if (!r.count) return;
        std::memcpy(vu.model, m.m, sizeof vu.model);
        vu.skin[0] = float(palette);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        SDL_DrawGPUPrimitives(pass, r.count, 1, r.first, 0);
        ++calls;
    };
    for (const Draw& d : draws_) {
        if (d.alpha < 1) continue;
        const GpuModel& g = gpu_[std::size_t(d.model)];
        const Mat4 sq = squashAbove(d.plane[3], d.buildK), body = sq * d.bodyM, turret = sq * d.turretM;
        drawRange(d.bodyPose >= 0 ? g.bodyPoses[std::size_t(d.bodyPose)] : g.body, body, d.palette);
        drawRange(g.house, g.houseOnTurret && d.turret ? turret : body, g.houseOnTurret && d.turret ? -1 : d.palette);
        if (d.turret) drawRange(g.turret, turret, -1);
    }
    return calls;
}

}  // namespace d2k::render3d
