/*
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS-Wii is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS-Wii. If not, see <https://www.gnu.org/licenses/>.
*/

// v1.101: block fast dispatch.
//
// Profiling the CPU-bound workloads showed the generated code itself was a small part of
// the CPU cost; most of it was the C++ path that leads to a block: Interpreter::runOpcode
// (pipeline shift, prefetch, condition lookup) -> ArmPpcJit::executeArm (opcode class
// filters) -> tryExecuteBlock (lookup, budget) -> jit_enter.
//
// fastDispatch() decides, *before* any guest state is touched, whether the instruction at
// the head of the pipeline starts a compiled block that the slow path would run right now.
// It applies exactly the slow path's conditions (MPU gate, ARM condition, same-page
// prefetch, block table hit incl. opcode0/opcode1/pool epoch/page epoch, event budget,
// loop iteration budget). If they all hold it advances R15/pcData by one instruction (the
// only part of the pipeline shift that compiled code depends on - every block exit rewrites
// both pipeline slots) and enters the block. Otherwise it returns 0 with nothing modified
// and the caller runs the unchanged slow path, so block selection, cycle counts and
// ARM9/ARM7 interleaving are identical to v1.86 by construction.

#pragma once

#include "arm_jit.h"
#include "mpu_pure.h"
#if NOODS_MPU_PURE_JIT
extern uint32_t g_mpuPureJitStats[4];
#endif
#include "core.h"

#if NOODS_JIT && NOODS_JIT_BLOCKS

// v1.117: ceil(2^32 / d) for the loop iteration budget (a loop costs at least 3 cycles, so
// d is never below 3; entries 0 and 1 are unused).
struct LoopRecipTable {
    uint32_t v[256];
    constexpr LoopRecipTable() : v() {
        for (uint32_t d = 2; d < 256; d++) v[d] = (uint32_t)(((1ull << 32) + d - 1) / d);
    }
};
inline constexpr LoopRecipTable g_loopRecip;

