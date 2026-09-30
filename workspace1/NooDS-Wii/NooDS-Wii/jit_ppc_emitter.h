// SPDX-License-Identifier: GPL-3.0-or-later
// PowerPC 750 instruction encoder for the native ALU path.
#pragma once
#include <cstddef>
#include <cstdint>

namespace JitPpc {
// Logical encoders take (destination, source, source), even though the
// instruction fields are RS, RA, RB. Subtraction is subf(d, a, b) = b-a.
uint32_t addi(unsigned d, unsigned a, int16_t v);
uint32_t addis(unsigned d, unsigned a, int16_t v);
uint32_t addic(unsigned d, unsigned a, int16_t v, bool record = false);
uint32_t ori(unsigned d, unsigned s, uint16_t v);
uint32_t oris(unsigned d, unsigned s, uint16_t v);
uint32_t xori(unsigned d, unsigned s, uint16_t v);
uint32_t andi(unsigned d, unsigned s, uint16_t v); // Always records CR0.
uint32_t add(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t addc(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t adde(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t subf(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t subfc(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t subfe(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t mullw(unsigned d, unsigned a, unsigned b, bool oe=false, bool rc=false);
uint32_t mulhw(unsigned d, unsigned a, unsigned b, bool rc=false);
uint32_t mulhwu(unsigned d, unsigned a, unsigned b, bool rc=false);
uint32_t bitAnd(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t andc(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t bitOr(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t bitXor(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t nor(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t slw(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t srw(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t sraw(unsigned d, unsigned s, unsigned b, bool rc=false);
uint32_t srawi(unsigned d, unsigned s, unsigned n, bool rc=false);
uint32_t extsb(unsigned d, unsigned s, bool rc=false);
uint32_t extsh(unsigned d, unsigned s, bool rc=false);
uint32_t rlwinm(unsigned d, unsigned s, unsigned sh, unsigned mb, unsigned me, bool rc=false);
uint32_t rlwimi(unsigned d, unsigned s, unsigned sh, unsigned mb, unsigned me, bool rc=false);
uint32_t rlwnm(unsigned d, unsigned s, unsigned b, unsigned mb, unsigned me, bool rc=false);
uint32_t lwz(unsigned d, unsigned a, int16_t offset);
uint32_t lbz(unsigned d, unsigned a, int16_t offset);
uint32_t lhz(unsigned d, unsigned a, int16_t offset);
uint32_t stw(unsigned s, unsigned a, int16_t offset);
uint32_t stwu(unsigned s, unsigned a, int16_t offset);
uint32_t stb(unsigned s, unsigned a, int16_t offset);
uint32_t sth(unsigned s, unsigned a, int16_t offset);
uint32_t lwbrx(unsigned d, unsigned a, unsigned b);
uint32_t lhbrx(unsigned d, unsigned a, unsigned b);
uint32_t stwbrx(unsigned s, unsigned a, unsigned b);
uint32_t sthbrx(unsigned s, unsigned a, unsigned b);
uint32_t lbzx(unsigned d, unsigned a, unsigned b);
uint32_t stbx(unsigned s, unsigned a, unsigned b);
uint32_t cmpw(unsigned field, unsigned a, unsigned b);
uint32_t cmplw(unsigned field, unsigned a, unsigned b);
uint32_t cmpwi(unsigned field, unsigned a, int16_t v);
uint32_t cmplwi(unsigned field, unsigned a, uint16_t v);
uint32_t mfcr(unsigned d);
uint32_t mtcrf(unsigned mask, unsigned s);
uint32_t mfspr(unsigned d, unsigned spr);
uint32_t mtspr(unsigned spr, unsigned s);
uint32_t bclr(unsigned bo=20, unsigned bi=0, bool link=false);
uint32_t bcctr(unsigned bo=20, unsigned bi=0, bool link=false);
// Relative branch offsets are from the branch instruction, NOT its successor.
// Range/alignment failures leave output untouched. Never silently truncate.
bool branch(int64_t delta, bool link, uint32_t &word);
bool branchCond(unsigned bo, unsigned bi, int64_t delta, bool link, uint32_t &word);

// Bounded, non-owning staging writer. It is NOT an executable arena and does
// not publish code or perform cache synchronization. A failed writer must
// never be published. reset() explicitly discards all prior output.
class Writer {
public:
    Writer(uint32_t *words, size_t capacity): words_(words), capacity_(capacity) {}
    bool emit(uint32_t word);
    bool patchBranch(size_t from, size_t to, bool link=false);
    bool patchBranchCond(size_t from, size_t to, unsigned bo, unsigned bi, bool link=false);
    void reset() { count_=0; failed_=false; }
    size_t size() const { return count_; }
    bool good() const { return !failed_; }
private:
    uint32_t *words_;
    size_t capacity_, count_=0;
    bool failed_=false;
};
} // namespace JitPpc
