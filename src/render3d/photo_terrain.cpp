#include "render3d/photo_terrain.hpp"

#include "render/art_pack.hpp"
#include "render3d/gpu.hpp"
#include "render3d/model_texture.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace d2k::render3d {

namespace {

constexpr const char* kRoles[PhotoTerrain::kLayers] = {"sand", "ripples", "gravel", "rock", "rough", "cliff", "concrete"};
constexpr const char* kKinds[3] = {"albedo", "normal", "orh"};
// World px per repeat of each scan (a cell is 32 px). Larger than life (a 2 m scan over 3 - 4 cells): at an RTS
// camera's distance a true-scale scan is a flat colour; these keep stones, cracks and ripples a few pixels wide
// at the default zoom and fine grain zoomed in. D2K_PHOTO_SCALE="s0,s1,...,s6" overrides (tuning).
constexpr float kRepeat[PhotoTerrain::kLayers] = {160, 200, 220, 150, 130, 104, 96};

// One .d2kt file (tools/fetch_photo_terrain.py): "D2KTEX1\0", u32 format (1 BC1, 5 BC5), w, h, levels, 4 x f32
// mean, then the levels (largest first), tightly packed 4 x 4 blocks.
struct TexFile {
    std::uint32_t format = 0, w = 0, h = 0, levels = 0;
    float mean[4] = {};
    std::vector<std::uint8_t> data;
    std::size_t blockBytes() const { return format == 1 ? 8 : 16; }
    std::size_t levelBytes(std::uint32_t l) const {
        const std::size_t bw = std::max<std::uint32_t>(1, (w >> l) / 4), bh = std::max<std::uint32_t>(1, (h >> l) / 4);
        return bw * bh * blockBytes();
    }
};

bool readTex(const fs::path& p, TexFile& t) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    char magic[8];
    std::uint32_t head[4];
    in.read(magic, 8);
    in.read(reinterpret_cast<char*>(head), sizeof head);
    in.read(reinterpret_cast<char*>(t.mean), sizeof t.mean);
    if (!in || std::memcmp(magic, "D2KTEX1\0", 8) != 0) return false;
    t.format = head[0];
    t.w = head[1];
    t.h = head[2];
    t.levels = head[3];
    if ((t.format != 1 && t.format != 5) || t.w < 4 || t.w > 8192 || t.w != t.h || (t.w & (t.w - 1)) || t.levels < 1 || t.levels > 14 ||
        (t.w >> (t.levels - 1)) < 4)
        return false;
    std::size_t total = 0;
    for (std::uint32_t l = 0; l < t.levels; ++l) total += t.levelBytes(l);
    t.data.resize(total);
    in.read(reinterpret_cast<char*>(t.data.data()), std::streamsize(total));
    return bool(in);
}

// A device without BC textures: one level decoded to RGBA8 (BC5: x, y in r, g).
std::vector<std::uint8_t> decodeLevel(const TexFile& t, std::uint32_t level, std::size_t offset) {
    const std::uint32_t w = t.w >> level, h = t.h >> level;
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4, 255);
    const std::uint8_t* src = t.data.data() + offset;
    for (std::uint32_t by = 0; by < h / 4; ++by)
        for (std::uint32_t bx = 0; bx < w / 4; ++bx, src += t.blockBytes()) {
            std::uint32_t px[16] = {};
            std::uint8_t r[16], g[16];
            if (t.format == 1) {
                decodeBc1(src, px);
            } else {
                decodeBc4(src, r);
                decodeBc4(src + 8, g);
                for (int i = 0; i < 16; ++i) px[i] = std::uint32_t(r[i]) | (std::uint32_t(g[i]) << 8) | 0xFFFF0000u;
            }
            for (int i = 0; i < 16; ++i) {
                const std::size_t o = (std::size_t(by * 4 + std::uint32_t(i / 4)) * w + bx * 4 + std::uint32_t(i % 4)) * 4;
                std::memcpy(&out[o], &px[i], 4);
            }
        }
    return out;
}

bool complete(const fs::path& dir) {
    std::error_code ec;
    for (const char* role : kRoles)
        for (const char* kind : kKinds)
            if (!fs::is_regular_file(dir / (std::string(role) + "_" + kind + ".d2kt"), ec)) return false;
    return true;
}

}  // namespace

std::string PhotoTerrain::packDir() {
    if (const char* e = std::getenv("D2K_PHOTO_PACK"); e && *e) return complete(fs::path(e)) ? std::string(e) : std::string();
    for (const auto& d : render::ArtPack::searchDirs()) {
        const fs::path p = fs::path(d) / "photo-terrain";
        if (complete(p)) return p.string();
    }
    return {};
}

std::unique_ptr<PhotoTerrain> PhotoTerrain::create(SDL_GPUDevice* dev, int aniso) {
    const std::string dir = packDir();
    if (dir.empty()) {
        SDL_Log("3D: photo terrain pack not installed (artpacks/photo-terrain: tools/fetch_photo_terrain.py) - the "
                "procedural Reimagined ground");
        return nullptr;
    }
    std::unique_ptr<PhotoTerrain> p(new PhotoTerrain(dev));
    if (!p->load(dir, aniso)) return nullptr;
    return p;
}

