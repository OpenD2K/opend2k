#include "render/weld_fx.hpp"

#include "formats/templates.hpp"
#include "game/world.hpp"
#include "render/art_pack.hpp"
#include "render/effects.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace d2k::render {

namespace {

std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

}  // namespace

bool weldFlash(const game::World& w, const game::Building& b, const formats::Templates& t, int anchors, WeldFlash& out) {
    if (anchors <= 0 || !b.alive || b.state == 17 || b.type < 0 || b.type >= formats::kMaxBuildingTypes) return false;
    const std::uint32_t f = b.flags;
    if ((f & 0x6) || !(f & 0x30) || !(f & 0x40)) return false;  // build-up / selling; not animating
    const auto& bt = t.bin.buildings[b.type];
    if (b.health * 2 < static_cast<std::int32_t>(bt.hitPoints)) return false;
    const int a = bt.buildingAnimation;
    if (a >= formats::kMaxBuildingTypes) return false;
    // The overlay frame as MapRenderer::drawBuilding picks it (speed of template index a).
    const int k = b.animCounter / std::max(1, int(t.bin.buildings[a].animationSpeed));
    if (k <= 0 || k >= int(t.bin.buildingAnimationFrames[a])) return false;  // 0 = the rest frame
    const std::uint32_t seed = mix(std::uint32_t(b.side) * 1009u + std::uint32_t(b.index) * 7919u + 1u);
    // The arc jumps to another anchor with every overlay frame and strobes tick by tick.
    out.anchor = int(mix(seed ^ (std::uint32_t(k) * 0x9e3779b9u)) % std::uint32_t(anchors));
    const std::uint32_t h = mix(seed + w.gameTicks() * 0x85ebca6bu);
    if ((h & 7u) == 0) return false;  // a short gap between arcs
    out.a = 0.55f + 0.45f * float((h >> 8) & 255u) / 255.0f;
    out.size = 0.8f + 0.4f * float((h >> 16) & 255u) / 255.0f;
    return true;
}

SDL_Texture* makeWeldGlowTexture(SDL_Renderer* r) {
    constexpr int N = 64;
    std::vector<std::uint32_t> px(std::size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (float(x) + 0.5f) / (N * 0.5f) - 1, dy = (float(y) + 0.5f) / (N * 0.5f) - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            float a = std::exp(-(d / 0.16f) * (d / 0.16f)) + 0.55f * std::exp(-(d / 0.45f) * (d / 0.45f));
            a *= std::clamp((1.0f - d) * 4.0f, 0.0f, 1.0f);
            const std::uint32_t A = std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[std::size_t(y) * N + std::size_t(x)] = (A << 24) | 0xFFFFFFu;  // ABGR8888: white, alpha
        }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, N, N);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), N * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

