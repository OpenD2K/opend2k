#pragma once
// Campaign progression outside the battles (re/notes/campaign-flow.md):
// - the campaign table (UI_DATA/campaign.uib = gCampaignTable 0x795708, 3 houses x 10 records
//   x 0x104 bytes, filled by CUIManager__LoadCampaignInformation 0x477790 from Campaign.txt in
//   the development build),
// - the territory map ("globe": CreateGlobe 0x42bc60, reveal order FUN_0042c6b0, hit test
//   FUN_0042d020 / FUN_0042cf10, region polygons at 0x4be3f4),
// - the single-player score (FUN_0043ec70 targets, FUN_0043f030 counting, rank FUN_0043fc00,
//   high-score table FUN_0043fc90 / GameSave\hs.dat).
// Pure logic: no SDL, no files.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::game {

constexpr int kCampaignHouses = 3;
constexpr int kCampaignRecords = 10;  // record 0 = initial territories, 1..9 = missions
constexpr int kRegionCount = 27;
constexpr int kNoOwner = 3;

// One 0x104-byte record (offsets into the record).
struct CampaignRecord {
    std::array<int, 2> region{};          // +0x00/+0x01 territory (1..27, 0 = none) of variant v1 / v2
    std::array<std::string, 2> mission;   // +0x02/+0x07 "a1v1" / "a1v2" ("" when region is 0)
    std::array<int, 2> arrowDir{};        // +0x0C/+0x10 North..NorthWest = 0..7 (unused by the game)
    std::array<int, 2> pointerX{}, pointerY{};  // +0x14/+0x18, +0x1C/+0x20 (0,0 = region centre)
    std::array<std::array<int, 3>, 3> changes{};  // +0x24/+0x27/+0x2A territories that go to A/H/O
    float scoreMultiplier = 1.0f;         // +0x30 (Campaign.txt column 17 * 0.01)
    std::array<int, 3> revealHouse{};     // +0x34/+0x38/+0x3C order the new territories appear in
    std::array<std::string, 3> revealSound;  // +0x40/+0x7C/+0xB8 speech per reveal group ("NONE")
    std::string music;                    // +0xF4 track of the mission
    int enemyHouse = 0;                   // +0x100 house of the score screen's enemy bars
};

struct CampaignTable {
    std::array<std::array<CampaignRecord, kCampaignRecords>, kCampaignHouses> rec{};
    bool loaded = false;
    const CampaignRecord& at(int house, int index) const;  // index clamped to 0..9
    // campaign.uib bytes (3 x 10 x 0x104); throws std::runtime_error when too short.
    static CampaignTable parse(std::span<const std::uint8_t> data);
};

// Territory shapes (0x4be3f4, 0x48 bytes each: blit x, y of Regions\<h><n>.BMP, point count,
// up to 7 POINTs) in 640x400 screen coordinates.
struct RegionShape {
    int x = 0, y = 0;
    std::vector<std::array<int, 2>> poly;
};
const std::array<RegionShape, kRegionCount>& regionShapes();
// The pointer position used when a record has (0,0): polygon centroid - (16, 32).
std::array<int, 2> regionPointerPos(int region);
// PtInRegion of CreatePolygonRgn(ALTERNATE) for region 1..27.
bool pointInRegion(int region, int x, int y);

// CreateGlobe 0x42bc60 for the player's house and MissionNumber n (1..9): owners after
// records 0..n-2, then record n-1 applied as "fresh" (shown appearing one by one).
struct GlobeState {
    std::array<int, kRegionCount> owner{};      // 0..2 house, kNoOwner = no picture
    std::array<int, kRegionCount> ownerBefore{}; // owner before the fresh changes
    std::array<bool, kRegionCount> fresh{};
};
GlobeState globeState(const CampaignTable& t, int house, int missionNumber);

// FUN_0042c6b0: the fresh territories in the order they appear: first those now owned by
// record[n].revealHouse[0] (lowest index first), then [1], then [2]. The first territory of
// each group plays record[n].revealSound[group] unless it is "NONE".
struct RevealStep {
    int region = 0;       // 1..27
    std::string sound;    // "" = none
};
std::vector<RevealStep> revealOrder(const CampaignTable& t, const GlobeState& g, int house, int missionNumber);