PhotoTerrain::~PhotoTerrain() {
    for (SDL_GPUTexture* t : tex_)
        if (t) SDL_ReleaseGPUTexture(dev_, t);
    if (sampler_) SDL_ReleaseGPUSampler(dev_, sampler_);
}

bool PhotoTerrain::load(const std::string& dir, int aniso) {
    const Uint64 t0 = SDL_GetTicksNS();
    const SDL_GPUTextureFormat bcFmt[3] = {SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM,
                                           SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM};
    bool bc = modelTextureCaps(dev_).bc;
    for (SDL_GPUTextureFormat f : bcFmt)
        bc = bc && SDL_GPUTextureSupportsFormat(dev_, f, SDL_GPU_TEXTURETYPE_2D_ARRAY, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    float repeat[kLayers];
    std::copy(std::begin(kRepeat), std::end(kRepeat), repeat);
    if (const char* e = std::getenv("D2K_PHOTO_SCALE")) {
        const char* s = e;
        for (int i = 0; i < kLayers && *s; ++i) {
            char* end = nullptr;
            const float v = std::strtof(s, &end);
            if (end == s) break;
            if (v >= 8) repeat[i] = v;
            s = *end == ',' ? end + 1 : end;
        }
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    if (!cmd) return false;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    std::vector<SDL_GPUTransferBuffer*> temps;
    bool ok = true;
    std::size_t bytes = 0;
    std::uint32_t size = 0;
    for (int k = 0; k < 3 && ok; ++k) {
        for (int layer = 0; layer < kLayers && ok; ++layer) {
            TexFile t;
            const fs::path file = fs::path(dir) / (std::string(kRoles[layer]) + "_" + kKinds[k] + ".d2kt");
            const std::uint32_t wantFmt = k == 1 ? 5u : 1u;
            if (!readTex(file, t) || t.format != wantFmt) {
                SDL_Log("3D: photo terrain: %s is not a texture of this pack", file.string().c_str());
                ok = false;
                break;
            }
            // Without BC: RGBA8 from the second level on (a quarter of the texels: the memory stays bounded).
            const std::uint32_t skip = bc || t.levels < 2 ? 0 : 1;
            const std::uint32_t w = t.w >> skip, levels = t.levels - skip;
            if (!tex_[k]) {
                size = w;
                SDL_GPUTextureCreateInfo ci{};
                ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
                ci.format = bc ? bcFmt[k] : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                ci.width = ci.height = w;
                ci.layer_count_or_depth = kLayers;
                ci.num_levels = levels;
                tex_[k] = SDL_CreateGPUTexture(dev_, &ci);
                if (!tex_[k]) {
                    SDL_Log("3D: photo terrain: texture array %ux%u failed: %s", w, w, SDL_GetError());
                    ok = false;
                    break;
                }
            } else if (w != size) {
                SDL_Log("3D: photo terrain: %s has another size than the rest of the pack", file.string().c_str());
                ok = false;
                break;
            }
            if (k == 0) {
                for (int c = 0; c < 3; ++c) layerU_[layer][c] = 1.0f / std::max(t.mean[c], 0.02f);
                layerU_[layer][3] = repeat[layer];
            }
            std::size_t offset = 0;
            for (std::uint32_t l = 0; l < t.levels; ++l) {
                const std::size_t lb = t.levelBytes(l);
                if (l >= skip) {
                    std::vector<std::uint8_t> rgba;
                    const std::uint8_t* src = t.data.data() + offset;
                    std::size_t n = lb;
                    if (!bc) {
                        rgba = decodeLevel(t, l, offset);
                        src = rgba.data();
                        n = rgba.size();
                    }
                    SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, Uint32(n), 0};
                    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
                    void* dst = tb ? SDL_MapGPUTransferBuffer(dev_, tb, false) : nullptr;
                    if (!dst) {
                        if (tb) SDL_ReleaseGPUTransferBuffer(dev_, tb);
                        ok = false;
                        break;
                    }
                    std::memcpy(dst, src, n);
                    SDL_UnmapGPUTransferBuffer(dev_, tb);
                    temps.push_back(tb);
                    const Uint32 lw = t.w >> l;
                    SDL_GPUTextureTransferInfo ti{tb, 0, lw, lw};
                    SDL_GPUTextureRegion reg{};
                    reg.texture = tex_[k];
                    reg.mip_level = l - skip;
                    reg.layer = Uint32(layer);
                    reg.w = reg.h = lw;
                    reg.d = 1;
                    SDL_UploadToGPUTexture(cp, &ti, &reg, false);
                    bytes += n;
                }
                offset += lb;
            }
        }
    }
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    for (SDL_GPUTransferBuffer* tb : temps) SDL_ReleaseGPUTransferBuffer(dev_, tb);
    if (!ok) return false;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.max_lod = 16;
    si.enable_anisotropy = gpuAnisotropy() && aniso > 1;
    si.max_anisotropy = float(std::max(1, aniso));
    sampler_ = SDL_CreateGPUSampler(dev_, &si);
    if (!sampler_) return false;
    SDL_Log("3D: photo terrain - %d layers %ux%u (%s, %.0f MB) from %s in %.0f ms", int(kLayers), size, size,
            bc ? "BC1 / BC5" : "RGBA8", double(bytes) / 1e6, dir.c_str(), double(SDL_GetTicksNS() - t0) / 1e6);
    return true;
}

}  // namespace d2k::render3d
