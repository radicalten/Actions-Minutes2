// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_compiler.h"
#include "jit.h"
namespace Jit {
using namespace JitPpc;
static void exitBranch(Writer &w) {
    // r11 is the raw resume address. The C++ exit applies the ORIGINAL refill,
    // not a duplicated implementation of guest fetch/mapping behavior.
    w.emit(addi(12,0,1));
    w.emit(stw(11,30,JIT_CTX_RESUME)); w.emit(stw(12,30,JIT_CTX_EXIT));
    finish(w,3);
}
static void setThumb(Writer &w,bool thumb) {
    w.emit(addi(12,0,thumb?1:0)); w.emit(rlwimi(29,12,5,26,26));
}
static void targetRegister(Writer &w,unsigned guest,uint32_t pcValue) {
    if (guest==15) constant(w,11,pcValue);
    else w.emit(bitOr(11,guest+14,guest+14));
}
bool compileArmBranch(Writer &w,uint32_t op,unsigned kind,bool arm7,uint32_t pc) {
    if (kind<45 || kind>49) return false;
    if ((kind==48 || kind==49) && arm7) { finish(w,1); return w.good(); }
    if (kind==47 || kind==48) { // BX / BLX register
        targetRegister(w,op&15,pc+8);
        if (kind==48) constant(w,28,pc+4);
        w.emit(rlwimi(29,11,5,26,26)); // Entry T=0; source bit0 selects ISA.
    } else {
        uint32_t offset=(op&0xFFFFFF)<<2;
        if (op&0x800000) offset|=0xFC000000; // Sign extend without signed-shift UB.
        if (kind==49) offset|=(op>>23)&2;
        constant(w,11,pc+8+offset);
        if (kind==46 || kind==49) constant(w,28,pc+4);
        if (kind==49) setThumb(w,true);
    }
    exitBranch(w); return w.good();
}
static void conditionValue(Writer &w,unsigned cond) {
    // Boolean values in r4=N,r5=Z,r6=C,r7=V; result r8. Do not alter CPSR.
    w.emit(rlwinm(4,29,1,31,31)); w.emit(rlwinm(5,29,2,31,31));
    w.emit(rlwinm(6,29,3,31,31)); w.emit(rlwinm(7,29,4,31,31));
    switch (cond>>1) {
    case 0: w.emit(bitOr(8,5,5)); break;
    case 1: w.emit(bitOr(8,6,6)); break;
    case 2: w.emit(bitOr(8,4,4)); break;
    case 3: w.emit(bitOr(8,7,7)); break;
    case 4: w.emit(xori(8,5,1)); w.emit(bitAnd(8,8,6)); break; // HI
    case 5: w.emit(bitXor(8,4,7)); w.emit(xori(8,8,1)); break; // GE
    case 6:
        w.emit(bitXor(8,4,7)); w.emit(bitOr(8,8,5)); w.emit(xori(8,8,1)); break; // GT
    }
    if (cond&1) w.emit(xori(8,8,1));
}
bool compileThumbBranch(Writer &w,uint16_t op,bool arm7,uint32_t pc) {
    unsigned index=op>>6;
    if (index>=0x11C && index<=0x11F) { // BX / BLX register
        bool link=index>=0x11E;
        if (link && arm7) { finish(w,1); return w.good(); }
        targetRegister(w,(op>>3)&15,pc+4);
        if (link) constant(w,28,pc+3);
        w.emit(rlwimi(29,11,5,26,26));
        exitBranch(w);
    } else if (index>=0x340 && index<=0x377) {
        conditionValue(w,(op>>8)&15);
        w.emit(cmpwi(0,8,0)); size_t skip=w.size(); w.emit(0);
        int32_t offset=int32_t(op&255)-((op&128)?256:0);
        constant(w,11,pc+4+uint32_t(offset*2)); exitBranch(w);
        w.patchBranchCond(skip,w.size(),12,2);
        finish(w,1);
    } else if (index>=0x380 && index<=0x39F) {
        int32_t offset=int32_t(op&0x7FF)-((op&0x400)?2048:0);
        constant(w,11,pc+4+uint32_t(offset*2)); exitBranch(w);
    } else if (index>=0x3C0 && index<=0x3DF) { // BL/BLX setup half
        int32_t offset=int32_t(op&0x7FF)-((op&0x400)?2048:0);
        constant(w,28,pc+4+uint32_t(offset*4096)); finish(w,1);
    } else if ((index>=0x3A0 && index<=0x3BF) || index>=0x3E0) {
        bool exchange=index<0x3C0;
        if (exchange && arm7) { finish(w,1); return w.good(); }
        w.emit(addi(11,28,(op&0x7FF)<<1)); // Capture old LR BEFORE writing it.
        constant(w,28,pc+3);
        if (exchange) setThumb(w,false);
        exitBranch(w);
    } else return false;
    return w.good();
}
}
