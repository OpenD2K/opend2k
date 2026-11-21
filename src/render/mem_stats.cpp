#include "render/mem_stats.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#include <mach/mach.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <malloc.h>
#include <psapi.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

namespace d2k::render::mem {

namespace {
std::atomic<std::int64_t> g_bytes[int(Cat::Count)];
std::atomic<std::uint64_t> g_peak{0};
}  // namespace

void add(Cat c, std::int64_t bytes) { g_bytes[int(c)].fetch_add(bytes, std::memory_order_relaxed); }
std::int64_t get(Cat c) { return g_bytes[int(c)].load(std::memory_order_relaxed); }

const char* name(Cat c) {
    switch (c) {
    case Cat::ModelCpuGeometry: return "model CPU geometry";
    case Cat::ModelCpuImages: return "model CPU images";
    case Cat::ModelGpuTextures: return "model GPU textures";
    case Cat::ModelGpuBuffers: return "model GPU buffers";
    case Cat::Count: break;
    }
    return "?";
}

static std::uint64_t peakSeen(std::uint64_t v) {
    std::uint64_t p = g_peak.load(std::memory_order_relaxed);
    while (v > p && !g_peak.compare_exchange_weak(p, v, std::memory_order_relaxed)) {}
    return std::max(p, v);
}

std::uint64_t footprint() {
    std::uint64_t v = 0;
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS)
        v = info.phys_footprint;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
        v = pmc.PrivateUsage;
#elif defined(__linux__) || defined(__ANDROID__)
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        unsigned long size = 0, rss = 0;
        if (std::fscanf(f, "%lu %lu", &size, &rss) == 2) v = std::uint64_t(rss) * std::uint64_t(sysconf(_SC_PAGESIZE));
        std::fclose(f);
    }
#endif
    peakSeen(v);
    return v;
}

std::uint64_t peakFootprint() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS &&
        n >= TASK_VM_INFO_REV1_COUNT)
        return peakSeen(info.ledger_phys_footprint_peak);
#endif
    return peakSeen(footprint());
}

void releaseFreeMemory() {
#if defined(__APPLE__)
    // (Does not empty malloc's large-block cache: platform/main.cpp turns that off.)
    malloc_zone_pressure_relief(nullptr, 0);
#elif defined(_WIN32)
    _heapmin();
#elif defined(__GLIBC__)
    malloc_trim(0);
#endif
}

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("D2K_MEM");
        return e && e[0] == '1';
    }();
    return on;
}

void checkpoint(const char* label) {
    if (!enabled()) return;
    const double mb = 1.0 / 1048576.0;
    const std::uint64_t now = footprint();
    SDL_Log("Memory [%s]: footprint %.0f MB (peak %.0f MB)", label, double(now) * mb, double(peakFootprint()) * mb);
    for (int c = 0; c < int(Cat::Count); ++c)
        if (const std::int64_t b = get(Cat(c))) SDL_Log("Memory [%s]:   %-22s %8.1f MB", label, name(Cat(c)), double(b) * mb);
}

std::int64_t textureBytes(int w, int h, double bytesPerTexel, int levels) {
    double total = 0;
    for (int l = 0; l < std::max(1, levels); ++l) {
        total += double(std::max(1, w)) * double(std::max(1, h)) * bytesPerTexel;
        if (w <= 1 && h <= 1) break;
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return std::int64_t(total);
}

}  // namespace d2k::render::mem
