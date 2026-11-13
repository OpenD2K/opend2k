#pragma once
// The original game's random number generator: the MSVC C runtime rand()
// (DUNE2000.DAT 0x4b1810, called via GetRandomValue 0x448460 and a thunk).
// Game logic must draw numbers in exactly the original order to stay faithful.

#include <cstdint>

namespace d2k::game {

class Random {
public:
    explicit Random(std::uint32_t seed = 1) : seed_(seed) {}
    void reseed(std::uint32_t seed) { seed_ = seed; }
    std::uint32_t seed() const { return seed_; }

    // 0..0x7FFF, identical to MSVC rand().
    int next() {
        seed_ = seed_ * 0x343FDu + 0x269EC3u;
        return static_cast<int>((seed_ >> 16) & 0x7FFF);
    }

private:
    std::uint32_t seed_;
};

}  // namespace d2k::game
