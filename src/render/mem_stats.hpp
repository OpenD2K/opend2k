#pragma once
// Memory accounting (docs/memory.md): per-category byte counters for the 3D models' allocations (CPU
// copies, GPU textures and buffers) and the process footprint
// (macOS phys_footprint, Linux / Android RSS, Windows private bytes). D2K_MEM=1 logs checkpoints
// (`checkpoint`) and the counters; everything else is a few relaxed atomic adds. Render-only.

#include <cstdint>

namespace d2k::render::mem {

enum class Cat : int {
    ModelCpuGeometry,  // Model3D triangle lists kept on the CPU (picking, decks, anchors)
    ModelCpuImages,    // decoded glTF images still held on the CPU
    ModelGpuTextures,  // model textures on the GPU (incl. mip chains)
    ModelGpuBuffers,   // the models' vertex / index buffers
    Count
};

// Adds (or with a negative value removes) bytes of a category.
void add(Cat c, std::int64_t bytes);
std::int64_t get(Cat c);
const char* name(Cat c);
// The process's memory footprint in bytes (0 when unknown) and its peak so far (macOS: the kernel's
// lifetime peak; elsewhere: the highest value seen by footprint() calls).
std::uint64_t footprint();
std::uint64_t peakFootprint();
bool enabled();  // D2K_MEM=1
// Hands memory the allocator keeps after frees back to the system (macOS: malloc_zone_pressure_relief,
// glibc: malloc_trim, Windows: _heapmin). After big transient work (a game's load, a game closed).
void releaseFreeMemory();
// D2K_MEM=1: logs "Memory [label]: footprint X MB (peak Y MB)" and the non-zero counters.
void checkpoint(const char* label);

// Bytes of a 2D texture of w x h texels at `bytesPerTexel` (fractional for block formats) with a full
// mip chain (levels > 1) or just the base level.
std::int64_t textureBytes(int w, int h, double bytesPerTexel, int levels);

}  // namespace d2k::render::mem
