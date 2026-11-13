#pragma once
// PS1 STR movies (the PS1 Dune 2000 cutscenes in DATA.XA): MDEC video (bitstream version 2)
// interleaved with CD-ROM XA ADPCM audio. Written from the public format descriptions
// (psx-spx "MDEC" and "CDROM XA Audio ADPCM", the MPEG-1 DCT coefficient VLC table B.14 the
// STR bitstream uses); details in docs/formats.md "PS1 STR".
//
// Video sector (Form 1, submode 0x48): 32-byte header {u16 0x0160, u16 0x8001, u16 chunk,
// u16 chunks, u32 frame, u32 frame bytes, u16 width, u16 height, ...} + 2016 bytes of the frame.
// Frame: {u16 MDEC code count, u16 0x3800, u16 qscale, u16 version} + bitstream (16-bit LE words,
// MSB first). Macroblocks 16x16, column by column; each Cr, Cb, Y0..Y3 8x8 blocks: DC = signed
// 10 bits, then AC VLCs (EOB "10", escape "000001" + 6-bit run + 10-bit level).
// Audio sector (Form 2, submode bit 2): 18 sound groups of 128 bytes, 4-bit ADPCM,
// coding bit 0 stereo, bit 2 18900 Hz (else 37800).

#include "formats/ps1_disc.hpp"

#include <cstdint>
#include <vector>

namespace d2k::formats::ps1 {

// One 2304-byte XA audio payload (4-bit) -> interleaved samples; `state` = {old1, old2} per
// channel, carried from sector to sector.
void decodeXaSector(const std::uint8_t* data, bool stereo, int state[2][2], std::vector<std::int16_t>& out);

// One MDEC v2 frame (from its 8-byte header on) -> width*height 0xAARRGGBB. False when the
// bitstream is not version 2 / broken (the picture is then partly drawn).
bool decodeMdecFrame(const std::uint8_t* data, std::size_t size, int width, int height, std::uint32_t* out);

class StrDecoder {
public:
    // An XA entry of `disc` (kept open by the caller). Reads ahead to learn the size and the
    // audio format.
    bool open(Disc& disc, const FatEntry& entry);
    int width() const { return w_; }
    int height() const { return h_; }
    int sampleRate() const { return rate_; }
    int channels() const { return stereo_ ? 2 : 1; }

    bool nextFrame();
    const std::vector<std::uint32_t>& pixels() const { return px_; }
    std::vector<std::int16_t> takeAudio();

private:
    Disc* disc_ = nullptr;
    std::uint32_t first_ = 0, count_ = 0, pos_ = 0;
    int w_ = 0, h_ = 0, rate_ = 0;
    bool stereo_ = false;
    int audioChannel_ = -1;
    int xaState_[2][2]{};
    std::vector<std::uint32_t> px_;
    std::vector<std::uint8_t> frame_;
    std::vector<bool> have_;
    std::uint32_t curFrame_ = 0xFFFFFFFF;
    int got_ = 0;
    std::vector<std::int16_t> audio_;
};

}  // namespace d2k::formats::ps1
