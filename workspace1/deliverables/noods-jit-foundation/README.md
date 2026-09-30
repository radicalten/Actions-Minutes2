# NooDS-Wii JIT foundation — partial delivery

**Not a finished JIT. No guest ARM/THUMB translation or runtime JIT integration is implemented.**

Read `WORK_REPORT.md` first for the exact verified/unverified/not-started breakdown.

## Contents

- `overlay/`: source, Makefile, tests, semantics notes and verification logs, laid out relative to the NooDS-Wii repository root.
- `foundation.patch`: equivalent patch against NooDS-Wii commit `1c995b48c37ebf3645646968c416958f79264137`.
- `WORK_REPORT.md`: work summary, results, known gaps and reproduction steps.
- `setup/`: toolchain bootstrap script, Docker layer manifest used, and environment exports. The script installs no system packages itself and extracts only the image's opt/ tree into `/home/user/dkp-work/stage`; copying that tree into `/opt/devkitpro` requires a separate authorized command.
- `commits.txt`: local feature-branch commits.
- `SHA256SUMS`: integrity hashes for bundle files.

## Apply (choose one; do not do both)

On a clean checkout at the pinned commit:

```sh
git switch -c feature/ppc-jit-foundation
git apply --check /path/to/foundation.patch
git apply /path/to/foundation.patch
```

Alternatively copy the contents of `overlay/` into the repository root. Back up your changes first; its Makefile is a complete file replacement.

Only `jit_ppc_emitter.cpp` and `jit_ppc_emitter.h` belong in the emulator source directory. `tools/jit_asm_oracle.cpp` and `tests/jit_flag_probe.cpp` have standalone test mains and must **not** be copied into `NooDS-Wii/`.

After applying, run the two shell test scripts under `tools/`, then clean-build the Wii app normally and with `NOODS_JIT=0`. See the report for tool dependencies. Both builds still execute the original interpreter. Runtime off-switch, native translators, trampoline, cache invalidation, lockstep and hardware validation remain outstanding.