// The territory (1..27) under (x, y) that can be attacked next (record[n].region[0/1]), or 0;
// `variant` gets 0 (v1) / 1 (v2).
int choosableRegionAt(const CampaignTable& t, int house, int missionNumber, int x, int y, int* variant = nullptr);

// ---- Score --------------------------------------------------------------------------------

struct ScoreSide {
    bool participates = false;  // CSide+0x24250
    int house = 0;              // CSide+0x24253
    int unitsLost = 0;          // +0x24a08
    int buildingsLost = 0;      // +0x24a0c
    int spiceHarvested = 0;     // +0x2477c
    // Used by the skirmish/multiplayer result page (FUN_0043dcb0) only.
    int unitsKilled = 0;        // +0x24c90
    int buildingsKilled = 0;    // +0x24c94
    int colour = 0;             // NetPlayersColor (skirmish colour index 0..7)
};

struct ScoreInput {
    std::array<ScoreSide, 8> sides{};
    int mySide = 0;
    int myHouse = 0;
    bool lost = false;       // gLose: our numbers are shown halved
    int missionNumber = 1;   // MissionNumber when the score screen opens (+1 after a win)
};

// FUN_0043ec70: per category (units, buildings, spice) the counter targets, out of 150/100/50.
// Ours: units (U - mine) * 150 / U with U = units lost by all sides (likewise buildings);
// spice mine * 50 / S. The enemy gets the rest (150 - ours ...), 0 when nothing happened.
struct ScoreTargets {
    std::array<int, 3> ours{}, theirs{};
};
constexpr std::array<int, 3> kScoreCategoryMax = {150, 100, 50};  // DAT_004dedc4/c8/cc
ScoreTargets scoreTargets(const ScoreInput& in);

// FUN_0043f030: one call per drawn frame. Counts units, then buildings, then spice (ours and
// theirs together, +1 per call each until the target; S_SCORETICK queued on every counting
// call), pausing 2 s (GetTickCount) after the units and after the buildings, and reports done
// once everything reached its target. Displayed numbers are ftol(counter * multiplier), ours
// divided by 2 after a lost mission; the totals are refreshed on every counting call: ours =
// the sum of our three (already halved) numbers divided by 2 again (original quirk), theirs
// is never halved. There is no skip: Continue simply leaves the page.
class ScoreCounter {
public:
    ScoreCounter() = default;
    ScoreCounter(const ScoreInput& in, float multiplier);
    // One FUN_0043f030 call at time `nowMs`; returns true when S_SCORETICK is queued.
    bool step(std::uint32_t nowMs);
    bool done() const { return done_; }        // DAT_00516270
    int category() const;                      // 0 units, 1 buildings, 2 spice, 3 all counted
    int counter(bool ours, int category) const { return ours ? our_[category] : their_[category]; }
    const ScoreTargets& targets() const { return targets_; }
    int shown(bool ours, int category) const;  // the number in the text box
    int ourTotal() const { return ourTotal_; }      // DAT_00516264
    int theirTotal() const { return theirTotal_; }  // DAT_00516268
    // Bar widths in pixels (0..100): counter * 100 / category max (the bar image is clipped).
    int barWidth(bool ours, int category) const;
    float multiplier() const { return mult_; }
    int divisor() const { return divisor_; }

private:
    void updateTotals();
    ScoreTargets targets_;
    std::array<int, 3> our_{}, their_{};
    float mult_ = 1.0f;
    int divisor_ = 1;
    bool paused_ = false, done_ = false;
    std::uint32_t pauseStart_ = 0;
    int ourTotal_ = 0, theirTotal_ = 0;
};

// FUN_0043fc00: rating 0..11 (text "Rank1".."Rank12").
int scoreRank(int score);

struct HighScore {
    bool used = false;
    std::string name;
    int rank = 0, house = 0, mission = 0, score = 0;
};
// gHighScores 0x515bb8: 6 rows of 0xdc bytes are shown (slot1..slot6); GameSave\hs.dat
// persists 0x44c bytes = the first 5 (we keep all 6).
constexpr int kHighScores = 6;
// FUN_0043fc90: inserts before the first free or lower entry; returns its index or -1.
// mission = missionNumber - 1 (1 when that is 0), house = the player's house.
int insertHighScore(std::array<HighScore, kHighScores>& table, int score, int house, int missionNumber);

}  // namespace d2k::game
