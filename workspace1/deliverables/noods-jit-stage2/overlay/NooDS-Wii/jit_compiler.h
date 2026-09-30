// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "jit_ppc_emitter.h"
namespace Jit {
using JitPpc::Writer;
// No guest PC, host-memory pointers, or incoming register/flag values are
// embedded in these leaf functions: their identity is the opcode itself.
bool compileArm(Writer &w, uint32_t opcode);
bool compileThumb(Writer &w, uint16_t opcode);
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
