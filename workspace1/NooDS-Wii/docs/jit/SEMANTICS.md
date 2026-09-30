# Interpreter semantics review — pinned source, not a finished specification

> Historical stage-1 review. Stage 3 adds multiply/branch leaves and keys the
> cache by exact prefetched opcode + full PC + CPU + ISA. See WORK_REPORT.md
> for current implementation and verification status.

Target: radicalten/NooDS-Wii commit `1c995b48c37ebf3645646968c416958f79264137`.
Comparison: dborth/vbagx commit `5db68c4e1c908b0c79b5f589b7a82e88a3dff1b2`.

This review is partial. The generated `source-return-index.md` is a searchable index, **not** a complete, annotated cycle table. Neither this document nor the encoder implements a guest translator.

## Pipeline and fallback boundary (source-level)

`Interpreter::runOpcode` first saves `opcode = pipeline[0]`, then moves `pipeline[1]` into `pipeline[0]`, increments r15 and fetches a new `pipeline[1]`.

| Boundary | ARM r15 | THUMB r15 | pipeline[0] |
|---|---|---|---|
| Before runOpcode, about to execute A | A+4 | A+2 | opcode at A |
| Inside handler for A | A+8 | A+4 | prefetched opcode at A+4 / A+2 |

Thus the briefing's assertion that pipeline[0] is the executing instruction is not correct **inside a handler** in this revision. A fallback must enter at one defined boundary, not advance the pipeline twice. Calling `runOpcode` requires the pre-runOpcode state. Calling a handler directly would require the post-fetch state and preserving condition/reserved handling separately.

`flushPipeline` takes a target in r15, aligns it, adds one instruction width, and fills the two slots. It therefore produces the pre-runOpcode state above. Rebuilding the pipeline at every sequential JIT exit needs extra review: a store may have changed already-prefetched instruction bytes, while the interpreter would keep those old bytes. Blindly flushing after every store can diverge from the interpreter even with perfect code-cache invalidation.

ARM condition dispatch: `condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]`: false costs 1, reserved calls `handleReserved`, otherwise selects the ARM dispatch table. Reserved includes BLX, HLE BIOS IRQ return, and DLDI operations. Do not compile reserved conditions as unconditional ordinary ARM instructions.

## Dispatch inventory (verified extraction)

- ARM: 4,096 entries, 525 distinct handler names, 1,237 contiguous name ranges.
- THUMB: 1,024 entries, 78 distinct handler names, 81 contiguous name ranges.
- THUMB index is `(opcode >> 6) & 0x3FF`.
- ARM index is `((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)`.
- Generated range files and JSON preserve the actual table order. Extraction asserts exact table lengths.
- THUMB `unkThumb` regions must remain fallbacks. `blxOffT` and `blxRegT` are present; CPU-specific restrictions must be retained.

## Flags (source-level plus emitted-code probes)

Use `subfc d, op2, op1` for op1-op2. PPC CA then matches ARM no-borrow carry, with no inversion. Use `addc`/`adde`/`subfc`/`subfe` with OE for combined carry and overflow. XER CA bit 29 maps to CPSR C directly; OV bit 30 rotates by 30 into CPSR V bit 28. CR0 LT maps to N, EQ rotates by 1 into Z.

`tests/jit_flag_probe.cpp` executes emitted instruction words under qemu-ppc `-cpu 750`, compares values and all CPSR bits against copied interpreter expressions, and preserves only volatile host state. No inline-assembly constraints are used. It covers ADDS, ADCS, SUBS, SBCS, RSBS and RSCS expressions, not PC destinations, shifter behavior, bank changes or actual instruction dispatch.

Important source quirk: `Interpreter::rscs` clears `0xC0000000`, not `0xF0000000`. Old C/V are OR-ed with new C/V. The probe intentionally reproduces this. A translator promising interpreter equivalence must preserve it or fall back. Fixing the interpreter to architectural behavior is a separate change, not part of this foundation.

