#!/usr/bin/env python3
"""Original control helpers, including their assertion-time game state."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from prepare_event0 import PE

def main(executable,output):
    p=PE(executable);sha=hashlib.sha256(p.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in p.sections:u.mem_write(p.base+va,p.data[offset:offset+size])
    base=0x4a5000;initial=bytes(u.mem_read(base,3882100));obj=0x6e6000;pc=0x6df000;faults=[];counts={};records=bytearray()
    def assertion(uc,*_):faults.append(True);uc.emu_stop()
    u.hook_add(UC_HOOK_CODE,assertion,begin=0x40192a,end=0x40192a)
    def packet(op,length=4):return struct.pack('<BBH',op,0,length)
    def case(entry,args,writes=(),script=b''):
        nonlocal records
        faults.clear();u.mem_write(base,initial)
        setup=[(obj+0x30,4,pc)]+[(pc+i,1,v) for i,v in enumerate(script)]+list(writes)
        for a,w,v in setup:u.mem_write(a,(v&((1<<(8*w))-1)).to_bytes(w,'little'))
        before=bytes(u.mem_read(base,len(initial)));u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<'+'I'*(1+len(args)),0x1000000,*args))
        u.emu_start(entry,0x1000000,count=1000000)
        assert faults or u.reg_read(UC_X86_REG_EIP)==0x1000000
        after=bytes(u.mem_read(base,len(initial)));changes=[]
        for at in range(0,len(initial),4096):
            if before[at:at+4096]!=after[at:at+4096]:changes.extend((base+i,after[i]) for i in range(at,min(at+4096,len(initial))) if before[i]!=after[i])
        records+=struct.pack('<II',entry,len(args))+b''.join(struct.pack('<I',a) for a in args)
        records+=struct.pack('<I',len(setup))+b''.join(struct.pack('<III',a,w,v) for a,w,v in setup)
        records+=struct.pack('<III',bool(faults),0 if faults else u.reg_read(UC_X86_REG_EAX),len(changes))+b''.join(struct.pack('<IB',a,v) for a,v in changes)
        counts[hex(entry)]=counts.get(hex(entry),0)+1
    for depth in range(8):
        for target in [0,pc+64,0xffffffff]:
            case(0x41b38c,[obj,target],[(obj+0xa8,4,depth),(obj+0x88+depth*4,4,0xabcdef01)])
        for head in [0,pc-32]:
            for exit_pc in [0,pc+64]:case(0x41b97f,[obj],[(obj+0xa8,4,depth),(obj+0x48+max(0,depth-1)*4,4,head),(obj+0x68+max(0,depth-1)*4,4,exit_pc)])
        for value in [0,1,0xffffffff]:
            for nested in [0,1,4]:
                script=packet(16)+packet(16)*nested+packet(18)*nested+packet(17)+packet(18)
                case(0x41c74e,[obj,value],[(obj+0xa8,4,depth),(obj+0x88+max(0,depth-1)*4,4,0xabcdef01)],script)
    for opener,closer in [(11,12),(13,14),(16,18)]:
        for nested in [0,1,4]:
            for end_length in [0,4]:
                script=packet(opener)*nested+packet(1)+packet(closer)*nested+packet(closer,end_length)
                for high in [0,0xabcd0000]:case(0x41a0fb,[pc,high|opener,high|closer],script=script)
    # Aliased bytecode makes source read/write order observable. False selection
    # must find the marker before clearing it; true selection reads length after
    # publishing the count, even when that store changes the header length.
    case(0x41c74e,[obj,0],[(obj+0xa8,4,1),(obj+0x30,4,obj+0x84),(obj+0x84,4,0x00040010),(obj+0x88,4,0x00040011)])
    case(0x41c74e,[obj,1],[(obj+0xa8,4,1),(obj+0x30,4,obj+0x86),(obj+0x86,4,0x00040010)])
    data=b'FSBCHP1\0'+struct.pack('<I',sum(counts.values()))+records;out=Path(output);out.write_bytes(data)
    out.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'fixture_sha256':hashlib.sha256(data).hexdigest(),'cases':sum(counts.values()),'entries':counts,'compared_data_bytes':len(initial),'substituted_calls':[],'assertion_observation_stop':'0x40192a'},indent=2)+'\n');print('control helper cases',sum(counts.values()))
if __name__=='__main__':main(*sys.argv[1:])
