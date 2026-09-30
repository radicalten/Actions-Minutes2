// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit.h"
#include "jit_cache.h"
#include "interpreter.h"
#include "settings.h"
namespace Jit {
namespace {
// The emulator thread is the only caller. Static snapshot/staging, no chaining.
JitContext context;
bool faulted=false;
}
bool execute(Interpreter &cpu,uint32_t opcode,bool thumb,int &cycles) {
    if (!Settings::jitEnabled || faulted) return false;
    uint32_t pc=*cpu.registers[15]-(thumb?4:8);
    JitEntry entry=lookup(opcode,thumb,cpu.arm7,pc);
    if (!entry) return false;
    for (unsigned i=0;i<16;i++) context.regs[i]=*cpu.registers[i];
    context.cpsr=cpu.cpsr; context.cycles=0; context.interpreter=&cpu;
    context.resumePC=0; context.exitKind=0;
    jitEnter(entry,&context);
#ifdef JIT_DIFFTEST
    // Generated code computes only register/flag/exit results. Execute the
    // original handler ONCE; only it performs a branch's real memory refill.
    uint32_t flushes=cpu.jitFlushCount;
    JitContext reference={};
    reference.cycles=thumb ? (cpu.*Interpreter::thumbInstrs[(opcode>>6)&0x3FF])(opcode)
        : ((opcode>>28)==15 ? cpu.handleReserved(opcode)
        : (cpu.*Interpreter::armInstrs[((opcode>>16)&0xFF0)|((opcode>>4)&15)])(opcode));
    reference.cpsr=cpu.cpsr; reference.interpreter=&cpu;
    reference.exitKind=cpu.jitFlushCount-flushes;
    // Predict only the reference PC normalization; no speculative memory read.
    if (context.exitKind==1) {
        bool nextThumb=context.cpsr&32;
        context.regs[15]=(context.resumePC&~(nextThumb?1u:3u))+(nextThumb?2:4);
    }
    bool equal=reference.cycles==context.cycles && reference.cpsr==context.cpsr &&
        context.exitKind==reference.exitKind && context.exitKind<=1;
    for (unsigned i=0;i<16;i++) {
        reference.regs[i]=*cpu.registers[i];
        equal=equal && reference.regs[i]==context.regs[i];
    }
    if (reference.exitKind) reference.resumePC=reference.regs[15]-((reference.cpsr&32)?2:4);
    if (!equal) {
        faulted=true;
        logMismatch(pc,opcode,cpu.arm7,thumb,context,reference);
    }
    cycles=reference.cycles; // Keep the real reference pipeline/state on failure.
#else
    for (unsigned i=0;i<15;i++) *cpu.registers[i]=context.regs[i];
    cpu.cpsr=context.cpsr;
    if (context.exitKind==1) {
        *cpu.registers[15]=context.resumePC;
        cpu.flushPipeline(); // Original mapping, reads, side effects and bias.
    }
    cycles=context.cycles;
#endif
    return true;
}
}
