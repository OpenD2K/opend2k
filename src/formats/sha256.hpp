#pragma once
// SHA-256 (FIPS 180-4), incremental: verifies downloaded art packs (platform/artpack_download.*).
// Written from the specification; no OS calls.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace d2k::formats {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t len);
    // The digest; the object must not be updated afterwards.
    std::array<std::uint8_t, 32> finish();
    // finish() as 64 lower-case hex digits.
    std::string finishHex();

    // One-shot helpers.
    static std::string hex(const void* data, std::size_t len);
    // Hash of a whole file (streamed); empty string if it cannot be read.
    static std::string fileHex(const std::string& path);

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    std::size_t used_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace d2k::formats
