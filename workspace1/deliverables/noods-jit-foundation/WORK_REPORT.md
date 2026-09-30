# NooDS-Wii ARM/THUMB → PPC JIT: foundation delivery

## Status: partial — NOT the requested finished recompiler

This package does **not** add a functioning JIT to NooDS-Wii. It provides the first verification layer: a PPC encoder, bounded staging writer, independent assembler oracle, executable PPC flag probes, dispatch extraction, source review, and a build-time source exclusion switch. It has no translated guest instruction blocks, no runtime JIT dispatcher and no speedup. It does not yet meet or exceed VBA-GX functionality.

Do not deploy or describe this as a completed ARMv4/v5 recompiler. The emulator still runs its original interpreter. A source build passing is not a JIT correctness result.

## Sources and authorization

- NooDS-Wii base: `1c995b48c37ebf3645646968c416958f79264137`.
- VBA-GX comparison base: `5db68c4e1c908b0c79b5f589b7a82e88a3dff1b2`.
- Read the supplied AGENTS.md and SKILL.md before changes.
- The user explicitly authorized direct implementation in place of delegated coding; no agent CLI was installed and no API credentials were requested or used.
- Local feature branch: `feature/arm-ppc-jit`. No remote push or PR was made.
- No VBA-GX implementation was vendored. Interpreter arithmetic expressions in the probe come from the pinned GPL-licensed NooDS-Wii source. New source carries GPL-3.0-or-later identifiers; the original LICENSE accompanies the bundle.

## Files

### Additive production-source foundation

- `NooDS-Wii/jit_ppc_emitter.h`
- `NooDS-Wii/jit_ppc_emitter.cpp`

The encoder supports 32-bit 750-family arithmetic, carry/overflow variants, logical operations, shifts/rotates, comparisons, D-form and byte-reversed memory accesses, CR/SPR moves and relative/indirect branches. It has no AltiVec, isel, popcntb or 64-bit host instructions. The bounded Writer refuses overflow and invalid branch ranges, uses instruction-relative offsets, leaves invalid branch outputs untouched, and latches write failure. This is a staging buffer, not an executable code allocator.

### Tests and analysis

- `tools/jit_asm_oracle.cpp`: encoder vectors and writer boundary checks.
- `tools/run_jit_asm_oracle.sh`: assemble with GNU PPC binutils, extract raw .text and compare big-endian words. Does not parse objdump text.
- `tests/jit_flag_probe.cpp`: builds small functions from the emitter and actually executes them in a PPC Linux process under QEMU.
- `tools/run_jit_flag_probe.sh`: static PPC build and qemu-ppc 750 execution.
- `tools/jit_extract_semantics.py`: checks dispatch-table sizes and generates name ranges, JSON and a return-expression inventory.
- `tools/jit_validate_dol.py`: basic DOL section/entry validation; not a runtime test.
- `docs/jit/SEMANTICS.md`: reviewed pipeline, flags, timing, scheduling, memory/alias and ABI issues.
- `docs/jit/*dispatch*`, `source-return-index.md`: generated review aids.
- `verification/`: exact build/test logs and ELF/DOL check outputs.

### Existing-file change

`Makefile`: adds `NOODS_JIT ?= 1`. `make NOODS_JIT=0` excludes `NooDS-Wii/jit_*.cpp` and adds `-DNOODS_NO_JIT`. It does not add a runtime setting. Both modes currently use only the interpreter.

## Verified

1. Installed devkitPPC via the official `devkitpro/devkitppc` Docker Hub image using SHA-256-checked layers. Compiler reported **16.1.0**. Installed Debian PPC Linux binutils/compiler and qemu-user-static for independent probes.
2. Built the **unmodified baseline** successfully.
3. Encoder oracle: **121 independently assembled vectors passed**. Writer overflow/patch/range checks passed.
4. Generated PPC arithmetic/flag functions: **324,576 cases passed** against copied interpreter expressions. Cases include all pairs of 16 boundary operands with all 16 old NZCV combinations for each of six operations, plus 50,000 deterministic random cases per operation. Entire result words and CPSR words were compared.
5. Extracted 4,096 ARM dispatch entries / 525 distinct handlers and 1,024 THUMB entries / 78 distinct handlers.
6. Clean build with the new encoder included as an input object: compile, link and elf2dol succeeded.
7. Clean `make NOODS_JIT=0` build succeeded, with no encoder object produced and no JitPpc symbols in the executable.
8. Both resulting DOLs passed basic structural validation; ELF and DOL entry were **0x80003f00**. The Makefile names its artifact `NooDS-Wii.dol`, not `boot.dol`; it can be renamed for the Homebrew Channel. No binary is included in this source-only bundle.

The linker garbage-collects unused encoder functions in the default build because no runtime JIT calls them yet. Equal binary sizes for enabled/disabled foundation builds are therefore expected, not evidence that a runtime JIT switch works.

