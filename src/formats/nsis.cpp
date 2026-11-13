#include "formats/nsis.hpp"

extern "C" {
#include "LzmaDec.h"
}

#ifdef D2K_HAVE_ZLIB
#include <zlib.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#define D2K_FSEEK _fseeki64
#define D2K_FTELL _ftelli64
#else
#define D2K_FSEEK fseeko
#define D2K_FTELL ftello
#endif

namespace d2k::formats::nsis {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("NSIS: " + msg); }

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* p) { return le32(p) | (std::uint64_t(le32(p + 4)) << 32); }

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------------------------------------
// Input file with 64-bit offsets.

class InFile {
public:
    explicit InFile(const std::string& path) {
#if defined(_WIN32)
        f_ = _wfopen(fs::path(path).wstring().c_str(), L"rb");
#else
        f_ = std::fopen(path.c_str(), "rb");
#endif
        if (!f_) fail("cannot open " + path);
        D2K_FSEEK(f_, 0, SEEK_END);
        size_ = static_cast<std::uint64_t>(D2K_FTELL(f_));
    }
    ~InFile() { std::fclose(f_); }
    InFile(const InFile&) = delete;
    InFile& operator=(const InFile&) = delete;

    std::uint64_t size() const { return size_; }
    void readAt(std::uint64_t off, void* buf, std::size_t n) {
        if (off > size_ || n > size_ - off) fail("unexpected end of installer");
        if (D2K_FSEEK(f_, static_cast<long long>(off), SEEK_SET) != 0 || std::fread(buf, 1, n, f_) != n)
            fail("read error");
    }
    std::vector<std::uint8_t> readAt(std::uint64_t off, std::size_t n) {
        std::vector<std::uint8_t> v(n);
        readAt(off, v.data(), n);
        return v;
    }

private:
    std::FILE* f_ = nullptr;
    std::uint64_t size_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Decompression streams. read() returns fewer bytes than asked only at the end of the stream.

void* lzAlloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void lzFree(ISzAllocPtr, void* p) { std::free(p); }
const ISzAlloc kAlloc = {lzAlloc, lzFree};

bool plausibleLzma(const std::uint8_t* p) {
    // props byte (lc/lp/pb), dictionary size, then the range coder's first byte (always 0).
    const std::uint32_t dict = le32(p + 1);
    return p[0] < 9 * 5 * 5 && dict >= (1u << 12) && dict <= (1u << 30) && p[5] == 0;
}

class Stream {
public:
    virtual ~Stream() = default;
    virtual std::size_t read(std::uint8_t* dst, std::size_t n) = 0;
    virtual std::uint64_t inputPos() const = 0;  // installer offset reached (progress)

    void readExact(std::uint8_t* dst, std::size_t n) {
        if (read(dst, n) != n) fail("data stream ended early (truncated or corrupt installer)");
    }
    void skip(std::uint64_t n) {
        std::uint8_t buf[1 << 16];
        while (n) {
            const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(n, sizeof buf));
            readExact(buf, k);
            n -= k;
        }
    }
};

// Bytes [pos, end) of the installer as they are.
class StoredStream final : public Stream {
public:
    StoredStream(InFile& in, std::uint64_t pos, std::uint64_t end) : in_(in), pos_(pos), end_(end) {}
    std::size_t read(std::uint8_t* dst, std::size_t n) override {
        const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(n, end_ - pos_));
        if (k) in_.readAt(pos_, dst, k);
        pos_ += k;
        return k;
    }
    std::uint64_t inputPos() const override { return pos_; }

private:
    InFile& in_;
    std::uint64_t pos_, end_;
};

// One LZMA stream (5 props bytes first) read incrementally from the installer.
class LzmaStream final : public Stream {
public:
    LzmaStream(InFile& in, std::uint64_t pos, std::uint64_t end) : in_(in), pos_(pos), end_(end), buf_(1 << 20) {
        if (end_ - pos_ < LZMA_PROPS_SIZE) fail("LZMA block too short");
        std::uint8_t props[LZMA_PROPS_SIZE];
        in_.readAt(pos_, props, sizeof props);
        pos_ += sizeof props;
        LzmaDec_Construct(&dec_);
        if (LzmaDec_Allocate(&dec_, props, LZMA_PROPS_SIZE, &kAlloc) != SZ_OK) fail("unsupported LZMA properties");
        LzmaDec_Init(&dec_);
    }
    ~LzmaStream() override { LzmaDec_Free(&dec_, &kAlloc); }