void PackAnchors::load(const ArtPack* pack, const formats::Templates& t) {
    types_.clear();
    unitTypes_.clear();
    if (!pack) return;
    struct Section {
        float scale = 1, yawDeg = 0, y = 0;
        std::vector<Point> pts;  // glb coordinates (x east, y up, z south; tiles) in dx / z / dy
        std::vector<Point> lights;  // fx_light* (vehicles), the same layout
        float dy2d = 0;  // vehicles: px the rendered frames draw the model origin below the unit position
    };
    std::map<std::string, Section> secs;
    {
        std::ifstream in(std::filesystem::path(pack->root()) / "models.ini");
        if (!in) return;
        std::string line, section;
        while (std::getline(in, line)) {
            if (const auto c = line.find_first_of(";#"); c != std::string::npos) line.erase(c);
            line = trim(line);
            if (line.empty()) continue;
            if (line[0] == '[') {
                section = trim(line.substr(1, line.find(']') - 1));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos || section.empty()) continue;
            std::string k = trim(line.substr(0, eq));
            const std::string val = trim(line.substr(eq + 1));
            for (char& c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
            Section& s = secs[section];
            if (k == "scale") s.scale = std::strtof(val.c_str(), nullptr);
            else if (k == "yaw") s.yawDeg = std::strtof(val.c_str(), nullptr);
            else if (k == "y") s.y = std::strtof(val.c_str(), nullptr);
            else if (k == "anchor2d_dy") s.dy2d = std::strtof(val.c_str(), nullptr);
            else if (k.rfind("anchor.fx_weld", 0) == 0) {
                Point p;
                p.name = k.substr(7);
                if (std::sscanf(val.c_str(), "%f %f %f %f", &p.dx, &p.z, &p.dy, &p.size) >= 3) s.pts.push_back(p);
            } else if (k.rfind("anchor.fx_light", 0) == 0) {
                Point p;
                p.name = k.substr(7);
                if (std::sscanf(val.c_str(), "%f %f %f %f", &p.dx, &p.z, &p.dy, &p.size) >= 3) s.lights.push_back(p);
            }
        }
    }
    const float e = 45.0f * 3.14159265f / 180.0f, se = std::sin(e), ce = std::cos(e);
    types_.resize(std::size_t(formats::kMaxBuildingTypes));
    bool any = false;
    for (int i = 0; i < int(t.bin.buildingCount) && i < formats::kMaxBuildingTypes; ++i) {
        std::string key(t.bin.buildingNames[i], strnlen(t.bin.buildingNames[i], sizeof t.bin.buildingNames[i]));
        for (char& c : key) c = c == ' ' ? '_' : char(std::toupper(static_cast<unsigned char>(c)));
        const auto it = secs.find(key);
        if (it == secs.end() || it->second.pts.empty()) continue;
        const auto& bt = t.bin.buildings[i];
        if (bt.buildingArt < 0 || bt.buildingArt >= formats::kMaxBuildingArt) continue;
        const int first = t.buildingArtFirstFrame[std::size_t(bt.buildingArt)];
        if (!pack->hasFrame(first + 1) && !pack->hasFrame(first)) continue;  // drawn with the original art
        // The glb origin: the occupied cells' centre on the ground (render_pack.py, footprint()).
        int n = 0, sx = 0, sy = 0;
        for (int q = 0; q < 16; ++q)
            if (bt.tilesOccupiedAll & (1u << q)) ++n, sx += q % 4, sy += q / 4;
        const float ox = n ? (float(sx) / float(n) + 0.5f) * 32 : float(bt.artWidth) * 0.5f;
        const float oz = n ? (float(sy) / float(n) + 0.5f) * 32 : float(bt.artHeight) * 0.5f;
        const Section& s = it->second;
        const float yr = s.yawDeg * 3.14159265f / 180.0f, cy = std::cos(yr), sn = std::sin(yr);
        for (Point p : s.pts) {
            // models.ini adjust: scale, yaw (clockwise from above: east -> south), lift.
            const float X = p.dx * s.scale, Y = p.z * s.scale + s.y, Z = p.dy * s.scale;
            const float x = X * cy - Z * sn, z = X * sn + Z * cy;
            // Orthographic, looking north 45 degrees down: screen y = ground y * sin - height * cos.
            p.dx = ox + 32.0f * x;
            p.dy = oz + 32.0f * z * se;
            p.z = 32.0f * Y * ce;
            types_[std::size_t(i)].push_back(p);
            any = true;
        }
        std::sort(types_[std::size_t(i)].begin(), types_[std::size_t(i)].end(),
                  [](const Point& a, const Point& b) { return a.name < b.name; });
    }
    if (!any) types_.clear();
    else SDL_Log("Art pack: fx_weld anchors from models.ini");
    // Vehicles drawn from the pack (their first frame is the pack's, with its own hotspot): fx_light* lamps.
    unitTypes_.resize(std::size_t(formats::kMaxUnitTypes));
    bool anyUnit = false;
    auto keyOf = [](const char* s, std::size_t n) {
        std::string k(s, strnlen(s, n));
        for (char& c : k) c = c == ' ' ? '_' : char(std::toupper(static_cast<unsigned char>(c)));
        return k;
    };
    for (int i = 0; i < int(t.bin.unitCount) && i < formats::kMaxUnitTypes; ++i) {
        const auto& ut = t.bin.units[i];
        if (ut.isInfantry || ut.unitArt < 0 || std::size_t(ut.unitArt) >= t.unitArtFirstFrame.size()) continue;
        const int first = t.unitArtFirstFrame[std::size_t(ut.unitArt)];
        int hx = 0, hy = 0;
        if (!pack->hotspot(first, hx, hy)) continue;  // drawn with the original art
        auto it = secs.find(keyOf(t.bin.unitNames[i], sizeof t.bin.unitNames[i]));
        if ((it == secs.end() || it->second.lights.empty()) && ut.unitGroup < formats::kMaxUnitTypes)
            it = secs.find(keyOf(t.bin.unitGroupNames[ut.unitGroup], sizeof t.bin.unitGroupNames[ut.unitGroup]));
        if (it == secs.end() || it->second.lights.empty()) continue;
        const Section& s = it->second;
        const float yr = s.yawDeg * 3.14159265f / 180.0f, cy = std::cos(yr), sn = std::sin(yr);
        for (const Point& p : s.lights) {
            const float X = p.dx * s.scale, Y = p.z * s.scale + s.y, Z = p.dy * s.scale;
            unitTypes_[std::size_t(i)].push_back(UnitPoint{p.name, X * cy - Z * sn, Y, X * sn + Z * cy, p.size, s.dy2d});
            anyUnit = true;
        }
        std::sort(unitTypes_[std::size_t(i)].begin(), unitTypes_[std::size_t(i)].end(),
                  [](const UnitPoint& a, const UnitPoint& b) { return a.name < b.name; });
    }
    if (!anyUnit) unitTypes_.clear();
    else SDL_Log("Art pack: vehicle fx_light anchors from models.ini");
}

bool PackAnchors::unitAnchors(const game::World& w, int side, int unit, std::string_view prefix,
                              std::vector<EffectAnchor>& out) const {
    out.clear();
    if (unitTypes_.empty() || prefix.rfind("fx_light", 0) != 0 || side < 0 || side >= int(w.sides().size())) return false;
    const auto& sd = w.sides()[std::size_t(side)];
    if (unit < 0 || unit >= int(sd.units.size())) return false;
    const game::Unit& u = sd.units[std::size_t(unit)];
    if (!u.alive || u.type < 0 || u.type >= int(unitTypes_.size()) || unitTypes_[std::size_t(u.type)].empty()) return false;
    float facing = float(u.facing & 31);
    if (fx_)
        if (const Effects::UnitVisual* v = fx_->unitVisual(side, unit)) facing = v->facing;
    // The model turns clockwise seen from above (render_pack / blender_render set_angle; the 3D yawM).
    const float a = facing * (2 * 3.14159265f / 32), c = std::cos(a), sn = std::sin(a);
    const float e = 45.0f * 3.14159265f / 180.0f, se = std::sin(e), ce = std::cos(e);
    const float ux = float(u.posX) / 65536.0f, uy = float(u.posY) / 65536.0f;
    // Flyers are drawn their flight height higher (MapRenderer::drawUnit).
    const float lift = (u.flags & 0x140) ? float(game::fixToPixel(u.posZ) & 0xff) : 0.0f;
    for (const UnitPoint& p : unitTypes_[std::size_t(u.type)])
        if (p.name.compare(0, prefix.size(), prefix) == 0) {
            const float x = p.x * c - p.z * sn, z = p.x * sn + p.z * c;
            // Orthographic, looking north 45 degrees down: screen y = ground y * sin - height * cos.
            out.push_back(EffectAnchor{ux + 32.0f * x, uy + p.dy2d + 32.0f * z * se, 32.0f * p.y * ce + lift, p.size});
        }
    return !out.empty();
}

bool PackAnchors::buildingAnchors(const game::World& w, int side, int building, std::string_view prefix,
                                  std::vector<EffectAnchor>& out) const {
    out.clear();
    if (types_.empty() || prefix.rfind("fx_weld", 0) != 0 || side < 0 || side >= int(w.sides().size())) return false;
    const auto& sd = w.sides()[std::size_t(side)];
    if (building < 0 || building >= int(sd.buildings.size())) return false;
    const game::Building& b = sd.buildings[std::size_t(building)];
    if (b.type < 0 || b.type >= int(types_.size())) return false;
    for (const Point& p : types_[std::size_t(b.type)])
        if (p.name.compare(0, prefix.size(), prefix) == 0)
            out.push_back(EffectAnchor{float(b.tileX * 32) + p.dx, float(b.tileY * 32) + p.dy, p.z, p.size});
    return !out.empty();
}

}  // namespace d2k::render
