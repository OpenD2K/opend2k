#pragma once
// The original route finder: FindPath 0x45eaf0 (a Bresenham line walker with two-sided
// wall following, not A*) and SmoothPath 0x45fcc0. See re/notes/movement.md §3.2-3.3.
//
// The work buffers mirror the game's globals (path A @0x6d1928, path B @0x6d3870, each
// 1000 (x,y) pairs; exits @0x6d5bb8). Returned paths point into these buffers, exclude
// the start cell, and stay valid until the next findPath call. SmoothPath edits the
// returned path in place, as the original does.

#include <array>
#include <functional>

namespace d2k::game {

struct PathPoint {
    int x = 0, y = 0;
    bool operator==(const PathPoint&) const = default;
};

enum PathStatus : int { kPathReached = 1, kPathTraceReached = 2, kPathEnclosed = 3, kPathFailed = 4 };

// Cell test = CanUnitUseSquare(x, y, unit, side, mode) bound to a unit and mode.
using Passable = std::function<bool(int x, int y)>;

struct PathResult {
    PathPoint* path = nullptr;  // into the finder's buffers
    int len = 0;
    int status = kPathFailed;  // 1 reached, 3 enclosed, 4 partial / failed
    int bestDist = 0;          // Chebyshev distance of the path end to the target
};

class PathFinder {
public:
    static constexpr int kMax = 1000;

    PathResult findPath(int sx, int sy, int tx, int ty, const Passable& passable);

    // SmoothPath 0x45fcc0. `mode0` must test with route mode 0 (Path_Straighten always
    // does, confirmed in asm `push 0`). `path`/`len` are updated in place.
    void smoothPath(int sx, int sy, PathPoint*& path, int& len, const Passable& mode0);

private:
    friend struct SaveAccess;  // savegame.cpp
    struct Buf {
        std::array<PathPoint, kMax + 2> p{};  // +2: the original may write one entry past 999
        int cnt = 0, st = 0;
    };
    struct Bresenham {
        int inc1 = 0, inc2 = 0, ax = 0, ay = 0, bx = 0, by = 0, err = 0;
        void init(int x, int y, int tx, int ty);
        PathPoint step(PathPoint p);
    };

    void scanLine(PathPoint s, PathPoint t, const Passable& pass);
    static bool closest(const Buf& b, int cnt, PathPoint c, PathPoint t, int& best, int* stOut, int& idx);
    void dualTrace(PathPoint p, PathPoint w, int d, PathPoint s, const Passable& pass, int& rA, int& rB);
    static int singleTrace(PathPoint p, PathPoint x, int d, Buf& b, PathPoint s, int side, const Passable& pass);
    PathResult pickPartial(bool fa, bool fb, int dA, int dB, int iA, int iB, PathPoint s);

    Buf a_, b_;
    std::array<PathPoint, kMax + 2> exits_{};
    int nExits_ = 0;
};

}  // namespace d2k::game
