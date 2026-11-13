#include "formats/ps1_str.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace d2k::formats::ps1 {

namespace {

std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

// ---- XA ADPCM ------------------------------------------------------------------------------

constexpr int kXaK0[5] = {0, 60, 115, 98, 122};
constexpr int kXaK1[5] = {0, 0, -52, -55, -60};

// ---- MDEC ----------------------------------------------------------------------------------

constexpr int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// The PS1's default intra quantisation matrix (MPEG-1's, with 2 for DC), row-major.
constexpr int kQuant[64] = {2,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37,
                            19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
                            22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
                            26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};

// MPEG-1 table B.14 (DCT coefficients, "next coefficient" form): code without its sign bit.
struct Vlc {
    const char* code;
    int run, level;
};
constexpr Vlc kAcCodes[] = {
    {"11", 0, 1},           {"011", 1, 1},          {"0100", 0, 2},         {"0101", 2, 1},
    {"00101", 0, 3},        {"00111", 3, 1},        {"00110", 4, 1},        {"000110", 1, 2},
    {"000111", 5, 1},       {"000101", 6, 1},       {"000100", 7, 1},       {"0000110", 0, 4},
    {"0000100", 2, 2},      {"0000111", 8, 1},      {"0000101", 9, 1},      {"00100110", 0, 5},
    {"00100001", 0, 6},     {"00100101", 1, 3},     {"00100100", 3, 2},     {"00100111", 10, 1},
    {"00100011", 11, 1},    {"00100010", 12, 1},    {"00100000", 13, 1},    {"0000001010", 0, 7},
    {"0000001100", 1, 4},   {"0000001011", 2, 3},   {"0000001111", 4, 2},   {"0000001001", 5, 2},
    {"0000001110", 14, 1},  {"0000001101", 15, 1},  {"0000001000", 16, 1},  {"000000011101", 0, 8},
    {"000000011000", 0, 9}, {"000000010011", 0, 10}, {"000000010000", 0, 11}, {"000000011011", 1, 5},
    {"000000010100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3}, {"000000011110", 6, 2},
    {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1}, {"000000011010", 18, 1},
    {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1}, {"0000000011010", 0, 12},
    {"0000000011001", 0, 13}, {"0000000011000", 0, 14}, {"0000000010111", 0, 15}, {"0000000010110", 1, 6},
    {"0000000010101", 1, 7}, {"0000000010100", 2, 5}, {"0000000010011", 3, 4}, {"0000000010010", 5, 3},
    {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1}, {"0000000011110", 23, 1},
    {"0000000011101", 24, 1}, {"0000000011100", 25, 1}, {"0000000011011", 26, 1}, {"00000000011111", 0, 16},
    {"00000000011110", 0, 17}, {"00000000011101", 0, 18}, {"00000000011100", 0, 19}, {"00000000011011", 0, 20},
    {"00000000011010", 0, 21}, {"00000000011001", 0, 22}, {"00000000011000", 0, 23}, {"00000000010111", 0, 24},
    {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27}, {"00000000010011", 0, 28},
    {"00000000010010", 0, 29}, {"00000000010001", 0, 30}, {"00000000010000", 0, 31}, {"000000000011000", 0, 32},
    {"000000000010111", 0, 33}, {"000000000010110", 0, 34}, {"000000000010101", 0, 35}, {"000000000010100", 0, 36},
    {"000000000010011", 0, 37}, {"000000000010010", 0, 38}, {"000000000010001", 0, 39}, {"000000000010000", 0, 40},
    {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10}, {"000000000011100", 1, 11},
    {"000000000011011", 1, 12}, {"000000000011010", 1, 13}, {"000000000011001", 1, 14}, {"0000000000010011", 1, 15},
    {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17}, {"0000000000010000", 1, 18}, {"0000000000010100", 6, 3},
    {"0000000000011010", 11, 2}, {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2}, {"0000000000010111", 14, 2},
    {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1}, {"0000000000011110", 28, 1},
    {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1}, {"0000000000011011", 31, 1},
};

enum : std::uint8_t { kVlcInvalid = 0, kVlcCoef = 1, kVlcEob = 2, kVlcEscape = 3 };
struct VlcEntry {
    std::uint8_t kind = kVlcInvalid, len = 0, run = 0, level = 0;
};

// 16-bit peek -> code (every code is at most 16 bits without its sign).
const std::array<VlcEntry, 65536>& vlcTable() {
    static const std::array<VlcEntry, 65536> table = [] {
        std::array<VlcEntry, 65536> t{};
        auto add = [&](const char* code, VlcEntry e) {
            const int len = int(std::strlen(code));
            unsigned v = 0;
            for (int i = 0; i < len; ++i) v = (v << 1) | unsigned(code[i] == '1');
            e.len = std::uint8_t(len);
            const unsigned base = v << (16 - len), n = 1u << (16 - len);
            for (unsigned i = 0; i < n; ++i) t[base + i] = e;
        };
        add("10", {kVlcEob, 0, 0, 0});
        add("000001", {kVlcEscape, 0, 0, 0});
        for (const Vlc& c : kAcCodes) add(c.code, {kVlcCoef, 0, std::uint8_t(c.run), std::uint8_t(c.level)});
        return t;
    }();
    return table;
}

// 16-bit little-endian words read most significant bit first.
class BitReader {
public:
    BitReader(const std::uint8_t* d, std::size_t n) : d_(d), n_(n & ~std::size_t(1)) {}
    unsigned peek16() {
        fill();
        return unsigned(buf_ >> (bits_ - 16)) & 0xFFFF;
    }
    void skip(int n) { bits_ -= n; }
    int sbits(int n) {  // two's complement
        fill();
        const unsigned v = unsigned(buf_ >> (bits_ - n)) & ((1u << n) - 1);
        bits_ -= n;
        return (v & (1u << (n - 1))) ? int(v) - (1 << n) : int(v);
    }
    unsigned bits(int n) {
        fill();
        const unsigned v = unsigned(buf_ >> (bits_ - n)) & ((1u << n) - 1);
        bits_ -= n;
        return v;
    }
    bool overrun() const { return pos_ >= n_ && bits_ <= extra_; }

private:
    void fill() {
        while (bits_ <= 32) {
            unsigned w = 0;
            if (pos_ + 1 < n_) w = le16(d_ + pos_);
            else extra_ += 16;
            pos_ += 2;
            buf_ = (buf_ << 16) | w;
            bits_ += 16;
        }
    }
    const std::uint8_t* d_;
    std::size_t n_, pos_ = 0;
    std::uint64_t buf_ = 0;
    int bits_ = 0, extra_ = 0;
};

// 8x8 inverse DCT (separable, float): flat block of DC value F -> F / 8.
struct Idct {
    float c[8][8];  // c[x][u] = C(u)/2 * cos((2x+1) u pi / 16)
    Idct() {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                c[x][u] = float((u == 0 ? std::sqrt(0.5) : 1.0) / 2.0 * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));
    }
    void run(const int in[64], float out[64]) const {
        float tmp[64];
        for (int v = 0; v < 8; ++v)  // rows: u -> x
            for (int x = 0; x < 8; ++x) {
                float s = 0;
                for (int u = 0; u < 8; ++u) s += c[x][u] * float(in[v * 8 + u]);
                tmp[v * 8 + x] = s;
            }
        for (int x = 0; x < 8; ++x)  // columns: v -> y
            for (int y = 0; y < 8; ++y) {
                float s = 0;
                for (int v = 0; v < 8; ++v) s += c[y][v] * tmp[v * 8 + x];
                out[y * 8 + x] = s;
            }
    }
};

bool decodeBlock(BitReader& br, int qscale, int coef[64]) {
    const auto& vlc = vlcTable();
    std::fill_n(coef, 64, 0);
    coef[0] = std::clamp(br.sbits(10) * kQuant[0], -1024, 1023);
    int k = 0;
    for (;;) {
        const VlcEntry& e = vlc[br.peek16()];
        int run = 0, level = 0;
        if (e.kind == kVlcEob) {
            br.skip(2);
            return true;
        }
        if (e.kind == kVlcEscape) {
            br.skip(6);
            run = int(br.bits(6));
            level = br.sbits(10);
        } else if (e.kind == kVlcCoef) {
            br.skip(e.len);
            run = e.run;
            level = br.bits(1) ? -int(e.level) : int(e.level);
        } else {
            return false;
        }
        k += run + 1;
        if (k > 63 || br.overrun()) return false;
        const int pos = kZigzag[k];
        coef[pos] = std::clamp((level * kQuant[pos] * qscale + 4) >> 3, -1024, 1023);
    }
}

std::uint8_t clamp8(float v) { return std::uint8_t(std::clamp(int(std::lround(v)), 0, 255)); }

}  // namespace