    std::size_t read(std::uint8_t* dst, std::size_t n) override {
        std::size_t done = 0;
        while (done < n && !finished_) {
            if (bufPos_ == bufLen_ && pos_ < end_) {
                bufLen_ = static_cast<std::size_t>(std::min<std::uint64_t>(buf_.size(), end_ - pos_));
                in_.readAt(pos_, buf_.data(), bufLen_);
                pos_ += bufLen_;
                bufPos_ = 0;
            }
            SizeT outLen = n - done, inLen = bufLen_ - bufPos_;
            ELzmaStatus status;
            if (LzmaDec_DecodeToBuf(&dec_, dst + done, &outLen, buf_.data() + bufPos_, &inLen, LZMA_FINISH_ANY,
                                    &status) != SZ_OK)
                fail("LZMA data error (corrupt installer)");
            bufPos_ += inLen;
            done += outLen;
            if (status == LZMA_STATUS_FINISHED_WITH_MARK) finished_ = true;
            else if (outLen == 0 && inLen == 0 && bufPos_ == bufLen_ && pos_ >= end_) finished_ = true;  // input end
        }
        return done;
    }
    std::uint64_t inputPos() const override { return pos_ - (bufLen_ - bufPos_); }

private:
    InFile& in_;
    std::uint64_t pos_, end_;
    std::vector<std::uint8_t> buf_;
    std::size_t bufPos_ = 0, bufLen_ = 0;
    CLzmaDec dec_;
    bool finished_ = false;
};

#ifdef D2K_HAVE_ZLIB
// Raw deflate (NSIS's zlib mode has no zlib header).
class InflateStream final : public Stream {
public:
    InflateStream(InFile& in, std::uint64_t pos, std::uint64_t end) : in_(in), pos_(pos), end_(end), buf_(1 << 20) {
        std::memset(&z_, 0, sizeof z_);
        if (inflateInit2(&z_, -MAX_WBITS) != Z_OK) fail("zlib init failed");
    }
    ~InflateStream() override { inflateEnd(&z_); }
    std::size_t read(std::uint8_t* dst, std::size_t n) override {
        std::size_t done = 0;
        while (done < n && !finished_) {
            if (z_.avail_in == 0) {
                if (pos_ >= end_) break;
                const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(buf_.size(), end_ - pos_));
                in_.readAt(pos_, buf_.data(), k);
                pos_ += k;
                z_.next_in = buf_.data();
                z_.avail_in = static_cast<uInt>(k);
            }
            z_.next_out = dst + done;
            z_.avail_out = static_cast<uInt>(std::min<std::size_t>(n - done, 1u << 30));
            const int r = inflate(&z_, Z_NO_FLUSH);
            done = static_cast<std::size_t>(z_.next_out - dst);
            if (r == Z_STREAM_END) finished_ = true;
            else if (r != Z_OK && r != Z_BUF_ERROR) fail("deflate data error (corrupt installer)");
        }
        return done;
    }
    std::uint64_t inputPos() const override { return pos_ - z_.avail_in; }

private:
    InFile& in_;
    std::uint64_t pos_, end_;
    std::vector<std::uint8_t> buf_;
    z_stream z_;
    bool finished_ = false;
};
#endif

// Decodes a complete LZMA stream held in memory (props first). Used for the independent chunks.
std::vector<std::uint8_t> lzmaDecodeAll(const std::vector<std::uint8_t>& src) {
    if (src.size() < LZMA_PROPS_SIZE) fail("LZMA chunk too short");
    CLzmaDec dec;
    LzmaDec_Construct(&dec);
    if (LzmaDec_Allocate(&dec, src.data(), LZMA_PROPS_SIZE, &kAlloc) != SZ_OK) fail("unsupported LZMA properties");
    LzmaDec_Init(&dec);
    std::vector<std::uint8_t> out(std::max<std::size_t>(src.size() * 2, 1 << 21));
    std::size_t inPos = LZMA_PROPS_SIZE, outPos = 0;
    const char* err = nullptr;
    for (;;) {
        if (outPos == out.size()) out.resize(out.size() * 2);
        SizeT outLen = out.size() - outPos, inLen = src.size() - inPos;
        ELzmaStatus status;
        if (LzmaDec_DecodeToBuf(&dec, out.data() + outPos, &outLen, src.data() + inPos, &inLen, LZMA_FINISH_ANY,
                                &status) != SZ_OK) {
            err = "LZMA data error (corrupt installer)";
            break;
        }
        inPos += inLen;
        outPos += outLen;
        if (status == LZMA_STATUS_FINISHED_WITH_MARK) break;
        if (inPos == src.size() && outPos < out.size()) {
            if (status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK) err = "LZMA chunk truncated";
            break;
        }
        if (outLen == 0 && inLen == 0 && outPos < out.size()) {
            err = "LZMA decoder stalled";
            break;
        }
    }
    LzmaDec_Free(&dec, &kAlloc);
    if (err) fail(err);
    out.resize(outPos);
    return out;
}

