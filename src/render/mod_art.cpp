#include "render/mod_art.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <unordered_map>

namespace d2k::render {

namespace {

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    }
    void i32(std::int32_t v) { bytes(&v, sizeof v); }
};

std::vector<std::uint64_t> hashAll(const formats::R16File& f) {
    std::vector<std::uint64_t> out(f.frames.size());
    for (std::size_t i = 0; i < f.frames.size(); ++i) out[i] = frameHash(f, i);
    return out;
}

// The base install's per-frame hashes by file (path + size + mtime): parsed and hashed once per run.
const std::vector<std::uint64_t>& baseHashes(const std::string& path) {
    static std::mutex m;
    static std::map<std::string, std::vector<std::uint64_t>> cache;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto time = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
    const std::string key = path + "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(time));
    std::lock_guard<std::mutex> lock(m);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    std::vector<std::uint64_t> h;
    try {
        h = hashAll(formats::loadR16(path));
    } catch (const std::exception&) {
        h.clear();
    }
    return cache.emplace(key, std::move(h)).first->second;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Per item of `mod`: the base index with the same hash (the same index preferred), -1 = none.
// A frame equal to the base frame at another index counts as moved only inside a run of at least this
// many consecutive frames with the same shift (frames inserted / removed before it). A lone frame the
// campaign replaced with a copy of another base frame stays the campaign's: the pack's art of that
// other frame was drawn for another object.
constexpr int kMinMovedRun = 8;

std::vector<int> sourceMap(const std::vector<std::uint64_t>& mod, const std::vector<std::uint64_t>& base, int& changed,
                           int& moved) {
    std::unordered_map<std::uint64_t, std::vector<int>> at;  // base hash -> its indices (first few)
    for (std::size_t i = 0; i < base.size(); ++i) {
        auto& v = at[base[i]];
        if (v.size() < 32) v.push_back(int(i));
    }
    const int n = int(mod.size()), bn = int(base.size());
    std::vector<int> src(mod.size(), -1);
    changed = moved = 0;
    for (int i = 0; i < n;) {
        if (i < bn && base[std::size_t(i)] == mod[std::size_t(i)]) {
            src[std::size_t(i)] = i;
            ++i;
            continue;
        }
        int best = -1, bestLen = 0;  // the base run that continues longest from here
        if (const auto it = at.find(mod[std::size_t(i)]); it != at.end())
            for (const int k : it->second) {
                int len = 0;
                while (i + len < n && k + len < bn && mod[std::size_t(i + len)] == base[std::size_t(k + len)]) ++len;
                if (len > bestLen) best = k, bestLen = len;
            }
        if (bestLen < kMinMovedRun) {
            ++changed;
            ++i;
            continue;
        }
        for (int j = 0; j < bestLen; ++j, ++i) {
            const bool same = i < bn && base[std::size_t(i)] == mod[std::size_t(i)];
            src[std::size_t(i)] = same ? i : best + j;
            moved += !same;
        }
    }
    return src;
}


}  // namespace

std::uint64_t frameHash(const formats::R16File& f, std::size_t frame) {
    const formats::R16Frame& fr = f.frames[frame];
    Fnv h;
    h.i32(fr.type);
    if (fr.empty()) return h.h;
    for (int v : {fr.width, fr.height, fr.x, fr.y, fr.bpp, fr.frameWidth, fr.frameHeight}) h.i32(v);
    h.bytes(fr.pixels.data(), fr.pixels.size());
    if (fr.bpp == 8 && fr.palette >= 0 && fr.palette < int(f.palettes.size()))
        h.bytes(f.palettes[std::size_t(fr.palette)].data(), sizeof(formats::Palette));
    return h.h;
}

int ModArt::packTile(const std::string& stem, int tile) const {
    if (tileset.empty() || lower(stem) != lower(tileset)) return tile;
    return tile >= 0 && tile < int(tileSource.size()) ? tileSource[std::size_t(tile)] : -1;
}

