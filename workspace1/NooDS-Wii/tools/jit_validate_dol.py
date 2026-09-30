#!/usr/bin/env python3
"""Basic DOL structural validation (not an execution test)."""
import pathlib, struct, sys
p=pathlib.Path(sys.argv[1]); data=p.read_bytes()
if len(data)<256: raise SystemExit('short DOL')
header=struct.unpack_from('>64I',data)
sections=[]
for i in range(18):
    offset=header[i]; address=header[18+i]; size=header[36+i]
    if not size: continue
    if offset<256 or offset+size>len(data): raise SystemExit(f'section {i} outside file')
    if address+size>0x100000000: raise SystemExit(f'section {i} wraps address space')
    sections.append((offset,offset+size,address,address+size,i))
for a,b in zip(sorted(sections),sorted(sections)[1:]):
    if a[1]>b[0]: raise SystemExit('overlapping file sections')
entry=header[56]
if entry!=0x80003F00: raise SystemExit(f'unexpected entry {entry:08x}')
if not any(a<=entry<b and i<7 for _,__,a,b,i in sections): raise SystemExit('entry outside text')
print(f'DOL_STRUCTURE_OK: {len(sections)} sections; entry=0x{entry:08x}; size={len(data)} bytes')
