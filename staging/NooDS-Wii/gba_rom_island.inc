// v1p170: Dedicated exact ARM7/GamePak-ROM execution island.
//
// Runs sequential-ish ARM code fetched from the cartridge ROM (regions 0x08-0x0D)
// in a tight loop while charging cycles through the *real* GbaBus model
// (fetchOpcode/branchRefill/dataAccess/stepInternal), so timing is identical to
// runGbaOpcode instruction-for-instruction. Unlike runGbaDedicated, no costs are
// hard-coded: the prefetch buffer state machine is exercised for every fetch,
// which keeps DMA-prefetch and H-blank observable behaviour unchanged.
//
// Supported encodings (everything else falls back to the interpreter):
//  * any false-condition instruction (fetch only, no side effects)
//  * data-processing via GbaArmAlu::eligible / ArmAluSpecialized (no PC dest)
//  * B / BL with a mapped target (pipeline reads via readMap7)
//  * MUL / MLA without the S bit (Rd != PC), exact signedMulTicks charging
//  * LDR / LDRB / STR / STRB, P=1 W=0 (offset forms, no writeback),
//    Rd != PC, register offset only with a zero shift field (bits 11-4),
//    loads from mapped {EWRAM,IWRAM,pal,VRAM,OAM,ROM}, stores to
//    {EWRAM,IWRAM,pal,VRAM,OAM,ROM} through the full Memory::write path.
// The loop breaks whenever an event comes within 96 cycles (worst-case handled
// instruction cost is 45), so mid-instruction DMA preemption stays exclusively
// in runGbaOpcode. IO, halfword/signed transfers, LDM/STM, SWI, BX, Thumb and
// unmapped accesses never cross the island.
#if NOODS_GBA_ISLAND_STATS
uint32_t g_islEntries=0,g_islFalseEntry=0,g_islInstrs=0;
uint32_t g_islBrk[8]={0}; // 0 cond2,1 page,2 region,3 margin,4 unhandled,5 unmapped,6 gpio,7 (spare)
uint32_t g_islCat[6]={0}; // alu,false,mul,branch,xfer,block
uint32_t g_islWhy[6]={0}; // thumb,notrom,unaligned,nopc,desync,margin
#endif
__attribute__((noinline)) bool Interpreter::runGbaRomIsland() {
    Core &c = *core;
    GbaBus &bus = c.gbaBus;
    uint32_t r15 = *registers[15];
    const uint32_t region0 = r15 >> 24;
    if ((cpsr & 0x20u) || region0 < 8u || region0 > 0xDu || (r15 & 3) || !pcData ||
        c.globalCycles != cycles || (int32_t)(c.events.frontCycles - cycles) <= 20) {
#if NOODS_GBA_ISLAND_STATS
        ++g_islFalseEntry;
        if (cpsr & 0x20u) ++g_islWhy[0];            // thumb
        else if (region0 < 8u || region0 > 0xDu) ++g_islWhy[1]; // not ROM
        else if (r15 & 3) ++g_islWhy[2];           // unaligned
        else if (!pcData) ++g_islWhy[3];           // no pc mirror
        else if (c.globalCycles != cycles) ++g_islWhy[4]; // cycle desync
        else ++g_islWhy[5];                        // event margin
#endif
        return false;
    }

    auto regs = registers;
    uint8_t **readMap = c.memory.readMap7;
    if (!readMap[r15 >> 12]) {
#if NOODS_GBA_ISLAND_STATS
        ++g_islFalseEntry;
#endif
        return false;
    }

    uint32_t clock = cycles;
    uint32_t op = pipeline[0], next = pipeline[1];
    uint8_t *pc = pcData;
    unsigned done = 0;

    while ((r15 & 0xFFFu) != 0xFFCu) {
        // Stay on GamePak ROM as the execution source; the interpreter continues
        // identically from the island's exit state (pcData preserved) if not.
        const uint32_t pcRegion = (r15 - 4u) >> 24;
        if (pcRegion < 8u || pcRegion > 0xDu) {
#if NOODS_GBA_ISLAND_STATS
            ++g_islBrk[2];
#endif
            break;
        }
        const uint16_t waitCnt = c.memory.getWaitCnt();
        const unsigned cond = condition[((op >> 24) & 0xF0u) | (cpsr >> 28)];
        if (cond == 2) {
#if NOODS_GBA_ISLAND_STATS
            ++g_islBrk[0];
#endif
            break;
        }

        // ---- classify (cond true only) ----
        bool alu = false, mulOp = false, branch = false, xfer = false, block = false;
        bool link = false, load = false, byte = false, mla = false;
        uint32_t target = 0, address = 0;
        unsigned blkM = 0;
        uint32_t blkList = 0, blkLow = 0;
        if (cond) {
            if (GbaArmAlu::eligible(op)) {
                alu = true;
            }
#if !defined(ROMISL_NO_BRANCH)
            else if ((op & 0x0E000000u) == 0x0A000000u) {
                // B / BL: at handler time r15 is PC+8; here it is still PC+4.
                target = (r15 + 4u + (uint32_t)((int32_t)(op << 8) >> 6)) & ~3u;
                if (!readMap[target >> 12] || !readMap[(target + 4u) >> 12])
                    break;
                link = (op & 0x01000000u) != 0;
                branch = true;
            }
#endif
#if !defined(ROMISL_NO_MUL)
            else if ((op & 0x0FC000F0u) == 0x00000090u && !(op & 0x00100000u) &&
                     ((op >> 16) & 0xF) != 15) {
                // MUL / MLA without flags; Rd is bits 16-19 on ARM7TDMI.
                // Rs/Rm values (and the tick count) are read after the pipeline
                // advance so a PC operand sees PC+8 like the interpreter does.
                mla = (op & 0x00200000u) != 0;
                mulOp = true;
            }
#endif
#if !defined(ROMISL_NO_XFER)
            else if ((op & 0x0D200000u) == 0x05000000u && ((op >> 12) & 0xF) != 15) {
                // LDR(B)/STR(B), P=1 W=0, immediate or plain register offset
                uint32_t offset;
                if (op & 0x02000000u) {
                    if (op & 0x0FF0u)
                        break; // shifted register offset: stay on the interpreter
                    offset = ((op & 0xF) == 15) ? (r15 + 4u) : *regs[op & 0xF];
                }
                else {
                    offset = op & 0xFFFu;
                }
                const uint32_t base = ((op >> 16) & 0xF) == 15 ? (r15 + 4u) : *regs[(op >> 16) & 0xF];
                address = (op & 0x00800000u) ? (base + offset) : (base - offset);
                load = (op & 0x00100000u) != 0;
                byte = (op & 0x00400000u) != 0;
                const uint32_t region = address >> 24;
                const bool okRegion = region == 2 || region == 3 || region == 5 ||
                                      region == 6 || region == 7 ||
                                      (region >= 8 && region <= 0xD);
                if (!okRegion)
                    break;
                if (load) {
                    if (!readMap[address >> 12])
                        break;
                }
                else if (address >= 0x80000C4u && address < 0x80000CAu) {
                    break; // GPIO: stay on the interpreter
                }
                xfer = true;
            }
#endif
#if !defined(ROMISL_NO_BLOCK)
            else if ((op & 0x0E000000u) == 0x08000000u && !(op & 0x00400000u) &&
                     (op & 0xFFFFu) != 0 && !(op & 0x8000u) && ((op >> 16) & 0xF) != 15) {
                // LDM/STM without S, non-empty list, no PC in list, Rn != PC
                const unsigned rn = (op >> 16) & 0xF;
                const uint32_t base = *regs[rn];
                blkM = bitCount[op & 0xFF] + bitCount[(op >> 8) & 0xFF];
                blkList = op & 0xFFFFu;
                const bool up = (op & 0x00800000u) != 0;
                const bool pre = (op & 0x01000000u) != 0;
                load = (op & 0x00100000u) != 0;
                blkLow = up ? base : (base - (blkM << 2));
                address = up ? (pre ? base + 4u : base) : (pre ? blkLow : blkLow + 4u);
                // every transfer address must be in a handled region (and mapped
                // for loads); transfers ascend by 4
                bool ok = true;
                uint32_t a = address;
                for (unsigned k = 0; k < blkM; k++, a += 4u) {
                    const uint32_t region = a >> 24;
                    const bool okRegion = region == 2 || region == 3 || region == 5 ||
                                          region == 6 || region == 7 ||
                                          (region >= 8 && region <= 0xD);
                    if (!okRegion || (load && !readMap[a >> 12]) ||
                        (!load && a >= 0x80000C4u && a < 0x80000CAu)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
                block = true;
            }
#endif
            else {
#if NOODS_GBA_ISLAND_STATS
                ++g_islBrk[4];
#endif
                break;
            }
        }

        // ---- event safety margin ----
        // Exact worst-case costs per category keep the margin tight so more
        // instructions stay in the island near events, while guaranteeing an
        // event can never come due inside an island instruction (mid-
        // instruction DMA preemption stays in runGbaOpcode).
        //   fetch (ARM, ROM, worst)      = 9+9 = 18
        //   ALU / false condition        = 18
        //   MUL/MLA                      = 18 + 5 internal
        //   LDR/STR                      = 18 + (1 stall + 9N + 9S) + 1I = 38
        //   B/BL                         = 18 + branchRefill(18+18) = 54
        //   LDM/STM                      = 18 + m*19 + 1I
        int32_t need = 20;
        if (block)
            need = (int32_t)(21 + blkM * 19);
        else if (branch)
            need = 56;
        else if (xfer)
            need = 40;
        else if (mulOp)
            need = 25;
        if ((int32_t)(c.events.frontCycles - clock) <= need) {
#if NOODS_GBA_ISLAND_STATS
            ++g_islBrk[3];
#endif
            break;
        }

        // ---- 1. fetch phase: advance pipeline, charge the real prefetch model ----
        const uint32_t executing = op;
        r15 += 4;
        *regs[15] = r15;
        op = next;
        pc += 4;
        next = U8TO32(pc, 0);
        clock += bus.fetchOpcode(r15, true, waitCnt);
        cycles = (c.globalCycles = clock);

        // Per-instruction bus flags are reset exactly where runGbaOpcode does it
        // (step 3, after the fetch): the first data access of every instruction
        // must see inSeqAccess == false and charge non-sequential timing.
        bus.beginInstruction();

        // ---- 2. execute + data-phase charging ----
        if (cond) {
            if (alu) {
                ArmAluSpecialized::execute(regs, cpsr, executing);
            }
            else if (mulOp) {
                // Read operands after the advance: registers[15] is PC+8 here,
                // exactly like the interpreter's handler environment.
                const uint32_t rm = *regs[executing & 0xF];
                const uint32_t rs = *regs[(executing >> 8) & 0xF];
                const int mulTicks = Arm7TdmiMul::signedMulTicks(rs);
                uint32_t result = rm * rs;
                if (mla)
                    result += *regs[(executing >> 12) & 0xF];
                *regs[(executing >> 16) & 0xF] = result;
                const int internal = mulTicks + (mla ? 2 : 1) - 1;
                if (internal > 0)
                    clock += bus.stepInternal(internal, waitCnt);
                cycles = (c.globalCycles = clock);
            }
            else if (branch) {
                if (link)
                    *regs[14] = r15 - 4u; // PC+8 at handler time, minus 4
                uint8_t *base0 = readMap[target >> 12];
                uint8_t *base1 = readMap[(target + 4u) >> 12];
                r15 = target + 4u;
                *regs[15] = r15;
                op = U8TO32(base0 + (target & 0xFFFu), 0);
                next = U8TO32(base1 + ((target + 4u) & 0xFFFu), 0);
                pc = base1 + ((target + 4u) & 0xFFFu);
                clock += bus.branchRefill(target, true, waitCnt);
                cycles = (c.globalCycles = clock);
            }
            else if (xfer) {
                if (load) {
                    // Charge exactly like Memory::gbaDataAccess, then read the
                    // mapped page exactly like the Memory::read fast path.
                    int cyc;
                    const uint32_t region = address >> 24;
                    if (!bus.active && region < 8 && ((0x9Bu >> region) & 1)) {
                        bus.nextSeq = false;
                        bus.inSeqAccess = true;
                        bus.hadDataAccess = true;
                        bus.hadLoad = true;
                        cyc = 1;
                    }
                    else {
                        cyc = bus.dataAccess(address, byte ? 1 : 4, bus.inSeqAccess, waitCnt, false);
                        bus.inSeqAccess = true;
                        bus.hadDataAccess = true;
                        bus.hadLoad = true;
                    }
                    clock += cyc;
                    cycles = (c.globalCycles = clock);
                    const uint8_t *data = readMap[address >> 12];
                    uint32_t value = byte ? data[address & 0xFFFu] : U8TO32(data, address & 0xFFCu);
                    if (!byte && (address & 3))
                        value = GbaArmAlu::ror(value, (address & 3) * 8);
                    *regs[(executing >> 12) & 0xF] = value;
                    // trailing 1I after loads (runGbaOpcode step 4)
                    clock += bus.stepInternal(1, waitCnt);
                    cycles = (c.globalCycles = clock);
                }
                else {
                    // Stores run through the full write path so IO-free regions
                    // keep VRAM byte rules and exact bus charging.
                    cycles = (c.globalCycles = clock);
                    bool prev = c.memory.gbaCpuExecActive;
                    c.memory.gbaCpuExecActive = true;
                    const uint32_t value = *regs[(executing >> 12) & 0xF];
                    if (byte)
                        c.memory.write<uint8_t>(true, address, value);
                    else
                        c.memory.write<uint32_t>(true, address, value);
                    c.memory.gbaCpuExecActive = prev;
                    clock = cycles; // onGbaCpuDataAccess charged through advanceGbaCycles
                }
            }
            else { // block: LDM/STM, ARM7 semantics matching the interpreter handlers
                const unsigned rn = (executing >> 16) & 0xF;
                const bool up = (executing & 0x00800000u) != 0;
                const bool wb = (executing & 0x00200000u) != 0;
                const uint32_t finalWB = up ? (blkLow + (blkM << 2)) : blkLow;
                uint32_t a = address;
                if (!load) {
                    // ARM7: if Rn is listed and not the first listed register,
                    // the writeback value is already in Rn for the stores.
                    if (wb && (blkList & (BIT(rn + 1) - 1)) > BIT(rn))
                        *regs[rn] = finalWB;
                    bool prev = c.memory.gbaCpuExecActive;
                    c.memory.gbaCpuExecActive = true;
                    for (unsigned i = 0; i < 16; i++) {
                        if (!(blkList & BIT(i)))
                            continue;
                        cycles = (c.globalCycles = clock);
                        c.memory.write<uint32_t>(true, a, *regs[i]);
                        clock = cycles;
                        a += 4u;
                    }
                    c.memory.gbaCpuExecActive = prev;
                    if (wb)
                        *regs[rn] = finalWB;
                }
                else {
                    // ARM7: Rn is updated before the loads; a listed Rn is then
                    // overwritten by its loaded value.
                    if (wb)
                        *regs[rn] = finalWB;
                    for (unsigned i = 0; i < 16; i++) {
                        if (!(blkList & BIT(i)))
                            continue;
                        int cyc;
                        const uint32_t region = a >> 24;
                        if (!bus.active && region < 8 && ((0x9Bu >> region) & 1)) {
                            bus.nextSeq = false;
                            bus.inSeqAccess = true;
                            bus.hadDataAccess = true;
                            bus.hadLoad = true;
                            cyc = 1;
                        }
                        else {
                            cyc = bus.dataAccess(a, 4, bus.inSeqAccess, waitCnt, false);
                            bus.inSeqAccess = true;
                            bus.hadDataAccess = true;
                            bus.hadLoad = true;
                        }
                        clock += cyc;
                        cycles = (c.globalCycles = clock);
                        // No misalignment rotation here: the interpreter's LDM
                        // handlers assign Memory::read<uint32_t>'s aligned word
                        // directly, unlike LDR.
                        const uint8_t *data = readMap[a >> 12];
                        *regs[i] = U8TO32(data, a & 0xFFCu);
                        a += 4u;
                    }
                    // trailing 1I after loads (runGbaOpcode step 4)
                    clock += bus.stepInternal(1, waitCnt);
                    cycles = (c.globalCycles = clock);
                }
            }
        }
#if NOODS_GBA_ISLAND_STATS
        ++g_islInstrs;
        if (!cond) ++g_islCat[1];
        else if (alu) ++g_islCat[0];
        else if (mulOp) ++g_islCat[2];
        else if (branch) ++g_islCat[3];
        else if (xfer) ++g_islCat[4];
        else ++g_islCat[5];
#endif
        ++done;
    }

    if (!done) {
#if NOODS_GBA_ISLAND_STATS
        ++g_islBrk[5];
#endif
        return false;
    }
#if NOODS_GBA_ISLAND_STATS
    ++g_islEntries;
#endif
    pipeline[0] = op;
    pipeline[1] = next;
    pcData = pc;
    cycles = clock;
    c.globalCycles = clock;
    bus.beginInstruction();
    c.memory.clearGbaDmaOpenBus();
    return true;
}
