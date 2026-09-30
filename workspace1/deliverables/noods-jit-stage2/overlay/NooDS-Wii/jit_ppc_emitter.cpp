// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit_ppc_emitter.h"
#include <cassert>
#include <limits>
namespace JitPpc {
namespace {
uint32_t dform(unsigned op, unsigned t, unsigned a, uint16_t v) {
    assert(t<32 && a<32); return op<<26 | t<<21 | a<<16 | v;
}
uint32_t xform(unsigned xo, unsigned t, unsigned a, unsigned b, bool rc=false) {
    assert(t<32 && a<32 && b<32); return 31u<<26 | t<<21 | a<<16 | b<<11 | xo<<1 | unsigned(rc);
}
uint32_t rotate(unsigned op, unsigned d, unsigned s, unsigned n, unsigned mb, unsigned me, bool rc) {
    assert(d<32 && s<32 && n<32 && mb<32 && me<32);
    return op<<26 | s<<21 | d<<16 | n<<11 | mb<<6 | me<<1 | unsigned(rc);
}
}
uint32_t addi(unsigned d,unsigned a,int16_t v) { return dform(14,d,a,uint16_t(v)); }
uint32_t addis(unsigned d,unsigned a,int16_t v) { return dform(15,d,a,uint16_t(v)); }
uint32_t addic(unsigned d,unsigned a,int16_t v,bool rc) { return dform(rc?13:12,d,a,uint16_t(v)); }
#define LOGIMM(name,op) uint32_t name(unsigned d,unsigned s,uint16_t v) { return dform(op,s,d,v); }
LOGIMM(ori,24) LOGIMM(oris,25) LOGIMM(xori,26) LOGIMM(andi,28)
#undef LOGIMM
#define ARITH(name,xo) uint32_t name(unsigned d,unsigned a,unsigned b,bool oe,bool rc) { return xform(xo | (unsigned(oe)<<9),d,a,b,rc); }
ARITH(add,266) ARITH(addc,10) ARITH(adde,138)
ARITH(subf,40) ARITH(subfc,8) ARITH(subfe,136) ARITH(mullw,235)
#undef ARITH
uint32_t mulhw(unsigned d,unsigned a,unsigned b,bool rc) { return xform(75,d,a,b,rc); }
uint32_t mulhwu(unsigned d,unsigned a,unsigned b,bool rc) { return xform(11,d,a,b,rc); }
#define LOGREG(name,xo) uint32_t name(unsigned d,unsigned s,unsigned b,bool rc) { return xform(xo,s,d,b,rc); }
LOGREG(bitAnd,28) LOGREG(andc,60) LOGREG(bitOr,444) LOGREG(bitXor,316)
LOGREG(nor,124) LOGREG(slw,24) LOGREG(srw,536) LOGREG(sraw,792) LOGREG(srawi,824)
#undef LOGREG
uint32_t extsb(unsigned d,unsigned s,bool rc) { return xform(954,s,d,0,rc); }
uint32_t extsh(unsigned d,unsigned s,bool rc) { return xform(922,s,d,0,rc); }
uint32_t rlwinm(unsigned d,unsigned s,unsigned n,unsigned mb,unsigned me,bool rc) { return rotate(21,d,s,n,mb,me,rc); }
uint32_t rlwimi(unsigned d,unsigned s,unsigned n,unsigned mb,unsigned me,bool rc) { return rotate(20,d,s,n,mb,me,rc); }
uint32_t rlwnm(unsigned d,unsigned s,unsigned n,unsigned mb,unsigned me,bool rc) { return rotate(23,d,s,n,mb,me,rc); }
#define MEMD(name,op) uint32_t name(unsigned t,unsigned a,int16_t v) { return dform(op,t,a,uint16_t(v)); }
MEMD(lwz,32) MEMD(lbz,34) MEMD(lhz,40) MEMD(stw,36) MEMD(stwu,37) MEMD(stb,38) MEMD(sth,44)
#undef MEMD
#define MEMX(name,xo) uint32_t name(unsigned t,unsigned a,unsigned b) { return xform(xo,t,a,b); }
MEMX(lwbrx,534) MEMX(lhbrx,790) MEMX(stwbrx,662) MEMX(sthbrx,918) MEMX(lbzx,87) MEMX(stbx,215)
#undef MEMX
uint32_t cmpw(unsigned f,unsigned a,unsigned b) { assert(f<8); return xform(0,f<<2,a,b); }
uint32_t cmplw(unsigned f,unsigned a,unsigned b) { assert(f<8); return xform(32,f<<2,a,b); }
uint32_t cmpwi(unsigned f,unsigned a,int16_t v) { assert(f<8); return dform(11,f<<2,a,uint16_t(v)); }
uint32_t cmplwi(unsigned f,unsigned a,uint16_t v) { assert(f<8); return dform(10,f<<2,a,v); }
uint32_t mfcr(unsigned d) { return xform(19,d,0,0); }
uint32_t mtcrf(unsigned mask,unsigned s) { assert(mask<256); return xform(144,s,0,0) | mask<<12; }
uint32_t mfspr(unsigned d,unsigned spr) { assert(spr<1024); return xform(339,d,spr&31,spr>>5); }
uint32_t mtspr(unsigned spr,unsigned s) { assert(spr<1024); return xform(467,s,spr&31,spr>>5); }
uint32_t bclr(unsigned bo,unsigned bi,bool link) {
    assert(bo<32 && bi<32); return 19u<<26 | bo<<21 | bi<<16 | 16u<<1 | unsigned(link);
}
uint32_t bcctr(unsigned bo,unsigned bi,bool link) {
    assert(bo<32 && bi<32 && (bo&4)); // CTR is the target; cannot decrement it.
    return 19u<<26 | bo<<21 | bi<<16 | 528u<<1 | unsigned(link);
}
bool branch(int64_t delta,bool link,uint32_t &word) {
    if (delta%4 || delta < -33554432 || delta > 33554428) return false;
    word=18u<<26 | (uint32_t(delta)&0x03FFFFFC) | unsigned(link); return true;
}
bool branchCond(unsigned bo,unsigned bi,int64_t delta,bool link,uint32_t &word) {
    if (bo>=32 || bi>=32 || delta%4 || delta < -32768 || delta > 32764) return false;
    word=16u<<26 | bo<<21 | bi<<16 | (uint32_t(delta)&0xFFFC) | unsigned(link); return true;
}
bool Writer::emit(uint32_t word) {
    if (failed_ || !words_ || count_>=capacity_) { failed_=true; return false; }
    words_[count_++]=word; return true;
}
bool Writer::patchBranch(size_t from,size_t to,bool link) {
    uint32_t word;
    // Bounds imply delta multiplication fits in int64_t on a 32-bit target;
    // explicit limit also makes the staging writer safe on 64-bit test hosts.
    if (failed_ || from>=count_ || to>count_ || count_>size_t(INT64_MAX/4) ||
        !branch((int64_t(to)-int64_t(from))*4,link,word)) { failed_=true; return false; }
    words_[from]=word; return true;
}
bool Writer::patchBranchCond(size_t from,size_t to,unsigned bo,unsigned bi,bool link) {
    uint32_t word;
    if (failed_ || from>=count_ || to>count_ || count_>size_t(INT64_MAX/4) ||
        !branchCond(bo,bi,(int64_t(to)-int64_t(from))*4,link,word)) { failed_=true; return false; }
    words_[from]=word; return true;
}
} // namespace JitPpc
