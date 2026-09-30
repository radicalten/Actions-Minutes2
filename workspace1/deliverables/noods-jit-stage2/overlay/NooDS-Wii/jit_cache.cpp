// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_cache.h"
#include "jit_compiler.h"
#include <cstring>
#if defined(JIT_TEST)
#include <sys/mman.h>
#else
#include <ogc/cache.h>
extern void *Noods_MEM2_Alloc(size_t size);
#endif
namespace Jit {
namespace {
#ifndef JIT_CACHE_BYTES
#define JIT_CACHE_BYTES (4u*1024u*1024u)
#endif
constexpr size_t arenaBytes=JIT_CACHE_BYTES;
static_assert(arenaBytes>=1024 && arenaBytes%32==0,"arena capacity/alignment");
struct Slot { uint32_t opcode, tag; JitEntry entry; };
Slot slots[4096] = {};
uint8_t *arena=nullptr;
size_t used=0;
bool allocationTried=false;
alignas(32) uint32_t staging[256]; // Never put the translation buffer on Wii stacks.
CacheStats stats={};
void publish(void *pointer,size_t length) {
#if defined(JIT_TEST)
    __builtin___clear_cache(static_cast<char*>(pointer),static_cast<char*>(pointer)+length);
#else
    DCFlushRange(pointer,length);
    ICInvalidateRange(pointer,length);
#endif
}
bool initialize() {
    if (allocationTried) return arena!=nullptr;
    allocationTried=true;
#if defined(JIT_TEST)
#ifdef JIT_TEST_ALLOC_FAIL
    return false; // Test-only simulation of the real allocation-failure exit.
#endif
    void *p=mmap(nullptr,arenaBytes,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (p==MAP_FAILED) return false;
#else
    // Allocate once for the emulator session. MEM2_Free is a no-op upstream.
    void *p=Noods_MEM2_Alloc(arenaBytes);
    if (!p || (reinterpret_cast<uintptr_t>(p)&31)) return false;
#endif
    arena=static_cast<uint8_t*>(p); return true;
}
}
void clearCache() {
    // Never patch executing code. There is no chaining or active native frame
    // when called by lookup() after a prior instruction returned.
    std::memset(slots,0,sizeof(slots)); used=0; ++stats.flushes;
}
CacheStats cacheStats() { return stats; }
JitEntry lookup(uint32_t opcode,bool thumb,bool arm7) {
    if (thumb && opcode>0xFFFF) return nullptr;
    uint32_t tag=1u|(uint32_t(thumb)<<1)|(uint32_t(arm7)<<2);
    size_t index=((opcode*0x9E3779B1u)^(opcode>>16)^(tag*0x85EBCA6Bu))&4095;
    Slot &slot=slots[index];
    if (slot.tag==tag && slot.opcode==opcode) { ++stats.hits; return slot.entry; }
    ++stats.misses;
    Writer w(staging,sizeof(staging)/sizeof(staging[0]));
    bool ok=thumb ? compileThumb(w,static_cast<uint16_t>(opcode)) : compileArm(w,opcode);
    if (!ok || !w.good()) {
        slot={opcode,tag,nullptr}; ++stats.rejected; return nullptr;
    }
    if (!initialize()) return nullptr; // Exact interpreter fallback, including OOM.
    size_t bytes=(w.size()*4+31)&~size_t(31);
    if (bytes>arenaBytes) return nullptr;
    if (used>arenaBytes-bytes) clearCache();
    uint8_t *entry=arena+used;
    std::memset(entry,0,bytes); std::memcpy(entry,staging,w.size()*4);
    publish(entry,bytes); // Aligned D writeback BEFORE I invalidate.
    used+=bytes;
    slot={opcode,tag,reinterpret_cast<JitEntry>(entry)};
    ++stats.compiles;
    return slot.entry;
}
}
