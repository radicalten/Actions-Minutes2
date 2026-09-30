#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
powerpc-linux-gnu-g++ -std=c++11 -O1 -static -mcpu=750 -Wall -Wextra -Wno-sign-compare tests/jit_flag_probe.cpp NooDS-Wii/jit_ppc_emitter.cpp -o "$work/flags"
qemu-ppc-static -cpu 750 "$work/flags"
