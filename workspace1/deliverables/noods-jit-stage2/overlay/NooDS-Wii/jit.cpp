// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit.h"
#include "jit_cache.h"
#include "interpreter.h"
#include "settings.h"
namespace Jit {
namespace {
// The emulator thread is the only caller. Neither translation nor execution
// allocates a large stack buffer; the stage is deliberately non-reentrant.
JitContext context;
bool faulted=false;
}
bool execute(Interpreter &cpu,uint32_t opcode,bool thumb,int &cycles) {
    if (!Settings::jitEnabled || faulted) return false;
    JitEntry entry=lookup(opcode,thumb,cpu.arm7);
    if (!entry) return false;
    for (unsigned i=0;i<16;i++) context.regs[i]=*cpu.registers[i];
    context.cpsr=cpu.cpsr; context.cycles=0; context.interpreter=&cpu;
    jitEnter(entry,&context);
#ifdef JIT_DIFFTEST
    // Only pure non-PC ALU leaves can reach here. Execute the original handler
    // ONCE on the unchanged real CPU. No MMIO/memory is duplicated and no
    // pipeline fetch or condition check is repeated.
    JitContext reference;
    reference.cycles=thumb ? (cpu.*Interpreter::thumbInstrs[(opcode>>6)&0x3FF])(opcode)
        : (cpu.*Interpreter::armInstrs[((opcode>>16)&0xFF0)|((opcode>>4)&15)])(opcode);
    reference.cpsr=cpu.cpsr; reference.interpreter=&cpu;
    bool equal=reference.cycles==context.cycles && reference.cpsr==context.cpsr;
    for (unsigned i=0;i<16;i++) {
        reference.regs[i]=*cpu.registers[i];
        equal=equal && reference.regs[i]==context.regs[i];
    }
    if (!equal) {
        faulted=true;
        logMismatch(context.regs[15]-(thumb?4:8),opcode,cpu.arm7,thumb,context,reference);
    }
    cycles=reference.cycles; // Keep the known reference state even on failure.
#else
    for (unsigned i=0;i<15;i++) *cpu.registers[i]=context.regs[i];
    cpu.cpsr=context.cpsr;
    cycles=context.cycles;
#endif
    return true;
}
}
