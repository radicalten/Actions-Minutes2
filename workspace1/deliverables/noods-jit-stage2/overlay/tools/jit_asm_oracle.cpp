// SPDX-License-Identifier: GPL-3.0-or-later
#include "../NooDS-Wii/jit_ppc_emitter.h"
#include <cstdio>
#include <cstdlib>
using namespace JitPpc;
static void require(bool ok) { if (!ok) { std::fputs("writer/range test failed\n",stderr); std::exit(1); } }
static void vector(const char *assembly,uint32_t word) { std::printf("%s|%08x\n",assembly,word); }
int main() {
    uint32_t w=0xDEADBEEF;
    require(!branch(2,false,w) && w==0xDEADBEEF);
    require(!branch(33554432,false,w)); require(!branch(-33554436,false,w));
    require(!branchCond(12,2,32768,false,w)); require(!branchCond(12,2,-32772,false,w));
    require(!branchCond(32,2,0,false,w)); require(!branchCond(12,32,0,false,w));
    uint32_t data[3]={0,0,0x12345678}; Writer writer(data,2);
    require(writer.emit(0)); require(writer.emit(0));
    require(writer.patchBranch(0,2)); require(data[0]==0x48000008);
    require(writer.patchBranchCond(1,0,12,2)); require(data[1]==0x4182FFFC);
    require(!writer.emit(0)); require(data[2]==0x12345678); require(!writer.good());
    require(!writer.patchBranch(0,1));
    writer.reset(); require(writer.good()); require(!writer.patchBranch(0,0));
    Writer empty(nullptr,0); require(!empty.emit(0));
#define V(asm, expr) vector(asm,expr)
    V("addi 3,0,-32768",addi(3,0,-32768)); V("addi 31,30,32767",addi(31,30,32767));
    V("addis 5,0,-1",addis(5,0,-1)); V("addic 3,4,-1",addic(3,4,-1));
    V("addic. 3,4,-1",addic(3,4,-1,true));
    V("ori 3,4,65535",ori(3,4,65535)); V("oris 3,4,32768",oris(3,4,32768));
    V("xori 31,3,32767",xori(31,3,32767)); V("andi. 3,4,255",andi(3,4,255));
#define A(name) \
    V(#name " 3,4,5",name(3,4,5)); V(#name ". 3,4,5",name(3,4,5,false,true)); \
    V(#name "o 3,4,5",name(3,4,5,true,false)); V(#name "o. 31,0,28",name(31,0,28,true,true))
    A(add); A(addc); A(adde); A(subf); A(subfc); A(subfe); A(mullw);
#undef A
    V("mulhw 3,4,5",mulhw(3,4,5)); V("mulhw. 31,0,28",mulhw(31,0,28,true));
    V("mulhwu 3,4,5",mulhwu(3,4,5)); V("mulhwu. 31,0,28",mulhwu(31,0,28,true));
#define L(asm,name) V(asm " 3,4,5",name(3,4,5)); V(asm ". 31,0,28",name(31,0,28,true))
    L("and",bitAnd); L("andc",andc); L("or",bitOr); L("xor",bitXor); L("nor",nor);
    L("slw",slw); L("srw",srw); L("sraw",sraw); L("srawi",srawi);
#undef L
    V("extsb 3,4",extsb(3,4)); V("extsb. 31,0",extsb(31,0,true));
    V("extsh 3,4",extsh(3,4)); V("extsh. 31,0",extsh(31,0,true));
    V("rlwinm 3,4,30,3,3",rlwinm(3,4,30,3,3));
    V("rlwinm. 31,0,0,31,0",rlwinm(31,0,0,31,0,true));
    V("rlwimi 3,4,30,3,3",rlwimi(3,4,30,3,3));
    V("rlwimi. 31,0,0,31,0",rlwimi(31,0,0,31,0,true));
    V("rlwnm 3,4,5,0,31",rlwnm(3,4,5,0,31));
    V("rlwnm. 31,0,28,31,0",rlwnm(31,0,28,31,0,true));
#define M(name) V(#name " 3,-32768(4)",name(3,4,-32768)); V(#name " 31,32767(30)",name(31,30,32767))
    M(lwz); M(lbz); M(lhz); M(stw); M(stwu); M(stb); M(sth);
#undef M
#define X(name) V(#name " 3,4,5",name(3,4,5)); V(#name " 31,0,28",name(31,0,28))
    X(lwbrx); X(lhbrx); X(stwbrx); X(sthbrx); X(lbzx); X(stbx);
#undef X
    V("cmpw 7,4,5",cmpw(7,4,5)); V("cmplw 0,31,0",cmplw(0,31,0));
    V("cmpwi 7,4,-32768",cmpwi(7,4,-32768)); V("cmplwi 0,31,65535",cmplwi(0,31,65535));
    V("mfcr 3",mfcr(3)); V("mfcr 31",mfcr(31)); V("mtcrf 255,3",mtcrf(255,3)); V("mtcrf 128,31",mtcrf(128,31));
    V("mfxer 3",mfspr(3,1)); V("mtxer 4",mtspr(1,4)); V("mflr 5",mfspr(5,8));
    V("mtlr 6",mtspr(8,6)); V("mfctr 7",mfspr(7,9)); V("mtctr 12",mtspr(9,12));
    V("mfspr 3,287",mfspr(3,287)); V("mtspr 272,31",mtspr(272,31));
    V("blr",bclr()); V("blrl",bclr(20,0,true)); V("bclr 12,2",bclr(12,2));
    V("bctr",bcctr()); V("bctrl",bcctr(20,0,true));
    require(branch(-33554432,false,w)); V("b .-33554432",w);
    require(branch(33554428,true,w)); V("bl .+33554428",w);
    require(branch(0,false,w)); V("b .",w);
    require(branchCond(12,2,-32768,false,w)); V("bc 12,2,.-32768",w);
    require(branchCond(4,30,32764,true,w)); V("bcl 4,30,.+32764",w);
    std::fputs("WRITER_RANGE_OK\n",stderr);
}
