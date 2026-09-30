# NooDS-Wii JIT — stage 3: native multiply and branch exits

## Status

**Implemented and tested as an experimental, default-off single-instruction JIT. Not yet the finished VBA-GX-parity-plus-ARM recompiler.**

This continuation adds real PPC translation of basic multiply families and branches. Native code computes results, targets and mode changes; the original interpreter code performs any required pipeline refill. The emulator still calls the JIT at one guest-instruction boundary at a time. No multi-instruction blocks, native memory transfers, FPS improvement or console-runtime correctness are claimed.

Source bases:

- Upstream NooDS-Wii: `1c995b48c37ebf3645646968c416958f79264137`.
- Stage-2 continuation: `3403f20` (full IDs are in `patch-bases.json`).
- VBA-GX comparison reference remains `5db68c4e1c908b0c79b5f589b7a82e88a3dff1b2`.

Direct implementation remains user-authorized. Work is on the local feature branch; no remote push/PR or external coding agent was used.

## New native coverage

### Multiplication

New file: **`NooDS-Wii/jit_compiler_multiply.cpp`**.

- ARM MUL, MLA, UMULL, UMLAL, SMULL, SMLAL, with and without S.
- THUMB MUL.
- Native PPC `mullw`, `mulhw`, `mulhwu`, `addc`/`adde`; no 64-bit host instructions or C++ arithmetic fallback inside generated code.
- Signed and unsigned high-word results, low-to-high accumulation carry and full-result Z flags.
- Captured input operands/timing multipliers before writing destinations, including destination/source aliases and RdLo == RdHi.
- PC-encoded operands/destinations remain conservative fallbacks, including unused short-multiply Rn fields encoded as PC.
- ARM9 DSP/halfword multiply extensions are **not** native yet; they remain interpreter operations.

Timing matches this repository's handlers, not a generic ARM timing formula:

| Family | ARM9 | ARM7 |
|---|---:|---:|
| MUL / MLA | 2 | m+1 / m+2 |
| MULS / MLAS | 4 | m+1 / m+2 |
| UMULL / UMLAL | 3 | 6 / 7 |
| UMULLS / UMLALS | 5 | 6 / 7 |
| SMULL / SMLAL | 3 | m+2 / m+3 |
| SMULLS / SMLALS | 5 | m+2 / m+3 |
| THUMB MUL | 4 | m+1 |

For the signed timing path, m is 1 for [-256,255], 2 for [-65536,65535], 3 for [-16777216,16777215], otherwise 4. THUMB uses the **old Rd** value as its timing multiplier; ARM uses the captured Rs value.

**Pinned-source quirk:** the ARM7 unsigned-long timing loop compares an unsigned operand against `(-1 << n)` and `(1 << n)`. At n=8/16/24, the unsigned comparisons cover all values, so m reaches 4. The native path preserves the observed 6/7 counts. Tests against both PPC Linux GCC and devkitPPC-compiled interpreter handlers confirm the selected implementation. These counts are not an assertion of architectural ARM accuracy or portability of the source's shift expressions across arbitrary compilers.

### Branches and link operations

New file: **`NooDS-Wii/jit_compiler_branch.cpp`**.

- ARM B, BL, BX, BLX register and reserved-condition BLX immediate.
- THUMB all 14 ordinary conditional branches, unconditional B, BX, BLX register, BL/BLX setup half, BL suffix and BLX suffix.
- PC register operands in BX/BLX are compile-time PC constants, never a cached guest r15 host register.
- LR-as-source aliasing is handled by capturing targets before replacing LR.
- ARM7 BLX behavior is the reference's 1-cycle no-op, not an ARM9 mode change.
- Taken branches return 3 cycles and request one refill; untaken THUMB conditionals and BL setup return 1 without refilling.
- Relative offsets are sign-extended without introducing signed-left-shift undefined behavior in the new compiler.
- Thumb/ARM target selection, alignment, PC wraparound, and link addresses are reproduced from the pinned handlers.

