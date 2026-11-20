#pragma once
#include <cstdint>
// Only ATTR0/ATTR1 determine scanline membership. ATTR2 and affine-matrix
// writes are reread when building the line list and do not invalidate masks.
namespace GbaObjMask {
inline bool changesMembership(const uint8_t *oam, uint32_t address, uint32_t value, unsigned bytes) {
    const unsigned off=address & (bytes==4 ? 0x3FCu : 0x3FEu);
    if (bytes==1 || (off&4u)) return false;
    uint32_t old=uint32_t(oam[off])|(uint32_t(oam[off+1])<<8);
    if (bytes==4) old|=(uint32_t(oam[off+2])<<16)|(uint32_t(oam[off+3])<<24);
    else value &= 0xFFFFu;
    return old!=value;
}
}
