#include "game/campaign.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace d2k::game {

namespace {

constexpr std::size_t kRecordSize = 0x104;

std::int32_t rd32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24));
}

std::string rdStr(const std::uint8_t* p, std::size_t max) {
    std::size_t n = 0;
    while (n < max && p[n]) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// Region table 0x4be3f4 (stride 0x48): blit position of Regions\<h><n>.BMP and the outline.
const std::array<RegionShape, kRegionCount> kShapes = {{
    {1, 72, {{{0, 74}}, {{64, 73}}, {{85, 121}}, {{54, 165}}, {{0, 175}}}},
    {1, 166, {{{0, 175}}, {{37, 169}}, {{70, 234}}, {{49, 270}}, {{0, 271}}}},
    {1, 245, {{{0, 271}}, {{49, 270}}, {{63, 247}}, {{122, 281}}, {{111, 327}}, {{0, 328}}}},
    {34, 136, {{{38, 169}}, {{55, 165}}, {{73, 139}}, {{121, 147}}, {{102, 269}}, {{63, 247}}, {{70, 235}}}},
    {63, 71, {{{64, 73}}, {{180, 73}}, {{194, 131}}, {{120, 147}}, {{73, 139}}, {{86, 121}}}},
    {112, 130, {{{120, 147}}, {{194, 131}}, {{221, 149}}, {{217, 208}}, {{113, 201}}}},
    {101, 198, {{{113, 202}}, {{217, 209}}, {{209, 261}}, {{122, 281}}, {{101, 268}}}},
    {107, 256, {{{123, 281}}, {{209, 261}}, {{219, 327}}, {{111, 327}}}},
    {178, 72, {{{181, 73}}, {{313, 73}}, {{221, 149}}, {{194, 131}}}},
    {206, 111, {{{221, 149}}, {{267, 113}}, {{304, 175}}, {{271, 217}}, {{214, 232}}, {{217, 208}}}},
    {203, 210, {{{214, 232}}, {{269, 216}}, {{286, 269}}, {{280, 327}}, {{220, 327}}, {{209, 261}}}},
    {276, 71, {{{313, 73}}, {{396, 73}}, {{394, 114}}, {{369, 119}}, {{283, 99}}}},
    {264, 101, {{{282, 100}}, {{369, 119}}, {{371, 156}}, {{324, 189}}, {{305, 175}}, {{267, 113}}}},
    {263, 171, {{{305, 175}}, {{364, 218}}, {{351, 276}}, {{286, 268}}, {{271, 217}}}},
    {275, 267, {{{286, 269}}, {{384, 280}}, {{470, 327}}, {{280, 327}}}},
    {313, 109, {{{369, 119}}, {{395, 114}}, {{440, 186}}, {{364, 218}}, {{325, 189}}, {{371, 156}}}},
    {347, 188, {{{364, 217}}, {{440, 188}}, {{449, 239}}, {{413, 295}}, {{384, 280}}, {{352, 276}}}},
    {391, 73, {{{396, 73}}, {{471, 73}}, {{482, 132}}, {{411, 138}}, {{395, 114}}}},
    {402, 115, {{{410, 138}}, {{518, 130}}, {{519, 181}}, {{444, 206}}, {{441, 188}}}},
    {435, 178, {{{445, 207}}, {{519, 181}}, {{572, 196}}, {{566, 227}}, {{507, 245}}, {{449, 239}}}},
    {406, 234, {{{449, 239}}, {{507, 245}}, {{505, 327}}, {{468, 327}}, {{412, 295}}}},
    {470, 73, {{{471, 73}}, {{572, 73}}, {{555, 127}}, {{483, 132}}}},
    {517, 126, {{{517, 130}}, {{555, 127}}, {{639, 142}}, {{639, 172}}, {{573, 195}}, {{520, 181}}}},
    {563, 168, {{{573, 196}}, {{639, 172}}, {{639, 244}}, {{611, 259}}, {{566, 288}}}},
    {501, 227, {{{508, 246}}, {{567, 228}}, {{611, 260}}, {{573, 290}}, {{571, 327}}, {{506, 327}}}},
    {547, 73, {{{572, 73}}, {{639, 73}}, {{639, 141}}, {{555, 127}}}},
    {563, 242, {{{573, 290}}, {{611, 260}}, {{638, 244}}, {{639, 327}}, {{570, 327}}}},
}};

// The four ways CreateGlobe writes owners from one record: the per-house change lists, then
// the two mission territories go to the player's house.
template <class F>
void forEachChange(const CampaignRecord& r, int house, F&& f) {
    for (int h = 0; h < 3; ++h)
        for (int reg : r.changes[h])
            if (reg > 0 && reg <= kRegionCount) f(reg - 1, h);
    for (int reg : r.region)
        if (reg > 0 && reg <= kRegionCount) f(reg - 1, house);
}

}  // namespace

