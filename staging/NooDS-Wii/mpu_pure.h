// Restricted-MPU pure-instruction classifier shared by interpreter and JIT.
#pragma once
#include <stdint.h>
#ifndef NOODS_MPU_PURE_JIT
#define NOODS_MPU_PURE_JIT 0
#endif
static inline bool mpuPureArm(uint32_t op) {
    if ((op & 0x0E000000u) != 0x02000000u &&
        (op & 0x0E000010u) != 0x00000000u) return false;
    const unsigned alu = (op >> 21) & 15u;
    if (alu >= 8 && alu <= 11 && !(op & (1u << 20))) return false;
    return ((op >> 12) & 15u) != 15u;
}
static inline bool mpuPureThumb(uint16_t op) {
    return (op < 0x4000u) || ((op & 0xFC00u) == 0x4000u);
}
