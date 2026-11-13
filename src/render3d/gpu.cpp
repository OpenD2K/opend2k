#include "render3d/gpu.hpp"

#include "render3d/shader_blobs.hpp"

#include <cstdlib>
#include <cstring>

namespace d2k::render3d {

namespace {

bool gDepthClamp = false;
bool gAnisotropy = false;
bool gHdrOk = true;

// SDL_CreateGPURenderer with options. `ourFormats`: ask only for a GPU device that accepts the
// shader formats this build carries for the 3D mode (render3d/shader_blobs.*). On Windows without
// DXIL (no dxc at build time) that skips Direct3D 12 and picks Vulkan; on macOS it is Metal as
// before. SDL_GPU_DRIVER (environment / hint) still overrides the driver. `features`: also ask for
// depth clamping + anisotropic filtering (gpuDepthClamp / gpuAnisotropy); a Vulkan device without
// them fails, so the caller retries without. `driver`: a preferred SDL_gpu backend ("vulkan"), nullptr = SDL's
// own choice; SDL_GPU_DRIVER (hint / environment) still wins (SDL_GPUSelectBackend reads the hint first).
SDL_Renderer* createGpuRenderer(SDL_Window* window, bool ourFormats, bool features, const char* driver = nullptr) {
    const SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetStringProperty(p, SDL_PROP_RENDERER_CREATE_NAME_STRING, "gpu");
    if (driver) SDL_SetStringProperty(p, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver);
    if (ourFormats) {
        const SDL_GPUShaderFormat f = builtShaderFormats();
        SDL_SetBooleanProperty(p, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, (f & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
        SDL_SetBooleanProperty(p, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, (f & SDL_GPU_SHADERFORMAT_DXIL) != 0);
        SDL_SetBooleanProperty(p, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN, (f & SDL_GPU_SHADERFORMAT_MSL) != 0);
    }
    // D2K_GPU_FEATURES=clamp / aniso: only that one (diagnosing a driver that breaks with the other).
    const char* only = std::getenv("D2K_GPU_FEATURES");
    const bool clamp = features && !(only && std::strcmp(only, "aniso") == 0);
    const bool aniso = features && !(only && std::strcmp(only, "clamp") == 0);
    SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, clamp);
    SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, aniso);
    SDL_Renderer* r = SDL_CreateRendererWithProperties(p);
    SDL_DestroyProperties(p);
    if (r) {
        gDepthClamp = clamp;
        gAnisotropy = aniso;
    }
    return r;
}

}  // namespace

bool createWindowAndRenderer(const char* title, int w, int h, SDL_WindowFlags flags, SDL_Window** window,
                             SDL_Renderer** renderer) {
    *window = SDL_CreateWindow(title, w, h, flags);
    if (!*window) return false;
    *renderer = nullptr;
    const char* pref = std::getenv("D2K_RENDERER");
    const bool wantGpu = !(pref && std::strcmp(pref, "sdl") == 0) && !SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    if (wantGpu) {
        // D2K_GPU_FEATURES=0: skip the optional features (tests the fallback path); =clamp / =aniso: only that one.
        const char* feat = std::getenv("D2K_GPU_FEATURES");
        const bool features = !(feat && std::strcmp(feat, "0") == 0);
        auto create = [&](const char* driver) {
            SDL_Renderer* r = nullptr;
            if (features) r = createGpuRenderer(*window, true, true, driver);
            if (!r) {
                if (features) SDL_Log("GPU renderer with depth clamp + anisotropy unavailable (%s): trying without", SDL_GetError());
                r = createGpuRenderer(*window, true, false, driver);
            }
            return r;
        };
#ifdef _WIN32
        // Windows: Vulkan first (Direct3D 12 with a model pack broke the device - docs/3d-mode.md "Direct3D 12");
        // vulkan-1.dll comes with the GPU driver. Without a Vulkan driver: SDL's choice (Direct3D 12).
        // SDL_GPU_DRIVER overrides.
        if (!SDL_GetHint(SDL_HINT_GPU_DRIVER)) {
            *renderer = create("vulkan");
            if (!*renderer) SDL_Log("Vulkan GPU renderer unavailable (%s): trying Direct3D 12", SDL_GetError());
        }
        if (!*renderer)
#endif
        *renderer = create(nullptr);
        if (!*renderer) {
            // No device for our 3D shaders (e.g. no Vulkan driver on a Windows build without DXIL):
            // any GPU device still runs Original / Enhanced through the GPU renderer.
            SDL_Log("GPU renderer for the 3D shaders unavailable (%s): trying any GPU device", SDL_GetError());
            *renderer = createGpuRenderer(*window, false, false);
        }
        if (!*renderer) SDL_Log("GPU renderer unavailable (%s): using the default renderer (no 3D mode)", SDL_GetError());
    }
    if (!*renderer) *renderer = SDL_CreateRenderer(*window, nullptr);
    if (!*renderer) {
        SDL_DestroyWindow(*window);
        *window = nullptr;
        return false;
    }
    if (SDL_GPUDevice* dev = gpuDeviceOf(*renderer)) {
        // Direct3D 12 (SDL 3.4.20): anisotropic samplers in the 3D mode break the device - every later
        // texture / buffer creation fails (DXGI_ERROR_INVALID_CALL) and the game crashes; depth clamping is
        // fine. Vulkan (the Windows default) keeps anisotropy. D2K_D3D12_ANISO=1 tries it anyway.
        const char* a = std::getenv("D2K_D3D12_ANISO");
        const bool d3d12 = std::strcmp(SDL_GetGPUDeviceDriver(dev), "direct3d12") == 0;
        if (gAnisotropy && d3d12 && !(a && a[0] == '1')) gAnisotropy = false;
        const char* h = std::getenv("D2K_D3D12_HDR");
        gHdrOk = !d3d12 || (h && h[0] == '1');
        if (!gHdrOk) SDL_Log("Direct3D 12: HDR (half-float 3D scene, extended-range output) off - 8-bit / SDR");
        SDL_Log("Renderer: SDL GPU renderer on %s (depth clamp %s, anisotropy %s)", SDL_GetGPUDeviceDriver(dev),
                gDepthClamp ? "on" : "off", gAnisotropy ? "on" : "off");
    }
    else
        SDL_Log("Renderer: %s", SDL_GetRendererName(*renderer));
    return true;
}

bool gpuDepthClamp() { return gDepthClamp; }
bool gpuAnisotropy() { return gAnisotropy; }
bool gpuHdrOk() { return gHdrOk; }

SDL_GPUDevice* gpuDeviceOf(SDL_Renderer* r) {
    if (!r) return nullptr;
    return static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
}

SDL_GPUTexture* gpuTextureOf(SDL_Texture* t) {
    if (!t) return nullptr;
    return static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

void submitRendererWork(SDL_Renderer* r) {
    const SDL_Rect one{0, 0, 1, 1};
    if (SDL_Surface* s = SDL_RenderReadPixels(r, &one)) SDL_DestroySurface(s);
}

}  // namespace d2k::render3d
