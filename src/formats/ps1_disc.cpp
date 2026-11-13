#include "formats/ps1_disc.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace d2k::formats::ps1 {

namespace fs = std::filesystem;

namespace {

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

bool sameNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// The first `FILE "name" BINARY` of a cue sheet, relative to the cue's folder.
std::string binFromCue(const std::string& cue) {
    std::ifstream in(cue);
    std::string line;
    while (std::getline(in, line)) {
        const auto p = line.find("FILE");
        if (p == std::string::npos) continue;
        const auto q1 = line.find('"', p), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        return (fs::path(cue).parent_path() / line.substr(q1 + 1, q2 - q1 - 1)).string();
    }
    return {};
}

}  // namespace

bool Disc::open(const std::string& path) {
    error_.clear();
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bin_ = ext == ".cue" ? binFromCue(path) : path;
    if (bin_.empty()) {
        error_ = "no FILE line in " + path;
        return false;
    }
    f_ = std::ifstream(bin_, std::ios::binary);
    std::error_code ec;
    const auto size = fs::file_size(bin_, ec);
    if (!f_ || ec || size < 20u * kRawSector) {
        error_ = "cannot open " + bin_;
        return false;
    }
    sectors_ = std::uint32_t(size / kRawSector);
    const std::vector<std::uint8_t> pvd = readForm1(16, 2048);
    if (pvd.size() < 2048 || std::string(reinterpret_cast<const char*>(&pvd[1]), 5) != "CD001") {
        error_ = "not a MODE2/2352 ISO9660 image";
        return false;
    }
    const std::uint32_t rootLba = le32(&pvd[156 + 2]), rootSize = le32(&pvd[156 + 10]);
    const std::vector<std::uint8_t> dir = readForm1(rootLba, std::min<std::uint32_t>(rootSize, 1u << 20));
    root_.clear();
    for (std::size_t pos = 0; pos < dir.size();) {
        const std::uint8_t len = dir[pos];
        if (len == 0) {
            pos = (pos / 2048 + 1) * 2048;
            continue;
        }
        if (pos + len > dir.size() || len < 34) break;
        const std::uint8_t nlen = dir[pos + 32];
        if (34 + nlen > len) break;
        std::string name(reinterpret_cast<const char*>(&dir[pos + 33]), nlen);
        if (const auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
        if (nlen > 1 || (name != std::string(1, '\0') && name != std::string(1, '\1')))
            root_.push_back({name, le32(&dir[pos + 2]), le32(&dir[pos + 10])});
        pos += len;
    }
    fat_.clear();
    mixLba_ = 0;
    DirEntry fat, xa, mix;
    if (find("DATA.MIX", mix)) mixLba_ = mix.lba;
    if (find("DATA.FAT", fat) && find("DATA.XA", xa)) {
        xaLba_ = xa.lba;
        const std::vector<std::uint8_t> t = readForm1(fat.lba, fat.size);
        if (t.size() >= 8) {
            const std::uint32_t nMix = le32(&t[0]), nXa = le32(&t[4]);
            for (std::uint32_t i = 0; i < nMix + nXa && 8 + (i + 1) * 28 <= t.size(); ++i) {
                const std::uint8_t* e = &t[8 + i * 28];
                FatEntry fe;
                fe.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 16));
                fe.sector = le32(e + 16);
                fe.size = le32(e + 20);
                fe.xa = i >= nMix;
                fat_.push_back(std::move(fe));
            }
        }
    }
    return true;
}

bool Disc::readRaw(std::uint32_t lba, std::uint8_t* out) {
    if (lba >= sectors_) return false;
    f_.clear();
    f_.seekg(std::streamoff(lba) * kRawSector);
    return bool(f_.read(reinterpret_cast<char*>(out), kRawSector));
}

std::vector<std::uint8_t> Disc::readForm1(std::uint32_t lba, std::uint32_t size) {
    std::vector<std::uint8_t> out;
    std::uint8_t s[kRawSector];
    out.reserve(size);
    for (std::uint32_t i = 0; out.size() < size; ++i) {
        if (!readRaw(lba + i, s)) break;
        const std::size_t n = std::min<std::size_t>(2048, size - out.size());
        out.insert(out.end(), s + 24, s + 24 + n);
    }
    return out;
}

bool Disc::find(const std::string& name, DirEntry& out) {
    for (const DirEntry& e : root_)
        if (sameNoCase(e.name, name)) {
            out = e;
            return true;
        }
    return false;
}

const FatEntry* Disc::xaEntry(const std::string& name) const {
    for (const FatEntry& e : fat_)
        if (e.xa && sameNoCase(e.name, name)) return &e;
    return nullptr;
}

const FatEntry* Disc::mixEntry(const std::string& name) const {
    for (const FatEntry& e : fat_)
        if (!e.xa && sameNoCase(e.name, name)) return &e;
    return nullptr;
}

std::vector<std::uint8_t> Disc::readMixFile(const std::string& name) {
    const FatEntry* e = mixEntry(name);
    if (!e || mixLba_ == 0) return {};
    std::vector<std::uint8_t> out = readForm1(mixLba_ + e->sector, e->size);
    if (out.size() != e->size) out.clear();
    return out;
}

}  // namespace d2k::formats::ps1
