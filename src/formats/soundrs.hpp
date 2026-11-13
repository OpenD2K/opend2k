#pragma once
// GAMESFX*/SOUND.RS: sound-effect archive. u32 header size, then entries
// {NUL-terminated name, u32 offset, u32 size} until the header size; entries are
// RIFF/WAVE PCM files. Also: a minimal PCM WAV decoder into AudClip.

#include "formats/aud.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::formats {

class SoundArchive {
public:
    static SoundArchive load(const std::string& path);
    // Name lookup is case-insensitive ("bazook1.wav" == "BAZOOK1.WAV").
    bool contains(const std::string& name) const;
    AudClip clip(const std::string& name) const;  // throws if missing / not PCM
    std::size_t size() const { return entries_.size(); }

private:
    std::vector<std::uint8_t> data_;
    std::unordered_map<std::string, std::pair<std::uint32_t, std::uint32_t>> entries_;  // lower-case name
};

AudClip parseWav(std::span<const std::uint8_t> data);

}  // namespace d2k::formats
