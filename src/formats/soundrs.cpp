#include "formats/soundrs.hpp"

#include "formats/io.hpp"

#include <cctype>

namespace d2k::formats {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

SoundArchive SoundArchive::load(const std::string& path) {
    SoundArchive a;
    a.data_ = readFile(path);
    ByteReader rd(a.data_);
    const std::uint32_t headerSize = rd.u32();
    if (headerSize > a.data_.size()) throw std::runtime_error("SOUND.RS: bad header size");
    while (rd.pos() < headerSize) {
        std::string name;
        for (std::uint8_t c = rd.u8(); c != 0; c = rd.u8()) name.push_back(static_cast<char>(c));
        const std::uint32_t off = rd.u32(), size = rd.u32();
        if (static_cast<std::uint64_t>(off) + size > a.data_.size()) throw std::runtime_error("SOUND.RS: entry out of range");
        a.entries_.emplace(lower(name), std::make_pair(off, size));
    }
    return a;
}

bool SoundArchive::contains(const std::string& name) const { return entries_.count(lower(name)) != 0; }

AudClip SoundArchive::clip(const std::string& name) const {
    auto it = entries_.find(lower(name));
    if (it == entries_.end()) throw std::runtime_error("SOUND.RS: no entry " + name);
    return parseWav(std::span<const std::uint8_t>(data_).subspan(it->second.first, it->second.second));
}

AudClip parseWav(std::span<const std::uint8_t> data) {
    ByteReader rd(data);
    auto tag = [&] { auto b = rd.bytes(4); return std::string(reinterpret_cast<const char*>(b.data()), 4); };
    if (tag() != "RIFF") throw std::runtime_error("WAV: not RIFF");
    rd.u32();
    if (tag() != "WAVE") throw std::runtime_error("WAV: not WAVE");
    AudClip clip;
    int bits = 0;
    while (rd.remaining() >= 8) {
        const std::string id = tag();
        const std::uint32_t size = rd.u32();
        auto body = rd.bytes(std::min<std::size_t>(size, rd.remaining()));
        if (id == "fmt ") {
            ByteReader f(body);
            if (f.u16() != 1) throw std::runtime_error("WAV: not PCM");
            clip.channels = f.u16();
            clip.sampleRate = static_cast<int>(f.u32());
            f.u32();
            f.u16();
            bits = f.u16();
        } else if (id == "data") {
            if (bits == 16) {
                for (std::size_t i = 0; i + 1 < body.size(); i += 2)
                    clip.samples.push_back(static_cast<std::int16_t>(body[i] | (body[i + 1] << 8)));
            } else if (bits == 8) {
                for (auto b : body) clip.samples.push_back(static_cast<std::int16_t>((int(b) - 128) << 8));
            } else {
                throw std::runtime_error("WAV: unsupported bit depth");
            }
        }
        if (size & 1 && rd.remaining()) rd.u8();  // RIFF chunks are word-aligned
    }
    if (clip.sampleRate == 0) throw std::runtime_error("WAV: no fmt chunk");
    return clip;
}

}  // namespace d2k::formats
