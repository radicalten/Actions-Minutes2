// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_compiler.h"
namespace Jit {
using namespace JitPpc;
#include "jit_decode_arm.inc"
unsigned armClass(uint32_t opcode) {
    return armNativeClass[((opcode>>16)&0xFF0)|((opcode>>4)&15)];
}
bool compileArm(Writer &w,uint32_t opcode,bool arm7,uint32_t pc) {
    unsigned kind=armClass(opcode);
    // The interpreter routes conditions before entry; only reserved BLX is native.
    if ((opcode>>28)==15) {
        if ((opcode&0x0E000000)!=0x0A000000) return false;
        return compileArmBranch(w,opcode,49,arm7,pc); // Reserved-condition BLX only.
    }
    if (!kind) return false;
    if (kind>=45) return compileArmBranch(w,opcode,kind,arm7,pc);
    if (kind>32) return compileMultiply(w,opcode,kind-33,arm7);
    unsigned rd=(opcode>>12)&15, rn=(opcode>>16)&15, rm=opcode&15, rs=(opcode>>8)&15;
    bool immediate=opcode&(1u<<25), byRegister=!immediate && (opcode&16);
    // Conservative: exclude even unused PC fields in tests and MOV forms.
    if (rd==15 || rn==15 || (!immediate && (rm==15 || (byRegister && rs==15)))) return false;
    unsigned op=(kind-1)>>1; bool flags=(kind-1)&1;
    bool shifterCarry=flags && (op==0 || op==1 || op==8 || op==9 || op>=12);
    w.emit(bitOr(5,rn+14,rn+14));
    if (immediate) {
        unsigned rotation=(opcode>>7)&30;
        uint32_t value=opcode&255;
        if (rotation) value=(value>>rotation)|(value<<(32-rotation));
        constant(w,4,value);
        if (rotation && shifterCarry) w.emit(rlwimi(29,4,30,2,2));
    } else if (byRegister) shiftRegister(w,(opcode>>5)&3,rm+14,rs+14,shifterCarry);
    else shiftImmediate(w,(opcode>>5)&3,rm+14,(opcode>>7)&31,shifterCarry);
    dataProcessing(w,op,flags,rd);
    finish(w,byRegister ? 2 : 1);
    return w.good();
}
}