void decodeXaSector(const std::uint8_t* data, bool stereo, int state[2][2], std::vector<std::int16_t>& out) {
    for (int g = 0; g < 18; ++g) {
        const std::uint8_t* grp = data + g * 128;
        std::int16_t buf[2][4 * 28];  // per channel: up to 4 units (stereo) or 8 units (mono, ch 0)
        std::int16_t mono[8 * 28];
        for (int unit = 0; unit < 8; ++unit) {
            const int ch = stereo ? (unit & 1) : 0;
            const int shift = grp[4 + unit] & 0x0F;
            int filt = (grp[4 + unit] >> 4) & 0x0F;
            if (filt > 4) filt = 0;
            int o1 = state[ch][0], o2 = state[ch][1];
            for (int i = 0; i < 28; ++i) {
                const std::uint8_t b = grp[16 + i * 4 + unit / 2];
                int nib = (unit & 1) ? (b >> 4) : (b & 0x0F);
                if (nib >= 8) nib -= 16;
                int v = (nib << 12) >> shift;
                v += (o1 * kXaK0[filt] + o2 * kXaK1[filt] + 32) >> 6;
                v = std::clamp(v, -32768, 32767);
                o2 = o1;
                o1 = v;
                if (stereo) buf[ch][(unit / 2) * 28 + i] = std::int16_t(v);
                else mono[unit * 28 + i] = std::int16_t(v);
            }
            state[ch][0] = o1;
            state[ch][1] = o2;
        }
        if (stereo) {
            for (int i = 0; i < 4 * 28; ++i) {
                out.push_back(buf[0][i]);
                out.push_back(buf[1][i]);
            }
        } else {
            out.insert(out.end(), mono, mono + 8 * 28);
        }
    }
}

