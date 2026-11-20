#pragma once
#include <stdint.h>
// buildWindowMask chooses a control byte from these four. If each byte has
// the BG bit, the resulting mask necessarily has it for every x.
static inline bool gbaAffineAllWindowsAllow(uint16_t winIn, uint16_t winOut, uint8_t bgBit) {
    return (winIn & (winIn >> 8) & winOut & (winOut >> 8) & bgBit) != 0;
}
