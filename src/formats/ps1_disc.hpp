#pragma once
// PS1 Dune 2000 disc image access (docs/ps1-assets.md): a raw MODE2/2352 track (.bin, or the
// .cue that names it), ISO9660 root directory, and the game's DATA.FAT table of contents for
// DATA.MIX / DATA.XA. Written from the public CD-ROM XA / ISO9660 layouts (the Python
// prototypes are tools/ps1/disc.py and fat.py).
//
// Raw sector (2352 bytes): 12 sync, 4 header, 8 subheader (file, channel, submode, coding, x2),
// then 2048 user bytes (Form 1) or 2324 (Form 2, submode bit 5).
// DATA.FAT: u32 mixCount, u32 xaCount, then 28-byte entries {char name[16], u32 sector, u32 size,
// u32 0}. XA entries: sector = raw sector index from the start of DATA.XA, size = sectors * 2336.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace d2k::formats::ps1 {

constexpr int kRawSector = 2352;

struct FatEntry {
    std::string name;
    std::uint32_t sector = 0;
    std::uint32_t size = 0;
    bool xa = false;  // in DATA.XA (else DATA.MIX)
};

class Disc {
public:
    // `path` = .cue (its first FILE line is used) or the raw .bin. False + error() on failure.
    bool open(const std::string& path);
    const std::string& error() const { return error_; }
    const std::string& binPath() const { return bin_; }

    // One raw 2352-byte sector; false past the end.
    bool readRaw(std::uint32_t lba, std::uint8_t* out);
    // Form 1 user data of a file (2048 bytes per sector).
    std::vector<std::uint8_t> readForm1(std::uint32_t lba, std::uint32_t size);

    struct DirEntry {
        std::string name;
        std::uint32_t lba = 0, size = 0;
    };
    // Root directory file (any case); false when missing.
    bool find(const std::string& name, DirEntry& out);

    // DATA.FAT (empty when the disc has none) and the first raw sector of DATA.XA.
    const std::vector<FatEntry>& fat() const { return fat_; }
    std::uint32_t xaStart() const { return xaLba_; }
    // An XA entry (STR video / XA music) by name, any case; null when missing.
    const FatEntry* xaEntry(const std::string& name) const;
    // A DATA.MIX entry (Form 1 file: banks, maps, ...) by name, any case; null when missing.
    const FatEntry* mixEntry(const std::string& name) const;
    // The bytes of a DATA.MIX entry (empty when missing or unreadable).
    std::vector<std::uint8_t> readMixFile(const std::string& name);

private:
    std::string bin_, error_;
    std::ifstream f_;
    std::uint32_t sectors_ = 0;
    std::vector<DirEntry> root_;
    std::vector<FatEntry> fat_;
    std::uint32_t xaLba_ = 0;
    std::uint32_t mixLba_ = 0;  // first sector of DATA.MIX (0 = none)
};

}  // namespace d2k::formats::ps1
