#pragma once
// Westwood .AUD audio (sound effects, voices, music in data/GAMESFX*, data/Music).
// Header: u16 sample rate, u32 compressed size, u32 uncompressed size, u8 flags
// (bit0 stereo, bit1 16-bit), u8 compression (99 = Westwood IMA ADPCM, 1 = WS-ADPCM).
// Body: chunks of {u16 compressed size, u16 output size, u32 0x0000DEAF} + data.
// Only IMA ADPCM mono 16-bit is used by Dune 2000.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct AudClip {
    int sampleRate = 0;
    int channels = 1;
    std::vector<std::int16_t> samples;  // interleaved if stereo
};

AudClip parseAud(std::span<const std::uint8_t> data);
AudClip loadAud(const std::string& path);

}  // namespace d2k::formats