The runtime now routes only the recognized reserved-condition BLX group through the native path. Other reserved opcodes, HLE BIOS/DLDI operations, SWIs and unknown instructions retain their existing interpreter routing.

### Existing stage-2 coverage retained

Non-PC ARM data processing and shifts, and THUMB ALU/shift/SP-adjustment operations remain native. The previously documented RSCS and THUMB NEG flag quirks remain intentionally equivalent to the interpreter. Original `interpreter_alu.cpp`, `interpreter_branch.cpp` and `interpreter_transfer.cpp` were not modified.

## Exit contract and pipeline correctness

`JitContext` now includes fixed-offset `resumePC` (offset 76) and `exitKind` (offset 80); PPC32 total size remains 96 bytes. Layout assertions cover these fields.

- `exitKind=0`: sequential leaf; preserve the already-advanced pipeline.
- `exitKind=1`: native code stores a raw target using r11 and the branch-exit marker using r12.
- Cycles still return in r3 through the existing ABI trampoline.
- The C++ bridge commits guest registers and CPSR, puts the requested target in guest r15 and calls the **original `Interpreter::flushPipeline()` once**.
- Generated code performs no guest-memory reads or helper calls. All target fetches, mapping choices, read side effects and PC bias remain in the original refill implementation.

This avoids duplicating guest fetch logic in the JIT. It also preserves prefetched bytes for sequential execution rather than indiscriminately flushing after every native instruction.

The existing scheduler loops, ARM7/ARM9 clock conversions and single-op overshoot granularity are unchanged. Full frame/event-queue equivalence has not been exercised on a running emulator; the pipeline harness checks the execution boundary rather than the complete Core.

## Cache identity changed — important integration requirement

The cache now compares **exact already-prefetched opcode + full 32-bit guest PC + CPU identity + ISA state** on every lookup. The opcode alone is no longer sufficient because relative targets and link addresses embed PC constants.

The hash may collide, including for PCs differing in their high bit; equality checks compare all fields. Tests explicitly alternate the same branch opcode at low/high addresses to reject incorrect target reuse.

There are still no cached host mapping pointers, successor instructions or native linked-target pointers. Every call first goes through the interpreter's existing fetch/prefetch path. A changed prefetched opcode selects a different entry; an old already-prefetched opcode retains its old semantics. Because each entry contains only the exact validated instruction, mapping changes and mirrored writes do not invalidate an embedded memory assumption in this design.

**This is not the final page-invalidation framework.** Address-specialized *multi-instruction* blocks and native memory paths must not be added by assuming this single-op guard covers them. Memory/DMA/remap invalidation hooks and page buckets remain outstanding.

The fixed arena remains 4 MiB for either emulation mode, allocated once through MEM2; the requested 2 MiB GBA sizing policy is still deferred. Published ranges use D-cache flush before I-cache invalidation. Hardware coherence remains unverified.

## Differential mode strengthened

`JIT_DIFFTEST=1` now supports branches as well as pure ALU/multiply operations:

1. Execute native code on the snapshot. It computes only registers, flags, cycles and a refill request.
2. Execute the actual original handler once on the unchanged CPU. **Only this reference execution performs real branch refill reads in differential mode.**
3. Compare r0-r15, CPSR, returned cycles and whether a refill occurred. Keep the reference CPU/pipeline state.
4. Log the first mismatch and latch native execution off for the session.

A diagnostic-only `jitFlushCount` in Interpreter counts calls to the original refill. It is compiled out unless differential testing is enabled and JIT is included; it is not serialized into save states.

Why compare the refill count? A branch to the next instruction can have the same post-handler r15 as sequential execution. Registers, flags and cycles alone would not necessarily reveal a missing refill. A dedicated fault-injection test deliberately constructs that case and confirms detection.

The log now includes native/reference exit kinds and raw-native/aligned-reference target information, in addition to PC, opcode, registers, CPSR and cycles. The path remains `sd:/noods/jit_log.txt`. Log-open failure still disables native execution rather than allowing a known mismatch to continue.

