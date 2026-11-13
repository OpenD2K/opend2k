#include "formats/aud.hpp"

#include "formats/io.hpp"
#include "formats/ima_adpcm.hpp"

#include <algorithm>

namespace d2k::formats {

namespace {

constexpr std::uint32_t kChunkMagic = 0x0000DEAF;
constexpr int kCompImaAdpcm = 99;

}  // namespace

AudClip parseAud(std::span<const std::uint8_t> data) {
    ByteReader rd(data);
    AudClip clip;
    clip.sampleRate = rd.u16();
    const std::uint32_t compSize = rd.u32();
    const std::uint32_t outSize = rd.u32();
    const std::uint8_t flags = rd.u8();
    const std::uint8_t comp = rd.u8();
    clip.channels = (flags & 1) ? 2 : 1;
    if (comp != kCompImaAdpcm || clip.channels != 1 || !(flags & 2))
        throw std::runtime_error("AUD: unsupported encoding (comp " + std::to_string(comp) + ")");
    (void)compSize;

    clip.samples.reserve(outSize / 2);
    ImaAdpcmState st;  // decoder state persists across chunks
    while (rd.remaining() >= 8) {
        const std::uint16_t chunkComp = rd.u16();
        rd.u16();  // chunk output size
        if (rd.u32() != kChunkMagic) throw std::runtime_error("AUD: bad chunk magic");
        for (std::uint8_t b : rd.bytes(std::min<std::size_t>(chunkComp, rd.remaining()))) {
            clip.samples.push_back(st.decode(b & 0x0F));  // low nibble first
            clip.samples.push_back(st.decode(b >> 4));
        }
    }
    if (clip.samples.size() > outSize / 2) clip.samples.resize(outSize / 2);
    return clip;
}

AudClip loadAud(const std::string& path) { return parseAud(readFile(path)); }

}  // namespace d2k::formats
