#include "formats/zip_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>

// stb_image's zlib decoder (public domain / MIT, third_party/stb) for deflated entries. Private
// copy (STB_IMAGE_STATIC): render3d/terrain_v2.cpp has the program's shared one, which the tests
// linking only d2k_core do not get.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace d2k::formats::zip {

namespace fs = std::filesystem;

namespace {

std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }
std::uint64_t le64(const std::uint8_t* p) { return std::uint64_t(le32(p)) | std::uint64_t(le32(p + 4)) << 32; }

}  // namespace

std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t crc) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    const auto* p = static_cast<const std::uint8_t*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::string safeRelativePath(const std::string& name) {
    if (name.empty() || name[0] == '/') return {};
    if (name.find('\\') != std::string::npos || name.find(':') != std::string::npos ||
        name.find('\0') != std::string::npos)
        return {};
    std::string out;
    std::size_t i = 0;
    while (i <= name.size()) {
        std::size_t j = name.find('/', i);
        if (j == std::string::npos) j = name.size();
        const std::string part = name.substr(i, j - i);
        i = j + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") return {};
        // Control characters have no business in a pack's file names.
        if (std::any_of(part.begin(), part.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; })) return {};
        if (!out.empty()) out += '/';
        out += part;
    }
    return out;
}

bool Archive::readAt(std::uint64_t off, void* dst, std::size_t n) {
    if (off > fileSize_ || n > fileSize_ - off) return false;
    f_.clear();
    f_.seekg(static_cast<std::streamoff>(off));
    f_.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return std::size_t(f_.gcount()) == n;
}

bool Archive::open(const std::string& path, std::string& error) {
    entries_.clear();
    f_.close();
    f_.open(fs::path(path), std::ios::binary);
    if (!f_) {
        error = "cannot open " + path;
        return false;
    }
    std::error_code ec;
    fileSize_ = fs::file_size(fs::path(path), ec);
    if (ec || fileSize_ < 22) {
        error = "not a zip file (too small)";
        return false;
    }
    // End of central directory: the last 22..65557 bytes.
    const std::size_t tailN = std::size_t(std::min<std::uint64_t>(fileSize_, 22 + 65535));
    std::vector<std::uint8_t> tail(tailN);
    if (!readAt(fileSize_ - tailN, tail.data(), tailN)) {
        error = "cannot read the zip directory";
        return false;
    }
    std::ptrdiff_t eocd = -1;
    for (std::ptrdiff_t i = std::ptrdiff_t(tailN) - 22; i >= 0; --i)
        if (le32(&tail[std::size_t(i)]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        error = "not a zip file (no end of central directory)";
        return false;
    }
    const std::uint8_t* e = &tail[std::size_t(eocd)];
    std::uint64_t count = le16(e + 10), cdSize = le32(e + 12), cdOff = le32(e + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOff == 0xFFFFFFFF) {
        // Zip64: the locator sits right before the classic record.
        const std::uint64_t eocdAbs = fileSize_ - tailN + std::uint64_t(eocd);
        std::uint8_t loc[20];
        if (eocdAbs < 20 || !readAt(eocdAbs - 20, loc, 20) || le32(loc) != 0x07064b50) {
            error = "bad zip64 locator";
            return false;
        }
        std::uint8_t z[56];
        if (!readAt(le64(loc + 8), z, 56) || le32(z) != 0x06064b50) {
            error = "bad zip64 directory record";
            return false;
        }
        count = le64(z + 32);
        cdSize = le64(z + 40);
        cdOff = le64(z + 48);
    }
    if (cdOff > fileSize_ || cdSize > fileSize_ - cdOff || cdSize > (std::uint64_t(1) << 31) || count > cdSize / 46) {
        error = "corrupt zip directory";
        return false;
    }
    std::vector<std::uint8_t> cd(static_cast<std::size_t>(cdSize));
    if (!readAt(cdOff, cd.data(), cd.size())) {
        error = "cannot read the zip directory";
        return false;
    }
    std::size_t p = 0;
    for (std::uint64_t n = 0; n < count; ++n) {
        if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) {
            error = "corrupt zip directory entry";
            return false;
        }
        const std::uint8_t* h = &cd[p];
        Entry en;
        const std::uint16_t madeBy = le16(h + 4), flags = le16(h + 8);
        en.method = le16(h + 10);
        en.crc32 = le32(h + 16);
        en.compressedSize = le32(h + 20);
        en.size = le32(h + 24);
        const std::size_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
        const std::uint32_t extAttr = le32(h + 38);
        en.localOffset = le32(h + 42);
        if (p + 46 + nameLen + extraLen + commentLen > cd.size()) {
            error = "corrupt zip directory entry";
            return false;
        }
        en.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        // Zip64 extra field: the 64-bit values for the fields set to 0xFFFFFFFF, in order.
        const std::uint8_t* x = h + 46 + nameLen;
        for (std::size_t k = 0; k + 4 <= extraLen;) {
            const std::uint16_t id = le16(x + k), sz = le16(x + k + 2);
            if (k + 4 + sz > extraLen) break;
            if (id == 0x0001) {
                const std::uint8_t* v = x + k + 4;
                std::size_t left = sz;
                auto take = [&](std::uint64_t& field) {
                    if (field != 0xFFFFFFFF || left < 8) return;
                    field = le64(v);
                    v += 8;
                    left -= 8;
                };
                take(en.size);
                take(en.compressedSize);
                take(en.localOffset);
            }
            k += 4 + sz;
        }
        if (flags & 1) {
            error = "encrypted zip entries are not supported";
            return false;
        }
        en.directory = !en.name.empty() && en.name.back() == '/';
        // Unix host (made-by high byte 3): mode in the high 16 bits of the external attributes.
        if ((madeBy >> 8) == 3 && ((extAttr >> 16) & 0170000) == 0120000) en.symlink = true;
        entries_.push_back(std::move(en));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool Archive::read(const Entry& e, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    if (e.directory) return true;
    if (e.size > std::uint64_t(std::numeric_limits<int>::max()) || e.compressedSize > std::uint64_t(std::numeric_limits<int>::max())) {
        error = e.name + ": entry too large";
        return false;
    }
    std::uint8_t lh[30];
    if (!readAt(e.localOffset, lh, 30) || le32(lh) != 0x04034b50) {
        error = e.name + ": bad local header";
        return false;
    }
    const std::uint64_t dataOff = e.localOffset + 30 + le16(lh + 26) + le16(lh + 28);
    std::vector<std::uint8_t> comp(static_cast<std::size_t>(e.compressedSize));
    if (!comp.empty() && !readAt(dataOff, comp.data(), comp.size())) {
        error = e.name + ": truncated data";
        return false;
    }
    if (e.method == 0) {
        if (e.compressedSize != e.size) {
            error = e.name + ": bad stored size";
            return false;
        }
        out = std::move(comp);
    } else if (e.method == 8) {
        out.resize(static_cast<std::size_t>(e.size));
        if (e.size > 0) {
            const int n = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), int(out.size()),
                                                           reinterpret_cast<const char*>(comp.data()), int(comp.size()));
            if (n != int(out.size())) {
                error = e.name + ": inflate failed";
                return false;
            }
        }
    } else {
        error = e.name + ": unsupported compression method " + std::to_string(e.method);
        return false;
    }
    if (crc32(out.data(), out.size()) != e.crc32) {
        error = e.name + ": CRC mismatch";
        return false;
    }
    return true;
}

