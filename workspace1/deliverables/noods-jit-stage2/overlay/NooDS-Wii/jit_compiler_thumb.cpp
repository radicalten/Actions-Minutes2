// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_compiler.h"
namespace Jit {
using namespace JitPpc;
bool compileThumb(Writer &w,uint16_t opcode) {
    // Ranges follow interpreter_lookup.cpp's 1024-entry THUMB dispatch.
    unsigned index=opcode>>6, rd=opcode&7, rs=(opcode>>3)&7;
    if (index<0x60) {
        shiftImmediate(w,index>>5,rs+14,(opcode>>6)&31,true);
        w.emit(bitOr(rd+14,4,4)); flagsNZ(w,4);
    } else if (index<0x80) {
        w.emit(bitOr(5,rs+14,rs+14));
        if (index<0x70) w.emit(bitOr(4,((opcode>>6)&7)+14,((opcode>>6)&7)+14));
        else constant(w,4,(opcode>>6)&7);
        dataProcessing(w,(opcode&0x200)?2:4,true,rd);
    } else if (index<0x100) {
        rd=(opcode>>8)&7;
        w.emit(bitOr(5,rd+14,rd+14)); constant(w,4,opcode&255);
        const unsigned ops[4]={13,10,4,2};
        dataProcessing(w,ops[(opcode>>11)&3],true,rd);
    } else if (index<0x110) {
        unsigned sub=index-0x100;
        if (sub==13) return false; // MUL: CPU-specific timing not implemented yet.
        if (sub==2 || sub==3 || sub==4 || sub==7) {
            unsigned type=sub==2?0:sub==3?1:sub==4?2:3;
            shiftRegister(w,type,rd+14,rs+14,true);
            w.emit(bitOr(rd+14,4,4)); flagsNZ(w,4);
        } else {
            w.emit(bitOr(5,rd+14,rd+14)); w.emit(bitOr(4,rs+14,rs+14));
            const unsigned ops[16]={0,1,0,0,0,5,6,0,8,2,10,11,12,0,14,15};
            if (sub==9) w.emit(addi(5,0,0)); // NEG = 0 - source
            dataProcessing(w,ops[sub],true,rd);
            if (sub==9) { // The reference NEG handler always clears V.
                w.emit(addi(6,0,0)); w.emit(rlwimi(29,6,0,3,3));
            }
        }
    } else if (index<0x11C) {
        rd=((opcode>>4)&8)|(opcode&7); rs=(opcode>>3)&15;
        if (rd==15 || rs==15) return false;
        unsigned type=(opcode>>8)&3;
        w.emit(bitOr(5,rd+14,rd+14)); w.emit(bitOr(4,rs+14,rs+14));
        dataProcessing(w,type==0?4:type==1?10:13,type==1,rd);
    } else if (index>=0x2A0 && index<=0x2BF) { // ADD Rd,SP,#imm
        w.emit(addi(((opcode>>8)&7)+14,27,(opcode&255)<<2));
    } else if (index>=0x2C0 && index<=0x2C3) { // ADD/SUB SP,#imm
        int offset=(opcode&127)<<2; if (opcode&128) offset=-offset;
        w.emit(addi(27,27,static_cast<int16_t>(offset)));
    } else return false;
    finish(w,1);
    return w.good();
}
}
