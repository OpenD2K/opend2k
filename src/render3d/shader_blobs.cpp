#include "render3d/shader_blobs.hpp"

#include "render3d/models_msl.hpp"
#include "render3d/shaders_msl.hpp"
#include "render3d/terrain_v2_msl.hpp"
#include "render3d/terrain_v3_msl.hpp"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <string>

namespace d2k::render3d {

namespace spirv {
#include "render3d/shaders_spirv.inc"
}
#if defined(D2K_HAVE_DXIL)
namespace dxil {
#include "shaders_dxil.inc"  // generated (cmake/CompileShadersDXIL.cmake)
}
#endif

namespace {

struct Blob {
    const unsigned char* code;
    std::size_t size;
};

struct Entry {
    const char* name;  // entry point (the same in every format)
    SDL_GPUShaderStage stage;
    const char* msl;   // MSL source holding it
    Blob spirv;
#if defined(D2K_HAVE_DXIL)
    Blob dxil;
#endif
};

#if defined(D2K_HAVE_DXIL)
#define D2K_SHADER(id, stage, msl)                                                                     \
    Entry { #id, stage, msl, {spirv::kSpirv_##id, sizeof spirv::kSpirv_##id}, {dxil::kDxil_##id, sizeof dxil::kDxil_##id} }
#else
#define D2K_SHADER(id, stage, msl) Entry { #id, stage, msl, {spirv::kSpirv_##id, sizeof spirv::kSpirv_##id} }
#endif

// terrain3_fs = everything of the designed terrain's source before its fragment function (the
// shared structs, noise, anti-tiled sampling, shadows) + the faithful body.
const char* terrain3Msl() {
    static const std::string src = [] {
        const char* head = msl::kTerrain2;
        const char* cut = std::strstr(head, msl::kTerrain2Main);
        return std::string(head, cut ? std::size_t(cut - head) : std::strlen(head)) + msl::kTerrain3Body;
    }();
    return src.c_str();
}

const Entry& entryFor(ShaderId id) {
    static const Entry kEntries[] = {
        D2K_SHADER(terrain_vs, SDL_GPU_SHADERSTAGE_VERTEX, msl::kTerrain),
        D2K_SHADER(terrain_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kTerrain),
        D2K_SHADER(sprite_vs, SDL_GPU_SHADERSTAGE_VERTEX, msl::kSprite),
        D2K_SHADER(sprite_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kSprite),
        D2K_SHADER(model_vs, SDL_GPU_SHADERSTAGE_VERTEX, msl::kModel),
        D2K_SHADER(model_shadow_vs, SDL_GPU_SHADERSTAGE_VERTEX, msl::kModel),
        D2K_SHADER(model_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kModel),
        D2K_SHADER(shadow_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kModel),
        D2K_SHADER(terrain2_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kTerrain2),
        D2K_SHADER(rock_vs, SDL_GPU_SHADERSTAGE_VERTEX, msl::kRock),
        D2K_SHADER(rock_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kRock),
        D2K_SHADER(depth_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kDepth),
        D2K_SHADER(terrain3_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, terrain3Msl()),
        D2K_SHADER(tonemap_fs, SDL_GPU_SHADERSTAGE_FRAGMENT, msl::kTonemap),
    };
    return kEntries[static_cast<int>(id)];
}
#undef D2K_SHADER

SDL_GPUShaderFormat pickFormat(SDL_GPUDevice* dev) {
    const SDL_GPUShaderFormat have = dev ? SDL_GetGPUShaderFormats(dev) & builtShaderFormats() : 0;
    for (SDL_GPUShaderFormat f : {SDL_GPU_SHADERFORMAT_MSL, SDL_GPU_SHADERFORMAT_DXIL, SDL_GPU_SHADERFORMAT_SPIRV})
        if (have & f) return f;
    return SDL_GPU_SHADERFORMAT_INVALID;
}

}  // namespace

SDL_GPUShaderFormat builtShaderFormats() {
    SDL_GPUShaderFormat f = SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV;
#if defined(D2K_HAVE_DXIL)
    f |= SDL_GPU_SHADERFORMAT_DXIL;
#endif
    return f;
}

bool deviceHasShaders(SDL_GPUDevice* dev) { return pickFormat(dev) != SDL_GPU_SHADERFORMAT_INVALID; }

SDL_GPUShader* createShader(SDL_GPUDevice* dev, ShaderId id, Uint32 samplers, Uint32 uniforms, Uint32 storageBuffers) {
    const Entry& e = entryFor(id);
    const SDL_GPUShaderFormat format = pickFormat(dev);
    SDL_GPUShaderCreateInfo ci{};
    ci.entrypoint = e.name;
    ci.format = format;
    ci.stage = e.stage;
    ci.num_samplers = samplers;
    ci.num_uniform_buffers = uniforms;
    ci.num_storage_buffers = storageBuffers;
    if (format == SDL_GPU_SHADERFORMAT_MSL) {
        ci.code = reinterpret_cast<const Uint8*>(e.msl);
        ci.code_size = std::strlen(e.msl);
    } else if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
        ci.code = e.spirv.code;
        ci.code_size = e.spirv.size;
#if defined(D2K_HAVE_DXIL)
    } else if (format == SDL_GPU_SHADERFORMAT_DXIL) {
        ci.code = e.dxil.code;
        ci.code_size = e.dxil.size;
#endif
    } else {
        SDL_Log("3D: no shader format for GPU driver %s", dev ? SDL_GetGPUDeviceDriver(dev) : "none");
        return nullptr;
    }
    SDL_GPUShader* s = SDL_CreateGPUShader(dev, &ci);
    if (!s) SDL_Log("3D: shader %s failed: %s", e.name, SDL_GetError());
    return s;
}

}  // namespace d2k::render3d
