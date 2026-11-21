#pragma once
// The 3D mode's shaders in every format this build carries, picked per GPU device:
//   MSL    (Metal: macOS / iPadOS)  - render3d/shaders_msl.hpp + models_msl.hpp + terrain_v2_msl.hpp + terrain_v3_msl.hpp +
//                                     clutter_msl.hpp + ssao_msl.hpp,
//                                     compiled by SDL at run time;
//   SPIR-V (Vulkan: Windows / Linux) - render3d/shaders_spirv.inc, from shaders.hlsl by tools/compile_shaders.sh;
//   DXIL   (Direct3D 12: Windows)   - <build>/generated/shaders_dxil.inc, from shaders.hlsl by dxc at build
//                                     time (cmake/CompileShadersDXIL.cmake), only when D2K_HAVE_DXIL.
// Preference: MSL, then DXIL, then SPIR-V (a device offers the formats its backend accepts).

#include <SDL3/SDL.h>

namespace d2k::render3d {

enum class ShaderId {
    TerrainVs, TerrainFs, SpriteVs, SpriteFs, ModelVs, ModelShadowVs, ModelFs, ShadowFs,
    Terrain2Fs, RockVs, RockFs,  // designed terrain (render3d/terrain_v2.*)
    DepthFs,                     // sun shadow map depth pass
    Terrain3Fs,                  // faithful designed terrain (render3d/terrain_v3_msl.hpp)
    TonemapFs,                   // HDR tone map, a custom shader of SDL's GPU renderer (render/hdr.*)
    FxVs, FxFs,                  // cinematic particles (render3d/cinematic_fx.*)
    FxcVs, FxcFs,                // their half-resolution composite
    ModelPbrFs,                  // Reimagined models with PBR maps (render3d/model_pbr_msl.hpp)
    ClutterVs, ClutterFs,        // Reimagined ground clutter (render3d/clutter_msl.hpp, render3d/ground_clutter.*)
    SsaoFs, SsaoBlurFs           // screen-space ambient occlusion (render3d/ssao_msl.hpp, render3d/ssao.*; with FxcVs)
};

// Formats compiled into this build (MSL | SPIRV, + DXIL when built with dxc).
SDL_GPUShaderFormat builtShaderFormats();

// True when `dev` accepts one of the built formats (otherwise Graphics: 3D falls back to Enhanced).
bool deviceHasShaders(SDL_GPUDevice* dev);

// Creates the shader in the best format for `dev`; logs and returns nullptr on failure.
SDL_GPUShader* createShader(SDL_GPUDevice* dev, ShaderId id, Uint32 samplers, Uint32 uniforms, Uint32 storageBuffers = 0);

}  // namespace d2k::render3d
