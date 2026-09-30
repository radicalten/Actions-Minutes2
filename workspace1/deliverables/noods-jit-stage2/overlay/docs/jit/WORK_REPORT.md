# NooDS-Wii JIT — stage 2: native single-instruction ALU path

## Status

**An experimental ARM/THUMB → PPC execution path is now integrated. It is default-off and is not the finished VBA-GX-parity-plus-ARM recompiler.**

Unlike stage 1, this version emits guest-operation-specific native PPC code, caches it, and executes it from `Interpreter::runOpcode`. It does not merely call interpreter handlers from generated code. Unsupported instructions take the existing interpreter path. No game-performance improvement or hardware correctness is claimed.

Base repository: radicalten/NooDS-Wii `1c995b48c37ebf3645646968c416958f79264137`.
Stage-1 continuation point: local commit `0738f72` (full hash recorded in the bundle's patch base metadata).
Comparison reference remains dborth/vbagx `5db68c4e1c908b0c79b5f589b7a82e88a3dff1b2`.
The user authorized direct implementation in the prior turn and requested continuation in the preferred order. No delegated agent, API credential, remote push or PR was used.

## What changed

### Native translators

- `jit_compiler_arm.cpp`: non-PC ARM data processing, using an allowlist generated from the actual dispatch table. Supports AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC, TST/TEQ/CMP/CMN, ORR/MOV/BIC/MVN, applicable S variants, rotated immediates, immediate shifts and register-specified shifts. PC operands and reserved conditions are excluded. Even unused PC-encoded Rn/Rd fields are conservatively rejected.
- `jit_compiler_thumb.cpp`: immediate LSL/LSR/ASR; ADD/SUB register and immediate; MOV/CMP/ADD/SUB imm8; logical ALU, ADC/SBC, TST/CMP/CMN, NEG, register LSL/LSR/ASR/ROR; non-PC high-register ADD/CMP/MOV; SP-relative address addition and SP adjustment.
- `jit_compiler_common.cpp`: operand shifts including zero/32/>32 cases, immediate constants, PPC arithmetic and flag materialization, exact 1/2-cycle leaf returns. This is original code, not vendored VBA-GX code.
- `jit_decode_arm.inc`: 4,096-entry generated allowlist, of which 784 table slots are ALU-eligible before PC rejection. `tools/jit_generate_decode.py --check` detects drift in the generated classification.

Interpreter-equivalence quirks are preserved: ARM RSCS ORs newly computed C/V into the old C/V bits, and THUMB NEG clears V even for the architectural overflow case. No interpreter semantics were silently corrected.

The native compiler accepts **20,323 of all 65,536 THUMB encodings** in the exhaustive acceptance sweep. That number describes this limited ALU subset, not overall CPU compatibility. MUL and all memory/branch/exception instructions remain interpreter operations.

### Execution/ABI

- `jit.h`: fixed-offset, 32-byte-aligned context with compile-time layout assertions. 96 bytes on PPC32.
- `jit_trampoline.cpp`: top-level assembly in a `.cpp`, so no Makefile .S rule is required. Saves/restores LR, the full CR and r14-r31. Uses the requested register contract: guest r0-r14 in host r14-r28, CPSR r29, context r30, interpreter r31. r2/r13 remain untouched; r1 is used only as the ABI stack pointer. Guest PC is not cached in a host register.
- `jit.cpp`: snapshots current banked register values, invokes a native leaf, then commits r0-r14/CPSR and returns its exact cycle count. No guest fetch, PC update, memory access or mode change occurs in a native leaf.

**Boundary:** native dispatch is inserted after the existing pipeline advance and, for ARM, after the existing condition check. Failure to compile executes the already-selected interpreter handler once at that same boundary. It does not call runOpcode a second time. False ARM conditions and reserved/HLE paths remain ahead of the JIT hook.

Each call performs exactly one guest instruction. Existing scheduler loops and their CPU/global cycle conversions are unchanged. This avoids introducing multi-instruction overshoot or different ARM7/ARM9 interleaving at this stage. It is not a completed block scheduler or chaining implementation.

### Cache and self-modification policy

`jit_cache.cpp` supplies a fixed **4 MiB**, 32-byte-aligned MEM2 arena allocated once, a 4,096-slot direct-mapped table and a static 256-word staging buffer. Arena exhaustion clears all entries and restarts at the beginning; it never grows. Allocation failure returns to the interpreter. Runtime-off does not allocate the arena.

After writing native code, it calls `DCFlushRange` followed by `ICInvalidateRange` over the aligned written range. Those calls compile on Wii; physical cache coherence still needs hardware validation.

**This cache is keyed by exact already-prefetched opcode, THUMB/ARM state and CPU identity—not guest PC.** Leaves embed no guest addresses, fetched memory pointers, mapping assumptions, register values or flag values. Consequently:

- A modified instruction selects code for its new opcode when fetched.
- Already-prefetched old bytes execute their old semantics, like the interpreter.
- Mirrored writes and remaps cannot make these pure, location-independent ALU leaves stale.
- No invalidation hook has been added to Memory/DMA/remapping in this stage.

This narrow content-addressed design must **not** be generalized to address-specialized blocks without adding mapping/alias-aware invalidation. There is no page-bucket cache, no native store path, and no claim that the final requested SMC framework is complete. Cached entry pointers are ephemeral and must be executed before another lookup can recycle the arena.

### Off-switches and diagnostics

- Runtime: `Settings::jitEnabled`, serialized as `jitEnabled=0` or `jitEnabled=1` in `noods.ini`. Default is **0**. No GUI toggle is added.
- Build-time: `make NOODS_JIT=0` excludes **both `jit.cpp` and `jit_*.cpp`**, defines `NOODS_NO_JIT`, and removes runtime hooks at preprocessing time. The setting may still exist in the ini but has no effect in this build.
- `make JIT_DIFFTEST=1`: for accepted pure ALU instructions, execute native code on a snapshot and the original handler once on the real CPU. Compare r0-r15, all CPSR bits and returned cycles. Keep the reference result. First mismatch logs PC/opcode/state to `sd:/noods/jit_log.txt`, flushes/closes the log, and latches native execution off for the session. Failure to open the log does not prevent disabling native execution.

Differential mode does not duplicate MMIO: memory instructions cannot enter the native path. The mode is a bring-up diagnostic, not a general memory-replay lockstep engine.

The Makefile clean rule now removes all source objects independent of the selected switch. Still run `make clean` after header edits or when switching build flags: automatic header/flag dependency tracking remains absent upstream.

## Verification actually performed

### 1. Encoder/flag foundation rerun

- **121** independent GNU assembler comparisons: passed.
- Bounded writer/range tests: passed.
- **324,576** emitted arithmetic/flag expression probes under qemu-ppc 750: passed.

### 2. Actual interpreter ALU oracle

`tools/jit_make_test_oracle.py` takes the **actual unchanged interpreter_alu.cpp**, substitutes only its `core.h` include for the minimal test include, and generates reference calls from the real dispatch tables. Handler bodies and macros are not reimplemented. PC/mode-change calls in this isolated harness abort if unexpectedly reached.

`tools/run_jit_native_tests.sh` executes native translator output through the production trampoline, with an assembly ABI checker around every call. Tests include:

- Every THUMB encoding, either accepted and compared or rejected without emitted code.
- Every ARM dispatch slot with varied register/immediate fields, plus 60,000 random ARM words.
- ARM7/ARM9 identities, old NZCV combinations, operand aliases and boundary values.
- Shift-count boundaries including 0, 31, 32, 33, 63, 64, 255, 256/257 and randomized values.
- r0-r15, all CPSR bits and cycles.
- Preservation of r14-r31, r2/r13 and the full CR on every native invocation; normal return also exercises LR restoration.
- A deliberately tiny 4 KiB test arena to force cache recycling, key collisions, repeated hits and negative-cache entries.

Result per run: **1,415,296 state comparisons passed**; 20,323 distinct THUMB encodings and 19,726 generated ARM candidates accepted in that sweep. Cache test observed 31,871 hits, 16,129 misses, 15,999 compilations, 124 arena flushes and 130 rejections.

Ran this twice: once with the interpreter oracle compiled by PPC Linux GCC, and again with the oracle compiled by **devkitPPC 16.1.0 using the port's optimization/ABI flags**, linked into the Linux/QEMU harness. Both passed the same cases. These are two oracle builds of the same corpus, not twice as many unique test states.

### 3. Pipeline and integration harness

`tools/jit_make_pipeline_test.py` extracts the exact current runOpcode/getOpcode16/getOpcode32 bodies and condition table. It uses a small instrumented memory backend and instrumented non-ALU fallback/reserved handlers. This is **not** the complete Core, scheduler, MMIO, BIOS or DMA implementation.

Each of four modes passed **43,776 step comparisons**:

1. Native included, runtime switched between off/on.
2. `JIT_DIFFTEST=1`.
3. `NOODS_NO_JIT` with no JIT implementation linked.
4. Simulated allocation failure, forcing every supported native attempt back to the interpreter.

Checks include condition true/false/reserved routing, fallback invocation counts, native and fallback adjacency, banked register pointers, page-boundary fetches, mapped/unmapped fetches, r15, both pipeline slots, fetch-side-effect counts, pcData, scheduler fields and memory bytes differing from already-prefetched opcodes.

An intentional reference-dispatch corruption verified that mismatch logging occurs, reference register results/cycles are retained, and later native attempts are disabled. This was a test fault injection, not an observed translator mismatch.

### 4. Clean Wii builds

Compile + link + elf2dol succeeded in all three configurations:

| Build | DOL entry | Result |
|---|---|---|
| Default, JIT included but ini default off | `0x80003f00` | Passed |
| `JIT_DIFFTEST=1` | `0x80003f00` | Passed |
| `NOODS_JIT=0` | `0x80003f00` | Passed |

DOL section/file-bound checks passed. Symbol inspection confirms native translator/dispatcher/trampoline symbols in the included build and **no Jit/JitPpc/jitEnter symbols or JIT objects** in the excluded build. Artifacts are named `NooDS-Wii.dol` by the upstream Makefile; a user may rename the result to `boot.dol` for HBC. This deliverable remains source-only.

During bring-up, the architecture guard was corrected to accept devkitPPC's `__PPC__` macro as well as Linux GCC's `__powerpc__`. The build-time exclusion was extended to include `jit.cpp`, whose name does not match `jit_*.cpp`. A test-only include needed correction for the compiled-out harness. Reported passes are from the corrected sources.

Exact outputs and build logs are in `verification/stage2/`. Stage-1 logs remain historical.

## Written but not verified on Wii/GCN hardware

- Wii execution of the native path, cache publication and long-running stability.
- Full emulator JIT_DIFFTEST ROM runs, peripheral behavior and frame scheduling.
- User-facing boot/loading and runtime ini toggling on a real console.
- Performance, FPS, compile overhead and code-cache pressure in games.

No Dolphin session, hardware run or GameCube build was performed. The instruction set is Gekko-compatible, but the surrounding port and allocator are still Wii-specific.

## Remaining work / deviations from the final design

| Final requirement | Stage-2 status |
|---|---|
| Native non-PC ARM ALU/shifters | Implemented and isolated-differential-tested |
| Native THUMB ALU/shifters except MUL/PC uses | Implemented and isolated-differential-tested |
| Branches/BX/BLX/BL pairs and PC-writing exits | Interpreter fallback only |
| Native loads/stores, PUSH/POP, LDM/STM, doubleword transfers | Interpreter fallback only |
| Native multiply families and exact CPU-specific ramps | Interpreter fallback only |
| Multi-instruction blocks, block budgets and native chaining | Not implemented |
| Shared resume-state epilogue using r11/r12 | Not implemented; pure leaves return cycles in r3 to the trampoline |
| Address-keyed page buckets / mirrored-write invalidation hooks | Not implemented; current content-keyed ALU leaves do not depend on memory locations |
| 2 MiB GBA / 4 MiB NDS arena policy | Currently fixed 4 MiB for either mode; mode-specific sizing deferred |
| Two off-switches | Implemented; mock integration and clean Wii builds passed |
| Native/reference mismatch diagnostic | Implemented for accepted pure leaves; general memory lockstep deferred |
| VBA-GX native coverage parity and performance superiority | **Not achieved** |
| GameCube platform build | Not implemented |

A one-instruction native call snapshots 15 registers and saves/restores the ABI context. **It may be slower than the interpreter.** The point of this stage is a tested execution contract, not a speed claim. Keep the default off for normal use until target validation and multi-instruction amortization work are done.

## Apply and reproduce

The bundle includes an overlay, a patch from the pinned upstream base, and an incremental patch from stage 1. Choose one method; do not apply multiple alternatives. Headers and `jit_decode_arm.inc` are required alongside the `.cpp` files. Do not put standalone test mains into the emulator source directory.

```sh
bash tools/run_jit_asm_oracle.sh
bash tools/run_jit_flag_probe.sh
bash tools/run_jit_native_tests.sh
bash tools/run_jit_pipeline_tests.sh

source /home/user/wii-env.sh
JIT_ORACLE_DEVKIT=1 bash tools/run_jit_native_tests.sh
make clean && make -j4
python3 tools/jit_validate_dol.py NooDS-Wii.dol
make clean && make JIT_DIFFTEST=1 -j4
make clean && make NOODS_JIT=0 -j4
```

For **experimental console testing**, use a JIT-included build and set `jitEnabled=1` on its own newline-terminated line in `noods.ini`. Start with `JIT_DIFFTEST=1`; inspect `sd:/noods/jit_log.txt`. To disable: set `jitEnabled=0`, or clean-build `NOODS_JIT=0`. This is a recommendation for future testing, not a claim that console testing has happened.

## Next preferred order

1. Add differential-tested multiply families and conservative PC/branch exits while retaining one-instruction scheduling. Preserve interpreter quirks or use fallback; do not silently change the reference.
2. Design multi-instruction pipeline maintenance and scheduler interleaving, then implement budgeted short ALU blocks with trace-level lockstep. This is needed to amortize the current entry cost.
3. Before address-specialized caching or native memory operations, implement alias-aware invalidation and robust remap/reset/state-load hooks. Add memory/MMIO differential replay or isolation; never execute side-effectful MMIO twice.
4. Complete THUMB memory/stack/branch families and ARM transfer/block/multiply coverage, then target hardware/Dolphin validation and profiling.

The original interpreter ALU/transfer/branch implementations were not changed. GPL license and attributions are retained. The historical stage-1 report is `WORK_REPORT_STAGE1.md`.
