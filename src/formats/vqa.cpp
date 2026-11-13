#include "formats/vqa.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace d2k::formats {

namespace {

constexpr std::uint32_t fourcc(const char (&s)[5]) {
    return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) |
           (std::uint32_t(std::uint8_t(s[2])) << 8) | std::uint32_t(std::uint8_t(s[3]));
}

constexpr std::uint32_t kFORM = fourcc("FORM"), kWVQA = fourcc("WVQA"), kVQHD = fourcc("VQHD");
constexpr std::uint32_t kVQFR = fourcc("VQFR"), kVQFL = fourcc("VQFL");
constexpr std::uint32_t kSND0 = fourcc("SND0"), kSND2 = fourcc("SND2");
constexpr std::uint32_t kCBF0 = fourcc("CBF0"), kCBFZ = fourcc("CBFZ"), kCBP0 = fourcc("CBP0"),
                        kCBPZ = fourcc("CBPZ");
constexpr std::uint32_t kCPL0 = fourcc("CPL0"), kCPLZ = fourcc("CPLZ");
constexpr std::uint32_t kVPT0 = fourcc("VPT0"), kVPTZ = fourcc("VPTZ"), kVPTR = fourcc("VPTR"),
                        kVPRZ = fourcc("VPRZ");

std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint32_t rgb555(std::uint16_t c) {
    const std::uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

}  // namespace

// Format80 commands (public LCW description):
//   0CCCPPPP PPPPPPPP        copy CCC+3 bytes from (write position - P)
//   10CCCCCC                 copy C literal bytes (C = 0: end of data)
//   11CCCCCC PPPP            copy C+3 bytes from output offset P (C < 62)
//   11111110 CCCC VV         fill C bytes with V
//   11111111 CCCC PPPP       copy C bytes from output offset P
// The relative variant measures the two "output offset" forms back from the write position.
bool decompressFormat80(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out, std::size_t maxOut,
                        bool relative) {
    out.resize(maxOut);
    std::size_t ip = 0, op = 0;
    auto copyFrom = [&](std::size_t src, std::size_t n) {
        if (src >= op) return false;
        n = std::min(n, maxOut - op);
        for (std::size_t i = 0; i < n; ++i) out[op + i] = out[src + i];  // may overlap forwards
        op += n;
        return true;
    };
    while (ip < in.size() && op < maxOut) {
        const std::uint8_t c = in[ip++];
        if (!(c & 0x80)) {
            if (ip >= in.size()) return false;
            const std::size_t n = ((c >> 4) & 7) + 3, back = (std::size_t(c & 0x0F) << 8) | in[ip++];
            if (back == 0 || back > op || !copyFrom(op - back, n)) return false;
        } else if (!(c & 0x40)) {
            std::size_t n = c & 0x3F;
            if (n == 0) break;
            if (ip + n > in.size()) return false;
            n = std::min(n, maxOut - op);
            std::memcpy(out.data() + op, in.data() + ip, n);
            ip += c & 0x3F;
            op += n;
        } else if (c == 0xFE) {
            if (ip + 3 > in.size()) return false;
            const std::size_t n = std::min<std::size_t>(le16(&in[ip]), maxOut - op);
            std::memset(out.data() + op, in[ip + 2], n);
            ip += 3;
            op += n;
        } else {
            std::size_t n;
            if (c == 0xFF) {
                if (ip + 2 > in.size()) return false;
                n = le16(&in[ip]);
                ip += 2;
            } else {
                n = (c & 0x3F) + 3;
            }
            if (ip + 2 > in.size()) return false;
            const std::size_t p = le16(&in[ip]);
            ip += 2;
            const std::size_t src = relative ? op - std::min(p, op + 1) : p;
            if (relative && p > op) return false;
            if (!copyFrom(src, n)) return false;
        }
    }
    out.resize(op);
    return true;
}

VqaDecoder::VqaDecoder() = default;
VqaDecoder::~VqaDecoder() = default;

bool VqaDecoder::open(const std::string& path) {
    auto f = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*f) {
        error_ = "cannot open " + path;
        return false;
    }
    in_ = std::move(f);
    return startStream();
}

