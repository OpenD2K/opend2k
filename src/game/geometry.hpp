#pragma once
// Direction and tile-step helpers of the original game (re/notes/movement.md §0).
// Facing runs 0..31 clockwise from north (0 N, 8 E, 16 S, 24 W). Pure integer maths.

#include <cstdlib>

namespace d2k::game {

// Atan4096 0x49e160: integer atan, 4096 units per circle (polynomial coefficients @0x4e95b0).
inline int atan4096(int a, int b) {
    if (a == 0 && b == 0) return 0;
    int q = 0;
    if (a < 0) { q = 1; a = -a; }
    if (b < 0) { q += 2; b = -b; }
    const int t = (b < a) ? (b << 15) / a : (a << 15) / b;
    const int t2 = t * t >> 15, t3 = t2 * t >> 15, t4 = t3 * t >> 15, t5 = t4 * t >> 15;
    int r = (109 * t2 >> 15) + (10429 * t >> 15) - (4290 * t3 >> 15) + (2246 * t4 >> 15) - (300 * t5 >> 15);
    if (b < a) r = 0x4000 - r;
    const int u = ((r >> 4) - 0x800) & 0xfff;
    switch (q) {
    case 0: return u;
    case 1: return 0xfff - u;
    case 2: return (0x7ff - u) & 0xfff;
    default: return (u - 0x800) & 0xfff;
    }
}

// GetFacing 0x45dfe0: 32-way facing from (x1,y1) towards (x2,y2), rounded to nearest.
inline int getFacing(int x1, int y1, int x2, int y2) {
    return ((atan4096(x1 - x2, y2 - y1) + 0x40) & 0xfff) >> 7;
}

// GetDirectionTo / GetDir8 0x45e020: GetFacing truncated (not rounded) to a multiple of 4.
inline int getDir8(int x1, int y1, int x2, int y2) { return getFacing(x1, y1, x2, y2) & ~3; }

// GetBestAngle 0x45e0e0: exact 8-way direction used by the path tracer (dx = x2-x1,
// dy = y1-y2, north positive). Diagonal only when |dx| == |dy|. The original DebugFatals
// on identical points; we return 0.
inline int getBestAngle(int x1, int y1, int x2, int y2) {
    const int dx = x2 - x1, dy = y1 - y2;
    if (dx >= 0 && dy >= 0) return dy <= dx ? (dx == dy ? 4 : 8) : 0;
    if (dx >= 0) return -dy <= dx ? (dx == -dy ? 12 : 8) : 16;
    if (dy <= 0) return -dy <= -dx ? (dx == dy ? 20 : 24) : 16;
    return dy <= -dx ? (dy == -dx ? 28 : 24) : 0;
}

// GetNextSquareInDirection 0x45de30. Only multiples of 4 move; others leave the cell as is.
inline void stepInDirection(int& x, int& y, int dir) {
    if (dir == 0 || dir == 4 || dir == 28) --y;
    if (dir == 4 || dir == 8 || dir == 12) ++x;
    if (dir == 12 || dir == 16 || dir == 20) ++y;
    if (dir == 20 || dir == 24 || dir == 28) --x;
}

// RotateDir45 0x45deb0.
inline int rotateDir45(int d, bool clockwise) { return clockwise ? (d + 4) & 31 : (d - 4) & 31; }

// Chebyshev distance (used throughout the path code).
inline int chebyshev(int x1, int y1, int x2, int y2) {
    const int dx = std::abs(x1 - x2), dy = std::abs(y1 - y2);
    return dx > dy ? dx : dy;
}

}  // namespace d2k::game