S-form r15 destinations may restore SPSR and bank pointers; do not keep stale register pointers over this transition.

## Timing review (incomplete)

- Ordinary ARM data processing: 1; PC destination: 3. Register-specified shifter wrappers add 1 to the underlying handler result, including PC cases.
- ARM condition false: 1.
- B/BL/BX: 3. ARM9 BLX: 3; ARM7 BLX handlers return 1 without the ARM9 operation.
- THUMB conditional B: 1 not taken, 3 taken.
- Basic ARM byte/halfword/word loads to ordinary registers: `(arm7 << 1) + 1`, i.e. ARM9 1 / ARM7 3. Representative PC destinations return 5, but mode switching differs by load family: LDR/LDRB may set T on ARM9; LDRH/LDRSH/LDRSB shown in this revision do not do the same T-bit update.
- Basic stores: `arm7 + 1`, i.e. ARM9 1 / ARM7 2.
- ARM MUL/MLA: ARM9 2. ARM7 uses a signed significance loop, then `m+1` / `m+2`. Do **not** substitute the briefing's unsigned three-comparison ramp.
- THUMB MUL: ARM9 **4** in this source, not the ARM MUL count of 2; ARM7 uses `m+1`.
- Long multiplies and block transfers require individual extraction and alias/order tests; not yet fully audited.

The run loops store CPU deadlines in the **global scheduling domain**: ARM9 handler returns are added directly; ARM7 handler returns are shifted left by one in NDS mode. GBA ARM7 uses shift zero. The briefing's wording about stored ARM7 cycles being a separate CPU-unit domain must not be used to redesign this interface. DSi has its own half-cycle accumulator and is a separate case.

In a dual-CPU run, respecting just the next peripheral-event deadline is insufficient: a JIT block must not run past the other CPU's next opportunity to execute if preserving the current interleaving is required.

## Memory, invalidation and aliases (source-level)

`Memory::read/write<T>` choose ARM7 maps or ARM9 TCM/non-TCM maps, then use `address & (0x1000 - sizeof(T))`. Guest bytes are little endian. The maps can be null; fallback methods contain I/O and other side effects.

Word load rotations are in interpreter handlers, not in Memory::read. ARM7 halfword rotation and signed halfword shifts are also handler behavior.

DMA uses `Memory::read/write(..., false)`; hook the non-TCM map path too. Save-state loading writes backing arrays and then remaps. Remapping must invalidate translations and any cached host-memory pointers.

A counter keyed only by virtual guest page is **not enough by itself**: for example NDS main RAM mapping uses `ram[address & 0x3FFFFF]`, creating virtual mirrors. Writes through another virtual page, through another CPU's mapping or a DMA non-TCM view can alter the same physical code. Need physical backing-page ownership/reverse aliases, or a demonstrably correct conservative invalidation scheme. A byte reference count must also never wrap to zero while code remains live.

Block identity must distinguish CPU, ISA state, guest address and relevant mapping generation. `(pc << 1) | thumb` in a 32-bit integer loses the top PC bit; a table can use it as a hash only if it also compares the complete identity. This matters for high BIOS addresses.

## ABI/arena work still required

Retain the requested r14-r28 guest register contract, r29 CPSR, r30 context, r31 interpreter. Preserve LR, all used nonvolatile GPRs and CR around native execution. Keep r0 outside the general scratch allocator. Offset macros need static assertions in a 32-bit PPC build.

The supplied Writer is a bounded **staging** writer. It allocates no executable arena and publishes nothing. A later arena implementation must perform aligned D-cache writeback followed by I-cache invalidation after all emission/patching. QEMU execution does not validate Wii cache coherence.

The current upstream build is Wii-specific and uses MEM2 and Wii APIs. GameCube support cannot be claimed merely because the instruction encoder targets Gekko-compatible instructions; GameCube allocation/platform integration and a separate build remain necessary.