The initial oracle invocation used a GNU assembler target flag (`-m750`) unsupported by the installed assembler. It was corrected to `-mgekko`; the reported passing result is from the corrected script. The initial enabled-symbol check also stopped when the linker discarded unused encoder code; verification was corrected to inspect the input object and document the lack of runtime references.

## Source-level findings, not full emulator verification

See SEMANTICS.md for details:

- Handler-time `pipeline[0]` contains the next prefetched instruction, not the current opcode saved by runOpcode.
- Native execution must respect already-prefetched bytes when stores modify upcoming instructions; invalidating blocks alone does not settle pipeline equivalence.
- Timing differs from several shortcuts in the briefing. ARM9 THUMB MUL returns 4, while ARM MUL returns 2. ARM7 multiply timing uses the source's significance tests.
- Stored ARM7 deadlines are already in the global scheduling domain after the run loop applies `<< 1`.
- The pinned RSCS handler preserves old C/V before OR-ing computed C/V. The probe deliberately preserves this interpreter quirk.
- Several load families differ in whether a PC load updates T; a single universal PC-load rule is unsafe.
- Physical memory mirrors and cross-CPU aliases must be invalidated, not only the virtual address used by a write.
- The repo is a Wii build. Gekko-compatible instruction words alone do not establish GameCube platform support.

## Written but not verified on target hardware

- All encoder code is compiled for PPC and checked against GNU assembly, but Wii/GCN execution and actual D-cache/I-cache publication have not been tested.
- The source review identifies integration requirements; it is not a complete handler-by-handler cycle specification or proof of SMC correctness.
- DOL structure and entry checks do not prove the emulator boots or runs a ROM correctly.

## Not started / remaining acceptance criteria

| Requirement | Current status |
|---|---|
| ARM native translator: ALU/shifter, branches, transfers, LDM/STM, multiplies | Not implemented |
| THUMB native translator at VBA-GX parity | Not implemented |
| Runtime dispatcher, interpreter fallback boundary and HLE exits | Not implemented |
| ABI trampoline, context offsets, shared epilogue, register contract | Not implemented |
| Fixed 2/4 MB executable arena and PPC cache publication | Not implemented |
| Page buckets, alias-aware SMC invalidation, remap/reset/state-load hooks | Not implemented |
| Native memory accesses and slow-path side-effect exits | Not implemented |
| Exact block-cycle accounting, dual-CPU scheduling and budget exits | Not implemented |
| Runtime `Settings::jitEnabled` / noods.ini switch | Not implemented |
| Build-time `NOODS_JIT=0` exclusion | Implemented and build-tested |
| Emulator lockstep r0-r15/CPSR/cycles/memory differential tests | Not implemented |
| Wii hardware / Dolphin / GameCube tests | Not run |
| Before/after FPS / comparison against VBA-GX | Not measured |

No empty `jit_compiler_arm.cpp`, `jit_compiler_thumb.cpp`, `jit_cache.cpp` or trampoline stubs are supplied: those names would suggest an implementation that does not exist.

## Reproduce

From the pinned repository with the overlay applied:

```sh
bash tools/run_jit_asm_oracle.sh
bash tools/run_jit_flag_probe.sh
python3 tools/jit_extract_semantics.py
source /home/user/wii-env.sh
make clean && make -j4
python3 tools/jit_validate_dol.py NooDS-Wii.dol
make clean && make NOODS_JIT=0 -j4
python3 tools/jit_validate_dol.py NooDS-Wii.dol
```

The host tests require g++, Python 3, powerpc-linux-gnu-as/objcopy/g++, and qemu-ppc-static. Wii builds require devkitPPC/libogc and the port libraries used by the original repo. The supplied environment script exports paths; this repo already defines platform flags in its Makefile. `/opt/devkitpro` is not a persisted workspace deliverable: the bootstrap script and layer manifest are included separately for reproducibility. Re-running the bootstrap's `latest` tag can select newer toolchain versions; the archived manifest identifies the layers used for this run.

Always clean when switching NOODS_JIT or editing headers: the upstream Makefile does not track header dependencies or flag changes. No claim is made for a no-clean toggle.

## Recommended continuation order

1. Finish the handler-specific semantics/cycle inventory and define pipeline-preserving native/fallback boundaries, including self-modifying prefetched code.
2. Freeze JitContext offsets, register/spill contract, CPU identity and global-domain budget contract. Preserve the interpreter untouched behind an off-switch.
3. Implement the fixed arena, coherent publication, trampoline and a one-instruction native path; compare that path against the actual interpreter before extending blocks.
4. Add alias-aware invalidation before enabling native stores. Validate mirrored RAM writes, ARM7-to-ARM9 writes, DMA, TCM, VRAM remaps and save-state reloads.
5. Implement all required THUMB families and ARM families incrementally, deriving legality from the pinned dispatch tables. Use interpreter exits for unresolved corner cases.
6. Add lockstep with isolated/replayable memory effects and first-mismatch logging. Do not run MMIO twice against one live Core.
7. Verify both off-switches, Wii/Dolphin ROM tests, then GameCube platform support and hardware cache coherency. Measure performance only after correctness gates pass.
