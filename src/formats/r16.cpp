#include "formats/r16.hpp"

#include "formats/io.hpp"

#include <stdexcept>

namespace d2k::formats {

namespace {
constexpr std::size_t kHeaderSize = 29;
constexpr std::size_t kPaletteBlockSize = 520;  // 8-byte header + 256 x u16
constexpr std::size_t kPaletteHeaderSize = 8;
}  // namespace

Rgba rgb555ToRgba(std::uint16_t c) {
    std::uint32_t r = (c >> 10) & 0x1F, g = (c >> 5) & 0x1F, b = c & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

R16File parseR16(std::span<const std::uint8_t> data, R16Layout layout) {
    R16File out;
    ByteReader rd(data);
    while (!rd.atEnd()) {
        const std::size_t index = out.frames.size();
        if (layout == R16Layout::Ui && index % 3 == 0 && index < 45) rd.bytes(8);  // per D2kEditor
        const std::size_t frameStart = rd.pos();
        const std::uint8_t type = rd.u8();
        if (type == 0) {
            // Empty entry: a single zero byte that still occupies a frame index
            // (art index tables in Templates.bin count it).
            out.frames.push_back(R16Frame{});
            continue;
        }
        if (rd.remaining() < kHeaderSize - 1)
            throw std::runtime_error("R16: truncated frame header at " + std::to_string(frameStart));

        R16Frame f;
        f.type = type;
        f.width = rd.i32();
        f.height = rd.i32();
        f.x = rd.i32();
        f.y = rd.i32();
        rd.u32();  // image pointer (runtime value, unused)
        const std::uint32_t palettePtr = rd.u32();
        f.bpp = rd.u8();
        f.frameHeight = rd.u8();
        f.frameWidth = rd.u8();
        rd.u8();  // alignment

        if ((f.bpp != 8 && f.bpp != 16) || f.width <= 0 || f.height <= 0 || f.width > 4096 ||
            f.height > 4096)
            throw std::runtime_error("R16: invalid frame at " + std::to_string(frameStart));

        const auto pix = rd.bytes(static_cast<std::size_t>(f.width) * f.height * (f.bpp / 8));
        f.pixels.assign(pix.begin(), pix.end());

        if (type == 1 && palettePtr != 0 && f.bpp == 8) {
            auto block = rd.bytes(kPaletteBlockSize);
            Palette p{};
            for (int i = 0; i < 256; ++i) {
                const std::size_t o = kPaletteHeaderSize + i * 2;
                p[i] = rgb555ToRgba(static_cast<std::uint16_t>(block[o] | (block[o + 1] << 8)));
            }
            out.palettes.push_back(p);
        }
        if (f.bpp == 8 && !out.palettes.empty()) f.palette = static_cast<int>(out.palettes.size()) - 1;
        out.frames.push_back(std::move(f));
    }
    return out;
}

R16File loadR16(const std::string& path, R16Layout layout) { return parseR16(readFile(path), layout); }

std::vector<Rgba> decodeFrame(const R16File& file, const R16Frame& f, bool zeroIsTransparent) {
    const std::size_t n = static_cast<std::size_t>(f.width) * f.height;
    std::vector<Rgba> out(n);
    if (f.bpp == 16) {
        for (std::size_t i = 0; i < n; ++i) {
            const auto c = static_cast<std::uint16_t>(f.pixels[i * 2] | (f.pixels[i * 2 + 1] << 8));
            out[i] = (zeroIsTransparent && c == 0) ? 0u : rgb555ToRgba(c);
        }
    } else {
        const Palette* pal = f.palette >= 0 ? &file.palettes[f.palette] : nullptr;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t idx = f.pixels[i];
            if (zeroIsTransparent && idx == 0) { out[i] = 0; continue; }
            out[i] = pal ? (*pal)[idx] : (0xFF000000u | idx * 0x010101u);
        }
    }
    return out;
}

}  // namespace d2k::formats