ExtractResult extractAll(const std::string& zipPath, const std::string& destDir, const std::string& prefix,
                         const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
    ExtractResult r;
    Archive a;
    if (!a.open(zipPath, r.error)) return r;
    // Check everything first: one bad name refuses the whole archive.
    struct Item { const Entry* e; std::string rel; };
    std::vector<Item> items;
    std::uint64_t total = 0;
    for (const Entry& e : a.entries()) {
        const std::string safe = safeRelativePath(e.name);
        if (safe.empty()) {
            if (e.name == "/" || e.name == "./") continue;
            r.error = "unsafe path in the archive: " + e.name;
            return r;
        }
        if (e.symlink) {
            r.error = "symbolic link in the archive: " + e.name;
            return r;
        }
        std::string rel = safe;
        if (!prefix.empty()) {
            const std::string pre = prefix.back() == '/' ? prefix.substr(0, prefix.size() - 1) : prefix;
            if (rel == pre) continue;  // the prefix folder itself
            if (rel.compare(0, pre.size() + 1, pre + "/") != 0) continue;
            rel = rel.substr(pre.size() + 1);
        }
        if (rel.empty()) continue;
        items.push_back({&e, rel});
        total += e.size;
    }
    std::error_code ec;
    const fs::path root(destDir);
    fs::create_directories(root, ec);
    if (ec) {
        r.error = "cannot create " + destDir + ": " + ec.message();
        return r;
    }
    std::vector<std::uint8_t> data;
    std::uint64_t done = 0;
    for (const Item& it : items) {
        if (progress && !progress(done, total)) {
            r.cancelled = true;
            r.error = "cancelled";
            return r;
        }
        const fs::path out = root / fs::path(it.rel);
        if (it.e->directory) {
            fs::create_directories(out, ec);
            continue;
        }
        fs::create_directories(out.parent_path(), ec);
        if (!a.read(*it.e, data, r.error)) return r;
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        if (!o || !o.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()))) {
            r.error = "cannot write " + out.string();
            return r;
        }
        done += it.e->size;
        r.bytes += data.size();
        ++r.files;
    }
    if (progress) progress(total, total);
    r.ok = true;
    return r;
}

}  // namespace d2k::formats::zip
