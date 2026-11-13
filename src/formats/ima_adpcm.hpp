#pragma once
// IMA ADPCM nibble decoder (the standard IMA/DVI step and index tables) as used by Westwood's
// .AUD files (compression 99) and the SND2 chunks of VQA movies: low nibble first, the state
// carries over from one chunk to the next.

#include <algorithm>
#include <cstdint>

namespace d2k::formats {

struct ImaAdpcmState {
    int predictor = 0;
    int index = 0;

    std::int16_t decode(int nibble) {
        static constexpr int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
        static constexpr int kStep[89] = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
            25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
            88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
            307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
            1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
            3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
        const int step = kStep[index];
        int diff = step >> 3;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 4) diff += step;
        if (nibble & 8) predictor -= diff;
        else predictor += diff;
        predictor = std::clamp(predictor, -32768, 32767);
        index = std::clamp(index + kIndex[nibble & 15], 0, 88);
        return static_cast<std::int16_t>(predictor);
    }
};

}  // namespace d2k::formats
