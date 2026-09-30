// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "jit.h"
namespace Jit {
// Stage-2 content-addressed cache: exact already-fetched opcode + ISA + CPU.
// Safe only because generated leaves have no PC, memory/map pointers, branches,
// or embedded guest values. This is NOT an address-keyed block cache.
// Returned pointers are ephemeral: execute before any subsequent lookup/clear.
JitEntry lookup(uint32_t opcode, bool thumb, bool arm7);
void clearCache(); // Call only on the emulation thread, outside native execution.
struct CacheStats { uint32_t hits, misses, compiles, flushes, rejected; };
CacheStats cacheStats();
}
