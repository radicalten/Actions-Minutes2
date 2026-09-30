#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 tools/jit_generate_decode.py --check
python3 tools/jit_make_test_oracle.py "$work"
oracle=powerpc-linux-gnu-g++
oracle_flags=(-O2 -mcpu=750 -DENDIAN_BIG -ffunction-sections -fdata-sections)
if [ "${JIT_ORACLE_DEVKIT:-0}" = 1 ]; then
    oracle=powerpc-eabi-g++
    oracle_flags+=(-meabi -mhard-float -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions)
fi
echo "ORACLE_COMPILER: $oracle"
"$oracle" "${oracle_flags[@]}" -c "$work/actual_alu.cpp" -o "$work/actual_alu.o"
"$oracle" "${oracle_flags[@]}" -c "$work/actual_branch.cpp" -o "$work/actual_branch.o"
powerpc-linux-gnu-g++ -std=c++11 -O2 -static -mcpu=750 -DENDIAN_BIG -DJIT_TEST -DJIT_CACHE_BYTES=4096 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    tests/jit_native_test.cpp tests/jit_abi_probe.s "$work/actual_alu.o" "$work/actual_branch.o" "$work/reference.cpp" \
    NooDS-Wii/jit_ppc_emitter.cpp NooDS-Wii/jit_compiler_branch.cpp NooDS-Wii/jit_compiler_multiply.cpp NooDS-Wii/jit_compiler_common.cpp \
    NooDS-Wii/jit_compiler_arm.cpp NooDS-Wii/jit_compiler_thumb.cpp \
    NooDS-Wii/jit_trampoline.cpp NooDS-Wii/jit_cache.cpp -o "$work/native"
qemu-ppc-static -cpu 750 "$work/native"
