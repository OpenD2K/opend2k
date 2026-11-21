#pragma once
// Track marks in 3D (docs/3d-mode.md "Track marks"): the trails of ground vehicles (render::Effects::Tread
// nodes) drawn as continuous imprint strips, one per track / tyre band of the vehicle, with a relief
// texture (a normal map: grousers, tyre lugs, the groove floor and the sand pushed up along its edges) lit
// by the sun as a multiply-2x decal (Renderer3D kRelief). Render only.
//
// The bands come from the vehicle model's running gear (render3d/model_drive.hpp DriveContact: its ground
// wheels and track loops; the skeleton wheels of a glTF) or, without a model, from a table by unit name.

#include "render3d/model_drive.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace d2k::render3d {

enum TrackPattern : std::uint8_t {
    kTrackTread,    // tank tracks: a flat floor with grouser ridges across
    kTrackTyre,     // small tyres: a chevron lug pattern
    kTrackBalloon,  // wide balloon tyres (harvester, MCV): a rounded floor with broad diagonal lugs
    kTrackScuff,    // turned on the spot: smeared, sand shoved out to the sides
    kTrackPatterns
};

struct TrackContact {
    struct Band {
        float lateral = 0, halfWidth = 0;  // world px; lateral + = right of the heading
    };
    std::vector<Band> bands;
    TrackPattern pattern = kTrackTread;
    float period = 8;  // world px of trail per texture repeat
    float depth = 1;   // strength of the imprint (heavier vehicles press deeper)
};

// The bands of a model's ground contacts (model px = world px; lateral = model x): overlapping contacts
// merge (a track loop and its road wheels, the tyres in line on one side). False = none.
bool trackContactFromGear(const std::vector<DriveContact>& contacts, TrackContact& out);
// Without a model: a plausible layout by the unit's template name (tracked / wheeled, its size).
TrackContact fallbackTrackContact(std::string_view unitName);

// The relief texture: kTrackPatterns columns side by side, one repeat (period) high; RGBA8 per texel
// r, g = the normal's x (across the band, + = right) and y (along it, + = forward) at 0.5 + n / 2,
// b = tone (0.5 = the ground's own colour; below darkens: pressed sand), a = coverage (soft ragged edges).
// The u span of a column covers kTrackSpan band half-widths either side of the band's centre line.
inline constexpr float kTrackSpan = 1.6f;
struct TrackTexture {
    int w = 0, h = 0;
    std::vector<std::vector<std::uint32_t>> levels;  // mip chain, level 0 first
};
TrackTexture buildTrackTexture();
void trackColumnU(TrackPattern p, int texW, float& u0, float& u1);

}  // namespace d2k::render3d