const CampaignRecord& CampaignTable::at(int house, int index) const {
    return rec[std::clamp(house, 0, kCampaignHouses - 1)][std::clamp(index, 0, kCampaignRecords - 1)];
}

CampaignTable CampaignTable::parse(std::span<const std::uint8_t> data) {
    if (data.size() < kRecordSize * kCampaignHouses * kCampaignRecords)
        throw std::runtime_error("campaign.uib: too short");
    CampaignTable t;
    for (int h = 0; h < kCampaignHouses; ++h)
        for (int m = 0; m < kCampaignRecords; ++m) {
            const std::uint8_t* p = data.data() + (static_cast<std::size_t>(h) * kCampaignRecords + m) * kRecordSize;
            CampaignRecord& r = t.rec[h][m];
            r.region = {p[0], p[1]};
            r.mission = {rdStr(p + 0x02, 5), rdStr(p + 0x07, 5)};
            r.arrowDir = {rd32(p + 0x0C), rd32(p + 0x10)};
            r.pointerX = {rd32(p + 0x14), rd32(p + 0x1C)};
            r.pointerY = {rd32(p + 0x18), rd32(p + 0x20)};
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < 3; ++i) r.changes[k][i] = p[0x24 + k * 3 + i];
            std::uint32_t bits = static_cast<std::uint32_t>(rd32(p + 0x30));
            std::memcpy(&r.scoreMultiplier, &bits, sizeof bits);
            r.revealHouse = {rd32(p + 0x34), rd32(p + 0x38), rd32(p + 0x3C)};
            r.revealSound = {rdStr(p + 0x40, 0x3C), rdStr(p + 0x7C, 0x3C), rdStr(p + 0xB8, 0x3C)};
            r.music = rdStr(p + 0xF4, 0x0C);
            r.enemyHouse = rd32(p + 0x100);
        }
    t.loaded = true;
    return t;
}

const std::array<RegionShape, kRegionCount>& regionShapes() { return kShapes; }

std::array<int, 2> regionPointerPos(int region) {
    if (region < 1 || region > kRegionCount) return {0, 0};
    const auto& poly = kShapes[region - 1].poly;
    int sx = 0, sy = 0;
    for (const auto& p : poly) {
        sx += p[0];
        sy += p[1];
    }
    const int n = static_cast<int>(poly.size());
    return {sx / n - 16, sy / n - 32};
}

