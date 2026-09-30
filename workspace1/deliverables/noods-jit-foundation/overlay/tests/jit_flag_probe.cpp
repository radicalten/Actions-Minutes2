// SPDX-License-Identifier: GPL-3.0-or-later
// Executes actual emitter output in big-endian PPC Linux under qemu-ppc.
// This is NOT an emulator lockstep test or a Wii cache-coherency test.
#include "../NooDS-Wii/jit_ppc_emitter.h"
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
using namespace JitPpc;
#define BIT(n) (uint32_t(1) << (n))
struct Result { uint32_t value, flags; };
using Function = void (*)(uint32_t, uint32_t, uint32_t, Result *);

// Flag expressions are copied from the pinned interpreter_alu.cpp handlers.
// Only surrounding pointer access/PC/mode handling is removed for this probe.
// In particular rscs intentionally preserves old C/V, unlike ARM hardware.
static Result reference(unsigned kind,uint32_t op1,uint32_t op2,uint32_t cpsr) {
    uint32_t v; uint32_t *op0=&v;
    switch(kind) {
    case 0:
        *op0=op1+op2;
        cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
            ((op1 > *op0) << 29) | ((~(op2 ^ op1) & (*op0 ^ op2) & BIT(31)) >> 3);
        break;
    case 1:
        *op0 = op1 + op2 + ((cpsr & BIT(29)) >> 29);
        cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0 ||
            (op2 == -1 && (cpsr & BIT(29)))) << 29) | ((~(op2 ^ op1) & (*op0 ^ op2) & BIT(31)) >> 3);
        break;
    case 2:
        *op0=op1-op2;
        cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
            ((op1 >= *op0) << 29) | (((op2 ^ op1) & ~(*op0 ^ op2) & BIT(31)) >> 3);
        break;
    case 3:
        *op0 = op1 - op2 - 1 + ((cpsr & BIT(29)) >> 29);
        cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0 &&
            (op2 != -1 || (cpsr & BIT(29)))) << 29) | (((op2 ^ op1) & ~(*op0 ^ op2) & BIT(31)) >> 3);
        break;
    case 4:
        *op0=op2-op1;
        cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
            ((op2 >= *op0) << 29) | (((op1 ^ op2) & ~(*op0 ^ op1) & BIT(31)) >> 3);
        break;
    default:
        *op0 = op2 - op1 - 1 + ((cpsr & BIT(29)) >> 29);
        cpsr = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op2 >= *op0 &&
            (op1 != -1 || (cpsr & BIT(29)))) << 29) | (((op1 ^ op2) & ~(*op0 ^ op1) & BIT(31)) >> 3);
        break;
    }
    return {v,cpsr};
}
static Function generate(uint32_t *code,unsigned kind) {
    Writer w(code,128);
    // SYSV: arguments r3=a, r4=b, r5=old CPSR, r6=result pointer.
    // Only volatile r7-r10, CR0, XER are changed. No calls or stack frame.
    w.emit(rlwinm(8,5,0,2,2)); // XER.CA = CPSR.C. Clear sticky SO and old OV.
    w.emit(mtspr(1,8));
    switch(kind) {
    case 0: w.emit(addc(7,3,4,true)); break;
    case 1: w.emit(adde(7,3,4,true)); break;
    case 2: w.emit(subfc(7,4,3,true)); break; // r3-r4
    case 3: w.emit(subfe(7,4,3,true)); break;
    case 4: w.emit(subfc(7,3,4,true)); break;
    case 5: w.emit(subfe(7,3,4,true)); break;
    }
    w.emit(mfspr(8,1));
    w.emit(bitOr(7,7,7,true)); w.emit(mfcr(10));
    w.emit(rlwinm(9,5,0,kind==5 ? 2 : 4,31)); // Preserve interpreter's RSCS quirk.
    w.emit(rlwimi(9,10,0,0,0)); // N = CR0.LT
    w.emit(rlwimi(9,10,1,1,1)); // Z = CR0.EQ
    if (kind==5) {
        // The interpreter ORs computed C/V into the old C/V bits.
        w.emit(rlwinm(10,8,0,2,2)); w.emit(bitOr(9,9,10));
        w.emit(rlwinm(10,8,30,3,3)); w.emit(bitOr(9,9,10));
    } else {
        w.emit(rlwimi(9,8,0,2,2)); w.emit(rlwimi(9,8,30,3,3));
    }
    w.emit(stw(7,6,0)); w.emit(stw(9,6,4)); w.emit(bclr());
    if (!w.good()) std::abort();
    __builtin___clear_cache(reinterpret_cast<char*>(code),reinterpret_cast<char*>(code+w.size()));
    return reinterpret_cast<Function>(code);
}
static uint32_t randomWord() {
    static uint32_t x=0x715391AB;
    x^=x<<13; x^=x>>17; x^=x<<5; return x;
}
static void check(Function f,unsigned kind,uint32_t a,uint32_t b,uint32_t flags) {
    Result actual{}, expected=reference(kind,a,b,flags); f(a,b,flags,&actual);
    if (actual.value!=expected.value || actual.flags!=expected.flags) {
        std::printf("FAIL kind=%u a=%08x b=%08x old=%08x actual=%08x/%08x expected=%08x/%08x\n",
            kind,a,b,flags,actual.value,actual.flags,expected.value,expected.flags); std::exit(1);
    }
}
int main() {
    void *arena=mmap(nullptr,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (arena==MAP_FAILED) { std::perror("mmap"); return 1; }
    const uint32_t values[]={0,1,2,0x7F,0x80,0xFF,0x100,0xFFFF,0x10000,0x7FFFFFFE,0x7FFFFFFF,0x80000000,0x80000001,0xFFFFFFFE,0xFFFFFFFF,0xA5A5A5A5};
    unsigned count=0;
    for (unsigned kind=0;kind<6;kind++) {
        Function f=generate(static_cast<uint32_t*>(arena),kind);
        for (uint32_t a:values) for (uint32_t b:values) for (unsigned nzcv=0;nzcv<16;nzcv++) {
            check(f,kind,a,b,(nzcv<<28)|0x080000DF); count++;
        }
        for (unsigned i=0;i<50000;i++) {
            uint32_t a=randomWord(), b=randomWord(), flags=randomWord();
            check(f,kind,a,b,flags); count++;
        }
    }
    munmap(arena,4096);
    std::printf("FLAG_PROBE_OK: %u emitted-code cases against interpreter expressions\n",count);
}