// The 64-bit build's solid stream: u24 compressed length + independent LZMA stream per chunk,
// terminated by a zero length. Chunks are decoded ahead on worker threads.
class ChunkedStream final : public Stream {
public:
    ChunkedStream(InFile& in, std::uint64_t first, std::uint64_t end, int threads) : in_(in) {
        threads_ = threads > 0 ? threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        std::uint64_t p = first;
        for (;;) {
            if (p + 3 > end) fail("chunk table runs past the archive end");
            std::uint8_t b[3];
            in_.readAt(p, b, 3);
            const std::uint32_t len = b[0] | (b[1] << 8) | (b[2] << 16);
            if (len == 0) break;
            if (p + 3 + len > end) fail("chunk runs past the archive end");
            chunks_.push_back({p + 3, len});
            p += 3 + len;
        }
        endPos_ = p;
    }
    ~ChunkedStream() override {
        for (auto& f : pending_)
            if (f.valid()) f.wait();
    }

    std::size_t read(std::uint8_t* dst, std::size_t n) override {
        std::size_t done = 0;
        while (done < n) {
            if (curPos_ == cur_.size()) {
                fill();
                if (pending_.empty()) break;
                cur_ = pending_.front().get();  // rethrows decode errors
                pending_.pop_front();
                curPos_ = 0;
                ++consumed_;
                fill();
                continue;
            }
            const std::size_t k = std::min(n - done, cur_.size() - curPos_);
            std::memcpy(dst + done, cur_.data() + curPos_, k);
            curPos_ += k;
            done += k;
        }
        return done;
    }
    std::uint64_t inputPos() const override {
        return consumed_ < chunks_.size() ? chunks_[consumed_].pos : endPos_;
    }

private:
    void fill() {
        while (pending_.size() < static_cast<std::size_t>(threads_) && next_ < chunks_.size()) {
            auto comp = in_.readAt(chunks_[next_].pos, chunks_[next_].len);
            ++next_;
            pending_.push_back(std::async(std::launch::async, [c = std::move(comp)] { return lzmaDecodeAll(c); }));
        }
    }
    struct Chunk {
        std::uint64_t pos;
        std::uint32_t len;
    };
    InFile& in_;
    int threads_ = 1;
    std::vector<Chunk> chunks_;
    std::uint64_t endPos_ = 0;
    std::size_t next_ = 0, consumed_ = 0;
    std::deque<std::future<std::vector<std::uint8_t>>> pending_;
    std::vector<std::uint8_t> cur_;
    std::size_t curPos_ = 0;
};

std::unique_ptr<Stream> makeBlockStream(InFile& in, Compression c, std::uint64_t pos, std::uint64_t end) {
    switch (c) {
    case Compression::Stored: return std::make_unique<StoredStream>(in, pos, end);
    case Compression::Lzma: return std::make_unique<LzmaStream>(in, pos, end);
#ifdef D2K_HAVE_ZLIB
    case Compression::Deflate: return std::make_unique<InflateStream>(in, pos, end);
#else
    case Compression::Deflate: fail("deflate-compressed installers are not supported in this build");
#endif
    case Compression::Bzip2: fail("BZIP2-compressed installers are not supported");
    }
    fail("unknown compression");
}

// The solid data stream (header first). Chunked64 starts with its u64 data size.
std::unique_ptr<Stream> makeSolidStream(InFile& in, const Archive& a, int threads) {
    if (a.layout == Layout::Chunked64) return std::make_unique<ChunkedStream>(in, a.dataStart + 8, a.dataEnd, threads);
    return makeBlockStream(in, a.compression, a.dataStart, a.dataEnd);
}

int lengthPrefixSize(const Archive& a) { return a.layout == Layout::Chunked64 ? 8 : 4; }

// ---------------------------------------------------------------------------------------------
// Script (header) interpretation.

constexpr char kInstDir = '\x01', kPluginsDir = '\x02', kUnresolved = '\x03';