bool VqaDecoder::openMemory(std::vector<std::uint8_t> data) {
    in_ = std::make_unique<std::istringstream>(std::string(data.begin(), data.end()), std::ios::binary);
    return startStream();
}

bool VqaDecoder::readChunkHeader(std::uint32_t& id, std::uint32_t& size) {
    std::uint8_t h[8];
    if (!in_->read(reinterpret_cast<char*>(h), 8)) return false;
    id = be32(h);
    size = be32(h + 4);
    return true;
}

bool VqaDecoder::readBytes(std::vector<std::uint8_t>& buf, std::uint32_t size) {
    buf.resize(size);
    if (size && !in_->read(reinterpret_cast<char*>(buf.data()), size)) return false;
    if (size & 1) in_->ignore(1);
    return true;
}

void VqaDecoder::skipBytes(std::uint32_t size) { in_->seekg(std::streamoff(size + (size & 1)), std::ios::cur); }

bool VqaDecoder::startStream() {
    hd_ = {};
    frame_ = 0;
    std::uint32_t id = 0, size = 0;
    std::uint8_t form[4];
    if (!readChunkHeader(id, size) || id != kFORM || !in_->read(reinterpret_cast<char*>(form), 4) ||
        be32(form) != kWVQA) {
        error_ = "not a VQA movie";
        return false;
    }
    if (!readChunkHeader(id, size) || id != kVQHD || size < 42 || !readBytes(chunk_, size)) {
        error_ = "VQA: missing VQHD";
        return false;
    }
    const std::uint8_t* h = chunk_.data();
    hd_.version = le16(h + 0);
    hd_.flags = le16(h + 2);
    hd_.frames = le16(h + 4);
    hd_.width = le16(h + 6);
    hd_.height = le16(h + 8);
    hd_.blockW = h[10];
    hd_.blockH = h[11];
    hd_.fps = h[12] ? h[12] : 15;
    hd_.cbParts = h[13];
    hd_.colors = le16(h + 14);
    hd_.maxBlocks = le16(h + 16);
    // +18 u32 unknown (offset), +22 u16 largest frame
    hd_.sampleRate = le16(h + 24);
    hd_.channels = h[26];
    hd_.bits = h[27];
    if (hd_.width <= 0 || hd_.height <= 0 || hd_.blockW <= 0 || hd_.blockH <= 0 || hd_.width % hd_.blockW ||
        hd_.height % hd_.blockH || hd_.width > 4096 || hd_.height > 4096) {
        error_ = "VQA: bad dimensions";
        return false;
    }
    if (hd_.channels < 1) hd_.channels = 1;
    if (hd_.channels > 2) hd_.channels = 2;
    if (!hd_.hasAudio()) hd_.sampleRate = 0;
    if (hd_.maxBlocks == 0) hd_.maxBlocks = 0xFFFF;
    pixels_.assign(std::size_t(hd_.width) * hd_.height, 0xFF000000u);
    codebook_.clear();
    cbPending_.clear();
    cbPendingParts = 0;
    for (int i = 0; i < 256; ++i) palette_[i] = 0xFF000000u;
    ima_[0] = ima_[1] = {};
    audio_.clear();
    error_.clear();
    return true;
}

std::vector<std::int16_t> VqaDecoder::takeAudio() {
    std::vector<std::int16_t> out;
    out.swap(audio_);
    return out;
}

void VqaDecoder::handleAudio(std::uint32_t id, std::span<const std::uint8_t> d) {
    if (!hd_.sampleRate) return;
    const int ch = hd_.channels;
    if (id == kSND0) {  // raw PCM
        if (hd_.bits == 16) {
            for (std::size_t i = 0; i + 1 < d.size(); i += 2) audio_.push_back(std::int16_t(le16(&d[i])));
        } else {
            for (std::uint8_t b : d) audio_.push_back(std::int16_t((int(b) - 128) << 8));
        }
        return;
    }
    // SND2: IMA ADPCM, two samples per byte (low nibble first). Stereo: the first half of the
    // chunk is the left channel, the second half the right one.
    if (ch == 1) {
        for (std::uint8_t b : d) {
            audio_.push_back(ima_[0].decode(b & 0x0F));
            audio_.push_back(ima_[0].decode(b >> 4));
        }
    } else {
        const std::size_t half = d.size() / 2;
        for (std::size_t i = 0; i < half; ++i) {
            const std::uint8_t l = d[i], r = d[half + i];
            audio_.push_back(ima_[0].decode(l & 0x0F));
            audio_.push_back(ima_[1].decode(r & 0x0F));
            audio_.push_back(ima_[0].decode(l >> 4));
            audio_.push_back(ima_[1].decode(r >> 4));
        }
    }
}

