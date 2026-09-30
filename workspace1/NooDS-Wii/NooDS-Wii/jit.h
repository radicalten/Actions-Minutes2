// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
class Interpreter;

// Handler-boundary snapshot. The interpreter, not native code, owns fetch/PC.
// Native leaves compute register/flag results and optional branch exit requests.
// Pipeline refill remains in the interpreter; native code performs no guest reads.
#define JIT_CTX_REGS 0
#define JIT_CTX_CPSR 64
#define JIT_CTX_CYCLES 68
#define JIT_CTX_INTERPRETER 72
#define JIT_CTX_RESUME 76
#define JIT_CTX_EXIT 80
struct alignas(32) JitContext {
    uint32_t regs[16];
    uint32_t cpsr;
    uint32_t cycles;
    Interpreter *interpreter;
    uint32_t resumePC; // Raw target, before alignment/refill.
    uint32_t exitKind; // 0: sequential, 1: request the original flushPipeline.
};
static_assert(offsetof(JitContext, regs)==JIT_CTX_REGS, "register offset");
static_assert(offsetof(JitContext, cpsr)==JIT_CTX_CPSR, "CPSR offset");
static_assert(offsetof(JitContext, cycles)==JIT_CTX_CYCLES, "cycle offset");
static_assert(offsetof(JitContext, interpreter)==JIT_CTX_INTERPRETER, "interpreter offset");
static_assert(alignof(JitContext)==32, "context alignment");
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(JitContext,resumePC)==JIT_CTX_RESUME, "resume offset");
static_assert(offsetof(JitContext,exitKind)==JIT_CTX_EXIT, "exit offset");
static_assert(sizeof(JitContext)==96, "32-bit context size");
#endif
using JitEntry = void (*)();
extern "C" void jitEnter(JitEntry entry, JitContext *context);

namespace Jit {
// Called after pipeline advance, and after ARM condition routing. On false,
// state is untouched and the caller executes the existing handler exactly once.
bool execute(Interpreter &cpu, uint32_t opcode, bool thumb, int &cycles);
// Log first mismatch, then permanently disable this session's native path.
void logMismatch(uint32_t pc, uint32_t opcode, bool arm7, bool thumb,
                 const JitContext &native, const JitContext &reference);
}