void appendUtf8(std::string& s, std::uint32_t cp) {
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Script {
    explicit Script(const std::vector<std::uint8_t>& header) : h(header) {}
    const std::vector<std::uint8_t>& h;
    bool unicode = false;
    bool ansiNsis3 = true;  // ANSI special codes 1..4 (NSIS 3) vs 252..255 (NSIS 2)
    std::uint32_t strOff = 0, strEnd = 0;
    std::map<int, std::string> vars;
    std::string outDir{kUnresolved};

    std::uint16_t u16at(std::size_t p) const { return static_cast<std::uint16_t>(h[p] | (h[p + 1] << 8)); }

    std::string var(int n) const {
        if (n == 21) return std::string(1, kInstDir);  // $INSTDIR
        if (n == 22) return outDir;                    // $OUTDIR
        if (n == 26) return std::string(1, kPluginsDir);
        auto it = vars.find(n);
        return it == vars.end() ? std::string(1, kUnresolved) : it->second;
    }

    // Decodes string `off` (negative = language string: unresolvable) with variables expanded.
    std::string str(std::int32_t off) const {
        if (off < 0) return std::string(1, kUnresolved);
        std::string r;
        if (unicode) {
            std::size_t p = strOff + static_cast<std::size_t>(off) * 2;
            while (p + 2 <= strEnd) {
                std::uint32_t c = u16at(p);
                p += 2;
                if (c == 0) break;
                if (c <= 4) {  // 1 lang, 2 shell, 3 var, 4 skip
                    if (p + 2 > strEnd) break;
                    const std::uint16_t v = u16at(p);
                    p += 2;
                    if (c == 4) { appendUtf8(r, v); continue; }
                    if (c == 3) r += var((v & 0x7F) | (((v >> 8) & 0x7F) << 7));
                    else r += kUnresolved;
                    continue;
                }
                if (c >= 0xD800 && c < 0xDC00 && p + 2 <= strEnd) {
                    const std::uint32_t lo = u16at(p);
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        p += 2;
                        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                appendUtf8(r, c);
            }
        } else {
            std::size_t p = strOff + static_cast<std::size_t>(off);
            const int lang = ansiNsis3 ? 1 : 255, shell = ansiNsis3 ? 2 : 254, varc = ansiNsis3 ? 3 : 253,
                      skipc = ansiNsis3 ? 4 : 252;
            while (p < strEnd) {
                const int c = h[p++];
                if (c == 0) break;
                if (c == lang || c == shell || c == varc || c == skipc) {
                    if (c == skipc) {
                        if (p < strEnd) appendUtf8(r, h[p++]);
                        continue;
                    }
                    if (p + 2 > strEnd) break;
                    const int v = (h[p] & 0x7F) | ((h[p + 1] & 0x7F) << 7);
                    p += 2;
                    r += c == varc ? var(v) : std::string(1, kUnresolved);
                    continue;
                }
                appendUtf8(r, static_cast<std::uint32_t>(c));  // Latin-1 approximation of the ANSI codepage
            }
        }
        return r;
    }
};

bool isAbsolute(const std::string& s) {
    if (!s.empty() && (s[0] == kInstDir || s[0] == kPluginsDir || s[0] == kUnresolved)) return true;
    if (s.size() >= 2 && s[1] == ':') return true;
    return s.size() >= 2 && s[0] == '\\' && s[1] == '\\';
}

// Path below $INSTDIR → "a/b/c"; empty if it is not below $INSTDIR or unsafe.
std::string relativeInstPath(const std::string& full) {
    if (full.empty() || full[0] != kInstDir) return {};
    std::string out, comp;
    auto flush = [&]() -> bool {
        if (comp.empty() || comp == ".") { comp.clear(); return true; }
        if (comp == ".." || comp.find_first_of(std::string{kInstDir, kPluginsDir, kUnresolved, ':', '\0'}) != std::string::npos)
            return false;
        // Windows drops trailing dots/spaces; so do we (keeps names portable).
        while (!comp.empty() && (comp.back() == '.' || comp.back() == ' ')) comp.pop_back();
        if (comp.empty()) return false;
        if (!out.empty()) out += '/';
        out += comp;
        comp.clear();
        return true;
    };
    for (std::size_t i = 1; i < full.size(); ++i) {
        const char c = full[i];
        if (c == '\\' || c == '/') {
            if (!flush()) return {};
        } else comp += c;
    }
    if (!flush()) return {};
    return out;
}

bool safeRelative(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find('\\') != std::string::npos || rel.find(':') != std::string::npos)
        return false;
    std::size_t s = 0;
    while (s <= rel.size()) {
        const std::size_t e = std::min(rel.find('/', s), rel.size());
        const std::string c = rel.substr(s, e - s);
        if (c.empty() || c == "." || c == "..") return false;
        s = e + 1;
    }
    return true;
}

void parseScript(Archive& a, const std::vector<std::uint8_t>& h) {
    if (h.size() < 4 + 8 * 8) fail("header block too small");
    std::uint32_t off[8], num[8];
    for (int i = 0; i < 8; ++i) {
        off[i] = le32(&h[4 + 8 * i]);
        num[i] = le32(&h[8 + 8 * i]);
    }
    const std::uint32_t secOff = off[1], secNum = num[1], entOff = off[2], entNum = num[2], strOff = off[3];
    const std::uint32_t langOff = off[4];
    if (entOff >= h.size() || strOff > h.size() || strOff <= entOff || entNum == 0) fail("bad header block table");
    const std::uint32_t es = (strOff - entOff) / entNum;
    if (es < 28 || es > 64 || es % 4 || entOff + std::uint64_t(es) * entNum > h.size()) fail("bad script entry size");
    a.entrySize = static_cast<int>(es);

    Script sc(h);
    sc.strOff = strOff;
    sc.strEnd = static_cast<std::uint32_t>(langOff > strOff && langOff <= h.size() ? langOff : h.size());
    // Section records hold a NSIS_MAX_STRLEN (1024) name buffer: 24 + 1024 bytes ANSI, 24 + 2048 Unicode.
    bool known = false;
    if (secNum > 0 && entOff > secOff) {
        const std::uint32_t ss = (entOff - secOff) / secNum;
        known = ss == 24 + 2048 || ss == 24 + 1024;
        sc.unicode = ss == 24 + 2048;
    }
    if (!known) {
        // Heuristic: Unicode string tables have mostly zero high bytes.
        std::size_t zeros = 0, n = 0;
        for (std::size_t p = strOff + 1; p < sc.strEnd && n < 2048; p += 2, ++n) zeros += h[p] == 0;
        sc.unicode = n > 16 && zeros * 10 > n * 6;
    }
    if (!sc.unicode) {
        std::size_t low = 0;
        for (std::size_t p = strOff; p < sc.strEnd; ++p) low += h[p] >= 1 && h[p] <= 4;
        sc.ansiNsis3 = low > 0;
    }
    a.unicode = sc.unicode;

    // Opcodes (NSIS fileform.h): EW_CREATEDIR 11, EW_EXTRACTFILE 20, EW_ASSIGNVAR 25. The 64-bit
    // build (36-byte entries) inserts two opcodes before EW_ASSIGNVAR (27 there).
    const int opCreateDir = 11, opExtract = 20, opAssign = es >= 36 ? 27 : 25;
    const bool wide = es >= 36;

    std::unordered_map<std::string, std::size_t> byPath;     // lower path -> files index
    std::unordered_map<std::string, std::string> dirSpelling;  // lower dir -> first spelling
    auto unifyCase = [&](const std::string& rel) {
        std::string out;
        std::size_t s = 0;
        for (;;) {
            const std::size_t e = rel.find('/', s);
            if (e == std::string::npos) {
                out += rel.substr(s);
                return out;
            }
            const std::string dir = out + rel.substr(s, e - s);
            auto [it, inserted] = dirSpelling.emplace(lower(dir), dir);
            out = it->second + "/";
            s = e + 1;
        }
    };

    for (std::uint32_t i = 0; i < entNum; ++i) {
        const std::uint8_t* e = &h[entOff + std::size_t(i) * es];
        const int which = static_cast<int>(le32(e));
        auto parm = [&](int k) { return static_cast<std::int32_t>(le32(e + 4 + 4 * k)); };
        if (which == opAssign) {
            const int v = parm(0);
            std::string val = (parm(2) == 0 && parm(3) == 0) ? sc.str(parm(1)) : std::string(1, kUnresolved);
            if (v == 22) sc.outDir = val;
            else if (v != 21 && v != 26) sc.vars[v] = val;
        } else if (which == opCreateDir) {
            if (parm(1) != 0) sc.outDir = sc.str(parm(0));  // SetOutPath
        } else if (which == opExtract) {
            const std::string name = sc.str(parm(1));
            const std::string full = isAbsolute(name) ? name : sc.outDir + "\\" + name;
            const std::string rel = relativeInstPath(full);
            if (rel.empty()) {
                ++a.skipped;
                continue;
            }
            File f;
            f.path = unifyCase(rel);
            f.offset = static_cast<std::uint32_t>(parm(2));
            if (wide) f.offset |= std::uint64_t(static_cast<std::uint32_t>(parm(3))) << 32;
            const int ft = wide ? 4 : 3;
            f.fileTime = static_cast<std::uint32_t>(parm(ft)) | (std::uint64_t(static_cast<std::uint32_t>(parm(ft + 1))) << 32);
            auto [it, inserted] = byPath.emplace(lower(f.path), a.files.size());
            if (inserted) a.files.push_back(std::move(f));
            else a.files[it->second] = std::move(f);  // later File commands overwrite earlier ones
        }
    }
}

// Reads the header block with the given layout guess; returns false if it does not fit.
bool tryReadHeader(InFile& in, Archive& a, std::vector<std::uint8_t>& header) {
    try {
        if (a.layout == Layout::NonSolid) {
            std::uint8_t b[4];
            in.readAt(a.dataStart, b, 4);
            const std::uint32_t v = le32(b);
            const std::uint64_t len = v & 0x7FFFFFFFu;
            if (a.dataStart + 4 + len > a.dataEnd) return false;
            a.headerBlockEnd = a.dataStart + 4 + len;
            if (!(v & 0x80000000u)) {
                if (len != a.headerSize) return false;
                header = in.readAt(a.dataStart + 4, static_cast<std::size_t>(len));
                return true;
            }
            auto s = makeBlockStream(in, a.compression, a.dataStart + 4, a.headerBlockEnd);
            header.assign(a.headerSize, 0);
            return s->read(header.data(), header.size()) == header.size();
        }
        auto s = makeSolidStream(in, a, 1);
        std::uint8_t b[8] = {};
        s->readExact(b, lengthPrefixSize(a));
        const std::uint64_t len = lengthPrefixSize(a) == 8 ? le64(b) : le32(b);
        if (len != a.headerSize) return false;
        header.assign(a.headerSize, 0);
        s->readExact(header.data(), header.size());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

std::string Archive::describe() const {
    const char* comp = compression == Compression::Lzma      ? "LZMA"
                       : compression == Compression::Deflate ? "deflate"
                       : compression == Compression::Bzip2   ? "BZIP2"
                                                             : "stored";
    const char* lay = layout == Layout::Chunked64 ? "chunked64 solid" : layout == Layout::Solid ? "solid" : "non-solid";
    return std::string("NSIS ") + (unicode ? "Unicode" : "ANSI") + ", " + comp + " " + lay + ", " +
           std::to_string(files.size()) + " files under $INSTDIR";
}

Archive open(const std::string& installerPath) {
    InFile in(installerPath);
    Archive a;
    a.installer = installerPath;

    // The first header sits at a 512-byte boundary after the exe stub:
    // u32 flags, u32 0xDEADBEEF, "NullsoftInst", u32 header size, u32 archive size.
    static const std::uint8_t kMagic[16] = {0xEF, 0xBE, 0xAD, 0xDE, 'N', 'u', 'l', 'l',
                                            's',  'o',  'f',  't',  'I', 'n', 's', 't'};
    bool found = false;
    std::vector<std::uint8_t> buf(1 << 20);
    for (std::uint64_t base = 0; base < in.size() && !found; base += buf.size()) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), in.size() - base));
        in.readAt(base, buf.data(), n);
        for (std::size_t p = 0; p + 28 <= n; p += 512)
            if (std::memcmp(&buf[p + 4], kMagic, 16) == 0) {
                a.firstHeader = base + p;
                found = true;
                break;
            }
    }
    if (!found) fail("not an NSIS installer (no NullsoftInst header)");
    std::uint8_t fh[28];
    in.readAt(a.firstHeader, fh, sizeof fh);
    const std::uint32_t flags = le32(fh);
    a.headerSize = le32(fh + 20);
    const std::uint32_t archiveSize = le32(fh + 24);
    if (flags & 1) fail("this is an uninstaller, not an installer");
    if (a.firstHeader + archiveSize > in.size() || archiveSize < 28 + 4) fail("installer is truncated");
    a.dataStart = a.firstHeader + 28;
    a.dataEnd = a.firstHeader + archiveSize - ((flags & 4) ? 0 : 4);  // FH_FLAGS_NO_CRC
    if (a.headerSize < 64 || a.headerSize > (64u << 20)) fail("implausible header size");

    std::uint8_t p[32] = {};
    in.readAt(a.dataStart, p, static_cast<std::size_t>(std::min<std::uint64_t>(sizeof p, a.dataEnd - a.dataStart)));

    // Candidate layouts, most specific first; the first whose header block decodes to the
    // announced size wins.
    struct Cand {
        Layout layout;
        Compression comp;
    };
    std::vector<Cand> cands;
    if (plausibleLzma(p + 11)) cands.push_back({Layout::Chunked64, Compression::Lzma});
    if (plausibleLzma(p)) cands.push_back({Layout::Solid, Compression::Lzma});
    if ((p[0] == 0 || p[0] == 1) && plausibleLzma(p + 1)) {
        if (p[0] == 1) fail("LZMA with the BCJ x86 filter is not supported");
        cands.push_back({Layout::Solid, Compression::Lzma});  // filter flag 0: data at +1
    }
    if (le32(p) & 0x80000000u) {
        if (plausibleLzma(p + 4)) cands.push_back({Layout::NonSolid, Compression::Lzma});
        cands.push_back({Layout::NonSolid, Compression::Deflate});
    } else {
        cands.push_back({Layout::NonSolid, Compression::Stored});
    }
    cands.push_back({Layout::Solid, Compression::Deflate});

    std::vector<std::uint8_t> header;
    bool ok = false;
    for (const Cand& c : cands) {
#ifndef D2K_HAVE_ZLIB
        if (c.comp == Compression::Deflate) continue;
#endif
        a.layout = c.layout;
        a.compression = c.comp;
        const std::uint64_t saveStart = a.dataStart;
        if (c.layout == Layout::Solid && c.comp == Compression::Lzma && !plausibleLzma(p)) a.dataStart += 1;
        if (tryReadHeader(in, a, header)) {
            ok = true;
            break;
        }
        a.dataStart = saveStart;
    }
    if (!ok) fail("unsupported or corrupt data block (BZIP2, deflate or an unknown NSIS variant?)");
    parseScript(a, header);
    return a;
}

