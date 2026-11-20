#pragma once
#include <cstdint>
#include <cstring>
#include "gx_indexed.h"
namespace NoodsGx2D {
inline uint32_t unpack565(uint16_t p) {
    // Original RGB5 colors survived GX RGB8 expansion and RGB565 copying.
    // Drop the replicated low green bit, then use DS's even 5->6 expansion.
    return ((p>>10)&0x3Eu)|((p&0x7C0u)<<1)|((p&31u)<<13);
}

// Inspect all palette bytes on each call: no generation-counter assumption.
// A changed guest byte invalidates both converted variants before reuse.
struct PaletteCache {
    uint8_t raw[512] = {}, converted[2][512] = {};
    unsigned valid = 0, conversions = 0;
    const uint8_t *get(const uint8_t *src, bool eight) {
        if (!valid || std::memcmp(raw,src,512)) {
            std::memcpy(raw,src,512); valid=0;
        }
        const unsigned kind=eight?1:0;
        if (!(valid & (1u<<kind))) {
            NoodsGx::packPalette(raw,converted[kind],256,true);
            if (!eight) for (unsigned bank=1;bank<16;++bank)
                converted[kind][bank*32]=converted[kind][bank*32+1]=0;
            valid |= 1u<<kind; ++conversions;
        }
        return converted[kind];
    }
};
// Frame-local immutable storage; reset only after the previous GX fence.
struct alignas(32) PalettePool {
    static constexpr unsigned CAPACITY=768;
    uint8_t raw[512], values[CAPACITY][512];
    const uint8_t *current[2]={nullptr,nullptr};
    unsigned count=0;
    bool valid=false;
    void reset() {count=0;valid=false;current[0]=current[1]=nullptr;}
    const uint8_t *get(const uint8_t *src, bool eight) {
        if (!valid || std::memcmp(raw,src,512)) {
            std::memcpy(raw,src,512);valid=true;current[0]=current[1]=nullptr;
        }
        const unsigned k=eight?1:0;
        if (current[k]) return current[k];
        if (count==CAPACITY) return nullptr; // caller retains embedded fallback
        uint8_t *dst=values[count++];
        NoodsGx::packPalette(raw,dst,256,true);
        if (!eight) for (unsigned bank=1;bank<16;++bank) dst[bank*32]=dst[bank*32+1]=0;
        return current[k]=dst;
    }
};
// Transaction-local GX small-TLUT residency. Pointer identity is safe only
// because every palette snapshot stays immutable until the completion fence.
struct TlutSlots {
    static constexpr unsigned CAPACITY=16;
    const uint8_t *resident[CAPACITY]={};
    unsigned next=0;
    unsigned get(const uint8_t *palette,bool &upload,unsigned pinned=0) {
        for (unsigned i=0;i<CAPACITY;++i) if (resident[i]==palette) {
            upload=false;return i;
        }
        // A layered draw pins at most four of the sixteen slots. Do not evict
        // an earlier layer while resolving the remaining layers for one quad.
        while (pinned&(1u<<next)) next=(next+1)%CAPACITY;
        const unsigned slot=next;
        next=(next+1)%CAPACITY;resident[slot]=palette;upload=true;return slot;
    }
};
// Stable per-BG TLUT names avoid rebinding atlas textures for changing palette
// snapshots. Equal pointers may share an earlier active BG's slot this draw.
struct LayerTlutSlots {
    const uint8_t *resident[4]={},*selected[4]={};
    unsigned chosen[4]={};
    void begin() {for (unsigned i=0;i<4;++i) selected[i]=nullptr;}
    unsigned get(unsigned bg,const uint8_t *palette,bool &upload) {
        unsigned slot=bg;
        for (unsigned i=0;i<bg;++i) if (selected[i]==palette) {slot=chosen[i];break;}
        selected[bg]=palette;chosen[bg]=slot;
        upload=resident[slot]!=palette;resident[slot]=palette;
        return slot;
    }
};
struct AtlasSlice {unsigned base,height;};
inline AtlasSlice atlasSlice(unsigned y,unsigned end) {
    const unsigned base=y&~3u;
    return {base,((end+3u)&~3u)-base};
}
inline unsigned orderedLayers(unsigned enabled,const uint8_t *priority,uint8_t *order) {
    unsigned count=0;
    for (int p=3;p>=0;--p) for (int bg=3;bg>=0;--bg)
        if ((enabled&(1u<<bg)) && priority[bg]==p) order[count++]=bg;
    return count;
}
inline bool eligibleBitmap(bool engineB,uint32_t dc,uint16_t cnt,uint16_t blend,uint8_t by,uint16_t master) {
    const unsigned mode=dc&7;
    const unsigned effect=(blend>>6)&3;
    return engineB && ((dc>>16)&3)==1 && mode>=3 && mode<=5 &&
        (dc&0x1F00u)==0x800u && !(dc&((1u<<3)|0xE000u)) &&
        (cnt&0x84u)==0x84u && !(cnt&(1u<<6)) && !(master&0xC000u) &&
        (effect==0 || (effect>=2 && by==0));
}
inline uint32_t bitmapPair(uint32_t little) {
    const uint32_t a=(little&0xFFFFu)&(0u-((little>>15)&1u));
    const uint32_t b=(little>>16)&(0u-(little>>31));
    return (a<<16)|b;
}
inline uint16_t bitmapPixel(uint16_t pixel) {
    // RGB5A3 bit15=0 would otherwise expose partial alpha, unlike DS bit15=0.
    // Keep DS BGR5 channel order; the dedicated GX stage swaps texture R/B.
    return (pixel&0x8000u)?pixel:0;
}
inline bool eligible(bool engineB, uint32_t dc, uint16_t blend, uint16_t master) {
    return engineB && ((dc>>16)&3)==1 && !(dc&7) &&
        !(dc&((1u<<3)|(1u<<12)|0xE000u|(1u<<30))) &&
        (dc&0xF00u) && !(blend&0xC0u) && !(master&0xC000u);
}
inline uint32_t fourPixels(uint32_t v, bool reverse) {
    if (reverse) return ((v&0xF000u)<<12)|((v&0xF00u)<<8)|((v&0xF0u)<<4)|(v&15u);
    return ((v&15u)<<24)|((v&0xF0u)<<12)|(v&0xF00u)|((v>>12)&15u);
}
inline void storeBe32(uint8_t *dst, uint32_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value=__builtin_bswap32(value);
#endif
    std::memcpy(dst,&value,4);
}
// Output a contiguous eight-pixel row. The caller owns clipping/CI8 tiling.
inline void linearTileRow(uint8_t *out, uint16_t tile, uint32_t lo, uint32_t hi, bool eight) {
    const bool flip=tile&(1u<<10);
    if (eight) {
        storeBe32(out,flip?hi:__builtin_bswap32(lo));
        storeBe32(out+4,flip?lo:__builtin_bswap32(hi));
    } else {
        const uint32_t bank=((tile>>8)&0xF0u)*0x01010101u;
        storeBe32(out,bank|fourPixels(flip?lo>>16:lo,flip));
        storeBe32(out+4,bank|fourPixels(flip?lo:lo>>16,flip));
    }
}
inline void tileRow(uint8_t *dst, int left, uint16_t tile, uint32_t lo, uint32_t hi, bool eight) {
#if NOODS_GX_2D_IO >= 3
    // A full aligned row stays inside one GX CI8 tile: two word stores replace
    // eight indexed byte stores. Edge/clipped/unaligned rows keep scalar code.
    if ((unsigned)left<=248u && !(left&7)) {
        uint8_t *out=dst+(left/8)*32;
        const bool flip=tile&(1u<<10);
        if (eight) {
            storeBe32(out,flip?hi:__builtin_bswap32(lo));
            storeBe32(out+4,flip?lo:__builtin_bswap32(hi));
        } else {
            const uint32_t bank=((tile>>8)&0xF0u)*0x01010101u;
            storeBe32(out,bank|fourPixels(flip?lo>>16:lo,flip));
            storeBe32(out+4,bank|fourPixels(flip?lo:lo>>16,flip));
        }
        return;
    }
#endif
    for (int k=0;k<8;++k) {
        const int x=left+k;
        if ((unsigned)x>=256u) continue;
        const int sx=(tile&(1u<<10))?7-k:k;
        const unsigned idx=eight?((sx<4?lo:hi)>>((sx&3)*8))&255u:(lo>>(sx*4))&15u;
        dst[(x/8)*32+(x&7)]=eight?idx:((tile>>8)&0xF0u)|idx;
    }
}
}
