#pragma once
#include <cstdint>

// Side-effect-free ARM data-processing operations. Conditions are checked by
// the caller. PC writes, register-controlled shifts and miscellaneous encodings
// stay in the original interpreter. Every accepted instruction costs one fetch
// cycle and no data/internal/branch cycles on ARM7TDMI in IWRAM.
namespace GbaArmAlu {
inline bool eligible(uint32_t op) {
    if (op&0x0C000000u) return false;
    if (!(op&0x02000000u) && (op&0x10u)) return false;
    const unsigned kind=(op>>21)&15;
    if (kind>=8 && kind<=11 && !(op&0x00100000u)) return false;
    return ((op>>12)&15)!=15;
}
inline uint32_t ror(uint32_t v,unsigned s) {
    return s ? (v>>s)|(v<<(32-s)) : v;
}
inline void execute(uint32_t **regs,uint32_t &cpsr,uint32_t op) {
    const unsigned kind=(op>>21)&15;
    const bool flags=op&0x00100000u;
    const bool arithmetic=(kind>=2 && kind<=7) || kind==10 || kind==11;
    const uint32_t oldCarry=(cpsr>>29)&1;
    uint32_t carry=oldCarry,b;
    if (op&0x02000000u) {
        const unsigned shift=(op>>7)&30;
        b=ror(op&255u,shift);
        if (shift) carry=b>>31;
    } else {
        const uint32_t v=*regs[op&15];
        const unsigned shift=(op>>7)&31;
        switch((op>>5)&3) {
        case 0: b=v<<shift;if(shift)carry=(v>>(32-shift))&1;break;
        case 1: b=shift?v>>shift:0;carry=(v>>(shift?shift-1:31))&1;break;
        case 2: b=uint32_t(int32_t(v)>>(shift?shift:31));carry=(v>>(shift?shift-1:31))&1;break;
        default:
            b=shift?ror(v,shift):(oldCarry<<31)|(v>>1);
            carry=(v>>(shift?shift-1:0))&1;break;
        }
    }
    const uint32_t a=*regs[(op>>16)&15];
    uint32_t result,overflow=0;
    if (!flags) {
        switch(kind) {
        case 0: result=a&b;break;case 1:result=a^b;break;
        case 2:result=a-b;break;case 3:result=b-a;break;
        case 4:result=a+b;break;case 5:result=a+b+oldCarry;break;
        case 6:result=a-b-1+oldCarry;break;case 7:result=b-a-1+oldCarry;break;
        case 12:result=a|b;break;case 13:result=b;break;
        case 14:result=a&~b;break;default:result=~b;break;
        }
        *regs[(op>>12)&15]=result;return;
    }
    switch(kind) {
    case 0: case 8: result=a&b;break;
    case 1: case 9: result=a^b;break;
    case 2: case 10:
        result=a-b;carry=a>=b;overflow=((a^b)&(a^result))>>31;break;
    case 3:
        result=b-a;carry=b>=a;overflow=((a^b)&(b^result))>>31;break;
    case 4: case 11:
        result=a+b;carry=result<a;overflow=(~(a^b)&(a^result))>>31;break;
    case 5:
        result=a+b+oldCarry;carry=(result<a)||(b==0xFFFFFFFFu&&oldCarry);
        overflow=(~(a^b)&(a^result))>>31;break;
    case 6:
        result=a-b-1+oldCarry;carry=(a>=b)&&((a!=b)||oldCarry);
        overflow=((a^b)&(a^result))>>31;break;
    case 7:
        result=b-a-1+oldCarry;carry=(b>=a)&&((b!=a)||oldCarry);
        overflow=((a^b)&(b^result))>>31;break;
    case 12:result=a|b;break;
    case 13:result=b;break;
    case 14:result=a&~b;break;
    default:result=~b;break;
    }
    if (kind<8 || kind>11) *regs[(op>>12)&15]=result;
    if (flags) {
        const uint32_t mask=arithmetic?0xF0000000u:0xE0000000u;
        cpsr=(cpsr&~mask)|(result&0x80000000u)|(uint32_t(result==0)<<30)|(carry<<29);
        if(arithmetic)cpsr|=overflow<<28;
    }
}
}
