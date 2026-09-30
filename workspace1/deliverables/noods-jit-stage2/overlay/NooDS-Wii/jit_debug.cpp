// SPDX-License-Identifier: GPL-3.0-or-later
#include "jit.h"
#include <cstdio>
namespace Jit {
void logMismatch(uint32_t pc,uint32_t opcode,bool arm7,bool thumb,
                 const JitContext &native,const JitContext &reference) {
    FILE *file=std::fopen("sd:/noods/jit_log.txt","a");
    if (!file) return;
    std::fprintf(file,"JIT_DIFFTEST first mismatch: ARM%d %s pc=%08lx opcode=%08lx; native disabled\n",
        arm7?7:9,thumb?"THUMB":"ARM",static_cast<unsigned long>(pc),static_cast<unsigned long>(opcode));
    for (unsigned i=0;i<16;i++)
        std::fprintf(file,"r%u native=%08lx reference=%08lx\n",i,
            static_cast<unsigned long>(native.regs[i]),static_cast<unsigned long>(reference.regs[i]));
    std::fprintf(file,"CPSR native=%08lx reference=%08lx cycles native=%lu reference=%lu\n",
        static_cast<unsigned long>(native.cpsr),static_cast<unsigned long>(reference.cpsr),
        static_cast<unsigned long>(native.cycles),static_cast<unsigned long>(reference.cycles));
    std::fflush(file); std::fclose(file);
}
}
