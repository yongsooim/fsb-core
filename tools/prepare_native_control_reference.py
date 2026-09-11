#!/usr/bin/env python3
"""Original x86 cases for the native event-control migration, no substituted calls."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
from prepare_event0 import PE

def main(executable,output,scope='valid'):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    base=0x4a5000;initial=bytes(u.mem_read(base,3882100));obj=0x6e6000;pc=0x6df000;records=bytearray();counts={}
    if scope not in ['valid','errors']:raise ValueError('unknown scope')
    faults=[]
    def assertion(uc,address,size,user):
        faults.append(address);uc.emu_stop() # Observe failure; never replace the assertion with a return.
    u.hook_add(UC_HOOK_CODE,assertion,begin=0x40192a,end=0x40192a)
    def packet(op,sub,args=()):return struct.pack('<BBH',op,sub,4+5*len(args))+b''.join(struct.pack('<BI',tag,value&0xffffffff) for tag,value in args)
    def case(op,sub,args=(),writes=(),extra=b''):
        nonlocal records
        faults.clear()
        entry=pe.u32(pe.offset(0x5aa410+op*4-pe.base));command=packet(op,sub,args)
        setup=[(pc+i,1,v) for i,v in enumerate(command+extra)]+[(obj+0x30,4,pc),(obj+0xa8,4,0),(0x768a8c,4,0)]+list(writes)
        u.mem_write(base,initial)
        for a,width,value in setup:u.mem_write(a,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
        before=bytes(u.mem_read(base,len(initial)));u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<II',0x1000000,obj))
        u.emu_start(entry,0x1000000,count=1000000)
        if scope=='errors' and not faults:raise RuntimeError(f'expected original assertion for {entry:x}')
        if scope=='valid' and (faults or u.reg_read(UC_X86_REG_EIP)!=0x1000000):raise RuntimeError(f'original handler {entry:x} did not return')
        after=bytes(u.mem_read(base,len(initial)));changes=[]
        for block in range(0,len(initial),4096):
            if before[block:block+4096]!=after[block:block+4096]:changes.extend((base+i,after[i]) for i in range(block,min(block+4096,len(initial))) if before[i]!=after[i])
        records+=struct.pack('<II',entry,obj)+(struct.pack('<I',bool(faults)) if scope=='errors' else b'')+struct.pack('<I',len(setup))+b''.join(struct.pack('<III',a,w,v&0xffffffff) for a,w,v in setup)
        records+=struct.pack('<I',len(changes))+b''.join(struct.pack('<IB',a,v) for a,v in changes);counts[hex(entry)]=counts.get(hex(entry),0)+1
    if scope=='valid':
        for sub in [0,1,255]:
            for depth in [0,1,6]:
                for exit_pc in [0,pc+64]:case(3,sub,[(0x80,pc+128)],[(obj+0xa8,4,depth),(obj+0x68+max(0,depth-1)*4,4,exit_pc)])
                case(7,sub,[(0x80,pc+128)],[(obj+0xa8,4,depth),(obj+0x88+depth*4,4,0xaabbccdd)])
                for selected in [0,1]:case(6,sub,[(0,2),(0,selected)],[(obj+0xa8,4,depth),(pc+2,2,22)],struct.pack('<II',pc+128,pc+256))
            for count in [0,1,3]:
                for nested in [False,True]:
                    tail=(packet(11,0,[(0,1)])+packet(12,0) if nested else b'')+packet(12,0)
                    case(11,sub,[(0,count)],extra=tail)
            for depth in [1,3,7]:
                for count in [1,3]:case(12,sub,writes=[(obj+0xa8,4,depth),(obj+0x48+(depth-1)*4,4,pc-32),(obj+0x68+(depth-1)*4,4,pc+4),(obj+0x88+(depth-1)*4,4,count)])
                case(14,sub,writes=[(obj+0xa8,4,depth),(obj+0x48+(depth-1)*4,4,pc-32),(obj+0x68+(depth-1)*4,4,pc+4)])
                case(18,sub,writes=[(obj+0xa8,4,depth),(obj+0x68+(depth-1)*4,4,0),(obj+0x88+(depth-1)*4,4,0xabcdef01)])
        for depth in [0,1,5]:
            frame=[(obj+0xa8,4,depth),(obj+0x20,4,1)]
            for i in range(depth):
                frame += [(obj+0x48+i*4,4,0 if i==0 else pc-32),(obj+0x68+i*4,4,pc+128 if i==0 else 0)]
            case(0,0,writes=frame)
            case(0,1,writes=frame)
            for value in [0,1,0xffffffff]:case(0,2,[(0,value),(0,0),(0,1)],frame)
            for exit_pc in [0,pc+128]:
                setup=[(obj+0xa8,4,depth),(obj+0x68+max(0,depth-1)*4,4,exit_pc)]
                for value in [0,1,0xffffffff]:
                    case(4,1,[(0,pc+128),(0,value)],setup)
                    case(4,0,[(0,pc+128),(0,value),(0,0),(0,1)],setup)
                for sub in [0,1,2]:
                    for index in [0,1,2]:case(5,sub,[(0,3),(0,index)],setup+[(pc+2,2,30)],struct.pack('<4I',pc+128,pc+256,pc+512,pc+1024))
            for value in [0,1,3]:case(8,0,[(0,pc+128),(0,value)],[(obj+0xa8,4,depth)])
            for sub in [0,1]:
                for value in [0,1]:
                    args=[(0,value),(0,0),(0,1)]
                    for nested in [False,True]:
                        extra=(packet(13,1,args)+packet(14,0) if nested else b'')+packet(14,0)
                        case(13,sub,args,[(obj+0xa8,4,depth)],extra)
            for sub in [0,1,2]:
                for value in [0,1,0xffffffff]:
                    args=[(0,value)] if sub==0 else [(0,value),(0,0),(0,1)]
                    for nested in [False,True]:
                        extra=(packet(16,0,[(0,1)])+packet(18,0) if nested else b'')+packet(17,3)+packet(18,0)
                        case(16,sub,args,[(obj+0xa8,4,depth)],extra)
        for depth in [1,3,6]:
            top=depth-1
            for value in [0,1,3]:
                case(8,0,[(0,pc+128),(0,7)],[(obj+0xa8,4,depth),(obj+0x48+top*4,4,pc),(obj+0x68+top*4,4,pc+14),(obj+0x88+top*4,4,value)])
            for value in [0,1]:
                for sub in [0,1]:case(13,sub,[(0,value),(0,0),(0,1)],[(obj+0xa8,4,depth),(obj+0x48+top*4,4,pc),(obj+0x68+top*4,4,pc+128)])
            for taken in [0,1]:
                for sub in [0,1,2,3]:
                    for value in [0,1,0xffffffff]:
                        args=[(0,value)] if sub==0 else [(0,value),(0,0),(0,1)]
                        case(17,sub,args,[(obj+0xa8,4,depth),(obj+0x68+top*4,4,0),(obj+0x88+top*4,4,taken)],packet(16,0,[(0,1)])+packet(18,0)+packet(18,0))
            for sub in [0,1]:
                for value in [0,1]:
                    setup=[(obj+0xa8,4,depth)]
                    for i in range(depth):setup += [(obj+0x48+i*4,4,pc-32),(obj+0x68+i*4,4,pc+128 if i==0 else 0)]
                    case(15,sub,[(0,value),(0,0),(0,1)],setup)
    else:
        overflow=[(obj+0xa8,4,7)]
        case(7,0,[(0,pc+128)],overflow)
        case(6,0,[(0,1),(0,0)],overflow+[(pc+2,2,18)],struct.pack('<I',pc+128))
        case(11,0,[(0,1)],overflow,packet(12,0))
        case(8,0,[(0,pc+128),(0,1)],[(obj+0xa8,4,6)])
        case(13,1,[(0,1),(0,0),(0,1)],overflow,packet(14,0))
        case(16,0,[(0,1)],overflow,packet(18,0))
        case(12,0,writes=[(obj+0xa8,4,1),(obj+0x48,4,0),(obj+0x68,4,pc+4),(obj+0x88,4,1)])
        case(12,0)
        case(14,0,writes=[(obj+0xa8,4,1),(obj+0x68,4,pc+128)])
        case(15,0,writes=[(obj+0xa8,4,1),(obj+0x48,4,0),(obj+0x68,4,pc+128)])
        case(18,0)
        case(18,0,writes=[(obj+0xa8,4,1),(obj+0x68,4,pc+128)])
        case(17,3,writes=[(obj+0xa8,4,1),(obj+0x68,4,pc+128)])
        case(13,0,[(0,0),(0,0),(0,1)],[(obj+0xa8,4,1),(obj+0x48,4,pc),(obj+0x68,4,0)])
    data=(b'FSBFVE1\0' if scope=='errors' else b'FSBFVM1\0')+struct.pack('<I',sum(counts.values()))+records;path=Path(output);path.write_bytes(data)
    path.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'cases':sum(counts.values()),'entry_counts':counts,'fixture_sha256':hashlib.sha256(data).hexdigest(),'substituted_calls':[],'scope':scope,'assertion_observation_stop':hex(0x40192a) if scope=='errors' else None,'compared_bytes_per_case':len(initial)},indent=2)+'\n');print(counts)
if __name__=='__main__':main(*sys.argv[1:])