namespace {

// Streams every selected file's data in offset order. `sink(dests, data, n, first, last)`.
struct Job {
    std::uint64_t offset;
    std::vector<const File*> dests;
};

std::vector<Job> buildJobs(const std::vector<const File*>& sel) {
    std::map<std::uint64_t, std::vector<const File*>> m;
    for (const File* f : sel) m[f->offset].push_back(f);
    std::vector<Job> jobs;
    jobs.reserve(m.size());
    for (auto& [off, d] : m) jobs.push_back({off, std::move(d)});
    return jobs;
}

using ChunkSink = std::function<bool(const Job&, const std::uint8_t*, std::size_t, bool begin, bool end)>;

// Returns false if the sink cancelled.
bool runJobs(const Archive& a, const std::vector<Job>& jobs, int threads, const ChunkSink& sink,
             std::uint64_t* inputPos = nullptr) {
    InFile in(a.installer);
    std::vector<std::uint8_t> buf(1 << 20);
    auto pump = [&](Stream& s, const Job& job, std::uint64_t len, bool unknownLen) -> bool {
        if (!sink(job, nullptr, 0, true, false)) return false;
        while (unknownLen || len) {
            const std::size_t want = unknownLen ? buf.size() : static_cast<std::size_t>(std::min<std::uint64_t>(len, buf.size()));
            const std::size_t got = s.read(buf.data(), want);
            if (!unknownLen && got != want) fail("data stream ended early (truncated or corrupt installer)");
            if (inputPos) *inputPos = s.inputPos();
            if (got && !sink(job, buf.data(), got, false, false)) return false;
            if (unknownLen && got < want) break;
            len -= unknownLen ? 0 : got;
        }
        return sink(job, nullptr, 0, false, true);
    };

    if (a.layout == Layout::NonSolid) {
        for (const Job& job : jobs) {
            std::uint8_t b[4];
            const std::uint64_t at = a.headerBlockEnd + job.offset;
            in.readAt(at, b, 4);
            const std::uint32_t v = le32(b);
            const std::uint64_t n = v & 0x7FFFFFFFu;
            if (at + 4 + n > a.dataEnd) fail("file data runs past the archive end");
            if (v & 0x80000000u) {
                auto s = makeBlockStream(in, a.compression, at + 4, at + 4 + n);
                if (!pump(*s, job, 0, true)) return false;
            } else {
                StoredStream s(in, at + 4, at + 4 + n);
                if (!pump(s, job, n, false)) return false;
            }
        }
        return true;
    }

    auto s = makeSolidStream(in, a, threads);
    const int lp = lengthPrefixSize(a);
    s->skip(std::uint64_t(lp) + a.headerSize);
    std::uint64_t pos = 0;
    for (const Job& job : jobs) {
        if (job.offset < pos) fail("overlapping file data");
        s->skip(job.offset - pos);
        std::uint8_t b[8] = {};
        s->readExact(b, lp);
        std::uint64_t len = lp == 8 ? le64(b) : le32(b);
        if (lp == 4) len &= 0x7FFFFFFFu;
        if (len > a.dataEnd * 64) fail("implausible file size");
        if (!pump(*s, job, len, false)) return false;
        pos = job.offset + lp + len;
    }
    return true;
}

bool startsWithNoCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

}  // namespace

