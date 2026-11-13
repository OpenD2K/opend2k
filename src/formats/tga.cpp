#include "formats/tga.hpp"

#include "formats/io.hpp"

#include <algorithm>
#include <stdexcept>

namespace d2k::formats {

Image parseTga(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const int idLen = r.u8();
    const int cmapType = r.u8();
    const int type = r.u8();
    r.bytes(5);  // colour map spec
    r.bytes(4);  // origin
    const int w = r.u16(), h = r.u16();
    const int bpp = r.u8();
    const int desc = r.u8();
    if (cmapType != 0 || (type != 2 && type != 10) || (bpp != 16 && bpp != 24 && bpp != 32))
        throw std::runtime_error("unsupported TGA");
    r.bytes(static_cast<std::size_t>(idLen));
    const int bytesPP = bpp / 8;
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h);
    auto readPixel = [&]() -> Rgba {
        auto b = r.bytes(static_cast<std::size_t>(bytesPP));
        if (bpp == 16) {
            const std::uint16_t c = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
            Rgba v = rgb555ToRgba(c & 0x7FFF);
            return v;
        }
        const std::uint32_t a = bpp == 32 ? b[3] : 0xFF;
        return a << 24 | std::uint32_t(b[0]) << 16 | std::uint32_t(b[1]) << 8 | b[2];
    };
    std::vector<Rgba> linear(img.pixels.size());
    if (type == 2) {
        for (auto& p : linear) p = readPixel();
    } else {
        std::size_t i = 0;
        while (i < linear.size()) {
            const int hdr = r.u8();
            const int n = (hdr & 0x7F) + 1;
            if (hdr & 0x80) {
                const Rgba v = readPixel();
                for (int k = 0; k < n && i < linear.size(); ++k) linear[i++] = v;
            } else {
                for (int k = 0; k < n && i < linear.size(); ++k) linear[i++] = readPixel();
            }
        }
    }
    const bool topDown = (desc & 0x20) != 0;
    for (int y = 0; y < h; ++y) {
        const int src = topDown ? y : h - 1 - y;
        std::copy_n(linear.begin() + static_cast<std::ptrdiff_t>(src) * w, w,
                    img.pixels.begin() + static_cast<std::ptrdiff_t>(y) * w);
    }
    // Images whose alpha channel is all zero are meant to be opaque.
    if (bpp == 32) {
        bool anyAlpha = false;
        for (auto p : img.pixels) anyAlpha |= (p >> 24) != 0;
        if (!anyAlpha)
            for (auto& p : img.pixels) p |= 0xFF000000u;
    }
    return img;
}

Image loadTga(const std::string& path) {
    const auto bytes = readFile(path);
    return parseTga(bytes);
}

}  // namespace d2k::formats
