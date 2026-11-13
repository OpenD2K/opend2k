#pragma once
// NSIS (Nullsoft Scriptable Install System) installer reader: lists and extracts the files an
// installer writes under $INSTDIR, without running it. Used to build the game data folder
// straight from the Gruntmods Edition installer ("Dune 2000 Gruntmods Edition 1.6.7.exe").
//
// Own implementation, written from the public NSIS format description (the NSIS sources'
// fileform.h: first header, header blocks, entries/opcodes, string coding). LZMA decoding uses
// Igor Pavlov's LZMA SDK (public domain, third_party/lzma). Layout notes: docs/formats.md (NSIS).
//
// Supported data layouts:
//  - "chunked64" (what Gruntmods 1.6.7 uses, NSIS build v18-Sep-2024): solid LZMA split into
//    independent LZMA streams of 2 MiB output each, u24 compressed-length prefix per chunk, u64
//    data size after the first header and u64 length prefixes inside the stream, 36-byte entries.
//    Verified bit-exact against an installed copy.
//  - standard NSIS 2/3 solid LZMA and non-solid LZMA / stored blocks (ANSI and Unicode);
//    deflate (zlib) only when built with zlib (D2K_HAVE_ZLIB). These follow the documented
//    format but had no sample installer to test against. BZIP2 and the BCJ filter are rejected.
//
// No SDL; plain file I/O. All functions throw std::runtime_error on malformed input except
// extract(), which reports errors in its result.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace d2k::formats::nsis {

enum class Compression { Stored, Lzma, Deflate, Bzip2 };
enum class Layout { Solid, NonSolid, Chunked64 };

struct File {
    std::string path;              // relative to $INSTDIR, '/'-separated, directory case unified
    std::uint64_t offset = 0;      // data offset (solid: in the stream after the header)
    std::uint64_t fileTime = 0;    // Windows FILETIME (100 ns since 1601), 0 = unknown
};

struct Archive {
    std::string installer;          // path of the .exe
    std::uint64_t firstHeader = 0;  // file offset of the 0xDEADBEEF first header (minus 4)
    std::uint64_t dataStart = 0;    // file offset just past the first header
    std::uint64_t dataEnd = 0;      // end of the archive data (before the trailing CRC)
    std::uint32_t headerSize = 0;   // uncompressed header block size
    Compression compression = Compression::Lzma;
    Layout layout = Layout::Solid;
    bool unicode = false;
    int entrySize = 28;             // bytes per script entry (28 standard, 36 in the 64-bit build)
    std::vector<File> files;        // unique destination paths, in script order
    int skipped = 0;                // EW_EXTRACTFILE outside $INSTDIR ($PLUGINSDIR...) or unresolved
    std::uint64_t headerBlockEnd = 0;  // non-solid: file offset where file data offsets start

    std::string describe() const;   // one line: "NSIS Unicode, LZMA chunked64, 14506 files"
};

// Parses the installer (first header, decompressed header, script). Throws on errors.
Archive open(const std::string& installerPath);

struct Progress {
    std::uint64_t inputDone = 0, inputTotal = 0;  // compressed bytes (drive a progress bar)
    std::size_t filesDone = 0, filesTotal = 0;
    std::uint64_t bytesWritten = 0;
    std::string current;  // file being written
};
// Called regularly during extract(); return false to cancel.
using ProgressFn = std::function<bool(const Progress&)>;

struct ExtractOptions {
    // Only files whose path starts with this prefix (case-insensitive, '/'-separated), e.g.
    // "Dune 2000/". Empty = everything under $INSTDIR.
    std::string prefix;
    // Write the files without that prefix (e.g. "Dune 2000/movies/X.VQA" -> <target>/X.VQA).
    bool stripPrefix = false;
    // Optional extra filter (true = extract).
    std::function<bool(const File&)> filter;
    // Decoder threads for the chunked layout (0 = hardware concurrency).
    int threads = 0;
};

struct ExtractResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};

// Writes the selected files below `targetDir` (created if needed). Never writes outside it:
// paths with "..", drive letters or absolute components are rejected.
ExtractResult extract(const Archive& archive, const std::string& targetDir, const ExtractOptions& options = {},
                      const ProgressFn& progress = {});

// Reads one file's content (decodes the solid stream up to it). Throws on errors. For tests.
std::vector<std::uint8_t> readFile(const Archive& archive, const File& file);

}  // namespace d2k::formats::nsis
