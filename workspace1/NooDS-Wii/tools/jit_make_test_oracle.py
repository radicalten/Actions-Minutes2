#!/usr/bin/env python3
"""Build a test-only oracle from the ACTUAL interpreter ALU and branch sources.
Only its core.h include is substituted. Handler bodies/macros remain unchanged.
The isolated refill records raw targets and models PC normalization, without reads.
The pipeline harness instead links the exact real refill body. Exceptions abort.
"""
import pathlib,re,sys,json
root=pathlib.Path(__file__).resolve().parent.parent
out=pathlib.Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
alu=(root/'NooDS-Wii/interpreter_alu.cpp').read_text()
branch=(root/'NooDS-Wii/interpreter_branch.cpp').read_text()
header='#include "'+str(root/'NooDS-Wii/jit.h')+'"\n#define private public\n#include "'+str(root/'NooDS-Wii/interpreter.h')+'"\n#undef private\n'
(out/'actual_alu.cpp').write_text(alu.replace('#include "core.h"',header))
(out/'actual_branch.cpp').write_text(branch.replace('#include "core.h"',header))
lookup=(root/'NooDS-Wii/interpreter_lookup.cpp').read_text()
explicit=set(re.findall(r'int Interpreter::(\w+)\(uint(?:16|32)_t opcode',alu+branch))
for name in re.findall(r'^ALU_FUNCS\((\w+),',alu,re.M):
 for suffix in ['Lli','Llr','Lri','Lrr','Ari','Arr','Rri','Rrr','Imm']:explicit.add(name+suffix)
s=header+'''#include <cstdlib>
Interpreter::Interpreter(Core *core, bool arm7): core(core), arm7(arm7) {
    for (unsigned i=0;i<32;i++) registers[i]=&registersUsr[i&15];
}
#ifndef JIT_PIPELINE_TEST
static JitContext *activeReference;
void Interpreter::flushPipeline() {
    activeReference->resumePC=*registers[15]; ++activeReference->exitKind;
    if(cpsr&32) *registers[15]=(*registers[15]&~1u)+2;
    else *registers[15]=(*registers[15]&~3u)+4;
}
#endif
int Interpreter::exception(uint8_t) { std::fputs("ORACLE_UNEXPECTED_EXCEPTION\\n",stderr); std::abort(); }
void Interpreter::setCpsr(uint32_t,bool) { std::fputs("ORACLE_UNEXPECTED_SET_CPSR\\n",stderr); std::abort(); }
'''
for mode,bits,mask in [('arm',32,'((op>>16)&0xFF0)|((op>>4)&15)'),('thumb',16,'(op>>6)&1023')]:
 body=re.search(r'Interpreter::'+mode+r'Instrs\[\]\)\(uint'+str(bits)+r'_t\)\s*=\s*\{(.*?)\};',lookup,re.S).group(1)
 names=re.findall(r'&Interpreter::(\w+)',re.sub(r'//[^\n]*','',body))
 groups={}
 for i,name in enumerate(names):
  if name in explicit:groups.setdefault(name,[]).append(i)
 s+=f'static int {mode}Reference(Interpreter &cpu,uint32_t op) {{\n switch ({mask}) {{\n'
 for name,indices in groups.items():
  s+=' '.join(f'case {i}:' for i in indices)+f' return cpu.{name}(op);\n'
 s+='default: return -1;\n }\n}\n'
s+='''extern "C" int jitReference(JitContext *context,uint32_t opcode,bool thumb,bool arm7) {
    Interpreter cpu(nullptr,arm7);
    for(unsigned i=0;i<16;i++) *cpu.registers[i]=context->regs[i];
    cpu.cpsr=context->cpsr;
#ifndef JIT_PIPELINE_TEST
    activeReference=context; context->exitKind=0; context->resumePC=0;
#endif
    int result=thumb?thumbReference(cpu,opcode):((opcode>>28)==15 ?
        (((opcode&0x0E000000)==0x0A000000)?cpu.blx(opcode):-1):armReference(cpu,opcode));
    for(unsigned i=0;i<16;i++) context->regs[i]=*cpu.registers[i];
    context->cpsr=cpu.cpsr; context->cycles=result;
    return result;
}
'''
(out/'reference.cpp').write_text(s)
print('ORACLE_GENERATED: actual interpreter ALU/branch sources, dispatch-derived reference calls')
