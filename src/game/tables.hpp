#pragma once
// Lookup tables of the original model.
//
// Sin/cos table (InitSinCosTables 0x44b090): value[dir][m] = trunc(sin(dir * 6.283185308 / 32)
// * m * 0x800) for 32 directions (0 = north, clockwise) and 512 magnitudes, i.e. a 16.16
// displacement of m/32 pixels. Callers index it as dir * 512 + m. The original computes it
// with the x87 FPU in the MSVC default 53-bit precision mode, so plain double arithmetic
// reproduces it.

#include <cstdint>

namespace d2k::game {

constexpr int kSinCosDirs = 32, kSinCosMags = 512, kSinCosSize = kSinCosDirs * kSinCosMags;

std::int32_t sinTableFlat(int index);  // index = dir * 512 + m; out-of-range -> 0
std::int32_t cosTableFlat(int index);
inline std::int32_t sinTable(int dir, int m) { return sinTableFlat(dir * kSinCosMags + m); }
inline std::int32_t cosTable(int dir, int m) { return cosTableFlat(dir * kSinCosMags + m); }

// Infantry animation {first, last} frame per CurrentAnimType (table 0x4e9530, 16 entries).
inline constexpr int kInfAnim[16][2] = {{0, 0},   {1, 6},   {7, 11},  {12, 12}, {13, 13}, {14, 16},
                                        {17, 21}, {12, 12}, {7, 11},  {22, 33}, {22, 29}, {22, 29},
                                        {22, 29}, {22, 29}, {22, 29}, {22, 29}};
inline int kInfAnimFirst(int t) { return kInfAnim[t & 15][0]; }

}  // namespace d2k::game
