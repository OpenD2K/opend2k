#pragma once
// PS1 Dune 2000 speech (docs/ps1-assets.md "Speech slots -> PC sample names"): the per-house
// SPU-ADPCM sound banks satr/shar/sord (+ matr for overflow ids) in DATA.MIX, and which bank
// slot holds the line that the PC game plays from GAMESFX[Eng]/<file>.AUD.
//
// Bank: <bank>.dat = u32 count, count x {u32 offset, u32 size} into the concatenation of
// <bank>00.snd, <bank>01.snd, ... (size 0 = unused slot). Slot data is raw SPU ADPCM
// ("VAG" body, no header): 16-byte blocks {u8 filter<<4 | shift, u8 flags, 14 bytes = 28
// 4-bit samples, low nibble first}. flags bit 0 = end of the sample (the block is still
// played), bit 1 = repeat (jump to the loop start; we never loop), bit 2 = loop start.
// DUNE.EXE keys every sound on with SPU pitch 650 = 650 * 44100 / 4096 = 6998 Hz.
// The play routine treats an id >= the house bank's count as matr[id - count].
// Written from the public SPU ADPCM description (no code copied).

#include "formats/aud.hpp"
#include "formats/ps1_disc.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2k::formats::ps1 {

constexpr int kSpuSpeechPitch = 650;
constexpr int kSpeechSampleRate = 6998;  // kSpuSpeechPitch * 44100 / 4096, rounded

// SPU ADPCM blocks -> 16-bit PCM (28 samples per 16-byte block). Decoding stops after the
// first block with the end flag (or at the end of the data); a trailing partial block is ignored.
std::vector<std::int16_t> decodeSpuAdpcm(std::span<const std::uint8_t> data);

// PC speech file name (no extension, any case: "AI_ATACK", "a_vsel1") -> PS1 bank
// ("satr"/"shar"/"sord"/"matr") + slot. False for names without a PS1 line (SFX, mission
// briefings *_MAP*, game loaded/saved, G_SSEL3/G_SCONF3, the Gruntmods additions, ...).
bool speechSlotFor(const std::string& pcFile, std::string& bank, int& slot);

// Speech clips straight from a PS1 disc image. The disc is opened once; each bank is read
// and kept in memory the first time one of its slots is asked for (about 0.5 MB per bank).
class SpeechBanks {
public:
    // .cue or raw .bin of the disc. False + error() when it is not a Dune 2000 PS1 disc.
    bool open(const std::string& discPath);
    bool isOpen() const { return open_; }
    const std::string& error() const { return error_; }

    // The PS1 line for a PC speech file name (see speechSlotFor). nullopt when the name has
    // no PS1 slot or the slot is empty on this disc (e.g. the Russian disc has no saboteur lines).
    std::optional<AudClip> clip(const std::string& pcFile);
    // One slot of a bank, decoded at kSpeechSampleRate (mono), trailing zero samples (padding)
    // removed. A slot past the bank's count resolves to matr[slot - count], as the game does.
    std::optional<AudClip> slotClip(const std::string& bank, int slot);
    // Number of slots in a bank (0 when the bank is missing).
    int slotCount(const std::string& bank);

private:
    struct Bank {
        bool loaded = false;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> slots;  // offset, size
        std::vector<std::uint8_t> data;                              // <bank>NN.snd concatenated
    };
    Bank* bank(const std::string& name);

    Disc disc_;
    bool open_ = false;
    std::string error_;
    std::map<std::string, Bank> banks_;
};

}  // namespace d2k::formats::ps1
