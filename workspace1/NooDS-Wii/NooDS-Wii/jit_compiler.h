// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "jit_ppc_emitter.h"
namespace Jit {
using JitPpc::Writer;
// pc is the executing instruction address, NOT the handler-time biased r15.
// Exact opcode + full PC + CPU + ISA identify each compiled single-op leaf.
bool compileArm(Writer &w, uint32_t opcode, bool arm7, uint32_t pc);
bool compileThumb(Writer &w, uint16_t opcode, bool arm7, uint32_t pc);
bool compileMultiply(Writer &w, uint32_t opcode, unsigned kind, bool arm7);
bool compileThumbMultiply(Writer &w, unsigned rd, unsigned rs, bool arm7);
bool compileArmBranch(Writer &w, uint32_t opcode, unsigned kind, bool arm7, uint32_t pc);
bool compileThumbBranch(Writer &w, uint16_t opcode, bool arm7, uint32_t pc);
unsigned armClass(uint32_t opcode);
void constant(Writer &w, unsigned dest, uint32_t value);
void flagsNZ(Writer &w, unsigned value);
void shiftImmediate(Writer &w, unsigned type, unsigned src, unsigned amount, bool carry);
void shiftRegister(Writer &w, unsigned type, unsigned src, unsigned count, bool carry);
// ARM operation numbers. Operands live in r5/r4, result in r3. Writes r14+rd
// except test/compare ops 8..11. This function does not handle guest PC.
void dataProcessing(Writer &w, unsigned op, bool flags, unsigned rd);
void finish(Writer &w, unsigned cycles);
}
