// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
class Interpreter;

// Handler-boundary snapshot. The interpreter, not native code, owns fetch/PC.
// Only pure, non-PC ALU operations are accepted by the stage-2 compiler.
#define JIT_CTX_REGS 0
#define JIT_CTX_CPSR 64
#define JIT_CTX_CYCLES 68
#define JIT_CTX_INTERPRETER 72
struct alignas(32) JitContext {
    uint32_t regs[16];
    uint32_t cpsr;
    uint32_t cycles;
    Interpreter *interpreter;
};
static_assert(offsetof(JitContext, regs)==JIT_CTX_REGS, "register offset");
static_assert(offsetof(JitContext, cpsr)==JIT_CTX_CPSR, "CPSR offset");
static_assert(offsetof(JitContext, cycles)==JIT_CTX_CYCLES, "cycle offset");
static_assert(offsetof(JitContext, interpreter)==JIT_CTX_INTERPRETER, "interpreter offset");
static_assert(alignof(JitContext)==32, "context alignment");
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(JitContext)==96, "32-bit context size");
#endif
using JitEntry = void (*)();
extern "C" void jitEnter(JitEntry entry, JitContext *context);

namespace Jit {
// Called after pipeline advance, and after the ARM condition check. On false,
// state is untouched and the caller executes the existing handler exactly once.
bool execute(Interpreter &cpu, uint32_t opcode, bool thumb, int &cycles);
// Log first mismatch, then permanently disable this session's native path.
void logMismatch(uint32_t pc, uint32_t opcode, bool arm7, bool thumb,
                 const JitContext &native, const JitContext &reference);
}