bool decodeMdecFrame(const std::uint8_t* data, std::size_t size, int width, int height, std::uint32_t* out) {
    if (size < 8 || le16(data + 2) != 0x3800 || le16(data + 6) != 2) return false;
    const int qscale = le16(data + 4);
    static const Idct idct;
    BitReader br(data + 8, size - 8);
    const int mbw = (width + 15) / 16, mbh = (height + 15) / 16;
    int coef[64];
    float cr[64], cb[64], y[4][64];
    for (int mx = 0; mx < mbw; ++mx) {
        for (int my = 0; my < mbh; ++my) {
            bool ok = decodeBlock(br, qscale, coef);
            if (ok) idct.run(coef, cr);
            ok = ok && decodeBlock(br, qscale, coef);
            if (ok) idct.run(coef, cb);
            for (int b = 0; b < 4 && ok; ++b) {
                ok = decodeBlock(br, qscale, coef);
                if (ok) idct.run(coef, y[b]);
            }
            if (!ok) return false;
            for (int py = 0; py < 16; ++py) {
                const int oy = my * 16 + py;
                if (oy >= height) break;
                for (int px = 0; px < 16; ++px) {
                    const int ox = mx * 16 + px;
                    if (ox >= width) break;
                    const float Y = y[(py >> 3) * 2 + (px >> 3)][(py & 7) * 8 + (px & 7)] + 128.0f;
                    const int ci = (py >> 1) * 8 + (px >> 1);
                    const float R = Y + 1.402f * cr[ci];
                    const float G = Y - 0.3437f * cb[ci] - 0.7143f * cr[ci];
                    const float B = Y + 1.772f * cb[ci];
                    out[std::size_t(oy) * width + ox] =
                        0xFF000000u | (std::uint32_t(clamp8(R)) << 16) | (std::uint32_t(clamp8(G)) << 8) | clamp8(B);
                }
            }
        }
    }
    return true;
}

