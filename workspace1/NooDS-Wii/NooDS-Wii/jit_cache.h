// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "jit.h"
namespace Jit {
// Every lookup validates the exact prefetched opcode, full guest PC, ISA and CPU.
// No native guest-memory reads, mapping pointers or successor bytes are cached.
// This is a guarded single-instruction cache, NOT a multi-instruction block cache.
// Returned pointers are ephemeral: execute before any subsequent lookup/clear.
JitEntry lookup(uint32_t opcode, bool thumb, bool arm7, uint32_t pc);
void clearCache(); // Call only on the emulation thread, outside native execution.
struct CacheStats { uint32_t hits, misses, compiles, flushes, rejected; };
CacheStats cacheStats();
}
