#include "formats/ps1_sound.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace d2k::formats::ps1 {

namespace {

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

struct SlotName {
    const char* pcFile;  // upper case, no extension
    const char* bank;
    int slot;
};

// Found by matching every slot of the English disc against the PC English AUDs
// (tools/ps1/match_speech.py; docs/ps1-assets.md). Per house bank: 55-66 infantry/vehicle
// select+confirm, 67-72 saboteur (sord), 73-78 engineer, 79-86 Fremen (satr), 87-90 Sardaukar
// (identical in all banks), 107-151 mentat lines in the order GUARD, RUN, 5MIN..1MIN, (game
// loaded/saved: empty), MFAIL ... NROOM, HATTK, ENEMY. satr has only 150 slots, so the
// Atreides HATTK/ENEMY (ids 150/151) play from matr 0/1. Sorted by name.
constexpr SlotName kSlots[] = {
    {"AI_1MIN", "satr", 113}, {"AI_2MIN", "satr", 112}, {"AI_3MIN", "satr", 111}, {"AI_4MIN", "satr", 110},
    {"AI_5MIN", "satr", 109}, {"AI_ABORT", "satr", 118}, {"AI_ATACK", "satr", 138}, {"AI_BDRDY", "satr", 136},
    {"AI_BLOST", "satr", 140}, {"AI_BUILD", "satr", 132}, {"AI_CANCL", "satr", 135}, {"AI_CAPT", "satr", 122},
    {"AI_DHRDY", "satr", 127}, {"AI_DPLOY", "satr", 130}, {"AI_ENEMY", "matr", 1}, {"AI_GANEW", "satr", 125},
    {"AI_GUARD", "satr", 107}, {"AI_HATTK", "matr", 0}, {"AI_HOLD", "satr", 134}, {"AI_LAUNC", "satr", 128},
    {"AI_MEND", "satr", 124}, {"AI_MFAIL", "satr", 116}, {"AI_MONEY", "satr", 121}, {"AI_MWIN", "satr", 117},
    {"AI_NEWOP", "satr", 137}, {"AI_NROOM", "satr", 149}, {"AI_ORDER", "satr", 144}, {"AI_PLACE", "satr", 131},
    {"AI_POWER", "satr", 120}, {"AI_PREP", "satr", 126}, {"AI_PRMRY", "satr", 146}, {"AI_REINF", "satr", 129},
    {"AI_RUN", "satr", 108}, {"AI_SELL", "satr", 123}, {"AI_SILOS", "satr", 119}, {"AI_SPORT", "satr", 142},
    {"AI_TRAIN", "satr", 133}, {"AI_ULOST", "satr", 139}, {"AI_UNRDY", "satr", 141}, {"AI_UPGOP", "satr", 143},
    {"AI_UPGRD", "satr", 145}, {"AI_WATTK", "satr", 148}, {"AI_WSIGN", "satr", 147}, {"A_ECONF1", "satr", 76},
    {"A_ECONF2", "satr", 77}, {"A_ECONF3", "satr", 78}, {"A_ESEL1", "satr", 73}, {"A_ESEL2", "satr", 74},
    {"A_ESEL3", "satr", 75}, {"A_FCONF1", "satr", 83}, {"A_FCONF2", "satr", 84}, {"A_FCONF3", "satr", 85},
    {"A_FCONF4", "satr", 86}, {"A_FSEL1", "satr", 79}, {"A_FSEL2", "satr", 80}, {"A_FSEL3", "satr", 81},
    {"A_FSEL4", "satr", 82}, {"A_ICONF1", "satr", 58}, {"A_ICONF2", "satr", 59}, {"A_ICONF3", "satr", 60},
    {"A_ISEL1", "satr", 55}, {"A_ISEL2", "satr", 56}, {"A_ISEL3", "satr", 57}, {"A_VCONF1", "satr", 64},
    {"A_VCONF2", "satr", 65}, {"A_VCONF3", "satr", 66}, {"A_VSEL1", "satr", 61}, {"A_VSEL2", "satr", 62},
    {"A_VSEL3", "satr", 63}, {"G_SCONF1", "satr", 89}, {"G_SCONF2", "satr", 90}, {"G_SSEL1", "satr", 87},
    {"G_SSEL2", "satr", 88}, {"HI_1MIN", "shar", 113}, {"HI_2MIN", "shar", 112}, {"HI_3MIN", "shar", 111},
    {"HI_4MIN", "shar", 110}, {"HI_5MIN", "shar", 109}, {"HI_ABORT", "shar", 118}, {"HI_ATACK", "shar", 138},
    {"HI_BDRDY", "shar", 136}, {"HI_BLOST", "shar", 140}, {"HI_BUILD", "shar", 132}, {"HI_CANCL", "shar", 135},
    {"HI_CAPT", "shar", 122}, {"HI_DHRDY", "shar", 127}, {"HI_DPLOY", "shar", 130}, {"HI_ENEMY", "shar", 151},
    {"HI_GANEW", "shar", 125}, {"HI_GUARD", "shar", 107}, {"HI_HATTK", "shar", 150}, {"HI_HOLD", "shar", 134},
    {"HI_LAUNC", "shar", 128}, {"HI_MEND", "shar", 124}, {"HI_MFAIL", "shar", 116}, {"HI_MONEY", "shar", 121},
    {"HI_MWIN", "shar", 117}, {"HI_NEWOP", "shar", 137}, {"HI_NROOM", "shar", 149}, {"HI_ORDER", "shar", 144},
    {"HI_PLACE", "shar", 131}, {"HI_POWER", "shar", 120}, {"HI_PREP", "shar", 126}, {"HI_PRMRY", "shar", 146},
    {"HI_REINF", "shar", 129}, {"HI_RUN", "shar", 108}, {"HI_SELL", "shar", 123}, {"HI_SILOS", "shar", 119},
    {"HI_SPORT", "shar", 142}, {"HI_TRAIN", "shar", 133}, {"HI_ULOST", "shar", 139}, {"HI_UNRDY", "shar", 141},
    {"HI_UPGOP", "shar", 143}, {"HI_UPGRD", "shar", 145}, {"HI_WATTK", "shar", 148}, {"HI_WSIGN", "shar", 147},
    {"H_ECONF1", "shar", 76}, {"H_ECONF2", "shar", 77}, {"H_ECONF3", "shar", 78}, {"H_ESEL1", "shar", 73},
    {"H_ESEL2", "shar", 74}, {"H_ESEL3", "shar", 75}, {"H_ICONF1", "shar", 58}, {"H_ICONF2", "shar", 59},
    {"H_ICONF3", "shar", 60}, {"H_ISEL1", "shar", 55}, {"H_ISEL2", "shar", 56}, {"H_ISEL3", "shar", 57},
    {"H_VCONF1", "shar", 64}, {"H_VCONF2", "shar", 65}, {"H_VCONF3", "shar", 66}, {"H_VSEL1", "shar", 61},
    {"H_VSEL2", "shar", 62}, {"H_VSEL3", "shar", 63}, {"OI_1MIN", "sord", 113}, {"OI_2MIN", "sord", 112},
    {"OI_3MIN", "sord", 111}, {"OI_4MIN", "sord", 110}, {"OI_5MIN", "sord", 109}, {"OI_ABORT", "sord", 118},
    {"OI_ATACK", "sord", 138}, {"OI_BDRDY", "sord", 136}, {"OI_BLOST", "sord", 140}, {"OI_BUILD", "sord", 132},
    {"OI_CANCL", "sord", 135}, {"OI_CAPT", "sord", 122}, {"OI_DHRDY", "sord", 127}, {"OI_DPLOY", "sord", 130},
    {"OI_ENEMY", "sord", 151}, {"OI_GANEW", "sord", 125}, {"OI_GUARD", "sord", 107}, {"OI_HATTK", "sord", 150},
    {"OI_HOLD", "sord", 134}, {"OI_LAUNC", "sord", 128}, {"OI_MEND", "sord", 124}, {"OI_MFAIL", "sord", 116},
    {"OI_MONEY", "sord", 121}, {"OI_MWIN", "sord", 117}, {"OI_NEWOP", "sord", 137}, {"OI_NROOM", "sord", 149},
    {"OI_ORDER", "sord", 144}, {"OI_PLACE", "sord", 131}, {"OI_POWER", "sord", 120}, {"OI_PREP", "sord", 126},
    {"OI_PRMRY", "sord", 146}, {"OI_REINF", "sord", 129}, {"OI_RUN", "sord", 108}, {"OI_SELL", "sord", 123},
    {"OI_SILOS", "sord", 119}, {"OI_SPORT", "sord", 142}, {"OI_TRAIN", "sord", 133}, {"OI_ULOST", "sord", 139},
    {"OI_UNRDY", "sord", 141}, {"OI_UPGOP", "sord", 143}, {"OI_UPGRD", "sord", 145}, {"OI_WATTK", "sord", 148},
    {"OI_WSIGN", "sord", 147}, {"O_ECONF1", "sord", 76}, {"O_ECONF2", "sord", 77}, {"O_ECONF3", "sord", 78},
    {"O_ESEL1", "sord", 73}, {"O_ESEL2", "sord", 74}, {"O_ESEL3", "sord", 75}, {"O_ICONF1", "sord", 58},
    {"O_ICONF2", "sord", 59}, {"O_ICONF3", "sord", 60}, {"O_ISEL1", "sord", 55}, {"O_ISEL2", "sord", 56},
    {"O_ISEL3", "sord", 57}, {"O_SCONF1", "sord", 70}, {"O_SCONF2", "sord", 71}, {"O_SCONF3", "sord", 72},
    {"O_SSEL1", "sord", 67}, {"O_SSEL2", "sord", 68}, {"O_SSEL3", "sord", 69}, {"O_VCONF1", "sord", 64},
    {"O_VCONF2", "sord", 65}, {"O_VCONF3", "sord", 66}, {"O_VSEL1", "sord", 61}, {"O_VSEL2", "sord", 62},
    {"O_VSEL3", "sord", 63},
};

}  // namespace

