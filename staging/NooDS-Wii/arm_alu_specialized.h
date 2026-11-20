#pragma once
#include "gba_arm_alu.h"
namespace ArmAluSpecialized {
using GbaArmAlu::ror;
template <unsigned Kind,bool Flags,int Shape=-1,typename Registers> __attribute__((always_inline)) inline void executeOp(Registers &regs,uint32_t &cpsr,uint32_t op) {
    constexpr unsigned kind=Kind;
    constexpr bool flags=Flags;
    constexpr bool arithmetic=(kind>=2 && kind<=7) || kind==10 || kind==11;
    const uint32_t oldCarry=(cpsr>>29)&1;
    uint32_t carry=oldCarry,b;
    if (Shape==4 || (Shape<0 && (op&0x02000000u))) {
        const unsigned shift=(op>>7)&30;
        b=ror(op&255u,shift);
        if (shift) carry=b>>31;
    } else {
        const uint32_t v=*regs[op&15];
        const unsigned shift=(op>>7)&31;
        switch(Shape<0?((op>>5)&3):unsigned(Shape)) {
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
template<class Registers> __attribute__((always_inline)) inline void execute(Registers &regs,uint32_t &cpsr,uint32_t op){
 switch((op>>20)&31){
 case 0:executeOp<0,false>(regs,cpsr,op);break;
 case 1:executeOp<0,true>(regs,cpsr,op);break;
 case 2:executeOp<1,false>(regs,cpsr,op);break;
 case 3:executeOp<1,true>(regs,cpsr,op);break;
 case 4:executeOp<2,false>(regs,cpsr,op);break;
 case 5:executeOp<2,true>(regs,cpsr,op);break;
 case 6:executeOp<3,false>(regs,cpsr,op);break;
 case 7:executeOp<3,true>(regs,cpsr,op);break;
 case 8:executeOp<4,false>(regs,cpsr,op);break;
 case 9:executeOp<4,true>(regs,cpsr,op);break;
 case 10:executeOp<5,false>(regs,cpsr,op);break;
 case 11:executeOp<5,true>(regs,cpsr,op);break;
 case 12:executeOp<6,false>(regs,cpsr,op);break;
 case 13:executeOp<6,true>(regs,cpsr,op);break;
 case 14:executeOp<7,false>(regs,cpsr,op);break;
 case 15:executeOp<7,true>(regs,cpsr,op);break;
 case 17:executeOp<8,true>(regs,cpsr,op);break;
 case 19:executeOp<9,true>(regs,cpsr,op);break;
 case 21:executeOp<10,true>(regs,cpsr,op);break;
 case 23:executeOp<11,true>(regs,cpsr,op);break;
 case 24:executeOp<12,false>(regs,cpsr,op);break;
 case 25:executeOp<12,true>(regs,cpsr,op);break;
 case 26:executeOp<13,false>(regs,cpsr,op);break;
 case 27:executeOp<13,true>(regs,cpsr,op);break;
 case 28:executeOp<14,false>(regs,cpsr,op);break;
 case 29:executeOp<14,true>(regs,cpsr,op);break;
 case 30:executeOp<15,false>(regs,cpsr,op);break;
 case 31:executeOp<15,true>(regs,cpsr,op);break;
 default:__builtin_unreachable();
 }
}
}