bool VqaDecoder::unpack(std::span<const std::uint8_t> d, bool compressed, std::vector<std::uint8_t>& out,
                        std::size_t maxOut) {
    if (!compressed) {
        out.assign(d.begin(), d.begin() + std::min(d.size(), maxOut));
        return true;
    }
    const bool relative = !d.empty() && d[0] == 0;
    return decompressFormat80(relative ? d.subspan(1) : d, out, maxOut, relative);
}

bool VqaDecoder::setCodebook(std::span<const std::uint8_t> d, bool compressed) {
    const std::size_t entry = std::size_t(hd_.blockW) * hd_.blockH * (hd_.hicolor() ? 2 : 1);
    return unpack(d, compressed, codebook_, entry * hd_.maxBlocks);
}

void VqaDecoder::putBlock(int block, int entry) {
    const int bw = hd_.blockW, bh = hd_.blockH, perRow = hd_.width / bw;
    const int bx = (block % perRow) * bw, by = (block / perRow) * bh;
    if (by >= hd_.height) return;
    const bool hc = hd_.hicolor();
    const std::size_t esz = std::size_t(bw) * bh * (hc ? 2 : 1);
    const std::size_t off = std::size_t(entry) * esz;
    if (off + esz > codebook_.size()) return;  // unknown entry: leave the old pixels
    const std::uint8_t* src = codebook_.data() + off;
    for (int y = 0; y < bh; ++y) {
        std::uint32_t* dst = pixels_.data() + std::size_t(by + y) * hd_.width + bx;
        for (int x = 0; x < bw; ++x) {
            if (hc) {
                dst[x] = rgb555(le16(src));
                src += 2;
            } else {
                dst[x] = palette_[*src++];
            }
        }
    }
}

// 8-bit pointers: two planes (low bytes of every block, then high bytes). A high byte equal to
// the fill marker (0xFF in version 1, 0x0F later) paints the block with colour = low byte.
void VqaDecoder::drawPalettized(std::span<const std::uint8_t> p) {
    const int perRow = hd_.width / hd_.blockW, total = perRow * (hd_.height / hd_.blockH);
    if (p.size() < std::size_t(total) * 2) return;
    const std::uint8_t marker = hd_.version == 1 ? 0xFF : 0x0F;
    for (int i = 0; i < total; ++i) {
        const std::uint8_t lo = p[i], hi = p[total + i];
        if (hi == marker) {
            const int bx = (i % perRow) * hd_.blockW, by = (i / perRow) * hd_.blockH;
            for (int y = 0; y < hd_.blockH; ++y)
                std::fill_n(pixels_.data() + std::size_t(by + y) * hd_.width + bx, hd_.blockW, palette_[lo]);
        } else {
            putBlock(i, hi * 256 + lo);
        }
    }
}

// Hicolor pointer codes: u16 LE each, type = top 3 bits.
//   0: skip (code & 0x1FFF) blocks (they keep the previous frame's pixels)
//   1: entry (code & 0xFF), repeated (((code >> 8) & 0x1F) + 1) * 2 times
//   2: entry (code & 0xFF) once, then that many (as in 1) one-byte entries
//   3: entry (code & 0x1FFF) once
//   5: entry (code & 0x1FFF), repeated <next byte> times
void VqaDecoder::drawHicolor(std::span<const std::uint8_t> c) {
    const int total = (hd_.width / hd_.blockW) * (hd_.height / hd_.blockH);
    std::size_t ip = 0;
    int pos = 0;
    while (pos < total && ip + 2 <= c.size()) {
        const int code = le16(&c[ip]);
        ip += 2;
        switch (code >> 13) {
            case 0: pos += code & 0x1FFF; break;
            case 1: {
                const int n = (((code >> 8) & 0x1F) + 1) * 2;
                for (int i = 0; i < n && pos < total; ++i) putBlock(pos++, code & 0xFF);
                break;
            }
            case 2: {
                const int n = (((code >> 8) & 0x1F) + 1) * 2;
                putBlock(pos++, code & 0xFF);
                for (int i = 0; i < n && pos < total && ip < c.size(); ++i) putBlock(pos++, c[ip++]);
                break;
            }
            case 3: putBlock(pos++, code & 0x1FFF); break;
            case 5: {
                if (ip >= c.size()) return;
                const int n = c[ip++];
                for (int i = 0; i < n && pos < total; ++i) putBlock(pos++, code & 0x1FFF);
                break;
            }
            default: return;  // 4, 6, 7 are not used
        }
    }
}

