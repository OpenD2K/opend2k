#pragma once
// Flyers in 3D (docs/3d-mode.md "Flyers"): the ground a flyer's altitude is measured from. Render only.
//
// The simulation flies carryalls / ornithopters / frigates at the original's 2D altitude (Unit::posZ low
// byte, 60 px cruising) over a flat map. Over the 3D relief (Reimagined cliffs up to ~72 px) the plain
// terrain height under the flyer's centre would let a big model (a carryall with a harvester under it)
// sink into a cliff next to it and jump up / down by a cliff's height at every cliff edge. Instead the
// altitude is measured from a "flight ground": the highest terrain around the flyer, where terrain
// farther than kFlightFootprint counts lower by kFlightGlide px per px - a glide slope, so the flyer
// climbs before a cliff and comes down after it. The samples sit on a fixed world lattice (not around
// the flyer), so the result moves continuously with the flyer; terms enter at a distance where they
// cannot be the maximum yet (kFlightRelief), so nothing pops. Low down (landing on / lifting off a drop or
// pickup cell, the sim's 10 px) it blends to the plain ground under the centre, so the flyer still touches
// down exactly where the 2D game puts it.
// Cruising height (owner's report: flyers bobbed up and down at every hill): above the glide slope's
// local ground the flyer is held at one map-wide level, flightCeiling = the 98th percentile of the
// terrain heights (cached per terrain), so a cruising flyer keeps the same height over sand and
// plateaus alike; the glide slope only matters over the rare spikes higher than that. Landing and
// lift-off still blend down to the plain ground from 50 px sim altitude.
#include "game/world.hpp"
#include "render3d/terrain.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace d2k::render3d {

inline constexpr float kFlightFootprint = 36.0f;  // px: a carryall + cargo's half size and a margin
inline constexpr float kFlightGlide = 0.8f;       // px of climb per px of approach
inline constexpr float kFlightRelief = 100.0f;    // px: more than any terrain's height range
inline constexpr float kFlightLattice = 16.0f;    // px: sample spacing (world-fixed)

// The flight ground at world px (gx, gy) for a flyer `alt` px above it (the carrier's altitude for a
// carried unit / cargo, so both use the same ground).
// The map-wide cruising floor: the 98th percentile of the terrain height (cell corners and centres),
// computed once per built terrain (rebuilt when the terrain object or its vertex count changes).
inline float flightCeiling(const Terrain& t) {
    static const Terrain* cachedFor = nullptr;
    static std::size_t cachedVerts = 0;
    static float cached = 0;
    if (cachedFor == &t && cachedVerts == t.vertices().size()) return cached;
    std::vector<float> hs;
    const int w = t.cellsW(), h = t.cellsH();
    hs.reserve(std::size_t(std::max(0, w * h * 2)));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            hs.push_back(t.height(float(x) * 32.0f, float(y) * 32.0f));
            hs.push_back(t.height(float(x) * 32.0f + 16.0f, float(y) * 32.0f + 16.0f));
        }
    float v = 0;
    if (!hs.empty()) {
        const std::size_t k = std::min(hs.size() - 1, hs.size() * 98 / 100);
        std::nth_element(hs.begin(), hs.begin() + std::ptrdiff_t(k), hs.end());
        v = hs[k];
    }
    cachedFor = &t, cachedVerts = t.vertices().size(), cached = v;
    return v;
}

inline float flightGround(const Terrain& t, float gx, float gy, float alt) {
    const float h0 = t.height(gx, gy);
    // Blend: plain ground at the sim's touch-down height (10 px) .. the flight ground from 50 px up.
    const float k = std::clamp((alt - 10.0f) / 40.0f, 0.0f, 1.0f), w = k * k * (3 - 2 * k);
    if (w <= 0) return h0;
    const float reach = kFlightFootprint + kFlightRelief / kFlightGlide;
    float m = h0;
    const int x0 = int(std::ceil((gx - reach) / kFlightLattice)), x1 = int(std::floor((gx + reach) / kFlightLattice));
    const int y0 = int(std::ceil((gy - reach) / kFlightLattice)), y1 = int(std::floor((gy + reach) / kFlightLattice));
    for (int iy = y0; iy <= y1; ++iy)
        for (int ix = x0; ix <= x1; ++ix) {
            const float px = float(ix) * kFlightLattice, py = float(iy) * kFlightLattice;
            const float d = std::hypot(px - gx, py - gy);
            if (d > reach) continue;
            const float v = t.height(px, py) - kFlightGlide * std::max(0.0f, d - kFlightFootprint);
            m = std::max(m, v);
        }
    m = std::max(m, flightCeiling(t));  // cruise at one level (see the header)
    return h0 + w * (m - h0);
}

// A unit drawn as a flyer (flags 0x140: flying 0x40 or carried 0x100): its altitude (the sim's low byte)
// and the ground it is measured from. A carried unit hangs 10 px under its carryall (dragCargo): it
// uses the carryall's altitude for the ground, so the two stay together.
inline float flyerAltitude(const game::Unit& u) { return float(game::fixToPixel(u.posZ) & 0xff); }
inline float flyerGround(const Terrain& t, const game::Unit& u) {
    const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
    const float carrier = flyerAltitude(u) + ((u.flags & 0x40) ? 0.0f : 10.0f);
    return flightGround(t, gx, gy, carrier);
}
// How much higher than the plain terrain under its centre a flyer is drawn (0 for ground units).
inline float flyerLift(const Terrain& t, const game::Unit& u) {
    if (!(u.flags & 0x140)) return 0;
    return flyerGround(t, u) - t.height(float(game::fixToPixel(u.posX)), float(game::fixToPixel(u.posY)));
}

}  // namespace d2k::render3d
