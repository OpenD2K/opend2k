#pragma once
// GPU side of the light grid (render3d/lights.hpp): the storage buffer every lit fragment shader reads
// (fragment storage buffer 0) and its transfer buffer. A renderer drawing without lights (the 2D terrain
// bake, a model pass without a grid) binds a small stand-in, since SDL_gpu wants every declared slot bound.

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>

namespace d2k::render3d {

class LightGridBuffer {
public:
    LightGridBuffer() = default;
    ~LightGridBuffer() { release(); }
    LightGridBuffer(const LightGridBuffer&) = delete;
    LightGridBuffer& operator=(const LightGridBuffer&) = delete;

    // bytes: capacity (a multiple of 16).
    bool init(SDL_GPUDevice* dev, std::uint32_t bytes) {
        release();
        dev_ = dev;
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bi.size = bytes;
        buf_ = SDL_CreateGPUBuffer(dev, &bi);
        cap_ = buf_ ? bytes : 0;
        return buf_ != nullptr;
    }
    // Copies `bytes` (<= capacity) into the buffer in its own copy pass (call outside a render pass). The GPU
    // buffer and the transfer buffer cycle, so a frame still in flight keeps reading its own copy.
    void upload(SDL_GPUCommandBuffer* cmd, const void* data, std::uint32_t bytes) {
        if (!buf_ || !cmd || bytes == 0) return;
        bytes = bytes > cap_ ? cap_ : bytes;
        if (!xfer_) {
            SDL_GPUTransferBufferCreateInfo ti{};
            ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            ti.size = cap_;
            xfer_ = SDL_CreateGPUTransferBuffer(dev_, &ti);
            if (!xfer_) return;
        }
        void* p = SDL_MapGPUTransferBuffer(dev_, xfer_, true);
        if (!p) return;
        std::memcpy(p, data, bytes);
        SDL_UnmapGPUTransferBuffer(dev_, xfer_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (!cp) return;
        SDL_GPUTransferBufferLocation src{xfer_, 0};
        SDL_GPUBufferRegion reg{buf_, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, true);
        SDL_EndGPUCopyPass(cp);
    }
    SDL_GPUBuffer* buffer() const { return buf_; }
    void release() {
        if (dev_) {
            if (buf_) SDL_ReleaseGPUBuffer(dev_, buf_);
            if (xfer_) SDL_ReleaseGPUTransferBuffer(dev_, xfer_);
        }
        buf_ = nullptr;
        xfer_ = nullptr;
        cap_ = 0;
    }

private:
    SDL_GPUDevice* dev_ = nullptr;
    SDL_GPUBuffer* buf_ = nullptr;
    SDL_GPUTransferBuffer* xfer_ = nullptr;
    std::uint32_t cap_ = 0;
};

// Binds the grid (or `fallback` when there is none) as fragment storage buffer 0. Called after every bind of a
// lit pipeline.
inline void bindLightGrid(SDL_GPURenderPass* pass, SDL_GPUBuffer* grid, LightGridBuffer& fallback, SDL_GPUDevice* dev) {
    SDL_GPUBuffer* b = grid;
    if (!b) {
        if (!fallback.buffer()) fallback.init(dev, 64);
        b = fallback.buffer();
    }
    if (b) SDL_BindGPUFragmentStorageBuffers(pass, 0, &b, 1);
}

}  // namespace d2k::render3d
