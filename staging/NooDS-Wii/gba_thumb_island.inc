// ============================================================================
// NooDS-Wii v1p170 — cycle-exact THUMB execution island for GamePak ROM.
//
// Most GBA games keep their hot loops in THUMB code (measured: 99.79% of the
// instructions executed by F-Zero: Maximum Velocity are THUMB), so the ARM-only
// island in gba_rom_island.inc never ran for them and only added entry cost.
// This island is the THUMB counterpart, built on the same principle:
//
//   * every fetch and data access is charged through the real GbaBus model
//     (fetchOpcode / branchRefill / dataAccess / stepInternal), so the cycle
//     counts are bit-identical to Interpreter::runGbaOpcode();
//   * only encodings whose semantics can be reproduced exactly are handled, and
//     anything else falls back to the interpreter with no state change;
//   * the loop exits before an event can come due, so mid-instruction DMA
//     preemption (runGbaOpcode step 2) stays exclusively in the interpreter.
//
// What is saved per instruction: the indirect handler-table call, repeated
// waitstate reads, the DMA preemption scan, the gbaCpuExecActive toggles and the
// branchy step-4 epilogue.
// ============================================================================

#if !defined(TISL_MAX_KIND)
#define TISL_MAX_KIND 6
#endif
#if !defined(TISL_MAX_DEPTH)
#define TISL_MAX_DEPTH 4096
#endif
#if !defined(TISL_KEEP_ISLAND)
#define TISL_KEEP_ISLAND 0
#endif
#if !defined(TISL_DMA_SCAN)
#define TISL_DMA_SCAN 0
#endif
#if !defined(TISL_SYNC_PIPELINE)
#define TISL_SYNC_PIPELINE 0
#endif
#if TISL_DMA_SCAN
static uint32_t g_tisPreempt=0, g_tisOverrun=0;
#endif

#if NOODS_GBA_ISLAND_VERIFY
// Differential self-check: run the instruction in the island, then rewind and let
// Interpreter::runGbaOpcode() run the very same instruction, and compare the full
// visible state. Any mismatch is reported with PC and opcode; after too many the
// island disables itself so the run keeps going on the interpreter.
static bool g_tisVerifyOff = false;
int g_tisVerifyFails = 0;
uint32_t g_tisFirstPc = 0, g_tisFirstOp = 0, g_tisFirstDepth = 0;
char g_tisFirstWhy[16] = {0};
uint32_t g_tisLastPc = 0, g_tisLastOp = 0;
uint32_t g_tisFirstI0=0, g_tisFirstI1=0, g_tisFirstP0=0, g_tisFirstP1=0, g_tisFirstR15=0, g_tisFirstIPc=0;
uint32_t g_tisRegIdx=99, g_tisRegIsl=0, g_tisRegRef=0, g_tisCpsrIsl=0, g_tisCpsrRef=0;
char g_tisLastWhy[16] = {0};
#define TISL_SNAPSHOT()                                                          \
    uint32_t vR[15];                                                             \
    for (unsigned vi = 0; vi < 15; vi++) vR[vi] = *regs[vi];                     \
    const uint32_t vR15 = r15, vCpsr = cpsr, vP0 = o, vP1 = next;                 \
    const uint32_t vClk = clock; uint8_t *const vPc = pc;                        \
    GbaBus vBus = bus; const uint16_t vO = o;
