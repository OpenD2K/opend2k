#pragma once
// D2K_FRAME_LOG=FILE (performance work): one line per frame with the frame's CPU time, its wall time
// (incl. present / vsync), the model ticks it ran, and the time spent in named sections
// (FrameProbe::Scope). Off (one bool test per scope) without the variable.

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>

namespace d2k::render {

struct FrameProbe {
    static constexpr int kMax = 48;
    struct Section {
        const char* name = nullptr;
        Uint64 ns = 0;
    };
    static FrameProbe& get() {
        static FrameProbe p;
        return p;
    }
    bool on = false;
    std::array<Section, kMax> sections{};
    int count = 0;
    int ticks = 0;  // model ticks this frame

    void add(const char* name, Uint64 ns) {
        for (int i = 0; i < count; ++i)
            if (sections[std::size_t(i)].name == name) {
                sections[std::size_t(i)].ns += ns;
                return;
            }
        if (count < kMax) sections[std::size_t(count++)] = {name, ns};
    }
    void reset() {
        for (int i = 0; i < count; ++i) sections[std::size_t(i)].ns = 0;
        ticks = 0;
    }
    struct Scope {
        const char* name;
        Uint64 t0;
        explicit Scope(const char* n) : name(n), t0(FrameProbe::get().on ? SDL_GetTicksNS() : 0) {}
        ~Scope() {
            if (t0) FrameProbe::get().add(name, SDL_GetTicksNS() - t0);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
};

}  // namespace d2k::render