ModArt analyseModArt(const ModArtInput& in) {
    ModArt r;
    const bool spritesMod = in.sprites && !in.spritesPath.empty() && in.spritesPath != in.baseSpritesPath;
    const bool templatesMod = in.templates && !in.templatesPath.empty() && in.templatesPath != in.baseTemplatesPath;
    const bool tilesMod = in.tiles && !in.tilesPath.empty() && in.tilesPath != in.baseTilesPath;
    r.tileAtrFromMod = in.tileAtrFromMod;
    r.active = spritesMod || templatesMod || tilesMod;
    if (!r.active) return r;

    // DATA.R16: per frame, the base frame with the same content.
    const std::vector<std::uint64_t>* baseH = nullptr;
    std::vector<std::uint64_t> modH;
    if (!in.baseSpritesPath.empty()) baseH = &baseHashes(in.baseSpritesPath);
    if (spritesMod) {
        modH = hashAll(*in.sprites);
        if (baseH && !baseH->empty()) {
            r.frameSource = sourceMap(modH, *baseH, r.changedFrames, r.movedFrames);
        } else {  // no base DATA.R16 to compare with: all of it is the campaign's
            r.frameSource.assign(modH.size(), -1);
            r.changedFrames = int(modH.size());
        }
        if (r.changedFrames == 0 && r.movedFrames == 0) r.frameSource.clear();  // a copy of the base file
    } else if (baseH) {
        modH = *baseH;
    }
    const std::vector<std::uint64_t>& bH = baseH && !baseH->empty() ? *baseH : modH;

    // Templates.bin: per type, the render-relevant fields and the content of its frames.
    if ((templatesMod || spritesMod) && in.templates) {
        formats::Templates baseLoaded;
        const formats::Templates* baseT = in.templates;
        if (templatesMod) {
            try {
                baseLoaded = formats::loadTemplates(in.baseTemplatesPath);
                baseT = &baseLoaded;
            } catch (const std::exception&) {
                baseT = nullptr;  // every used type counts as the campaign's
            }
        }
        const formats::Templates& M = *in.templates;
        auto framesEq = [&](int mf, int bf, int mn, int bn) {
            if (mn != bn) return false;
            for (int k = 0; k < mn; ++k) {
                const int a = mf + k, b = bf + k;
                const bool aOut = a < 0 || a >= int(modH.size()), bOut = b < 0 || b >= int(bH.size());
                if (aOut || bOut) {
                    if (aOut != bOut) return false;
                    continue;  // missing on both sides
                }
                if (modH[std::size_t(a)] != bH[std::size_t(b)]) return false;
            }
            return true;
        };
        auto unitArtEq = [&](const formats::Templates& B, int ma, int ba) {
            const bool mv = ma >= 0 && ma < M.bin.unitArtCount && ma < formats::kMaxUnitArt;
            const bool bv = ba >= 0 && ba < B.bin.unitArtCount && ba < formats::kMaxUnitArt;
            if (!mv || !bv) return mv == bv;
            return framesEq(M.unitArtFirstFrame[std::size_t(ma)], B.unitArtFirstFrame[std::size_t(ba)],
                            M.bin.unitArtAnimationFrames[ma] * M.bin.unitArtDirectionFrames[ma],
                            B.bin.unitArtAnimationFrames[ba] * B.bin.unitArtDirectionFrames[ba]);
        };
        auto bldArtEq = [&](const formats::Templates& B, int ma, int ba) {
            const bool mv = ma >= 0 && ma < M.bin.buildingArtCount && ma < formats::kMaxBuildingArt;
            const bool bv = ba >= 0 && ba < B.bin.buildingArtCount && ba < formats::kMaxBuildingArt;
            if (!mv || !bv) return mv == bv;
            return framesEq(M.buildingArtFirstFrame[std::size_t(ma)], B.buildingArtFirstFrame[std::size_t(ba)],
                            M.bin.buildingArtDirections[ma] * 2 + 1, B.bin.buildingArtDirections[ba] * 2 + 1);
        };
        auto tableEq = [&](const std::vector<int>& mFirst, const std::uint8_t* mCount, const std::vector<int>& bFirst,
                           const std::uint8_t* bCount, int ma, int ba, int mMax, int bMax) {
            const bool mv = ma >= 0 && ma < mMax, bv = ba >= 0 && ba < bMax;
            if (!mv || !bv) return mv == bv;
            return framesEq(mFirst[std::size_t(ma)], bFirst[std::size_t(ba)], mCount[ma], bCount[ba]);
        };
        // What differs between mod type i and base type j that the art depends on ("" = nothing).
        auto unitDiff = [&](int i, int j) -> std::string {
            const formats::Templates& B = *baseT;
            const auto& mu = M.bin.units[i];
            const auto& bu = B.bin.units[j];
            if (!unitArtEq(B, mu.unitArt, bu.unitArt)) return "art";
            if (!unitArtEq(B, mu.barrelArt, bu.barrelArt)) return "barrel art";
            if (mu.isInfantry != bu.isInfantry || mu.specialBehavior != bu.specialBehavior) return "behaviour";
            if (std::memcmp(mu.directionFrames, bu.directionFrames, sizeof mu.directionFrames) != 0) return "directions";
            return {};
        };
        auto buildingDiff = [&](int i, int j) -> std::string {
            const formats::Templates& B = *baseT;
            const auto& mb = M.bin.buildings[i];
            const auto& bb = B.bin.buildings[j];
            if (mb.artWidth != bb.artWidth || mb.artHeight != bb.artHeight || mb.tilesOccupiedAll != bb.tilesOccupiedAll)
                return "size / footprint";
            if (mb.specialBehavior != bb.specialBehavior) return "behaviour";
            if (std::memcmp(mb.directionFrames, bb.directionFrames, sizeof mb.directionFrames) != 0) return "directions";
            if (!bldArtEq(B, mb.buildingArt, bb.buildingArt)) return "art";
            if (!bldArtEq(B, mb.barrelArt, bb.barrelArt)) return "barrel art";
            if (!tableEq(M.buildupArtFirstFrame, M.bin.buildupArtFrames, B.buildupArtFirstFrame, B.bin.buildupArtFrames,
                         mb.buildupArt, bb.buildupArt, M.bin.buildingCount, B.bin.buildingCount))
                return "build-up art";
            if (!tableEq(M.buildingAnimationFirstFrame, M.bin.buildingAnimationFrames, B.buildingAnimationFirstFrame,
                         B.bin.buildingAnimationFrames, mb.buildingAnimation, bb.buildingAnimation, M.bin.buildingCount,
                         B.bin.buildingCount))
                return "animation art";
            return {};
        };
        // Per mod type: the base type it is (same name and art, else - renamed - the first base type
        // with the same art, preferring the same owner houses), whose name / group the 3D models are
        // looked up by; -1 = the campaign's own art. The group alone never decides (mods reuse groups
        // as a technical trick).
        auto match = [&](int i, int count, int baseCount, auto name, auto owner, auto diff, std::vector<int>& base,
                         std::vector<std::string>& keyName, std::vector<std::string>& keyGroup, auto baseName,
                         auto baseGroup, const char* kind, int& own) {
            for (; i < count; ++i) {
                int j = -1;
                for (int k = 0; k < baseCount && j < 0; ++k)
                    if (baseName(k) == name(i)) j = k;
                std::string why = !baseT ? "no base Templates.bin" : j < 0 ? "new / renamed type" : diff(i, j);
                if (!why.empty() && baseT) {
                    int best = -1;
                    for (int k = 0; k < baseCount; ++k) {
                        if (!diff(i, k).empty()) continue;
                        if (best < 0 || (owner(i) == owner(-1 - k) && owner(i) != owner(-1 - best))) best = k;
                    }
                    if (best >= 0) {
                        r.ownTypes.push_back(std::string(kind) + " " + std::to_string(i) + " " + name(i) + ": " + why +
                                             " - drawn as base " + baseName(best));
                        j = best;
                        why.clear();
                    }
                }
                if (why.empty() && j >= 0) {
                    base[std::size_t(i)] = j;
                    keyName[std::size_t(i)] = baseName(j);
                    keyGroup[std::size_t(i)] = baseGroup(j);
                    continue;
                }
                ++own;
                r.ownTypes.push_back(std::string(kind) + " " + std::to_string(i) + " " + name(i) + ": " + why);
            }
        };
        const int unitCount = std::min<int>(M.bin.unitCount, formats::kMaxUnitTypes);
        const int bldCount = std::min<int>(M.bin.buildingCount, formats::kMaxBuildingTypes);
        const int baseUnits = baseT ? std::min<int>(baseT->bin.unitCount, formats::kMaxUnitTypes) : 0;
        const int baseBlds = baseT ? std::min<int>(baseT->bin.buildingCount, formats::kMaxBuildingTypes) : 0;
        r.unitBase.assign(formats::kMaxUnitTypes, -1);
        r.unitKeyName.assign(formats::kMaxUnitTypes, {});
        r.unitKeyGroup.assign(formats::kMaxUnitTypes, {});
        r.buildingBase.assign(formats::kMaxBuildingTypes, -1);
        r.buildingKeyName.assign(formats::kMaxBuildingTypes, {});
        r.buildingKeyGroup.assign(formats::kMaxBuildingTypes, {});
        auto str = [](const char* s, std::size_t n) { return std::string(s, strnlen(s, n)); };
        match(0, unitCount, baseUnits,
              [&](int i) { return str(M.bin.unitNames[i], sizeof M.bin.unitNames[i]); },
              [&](int i) { return i >= 0 ? int(M.bin.units[i].ownerHouse) : int(baseT->bin.units[-1 - i].ownerHouse); },
              unitDiff, r.unitBase, r.unitKeyName, r.unitKeyGroup,
              [&](int j) { return str(baseT->bin.unitNames[j], sizeof baseT->bin.unitNames[j]); },
              [&](int j) {
                  const int g = baseT->bin.units[j].unitGroup;
                  return g < formats::kMaxUnitTypes ? str(baseT->bin.unitGroupNames[g], sizeof baseT->bin.unitGroupNames[g])
                                                    : std::string();
              },
              "unit", r.ownUnits);
        match(0, bldCount, baseBlds,
              [&](int i) { return str(M.bin.buildingNames[i], sizeof M.bin.buildingNames[i]); },
              [&](int i) {
                  return i >= 0 ? int(M.bin.buildings[i].ownerHouse) : int(baseT->bin.buildings[-1 - i].ownerHouse);
              },
              buildingDiff, r.buildingBase, r.buildingKeyName, r.buildingKeyGroup,
              [&](int j) { return str(baseT->bin.buildingNames[j], sizeof baseT->bin.buildingNames[j]); },
              [&](int j) {
                  const int g = baseT->bin.buildings[j].buildingGroup;
                  return g < formats::kMaxBuildingTypes
                             ? str(baseT->bin.buildingGroupNames[g], sizeof baseT->bin.buildingGroupNames[g])
                             : std::string();
              },
              "building", r.ownBuildings);
        // Identity everywhere (same types, same art, same keys): nothing to remap.
        bool identity = true;
        for (int i = 0; i < unitCount && identity; ++i)
            identity = r.unitBase[std::size_t(i)] == i &&
                       r.unitKeyGroup[std::size_t(i)] == str(M.bin.unitGroupNames[std::min<int>(M.bin.units[i].unitGroup, formats::kMaxUnitTypes - 1)],
                                                             sizeof M.bin.unitGroupNames[0]);
        for (int i = 0; i < bldCount && identity; ++i)
            identity = r.buildingBase[std::size_t(i)] == i &&
                       r.buildingKeyGroup[std::size_t(i)] ==
                           str(M.bin.buildingGroupNames[std::min<int>(M.bin.buildings[i].buildingGroup, formats::kMaxBuildingTypes - 1)],
                               sizeof M.bin.buildingGroupNames[0]);
        if (identity) {
            r.unitBase.clear(), r.unitKeyName.clear(), r.unitKeyGroup.clear();
            r.buildingBase.clear(), r.buildingKeyName.clear(), r.buildingKeyGroup.clear();
        }
    }


    // The tileset: per tile, the base tile of that name with the same pixels.
    if (tilesMod) {
        r.tileset = in.tilesetStem;
        const std::vector<std::uint64_t> modT = hashAll(*in.tiles);
        const std::vector<std::uint64_t>* baseTiles =
            in.baseTilesPath.empty() || in.tilesConverted ? nullptr : &baseHashes(in.baseTilesPath);
        if (baseTiles && !baseTiles->empty()) {
            int moved = 0;
            r.tileSource = sourceMap(modT, *baseTiles, r.changedTiles, moved);
            r.changedTiles += moved;  // a tile moved to another index is drawn there with the base tile's art
        } else {
            r.tileSource.assign(modT.size(), -1);
            r.changedTiles = int(modT.size());
        }
    }

    std::string s = "Mod art: " + std::to_string(r.changedFrames) + " DATA.R16 frames";
    if (r.movedFrames) s += " (+" + std::to_string(r.movedFrames) + " moved)";
    s += ", " + std::to_string(r.ownUnits + r.ownBuildings) + " types (" + std::to_string(r.ownUnits) + " units, " +
         std::to_string(r.ownBuildings) + " buildings) use the campaign's own art";
    s += "; terrain: ";
    if (tilesMod && r.changedTiles == 0)
        s += "tileset " + r.tileset + " from the campaign (identical to the base set)";
    else if (tilesMod)
        s += "tileset " + r.tileset + " from the campaign (" + std::to_string(r.changedTiles) + " of " +
             std::to_string(r.tileSource.size()) + " tiles not in the base set: no art-pack tiles there)";
    else
        s += "base tileset";
    if (r.tileAtrFromMod) s += ", campaign TILEATR";
    s += "; designed terrain follows the mission's own tiles";
    r.summary = s;
    return r;
}

}  // namespace d2k::render
