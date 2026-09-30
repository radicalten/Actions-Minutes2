# NooDS-Wii JIT — stage 2 source bundle

**Experimental, default-off native ARM/THUMB ALU execution. Not the finished full-coverage recompiler.**

Start with `WORK_REPORT.md`: it separates tested results, hardware-unverified work and remaining requirements. This replaces the stage-1 foundation-only delivery; the interpreter is now connected to a native PPC path for supported non-PC ALU instructions.

## Apply ONE alternative

Back up local edits and work on a feature branch.

### Clean pinned upstream checkout

Base: `1c995b48c37ebf3645646968c416958f79264137`.

```sh
git switch -c feature/ppc-jit-stage2
git apply --check /path/to/from-upstream.patch
git apply /path/to/from-upstream.patch
```

### Checkout with the exact stage-1 bundle already applied

```sh
git apply --check /path/to/from-stage1.patch
git apply /path/to/from-stage1.patch
```

### File overlay

Copy the contents of `overlay/` into the NooDS-Wii repository root, preserving relative paths and replacing the listed existing files. This works over either the pinned upstream or the unmodified stage-1 overlay. Do not also apply a patch.

The test logs in patches can produce harmless trailing-whitespace warnings. They are recorded tool output; use normal patch context checks to detect real mismatches.

## Important files

Production source is in `overlay/NooDS-Wii/`:

- `jit.cpp`, `jit_cache.cpp`, `jit_trampoline.cpp`, `jit_debug.cpp`
- `jit_compiler_arm.cpp`, `jit_compiler_thumb.cpp`, `jit_compiler_common.cpp`
- `jit_ppc_emitter.cpp`
- All corresponding headers plus the generated **`jit_decode_arm.inc`**
- Updated `interpreter.cpp/.h` and `settings.cpp/.h`

The root Makefile change is required, especially for `NOODS_JIT=0`. `.cpp` files alone are not enough. Keep test programs in `tools/` and `tests/`: they contain standalone mains and must not be added to the emulator's source glob.

## Build / enable

With devkitPPC/libogc and the original project's port libraries installed:

```sh
source /home/user/wii-env.sh
make clean && make -j4
```

The artifact is `NooDS-Wii.dol`; rename your built copy to `boot.dol` if needed for HBC. No DOL is included in this source-only bundle.

The native path is **off by default**. For experimental testing, add/update this newline-terminated setting in `noods.ini`:

```ini
jitEnabled=1
```

Prefer a clean `make JIT_DIFFTEST=1` build for initial console bring-up. Its first detected native/reference mismatch is logged to `sd:/noods/jit_log.txt`, the reference result is retained and native execution is disabled for that session.

Disable with `jitEnabled=0`, or clean-build `make NOODS_JIT=0`. Always clean after changing headers or build flags.

## Re-run tests

Dependencies: Python 3, host g++, powerpc-linux-gnu binutils/g++, and qemu-ppc-static. The additional devkit oracle pass requires powerpc-eabi-g++ on PATH.

```sh
bash tools/run_jit_asm_oracle.sh
bash tools/run_jit_flag_probe.sh
bash tools/run_jit_native_tests.sh
bash tools/run_jit_pipeline_tests.sh
JIT_ORACLE_DEVKIT=1 bash tools/run_jit_native_tests.sh
```

The source extraction tests use real interpreter ALU handlers, but the pipeline harness has instrumented memory/fallbacks rather than the full emulator Core. Passing these tests is not a hardware/ROM test.

## Other contents

- `WORK_REPORT.md`: current status, coverage, design limitations and next steps.
- `overlay/docs/jit/WORK_REPORT_STAGE1.md`: historical foundation report.
- `overlay/verification/stage2/`: clean build and test evidence.
- `patch-bases.json`, `commits.txt`: exact source provenance.
- `SHA256SUMS`: checksums for all bundle files except itself.
- `setup/`: prior environment exports and the official Docker Hub toolchain extraction script/manifest. These are optional sandbox setup aids, not required changes to your repo. The bootstrap script expects `/home/user/dkp-work/` to exist and does not copy the extracted toolchain into `/opt` itself. Its `latest` tag can change; the manifest identifies this run's layers.
- `LICENSE`: upstream GPL license. New source uses GPL-3.0-or-later identifiers.

**Limitations:** one instruction per native entry; no native memory/branch/multiply families; no address-specialized blocks or page invalidation framework; no GameCube platform build; no measured speedup; no Dolphin/Wii/GCN hardware run. This stage can be slower than the interpreter and remains a correctness/bring-up milestone.
