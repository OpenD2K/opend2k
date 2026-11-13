#pragma once
// Westwood VQA movies (Dune 2000's movies/*.VQA cutscenes), written from public format
// descriptions (Gordan Ugarkovic's VQA_INFO, the MultimediaWiki VQA page). Format details:
// docs/formats.md "VQA".
//
// IFF container "FORM" <size BE> "WVQA", then chunks {id, u32 BE size, data, pad to even}:
//   VQHD  header (42 bytes, little endian, see VqaHeader);
//   FINF  frame offsets; LINF / CINF (loop and codebook tables in Dune 2000's files) — unused;
//   SND0 / SND2  audio for one frame (PCM / IMA ADPCM); SN2J  ADPCM resync info — unused;
//   VQFL  a full codebook (CBFZ) that applies from the next frame on;
//   VQFR  one frame: CBF0/CBFZ (full codebook), CBP0/CBPZ (1/cbParts of the next codebook),
//         CPL0/CPLZ (palette), VPT0/VPTZ (8-bit pointers), VPTR/VPRZ (hicolor pointer codes).
// "Z" chunks are Format80 (LCW) compressed; a leading 0 byte selects the relative variant.
//
// Dune 2000 PC: every movie is version 2, **hicolor** (colours = 0, flags 0x10): 640x400,
// 4x4 or 4x2 blocks of RGB555 pixels, 15 fps, 22050 Hz mono 16-bit IMA ADPCM. The palettized
// variant (Tiberian Dawn / Red Alert) is decoded too.

#include "formats/ima_adpcm.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct VqaHeader {
    int version = 0;
    int flags = 0;       // bit 0: has audio; bit 4: hicolor
    int frames = 0;
    int width = 0, height = 0;
    int blockW = 0, blockH = 0;
    int fps = 15;
    int cbParts = 0;     // frames per partial-codebook group (CBP chunks)
    int colors = 0;      // palette entries; 0 = hicolor
    int maxBlocks = 0;   // codebook entries
    int sampleRate = 0;  // 0 = no audio
    int channels = 0;
    int bits = 0;

    bool hicolor() const { return colors == 0 || (flags & 0x10) != 0; }
    bool hasAudio() const { return (flags & 1) != 0 && sampleRate > 0; }
};

// Format80 / LCW decompression into `out` (resized to what was produced, at most `maxOut`).
// `relative` = the variant whose absolute-copy commands count back from the write position
// (chunks that start with a 0 byte; the caller strips it). Returns false on malformed input.
bool decompressFormat80(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& out, std::size_t maxOut,
                        bool relative);

class VqaDecoder {
public:
    VqaDecoder();
    ~VqaDecoder();
    VqaDecoder(const VqaDecoder&) = delete;
    VqaDecoder& operator=(const VqaDecoder&) = delete;

    // Opens a movie file and reads its header. A 0-byte or broken file fails (error()).
    bool open(const std::string& path);
    // Same from memory (tests).
    bool openMemory(std::vector<std::uint8_t> data);
    const std::string& error() const { return error_; }
    const VqaHeader& header() const { return hd_; }

    // Decodes the next frame (and the audio chunks that precede it). False at the end.
    bool nextFrame();
    int frameIndex() const { return frame_ - 1; }  // index of the frame in pixels()
    // The current frame, width*height pixels 0xAARRGGBB (A = 0xFF).
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    // Audio decoded since the last call: interleaved signed 16-bit at header().sampleRate.
    std::vector<std::int16_t> takeAudio();

private:
    bool readChunkHeader(std::uint32_t& id, std::uint32_t& size);
    bool readBytes(std::vector<std::uint8_t>& buf, std::uint32_t size);
    void skipBytes(std::uint32_t size);
    bool startStream();
    void handleAudio(std::uint32_t id, std::span<const std::uint8_t> data);
    bool handleFrame(std::span<const std::uint8_t> data);
    bool setCodebook(std::span<const std::uint8_t> data, bool compressed);
    bool unpack(std::span<const std::uint8_t> data, bool compressed, std::vector<std::uint8_t>& out,
                std::size_t maxOut);
    void drawPalettized(std::span<const std::uint8_t> ptr);
    void drawHicolor(std::span<const std::uint8_t> codes);
    void putBlock(int block, int entry);

    std::unique_ptr<std::istream> in_;
    std::string error_;
    VqaHeader hd_;
    int frame_ = 0;
    std::vector<std::uint8_t> codebook_;    // entries of blockW*blockH bytes (x2 when hicolor)
    std::vector<std::uint8_t> cbPending_;   // CBP chunks collected for the next codebook
    int cbPendingParts = 0;
    bool cbPendingZ = false;
    std::uint32_t palette_[256]{};
    std::vector<std::uint32_t> pixels_;
    std::vector<std::uint8_t> chunk_, scratch_;
    std::vector<std::int16_t> audio_;
    ImaAdpcmState ima_[2];
};

}  // namespace d2k::formats