#define TISL_VERIFY() do {                                                       \
    uint32_t iR[15];                                                             \
    for (unsigned vi = 0; vi < 15; vi++) iR[vi] = *regs[vi];                     \
    const uint32_t iR15 = r15, iCpsr = cpsr, iP0 = op, iP1 = next;                  \
    const uint32_t iClk = clock; GbaBus iBus = bus; uint8_t *const iPc = pc;      \
    for (unsigned vi = 0; vi < 15; vi++) *regs[vi] = vR[vi];                     \
    *regs[15] = vR15; cpsr = vCpsr;                                              \
    pipeline[0] = vP0; pipeline[1] = vP1; pc = vPc; pcData = vPc; bus = vBus;    \
    r15 = vR15; clock = vClk; cycles = vClk; c.globalCycles = vClk;              \
    op = (uint16_t)vP0; next = (uint16_t)vP1;                                    \
    runGbaOpcode();                                                              \
    const char *why = nullptr;                                                   \
    for (unsigned vi = 0; vi < 15; vi++)                                         \
        if (*regs[vi] != iR[vi]) { why = "reg"; g_tisRegIdx = vi;                \
            g_tisRegIsl = iR[vi]; g_tisRegRef = *regs[vi]; break; }              \
    if (!why && cpsr != iCpsr) { why = "cpsr"; g_tisCpsrIsl = iCpsr; g_tisCpsrRef = cpsr; } \
    else if (!why) { g_tisCpsrIsl = iCpsr; g_tisCpsrRef = cpsr; }                 \
    if (!why && *regs[15] != iR15) why = "r15";                                  \
    if (!why && iClk != (uint32_t)c.globalCycles) why = "cycles";                \
    if (!why && (pipeline[0] != iP0 || pipeline[1] != iP1)) why = "pipe";        \
    if (!why && iBus.headAddr != bus.headAddr) why = "bus.head";                 \
    if (!why && iBus.tailAddr != bus.tailAddr) why = "bus.tail";                 \
    if (!why && iBus.count != bus.count) why = "bus.count";                      \
    if (!why && iBus.duty != bus.duty) why = "bus.duty";                         \
    if (!why && iBus.active != bus.active) why = "bus.active";                   \
    if (!why && iBus.nextSeq != bus.nextSeq) why = "bus.nextSeq";                \
    if (!why && iBus.inSeqAccess != bus.inSeqAccess) why = "bus.inSeq";          \
    if (!why && iBus.hadDataAccess != bus.hadDataAccess) why = "bus.hadData";    \
    if (!why && iBus.hadLoad != bus.hadLoad) why = "bus.hadLoad";                \
    if (!why && iBus.branched != bus.branched) why = "bus.branched";             \
    if (!why && iBus.lastFetchUsedDuty != bus.lastFetchUsedDuty) why = "bus.dutyUsed"; \
    if (!why && iBus.preSwiSaved != bus.preSwiSaved) why = "bus.preSwi";         \
    if (why) {                                                                   \
        if (g_tisVerifyFails == 0) {                                             \
            g_tisFirstPc = vR15 - 2u; g_tisFirstOp = vO; g_tisFirstDepth = done;  \
            g_tisFirstI0 = iP0; g_tisFirstI1 = iP1;                              \
            g_tisFirstP0 = pipeline[0]; g_tisFirstP1 = pipeline[1];              \
            g_tisFirstR15 = iR15; g_tisFirstIPc = (uint32_t)(iPc - readMap[iR15 >> 12]); \
            { unsigned k=0; while (why[k] && k < 15) { g_tisFirstWhy[k]=why[k]; ++k; } g_tisFirstWhy[k]=0; } \
        }                                                                        \
        g_tisLastPc = vR15 - 2u; g_tisLastOp = vO;                               \
        { unsigned k=0; while (why[k] && k < 15) { g_tisLastWhy[k]=why[k]; ++k; } g_tisLastWhy[k]=0; } \
        if (++g_tisVerifyFails > 60) g_tisVerifyOff = true;                      \
    }                                                                            \
    if (TISL_KEEP_ISLAND && !why) {                                              \
        /* continue from the island's own state: only difference left vs a      \
           non-verify build is that runGbaOpcode() also ran */                  \
        for (unsigned vi = 0; vi < 15; vi++) *regs[vi] = iR[vi];                 \
        *regs[15] = iR15; r15 = iR15; cpsr = iCpsr;                              \
        pipeline[0] = iP0; pipeline[1] = iP1; bus = iBus;                        \
        clock = iClk; cycles = iClk; c.globalCycles = iClk;                      \
        op = (uint16_t)iP0; next = (uint16_t)iP1; pc = iPc; pcData = iPc;        \
    } else {                                                                     \
        r15 = *regs[15]; clock = c.globalCycles; cycles = clock;                 \
        op = (uint16_t)pipeline[0]; next = (uint16_t)pipeline[1]; pc = pcData;   \
    }                                                                            \
} while (0)
#else
#define TISL_SNAPSHOT() ((void)0)
#define TISL_VERIFY() ((void)0)
#endif

