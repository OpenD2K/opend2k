#include "formats/mis.hpp"

#include "formats/io.hpp"

#include <algorithm>
#include <type_traits>

namespace d2k::formats {

namespace {
constexpr std::size_t kMisOrigSize = 68066;
constexpr std::size_t kNameField = 200;
constexpr std::size_t kMessageData = 692;

std::string fixedString(std::span<const std::uint8_t> s) {
    std::string out;
    for (auto c : s) {
        if (c == 0) break;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

template <std::size_t N>
std::array<std::uint8_t, N> rawRecord(ByteReader& rd) {
    std::array<std::uint8_t, N> r{};
    auto b = rd.bytes(N);
    std::copy(b.begin(), b.end(), r.begin());
    return r;
}
}  // namespace

MisFile parseMis(std::span<const std::uint8_t> data) {
    if (data.size() < kMisOrigSize)
        throw std::runtime_error("MIS: file too small (" + std::to_string(data.size()) + " bytes)");
    ByteReader rd(data);
    MisFile m;
    for (auto& v : m.techLevel) v = rd.u8();
    for (auto& v : m.startingMoney) v = rd.u32();
    rd.bytes(40);  // unknown
    for (auto& v : m.houseId) v = rd.u8();
    for (int i = 0; i < kMaxSides; ++i) m.aiSegments.push_back(rawRecord<kMisAiSegmentSize>(rd));
    for (auto& row : m.allegiance)
        for (auto& v : row) v = rd.u8();

    std::vector<std::array<std::uint8_t, kMisEventSize>> events;
    for (int i = 0; i < kMisOrigEvents; ++i) events.push_back(rawRecord<kMisEventSize>(rd));
    std::vector<std::array<std::uint8_t, kMisConditionSize>> conditions;
    for (int i = 0; i < kMisOrigConditions; ++i) conditions.push_back(rawRecord<kMisConditionSize>(rd));

    m.tileset = fixedString(rd.bytes(kNameField));
    m.tileAttributes = fixedString(rd.bytes(kNameField));
    const int numEvents = std::min<int>(rd.u8(), kMisOrigEvents);
    const int numConditions = std::min<int>(rd.u8(), kMisOrigConditions);
    m.timeLimit = rd.i32();
    rd.bytes(kMessageData);

    if (data.size() >= kMisOrigSize + 4) {
        // extend-event-limits.asm (LoadMission 0x44fdc7): u16 event count, u16 condition count,
        // conditions 48..255 (28 B each), events 64..1023 (72 B each). A short file reads as
        // much as is there (ReadFile semantics); missing records stay zero.
        m.extended = true;
        m.extendedEvents = std::min<int>(rd.u16(), kMisExtMaxEvents);
        m.extendedConditions = std::min<int>(rd.u16(), kMisExtMaxConditions);
        std::vector<std::array<std::uint8_t, kMisConditionSize>> xc(conditions);
        std::vector<std::array<std::uint8_t, kMisEventSize>> xe(events);
        auto take = [&](auto& vec, std::size_t size) {
            typename std::remove_reference_t<decltype(vec)>::value_type r{};
            const std::size_t n = std::min<std::size_t>(size, rd.remaining());
            auto b = rd.bytes(n);
            std::copy(b.begin(), b.end(), r.begin());
            vec.push_back(r);
        };
        for (int i = kMisOrigConditions; i < kMisExtMaxConditions; ++i) take(xc, kMisConditionSize);
        for (int i = kMisOrigEvents; i < kMisExtMaxEvents; ++i) take(xe, kMisEventSize);
        xc.resize(m.extendedConditions);
        xe.resize(m.extendedEvents);
        m.extConditions = std::move(xc);
        m.extEvents = std::move(xe);
    }
    events.resize(numEvents);
    conditions.resize(numConditions);
    m.events = std::move(events);
    m.conditions = std::move(conditions);
    return m;
}

MisFile loadMis(const std::string& path) { return parseMis(readFile(path)); }

}  // namespace d2k::formats
