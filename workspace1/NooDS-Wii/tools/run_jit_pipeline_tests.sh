#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 tools/jit_make_test_oracle.py "$work"
python3 tools/jit_make_pipeline_test.py "$work"
mkdir -p "$work/sd:/noods"
for mode in native diff refill-fault disabled oom; do
    flags=()
    sources=()
    if [ "$mode" = disabled ]; then
        flags+=(-DNOODS_NO_JIT)
    else
        sources+=(NooDS-Wii/jit.cpp NooDS-Wii/jit_cache.cpp NooDS-Wii/jit_debug.cpp
            NooDS-Wii/jit_ppc_emitter.cpp NooDS-Wii/jit_trampoline.cpp
            NooDS-Wii/jit_compiler_branch.cpp NooDS-Wii/jit_compiler_multiply.cpp NooDS-Wii/jit_compiler_common.cpp NooDS-Wii/jit_compiler_arm.cpp NooDS-Wii/jit_compiler_thumb.cpp)
    fi
    if [ "$mode" = diff ]; then flags+=(-DJIT_DIFFTEST); fi
    if [ "$mode" = refill-fault ]; then flags+=(-DJIT_DIFFTEST -DJIT_TEST_REFILL_FAULT); fi
    if [ "$mode" = oom ]; then flags+=(-DJIT_TEST_ALLOC_FAIL); fi
    powerpc-linux-gnu-g++ -std=c++11 -O2 -static -mcpu=750 -DENDIAN_BIG -DJIT_TEST -DJIT_PIPELINE_TEST \
        -ffunction-sections -fdata-sections -Wl,--gc-sections "${flags[@]}" \
        "$work/pipeline.cpp" "$work/actual_alu.cpp" "$work/actual_branch.cpp" "$work/reference.cpp" "${sources[@]}" -o "$work/$mode"
    echo "PIPELINE_MODE: $mode"
    (cd "$work"; rm -f "sd:/noods/jit_log.txt"; qemu-ppc-static -cpu 750 "./$mode")
done