ExtractResult extract(const Archive& a, const std::string& targetDir, const ExtractOptions& opt,
                      const ProgressFn& progress) {
    ExtractResult r;
    try {
        std::vector<const File*> sel;
        for (const File& f : a.files) {
            if (!opt.prefix.empty() && !startsWithNoCase(f.path, opt.prefix)) continue;
            if (opt.filter && !opt.filter(f)) continue;
            if (!safeRelative(f.path)) fail("unsafe path in installer: " + f.path);
            sel.push_back(&f);
        }
        if (sel.empty()) fail("no files to extract");
        std::error_code ec;
        fs::create_directories(targetDir, ec);
        if (ec) fail("cannot create " + targetDir + ": " + ec.message());
        const fs::path root = fs::absolute(fs::path(targetDir), ec).lexically_normal();
        auto inside = [&](const fs::path& p) {
            const auto rs = root.native(), ps = p.native();
            return ps.size() > rs.size() && ps.compare(0, rs.size(), rs) == 0;
        };

        Progress pr;
        pr.inputTotal = a.dataEnd - a.dataStart;
        pr.filesTotal = sel.size();
        std::uint64_t inputPos = a.dataStart, sinceReport = 0;
        std::vector<std::unique_ptr<std::ofstream>> outs;
        bool cancelled = false;

        auto report = [&]() -> bool {
            if (!progress) return true;
            pr.inputDone = inputPos > a.dataStart ? inputPos - a.dataStart : 0;
            if (!progress(pr)) {
                cancelled = true;
                return false;
            }
            return true;
        };

        auto sink = [&](const Job& job, const std::uint8_t* d, std::size_t n, bool begin, bool end) -> bool {
            if (begin) {
                outs.clear();
                for (const File* f : job.dests) {
                    const std::string rel = opt.stripPrefix ? f->path.substr(opt.prefix.size()) : f->path;
                    const fs::path dst = (root / fs::path(rel)).lexically_normal();
                    if (!inside(dst)) fail("refusing to write outside the target folder: " + f->path);
                    fs::create_directories(dst.parent_path(), ec);
                    if (ec) fail("cannot create " + dst.parent_path().string() + ": " + ec.message());
                    auto o = std::make_unique<std::ofstream>(dst, std::ios::binary | std::ios::trunc);
                    if (!*o) fail("cannot write " + dst.string());
                    outs.push_back(std::move(o));
                }
                pr.current = job.dests.front()->path;
                return report();
            }
            if (end) {
                for (auto& o : outs) {
                    o->close();
                    if (!*o) fail("write error (disk full?) on " + job.dests.front()->path);
                }
                outs.clear();
                pr.filesDone += job.dests.size();
                ++r.files;
                return report();
            }
            for (auto& o : outs) o->write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
            pr.bytesWritten += n;
            r.bytes += n;
            sinceReport += n;
            if (sinceReport >= (4u << 20)) {
                sinceReport = 0;
                return report();
            }
            return true;
        };
        r.files = 0;
        const bool done = runJobs(a, buildJobs(sel), opt.threads, sink, &inputPos);
        r.files = pr.filesDone;
        if (!done || cancelled) {
            r.cancelled = true;
            r.error = "cancelled";
            return r;
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
    }
    return r;
}

std::vector<std::uint8_t> readFile(const Archive& a, const File& file) {
    std::vector<std::uint8_t> out;
    std::vector<const File*> sel{&file};
    runJobs(a, buildJobs(sel), 0, [&](const Job&, const std::uint8_t* d, std::size_t n, bool, bool) {
        if (d) out.insert(out.end(), d, d + n);
        return true;
    });
    return out;
}

}  // namespace d2k::formats::nsis
