#pragma once
// SDL_gpu plumbing shared by the app and the 3D mode (docs/3d-mode.md "SDL_gpu + SDL_Renderer").
//
// The whole app draws through ONE SDL_Renderer. When possible that is SDL 3.4's GPU renderer
// (SDL_CreateGPURenderer): it renders the 2D UI with SDL_gpu, so the 3D mode can use the same
// SDL_GPUDevice, sample every SDL_Texture the 2D code already made (sprites, tiles, the soft
// shroud mask) through SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, and hand its frame back as an
// SDL_Texture wrapping its own SDL_GPUTexture. Original / Enhanced draw exactly as with SDL's
// default (Metal) renderer. Without the GPU renderer (SDL_RENDER_DRIVER set, D2K_RENDERER=sdl,
// or creation failed) the app uses SDL's default renderer and the 3D mode falls back to Enhanced.

#include <SDL3/SDL.h>

namespace d2k::render3d {

// SDL_CreateWindowAndRenderer replacement: the GPU renderer if available, else the default.
bool createWindowAndRenderer(const char* title, int w, int h, SDL_WindowFlags flags, SDL_Window** window,
                             SDL_Renderer** renderer);

// The SDL_GPUDevice of a GPU renderer (nullptr for other renderers).
SDL_GPUDevice* gpuDeviceOf(SDL_Renderer* r);
// The SDL_GPUTexture behind a GPU renderer's texture (nullptr otherwise).
SDL_GPUTexture* gpuTextureOf(SDL_Texture* t);
// The GPU renderer records texture uploads into its own command buffer and submits it only at
// SDL_RenderPresent. Work submitted by the 3D mode in between runs BEFORE those uploads, so a
// texture created this frame would be sampled empty. This forces the renderer to submit now
// (via a 1x1 SDL_RenderReadPixels, which submits and waits): call it only when new textures
// were created since the last call (first frames, new sprite frames, a rebuilt atlas).
void submitRendererWork(SDL_Renderer* r);

// Optional SDL_gpu device features. SDL's GPU renderer turns them off unless asked, and with them
// off a pipeline must clip depth and a sampler must not be anisotropic (SDL_RENDER_GPU_DEBUG=1
// asserts; Vulkan / D3D12 get an invalid pipeline or sampler). createWindowAndRenderer asks for
// both and falls back without them; the 3D code reads what the device got:
//   depth clamp: depth-clamped pipelines (as before) when available, else depth clip;
//   anisotropy: anisotropic samplers only when available.
bool gpuDepthClamp();
bool gpuAnisotropy();
// HDR (a half-float 3D scene, extended-range output): not on Direct3D 12 with SDL 3.4.20 - a pipeline for
// the half-float scene fails to create (black battlefield) and the extended swapchain crashes; Vulkan /
// Metal keep it. D2K_D3D12_HDR=1 retests it.
bool gpuHdrOk();
// enable_depth_clip for a 3D pipeline's rasterizer state.
inline bool gpuDepthClip() { return !gpuDepthClamp(); }

}  // namespace d2k::render3d