## Verified results

### Encoder and expression probes

- 121 independently assembled PPC encoding vectors: passed.
- Writer bounds/range checks: passed.
- 324,576 existing arithmetic/flag expression probes: passed.
- PPC source syntax check with warnings treated as errors: passed.

### Native execution against actual interpreter sources

The oracle now compiles the **actual unchanged ALU and branch translation units**, substituting their core include for a test include. Dispatch-derived reference calls are generated from the real tables. The isolated branch oracle records the raw target and models PC normalization without reading memory; the separate pipeline harness uses the exact real refill body.

Per oracle build:

- **2,371,944 state comparisons passed.**
- Exhaustive THUMB acceptance sweep: **32,419 unique encodings accepted** out of 65,536. This includes the ALU, multiply and branch subset—not native memory coverage.
- **36,133 ARM candidates accepted** across the generated/random/targeted corpus; this is a candidate count, not a count of all distinct legal ARM instructions.
- Exact registers, CPSR, cycles, branch requests and raw targets compared.
- ABI probe around every execution checks r14-r31, r2/r13 and the full CR; ordinary return exercises LR restoration.
- Targeted multiply/branch cases are required to compile natively; they cannot silently pass by falling back. A THUMB coverage floor detects regressions.
- Dedicated multiply tests cover signed timing thresholds, accumulation carry and source/destination/long-result aliases.
- Branch cases include all THUMB conditions/offset encodings, low/high/wrapping PCs, halfword-aligned Thumb PCs, LR/PC register operands and ARM7/ARM9 differences.
- A 4 KiB test arena forced **128 flushes**, with 32,637 hits, 16,563 misses, 16,431 compilations and 132 rejections in the cache stress sequence.

Ran the same corpus with the oracle compiled by PPC Linux GCC, and again by **devkitPPC 16.1.0 using the port's optimization/ABI flags**. Both passed. These are two builds of the same corpus, not twice as many unique states.

### Pipeline integration and fault handling

**84,528 step comparisons passed in each of five configurations:**

1. Native runtime switching off/on.
2. `JIT_DIFFTEST=1` with a register/cycle mismatch injection at the end.
3. Differential mode with the missing-refill-only injection at the end.
4. Build-time JIT exclusion.
5. Simulated code-allocation failure.

The harness extracts the exact runOpcode/getOpcode16/getOpcode32/flushPipeline bodies and condition table, and links the actual branch handlers. Memory is a small instrumented mirrored backend; exceptions, unknown instructions, non-ALU/non-branch operations and non-BLX reserved handling remain instrumented fallbacks rather than the full emulator implementation.

Checks include pipeline slots, r15, pcData, read counts and address traces, branch-target refills, mode switches, banked pointers, reserved BLX routing, both halves of BL/BLX pairs, page crossings and stale prefetched bytes. The harness also verifies that differential faults retain the reference result and disable subsequent native attempts, and that allocation failure produces correct interpreter fallback.

One test-harness issue was corrected during expansion: including the actual branch unit initially routed the deliberately instrumented SWI cases into the aborting exception stub. SWI/SWI-T were restored to the harness's instrumented fallback table; production SWI routing was not changed. Reported passes use the corrected harness.

### Clean Wii builds

| Configuration | DOL bytes | ELF/DOL entry | Result |
|---|---:|---|---|
| JIT included, ini default off | 1,341,064 | `0x80003f00` | Passed |
| `JIT_DIFFTEST=1` | 1,343,272 | `0x80003f00` | Passed |
| `NOODS_JIT=0` | 1,318,216 | `0x80003f00` | Passed |

Compile, link, elf2dol, DOL structural checks and symbol checks passed. The excluded build has no JIT implementation objects or Jit/JitPpc/jitEnter symbols. The Makefile still names the output `NooDS-Wii.dol`; it can be renamed to `boot.dol` by the user. This bundle is source-only.

