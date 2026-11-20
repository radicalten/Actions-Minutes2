// Dedicated exact ARM7/IWRAM execution island (iteration 18).
// Compile-time ALU specialization; branches remain inside the execution island.
// Exact event/page guards and original memory-write fallback remain mandatory.
// No CPU mode, interrupt, ROM/prefetch or unsupported instruction crosses the island.
__attribute__((always_inline)) inline bool Interpreter::runGbaDedicated() {
    Core &c=*core;GbaBus &bus=c.gbaBus;
    uint32_t r15=*registers[15];
    if ((cpsr&0x20u) || (r15>>24)!=3 || (r15&3) || !pcData || bus.active ||
        c.globalCycles!=cycles || c.events.frontCycles<=cycles+1) return false;
    auto regs=registers;
    uint32_t clock=cycles,op=pipeline[0],next=pipeline[1];
    uint8_t *pc=pcData;unsigned done=0,last=0;bool branchSeen=false;
    while((r15&0xFFFu)!=0xFFCu) {
        const unsigned cond=condition[((op>>24)&0xF0u)|(cpsr>>28)];
        if (cond==2) break;
        unsigned type=0,cost=1;
        uint32_t address=0,target=0,dataCost=0;bool load=false,byte=false,link=false;
        if(cond) {
            if(GbaArmAlu::eligible(op))type=1;
            else if((op&0x0E000000u)==0x0A000000u || (op&0x0FFFFFF0u)==0x012FFF10u) {
                if((op&0x0E000000u)==0x0A000000u) {
                    target=r15+4+(uint32_t(int32_t(op<<8)>>6));link=op&0x01000000u;
                } else {
                    target=(op&15)==15?r15+4:*regs[op&15];
                    if(target&1)break;
                    target&=~3u;
                }
                if((target>>24)!=3 || (target&0xFFFu)>0xFF8u || !c.memory.readMap7[target>>12])break;
                cost=3;type=3;
            } else {
                if ((op&0x0D200000u)!=0x05000000u || ((op>>12)&15)==15 ||
                    ((op&0x02000000u) && (op&0x10u))) break;
                auto reg=[&](unsigned n){return n==15?r15+4:*regs[n];};
                uint32_t offset=op&0xFFFu;
                if(op&0x02000000u) {
                    const uint32_t v=reg(op&15);const unsigned n=(op>>7)&31;
                    switch((op>>5)&3) {
                    case 0:offset=v<<n;break;case 1:offset=n?v>>n:0;break;
                    case 2:offset=uint32_t(int32_t(v)>>(n?n:31));break;
                    default:offset=n?GbaArmAlu::ror(v,n):((cpsr&0x20000000u)<<2)|(v>>1);break;
                    }
                }
                address=reg((op>>16)&15);address=(op&0x00800000u)?address+offset:address-offset;
                load=op&0x00100000u;byte=op&0x00400000u;
                const unsigned region=address>>24;
                if(region!=2 && region!=3 && region!=6) break;
                if(!(load?c.memory.readMap7:c.memory.writeMap7)[address>>12]) break;
                dataCost=region==2?(byte?3:6):region==6?(byte?1:2):1;
                cost=1+dataCost+(load?1:0);type=2;
            }
        }
        if(c.events.frontCycles-clock<=cost)break;
        const uint32_t executing=op;
        r15+=4;*regs[15]=r15;op=next;pc+=4;next=U8TO32(pc,0);
        last=0;
        if(type==1) {
            ArmAluSpecialized::execute(regs,cpsr,executing);
        }
        else if(type==2) {
            last=load?2:1;
            if(load) {
                const uint8_t *data=c.memory.readMap7[address>>12];
                uint32_t value=byte?data[address&0xFFFu]:U8TO32(data,address&0xFFCu);
                if(!byte && (address&3))value=GbaArmAlu::ror(value,(address&3)*8);
                *regs[(executing>>12)&15]=value;
            } else {
                cycles=clock+1+dataCost;c.globalCycles=cycles;
                bus.beginInstruction();bus.nextSeq=false;bus.inSeqAccess=bus.hadDataAccess=true;
                bus.lastFetchUsedDuty=false;
                const uint32_t value=*regs[(executing>>12)&15];
                if(byte)c.memory.write<uint8_t>(true,address,value);
                else c.memory.write<uint32_t>(true,address,value);
            }
        } else if(type==3) {
            branchSeen=true;
            if(link)*regs[14]=r15-4;
            r15=target+4;*regs[15]=r15;
            pc=c.memory.readMap7[target>>12]+(target&0xFFFu);
            op=U8TO32(pc,0);pc+=4;next=U8TO32(pc,0);last=3;
        }
        clock+=cost;++done;
    }
    if (!done) return false;
    pipeline[0]=op;pipeline[1]=next;pcData=pc;cycles=clock;c.globalCycles=clock;
    bus.lastFetchUsedDuty=false;bus.beginInstruction();bus.nextSeq=last==0 || last==3;
    bus.active=false;if(branchSeen)bus.count=bus.duty=0;
    if(last==3){bus.count=bus.duty=0;bus.branched=true;}
    else if(last==1 || last==2){bus.inSeqAccess=bus.hadDataAccess=true;bus.hadLoad=last==2;}
    c.memory.clearGbaDmaOpenBus();
    return true;
}