template <typename RemFn>
FORCE_INLINE int ArmPpcJit::fastDispatch(Interpreter *cpu, Core *core, const bool arm7, const uint32_t shift, RemFn &&remFn) {
    const uint32_t cpsr = cpu->cpsr;
    const uint32_t r15 = cpu->registersUsr[15];
    uint8_t *const pd = cpu->pcData;
    const uint32_t op0 = cpu->pipeline[0];
    uint32_t instrPC, step, wantFlags;
    size_t bIdx;
    if (cpsr & 0x20u) {
        if (__builtin_expect(((r15 + 2u) & 0xFFEu) == 0u, 0)) return 0;
        instrPC = r15 - 2u;
        step = 2u;
        wantFlags = (currentEpoch << 2) | 3u;
        bIdx = ((instrPC >> 1) ^ (instrPC >> 11)) & (BLOCK_TABLE_SIZE - 1);
    } else {
        if (__builtin_expect(((r15 + 4u) & 0xFFCu) == 0u, 0)) return 0;
        const uint8_t cond = Interpreter::condition[((op0 >> 24) & 0xF0u) | (cpsr >> 28)];
        if (__builtin_expect(cond != 1, 0)) {
#if NOODS_SKIP_FALSE
            // v1.121: condition false. The slow path would shift the pipeline, add 4 to R15
            // and charge one cycle (runOpcode); do exactly that here, behind the same MPU
            // gate. Not in GBA mode, which has its own run loop and timing.
            if (cond == 0 && pd != nullptr && !core->gbaMode) {
                if (!arm7 && __builtin_expect(core->cp15.mpuRestricted, 0)) {
                    const bool privileged = (cpsr & 0x1Fu) != 0x10u;
                    const uint32_t page = (r15 - 4u) >> 12;
                    if (privileged && core->cp15.mpuFastPriv) {
                        if (page != core->cp15.execOkPage) {
                            if (!(core->cp15.getPagePerm(page) & 0x04)) return 0;
                            core->cp15.execOkPage = page;
                        }
                    } else {
                        // False condition: no data access or other guest state
                        // is changed. The slow path checks instruction execute
                        // permission for this exact page and privilege level.
                        // Do not reuse the privileged execOkPage for user mode.
                        if (!(core->cp15.getPagePerm(page) & (privileged ? 0x04u : 0x20u)))
                            return 0;
                    }
                }
                cpu->pipeline[0] = cpu->pipeline[1];
                cpu->registersUsr[15] = r15 + 4u;
                cpu->pcData = pd + 4;
                cpu->pipeline[1] = U8TO32(pd + 4, 0);
                return 1;
            }
#endif
            return 0;
        }
        instrPC = r15 - 4u;
        step = 4u;
        wantFlags = (currentEpoch << 2) | 1u;
        bIdx = ((instrPC >> 2) ^ (instrPC >> 11)) & (BLOCK_TABLE_SIZE - 1);
    }
    if (__builtin_expect(pd == nullptr, 0)) return 0;

    const BlockEntry *ep = &(arm7 ? blockTable[1] : arm9Table)[bIdx];
    if (__builtin_expect(ep->pc != instrPC || ep->opcode0 != op0 || ep->opcode1 != cpu->pipeline[1] ||
                         ep->epochAndFlags != wantFlags, 0)) {
        // v1.113: a block whose slot was taken lives in the alternate slot.
        ep += (ptrdiff_t)(bIdx ^ BLOCK_ALT_XOR) - (ptrdiff_t)bIdx;
        if (ep->pc != instrPC || ep->opcode0 != op0 || ep->opcode1 != cpu->pipeline[1] ||
            ep->epochAndFlags != wantFlags) {
#if NOODS_BUDGET_DISPATCH
            return tryBudgetStep(cpu, op0, (cpsr & 0x20u) != 0, true);
#else
            return 0;
#endif
        }
    }
    const BlockEntry &e = *ep;
    if (__builtin_expect(e.pageEpoch != pageEpoch[arm7 ? 1 : 0][(instrPC >> 12) & (PAGE_EPOCH_SIZE - 1)], 0)) {
#if NOODS_BUDGET_DISPATCH
        return tryBudgetStep(cpu, op0, (cpsr & 0x20u) != 0);
#else
        return 0;
#endif
    }

    if (!arm7 && __builtin_expect(core->cp15.mpuRestricted, 0)) {
#if NOODS_MPU_PURE_JIT
        const bool priv = (cpsr & 0x1Fu) != 0x10u;
        if (!priv || !core->cp15.mpuFastPriv) {
            // No linked blocks and no branches: all instructions stay in the
            // same page and are register-only. Do not reuse execOkPage for a
            // different privilege level.
            const unsigned strictFlags = e.instrCount & 0x0700u;
            const uint8_t pagePerm = core->cp15.getPagePerm(instrPC >> 12);
            const bool soloBranch = strictFlags == 0x0700u && soloBit &&
                                    (pagePerm & (priv ? 0x01u : 0x08u));
            if ((strictFlags != 0x0500u && !soloBranch) || (e.prefixCycles >> 8) != 0u ||
                !(pagePerm & (priv ? 0x04u : 0x20u))) {
                ++g_mpuPureJitStats[3]; return 0;
            }
        } else
#endif
        {
            if ((cpsr & 0x1Fu) == 0x10u) return 0;
            const uint32_t page = instrPC >> 12;
            if (__builtin_expect(page != core->cp15.execOkPage, 0)) {
                if (!core->cp15.mpuFastPriv || !(core->cp15.getPagePerm(page) & 0x04))
                    return 0;
                core->cp15.execOkPage = page;
            }
        }
    }

    // Same budget rule as tryExecuteBlock: the whole block must end before the next event.
    const uint32_t pref = e.prefixCycles;
    if (__builtin_expect(core->globalCycles + ((pref & 0xFFu) << shift) >= core->events.frontCycles, 0)) {
#if NOODS_STEP_BLOCK
        // v1.117: the block does not fit before the next event, so the slow path would look
        // the block up again, reject it for the same reason and interpret this one
        // instruction. Do that directly (Thumb on the ARM9 only).
        // v1.118: also for the ARM7 of a DS game (not in GBA mode, which has its own timing).
        if ((cpsr & 0x20u) && (!arm7 || !core->gbaMode)) {
            cpu->registersUsr[15] = r15 + 2u;
            cpu->pcData = pd + 2;
            noFitNext[arm7 ? 1 : 0] = instrPC + 2u; // v1.123
            noFitEv[arm7 ? 1 : 0] = core->events.frontCycles;
            return stepThumb(cpu, op0);
        }
#endif
#if NOODS_ARM_NOFIT_STEP
        // The ARM condition, mapped fetch and privileged MPU gate above have
        // already succeeded. The slow path would reject this same block at
        // the same event budget and run exactly one interpreter instruction.
        // Preserve that pipeline transition and no-fit chain bookkeeping here.
        // GBA/DSi keep their original timing paths.
        if (!(cpsr & 0x20u) && !core->gbaMode && !core->dsiMode) {
            cpu->registersUsr[15] = r15 + 4u;
            cpu->pcData = pd + 4;
            noFitNext[arm7 ? 1 : 0] = instrPC + 4u;
            noFitEv[arm7 ? 1 : 0] = core->events.frontCycles;
#if NOODS_JIT_POLL_SKIP
            ++pollEpoch;
#endif
            return stepArm(cpu, op0);
        }
#endif
        return 0;
    }

    uint32_t maxIters = 1u;
    const uint32_t loopCyc = pref >> 8;
    if (__builtin_expect(loopCyc != 0u, 0)) {
        const uint32_t kind = (e.instrCount >> 10) & 3u;
        bool useIndep = (kind == 1u);
        if (kind == 3u) {
            uint32_t baseAddr = cpu->registersUsr[(e.instrCount >> 12) & 7u];
            useIndep = ((baseAddr - 0x04000000u) < 0x140u);
        }
        const uint32_t rem = remFn(useIndep);
        if (rem > 1u) {
            // v1.117: (rem - 1) / loopCyc + 1, capped at 4096, without the divide (this runs
            // for every entry into a loop block). Past the cap no quotient is needed; below
            // it rem - 1 < 4096 * 255, where a 32-bit reciprocal gives the exact quotient.
            const uint32_t n = rem - 1u;
            if (__builtin_expect(loopCyc < 2u, 0)) {
                maxIters = n / loopCyc + 1u;
                if (maxIters > 4096u) maxIters = 4096u;
            } else if (n >= loopCyc * 4095u)
                maxIters = 4096u;
            else
                maxIters = (uint32_t)(((uint64_t)n * g_loopRecip.v[loopCyc]) >> 32) + 1u;
        }
    }

    cpu->registersUsr[15] = r15 + step;
    cpu->pcData = pd + step;
#if NOODS_MPU_PURE_JIT
    if (!arm7 && core->cp15.mpuRestricted &&
        (!core->cp15.mpuFastPriv || ((cpsr & 0x1Fu) == 0x10u))) ++g_mpuPureJitStats[1];
#endif
    return jit_enter(e.code, cpu, maxIters);
}

#endif