Exact evidence is in `verification/stage3/`. Previous stages' logs and reports remain historical, not current status claims.

## Written but not target-runtime verified

- Actual Wii execution of native multiply/branch paths and physical instruction-cache coherence.
- Full-ROM differential runs, real BIOS/HLE paths, full memory/MMIO/event-queue behavior and long-run stability.
- On-console ini enablement and logging behavior.
- Performance/FPS and code-cache pressure in games.

No Dolphin, Wii or GameCube hardware test was run. Gekko-compatible emitted instructions do not establish a GameCube platform build; surrounding platform/allocator support is still Wii-specific.

## Remaining acceptance criteria

| Requirement | Current status |
|---|---|
| Non-PC ARM ALU/shifts | Native; tested |
| THUMB ALU/shifts/MUL | Native, except existing PC-source high-register/address-generation fallbacks |
| Basic ARM short/long multiply families | Native; tested |
| ARM9 DSP multiply extensions | Interpreter fallback |
| ARM/THUMB branch families | Native; tested, refill delegated to original C++ |
| PC-writing data-processing/SPSR exits | Interpreter fallback |
| Native loads/stores, PUSH/POP, LDM/STM, doubleword transfers | Not implemented |
| Multi-instruction blocks, budgets and chaining | Not implemented |
| Alias-aware page invalidation and remap/reset/state-load hooks | Not implemented for the final block design |
| 2 MiB GBA / 4 MiB NDS arena policy | Fixed 4 MiB currently |
| Both off-switches | Implemented; test/build verified |
| General side-effectful memory lockstep | Not implemented |
| VBA-GX native coverage parity / superior performance | **Not achieved** |
| GameCube platform build | Not implemented |

Single-instruction entry/exit still snapshots/spills the guest register set and pays the ABI save/restore cost. It may be slower than the interpreter. Default-off remains intentional.

## Apply and reproduce

Choose either the cumulative patch from pinned upstream, the incremental patch from stage 2, or the full file overlay. Do not apply multiple alternatives. Required headers/generated include and existing-file integration changes must accompany the new `.cpp` files.

**Always `make clean` after applying this update**, editing headers or changing build flags. The diagnostic Interpreter layout and cache/compiler signatures changed. Upstream still lacks complete header/flag dependency tracking.

```sh
bash tools/run_jit_asm_oracle.sh
bash tools/run_jit_flag_probe.sh
bash tools/run_jit_native_tests.sh
bash tools/run_jit_pipeline_tests.sh
source /home/user/wii-env.sh
JIT_ORACLE_DEVKIT=1 bash tools/run_jit_native_tests.sh
make clean && make -j4
make clean && make JIT_DIFFTEST=1 -j4
make clean && make NOODS_JIT=0 -j4
```

Runtime enablement is still a newline-terminated `jitEnabled=1` line in `noods.ini`; default is 0. Prefer a differential build for initial experimental console testing and inspect the first-mismatch log. Set `jitEnabled=0` or use the compiled-out build to disable. These are testing instructions, not evidence of a console run.

## Next preferred order

1. Design and test bounded multi-instruction ALU/multiply prefixes to amortize the per-op entry cost. Preserve the exact prefetch state and respect both the next event and the other CPU's next scheduling opportunity. Emit ARM condition checks inside any prefix; terminate at branches initially.
2. Before caching successor bytes or native memory pointers, add validated code-span identity and alias-aware invalidation/remap/reset/state-load handling. Do not treat today's one-op opcode guard as a multi-op SMC solution.
3. Add native memory/stack/block-transfer families with side-effect-aware exits and a memory-isolated/replayable differential strategy. Do not double-execute MMIO.
4. Finish remaining ARM PC/SPSR/DSP cases, run full-emulator differential ROM tests, then Dolphin/hardware validation and profiling.

GPL license/attributions are retained. Stage-1 and stage-2 reports are archived as `WORK_REPORT_STAGE1.md` and `WORK_REPORT_STAGE2.md`.
