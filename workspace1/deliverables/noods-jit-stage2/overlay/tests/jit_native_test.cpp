// SPDX-License-Identifier: GPL-3.0-or-later
#include "../NooDS-Wii/jit.h"
#include "../NooDS-Wii/jit_compiler.h"
#include "../NooDS-Wii/jit_cache.h"
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" int jitReference(JitContext *,uint32_t,bool,bool);
extern "C" int jitAbiProbe(JitEntry,JitContext *);
static uint32_t rng=0x517493A7;
static uint32_t randomWord() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static const uint32_t edge[]={0,1,2,31,32,33,63,64,127,128,255,256,257,0x7FFFFFFF,0x80000000,0xFFFFFFFF};
static unsigned long cases=0,armAccepted=0,thumbAccepted=0;
static void run(JitEntry entry,uint32_t opcode,bool thumb,unsigned samples) {
    for (unsigned sample=0;sample<samples;sample++) for (unsigned arm7=0;arm7<2;arm7++) {
        JitContext native{};
        for (unsigned r=0;r<16;r++) native.regs[r]=sample<16?edge[(sample+r)%16]:randomWord();
        // Exercise every old-NZCV combination and deliberately varied low CPSR bits.
        native.cpsr=(sample<16 ? sample<<28 : randomWord()&0xF0000000)|0x080000DF|(thumb?32:0);
        JitContext reference=native;
        if (jitReference(&reference,opcode,thumb,arm7)<0) {
            std::printf("BAD_ALLOWLIST opcode=%08x thumb=%u\n",opcode,thumb); std::exit(1);
        }
        if (jitAbiProbe(entry,&native)) { std::puts("ABI_FAILURE"); std::exit(1); }
        bool equal=native.cpsr==reference.cpsr && native.cycles==reference.cycles;
        for (unsigned r=0;r<16;r++) equal &= native.regs[r]==reference.regs[r];
        if (!equal) {
            std::printf("NATIVE_FAIL op=%08x thumb=%u arm7=%u sample=%u cpsr=%08x/%08x cycles=%u/%u\n",
                opcode,thumb,arm7,sample,native.cpsr,reference.cpsr,native.cycles,reference.cycles);
            for(unsigned r=0;r<16;r++) std::printf("r%u=%08x/%08x\n",r,native.regs[r],reference.regs[r]);
            std::exit(1);
        }
        cases++;
    }
}
int main() {
    void *memory=mmap(nullptr,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(memory==MAP_FAILED) { std::perror("mmap"); return 1; }
    uint32_t *code=static_cast<uint32_t*>(memory);
    auto check=[&](uint32_t op,bool thumb,unsigned samples) {
        JitPpc::Writer w(code,1024);
        bool accepted=thumb?Jit::compileThumb(w,op):Jit::compileArm(w,op);
        if(!accepted) { if(w.size()) { std::puts("REJECT_EMITTED_CODE"); std::exit(1); } return; }
        if(!w.good()) std::abort();
        if(thumb) thumbAccepted++; else armAccepted++;
        __builtin___clear_cache(static_cast<char*>(memory),static_cast<char*>(memory)+w.size()*4);
        run(reinterpret_cast<JitEntry>(memory),op,thumb,samples);
    };
    // Every THUMB encoding, including rejection of all unknown/branch/memory ops.
    for(unsigned op=0;op<65536;op++) check(op,true,20);
    // Every ARM dispatch slot with independent register/immediate low fields.
    for(unsigned slot=0;slot<4096;slot++) for(unsigned sample=0;sample<16;sample++) {
        uint32_t op=0xE0000000u|((slot&0xFF0)<<16)|((slot&15)<<4);
        op|=randomWord()&0x000FFF0Fu;
        check(op,false,20);
    }
    // Broader randomized ARM fields, conditions and shift amounts.
    for(unsigned i=0;i<60000;i++) check(randomWord(),false,8);
    // Content-addressed cache: exact opcode changes, CPU/ISA keys, negatives,
    // collisions and arena wrap are exercised with a tiny arena in this test.
    for(unsigned i=0;i<16000;i++) {
        uint32_t op=0xE2800000u|(randomWord()&0x000EEFFFu);
        JitEntry entry=Jit::lookup(op,false,i&1);
        if(!entry) continue;
        if(Jit::lookup(op,false,i&1)!=entry) std::abort();
        run(entry,op,false,1);
        if(Jit::lookup(0xE12FFF1Eu,false,false)) std::abort(); // BX must fall back.
        if(Jit::lookup(0x10000u,true,false)) std::abort();
    }
    auto stats=Jit::cacheStats();
    if(!stats.hits || !stats.flushes || !stats.rejected) std::abort();
    std::printf("NATIVE_ORACLE_OK: %lu states; %lu THUMB encodings; %lu ARM candidates accepted\n",cases,thumbAccepted,armAccepted);
    std::printf("CACHE_OK: hits=%u misses=%u compiled=%u flushes=%u rejected=%u\n",stats.hits,stats.misses,stats.compiles,stats.flushes,stats.rejected);
    std::puts("ABI_OK: r14-r31, r2/r13 and full CR checked on every invocation");
    munmap(memory,4096);
}