std::vector<std::int16_t> decodeSpuAdpcm(std::span<const std::uint8_t> data) {
    // Prediction filters (coefficients / 64) of the SPU / CD-XA ADPCM format.
    static constexpr int kPos[5] = {0, 60, 115, 98, 122};
    static constexpr int kNeg[5] = {0, 0, -52, -55, -60};
    std::vector<std::int16_t> out;
    out.reserve(data.size() / 16 * 28);
    int s1 = 0, s2 = 0;
    for (std::size_t o = 0; o + 16 <= data.size(); o += 16) {
        const std::uint8_t* b = data.data() + o;
        int shift = b[0] & 15;
        if (shift > 12) shift = 9;  // hardware treats shift 13-15 like 9
        int filter = b[0] >> 4;
        if (filter > 4) filter = 0;
        for (int i = 0; i < 28; ++i) {
            const int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
            const int s = static_cast<std::int16_t>(nib << 12) >> shift;  // sign-extended 4-bit
            int v = s + ((s1 * kPos[filter] + s2 * kNeg[filter] + 32) >> 6);
            v = std::clamp(v, -32768, 32767);
            s2 = s1;
            s1 = v;
            out.push_back(static_cast<std::int16_t>(v));
        }
        if (b[1] & 1) break;  // end flag: this block is the last one played
    }
    return out;
}

