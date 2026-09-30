// SPDX-License-Identifier: GPL-3.0-or-later
// Appended by jit_make_pipeline_test.py to the extracted execution code.
static void fail(const char *text,uint32_t op) {
    std::printf("PIPELINE_FAIL %s opcode=%08x\n",text,op); std::exit(1);
}
static void prepare(Interpreter &cpu,Core &core,uint32_t op,bool thumb,unsigned flags,bool unmapped,bool banked) {
    unsigned width=thumb?2:4, address=4096-2*width;
    if(banked) for(unsigned i=8;i<15;i++) cpu.registers[i]=&cpu.registersFiq[i-8];
    for(unsigned i=0;i<15;i++) *cpu.registers[i]=0x78123456u+i*0x234567u;
    cpu.cpsr=(flags<<28)|0xDF|(thumb?32:0);
    cpu.cycles=0x123456; cpu.halted=0; cpu.dsiCycle=true;
    *cpu.registers[15]=address+width;
    cpu.pipeline[0]=op;
    cpu.pipeline[1]=thumb ? 0x2112 : 0xE3A01012; // Prefetched MOV r1,#18.
    // Memory deliberately differs from both prefetched instructions.
    std::memset(core.memory.bytes,0xA5,sizeof(core.memory.bytes));
    cpu.pcData=core.memory.bytes+address+width;
    if(unmapped) {
        core.memory.readMap9A[1]=nullptr; core.memory.readMap7[1]=nullptr;
    }
}
int main() {
    const uint32_t armOps[]={0xE3A02037,0xE2911001,0xE0812413,0xE1A002E3,0xE1A00263,
        0xE2F11001,0xE0202391,0xE08000AF,0xE12FFF1E,0xFF000000,
        0xEAFFFFFF,0xEAFFFFFE,0xEB000001,0xE12FFF3E,0xE12FFF1F,
        0xEA800000,0xEB7FFFFF,0xE0F12392,0xE0910392};
    const uint32_t thumbOps[]={0x2000,0x3101,0x1C89,0x4088,0x41C8,0x4248,0xB081,0x4508,
        0x4348,0xDE00,0x4770,0x47F0,0x4778,0xD000,0xD1FF,0xD800,0xD900,
        0xDC00,0xDDFF,0xE000,0xE7FF,0xF000,0xF7FF,0xF800,0xE800,0xEFFF};
    unsigned checks=0;
    for(unsigned thumb=0;thumb<2;thumb++) for(unsigned arm7=0;arm7<2;arm7++)
    for(unsigned mapping=0;mapping<2;mapping++) for(unsigned banked=0;banked<2;banked++)
    for(unsigned nzcv=0;nzcv<16;nzcv++) for(unsigned cond=0;cond<(thumb?1:16);cond++) {
        unsigned count=thumb?sizeof(thumbOps)/4:sizeof(armOps)/4;
        for(unsigned i=0;i<count;i++) {
            uint32_t opcode=thumb?thumbOps[i]:((armOps[i]&0x0FFFFFFF)|(cond<<28));
            Core ca,cb;
            Interpreter reference(&ca,arm7),native(&cb,arm7);
            prepare(reference,ca,opcode,thumb,nzcv,mapping,banked);
            prepare(native,cb,opcode,thumb,nzcv,mapping,banked);
            for(unsigned step=0;step<2;step++) {
                unsigned f=fallbackCalls,h=reservedCalls;
                Settings::jitEnabled=0; int expected=reference.runOpcode();
                unsigned expectedFallback=fallbackCalls-f,expectedReserved=reservedCalls-h;
                f=fallbackCalls;h=reservedCalls;
                Settings::jitEnabled=1; int actual=native.runOpcode();
                if(actual!=expected || native.cpsr!=reference.cpsr) fail("cycles/flags",opcode);
                for(unsigned r=0;r<16;r++) if(*native.registers[r]!=*reference.registers[r]) fail("register",opcode);
                if(std::memcmp(native.pipeline,reference.pipeline,sizeof(native.pipeline))) fail("prefetch",opcode);
                if(ca.memory.reads!=cb.memory.reads || ca.memory.readTrace!=cb.memory.readTrace) fail("read side effect",opcode);
                if((native.pcData==nullptr)!=(reference.pcData==nullptr)) fail("pointer null",opcode);
                if(native.pcData && native.pcData-cb.memory.bytes!=reference.pcData-ca.memory.bytes) fail("pointer offset",opcode);
                if(native.cycles!=reference.cycles || native.halted!=reference.halted || native.dsiCycle!=reference.dsiCycle) fail("scheduler fields",opcode);
                if(fallbackCalls-f!=expectedFallback || reservedCalls-h!=expectedReserved) fail("fallback count",opcode);
#if defined(JIT_DIFFTEST) && !defined(NOODS_NO_JIT)
                if(native.jitFlushCount!=reference.jitFlushCount) fail("refill count",opcode);
#endif
                checks++;
            }
        }
    }
    // Execute real BL/BLX prefix/suffix pairs at a page boundary. The suffix
    // remains prefetched even though the backing memory contains another word.
    for(unsigned arm7=0;arm7<2;arm7++) for(unsigned exchange=0;exchange<2;exchange++)
    for(unsigned negative=0;negative<2;negative++) for(unsigned halfstep=0;halfstep<2;halfstep++) {
        Core ca,cb; Interpreter a(&ca,arm7),b(&cb,arm7);
        uint32_t prefix=negative?0xF7FF:0xF000, suffix=(exchange?0xE800:0xF800)|0x3FF;
        prepare(a,ca,prefix,true,7,false,true); prepare(b,cb,prefix,true,7,false,true);
        if(halfstep) { *a.registers[15]+=2; *b.registers[15]+=2; a.pcData+=2; b.pcData+=2; }
        a.pipeline[1]=b.pipeline[1]=suffix;
        for(unsigned step=0;step<3;step++) {
            Settings::jitEnabled=0; int ac=a.runOpcode(); Settings::jitEnabled=1; int bc=b.runOpcode();
            if(ac!=bc || a.cpsr!=b.cpsr || ca.memory.readTrace!=cb.memory.readTrace ||
                ca.memory.reads!=cb.memory.reads || std::memcmp(a.pipeline,b.pipeline,sizeof(a.pipeline))) fail("BL pair state",prefix);
            for(unsigned r=0;r<16;r++) if(*a.registers[r]!=*b.registers[r]) fail("BL pair register",prefix);
            checks++;
        }
    }
#ifndef NOODS_NO_JIT
    auto stats=Jit::cacheStats();
#ifdef JIT_TEST_ALLOC_FAIL
    if(stats.compiles || !stats.misses) fail("OOM cache state",0);
    std::puts("OOM_FALLBACK_OK: all native allocations denied; reference execution preserved");
#else
    if(!stats.compiles || !stats.hits || !stats.rejected) fail("no cache activity",0);
#endif
#ifdef JIT_DIFFTEST
    // Intentionally corrupt a REFERENCE dispatch slot to test first-mismatch
    // reporting, reference-state retention and permanent native disablement.
    Core core; Interpreter cpu(&core,false);
    prepare(cpu,core,0xE3A02037,false,0,false,false);
    *cpu.registers[15]+=4; // Enter at the post-fetch handler boundary.
#ifdef JIT_TEST_REFILL_FAULT
    // B to A+4 has the SAME post-handler r15 as sequential execution. Replace
    // it with an instrumented no-op returning 3, so ONLY the refill count differs.
    Interpreter::armInstrs[0xAFF]=&Interpreter::unkArm;
    uint32_t savedR0=*cpu.registers[0],savedPC=*cpu.registers[15];
    int cycles=-1;
    if(!Jit::execute(cpu,0xEAFFFFFF,false,cycles) || cycles!=3) fail("refill fault result",0);
    if(*cpu.registers[0]!=savedR0 || *cpu.registers[15]!=savedPC) fail("refill reference state",0);
#else
    Interpreter::armInstrs[0x3A3]=&Interpreter::unkArm; // MOV immediate index for 0x37
    uint32_t savedR0=*cpu.registers[0],savedR2=*cpu.registers[2];
    int cycles=-1;
    if(!Jit::execute(cpu,0xE3A02037,false,cycles) || cycles!=3) fail("fault result",0);
    if(*cpu.registers[0]!=(savedR0^0xE3A02037u) || *cpu.registers[2]!=savedR2) fail("reference state not retained",0);
#endif
    int unused=99;
    if(Jit::execute(cpu,0xE3A01012,false,unused) || unused!=99) fail("fault not latched",0);
    FILE *log=std::fopen("sd:/noods/jit_log.txt","r");
    if(!log) fail("no mismatch log",0);
    char text[2048]={};std::fread(text,1,sizeof(text)-1,log);std::fclose(log);
#ifdef JIT_TEST_REFILL_FAULT
    if(!std::strstr(text,"opcode=eaffffff") || !std::strstr(text,"exit native=1 reference=0")) fail("refill-only mismatch not detected",0);
    std::puts("REFILL_FAULT_OK: equal registers/flags/cycles, missing refill detected and native disabled");
#else
    if(!std::strstr(text,"opcode=e3a02037")) fail("mismatch log content",0);
    std::puts("DIFFTEST_FAULT_OK: injected mismatch logged; reference retained; native disabled");
#endif
#endif
#endif
    std::printf("PIPELINE_OK: %u step comparisons; runtime toggle, condition checks, banked pointers, page crossings and prefetched stale bytes\n",checks);
}