bool StrDecoder::open(Disc& disc, const FatEntry& entry) {
    disc_ = &disc;
    first_ = disc.xaStart() + entry.sector;
    count_ = entry.size / 2336;
    pos_ = 0;
    w_ = h_ = rate_ = 0;
    audioChannel_ = -1;
    std::uint8_t s[kRawSector];
    for (std::uint32_t i = 0; i < count_ && i < 200 && (w_ == 0 || rate_ == 0); ++i) {
        if (!disc.readRaw(first_ + i, s)) break;
        const std::uint8_t channel = s[17], submode = s[18], coding = s[19];
        if (le16(s + 24) == 0x0160 && le16(s + 26) == 0x8001) {
            w_ = le16(s + 24 + 16);
            h_ = le16(s + 24 + 18);
        } else if (submode & 0x04) {
            if (audioChannel_ < 0) {
                audioChannel_ = channel;
                stereo_ = coding & 1;
                rate_ = (coding & 4) ? 18900 : 37800;
            }
        }
    }
    if (w_ <= 0 || h_ <= 0 || w_ > 1024 || h_ > 1024) return false;
    px_.assign(std::size_t(w_) * h_, 0xFF000000u);
    curFrame_ = 0xFFFFFFFF;
    got_ = 0;
    xaState_[0][0] = xaState_[0][1] = xaState_[1][0] = xaState_[1][1] = 0;
    audio_.clear();
    return true;
}

std::vector<std::int16_t> StrDecoder::takeAudio() {
    std::vector<std::int16_t> out;
    out.swap(audio_);
    return out;
}

bool StrDecoder::nextFrame() {
    if (!disc_) return false;
    std::uint8_t s[kRawSector];
    while (pos_ < count_) {
        if (!disc_->readRaw(first_ + pos_++, s)) return false;
        const std::uint8_t channel = s[17], submode = s[18];
        const std::uint8_t* u = s + 24;
        if (le16(u) == 0x0160 && le16(u + 2) == 0x8001) {
            const int chunk = le16(u + 4), chunks = le16(u + 6);
            const std::uint32_t frame = le32(u + 8), bytes = le32(u + 12);
            if (chunks <= 0 || chunk >= chunks || bytes > 2016u * chunks) continue;
            if (frame != curFrame_) {
                curFrame_ = frame;
                frame_.assign(std::size_t(chunks) * 2016, 0);
                have_.assign(std::size_t(chunks), false);
                got_ = 0;
            }
            if (have_.size() != std::size_t(chunks) || have_[std::size_t(chunk)]) continue;
            std::memcpy(frame_.data() + std::size_t(chunk) * 2016, u + 32, 2016);
            have_[std::size_t(chunk)] = true;
            if (++got_ == chunks) {
                decodeMdecFrame(frame_.data(), bytes, w_, h_, px_.data());
                got_ = 0;
                curFrame_ = 0xFFFFFFFF;
                return true;
            }
        } else if ((submode & 0x04) && channel == audioChannel_) {
            decodeXaSector(u, stereo_, xaState_, audio_);
        }
    }
    return false;
}

}  // namespace d2k::formats::ps1
