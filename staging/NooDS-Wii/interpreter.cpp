#include "gba_arm_alu.h"
#if NOODS_GBA_DEDICATED || NOODS_GBA_ROM_ISLAND
#include "arm_alu_specialized.h"
#endif
/*
    Copyright (C) 2019-2025 Hydr8gon
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

#include "core.h"
#include "arm_jit.h"
#include "arm_jit_fast.h"
#include "mpu_pure.h"
#include <tuxedo/thread.h>

#ifndef NOODS_STRICT_SINGLE_PRECHECK
#define NOODS_STRICT_SINGLE_PRECHECK 1
#endif
#ifndef NOODS_STRICT_BRANCH_PRECHECK
#define NOODS_STRICT_BRANCH_PRECHECK 1
#endif

#if NOODS_NDS_TASK_PROFILE
// Profile every scheduler mode, not just the dual-CPU NDS loop. The old
// NDS_CPU counter deliberately remains dual-only for historical comparisons.
static uint64_t singleCpuTicks[3]; // NDS ARM9 only, NDS ARM7 only, GBA
static uint64_t singleTaskTicks[3][MAX_TASKS];
static uint32_t singleTaskCalls[3][MAX_TASKS];
#endif

Interpreter::Interpreter(Core *core, bool arm7): core(core), arm7(arm7) {
    // Initialize the registers for user mode
    for (int i = 0; i < 32; i++)
        registers[i] = &registersUsr[i & 0xF];
}

void Interpreter::saveState(FILE *file) {
    // Write state data to the file
    fwrite(pipeline, 4, sizeof(pipeline) / 4, file);
    fwrite(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fwrite(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fwrite(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fwrite(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fwrite(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fwrite(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fwrite(&cpsr, sizeof(cpsr), 1, file);
    fwrite(&spsrFiq, sizeof(spsrFiq), 1, file);
    fwrite(&spsrSvc, sizeof(spsrSvc), 1, file);
    fwrite(&spsrAbt, sizeof(spsrAbt), 1, file);
    fwrite(&spsrIrq, sizeof(spsrIrq), 1, file);
    fwrite(&spsrUnd, sizeof(spsrUnd), 1, file);
    fwrite(&cycles, sizeof(cycles), 1, file);
    fwrite(&halted, sizeof(halted), 1, file);
    fwrite(&dsiCycle, sizeof(dsiCycle), 1, file);
    fwrite(&ime, sizeof(ime), 1, file);
    fwrite(&ie, sizeof(ie), 1, file);
    fwrite(&irf, sizeof(irf), 1, file);
    fwrite(&postFlg, sizeof(postFlg), 1, file);
}

void Interpreter::loadState(FILE *file) {
    // Read state data from the file
    fread(pipeline, 4, sizeof(pipeline) / 4, file);
    fread(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fread(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fread(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fread(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fread(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fread(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fread(&cpsr, sizeof(cpsr), 1, file);
    fread(&spsrFiq, sizeof(spsrFiq), 1, file);
    fread(&spsrSvc, sizeof(spsrSvc), 1, file);
    fread(&spsrAbt, sizeof(spsrAbt), 1, file);
    fread(&spsrIrq, sizeof(spsrIrq), 1, file);
    fread(&spsrUnd, sizeof(spsrUnd), 1, file);
    fread(&cycles, sizeof(cycles), 1, file);
    fread(&halted, sizeof(halted), 1, file);
    fread(&dsiCycle, sizeof(dsiCycle), 1, file);
    fread(&ime, sizeof(ime), 1, file);
    fread(&ie, sizeof(ie), 1, file);
    fread(&irf, sizeof(irf), 1, file);
    fread(&postFlg, sizeof(postFlg), 1, file);

    // Update mapped registers
    swapRegisters(cpsr);
    pcData = nullptr;
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif
}

void Interpreter::init() {
    // Prepare to boot the BIOS
    spsrFiq = spsrSvc = spsrAbt = spsrIrq = spsrUnd = 0x10;
    setCpsr(0x000000D3); // Supervisor, interrupts off
    registersUsr[15] = arm7 ? 0x00000000 : 0xFFFF0000;
    flushPipeline();
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif

    // Reset the I/O registers
    ime = 0;
    ie = irf = 0;
    postFlg = 0;
}

void Interpreter::directBoot() {
    // Prepare to directly boot a ROM
    spsrFiq = spsrSvc = spsrAbt = spsrIrq = spsrUnd = 0x10;
    setCpsr(0x000000DF); // System, interrupts off
    registersUsr[15] = core->gbaMode ? 0x8000000 : entryAddr;
    registersUsr[14] = registersUsr[15];
    registersUsr[12] = registersUsr[15];
    registersUsr[13] = arm7 ? (core->gbaMode ? 0x3007F00 : 0x380FD80) : 0x3002F7C;
    registersIrq[0] = arm7 ? (core->gbaMode ? 0x3007FA0 : 0x380FF80) : 0x3003F80;
    registersSvc[0] = arm7 ? (core->gbaMode ? 0x3007FE0 : 0x380FFC0) : 0x3003FC0;
    flushPipeline();
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif
}

void Interpreter::resetCycles() {
    // Preserve 0xFFFFFFFF sentinel when halted; otherwise adjust for global cycle reset
    if (cycles != 0xFFFFFFFFu)
        cycles -= std::min(core->globalCycles, cycles);
}

void Interpreter::runCoreNone(Core &core) {
    // Run the core with no active CPUs
    while (true) {
        // v1.116: the flag is only read here in the common case; interrupts are masked only
        // when a stop request is actually taken (this runs once per scheduler event).
        if (__builtin_expect(!core.running, 0)) {
            PPCIrqState st = PPCIrqLockByMsr();
            core.running = true;
            PPCIrqUnlockByMsr(st);
            break;
        }

        // Jump to the next task and run all that are scheduled at this timestamp
        uint32_t target = core.events[0].cycles;
        while (core.events[0].cycles <= target) {
            SchedEvent ev = core.events[0];
            core.events.popFront();
            core.globalCycles = ev.cycles;
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
            ArmPpcJit::pollEpoch++;
#endif
            core.tasks[ev.task]();
            if (ev.task == RESET_CYCLES)
                target = 0;
        }
    }
}

#if NOODS_GBA_ARM_BURST
#if NOODS_NDS_TASK_PROFILE
static uint32_t gbaBurstCounts[4]; // successful runs, instructions, ALU, mapped transfers
#endif
#if NOODS_GBA_ARM_BURST >= 3
__attribute__((always_inline)) inline
#endif
bool Interpreter::runGbaArmBurst() {
    Core &c=*core;GbaBus &bus=c.gbaBus;
    uint32_t r15=*registers[15];
    if ((cpsr&0x20u) || (r15>>24)!=3 || (r15&3) || !pcData || bus.active ||
        c.globalCycles!=cycles || c.events.frontCycles<=cycles+1) return false;
    unsigned slots=(0xFFCu-(r15&0xFFFu))/4;
    if (!slots) return false;
    uint32_t clock=cycles,op=pipeline[0],next=pipeline[1];
    uint8_t *pc=pcData;unsigned done=0,last=0;
    do {
        const unsigned cond=condition[((op>>24)&0xF0u)|(cpsr>>28)];
        if (cond==2) break;
        const bool alu=GbaArmAlu::eligible(op);
        uint32_t address=0,dataCost=0;bool load=false,byte=false;
        if (cond && !alu) {
#if NOODS_GBA_ARM_BURST >= 2
            // Pre-indexed, no-writeback, ordinary word/byte transfers only.
            // All I/O, BIOS/open-bus, GamePak and special encodings fall back.
            if ((op&0x0D200000u)!=0x05000000u || ((op>>12)&15)==15 ||
                ((op&0x02000000u) && (op&0x10u))) break;
            auto reg=[&](unsigned n){return n==15?r15+4:*registers[n];};
            uint32_t offset=op&0xFFFu;
            if(op&0x02000000u) {
                const uint32_t v=reg(op&15);const unsigned n=(op>>7)&31;
                switch((op>>5)&3) {
                case 0:offset=v<<n;break;case 1:offset=n?v>>n:0;break;
                case 2:offset=uint32_t(int32_t(v)>>(n?n:31));break;
                default:offset=n?GbaArmAlu::ror(v,n):((cpsr&0x20000000u)<<2)|(v>>1);break;
                }
            }
            address=reg((op>>16)&15);address=(op&0x00800000u)?address+offset:address-offset;
            load=op&0x00100000u;byte=op&0x00400000u;
            const unsigned region=address>>24;
            if(region!=2 && region!=3 && region!=6) break;
            if(!(load?c.memory.readMap7:c.memory.writeMap7)[address>>12]) break;
            dataCost=region==2?(byte?3:6):region==6?(byte?1:2):1;
#else
            break;
#endif
        }
        const unsigned cost=1+dataCost+(load?1:0);
        if(c.events.frontCycles-clock<=cost)break;
        const uint32_t executing=op;
        r15+=4;*registers[15]=r15;op=next;pc+=4;next=U8TO32(pc,0);
        last=0;
        if(cond) {
            if(alu) {
                GbaArmAlu::execute(registers,cpsr,executing);
#if NOODS_NDS_TASK_PROFILE
                ++gbaBurstCounts[2];
#endif
            } else {
                last=load?2:1;
                if(load) {
                    const uint8_t *data=c.memory.readMap7[address>>12];
                    uint32_t value=byte?data[address&0xFFFu]:U8TO32(data,address&0xFFCu);
                    if(!byte && (address&3))value=GbaArmAlu::ror(value,(address&3)*8);
                    *registers[(executing>>12)&15]=value;
                } else {
                    // Preserve ordinary write-side SMC invalidation and GBA byte-VRAM rules.
                    cycles=clock+1+dataCost;c.globalCycles=cycles;
                    bus.beginInstruction();bus.nextSeq=false;bus.inSeqAccess=bus.hadDataAccess=true;
                    bus.lastFetchUsedDuty=false;
                    const uint32_t value=*registers[(executing>>12)&15];
                    if(byte)c.memory.write<uint8_t>(true,address,value);
                    else c.memory.write<uint32_t>(true,address,value);
                }
#if NOODS_NDS_TASK_PROFILE
                ++gbaBurstCounts[3];
#endif
            }
        }
        clock+=cost;++done;
    } while(done<slots);
    if (!done) return false;
    pipeline[0]=op;pipeline[1]=next;pcData=pc;cycles=clock;c.globalCycles=clock;
    bus.lastFetchUsedDuty=false;bus.beginInstruction();bus.nextSeq=last==0;
    if(last){bus.inSeqAccess=bus.hadDataAccess=true;bus.hadLoad=last==2;}
    c.memory.clearGbaDmaOpenBus();
#if NOODS_NDS_TASK_PROFILE
    ++gbaBurstCounts[0];gbaBurstCounts[1]+=done;
#endif
    return true;
}
#endif
#if NOODS_GBA_DEDICATED
#include "gba_dedicated.inc"
#endif
#if NOODS_GBA_ROM_ISLAND
#include "arm7tdmi_mul.h"
#include "gba_rom_island.inc"
#endif
#if NOODS_GBA_THUMB_ISLAND
#if !NOODS_GBA_ROM_ISLAND
#include "arm7tdmi_mul.h"
#endif
#if NOODS_GBA_ISLAND_STATS
uint32_t g_tisEntries=0,g_tisFalseEntry=0,g_tisInstrs=0;
uint32_t g_tisBrk[8]={0};  // 0 region,1 margin,2 spare,3 spare,4 unhandled,5 region/mapped,6 page,7 spare
uint32_t g_tisCat[7]={0};  // kind 0..6
#endif
#include "gba_thumb_island.inc"
#if NOODS_GBA_ISLAND_STATS
void dumpGbaThumbIslandStats(FILE *f) {
    if (!f) return;
    fprintf(f, "GBA_TISLAND entries=%u false_entries=%u instrs=%u cats alu=%u x2=%u mul=%u xfer=%u block=%u branch=%u blsetup=%u\n",
        g_tisEntries,g_tisFalseEntry,g_tisInstrs,g_tisCat[0],g_tisCat[1],g_tisCat[2],g_tisCat[3],g_tisCat[4],g_tisCat[5],g_tisCat[6]);
    fprintf(f, "GBA_TISLAND_BREAKS region=%u margin=%u b2=%u b3=%u unhandled=%u regionmap=%u page=%u\n",
        g_tisBrk[0],g_tisBrk[1],g_tisBrk[2],g_tisBrk[3],g_tisBrk[4],g_tisBrk[5],g_tisBrk[6]);
#if NOODS_GBA_ISLAND_VERIFY
    { extern int g_tisVerifyFails; extern uint32_t g_tisFirstPc, g_tisFirstOp, g_tisFirstDepth, g_tisLastPc, g_tisLastOp;
      extern char g_tisFirstWhy[16], g_tisLastWhy[16];
      extern uint32_t g_tisFirstI0, g_tisFirstI1, g_tisFirstP0, g_tisFirstP1, g_tisFirstR15, g_tisFirstIPc;
      fprintf(f, "GBA_TISLAND_VERIFY fails=%d first=%s pc=%08X op=%04X depth=%u last=%s pc=%08X op=%04X\n",
        g_tisVerifyFails, g_tisFirstWhy, g_tisFirstPc, g_tisFirstOp, g_tisFirstDepth,
        g_tisLastWhy, g_tisLastPc, g_tisLastOp);
      extern uint32_t g_tisRegIdx, g_tisRegIsl, g_tisRegRef, g_tisCpsrIsl, g_tisCpsrRef;
      fprintf(f, "GBA_TISLAND_PIPE isl=[%04X,%04X] ref=[%04X,%04X] r15=%08X pcOff=%u\n",
        g_tisFirstI0, g_tisFirstI1, g_tisFirstP0, g_tisFirstP1, g_tisFirstR15, g_tisFirstIPc);
      fprintf(f, "GBA_TISLAND_REG idx=%u isl=%08X ref=%08X cpsr isl=%08X ref=%08X\n",
        g_tisRegIdx, g_tisRegIsl, g_tisRegRef, g_tisCpsrIsl, g_tisCpsrRef); }
#endif
}
#endif
#endif
#if NOODS_GBA_ROM_ISLAND
#if NOODS_GBA_ISLAND_STATS
uint32_t g_loopIters=0, g_loopThumb=0, g_loopOpcode=0;
void dumpGbaIslandStats(FILE *f) {
    if (f) fprintf(f, "GBA_LOOP iters=%u thumb=%u runGbaOpcode=%u\n", g_loopIters, g_loopThumb, g_loopOpcode);
    if (!f) return;
    fprintf(f, "GBA_ISLAND entries=%u false_entries=%u instrs=%u cats alu=%u false=%u mul=%u branch=%u xfer=%u block=%u\n",
        g_islEntries,g_islFalseEntry,g_islInstrs,g_islCat[0],g_islCat[1],g_islCat[2],g_islCat[3],g_islCat[4],g_islCat[5]);
    fprintf(f, "GBA_ISLAND_BREAKS cond2=%u page=%u region=%u margin=%u unhandled=%u noprog=%u gpio=%u\n",
        g_islBrk[0],g_islBrk[1],g_islBrk[2],g_islBrk[3],g_islBrk[4],g_islBrk[5],g_islBrk[6]);
    fprintf(f, "GBA_ISLAND_WHY thumb=%u notrom=%u unaligned=%u nopc=%u desync=%u margin=%u\n",
        g_islWhy[0],g_islWhy[1],g_islWhy[2],g_islWhy[3],g_islWhy[4],g_islWhy[5]);
}
#endif
#endif
#if NOODS_JIT && NOODS_JIT_BLOCKS
// v1p171: relaxed GBA timing, opt-in via Settings::gbaFastTiming (off by default).
// Kept out of line on purpose: inlining this loop into runCoreSingle cost the exact
// path ~3% (LTO re-decides inlining for the whole hot function), so it lives here and
// the exact path keeps v1p170's code shape.
void __attribute__((noinline)) Interpreter::runGbaFastTiming(Interpreter &arm, Core &core) {
    const bool jitOn = Settings::armJit != 0;
    ArmPpcJit::linkMode = 4u; // v1.120
    while (core.events.frontCycles > arm.cycles) {
        const uint32_t pcNow = arm.registersUsr[15];
        const uint32_t thumbNow = arm.cpsr & 0x20u;
        int c = 0;
        if (__builtin_expect(jitOn, 1))
            c = ArmPpcJit::fastDispatch(&arm, &core, true, 0u, [&](bool) {
                return core.events.frontCycles - arm.cycles; });
        if (__builtin_expect(c == 0, 0)) {
            uint32_t rem = core.events[0].cycles - arm.cycles;
            c = arm.runOpcode(rem, rem);
        }
        const uint32_t region = pcNow >> 24;
        if (region >= 8u) c += thumbNow ? c : (c << 1);          // cartridge ROM
        else if (region == 2u) c *= thumbNow ? 3 : 6;            // external work RAM
        arm.cycles = (core.globalCycles += c);
    }
}
#endif
template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);
template <bool _arm7, int shift> void Interpreter::runCoreSingle(Core &core) {
    // Run the core with one active CPU
    Interpreter &arm = core.interpreter[_arm7];
    while (true) {
        // v1.116: the flag is only read here in the common case; interrupts are masked only
        // when a stop request is actually taken (this runs once per scheduler event).
        if (__builtin_expect(!core.running, 0)) {
            PPCIrqState st = PPCIrqLockByMsr();
            core.running = true;
            PPCIrqUnlockByMsr(st);
            break;
        }

#if NOODS_NDS_TASK_PROFILE
        const unsigned profileMode = core.gbaMode ? 2u : (_arm7 ? 1u : 0u);
        const uint64_t singleCpuStart = PPCGetTickCount();
#endif
        // Run a CPU until the next scheduled task
        core.globalCycles = std::max(core.globalCycles, arm.cycles);
#if NOODS_JIT && NOODS_JIT_BLOCKS
        ArmPpcJit::linkMode = 0u; // v1.120: set again by the loop that supports block links
#endif
#if NOODS_JIT && NOODS_JIT_BLOCKS
        if (__builtin_expect(Settings::gbaFastTiming && _arm7 && core.gbaMode && shift == 0, 0))
            runGbaFastTiming(arm, core);
        else
#endif
        if (_arm7 && core.gbaMode) {
            while (core.events.frontCycles > arm.cycles) {
#if NOODS_GBA_ISLAND_STATS
                ++g_loopIters;
                if (arm.isThumb()) ++g_loopThumb;
#endif
                // Cheap inline mode gate: the islands are mode-specific, and
                // nearly all GBA titles run THUMB code, so an unguarded ARM-only
                // island would cost a function call per THUMB instruction.
#if NOODS_GBA_THUMB_ISLAND
                if(arm.isThumb()) { if(arm.runGbaThumbIsland()) continue; }
                else
#endif
#if NOODS_GBA_ROM_ISLAND
                if(!arm.isThumb() && arm.runGbaRomIsland()) continue;
#if NOODS_GBA_THUMB_ISLAND
                else
#endif
#endif
#if NOODS_GBA_THUMB_ISLAND && !NOODS_GBA_ROM_ISLAND
                else
#endif
                { }
#if NOODS_GBA_DEDICATED
                if(arm.runGbaDedicated()) continue;
#endif
#if NOODS_GBA_ARM_BURST
                if(arm.runGbaArmBurst()) continue;
#endif
#if NOODS_JIT && NOODS_JIT_BLOCKS && NOODS_GBA_JIT
                if (Settings::armJit && arm.pcData && core.gbaBus.nextSeq &&
                    ArmPpcJit::tryExecuteGbaFast(&arm, core.memory.getWaitCnt(), core.events[0].cycles))
                    continue;
#endif
#if NOODS_GBA_ISLAND_STATS
                ++g_loopOpcode;
#endif
                arm.runGbaOpcode();
            }
        }
        else {
#if NOODS_JIT && NOODS_JIT_BLOCKS
            // <true, 0> is the GBA instantiation; its NDS branch is never taken, and is kept
            // as it was so the GBA loop's code generation does not change.
            if (_arm7 && shift == 0) {
                while (core.events[0].cycles > arm.cycles) {
                    uint32_t rem = (core.events[0].cycles - arm.cycles + shift) >> shift;
                    arm.cycles = (core.globalCycles += arm.runOpcode(rem, rem) << shift);
                }
            }
            else {
            // v1.101: try the inline block-hit path first; fall back to the unchanged slow path.
            const bool jitOn = Settings::armJit != 0;
            ArmPpcJit::linkMode = _arm7 ? 3u : 1u; // v1.120
            while (core.events.frontCycles > arm.cycles) {
                int c = 0;
                if (__builtin_expect(jitOn, 1))
                    c = ArmPpcJit::fastDispatch(&arm, &core, _arm7, (uint32_t)shift, [&](bool) {
                        return (core.events.frontCycles - arm.cycles + shift) >> shift; });
                if (__builtin_expect(c == 0, 0)) {
                    uint32_t rem = (core.events[0].cycles - arm.cycles + shift) >> shift;
                    c = arm.runOpcode(rem, rem);
                }
                arm.cycles = (core.globalCycles += c << shift);
            }
            }
#else
            while (core.events[0].cycles > arm.cycles) {
                uint32_t rem = (core.events[0].cycles - arm.cycles + shift) >> shift;
                arm.cycles = (core.globalCycles += arm.runOpcode(rem, rem) << shift);
            }
#endif
        }

#if NOODS_NDS_TASK_PROFILE
        singleCpuTicks[profileMode] += PPCGetTickCount() - singleCpuStart;
#endif
        // Run all tasks scheduled up to the current timestamp, keeping mid-instruction GBA DMAs for runGbaOpcode Step 2!
        uint32_t initialTarget = core.events[0].cycles;
        uint32_t limit = std::max(initialTarget, arm.halted ? initialTarget : arm.cycles);
        while (core.events[0].cycles <= limit) {
            SchedEvent ev = core.events[0];
            if (core.gbaMode && !Settings::gbaFastTiming && ev.cycles > initialTarget &&
                ev.task >= DMA7_TRANSFER0 && ev.task <= DMA7_TRANSFER3) {
                break;
            }
            core.events.popFront();
            core.globalCycles = ev.cycles;
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
            ArmPpcJit::pollEpoch++;
#endif
#if NOODS_NDS_TASK_PROFILE
            const uint64_t singleTaskStart = PPCGetTickCount();
            core.tasks[ev.task]();
            singleTaskTicks[profileMode][ev.task] += PPCGetTickCount() - singleTaskStart;
            ++singleTaskCalls[profileMode][ev.task];
#else
            core.tasks[ev.task]();
#endif
            if (ev.task == RESET_CYCLES) {
                initialTarget = 0;
                limit = arm.halted ? 0u : arm.cycles;
            }
            if (!arm.halted && arm.cycles > limit)
                limit = arm.cycles;
        }
        if (!arm.halted)
            core.globalCycles = std::max(core.globalCycles, arm.cycles);
    }
}

#if NOODS_NDS_TASK_PROFILE
// Diagnostic only: aggregate PPC time in CPU scheduling and each scheduled task.
static uint64_t ndsCpuTicks;
static uint64_t ndsTaskTicks[MAX_TASKS];
static uint32_t ndsTaskCalls[MAX_TASKS];

void Interpreter::dumpNdsTaskProfile(FILE *f) {
#if NOODS_GBA_ARM_BURST
    fprintf(f,"GBA_BURST runs=%u instructions=%u alu=%u memory=%u\n",gbaBurstCounts[0],gbaBurstCounts[1],gbaBurstCounts[2],gbaBurstCounts[3]);
#endif


    for (unsigned mode = 0; mode < 3; ++mode) {
        fprintf(f, "SINGLE_CPU mode=%u us=%llu\n", mode, (unsigned long long)PPCTicksToUs(singleCpuTicks[mode]));
        for (unsigned task = 0; task < MAX_TASKS; ++task) {
            if (singleTaskCalls[mode][task])
                fprintf(f, "SINGLE_TASK mode=%u id=%u count=%u us=%llu\n", mode, task, singleTaskCalls[mode][task], (unsigned long long)PPCTicksToUs(singleTaskTicks[mode][task]));
        }
    }
    fprintf(f, "NDS_CPU us=%llu\n", (unsigned long long)PPCTicksToUs(ndsCpuTicks));
    for (unsigned i = 0; i < MAX_TASKS; ++i) {
        if (ndsTaskCalls[i])
            fprintf(f, "NDS_TASK id=%u count=%u us=%llu\n", i, ndsTaskCalls[i],
                    (unsigned long long)PPCTicksToUs(ndsTaskTicks[i]));
    }
}
#endif

void Interpreter::runCoreNds(Core &core) {
    // Run the core with both CPUs active in NDS mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (true) {
        // v1.116: the flag is only read here in the common case; interrupts are masked only
        // when a stop request is actually taken (this runs once per scheduler event).
        if (__builtin_expect(!core.running, 0)) {
            PPCIrqState st = PPCIrqLockByMsr();
            core.running = true;
            PPCIrqUnlockByMsr(st);
            break;
        }

        // Run the ARM9 and half-speed ARM7 until the next scheduled task
#if NOODS_NDS_TASK_PROFILE
        uint64_t cpuStart = PPCGetTickCount();
#endif
#if NOODS_JIT && NOODS_JIT_BLOCKS
        const bool jitOn = Settings::armJit != 0;
#endif
#if NOODS_JIT && NOODS_JIT_BLOCKS
        ArmPpcJit::linkMode = 2u; // v1.120
#endif
        while (core.events.frontCycles > core.globalCycles) {
            const uint32_t evTarget = core.events.frontCycles;
#if NOODS_JIT && NOODS_JIT_BLOCKS
            ArmPpcJit::linkEvTarget = evTarget;
#endif
            if (core.globalCycles >= arm9.cycles) {
#if NOODS_JIT && NOODS_JIT_BLOCKS
                // v1.101: the remaining-cycle bounds are only needed by loop blocks and by the
                // slow path, so they are computed lazily.
                auto remIndep9 = [&]() -> uint32_t { return evTarget - core.globalCycles; };
                auto remShared9 = [&]() -> uint32_t {
                    uint32_t safeEnd9 = arm7.halted ? evTarget : std::min<uint32_t>(evTarget, std::max<uint32_t>(core.globalCycles + 1u, arm7.cycles));
                    return safeEnd9 - core.globalCycles;
                };
                int c9 = 0;
                if (__builtin_expect(jitOn, 1))
                    c9 = ArmPpcJit::fastDispatch(&arm9, &core, false, 0u, [&](bool indep) {
                        return indep ? remIndep9() : remShared9(); });
                if (__builtin_expect(c9 == 0, 0))
                    c9 = arm9.runOpcode(remIndep9(), remShared9());
                arm9.cycles = core.globalCycles + c9;
#else
                uint32_t remIndep9 = evTarget - core.globalCycles;
                uint32_t safeEnd9 = arm7.halted ? evTarget : std::min<uint32_t>(evTarget, std::max<uint32_t>(core.globalCycles + 1u, arm7.cycles));
                uint32_t remShared9 = safeEnd9 - core.globalCycles;
                arm9.cycles = core.globalCycles + arm9.runOpcode(remIndep9, remShared9);
#endif
            }
            if (core.globalCycles >= arm7.cycles) {
#if NOODS_JIT && NOODS_JIT_BLOCKS
                auto remIndep7 = [&]() -> uint32_t { return (evTarget - core.globalCycles + 1u) >> 1; };
                auto remShared7 = [&]() -> uint32_t {
                    uint32_t safeEnd7 = arm9.halted ? evTarget : std::min<uint32_t>(evTarget, std::max<uint32_t>(core.globalCycles + 2u, arm9.cycles));
                    return (safeEnd7 - core.globalCycles + 1u) >> 1;
                };
                int c7 = 0;
                if (__builtin_expect(jitOn, 1))
                    c7 = ArmPpcJit::fastDispatch(&arm7, &core, true, 1u, [&](bool indep) {
                        return indep ? remIndep7() : remShared7(); });
                if (__builtin_expect(c7 == 0, 0))
                    c7 = arm7.runOpcode(remIndep7(), remShared7());
                arm7.cycles = core.globalCycles + (c7 << 1);
#else
                uint32_t remIndep7 = (evTarget - core.globalCycles + 1u) >> 1;
                uint32_t safeEnd7 = arm9.halted ? evTarget : std::min<uint32_t>(evTarget, std::max<uint32_t>(core.globalCycles + 2u, arm9.cycles));
                uint32_t remShared7 = (safeEnd7 - core.globalCycles + 1u) >> 1;
                arm7.cycles = core.globalCycles + (arm7.runOpcode(remIndep7, remShared7) << 1);
#endif
            }
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

#if NOODS_NDS_TASK_PROFILE
        ndsCpuTicks += PPCGetTickCount() - cpuStart;
#endif
        // Jump to the next task and run all that are scheduled now
        uint32_t target = core.events[0].cycles;
        core.globalCycles = target;
        while (core.events[0].cycles <= target) {
            SchedEvent ev = core.events[0];
            core.events.popFront();
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
            ArmPpcJit::pollEpoch++;
#endif
#if NOODS_NDS_TASK_PROFILE
            uint64_t taskStart = PPCGetTickCount();
            core.tasks[ev.task]();
            ndsTaskTicks[ev.task] += PPCGetTickCount() - taskStart;
            ndsTaskCalls[ev.task]++;
#else
            core.tasks[ev.task]();
#endif
            if (ev.task == RESET_CYCLES)
                target = 0;
        }
    }
}

void Interpreter::runCoreDsi(Core &core) {
    // Run the core in DSi mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (true) {
        // v1.116: the flag is only read here in the common case; interrupts are masked only
        // when a stop request is actually taken (this runs once per scheduler event).
        if (__builtin_expect(!core.running, 0)) {
            PPCIrqState st = PPCIrqLockByMsr();
            core.running = true;
            PPCIrqUnlockByMsr(st);
            break;
        }

        // Run both CPUs until the next scheduled task
#if NOODS_JIT && NOODS_JIT_BLOCKS
        ArmPpcJit::linkMode = 0u; // v1.120: no block links in DSi mode
#endif
        while (core.events[0].cycles > core.globalCycles) {
            // Run the ARM9 twice as fast as usual
            if (core.globalCycles >= arm9.cycles) {
                int cycles = arm9.runOpcode() + arm9.dsiCycle;
                arm9.cycles = core.globalCycles + (cycles >> 1);
                arm9.dsiCycle = (cycles & 0x1);
            }

            // Run the ARM7 at half speed and advance to the next opcode cycle
            if (core.globalCycles >= arm7.cycles)
                arm7.cycles = core.globalCycles + (arm7.runOpcode() << 1);
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

        // Jump to the next task and run all that are scheduled now
        uint32_t target = core.events[0].cycles;
        core.globalCycles = target;
        while (core.events[0].cycles <= target) {
            SchedEvent ev = core.events[0];
            core.events.popFront();
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
            ArmPpcJit::pollEpoch++;
#endif
            core.tasks[ev.task]();
            if (ev.task == RESET_CYCLES)
                target = 0;
        }
    }
}

#ifndef NOODS_MPU_PURE_FAST
#define NOODS_MPU_PURE_FAST 0
#endif
uint32_t g_mpuPureFastStats[3]; // arm pure, thumb pure, condition false

// Conservative instruction classes that cannot access DS data memory and do
// not change PC, modes or status-register banking. Unrecognized forms retain
// the original checked path. Instruction permission is checked separately.
FORCE_INLINE int Interpreter::runOpcode(uint32_t remIndep, uint32_t remShared) {
    if (!arm7 && __builtin_expect(core->cp15.mpuRestricted, 0)) {
        const uint32_t faultPc = *registers[15] - ((cpsr & BIT(5)) ? 2u : 4u);
        const bool priv = isPrivileged();
        // Fast gate: standard privileged layout (e.g. libnds default MPU) with executable code page
        // needs no per-instruction data check on mapped RAM/IO; only enter slow path on User mode,
        // custom data restrictions, or non-executable PC page.
        if (!priv || !core->cp15.mpuFastPriv ||
            !(core->cp15.getPagePerm(faultPc >> 12) & 0x04)) {
            if (!core->cp15.checkAccessPage(faultPc, false, false, priv))
                return takeAbort(0x0C, faultPc + 4);

#if NOODS_MPU_PURE_FAST
            // The same checked interpreter dispatch with no rollback snapshot
            // is safe only if the instruction provably cannot touch data.
            // Condition-false ARM instructions do not execute at all.
            const uint32_t pureOp = pipeline[0];
            const bool thumbPure = (cpsr & BIT(5)) && mpuPureThumb((uint16_t)pureOp);
            const int armCond = !(cpsr & BIT(5)) ? condition[((pureOp >> 24) & 0xF0) | (cpsr >> 28)] : 2;
            const bool falseArm = !(cpsr & BIT(5)) && armCond == 0;
            const bool armPure = !(cpsr & BIT(5)) && armCond == 1 && mpuPureArm(pureOp);
            if (thumbPure || armPure || falseArm) {
                pipeline[0] = pipeline[1];
                if (cpsr & BIT(5)) {
                    pipeline[1] = (((*registers[15] += 2) & 0xFFE) && pcData) ? U8TO16(pcData += 2, 0) : getOpcode16();
                    ++g_mpuPureFastStats[1];
#if NOODS_MPU_PURE_JIT && NOODS_JIT && NOODS_JIT_BLOCKS
                    if (Settings::armJit) return ArmPpcJit::executeThumb(this, pureOp, remIndep, remShared);
#endif
                    return (this->*thumbInstrs[(pureOp >> 6) & 0x3FF])(pureOp);
                }
                pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();
                if (falseArm) { ++g_mpuPureFastStats[2]; return 1; }
                ++g_mpuPureFastStats[0];
#if NOODS_MPU_PURE_JIT && NOODS_JIT && NOODS_JIT_BLOCKS
                if (Settings::armJit) return ArmPpcJit::executeArm(this, pureOp, remIndep, remShared);
#endif
                return (this->*armInstrs[((pureOp >> 16) & 0xFF0) | ((pureOp >> 4) & 0xF)])(pureOp);
            }
#endif
#if NOODS_STRICT_SINGLE_PRECHECK
            // Preflight a single immediate-offset ARM load/store. The handler
            // performs exactly one data access at this address, so if that
            // access is permitted there is no possible data abort to roll
            // back. Keep PC operands, writeback, register offsets and all
            // other instruction classes on the original snapshot path.
            // The address passed to checkAccessPage matches Memory::read/write
            // before their alignment behavior, including unsigned wraparound.
            const uint32_t strictOp = pipeline[0];
            if (!(cpsr & BIT(5)) && condition[((strictOp >> 24) & 0xF0) | (cpsr >> 28)] == 1 &&
                (strictOp & 0x0F200000u) == 0x05000000u &&
                ((strictOp >> 16) & 15u) != 15u && ((strictOp >> 12) & 15u) != 15u) {
                const uint32_t base = *registers[(strictOp >> 16) & 15u];
                const uint32_t imm = strictOp & 0xFFFu;
                const uint32_t address = (strictOp & BIT(23)) ? base + imm : base - imm;
                // Unmapped accesses can invoke MMIO and nested DMA operations;
                // keep those on the original checked path as well.
                const bool isWrite = !(strictOp & BIT(20));
                const uint32_t page = address >> 12;
                uint8_t *const dataPage = isWrite ? core->memory.writeMap9A[page] : core->memory.readMap9A[page];
                if (dataPage && core->cp15.checkAccessPage(address, isWrite, true, priv)) {
                    // A mapped, word-sized read has no MMIO effects. When the
                    // next opcode is also a direct pointer fetch, no mapping
                    // can change between the permission check and this load.
                    // Preserve Memory::read's aligned-word and ARM rotation rules.
                    const uint8_t *const data = dataPage + (address & 0xFFCu);
                    if (!isWrite && !(strictOp & BIT(22)) && !core->memory.mpuDataCheckActive &&
                        pcData && ((*registers[15] + 4u) & 0xFFCu) && !((uintptr_t)data & 3u)) {
                        pipeline[0] = pipeline[1];
                        *registers[15] += 4;
                        pipeline[1] = U8TO32(pcData += 4, 0);
                        uint32_t value;
                        __builtin_memcpy(&value, data, 4);
#ifdef ENDIAN_BIG
                        value = __builtin_bswap32(value);
#endif
                        const unsigned shift = (address & 3u) * 8u;
                        if (shift) value = (value >> shift) | (value << (32u - shift));
                        *registers[(strictOp >> 12) & 15u] = value;
                        return 1;
                    }
                    pipeline[0] = pipeline[1];
                    pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();
                    return (this->*armInstrs[((strictOp >> 16) & 0xFF0) | ((strictOp >> 4) & 0xF)])(strictOp);
                }
            }
#endif
#if NOODS_STRICT_BRANCH_PRECHECK
            // The checked interpreter's branch refill performs a data-checked
            // read at its target. Only bypass rollback if the target is mapped,
            // both opcodes fit in the page, and that same data read is allowed.
            // For all other cases preserve the checked refill and abort path.
            const uint32_t brOp = pipeline[0];
            if (!(cpsr & BIT(5)) && condition[((brOp >> 24) & 0xF0) | (cpsr >> 28)] == 1) {
                const bool immBranch = (brOp & 0x0E000000u) == 0x0A000000u;
                const bool regBranch = (brOp & 0x0FFFFFF0u) == 0x012FFF10u && (brOp & 15u) != 15u;
                if (immBranch || regBranch) {
                    const uint32_t target = immBranch ? (*registers[15] + 4u + uint32_t(int32_t(brOp << 8) >> 6)) : *registers[brOp & 15u];
                    const bool thumbTarget = regBranch && (target & 1u);
                    const uint32_t alignedTarget = target & (thumbTarget ? ~1u : ~3u);
                    const uint32_t offset = alignedTarget & 0xFFFu;
                    uint8_t *const targetPage = core->memory.readMap9A[alignedTarget >> 12];
                    if (offset <= (thumbTarget ? 0xFFCu : 0xFF8u) && targetPage &&
                        core->cp15.checkAccessPage(alignedTarget, false, true, priv)) {
                        // With direct source and target fetches, the sequential
                        // pipeline read that the branch discards has no side
                        // effects. Refill exactly as flushPipeline would.
                        if (!core->gbaMode && !core->memory.mpuDataCheckActive && pcData &&
                            ((*registers[15] + 4u) & 0xFFCu)) {
                            if (immBranch && (brOp & BIT(24))) *registers[14] = *registers[15];
                            if (thumbTarget) cpsr |= BIT(5);
                            uint8_t *const targetData = targetPage + offset;
                            if (thumbTarget) {
                                *registers[15] = alignedTarget + 2;
                                pipeline[0] = U8TO16(targetData, 0);
                                pipeline[1] = U8TO16(targetData, 2);
                                pcData = targetData + 2;
                            } else {
                                *registers[15] = alignedTarget + 4;
                                pipeline[0] = U8TO32(targetData, 0);
                                pipeline[1] = U8TO32(targetData, 4);
                                pcData = targetData + 4;
                            }
                            return 3;
                        }
                        pipeline[0] = pipeline[1];
                        pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();
                        return (this->*armInstrs[((brOp >> 16) & 0xFF0) | ((brOp >> 4) & 0xF)])(brOp);
                    }
                }
            }
#endif
            uint32_t savedRegs[15];
            for (int i = 0; i < 15; i++)
                savedRegs[i] = *registers[i];

            uint32_t opcode = pipeline[0];
            pipeline[0] = pipeline[1];
            int cyc;
            if (cpsr & BIT(5)) {
                pipeline[1] = (((*registers[15] += 2) & 0xFFE) && pcData) ? U8TO16(pcData += 2, 0) : getOpcode16();
                core->memory.mpuDataCheckActive = true;
                dataAbort = false;
                cyc = (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
                core->memory.mpuDataCheckActive = false;
            }
            else {
                pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();
                switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
                    case 0: cyc = 1; break;
                    case 2:
                        core->memory.mpuDataCheckActive = true; dataAbort = false;
                        cyc = handleReserved(opcode);
                        core->memory.mpuDataCheckActive = false;
                        break;
                    default:
                        core->memory.mpuDataCheckActive = true; dataAbort = false;
                        cyc = (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
                        core->memory.mpuDataCheckActive = false;
                        break;
                }
            }
            if (dataAbort) {
                dataAbort = false;
                for (int i = 0; i < 15; i++)
                    *registers[i] = savedRegs[i];
                return takeAbort(0x10, faultPc + 8);
            }
            return cyc;
        }
    }

    // Push the next opcode through the pipeline
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];

    int cyc;
    // Execute an instruction
    if (cpsr & BIT(5)) { // THUMB mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((*registers[15] += 2) & 0xFFE) && pcData) ? U8TO16(pcData += 2, 0) : getOpcode16();

        // Execute a THUMB instruction
#if NOODS_JIT
        cyc = Settings::armJit ? ArmPpcJit::executeThumb(this, opcode, remIndep, remShared)
                               : (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
#else
        cyc = (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
#endif
    }
    else { // ARM mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();

        // Execute an ARM instruction based on its condition
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
            case 0: cyc = 1; break; // False
            case 2: cyc = handleReserved(opcode); break; // Reserved
#if NOODS_JIT
            default: cyc = Settings::armJit ? ArmPpcJit::executeArm(this, opcode, remIndep, remShared)
                                            : (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode); break;
#else
            default: cyc = (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode); break;
#endif
        }
    }
    if (arm7 && core->gbaMode)
        core->memory.clearGbaDmaOpenBus();
    return cyc;
}

void Interpreter::advanceGbaCycles(int c) {
    cycles = (core->globalCycles += c);
}

void Interpreter::stallGbaDmaCycles(uint32_t c) {
    if (!halted) {
        cycles = std::max(cycles, core->globalCycles) + c;
    }
}

#if NOODS_GBA_ARM_BURST >= 3 || NOODS_GBA_DEDICATED
__attribute__((noinline))
#endif
void Interpreter::runGbaOpcode() {
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];
    uint16_t waitCnt = core->memory.getWaitCnt();
    bool thumb = (cpsr & BIT(5)) != 0;

    // 1. Instruction fetch phase (pipeline[1])
    if (thumb) {
        pipeline[1] = (((*registers[15] += 2) & 0xFFE) && pcData) ? U8TO16(pcData += 2, 0) : getOpcode16();
#if NOODS_GBA_FETCH_FAST
        int fFast = core->gbaBus.fetchThumbRomSeq(*registers[15], waitCnt);
        int fCyc = (fFast >= 0) ? fFast : core->gbaBus.fetchOpcode(*registers[15], false, waitCnt);
#else
        int fCyc = core->gbaBus.fetchOpcode(*registers[15], false, waitCnt);
#endif
        cycles = (core->globalCycles += fCyc);
    }
    else {
        pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();
        int fCyc = core->gbaBus.fetchOpcode(*registers[15], true, waitCnt);
        cycles = (core->globalCycles += fCyc);
    }

    bool usedDuty = core->gbaBus.lastFetchUsedDuty;

    // 2. Allow any pending GBA DMA transfer ready by end of fetch to preempt bus before data phase,
    //    even if a scanline/audio task sits earlier in core->events!
    if (!halted && core->events.frontCycles <= cycles) {
        for (size_t idx = 0; idx < core->events.size() && core->events[idx].cycles <= cycles; ) {
            SchedTask t = core->events[idx].task;
            if (t >= DMA7_TRANSFER0 && t <= DMA7_TRANSFER3) {
                core->events.eraseAt(idx);
                core->globalCycles = cycles;
                core->tasks[t]();
                core->globalCycles = cycles;
                int sWait = GbaBus::romWaitS(*registers[15], waitCnt);
                if (usedDuty && ((core->dma[1].firstPakWasWrite && sWait == 3) ||
                                 (core->dma[1].firstPakWasRead && !(cpsr & BIT(5)) && sWait == 2))) {
                    cycles = (core->globalCycles += 1);
                    usedDuty = false;
                }
                idx = 0; // Restart scan in case higher-priority DMA became ready
                continue;
            }
            idx++;
        }
        waitCnt = core->memory.getWaitCnt();
    }

    // 3. Execute instruction (data accesses update cycles directly on bus read/write!)
    core->gbaBus.beginInstruction();
    core->memory.gbaCpuExecActive = true;
    int baseCyc;
    if (thumb) {
        baseCyc = (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
    }
    else {
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
            case 0: baseCyc = 1; break;
            case 2: baseCyc = handleReserved(opcode); break;
            default: baseCyc = (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode); break;
        }
    }
    core->memory.gbaCpuExecActive = false;
    if (halted) {
        core->memory.clearGbaDmaOpenBus();
        return;
    }
    waitCnt = core->memory.getWaitCnt();

    // 4. Trailing internal cycles (1I after loads, multiply/shift internal ticks)
    if (core->gbaBus.branched) {
        // Note: if LDR PC / POP {PC} loaded from memory before branching, add 1I
        if (core->gbaBus.hadLoad) {
            int iCyc = core->gbaBus.stepInternal(1, waitCnt);
            cycles = (core->globalCycles += iCyc);
        }
    }
    else if (core->gbaBus.hadDataAccess) {
        if (core->gbaBus.hadLoad) {
            int iCyc = core->gbaBus.stepInternal(1, waitCnt);
            cycles = (core->globalCycles += iCyc);
        }
    }
    else {
        int internal = baseCyc - 1;
        if (internal > 0) {
            int iCyc = core->gbaBus.stepInternal(internal, waitCnt);
            cycles = (core->globalCycles += iCyc);
        }
    }

    core->memory.clearGbaDmaOpenBus();
}

uint16_t Interpreter::getOpcode16() {
    // Set the opcode pointer or fall back to a regular 16-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[*registers[15] >> 12])) {
        bool prev = core->memory.gbaCpuExecActive;
        core->memory.gbaCpuExecActive = false;
        uint16_t op = core->memory.read<uint16_t>(arm7, *registers[15]);
        core->memory.gbaCpuExecActive = prev;
        return op;
    }
    pcData += (*registers[15] & 0xFFE);
    return U8TO16(pcData, 0);
}

uint32_t Interpreter::getOpcode32() {
    // Set the opcode pointer or fall back to a regular 32-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[*registers[15] >> 12])) {
        bool prev = core->memory.gbaCpuExecActive;
        core->memory.gbaCpuExecActive = false;
        uint32_t op = core->memory.read<uint32_t>(arm7, *registers[15]);
        core->memory.gbaCpuExecActive = prev;
        return op;
    }
    pcData += (*registers[15] & 0xFFC);
    return U8TO32(pcData, 0);
}

void Interpreter::halt(int bit) {
    // Set a halt bit and disable the CPU if newly halted
    bool before = halted;
    halted |= BIT(bit);
    if (before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0xFFFFFFFF;
}

void Interpreter::unhalt(int bit) {
    // Clear a halt bit and enable the CPU if newly halted
    bool before = halted;
    halted &= ~BIT(bit);
    if (!before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0;
}

void Interpreter::sendInterrupt(int bit) {
    // Set the interrupt's request bit
    irf |= BIT(bit);

    // Trigger an interrupt if the conditions are met, or unhalt the CPU even if interrupts are disabled
    // The ARM9 additionally needs IME to be set for it to unhalt, but the ARM7 doesn't care
    if (ie & irf) {
        if (ime && !(cpsr & BIT(7)))
            core->schedule(SchedTask(ARM9_INTERRUPT + arm7), core->gbaMode ? 3 : ((arm7 && !core->gbaMode) + 1));
        else if (ime || arm7)
            unhalt(0);
    }
}

void Interpreter::interrupt() {
    // Trigger an interrupt and unhalt the CPU if the conditions still hold
    if (ime && (ie & irf) && !(cpsr & BIT(7))) {
        uint32_t irqStart = halted ? core->globalCycles : std::max(core->globalCycles, cycles);
        int cyc = exception(0x18);
        unhalt(0);
        if (core->gbaMode)
            cycles = (core->globalCycles = irqStart + cyc);
    }
}

int Interpreter::takeAbort(uint8_t vector, uint32_t lrVal) {
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x17, true); // ARM, IRQ off, Abort mode
    *registers[14] = lrVal;
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) + vector;
    flushPipeline();
    return 3;
}

int Interpreter::exception(uint8_t vector) {
    // Forward the call to HLE BIOS if enabled, unless on ARM9 with the exception address changed
    if (bios && (arm7 || core->cp15.exceptionAddr))
        return bios->execute(vector, registers);

    // Switch the CPU mode, save the return address, and jump to the exception vector
    static const uint8_t modes[] = { 0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11 };
    setCpsr((cpsr & ~0x3F) | BIT(7) | modes[vector >> 2], true); // ARM, interrupts off, new mode
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) >> 4);
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) + vector;
    flushPipeline();
    return 3;
}

int Interpreter::undefinedException(bool thumb) {
    // Match Dust's ARM7/ARM9 undefined-instruction entry: preserve the old CPSR
    // in the Undefined SPSR bank, clear Thumb state, disable IRQs, and branch to +4.
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x1B, true);
    *registers[14] = *registers[15] - (thumb ? 2 : 4);
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) | 0x04;
    flushPipeline();
    return 3;
}

void Interpreter::flushPipeline() {
    // v1.118: the usual case - a DS CPU jumping to code in a mapped page, with both pipeline
    // words in that page - is served from one page-table lookup. It leaves exactly the state
    // the general code below does (R15, pipeline, pcData).
    if (__builtin_expect(!core->gbaMode && !core->memory.mpuDataCheckActive, 1)) {
        uint8_t *const page = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[*registers[15] >> 12];
        if (__builtin_expect(page != nullptr, 1)) {
            if (cpsr & BIT(5)) {
                const uint32_t targetPc = *registers[15] & ~0x1u;
                if (__builtin_expect((targetPc & 0xFFE) != 0xFFE, 1)) {
                    const uint8_t *p = page + (targetPc & 0xFFE);
                    *registers[15] = targetPc + 2;
                    pipeline[0] = U8TO16(p, 0);
                    pipeline[1] = U8TO16(p, 2);
                    pcData = const_cast<uint8_t*>(p) + 2;
                    return;
                }
            } else {
                const uint32_t targetPc = *registers[15] & ~0x3u;
                if (__builtin_expect((targetPc & 0xFFC) != 0xFFC, 1)) {
                    const uint8_t *p = page + (targetPc & 0xFFC);
                    *registers[15] = targetPc + 4;
                    pipeline[0] = U8TO32(p, 0);
                    pipeline[1] = U8TO32(p, 4);
                    pcData = const_cast<uint8_t*>(p) + 4;
                    return;
                }
            }
        }
    }
    bool prevExec = core->memory.gbaCpuExecActive;
    core->memory.gbaCpuExecActive = false;
    uint32_t targetPc;
    // Adjust the program counter and refill the pipeline after a jump
    if (cpsr & BIT(5)) { // THUMB mode
        targetPc = *registers[15] & ~0x1u;
        *registers[15] = targetPc + 2;
        pipeline[0] = core->memory.read<uint16_t>(arm7, targetPc);
        pipeline[1] = getOpcode16();
    }
    else { // ARM mode
        targetPc = *registers[15] & ~0x3u;
        *registers[15] = targetPc + 4;
        pipeline[0] = core->memory.read<uint32_t>(arm7, targetPc);
        pipeline[1] = getOpcode32();
    }
    core->memory.gbaCpuExecActive = prevExec;
    if (prevExec && arm7 && core->gbaMode) {
        int refill = core->gbaBus.branchRefill(targetPc, !(cpsr & BIT(5)), core->memory.getWaitCnt());
        cycles = (core->globalCycles += refill);
    }
}

void Interpreter::swapRegisters(uint32_t value) {
    // Swap banked registers based on a CPU mode value
    switch (value & 0x0F) {
    case 0x00: // User
    case 0x04: // Reserved: System register bank, Abort SPSR bank
    case 0x05:
    case 0x06:
    case 0x08: // Reserved: System register bank, Undefined SPSR bank
    case 0x09:
    case 0x0A:
    case 0x0C: // Reserved and System modes share the System register bank
    case 0x0D:
    case 0x0E:
    case 0x0F: // System
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUsr[13];
        registers[14] = &registersUsr[14];
        if ((value & 0x0F) >= 0x04 && (value & 0x0F) <= 0x06)
            spsr = &spsrAbt;
        else if ((value & 0x0F) >= 0x08 && (value & 0x0F) <= 0x0A)
            spsr = &spsrUnd;
        else
            spsr = nullptr;
        break;

    case 0x01: // FIQ (mode field bits 3:0)
        registers[8] = &registersFiq[0];
        registers[9] = &registersFiq[1];
        registers[10] = &registersFiq[2];
        registers[11] = &registersFiq[3];
        registers[12] = &registersFiq[4];
        registers[13] = &registersFiq[5];
        registers[14] = &registersFiq[6];
        spsr = &spsrFiq;
        break;

    case 0x02: // IRQ (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersIrq[0];
        registers[14] = &registersIrq[1];
        spsr = &spsrIrq;
        break;

    case 0x03: // Supervisor (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersSvc[0];
        registers[14] = &registersSvc[1];
        spsr = &spsrSvc;
        break;

    case 0x07: // Abort (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersAbt[0];
        registers[14] = &registersAbt[1];
        spsr = &spsrAbt;
        break;

    case 0x0B: // Undefined (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUnd[0];
        registers[14] = &registersUnd[1];
        spsr = &spsrUnd;
        break;

    default:
        LOG_CRIT("Unknown ARM%d CPU mode: 0x%X\n", arm7 ? 7 : 9, value & 0x0F);
        break;
    }
}

void Interpreter::setCpsr(uint32_t value, bool save) {
    // Update registers if the CPU mode changed
    if ((value & 0x0F) != (cpsr & 0x0F))
        swapRegisters(value);

    // Set the CPSR, saving the old value if requested
    if (save && spsr) *spsr = cpsr;
    cpsr = value;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

int Interpreter::handleReserved(uint32_t opcode) {
    // The ARM9-exclusive BLX instruction uses the reserved condition code, so let it run
    if ((opcode & 0xE000000) == 0xA000000)
        return blx(opcode); // BLX label

    // If the special HLE BIOS opcode was jumped to, return from an HLE interrupt
    if (bios && opcode == 0xFF000000)
        return finishHleIrq();

    // If a DLDI function was jumped to, HLE it and return
    if (core->dldi.isPatched()) {
        uint32_t **r = registers;
        switch (opcode) {
            case DLDI_START: *r[0] = core->dldi.startup(); break;
            case DLDI_INSERT: *r[0] = core->dldi.isInserted(); break;
            case DLDI_READ: *r[0] = core->dldi.readSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_WRITE: *r[0] = core->dldi.writeSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_CLEAR: *r[0] = core->dldi.clearStatus(); break;
            case DLDI_STOP: *r[0] = core->dldi.shutdown(); break;
        }
        return bx(14);
    }

    // Treat anything else as an unknown opcode
    return unkArm(opcode);
}

int Interpreter::handleHleIrq() {
    // Switch to IRQ mode, save the return address, and push registers to the stack
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x12, true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) ? 2 : 0);
    stmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));

    // Set the return address to the special HLE BIOS opcode amd jump to the interrupt handler
    *registers[14] = arm7 ? 0x00000000 : 0xFFFF0000;
    *registers[15] = core->memory.read<uint32_t>(arm7, arm7 ? 0x3FFFFFC : (core->cp15.dtcmAddr + 0x3FFC));
    flushPipeline();
    return core->gbaMode ? 21 : 3;
}

int Interpreter::finishHleIrq() {
    bool wasIntrWait = bios->shouldCheck();
    // Update the wait flags if in the middle of an HLE IntrWait function
    if (wasIntrWait)
        bios->checkWaitFlags();

    // Pop registers from the stack, jump to the return address, and restore the mode
    ldmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[15] = *registers[14] - 4;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    if (core->gbaMode) {
        core->memory.setGbaBiosOpenBus(0xE25EF004u);
        if (wasIntrWait && !bios->shouldCheck()) {
            // Returning out of SWI IntrWait / VBlankIntrWait BIOS body
            inGbaHaltSwi = false;
            advanceGbaCycles(42);
        }
        else if (inGbaHaltSwi) {
            // Returning out of SWI Halt BIOS body
            inGbaHaltSwi = false;
            advanceGbaCycles(23);
        }
    }
    return 3;
}

int Interpreter::unkArm(uint32_t opcode) {
    // Unknown ARM encodings enter Undefined mode rather than acting as NOPs.
    (void)opcode;
    return undefinedException(false);
}

int Interpreter::unkThumb(uint16_t opcode) {
    // Unknown Thumb encodings enter the same Undefined vector with a Thumb LR offset.
    (void)opcode;
    return undefinedException(true);
}

void Interpreter::writeIme(uint8_t value) {
    // Write to the IME register
    ime = value & 0x01;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), core->gbaMode ? 3 : ((arm7 && !core->gbaMode) + 1));
}

void Interpreter::writeIe(uint32_t mask, uint32_t value) {
    // Write to the IE register
    mask &= (arm7 ? (core->gbaMode ? 0x3FFF : 0x01FF3FFF) : 0x003F3F7F);
    ie = (ie & ~mask) | (value & mask);

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), core->gbaMode ? 3 : ((arm7 && !core->gbaMode) + 1));
}

void Interpreter::writeIrf(uint32_t mask, uint32_t value) {
    // Write to the IF register
    // Setting a bit actually clears it to acknowledge an interrupt
    irf &= ~(value & mask);
}

void Interpreter::writePostFlg(uint8_t value) {
    // Write to the POSTFLG register
    // The first bit can be set, but never cleared
    // For some reason, the second bit is writable on the ARM9
    postFlg |= value & 0x01;
    if (!arm7) postFlg = (postFlg & ~0x02) | (value & 0x02);
}