__attribute__((noinline)) bool Interpreter::runGbaThumbIsland() {
    Core &c = *core;
    GbaBus &bus = c.gbaBus;
    uint32_t r15 = *registers[15];
    const uint32_t region0 = r15 >> 24;
#if NOODS_GBA_ISLAND_VERIFY
    if (g_tisVerifyOff) return false;
#endif
    if (!(cpsr & 0x20u) || region0 < 8u || region0 > 0xDu || (r15 & 1) || !pcData ||
        c.globalCycles != cycles || (int32_t)(c.events.frontCycles - cycles) <= 24) {
#if NOODS_GBA_ISLAND_STATS
        ++g_tisFalseEntry;
#endif
        return false;
    }

    auto regs = registers;
    uint8_t **readMap = c.memory.readMap7;
    if (!readMap[r15 >> 12]) {
#if NOODS_GBA_ISLAND_STATS
        ++g_tisFalseEntry;
#endif
        return false;
    }

    uint32_t clock = cycles;
    uint16_t op = (uint16_t)pipeline[0];
    uint16_t next = (uint16_t)pipeline[1];
    uint8_t *pc = pcData;
    unsigned done = 0;

    while (true) {
        // Stay on GamePak ROM as the execution source (executing address r15-2).
        if (done >= TISL_MAX_DEPTH) break;
        const uint32_t pcRegion = (r15 - 2u) >> 24;
        if (pcRegion < 8u || pcRegion > 0xDu) {
#if NOODS_GBA_ISLAND_STATS
            ++g_tisBrk[0];
#endif
            break;
        }
        uint16_t waitCnt = c.memory.getWaitCnt();

        // ---- classify ----
        // kind: 0 = unsupported, 1 = alu, 2 = mul, 3 = xfer, 4 = block, 5 = branch,
        //       6 = BL setup (no branch)
        unsigned kind = 0;
        bool load = false, byte = false, link = false;
        uint32_t address = 0, target = 0;
        unsigned m = 0, rd = 0;
        uint32_t list = 0, low = 0;
        const uint16_t o = op;
        const unsigned hi5 = o >> 11;

        if (hi5 < 3) {
            kind = 1; // LSL/LSR/ASR Rd,Rs,#imm
        }
        else if (hi5 == 3) {
            kind = 1; // ADD/SUB register and 3-bit immediate
        }
        else if (hi5 < 8) {
            kind = 1; // MOV/CMP/ADD/SUB Rd,#imm8
        }
        else if ((o & 0xFC00) == 0x4000) {
            kind = (((o >> 6) & 0xF) == 13) ? 2 : 1; // ALU ops / MUL
        }
        else if ((o & 0xF800) == 0x4800) {
            kind = 3; load = true; // LDR Rd,[PC,#imm8*4]
            // Handlers see PC+4; at classify time r15 is still PC+2.
            // Rd lives in bits 10-8 here, not bits 2-0.
            rd = (o >> 8) & 7;
            address = ((r15 + 2u) & ~3u) + (uint32_t)((o & 0xFF) << 2);
        }
        else if ((o & 0xF000) == 0x5000) {
            // STR/STRB/LDR/LDRB Rd,[Rb,Ro]; halfword and signed forms stay on the
            // interpreter (their alignment rules differ).
            const unsigned sel = (o >> 9) & 7;
            if (sel & 1) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[4];
#endif
                break;
            }
            load = (sel & 4) != 0;
            byte = (sel & 2) != 0;
            rd = o & 7;
            address = *regs[(o >> 3) & 7] + *regs[(o >> 6) & 7];
            kind = 3;
        }
        else if ((o & 0xE000) == 0x6000) {
            const unsigned sel = (o >> 11) & 3; // 00 STR 01 LDR 10 STRB 11 LDRB
            load = (sel & 1) != 0;
            byte = (sel & 2) != 0;
            const uint32_t imm = (o >> 6) & 0x1F;
            rd = o & 7;
            address = *regs[(o >> 3) & 7] + (byte ? imm : (imm << 2));
            kind = 3;
        }
        else if ((o & 0xF000) == 0x9000) {
            load = (o & 0x0800) != 0; // STR/LDR Rd,[SP,#imm8*4]
            rd = (o >> 8) & 7;        // Rd in bits 10-8
            address = *regs[13] + (uint32_t)((o & 0xFF) << 2);
            kind = 3;
        }
        else if (hi5 == 20 || hi5 == 21) {
            kind = 1; // ADD Rd,PC/SP,#imm8*4
        }
        else if ((o & 0xFF80) == 0xB000 || (o & 0xFF80) == 0xB080) {
            kind = 1; // ADD/SUB SP,#imm7*4
        }
        else if ((o & 0xF600) == 0xB400) {
            const bool isPop = (o & 0x0800) != 0;
            const bool extra = (o & 0x0100) != 0;
            if (isPop && extra) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[4];
#endif
                break; // POP {...,PC} branches
            }
            list = o & 0xFF;
            m = bitCount[list] + (extra ? 1 : 0);
            load = isPop;
            kind = 4;
            low = isPop ? *regs[13] : (*regs[13] - (m << 2));
            address = isPop ? low : low;
        }
        else if ((o & 0xF000) == 0xC000) {
            list = o & 0xFF;
            if (!list) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[4];
#endif
                break; // empty list has PC load/store special cases
            }
            load = (o & 0x0800) != 0;
            m = bitCount[list];
            kind = 4;
            const uint32_t base = *regs[(o >> 8) & 7];
            low = base;
            address = base;
        }
        else if ((o & 0xFF00) == 0xDF00) {
#if NOODS_GBA_ISLAND_STATS
            ++g_tisBrk[4];
#endif
            break; // SWI
        }
        else if ((o & 0xF000) == 0xD000) {
            const unsigned cc = (o >> 8) & 0xF;
            if (cc >= 14) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[4];
#endif
                break;
            }
            const unsigned cond = condition[(cc << 4) | (cpsr >> 28)];
            if (cond != 1) {
                kind = 1; // not taken: one cycle, no flags changed
            }
            else {
                kind = 5;
                target = r15 + 2u + (uint32_t)((int32_t)(int8_t)(uint8_t)o << 1);
            }
        }
        else if ((o & 0xF800) == 0xE000) {
            kind = 5; // B label
            target = r15 + 2u + (uint32_t)((int32_t)(int16_t)(uint16_t)(o << 5) >> 4);
        }
        else if ((o & 0xF800) == 0xF000) {
            kind = 6; // BL setup: LR = PC + (imm11 << 11), no branch
        }
        else if ((o & 0xF800) == 0xF800) {
            kind = 5; link = true; // BL label
            target = *regs[14] + (uint32_t)((o & 0x7FF) << 1);
        }
        else {
#if NOODS_GBA_ISLAND_STATS
            ++g_tisBrk[4];
#endif
            break;
        }

