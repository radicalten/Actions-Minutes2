#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CXX:-g++} -std=c++11 -Wall -Wextra -Werror -pedantic tools/jit_asm_oracle.cpp NooDS-Wii/jit_ppc_emitter.cpp -o "$work/oracle"
"$work/oracle" > "$work/vectors"
python3 - "$work" <<'PY'
import sys, pathlib, subprocess, struct
p=pathlib.Path(sys.argv[1]); rows=[r.split('|') for r in (p/'vectors').read_text().splitlines()]
(p/'oracle.s').write_text('.text\n'+ '\n'.join(r[0] for r in rows)+'\n')
subprocess.run(['powerpc-linux-gnu-as','-mgekko','-mbig','-o',str(p/'oracle.o'),str(p/'oracle.s')],check=True)
subprocess.run(['powerpc-linux-gnu-objcopy','-O','binary','--only-section=.text',str(p/'oracle.o'),str(p/'oracle.bin')],check=True)
data=(p/'oracle.bin').read_bytes()
assert len(data)==len(rows)*4, (len(data),len(rows))
failures=[]
for i,(asm,expected) in enumerate(rows):
    actual=struct.unpack_from('>I',data,4*i)[0]
    if actual!=int(expected,16): failures.append(f'{asm}: assembler={actual:08x}, emitter={expected}')
if failures: raise SystemExit('\n'.join(failures))
print(f'ASM_ORACLE_OK: {len(rows)} independently assembled vectors')
PY