bool VqaDecoder::handleFrame(std::span<const std::uint8_t> d) {
    std::size_t p = 0;
    std::span<const std::uint8_t> ptr;
    bool ptrZ = false, ptrHi = false, havePtr = false;
    while (p + 8 <= d.size()) {
        const std::uint32_t id = be32(&d[p]), size = be32(&d[p + 4]);
        p += 8;
        if (size > d.size() - p) return false;
        const auto body = d.subspan(p, size);
        p += size + (size & 1);
        if (id == kCBF0 || id == kCBFZ) {
            if (!setCodebook(body, id == kCBFZ)) return false;
        } else if (id == kCBP0 || id == kCBPZ) {
            cbPending_.insert(cbPending_.end(), body.begin(), body.end());
            cbPendingZ = id == kCBPZ;
            if (++cbPendingParts >= std::max(1, hd_.cbParts)) {
                std::vector<std::uint8_t> all;
                all.swap(cbPending_);
                cbPendingParts = 0;
                if (!setCodebook(all, cbPendingZ)) return false;
            }
        } else if (id == kCPL0 || id == kCPLZ) {
            if (!unpack(body, id == kCPLZ, scratch_, 768)) return false;
            const std::size_t n = std::min<std::size_t>(256, scratch_.size() / 3);
            bool sixBit = true;
            for (std::size_t i = 0; i < n * 3; ++i) sixBit = sixBit && scratch_[i] < 64;
            for (std::size_t i = 0; i < n; ++i) {
                std::uint32_t r = scratch_[3 * i], g = scratch_[3 * i + 1], b = scratch_[3 * i + 2];
                if (sixBit) r = (r << 2) | (r >> 4), g = (g << 2) | (g >> 4), b = (b << 2) | (b >> 4);
                palette_[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        } else if (id == kVPT0 || id == kVPTZ || id == kVPTR || id == kVPRZ) {
            ptr = body;
            ptrZ = id == kVPTZ || id == kVPRZ;
            ptrHi = id == kVPTR || id == kVPRZ;
            havePtr = true;
        }
    }
    if (!havePtr) return true;
    const std::size_t blocks = std::size_t(hd_.width / hd_.blockW) * (hd_.height / hd_.blockH);
    std::vector<std::uint8_t> raw;
    if (!unpack(ptr, ptrZ, raw, blocks * 4 + 0x10000)) return false;
    if (ptrHi || hd_.hicolor()) drawHicolor(raw);
    else drawPalettized(raw);
    return true;
}

bool VqaDecoder::nextFrame() {
    if (!in_ || (hd_.frames && frame_ >= hd_.frames)) return false;
    std::uint32_t id = 0, size = 0;
    while (readChunkHeader(id, size)) {
        if (id == kVQFR) {
            if (!readBytes(chunk_, size) || !handleFrame(chunk_)) {
                error_ = "VQA: bad frame " + std::to_string(frame_);
                return false;
            }
            ++frame_;
            return true;
        }
        if (id == kVQFL) {  // the next codebook, wrapped like a frame (CBFZ)
            if (!readBytes(chunk_, size) || !handleFrame(chunk_)) {
                error_ = "VQA: bad VQFL";
                return false;
            }
        } else if (id == kSND0 || id == kSND2) {
            if (!readBytes(chunk_, size)) return false;
            handleAudio(id, chunk_);
        } else {
            skipBytes(size);
        }
    }
    return false;
}

}  // namespace d2k::formats
