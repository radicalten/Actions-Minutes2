#!/usr/bin/env python3
"""Use exact runOpcode/getOpcode source with a small test memory backend.
This tests integration boundaries, NOT the full Core/event queue/MMIO system.
"""
import pathlib,re,sys
root=pathlib.Path(__file__).resolve().parent.parent
out=pathlib.Path(sys.argv[1])
lookup=(root/'NooDS-Wii/interpreter_lookup.cpp').read_text()
interpreter=(root/'NooDS-Wii/interpreter.cpp').read_text()
alu=(root/'NooDS-Wii/interpreter_alu.cpp').read_text()
methods=set(re.findall(r'int Interpreter::(\w+)\(uint(?:16|32)_t opcode',alu))
for name in re.findall(r'^ALU_FUNCS\((\w+),',alu,re.M):
 for suffix in ['Lli','Llr','Lri','Lrr','Ari','Arr','Rri','Rrr','Imm']:methods.add(name+suffix)
s='#include "'+str(root/'NooDS-Wii/jit.h')+'"\n'
s+='#define private public\n#include "'+str(root/'NooDS-Wii/interpreter.h')+'"\n#undef private\n'
s+='#include "'+str(root/'NooDS-Wii/settings.h')+'"\n'
s+='#include "'+str(root/'NooDS-Wii/jit_cache.h')+'"\n'
s+='''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
struct TestMemory {
 alignas(32) uint8_t bytes[8192] = {};
 uint8_t *readMap9A[2] = {bytes,bytes+4096};
 uint8_t *readMap7[2] = {bytes,bytes+4096};
 unsigned reads=0;
 template<typename T> T read(bool,uint32_t address) {
  ++reads; if(address+sizeof(T)>sizeof(bytes)) std::abort();
  T v=0; for(unsigned i=0;i<sizeof(T);i++) v|=uint32_t(bytes[address+i])<<(8*i);
  return v;
 }
};
class Core { public: TestMemory memory; };
int Settings::jitEnabled=0;
static unsigned fallbackCalls=0,reservedCalls=0;
int Interpreter::unkArm(uint32_t opcode) { ++fallbackCalls; *registers[0]^=opcode; return 3; }
int Interpreter::unkThumb(uint16_t opcode) { ++fallbackCalls; *registers[0]^=opcode; return 3; }
int Interpreter::handleReserved(uint32_t) { ++reservedCalls; return 7; }
'''
for mode,bits,length in [('arm',32,4096),('thumb',16,1024)]:
 body=re.search(r'Interpreter::'+mode+r'Instrs\[\]\)\(uint'+str(bits)+r'_t\)\s*=\s*\{(.*?)\};',lookup,re.S).group(1)
 names=re.findall(r'&Interpreter::(\w+)',re.sub(r'//[^\n]*','',body));assert len(names)==length
 fallback='unkArm' if mode=='arm' else 'unkThumb'
 s+=f'int (Interpreter::*Interpreter::{mode}Instrs[{length}])(uint{bits}_t) = {{\n'
 s+=''.join('&Interpreter::'+(name if name in methods else fallback)+',\n' for name in names)+'};\n'
m=re.search(r'const uint8_t Interpreter::condition\[256\]\s*=\s*\{.*?\};',lookup,re.S);s+=m.group(0)+'\n'
for prefix in ['FORCE_INLINE int Interpreter::runOpcode()', 'uint16_t Interpreter::getOpcode16()', 'uint32_t Interpreter::getOpcode32()']:
 start=interpreter.index(prefix);brace=interpreter.index('{',start);depth=1;end=brace+1
 while depth:
  if interpreter[end]=='{':depth+=1
  elif interpreter[end]=='}':depth-=1
  end+=1
 s+=interpreter[start:end]+'\n'
s+=(root/'tests/jit_pipeline_test.inc').read_text()
(out/'pipeline.cpp').write_text(s)
print('PIPELINE_SOURCE_OK: exact runOpcode/getOpcode bodies and condition table; instrumented memory/fallbacks')
