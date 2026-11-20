#pragma once
#include <cstdint>

// Pure packing helpers shared by the Wii renderer and host architecture tests.
// Inputs must be contiguous mapped DS texture/palette spans; callers gate them.
namespace NoodsGx {
inline uint16_t paletteRgb5A3(uint16_t ds, bool transparent) {
    return transparent ? 0 : (uint16_t)(0x8000u | ((ds & 31u) << 10) |
                                      (ds & 0x3E0u) | ((ds >> 10) & 31u));
}
inline void packPalette(const uint8_t *src, uint8_t *dst, unsigned entries, bool trans0) {
    for (unsigned i = 0; i < entries; ++i) {
        const uint16_t v = paletteRgb5A3((uint16_t)(src[i*2] | (src[i*2+1] << 8)), trans0 && i == 0);
        dst[i*2] = v >> 8; dst[i*2+1] = v;
    }
}
// GX CI8 tiles are 8 by 4, unlike RGB5A3's 4 by 4. Even DS 2/4-bit
// indices are expanded only to indices, leaving palette lookup to GX.
inline void packIndices(const uint8_t *src, uint8_t *dst, unsigned w, unsigned h, unsigned fmt) {
    const unsigned shift = fmt == 2 ? 2 : (fmt == 3 ? 1 : 0);
    const unsigned bits = 8u >> shift, mask = (1u << bits) - 1u;
    for (unsigned ty = 0; ty < h; ty += 4)
        for (unsigned tx = 0; tx < w; tx += 8)
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 8; ++x) {
                    const unsigned pos = (ty+y)*w+tx+x;
                    *dst++ = (src[pos >> shift] >> ((pos & ((1u << shift)-1u))*bits)) & mask;
                }
}
}