#if TISL_MAX_KIND < 6
        if (kind > TISL_MAX_KIND) break;
#endif
        // ---- region / mapping validation for data accesses ----
        if (kind == 3) {
            const uint32_t region = address >> 24;
            const bool okRegion = region == 2 || region == 3 || region == 5 ||
                                  region == 6 || region == 7 ||
                                  (region >= 8 && region <= 0xD);
            if (!okRegion || (load && !readMap[address >> 12]) ||
                (!load && address >= 0x80000C4u && address < 0x80000CAu)) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[5];
#endif
                break;
            }
        }
        else if (kind == 4) {
            bool ok = true;
            for (uint32_t a = address, k = 0; k < m; k++, a += 4u) {
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
            if (!ok) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[5];
#endif
                break;
            }
        }

        // ---- event safety margin: exact worst-case cost per category ----
        //   fetch (THUMB, ROM, worst) = 9+9 = 18 (16-bit ROM bus halves)
        //   ALU / not-taken branch    = 18
        //   MUL                       = 18 + 5 internal
        //   LDR/STR                   = 18 + (1 stall + 9N + 9S) + 1I = 38
        //   B/BL                      = 18 + branchRefill(18+18) = 54
        //   PUSH/POP/STMIA/LDMIA      = 18 + m*19 + 1I
        int32_t need = 20;
        if (kind == 4)
            need = (int32_t)(21 + m * 19);
        else if (kind == 5)
            need = 56;
        else if (kind == 3)
            need = 40;
        else if (kind == 2)
            need = 25;
        if ((int32_t)(c.events.frontCycles - clock) <= need) {
#if NOODS_GBA_ISLAND_STATS
            ++g_tisBrk[1];
#endif
            break;
        }

        // ---- 1. fetch phase: advance the pipeline, charge the prefetch model ----
        TISL_SNAPSHOT();
        const uint16_t executing = o;
        const uint32_t newPc = r15 + 2u;
        uint8_t *newPtr;
        if ((newPc & 0xFFEu) && pc) {
            newPtr = pc + 2;
        }
        else { // page switch: re-derive the opcode pointer exactly like getOpcode16
            uint8_t *np = readMap[newPc >> 12];
            if (!np) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[6];
#endif
                break;
            }
            newPtr = np + (newPc & 0xFFEu);
        }
        const uint16_t newWord = U8TO16(newPtr, 0);
        // commit the pipeline advance (nothing above may break after this point)
        r15 = newPc;
        *regs[15] = r15;
        op = next; // pipeline[0] = pipeline[1], then the freshly fetched halfword
        next = newWord;
        pc = newPtr;
        clock += bus.fetchOpcode(r15, false, waitCnt);
        cycles = (c.globalCycles = clock);