bool pointInRegion(int region, int x, int y) {
    if (region < 1 || region > kRegionCount) return false;
    const auto& poly = kShapes[region - 1].poly;
    // Even-odd rule at the pixel centre (CreatePolygonRgn ALTERNATE fills pixel centres).
    const double px = x + 0.5, py = y + 0.5;
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
        if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

GlobeState globeState(const CampaignTable& t, int house, int missionNumber) {
    GlobeState g;
    g.owner.fill(kNoOwner);
    const int n = std::clamp(missionNumber, 1, kCampaignRecords - 1);
    for (int i = 0; i < n - 1; ++i) forEachChange(t.at(house, i), house, [&](int r, int h) { g.owner[r] = h; });
    g.ownerBefore = g.owner;
    forEachChange(t.at(house, n - 1), house, [&](int r, int h) {
        g.owner[r] = h;
        g.fresh[r] = true;
    });
    return g;
}

std::vector<RevealStep> revealOrder(const CampaignTable& t, const GlobeState& g, int house, int missionNumber) {
    const CampaignRecord& r = t.at(house, std::clamp(missionNumber, 1, kCampaignRecords - 1));
    std::vector<RevealStep> out;
    for (int grp = 0; grp < 3; ++grp) {
        bool first = true;
        for (int i = 0; i < kRegionCount; ++i) {
            if (!g.fresh[i] || g.owner[i] == kNoOwner || g.owner[i] != r.revealHouse[grp]) continue;
            RevealStep s;
            s.region = i + 1;
            if (first && r.revealSound[grp] != "NONE") s.sound = r.revealSound[grp];
            first = false;
            out.push_back(std::move(s));
        }
    }
    return out;
}

int choosableRegionAt(const CampaignTable& t, int house, int missionNumber, int x, int y, int* variant) {
    const CampaignRecord& r = t.at(house, std::clamp(missionNumber, 1, kCampaignRecords - 1));
    for (int v = 0; v < 2; ++v)
        if (r.region[v] != 0 && pointInRegion(r.region[v], x, y)) {
            if (variant) *variant = v;
            return r.region[v];
        }
    return 0;
}

// ---- Score --------------------------------------------------------------------------------

ScoreTargets scoreTargets(const ScoreInput& in) {
    long long u = 0, b = 0, s = 0;
    for (const ScoreSide& sd : in.sides) {
        if (!sd.participates) continue;
        u += static_cast<std::uint32_t>(sd.unitsLost);
        b += static_cast<std::uint32_t>(sd.buildingsLost);
        s += static_cast<std::uint32_t>(sd.spiceHarvested);
    }
    const ScoreSide& me = in.sides[static_cast<std::size_t>(std::clamp(in.mySide, 0, 7))];
    ScoreTargets t;
    // Unsigned 32-bit arithmetic as in the original.
    auto share = [](long long total, std::uint32_t part, int max, bool lossShare) -> int {
        if (total == 0) return 0;
        const std::uint32_t tot = static_cast<std::uint32_t>(total);
        const std::uint32_t num = lossShare ? tot - part : part;
        return static_cast<int>((num * static_cast<std::uint32_t>(max)) / tot);
    };
    t.ours[0] = share(u, static_cast<std::uint32_t>(me.unitsLost), kScoreCategoryMax[0], true);
    t.ours[1] = share(b, static_cast<std::uint32_t>(me.buildingsLost), kScoreCategoryMax[1], true);
    t.ours[2] = share(s, static_cast<std::uint32_t>(me.spiceHarvested), kScoreCategoryMax[2], false);
    t.theirs[0] = u ? kScoreCategoryMax[0] - t.ours[0] : 0;
    t.theirs[1] = b ? kScoreCategoryMax[1] - t.ours[1] : 0;
    t.theirs[2] = s ? kScoreCategoryMax[2] - t.ours[2] : 0;
    return t;
}

ScoreCounter::ScoreCounter(const ScoreInput& in, float multiplier)
    : targets_(scoreTargets(in)), mult_(multiplier), divisor_(in.lost ? 2 : 1) {}

namespace {
// __ftol of (float)counter * multiplier (x87: exact in extended precision, truncated).
int ftolMul(int counter, float m) { return static_cast<int>(static_cast<long double>(counter) * static_cast<long double>(m)); }
}  // namespace

int ScoreCounter::shown(bool ours, int c) const {
    const int v = ftolMul(ours ? our_[c] : their_[c], mult_);
    return ours ? static_cast<int>(static_cast<std::uint32_t>(v) / static_cast<std::uint32_t>(divisor_)) : v;
}

int ScoreCounter::barWidth(bool ours, int c) const {
    return (ours ? our_[c] : their_[c]) * 100 / kScoreCategoryMax[c];
}

int ScoreCounter::category() const {
    for (int c = 0; c < 3; ++c)
        if (our_[c] != targets_.ours[c] || their_[c] != targets_.theirs[c]) return c;
    return 3;
}

void ScoreCounter::updateTotals() {
    theirTotal_ = ftolMul(their_[2], mult_) + ftolMul(their_[1], mult_) + ftolMul(their_[0], mult_);
    int sum = 0;
    for (int c = 0; c < 3; ++c) sum += shown(true, c);
    ourTotal_ = sum / divisor_;
}

bool ScoreCounter::step(std::uint32_t nowMs) {
    if (paused_) {
        const std::int32_t d = static_cast<std::int32_t>(nowMs - pauseStart_);
        if ((d < 0 ? -d : d) < 2000) return false;
    }
    paused_ = false;
    pauseStart_ = nowMs;
    const int c = category();
    if (c == 3) {
        done_ = true;
        return false;
    }
    if (our_[c] != targets_.ours[c]) ++our_[c];
    if (their_[c] != targets_.theirs[c]) ++their_[c];
    // Units and buildings: a 2 s pause once both counters arrived (spice: straight to done).
    if (c < 2 && our_[c] == targets_.ours[c] && their_[c] == targets_.theirs[c]) {
        paused_ = true;
        pauseStart_ = nowMs;
    }
    updateTotals();
    return true;
}

int scoreRank(int score) {
    const std::uint32_t s = static_cast<std::uint32_t>(score);
    if (s > 1349) return 11;
    if (s > 1299) return 10;
    if (s > 1199) return 9;
    if (s > 1099) return 8;
    if (s > 949) return 7;
    if (s > 799) return 6;
    if (s > 649) return 5;
    if (s > 499) return 4;
    if (s > 349) return 3;
    if (s > 299) return 2;
    return s > 249 ? 1 : 0;
}

int insertHighScore(std::array<HighScore, kHighScores>& table, int score, int house, int missionNumber) {
    for (int i = 0; i < kHighScores; ++i) {
        if (table[i].used && static_cast<std::uint32_t>(table[i].score) >= static_cast<std::uint32_t>(score)) continue;
        for (int j = kHighScores - 1; j > i; --j) table[j] = table[j - 1];
        HighScore& e = table[i];
        e.used = true;
        e.name.clear();
        e.rank = std::min(scoreRank(score), 11);
        e.house = std::clamp(house, 0, 3);
        e.mission = missionNumber - 1 == 0 ? 1 : missionNumber - 1;
        e.score = score;
        return i;
    }
    return -1;
}

}  // namespace d2k::game
