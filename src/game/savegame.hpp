#pragma once
// Save games: a versioned binary snapshot of the complete mutable World state plus the session
// metadata the frontend needs to rebuild the static parts (map, mission, tileset, rules).
// Pure logic: no SDL / OS calls (the frontend does the file I/O).
//
// File layout (all integers little-endian):
//   "D2KSAVE\0"  8 bytes magic
//   u32          format version (kSaveVersion)
//   u32 + bytes  metadata section (SaveMeta, see writeMeta)
//   u32 + bytes  world section (saveWorldState)
//   u64          FNV-1a 64 of everything before it
//
// Loading a game: the frontend constructs the World exactly as for a new game (same map,
// mission, rules and setup: that restores everything static such as Rules pointers and the
// default VARS), then loadWorldState() overwrites every mutable field. The world section is a
// field-by-field dump: every struct has one io() visitor in savegame.cpp that lists its
// fields, shared by saving, loading and a compile-time field counter. Static assertions
// compare that count with the struct's real member count (aggregates) and pin sizeof (libc++,
// 64-bit), so adding a member to Unit/Side/World/... without updating savegame.cpp fails to
// compile. Determinism round trips (tests/game_test.cpp, --simtest) check the result.

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2k::game {

class World;

inline constexpr char kSaveMagic[8] = {'D', '2', 'K', 'S', 'A', 'V', 'E', '\0'};
inline constexpr std::uint32_t kSaveVersion = 7;  // 2: cheat state (World::cheats_); 3: Side::autoQueue;
                                                  // 4: community extended events (World::ext_, tags);
                                                  // 5: cheat reinforcements, rally points, Order payload
                                                  // 6: play as any side (World::scriptSide_, SaveMeta::playAsSide)
                                                  // 7: campaign-map saves (SaveMeta::kind, missionNumber);
                                                  //    version 6 files still load (same world section)
inline constexpr std::uint32_t kOldestSaveVersion = 6;

struct SaveError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Little-endian byte writer / bounds-checked reader.
class SaveWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) { le(v, 2); }
    void u32(std::uint32_t v) { le(v, 4); }
    void u64(std::uint64_t v) { le(v, 8); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f32(float v);
    void bytes(const void* p, std::size_t n);
    void str(const std::string& s);
    const std::vector<std::uint8_t>& data() const { return buf_; }
    std::vector<std::uint8_t>& data() { return buf_; }

private:
    void le(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    std::vector<std::uint8_t> buf_;
};

class SaveReader {
public:
    explicit SaveReader(std::span<const std::uint8_t> d) : d_(d) {}
    std::uint8_t u8() { need(1); return d_[pos_++]; }
    std::uint16_t u16() { return static_cast<std::uint16_t>(le(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(le(4)); }
    std::uint64_t u64() { return le(8); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    float f32();
    void bytes(void* p, std::size_t n);
    std::string str();
    std::span<const std::uint8_t> take(std::size_t n);  // sub-range, advances
    std::size_t pos() const { return pos_; }
    bool atEnd() const { return pos_ == d_.size(); }

private:
    void need(std::size_t n) const {
        if (d_.size() - pos_ < n) throw SaveError("save file truncated");
    }
    std::uint64_t le(int n) {
        need(static_cast<std::size_t>(n));
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= std::uint64_t(d_[pos_++]) << (8 * i);
        return v;
    }
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
};

// What a slot holds. The original's save header (FUN_004414d0, 0xd8 bytes) has an "in battle"
// byte: UISaveGame FUN_00474e20 called from the territory map (GameState != 2) writes only the
// header and the campaign globals (MissionNumber, MySideID, GameType, gDifficultyLevel, the
// sound track and the options), no world; loading one returns to the territory map
// (re/notes/campaign-flow.md section 2.1).
enum class SaveKind : std::uint8_t { Battle = 0, CampaignMap = 1 };

// What the frontend needs to recreate the session (mirrors platform::GameStart) plus display
// data for the slot list and the camera.
struct SaveMeta {
    SaveKind kind = SaveKind::Battle;
    // CampaignMap: MissionNumber (1..9) of the territory map; the house is campaignHouse (0..2)
    // and gDifficultyLevel is difficulty. The territory owners are not stored: CreateGlobe
    // 0x42bc60 derives them from the house and MissionNumber (game/campaign.hpp globeState).
    int missionNumber = 0;
    std::string name;           // shown in the slot list (mission title)
    std::int64_t savedAt = 0;   // unix time (seconds), set by the frontend
    std::string map;            // e.g. "H5V1.MAP"
    std::string tileset;        // tileset override ("" = from the mission / MP map table)
    bool skirmish = false;
    int playerHouse = 0, aiPlayers = 1, techLevel = 7;
    std::uint32_t startingCredits = 5000;
    int unitCount = 10, worms = 1, difficulty = 1;
    int campaignHouse = -1, missionIndex = -1;  // where the menu returns after the game
    int playerSide = 0;
    int playAsSide = -1;  // GameStart::playAsSide ("Play as any side"; -1 = the mission's own side)
    float cameraX = 0, cameraY = 0, cameraZoom = 1;
    std::uint32_t gameTicks = 0;  // informational (also in the world section)
    // The frontend's on-screen mission message log (text already resolved, shown until `until`).
    struct Message {
        std::string text;
        std::uint32_t until = 0;
    };
    std::vector<Message> messages;
};

// The world section alone (also the input of the state hash).
std::vector<std::uint8_t> saveWorldState(const World& w);
// Overwrites every mutable field of `w` (constructed for the same map/mission/setup).
// Throws SaveError on malformed data or a map size mismatch.
void loadWorldState(World& w, std::span<const std::uint8_t> data);
// FNV-1a 64 of saveWorldState(w): equal hashes = equal simulation state.
std::uint64_t worldStateHash(const World& w);
std::uint64_t fnv1a64(std::span<const std::uint8_t> data);

// Whole save file.
std::vector<std::uint8_t> writeSaveFile(const SaveMeta& meta, const World& w);
// A campaign-map save (meta.kind = CampaignMap): the same file with an empty world section.
std::vector<std::uint8_t> writeCampaignSaveFile(const SaveMeta& meta);
// Header + metadata only (slot lists); throws SaveError.
SaveMeta readSaveMeta(std::span<const std::uint8_t> file);
// Metadata and the world section (a sub-span of `file`); throws SaveError (bad magic,
// unsupported version, checksum mismatch, truncation).
SaveMeta readSaveFile(std::span<const std::uint8_t> file, std::span<const std::uint8_t>& worldSection);

}  // namespace d2k::game
