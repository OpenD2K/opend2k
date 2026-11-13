#pragma once
// The computer player's state (the original CAI object, one per side; re/notes/ai.md).
// The original keeps it as a 7608-byte struct (gAIArray 0x4f4118, stride 0x1db8) whose
// initial contents come straight from the .MIS AI segment. Many fields are only known by
// offset, so the state is kept as raw bytes with typed accessors at the original offsets
// (field names: ref/D2kEditor/config/game_structs.ini [AI], dune2000-sp-patches ai.h).
// Mission events (Mission__CheckEvents) poke some fields directly (0x1e, 0xf14, 0x1d2c).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2k::game {

constexpr std::size_t kAiSize = 7608;

struct AiState {
    std::array<std::uint8_t, kAiSize> raw{};

    template <class T> T get(std::size_t off) const {
        T v;
        std::memcpy(&v, raw.data() + off, sizeof v);
        return v;
    }
    template <class T> void set(std::size_t off, T v) { std::memcpy(raw.data() + off, &v, sizeof v); }
    std::uint8_t& b(std::size_t off) { return raw[off]; }
    std::uint8_t b(std::size_t off) const { return raw[off]; }

    int side() const { return raw[0x00]; }        // AISide
    bool enabled() const { return raw[0x01] != 0; }  // __IsAI ("AI enabled")
};

}  // namespace d2k::game
