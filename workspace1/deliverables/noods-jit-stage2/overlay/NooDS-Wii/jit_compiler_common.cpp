// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_compiler.h"
namespace Jit {
using namespace JitPpc;
void constant(Writer &w,unsigned d,uint32_t v) {
    // Explicit halves avoid signed low-half adjustment mistakes.
    w.emit(addis(d,0,static_cast<int16_t>(v>>16)));
    w.emit(ori(d,d,static_cast<uint16_t>(v)));
}
void flagsNZ(Writer &w,unsigned value) {
    w.emit(bitOr(value,value,value,true)); w.emit(mfcr(8));
    w.emit(rlwimi(29,8,0,0,0)); w.emit(rlwimi(29,8,1,1,1));
}
static void carryBit(Writer &w,unsigned src,unsigned bit) {
    w.emit(rlwimi(29,src,(29-bit)&31,2,2));
}
void shiftImmediate(Writer &w,unsigned type,unsigned src,unsigned n,bool carry) {
    switch(type) {
    case 0: // LSL (zero preserves C)
        w.emit(rlwinm(4,src,n,0,31-n));
        if (carry && n) carryBit(w,src,32-n);
        break;
    case 1: // LSR #0 means #32
        w.emit(n ? rlwinm(4,src,32-n,n,31) : addi(4,0,0));
        if (carry) carryBit(w,src,n ? n-1 : 31);
        break;
    case 2: // ASR #0 means #32
        w.emit(srawi(4,src,n ? n : 31));
        if (carry) carryBit(w,src,n ? n-1 : 31);
        break;
    case 3:
        if (n) w.emit(rlwinm(4,src,32-n,0,31));
        else { // RRX consumes the original CPSR.C before replacing it.
            w.emit(rlwinm(6,29,2,0,0));
            w.emit(rlwinm(4,src,31,1,31)); w.emit(bitOr(4,4,6));
        }
        if (carry) carryBit(w,src,n ? n-1 : 0);
        break;
    }
}
void shiftRegister(Writer &w,unsigned type,unsigned src,unsigned count,bool carry) {
    w.emit(rlwinm(6,count,0,24,31)); // ARM uses the bottom BYTE, not 5/6 bits.
    w.emit(bitOr(4,src,src));
    w.emit(cmpwi(0,6,0)); size_t zero=w.size(); w.emit(0);
    if (type==3) {
        w.emit(rlwinm(7,6,0,27,31));
        w.emit(addi(8,0,32)); w.emit(subf(7,7,8));
        w.emit(rlwnm(4,src,7,0,31));
        if (carry) carryBit(w,4,31);
    } else {
        w.emit(cmpwi(0,6,32)); size_t small=w.size(); w.emit(0);
        size_t end=0;
        if (type==2) w.emit(addi(6,0,32)); // ASR saturates at sign-fill.
        else {
            w.emit(addi(4,0,0)); if (carry) carryBit(w,4,0);
            end=w.size(); w.emit(0);
        }
        w.patchBranchCond(small,w.size(),4,1); // not GT => count <= 32
        if (carry) {
            if (type==0) { w.emit(addi(7,0,32)); w.emit(subf(7,6,7)); }
            else w.emit(addi(7,6,-1));
            w.emit(srw(8,src,7)); carryBit(w,8,0);
        }
        w.emit(type==0 ? slw(4,src,6) : type==1 ? srw(4,src,6) : sraw(4,src,6));
        if (end) w.patchBranch(end,w.size());
    }
    w.patchBranchCond(zero,w.size(),12,2); // EQ => unchanged value and carry
}
void dataProcessing(Writer &w,unsigned op,bool flags,unsigned rd) {
    bool arithmetic=(op>=2 && op<=7) || op==10 || op==11;
    if (op>=5 && op<=7) {
        w.emit(rlwinm(6,29,0,2,2)); w.emit(mtspr(1,6));
    }
    switch(op) {
    case 0: case 8: w.emit(bitAnd(3,5,4)); break;
    case 1: case 9: w.emit(bitXor(3,5,4)); break;
    case 2: case 10: w.emit(subfc(3,4,5,flags)); break;
    case 3: w.emit(subfc(3,5,4,flags)); break;
    case 4: case 11: w.emit(addc(3,5,4,flags)); break;
    case 5: w.emit(adde(3,5,4,flags)); break;
    case 6: w.emit(subfe(3,4,5,flags)); break;
    case 7: w.emit(subfe(3,5,4,flags)); break;
    case 12: w.emit(bitOr(3,5,4)); break;
    case 13: w.emit(bitOr(3,4,4)); break;
    case 14: w.emit(andc(3,5,4)); break;
    case 15: w.emit(nor(3,4,4)); break;
    }
    if (flags) {
        if (arithmetic) {
            w.emit(mfspr(7,1));
            if (op==7) { // Preserve pinned interpreter RSCS old-C/V OR quirk.
                w.emit(rlwinm(6,7,0,2,2)); w.emit(bitOr(29,29,6));
                w.emit(rlwinm(6,7,30,3,3)); w.emit(bitOr(29,29,6));
            } else {
                w.emit(rlwimi(29,7,0,2,2)); w.emit(rlwimi(29,7,30,3,3));
            }
        }
        flagsNZ(w,3);
    }
    if (op<8 || op>11) w.emit(bitOr(rd+14,3,3));
}
void finish(Writer &w,unsigned cycles) {
    w.emit(addi(3,0,static_cast<int16_t>(cycles))); w.emit(bclr());
}
}