bool speechSlotFor(const std::string& pcFile, std::string& bank, int& slot) {
    std::string key = upper(pcFile);
    if (const auto dot = key.rfind('.'); dot != std::string::npos && key.substr(dot) == ".AUD") key.resize(dot);
    const auto* end = std::end(kSlots);
    const auto* it = std::lower_bound(std::begin(kSlots), end, key,
                                      [](const SlotName& e, const std::string& k) { return std::strcmp(e.pcFile, k.c_str()) < 0; });
    if (it == end || key != it->pcFile) return false;
    bank = it->bank;
    slot = it->slot;
    return true;
}

bool SpeechBanks::open(const std::string& discPath) {
    open_ = false;
    banks_.clear();
    error_.clear();
    if (!disc_.open(discPath)) {
        error_ = disc_.error();
        return false;
    }
    if (!disc_.mixEntry("satr.dat") || !disc_.mixEntry("satr00.snd")) {
        error_ = "no PS1 Dune 2000 sound banks (DATA.MIX satr.dat) on " + discPath;
        return false;
    }
    open_ = true;
    return true;
}

SpeechBanks::Bank* SpeechBanks::bank(const std::string& name) {
    if (!open_) return nullptr;
    Bank& b = banks_[name];
    if (b.loaded) return &b;
    b.loaded = true;
    const std::vector<std::uint8_t> dat = disc_.readMixFile(name + ".dat");
    if (dat.size() < 4) return &b;
    const std::uint32_t n = std::min<std::uint32_t>(le32(dat.data()), std::uint32_t((dat.size() - 4) / 8));
    for (int i = 0; i < 100; ++i) {
        char part[8];
        std::snprintf(part, sizeof part, "%02d.snd", i);
        const std::vector<std::uint8_t> snd = disc_.readMixFile(name + part);
        if (snd.empty()) break;
        b.data.insert(b.data.end(), snd.begin(), snd.end());
    }
    b.slots.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) b.slots.emplace_back(le32(&dat[4 + i * 8]), le32(&dat[8 + i * 8]));
    return &b;
}

int SpeechBanks::slotCount(const std::string& name) {
    const Bank* b = bank(name);
    return b ? int(b->slots.size()) : 0;
}

std::optional<AudClip> SpeechBanks::slotClip(const std::string& name, int slot) {
    const Bank* b = bank(name);
    if (!b || slot < 0) return std::nullopt;
    if (slot >= int(b->slots.size())) {
        if (name == "matr" || b->slots.empty()) return std::nullopt;
        return slotClip("matr", slot - int(b->slots.size()));
    }
    const auto [off, size] = b->slots[std::size_t(slot)];
    if (size == 0 || std::uint64_t(off) + size > b->data.size()) return std::nullopt;
    AudClip clip;
    clip.sampleRate = kSpeechSampleRate;
    clip.channels = 1;
    clip.samples = decodeSpuAdpcm(std::span<const std::uint8_t>(b->data.data() + off, size));
    // The Russian disc pads its (shorter) lines with digital silence to the English slot sizes;
    // drop it so a queued line follows without a gap.
    while (!clip.samples.empty() && clip.samples.back() == 0) clip.samples.pop_back();
    if (clip.samples.empty()) return std::nullopt;
    return clip;
}

std::optional<AudClip> SpeechBanks::clip(const std::string& pcFile) {
    std::string name;
    int slot = 0;
    if (!speechSlotFor(pcFile, name, slot)) return std::nullopt;
    return slotClip(name, slot);
}

}  // namespace d2k::formats::ps1
