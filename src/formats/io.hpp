#pragma once
// Little-endian byte reading helpers shared by format loaders.

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2k::formats {

// Find `name` inside `dir` ignoring case (game data mixes "Bloxbgbs"/"BLOXBGBS"/"0PLAY4.map").
// Returns the full path, or an empty string if not found.
inline std::string findFileNoCase(const std::string& dir, const std::string& name) {
    namespace fs = std::filesystem;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const fs::path exact = fs::path(dir) / name;
    std::error_code ec;
    if (fs::exists(exact, ec)) return exact.string();
    const std::string want = lower(name);
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (lower(e.path().filename().string()) == want) return e.path().string();
    return {};
}

inline std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> d) : data_(d) {}

    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return data_.size() - pos_; }
    bool atEnd() const { return pos_ >= data_.size(); }

    std::span<const std::uint8_t> bytes(std::size_t n) {
        need(n);
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }
    std::uint8_t u8() { need(1); return data_[pos_++]; }
    std::uint16_t u16() { auto b = bytes(2); return static_cast<std::uint16_t>(b[0] | (b[1] << 8)); }
    std::uint32_t u32() {
        auto b = bytes(4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
               (std::uint32_t(b[3]) << 24);
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

private:
    void need(std::size_t n) const {
        if (n > remaining()) throw std::runtime_error("unexpected end of data");
    }
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

}  // namespace d2k::formats
