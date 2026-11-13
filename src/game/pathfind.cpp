// FindPath 0x45eaf0 and SmoothPath 0x45fcc0, reimplemented from the decompiled code
// (re/ghidra/decomp/0045eaf0_FUN_0045eaf0.c and helpers; re/notes/movement.md §3.2-3.3).
// Branch order and buffer bookkeeping follow the decompilation exactly, including quirks.

#include "game/pathfind.hpp"

#include "game/geometry.hpp"

namespace d2k::game {

namespace {
int cheb(PathPoint a, PathPoint b) { return chebyshev(a.x, a.y, b.x, b.y); }
PathPoint stepDir(PathPoint p, int dir) {
    stepInDirection(p.x, p.y, dir);
    return p;
}
}  // namespace

// Bresenham_Init 0x45fd40: x-major when dy <= dx (this includes exact diagonals); sign(0) = +1.
void PathFinder::Bresenham::init(int x, int y, int tx, int ty) {
    const int ux = tx - x, uy = ty - y;
    const int dx = ux < 0 ? -ux : ux, dy = uy < 0 ? -uy : uy;
    const int sx = ux < 0 ? -1 : 1, sy = uy < 0 ? -1 : 1;
    ax = ay = bx = by = 0;
    if (dy <= dx) {
        inc1 = dy * 2;
        inc2 = dy * 2 - dx * 2;
        err = inc1 - dx;
        ay = sy;  // added only when err >= 0
        bx = sx;  // always added
    } else {
        inc1 = dx * 2;
        inc2 = dx * 2 - dy * 2;
        err = inc1 - dy;
        ax = sx;
        by = sy;
    }
}

// Bresenham_Step 0x45fe20.
PathPoint PathFinder::Bresenham::step(PathPoint p) {
    if (err >= 0) {
        err += inc2;
        return {p.x + ax + bx, p.y + ay + by};
    }
    err += inc1;
    return {p.x + bx, p.y + by};
}

// Path_ScanLineExits 0x45e960: the first passable cell after every blocked run on the line
// S->T. T itself counts only when it ends a blocked run and is passable.
void PathFinder::scanLine(PathPoint s, PathPoint t, const Passable& pass) {
    nExits_ = 0;
    Bresenham br;
    br.init(s.x, s.y, t.x, t.y);
    PathPoint p = s;
    for (;;) {
        do {
            p = br.step(p);
            if (p == t) return;
        } while (pass(p.x, p.y));
        // (the last free cell before the run is only recorded for debug drawing)
        do {
            p = br.step(p);
            if (p == t) {
                if (pass(p.x, p.y) && nExits_ < kMax) exits_[nExits_++] = p;
                return;
            }
        } while (!pass(p.x, p.y));
        if (nExits_ < kMax) exits_[nExits_++] = p;
    }
}

// Path_ClosestToTarget 0x45e890. Scans b.p[0..cnt) (from the buffer start, not from st).
// `stOut` is written only on improvement, with the index after the last occurrence of c so
// far (loops through c are trimmed). Returns whether anything beat Cheb(c, t).
bool PathFinder::closest(const Buf& b, int cnt, PathPoint c, PathPoint t, int& best, int* stOut, int& idx) {
    const int init = cheb(c, t);
    best = init;
    idx = 0;
    int last = 0;
    for (int i = 0; i < cnt; ++i) {
        if (b.p[i] == c) last = i + 1;
        const int d = cheb(b.p[i], t);
        if (d < best) {
            if (stOut) *stOut = last;
            best = d;
            idx = i;
        }
    }
    return best != init;
}

// Path_DualWallTrace 0x45e3b0. A sweeps clockwise (obstacle on its left), B anticlockwise;
// both advance in lockstep and the first within one cell of W wins. Results: 2 reached,
// 3 enclosed, 4 failed (the loser of a race gets 4).
void PathFinder::dualTrace(PathPoint p, PathPoint w, int d, PathPoint s, const Passable& pass, int& rA, int& rB) {
    PathPoint a = p, b = p;
    int dA = d, dB = d, nearA = 0, nearB = 0;
    for (;;) {
        PathPoint ca, cb;
        int tries = 0;
        do {
            dA = rotateDir45(dA, true);
            ca = stepDir(a, dA);
            if (++tries > 8) { rA = rB = kPathEnclosed; return; }
        } while (!pass(ca.x, ca.y) && ca != s);
        tries = 0;
        do {
            dB = rotateDir45(dB, false);
            cb = stepDir(b, dB);
            if (++tries > 8) { rA = rB = kPathEnclosed; return; }
        } while (!pass(cb.x, cb.y) && cb != s);
        a_.p[a_.cnt++] = ca;
        if (a_.cnt > 999) { rA = rB = kPathFailed; return; }
        b_.p[b_.cnt++] = cb;
        if (b_.cnt > 999) { rA = rB = kPathFailed; return; }
        if (ca == s) a_.st = a_.cnt;
        if (cb == s) b_.st = b_.cnt;
        if (cheb(ca, w) < 2) {
            if (pass(w.x, w.y)) {
                a_.p[a_.cnt++] = w;
                rA = kPathTraceReached;
                rB = kPathFailed;
            } else {
                rA = rB = kPathFailed;
            }
            return;
        }
        if (cheb(cb, w) < 2) {
            if (pass(w.x, w.y)) {
                b_.p[b_.cnt++] = w;
                rB = kPathTraceReached;
                rA = kPathFailed;
            } else {
                rA = rB = kPathFailed;
            }
            return;
        }
        if (cheb(ca, p) < 2 && ++nearA > 8) { rA = rB = kPathFailed; return; }
        if (cheb(cb, p) < 2 && ++nearB > 8) { rA = rB = kPathFailed; return; }
        dA = rotateDir45(rotateDir45(rotateDir45(dA, false), false), false);
        dB = rotateDir45(rotateDir45(rotateDir45(dB, true), true), true);
        a = ca;
        b = cb;
    }
}

// Path_SingleWallTrace 0x45e1a0: one side of the dual trace (side 0 = A rule, 1 = B rule).
int PathFinder::singleTrace(PathPoint p, PathPoint x, int d, Buf& b, PathPoint s, int side, const Passable& pass) {
    PathPoint c = p;
    int near = 0;
    for (;;) {
        PathPoint n;
        int tries = 0;
        do {
            d = rotateDir45(d, side == 0);
            n = stepDir(c, d);
            if (++tries > 8) return kPathEnclosed;
        } while (!pass(n.x, n.y) && n != s);
        b.p[b.cnt++] = n;
        if (b.cnt > 999) return kPathFailed;
        if (n == s) b.st = b.cnt;
        if (cheb(n, x) < 2) {
            if (!pass(x.x, x.y)) return kPathFailed;
            b.p[b.cnt++] = x;
            return kPathTraceReached;
        }
        if (cheb(n, p) < 2 && ++near > 8) return kPathFailed;
        d = rotateDir45(rotateDir45(rotateDir45(d, side != 0), side != 0), side != 0);
        c = n;
    }
}

// The "best partial path" selection shared by the target-blocked branch and the final
// fallback of FindPath (labels LAB_0045f6ed / LAB_0045f911 / LAB_0045f889 ...).
PathResult PathFinder::pickPartial(bool fa, bool fb, int dA, int dB, int iA, int iB, PathPoint s) {
    PathResult r;
    r.status = kPathFailed;
    auto useA = [&] { r.bestDist = dA; r.len = iA - a_.st + 1; r.path = &a_.p[a_.st]; };
    auto useB = [&] { r.bestDist = dB; r.len = iB - b_.st + 1; r.path = &b_.p[b_.st]; };
    if (!fa) {
        if (fb) useB();
        else {
            r.len = 0;
            a_.p[0] = s;  // returns [S]
            r.path = &a_.p[0];
        }
    } else if (!fb) {
        useA();
    } else if (dA != dB) {
        if (dB <= dA) useB(); else useA();
    } else {
        if (iB - b_.st <= iA - a_.st) useB(); else useA();
    }
    return r;
}

PathResult PathFinder::findPath(int sx, int sy, int tx, int ty, const Passable& pass) {
    const PathPoint S{sx, sy}, T{tx, ty};
    int wp = 0;
    a_.st = a_.cnt = b_.st = b_.cnt = 0;
    PathResult res;
    res.status = kPathFailed;
    scanLine(S, T, pass);

    PathPoint cur = S;
    PathPoint lastP = S;  // local_20 in the decompilation: last passable cell walked (or W)
    auto failure = [&] {  // DebugFatal paths ("Error with route - full", "Problem with routefind")
        PathResult r;
        r.status = kPathFailed;
        r.len = 0;
        a_.p[0] = S;
        r.path = &a_.p[0];
        return r;
    };

    int guard = 0;
    do {
        // Not in the original: a cap on outer iterations so a pathological map cannot hang
        // us (the original would spin forever there). Never reached on real maps.
        if (++guard > 4 * kMax) return failure();
        Bresenham br;
        br.init(cur.x, cur.y, T.x, T.y);
        PathPoint p = cur, n;
        for (;;) {
            n = br.step(p);
            if (!pass(n.x, n.y)) break;
            a_.p[a_.cnt++] = n;
            b_.p[b_.cnt++] = n;
            lastP = n;
            int dA, dB, iA, iB;
            if (a_.cnt > 999) {  // overflow: only reachable on degenerate maps
                if (b_.cnt < 1000) {
                    closest(b_, b_.cnt, S, T, dB, &b_.st, iB);
                    res.bestDist = dB;
                    res.len = b_.cnt - b_.st;
                    res.path = &b_.p[b_.st];
                    return res;
                }
                const bool fa = closest(a_, a_.cnt, S, T, dA, &a_.st, iA);
                const bool fb = closest(b_, b_.cnt, S, T, dB, &b_.st, iB);
                auto useA = [&] { res.bestDist = dA; res.len = a_.cnt - a_.st; res.path = &a_.p[a_.st]; };
                auto useB = [&] { res.bestDist = dB; res.len = b_.cnt - b_.st; res.path = &b_.p[b_.st]; };
                if (fa) {
                    if (!fb) useA();
                    else if (dA == dB) { if (iB - b_.st <= iA - a_.st) useB(); else useA(); }
                    else if (dB <= dA) useB();
                    else useA();
                } else if (!fb) {
                    res.bestDist = dB;
                    res.len = 0;
                    b_.p[0] = S;
                    res.path = &b_.p[0];
                } else {
                    useB();
                }
                return res;
            }
            if (b_.cnt > 999) {
                closest(a_, a_.cnt, S, T, dA, &a_.st, iA);
                res.bestDist = dA;
                res.len = a_.cnt - a_.st;
                res.path = &a_.p[a_.st];
                return res;
            }
            if (n == T) {
                res.status = kPathReached;
                res.bestDist = 0;
                if (b_.cnt - b_.st < a_.cnt - a_.st) {
                    res.len = b_.cnt - b_.st;
                    res.path = &b_.p[b_.st];
                } else {
                    res.len = a_.cnt - a_.st;
                    res.path = &a_.p[a_.st];
                }
                return res;
            }
            p = n;
        }

        if (n == T) {  // the target itself is blocked: best partial path
            int dA, dB, iA, iB;
            const bool fa = closest(a_, a_.cnt, S, T, dA, &a_.st, iA);
            const bool fb = closest(b_, b_.cnt, S, T, dB, &b_.st, iB);
            return pickPartial(fa, fb, dA, dB, iA, iB, S);
        }

        PathPoint w = wp + 1 <= nExits_ ? exits_[wp] : T;
        ++wp;
        const int d = getBestAngle(p.x, p.y, n.x, n.y);
        const int cntA0 = a_.cnt, stA0 = a_.st, cntB0 = b_.cnt, stB0 = b_.st;
        int rA = 0, rB = 0;
        dualTrace(p, w, d, S, pass, rA, rB);
        PathPoint next = w;
        if (rA == kPathTraceReached || rB == kPathTraceReached) lastP = w;

        if (rA == kPathFailed && rB == kPathFailed) {
            // The trace failed: maybe it already passed a later exit; re-trace one side to it.
            // Decision (UNCERTAIN in movement.md): the decompilation resets cnt to the count
            // saved before the dual trace (cntA0/cntB0), not to the matching index, then
            // single-traces from p to the exit again; the scan continues over the new entries.
            bool found = false;
            for (int j = wp; j < nExits_ && !found; ++j) {
                const PathPoint e = exits_[j];
                for (int k = cntA0; k < a_.cnt; ++k)  // a_.cnt is re-read: the original rescans
                    if (a_.p[k] == e) {
                        a_.cnt = cntA0;
                        a_.st = stA0;
                        rA = singleTrace(p, e, d, a_, S, 0, pass);
                        found = true;
                        next = lastP = e;
                    }
                for (int k = cntB0; k < b_.cnt; ++k)
                    if (b_.p[k] == e) {
                        b_.cnt = cntB0;
                        b_.st = stB0;
                        rB = singleTrace(p, e, d, b_, S, 1, pass);
                        found = true;
                        next = lastP = e;
                    }
                if (found) wp = j + 1;
            }
            if (!found) {
                int dA, dB, iA, iB, dummy = 0;
                bool fa = closest(a_, a_.cnt, p, T, dA, &dummy, iA);
                bool fb = closest(b_, b_.cnt, p, T, dB, &dummy, iB);
                a_.st = stA0;
                b_.st = stB0;
                if (!fa && !fb) {  // no point on either trace beats p
                    fa = closest(a_, a_.cnt, S, lastP, dA, &a_.st, iA);
                    fb = closest(b_, b_.cnt, S, lastP, dB, &b_.st, iB);
                    return pickPartial(fa, fb, dA, dB, iA, iB, S);
                }
                --wp;  // retry the same exit later
                bool chooseB;
                if (!fa) chooseB = true;
                else if (!fb) chooseB = false;
                else if (dA == dB) chooseB = iB < iA;
                else chooseB = !(dA <= dB);
                if (chooseB) {
                    rB = kPathTraceReached;
                    next = b_.p[iB];
                    b_.cnt = iB + 1;
                } else {
                    rA = kPathTraceReached;
                    next = a_.p[iA];
                    a_.cnt = iA + 1;
                }
            }
        }

        if (rA == kPathTraceReached || rB == kPathTraceReached) {
            bool winB;
            if (rA == kPathTraceReached && rB == kPathTraceReached) winB = b_.cnt - b_.st < a_.cnt - a_.st;
            else winB = rB == kPathTraceReached;
            Buf& win = winB ? b_ : a_;
            Buf& other = winB ? a_ : b_;
            const int len = win.cnt - win.st;
            for (int i = 0; i < len; ++i) win.p[i] = win.p[win.st + i];
            for (int i = 0; i < len; ++i) other.p[i] = win.p[i];
            a_.st = b_.st = 0;
            a_.cnt = b_.cnt = len;
        } else if (rA == kPathEnclosed && rB == kPathEnclosed) {
            PathResult r;
            r.status = kPathEnclosed;
            int idx = 0;
            closest(a_, a_.cnt, S, T, r.bestDist, &a_.st, idx);
            r.len = 0;
            b_.p[0] = S;
            r.path = &b_.p[0];
            return r;
        } else {
            return failure();
        }
        cur = next;
    } while (cur != T);

    res.status = kPathReached;
    res.bestDist = 0;
    if (b_.cnt - b_.st < a_.cnt - a_.st) {
        res.len = b_.cnt - b_.st;
        res.path = &b_.p[b_.st];
    } else {
        res.len = a_.cnt - a_.st;
        res.path = &a_.p[a_.st];
    }
    return res;
}

namespace {

// Path_RemoveLoops 0x45f970: removes the first loop path[i..j-1] (i <= 11) where
// path[i] == path[j]; the array start moves forward by j-i.
PathPoint* removeLoops(PathPoint* path, int& len) {
    if (len < 2) return path;
    for (int i = 0; i < len;) {
        for (int j = i + 1; j < len; ++j)
            if (path[i] == path[j]) {
                const int cut = j - i;
                len -= cut;
                for (int k = i - 1; k >= 0; --k) path[k + cut] = path[k];
                return path + cut;
            }
        if (++i > 11) return path;
    }
    return path;
}

// Path_CutCorners 0x45fa10.
PathPoint* cutCorners(PathPoint s, PathPoint* path, int& len) {
    if (len < 3) return path;
    if (cheb(s, path[1]) < 2) {
        ++path;
        if (--len < 3) return path;
    }
    int k = 0;
    for (int iter = 0; iter < 12; ++iter) {
        if (cheb(path[k], path[k + 2]) < 2) {
            for (int m = k; m >= 0; --m) path[m + 1] = path[m];  // delete path[k+1]
            ++path;
            if (--len < 3) return path;
        } else {
            ++k;
        }
        if (len <= k + 2) return path;
    }
    return path;
}

// Path_Straighten 0x45fb00: turns 2-step dog-legs into straight lines (always mode 0).
void straighten(PathPoint s, PathPoint* path, int len, bool& changed, const Passable& pass) {
    changed = false;
    if (len < 2) return;
    {
        PathPoint& b = path[0];
        const PathPoint c = path[1];
        const int dx = s.x - c.x;
        if ((dx == 2 || dx == -2) && s.y == c.y && s.y != b.y) {
            if (pass(s.x < b.x ? s.x + 1 : s.x - 1, s.y)) { b.y = s.y; changed = true; }
        } else if (dx == 0 && (s.y - c.y == 2 || s.y - c.y == -2) && s.x != b.x) {
            if (pass(s.x, s.y < b.y ? s.y + 1 : s.y - 1)) { b.x = s.x; changed = true; }
        }
    }
    if (len <= 2) return;
    for (int k = 0, iter = 0;;) {
        const PathPoint a = path[k];
        PathPoint& b = path[k + 1];
        const PathPoint c = path[k + 2];
        const int dx = a.x - c.x;
        if ((dx == 2 || dx == -2) && a.y == c.y && a.y != b.y) {
            if (pass(a.x < b.x ? a.x + 1 : a.x - 1, a.y)) { b.y = a.y; changed = true; }
        } else if (dx == 0 && (a.y - c.y == 2 || a.y - c.y == -2) && a.x != b.x) {
            if (pass(a.x, b.y <= a.y ? a.y - 1 : a.y + 1)) { b.x = a.x; changed = true; }
        }
        ++k;
        if (!(k + 2 < len && ++iter < 12)) break;
    }
}

}  // namespace

void PathFinder::smoothPath(int sx, int sy, PathPoint*& path, int& len, const Passable& mode0) {
    const PathPoint s{sx, sy};
    int n = len;
    bool again;
    do {
        path = removeLoops(path, len);
        again = n != len;
        n = len;
    } while (again);
    bool changed;
    do {
        do {
            n = len;
            path = cutCorners(s, path, len);
            straighten(s, path, len, changed, mode0);
        } while (n != len);
    } while (changed);
}

}  // namespace d2k::game
