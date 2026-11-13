#pragma once
// Minimal ZIP reader for the downloadable art pack (platform/artpack_download.*): central
// directory (+ Zip64), stored and deflated entries (inflate via stb_image's zlib decoder,
// third_party/stb), CRC-32 check, and extraction that refuses unsafe paths (absolute paths,
// "..", drive letters, backslashes, symlinks). Files are read in place (no whole-archive load);
// each entry is inflated in memory, so a single entry must stay below 2 GB.

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace d2k::formats::zip {

struct Entry {
    std::string name;  // as stored ('/' separators)
    std::uint64_t compressedSize = 0, size = 0, localOffset = 0;
    std::uint32_t crc32 = 0;
    std::uint16_t method = 0;  // 0 stored, 8 deflate
    bool directory = false;
    bool symlink = false;  // Unix mode S_IFLNK in the external attributes
};

std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc = 0);

// `name` as a safe relative path ("a/b.png"; "./" and empty parts dropped), or "" when it would
// leave the destination: absolute, "..", a drive letter / colon, a backslash or a NUL.
std::string safeRelativePath(const std::string& name);

class Archive {
public:
    Archive() = default;
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool open(const std::string& path, std::string& error);
    const std::vector<Entry>& entries() const { return entries_; }
    // The entry's data, CRC-checked.
    bool read(const Entry& e, std::vector<std::uint8_t>& out, std::string& error);

private:
    bool readAt(std::uint64_t off, void* dst, std::size_t n);
    std::ifstream f_;
    std::uint64_t fileSize_ = 0;
    std::vector<Entry> entries_;
};

struct ExtractResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};

// Extracts every entry whose name starts with `prefix` (e.g. "opend2k-redesign/", "" = all) into
// `destDir` with the prefix removed. Fails (without extracting anything) when any entry is
// unsafe (safeRelativePath, symlinks, a prefix/entry outside the prefix is just skipped).
// progress(doneBytes, totalBytes) returning false cancels.
ExtractResult extractAll(const std::string& zipPath, const std::string& destDir, const std::string& prefix,
                         const std::function<bool(std::uint64_t, std::uint64_t)>& progress = {});

}  // namespace d2k::formats::zip