#if TISL_DMA_SCAN
        // runGbaOpcode step 2, copied verbatim: a DMA that became ready by the end
        // of the fetch preempts the bus before the data phase.
        if (!halted && c.events.frontCycles <= clock) {
            ++g_tisPreempt;
            bool usedDuty = bus.lastFetchUsedDuty;
            for (size_t idx = 0; idx < c.events.size() && c.events[idx].cycles <= clock; ) {
                SchedTask t = c.events[idx].task;
                if (t >= DMA7_TRANSFER0 && t <= DMA7_TRANSFER3) {
                    c.events.eraseAt(idx);
                    c.globalCycles = clock;
                    c.tasks[t]();
                    c.globalCycles = clock;
                    int sWait = GbaBus::romWaitS(r15, waitCnt);
                    if (usedDuty && ((c.dma[1].firstPakWasWrite && sWait == 3) ||
                                     (c.dma[1].firstPakWasRead && !(cpsr & BIT(5)) && sWait == 2))) {
                        clock = (c.globalCycles += 1);
                        usedDuty = false;
                    }
                    idx = 0;
                    continue;
                }
                idx++;
            }
            cycles = clock;
            waitCnt = c.memory.getWaitCnt();
        }
#endif
        bus.beginInstruction();

        // ---- 2. execute + data-phase charging ----
        // Match runGbaOpcode step 3: the DMA open-bus latch is CPU-execution aware,
        // so every island instruction has to toggle the same flag and clear the
        // latch afterwards, exactly like the interpreter does.
        c.memory.gbaCpuExecActive = true;
        if (kind == 1) {
            const unsigned e5 = executing >> 11;
            if (e5 < 3) { // LSL/LSR/ASR Rd,Rs,#imm5
                const uint8_t shift = (executing >> 6) & 0x1F;
                const uint32_t src = *regs[(executing >> 3) & 7];
                uint32_t res;
                bool carryOut;
                if (e5 == 0) { // LSL
                    res = src << shift;
                    carryOut = shift > 0 ? (bool)(src & BIT(32 - shift)) : (bool)(cpsr & BIT(29));
                    *regs[executing & 7] = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    if (shift > 0) cpsr = (cpsr & ~BIT(29)) | (carryOut << 29);
                }
                else if (e5 == 1) { // LSR
                    res = shift ? (src >> shift) : 0;
                    carryOut = (bool)(src & BIT(shift ? (shift - 1) : 31));
                    *regs[executing & 7] = res;
                    cpsr = (cpsr & ~0xE0000000) | (res & BIT(31)) | ((res == 0) << 30) | (carryOut << 29);
                }
                else { // ASR
                    res = shift ? ((int32_t)src >> shift) : ((src & BIT(31)) ? 0xFFFFFFFFu : 0);
                    carryOut = (bool)(src & BIT(shift ? (shift - 1) : 31));
                    *regs[executing & 7] = res;
                    cpsr = (cpsr & ~0xE0000000) | (res & BIT(31)) | ((res == 0) << 30) | (carryOut << 29);
                }
            }
            else if (e5 == 3) { // ADD/SUB register and 3-bit immediate
                const uint32_t a = *regs[(executing >> 3) & 7];
                const bool isSub = (executing & BIT(9)) != 0;
                const uint32_t b = (executing & BIT(10)) ? (uint32_t)((executing >> 6) & 7)
                                                         : *regs[(executing >> 6) & 7];
                const uint32_t res = isSub ? (a - b) : (a + b);
                *regs[executing & 7] = res;
                cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                       (isSub ? ((a >= res) << 29) : ((a > res) << 29)) |
                       (isSub ? ((uint32_t)(((b ^ a) & ~(res ^ b) & BIT(31))) >> 3)
                              : ((uint32_t)((~(b ^ a) & (res ^ b) & BIT(31))) >> 3));
            }
            else if (e5 < 8) { // MOV/CMP/ADD/SUB Rd,#imm8
                const unsigned rd = (executing >> 8) & 7;
                const uint32_t imm = executing & 0xFF;
                const unsigned sel = (executing >> 11) & 3;
                if (sel == 0) { // MOV
                    cpsr = (cpsr & ~0xC0000000) | (imm & BIT(31)) | ((imm == 0) << 30);
                    *regs[rd] = imm;
                }
                else if (sel == 1) { // CMP
                    const uint32_t a = *regs[rd];
                    const uint32_t res = a - imm;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           ((a >= res) << 29) |
                           ((uint32_t)(((imm ^ a) & ~(res ^ imm) & BIT(31))) >> 3);
                }
                else { // ADD / SUB
                    const uint32_t a = *regs[rd];
                    const bool isSub = (sel == 3);
                    const uint32_t res = isSub ? (a - imm) : (a + imm);
                    *regs[rd] = res;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           (isSub ? ((a >= res) << 29) : ((a > res) << 29)) |
                           (isSub ? ((uint32_t)(((imm ^ a) & ~(res ^ imm) & BIT(31))) >> 3)
                                  : ((uint32_t)((~(imm ^ a) & (res ^ imm) & BIT(31))) >> 3));
                }
            }
            else if ((executing & 0xFC00) == 0x4000) { // ALU ops
                const unsigned sub = (executing >> 6) & 0xF;
                uint32_t *rd = regs[executing & 7];
                const uint32_t a = *rd;
                const uint32_t b = *regs[(executing >> 3) & 7];
                switch (sub) {
                case 2: { // LSL
                    const uint8_t s = (uint8_t)b;
                    const uint32_t res = (s < 32) ? (a << s) : 0;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    if (s > 0) cpsr = (cpsr & ~BIT(29)) | ((bool)(s <= 32 && (a & BIT(32 - s))) << 29);
                    break;
                }
                case 3: { // LSR
                    const uint8_t s = (uint8_t)b;
                    const uint32_t res = (s < 32) ? (a >> s) : 0;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    if (s > 0) cpsr = (cpsr & ~BIT(29)) | ((bool)(s <= 32 && (a & BIT(s - 1))) << 29);
                    break;
                }
                case 4: { // ASR
                    const uint8_t s = (uint8_t)b;
                    const uint32_t res = (s < 32) ? ((int32_t)a >> s) : ((a & BIT(31)) ? 0xFFFFFFFFu : 0);
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    if (s > 0) cpsr = (cpsr & ~BIT(29)) | ((bool)(a & BIT((s <= 32) ? (s - 1) : 31)) << 29);
                    break;
                }
                case 7: { // ROR
                    const uint8_t s = (uint8_t)b;
                    const unsigned amt = s & 31;
                    const uint32_t res = amt ? ((a >> amt) | (a << (32 - amt))) : a;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    if (s > 0) cpsr = (cpsr & ~BIT(29)) | ((bool)(a & BIT((s - 1) & 0x1F)) << 29);
                    break;
                }
                case 0: { // AND
                    const uint32_t res = a & b;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                case 1: { // EOR
                    const uint32_t res = a ^ b;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                case 5: { // ADC
                    const uint32_t carryIn = (cpsr & BIT(29)) >> 29;
                    const uint32_t res = a + b + carryIn;
                    *rd = res;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           (((a > res || (b == 0xFFFFFFFFu && (cpsr & BIT(29)))) << 29) & 0xFFFFFFFFu) |
                           ((uint32_t)((~(b ^ a) & (res ^ b) & BIT(31))) >> 3);
                    break;
                }
                case 6: { // SBC
                    const uint32_t carryIn = (cpsr & BIT(29)) >> 29;
                    const uint32_t res = a - b - 1 + carryIn;
                    *rd = res;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           (((a >= res && (b != 0xFFFFFFFFu || (cpsr & BIT(29)))) << 29) & 0xFFFFFFFFu) |
                           ((uint32_t)(((b ^ a) & ~(res ^ b) & BIT(31))) >> 3);
                    break;
                }
                case 8: { // TST
                    const uint32_t res = a & b;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                case 9: { // NEG
                    const uint32_t res = 0 - b;
                    *rd = res;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           ((b == 0) << 29) | ((b == 0x80000000u) << 28);
                    break;
                }
                case 10: { // CMP
                    const uint32_t res = a - b;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           ((a >= res) << 29) |
                           ((uint32_t)(((b ^ a) & ~(res ^ b) & BIT(31))) >> 3);
                    break;
                }
                case 11: { // CMN
                    const uint32_t res = a + b;
                    cpsr = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) |
                           ((a > res) << 29) |
                           ((uint32_t)((~(b ^ a) & (res ^ b) & BIT(31))) >> 3);
                    break;
                }
                case 12: { // ORR
                    const uint32_t res = a | b;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                case 14: { // BIC
                    const uint32_t res = a & ~b;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                default: { // MVN
                    const uint32_t res = ~b;
                    *rd = res;
                    cpsr = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
                    break;
                }
                }
            }
            else if (e5 == 20 || e5 == 21) { // ADD Rd,PC/SP,#imm8*4
                const uint32_t src = (e5 == 20) ? (*regs[15] & ~3u) : *regs[13];
                *regs[(executing >> 8) & 7] = src + (uint32_t)((executing & 0xFF) << 2);
            }
            else if (e5 == 22) { // ADD/SUB SP,#imm7*4
                const uint32_t imm = (uint32_t)(((executing & BIT(7)) ? (0 - (executing & 0x7F))
                                                                      : (executing & 0x7F)) << 2);
                *regs[13] += imm;
            }
            else { // not-taken conditional branch: one cycle, nothing changes
            }
        }
        else if (kind == 2) { // MUL Rd,Rs (ARM7: exact ticks and carry)
            const uint32_t a = *regs[executing & 7];
            const uint32_t b = *regs[(executing >> 3) & 7];
            auto r = Arm7TdmiMul::execute(Arm7TdmiMul::SHORT, b, a);
            const uint32_t res = (uint32_t)r.output;
            *regs[executing & 7] = res;
            cpsr = (cpsr & ~0xE0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((uint32_t)r.carry << 29);
            const int internal = r.ticks; // handler returns ticks+1, one is the fetch
            if (internal > 0)
                clock += bus.stepInternal(internal, waitCnt);
            cycles = (c.globalCycles = clock);
        }
        else if (kind == 3) {
            if (load) {
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
                *regs[rd] = value;
                clock += bus.stepInternal(1, waitCnt); // trailing 1I after loads
                cycles = (c.globalCycles = clock);
            }
            else {
                cycles = (c.globalCycles = clock);
                bool prev = c.memory.gbaCpuExecActive;
                c.memory.gbaCpuExecActive = true;
                const uint32_t value = *regs[rd];
                if (byte)
                    c.memory.write<uint8_t>(true, address, value);
                else
                    c.memory.write<uint32_t>(true, address, value);
                c.memory.gbaCpuExecActive = prev;
                clock = cycles; // charged through advanceGbaCycles inside write
            }
        }
        else if (kind == 4) { // PUSH/POP/STMIA/LDMIA
            uint32_t a = address;
            if ((executing & 0xF600) == 0xB400) { // PUSH/POP
                if (load) {
                    for (unsigned i = 0; i < 8; i++) {
                        if (!(list & BIT(i))) continue;
                        int cyc;
                        const uint32_t region = a >> 24;
                        if (!bus.active && region < 8 && ((0x9Bu >> region) & 1)) {
                            bus.nextSeq = false; bus.inSeqAccess = true;
                            bus.hadDataAccess = true; bus.hadLoad = true;
                            cyc = 1;
                        }
                        else {
                            cyc = bus.dataAccess(a, 4, bus.inSeqAccess, waitCnt, false);
                            bus.inSeqAccess = true; bus.hadDataAccess = true; bus.hadLoad = true;
                        }
                        clock += cyc;
                        cycles = (c.globalCycles = clock);
                        *regs[i] = U8TO32(readMap[a >> 12], a & 0xFFCu);
                        *regs[13] = (a += 4u);
                    }
                }
                else {
                    const uint32_t sp0 = *regs[13] - (m << 2);
                    *regs[13] = sp0;
                    a = sp0;
                    bool prev = c.memory.gbaCpuExecActive;
                    c.memory.gbaCpuExecActive = true;
                    for (unsigned i = 0; i < 8; i++) {
                        if (!(list & BIT(i))) continue;
                        cycles = (c.globalCycles = clock);
                        c.memory.write<uint32_t>(true, a, *regs[i]);
                        clock = cycles;
                        a += 4u;
                    }
                    if (executing & 0x0100) { // PUSH {...,LR}
                        cycles = (c.globalCycles = clock);
                        c.memory.write<uint32_t>(true, a, *regs[14]);
                        clock = cycles;
                    }
                    c.memory.gbaCpuExecActive = prev;
                }
            }
            else { // STMIA/LDMIA Rb!,<Rlist>
                const unsigned rb = (executing >> 8) & 7;
                if (load) {
                    *regs[rb] += (m << 2); // writeback happens before the loads
                    for (unsigned i = 0; i < 8; i++) {
                        if (!(list & BIT(i))) continue;
                        int cyc;
                        const uint32_t region = a >> 24;
                        if (!bus.active && region < 8 && ((0x9Bu >> region) & 1)) {
                            bus.nextSeq = false; bus.inSeqAccess = true;
                            bus.hadDataAccess = true; bus.hadLoad = true;
                            cyc = 1;
                        }
                        else {
                            cyc = bus.dataAccess(a, 4, bus.inSeqAccess, waitCnt, false);
                            bus.inSeqAccess = true; bus.hadDataAccess = true; bus.hadLoad = true;
                        }
                        clock += cyc;
                        cycles = (c.globalCycles = clock);
                        *regs[i] = U8TO32(readMap[a >> 12], a & 0xFFCu);
                        a += 4u;
                    }
                }
                else {
                    bool prev = c.memory.gbaCpuExecActive;
                    c.memory.gbaCpuExecActive = true;
                    if ((list & (BIT(rb + 1) - 1)) > BIT(rb)) // ARM7 quirk
                        *regs[rb] = a + (m << 2);
                    for (unsigned i = 0; i < 8; i++) {
                        if (!(list & BIT(i))) continue;
                        cycles = (c.globalCycles = clock);
                        c.memory.write<uint32_t>(true, a, *regs[i]);
                        clock = cycles;
                        a += 4u;
                    }
                    *regs[rb] = a;
                    c.memory.gbaCpuExecActive = prev;
                }
            }
            if (load) {
                clock += bus.stepInternal(1, waitCnt); // trailing 1I after loads
                cycles = (c.globalCycles = clock);
            }
        }
        else if (kind == 6) { // BL setup
            *regs[14] = r15 + (uint32_t)(((int32_t)(int16_t)(uint16_t)(executing << 5) >> 4) << 11);
        }
        else { // branch
            if (link)
                *regs[14] = r15 - 1u; // return address with the THUMB bit set
            const uint32_t tp = target & ~1u;
            uint8_t *b0 = readMap[tp >> 12];
            uint8_t *b1 = readMap[(tp + 2u) >> 12];
            if (!b0 || !b1) {
#if NOODS_GBA_ISLAND_STATS
                ++g_tisBrk[6];
#endif
                break;
            }
            r15 = tp + 2u;
            *regs[15] = r15;
            op = U8TO16(b0 + (tp & 0xFFFu), 0);
            next = U8TO16(b1 + ((tp + 2u) & 0xFFFu), 0);
            pc = b1 + ((tp + 2u) & 0xFFFu);
            clock += bus.branchRefill(tp, false, waitCnt);
            cycles = (c.globalCycles = clock);
        }

        c.memory.gbaCpuExecActive = false;
        c.memory.clearGbaDmaOpenBus();
#if TISL_SYNC_PIPELINE
        pipeline[0] = op;
        pipeline[1] = next;
        pcData = pc;
        cycles = clock;
        c.globalCycles = clock;
#endif
#if TISL_DMA_SCAN
        if ((int32_t)(c.events.frontCycles - clock) < 0) {
            if (g_tisOverrun < 40)
                fprintf(stderr, "TISL_OVERRUN pc=%08X op=%04X kind=%u by=%d\n",
                        (unsigned)(r15 - 2u), (unsigned)executing, kind,
                        (int)(clock - c.events.frontCycles));
            ++g_tisOverrun;
        }
#endif
        TISL_VERIFY();
#if NOODS_GBA_ISLAND_STATS
        ++g_tisInstrs;
        if (kind <= 6) ++g_tisCat[kind];
#endif
        ++done;
    }

    if (!done)
        return false;
#if NOODS_GBA_ISLAND_STATS
    ++g_tisEntries;
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
