// SPDX-License-Identifier: GPL-3.0-or-later
#include "../NooDS-Wii/jit.h"
#include "../NooDS-Wii/jit_compiler.h"
#include "../NooDS-Wii/jit_cache.h"
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
extern "C" int jitReference(JitContext *,uint32_t,bool,bool);
extern "C" int jitAbiProbe(JitEntry,JitContext *);
static uint32_t rng=0x517493A7;
static uint32_t randomWord() { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static const uint32_t edge[]={0,1,2,31,32,33,63,64,127,128,255,256,257,0x7FFFFFFF,0x80000000,0xFFFFFFFF,
    0xFFFFFF00,0xFFFFFEFF,0xFFFF0000,0xFFFEFFFF,0xFF000000,0xFEFFFFFF,
    0xFFFF,0x10000,0xFFFFFF,0x1000000,0x7FFFFFFE,0x80000001,0x12345678,0x87654321,0xAAAAAAAA,0x55555555};
static const unsigned edgeCount=sizeof(edge)/sizeof(edge[0]);
static unsigned long cases=0,armAccepted=0,thumbAccepted=0;
static void run(JitEntry entry,uint32_t opcode,bool thumb,unsigned samples,bool arm7,uint32_t pc) {
    for (unsigned sample=0;sample<samples;sample++) {
        JitContext native{};
        for (unsigned r=0;r<16;r++) native.regs[r]=sample<edgeCount?edge[(sample+r)%edgeCount]:randomWord();
        native.regs[15]=pc+(thumb?4:8);
        // Exercise every old-NZCV combination and deliberately varied low CPSR bits.
        native.cpsr=(sample<edgeCount ? (sample&15)<<28 : randomWord()&0xF0000000)|0x080000DF|(thumb?32:0);
        JitContext reference=native;
        if (jitReference(&reference,opcode,thumb,arm7)<0) {
            std::printf("BAD_ALLOWLIST opcode=%08x thumb=%u\n",opcode,thumb); std::exit(1);
        }
        if (jitAbiProbe(entry,&native)) { std::puts("ABI_FAILURE"); std::exit(1); }
        bool exitEqual=native.exitKind==reference.exitKind && (!native.exitKind || native.resumePC==reference.resumePC);
        if (native.exitKind==1) native.regs[15]=(native.resumePC&~((native.cpsr&32)?1u:3u))+((native.cpsr&32)?2:4);
        bool equal=exitEqual && native.cpsr==reference.cpsr && native.cycles==reference.cycles;
        for (unsigned r=0;r<16;r++) equal &= native.regs[r]==reference.regs[r];
        if (!equal) {
            std::printf("NATIVE_FAIL op=%08x thumb=%u arm7=%u sample=%u cpsr=%08x/%08x cycles=%u/%u\n",
                opcode,thumb,arm7,sample,native.cpsr,reference.cpsr,native.cycles,reference.cycles);
            std::printf("exit=%u/%u target=%08x/%08x\n",native.exitKind,reference.exitKind,native.resumePC,reference.resumePC);
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
    auto check=[&](uint32_t op,bool thumb,unsigned samples,uint32_t pc=0x02000000,bool required=false) {
        for (unsigned arm7=0;arm7<2;arm7++) {
        JitPpc::Writer w(code,1024);
        bool accepted=thumb?Jit::compileThumb(w,op,arm7,pc):Jit::compileArm(w,op,arm7,pc);
        if(!accepted) { if(required) { std::printf("MISSING_NATIVE_COVERAGE op=%08x thumb=%u arm7=%u\n",op,thumb,arm7); std::exit(1); } if(w.size()) { std::puts("REJECT_EMITTED_CODE"); std::exit(1); } continue; }
        if(!w.good()) std::abort();
        if (!arm7) { if(thumb) thumbAccepted++; else armAccepted++; }
        __builtin___clear_cache(static_cast<char*>(memory),static_cast<char*>(memory)+w.size()*4);
        run(reinterpret_cast<JitEntry>(memory),op,thumb,samples,arm7,pc);
        }
    };
    // Every THUMB encoding, including rejection of all unknown/branch/memory ops.
    for(unsigned op=0;op<65536;op++) check(op,true,20);
    unsigned long uniqueThumb=thumbAccepted;
    if(uniqueThumb<32419) { std::puts("THUMB_COVERAGE_REGRESSION"); return 1; }
    // Every ARM dispatch slot with independent register/immediate low fields.
    for(unsigned slot=0;slot<4096;slot++) for(unsigned sample=0;sample<16;sample++) {
        uint32_t op=0xE0000000u|((slot&0xFF0)<<16)|((slot&15)<<4);
        op|=randomWord()&0x000FFF0Fu;
        check(op,false,20);
    }
    // Broader randomized ARM fields, conditions and shift amounts.
    for(unsigned i=0;i<60000;i++) check(randomWord(),false,8);
    // Multiply aliases + every signed timing threshold, including unsigned-long
    // timing's source quirk. Also force carry between accumulated product halves.
    for(unsigned kind=0;kind<12;kind++) for(unsigned alias=0;alias<8;alias++) {
        unsigned hi=alias&1?2:0, lo=alias&2?hi:1, rm=alias&4?hi:2, rs=alias&2?lo:3;
        uint32_t op=0xE0000090u|((kind<4?kind:kind+4)<<20)|(hi<<16)|(lo<<12)|(rs<<8)|rm;
        check(op,false,64,0x02000000,true);
    }
    const uint32_t pcs[]={0,0x02000000,0x82000000,0xFFFF0000,0xFFFFFFF8,0xFFFFFFFC};
    const uint32_t branches[]={0xEA000000,0xEAFFFFFF,0xEAFFFFFE,0xEB7FFFFF,0xEA800000,
        0xE12FFF10,0xE12FFF1F,0xE12FFF3E,0xE12FFF3F,0xFA000000,0xFBFFFFFF};
    for(uint32_t pc:pcs) {
        for(uint32_t op:branches) check(op,false,32,pc,true);
        for(uint32_t op:{0x4348u,0x4700u,0x4778u,0x47F0u,0xE000u,0xE7FFu,0xF000u,0xF7FFu,0xF800u,0xFFFFu,0xE800u,0xEFFFu}) { check(op,true,32,pc,true); check(op,true,32,pc+2,true); }
    }
    // Content-addressed cache: exact opcode changes, CPU/ISA keys, negatives,
    // collisions and arena wrap are exercised with a tiny arena in this test.
    for(unsigned i=0;i<16000;i++) {
        uint32_t op=0xE2800000u|(randomWord()&0x000EEFFFu);
        JitEntry entry=Jit::lookup(op,false,i&1,0x02000000);
        if(!entry) continue;
        if(Jit::lookup(op,false,i&1,0x02000000)!=entry) std::abort();
        run(entry,op,false,1,i&1,0x02000000);
        if(Jit::lookup(0xEF000001u,false,false,0)) std::abort(); // SWI must fall back.
        if(Jit::lookup(0x10000u,true,false,0)) std::abort();
    }
    // Same opcode and colliding high-PC bits must NEVER reuse a wrong target.
    for(unsigned i=0;i<100;i++) for(uint32_t pc:pcs) for(unsigned arm7=0;arm7<2;arm7++) {
        auto entry=Jit::lookup(0xEAFFFFFE,false,arm7,pc);
        if(!entry) std::abort();
        run(entry,0xEAFFFFFE,false,1,arm7,pc);
    }
    auto stats=Jit::cacheStats();
    if(!stats.hits || !stats.flushes || !stats.rejected) std::abort();
    std::printf("NATIVE_ORACLE_OK: %lu states; %lu unique THUMB encodings; %lu ARM candidates accepted\n",cases,uniqueThumb,armAccepted);
    std::printf("CACHE_OK: hits=%u misses=%u compiled=%u flushes=%u rejected=%u\n",stats.hits,stats.misses,stats.compiles,stats.flushes,stats.rejected);
    std::puts("ABI_OK: r14-r31, r2/r13 and full CR checked on every invocation");
    munmap(memory,4096);
}
