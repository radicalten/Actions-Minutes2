// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_compiler.h"
namespace Jit {
using namespace JitPpc;
// The pinned signed timing loop accepts [-256,255], then [-65536,65535],
// then [-16777216,16777215]. Sign-fold before testing upper bytes. r9 is
// captured BEFORE writing guest destinations (including destination aliases).
static void signedTiming(Writer &w,unsigned base) {
    w.emit(srawi(7,9,31)); w.emit(bitXor(7,9,7));
    w.emit(addi(3,0,base+1));
    for (unsigned shift=8;shift<=24;shift+=8) {
        w.emit(rlwinm(8,7,32-shift,shift,31)); w.emit(cmpwi(0,8,0));
        size_t zero=w.size(); w.emit(0);
        w.emit(addi(3,3,1)); w.patchBranchCond(zero,w.size(),12,2);
    }
    w.emit(bclr());
}
bool compileMultiply(Writer &w,uint32_t opcode,unsigned kind,bool arm7) {
    // Kinds: MUL[S], MLA[S], UMULL[S], UMLAL[S], SMULL[S], SMLAL[S].
    if (kind>=12) return false;
    unsigned hi=(opcode>>16)&15, lo=(opcode>>12)&15, rm=opcode&15, rs=(opcode>>8)&15;
    if (hi==15 || lo==15 || rm==15 || rs==15) return false;
    bool flags=kind&1, accumulate=kind&2, wide=kind>=4, isSigned=kind>=8;
    w.emit(bitOr(4,rm+14,rm+14)); w.emit(bitOr(5,rs+14,rs+14));
    if (arm7) w.emit(bitOr(9,5,5));
    w.emit(mullw(3,4,5));
    if (wide) {
        w.emit(isSigned ? mulhw(6,4,5) : mulhwu(6,4,5));
        if (accumulate) {
            w.emit(addc(3,3,lo+14)); w.emit(adde(6,6,hi+14));
        }
        // Match low-then-high interpreter write order, even RdLo == RdHi.
        w.emit(bitOr(lo+14,3,3)); w.emit(bitOr(hi+14,6,6));
        if (flags) {
            w.emit(bitOr(7,3,6,true)); w.emit(mfcr(8));
            w.emit(rlwimi(29,6,0,0,0)); // N from high word.
            w.emit(rlwimi(29,8,1,1,1)); // Z from BOTH result words.
        }
    } else {
        if (accumulate) w.emit(add(3,3,lo+14));
        w.emit(bitOr(hi+14,3,3));
        if (flags) flagsNZ(w,3);
    }
    if (!arm7) finish(w,(wide?3:2)+(flags?2:0));
    else if (wide && !isSigned) {
        // Source quirk: unsigned op3 < unsigned(-1 << n) OR op3 >= (1 << n)
        // is true for all words for n=8,16,24. Thus its m is always 4.
        finish(w,6+(accumulate?1:0));
    } else signedTiming(w,(wide?2:1)+(accumulate?1:0));
    return w.good();
}
bool compileThumbMultiply(Writer &w,unsigned rd,unsigned rs,bool arm7) {
    // THUMB timing uses original Rd, not Rs.
    if (arm7) w.emit(bitOr(9,rd+14,rd+14));
    w.emit(mullw(3,rd+14,rs+14));
    w.emit(bitOr(rd+14,3,3)); flagsNZ(w,3);
    if (arm7) signedTiming(w,1); else finish(w,4);
    return w.good();
}
}
