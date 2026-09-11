#!/usr/bin/env python3
"""Execute early-field VM handlers in original x86; no game calls substituted."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
from prepare_event0 import PE

def main(executable,output,scope="field"):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong original EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    base=0x4a5000;initial=bytes(u.mem_read(base,3882100));obj=0x6e6000;pc=0x6df000;records=bytearray();counts={}
    def case(entry,op,sub,args,writes):
        nonlocal records
        command=struct.pack('<BBH',op,sub,4+len(args)*5)+b''.join(struct.pack('<BI',tag,value&0xffffffff) for tag,value in args)
        writes=[(pc+i,1,value) for i,value in enumerate(command)]+[(obj+0x30,4,pc),(0x768a8c,4,0)]+writes
        u.mem_write(base,initial)
        for a,width,value in writes:u.mem_write(a,(value&((1<<(width*8))-1)).to_bytes(width,'little'))
        before=bytes(u.mem_read(base,len(initial)));u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<II',0x1000000,obj))
        u.emu_start(entry,0x1000000,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('original VM handler failed to return')
        after=bytes(u.mem_read(base,len(initial)));changes=[]
        for block in range(0,len(initial),4096):
            if before[block:block+4096]!=after[block:block+4096]:
                changes.extend((base+i,after[i]) for i in range(block,min(block+4096,len(initial))) if before[i]!=after[i])
        records+=struct.pack('<III',entry,obj,len(writes))
        for a,width,value in writes:records+=struct.pack('<III',a,width,value&0xffffffff)
        records+=struct.pack('<I',len(changes))
        for a,value in changes:records+=struct.pack('<IB',a,value)
        counts[hex(entry)]=counts.get(hex(entry),0)+1
    if scope=="e2":
        for seed in range(16):
            for selector in [0,5,6]:
                case(0x42dd80,0xe2,4,[(0,selector)],[(0x5d0a40,4,seed*7),(obj+0x44,4,0xabcdef01),(0x806b28+(0x191//32)*4,4,seed)])
    elif scope=="campaign":
        for seed in range(16):
            value=[0,1,3,255,65535,0x7fffffff,0x80000000,0xffffffff][seed%8]
            slot=seed%12
            case(0x42e591,0xe6,4,[(0,slot)],[(0x5aff9c+slot*84,4,1 if seed&1 else 0),(0x5affa0+slot*84,4,203+seed),(0x5affa4+slot*84,4,seed)])
            case(0x42e591,0xe6,8,[(0,slot)],[])
            case(0x42e591,0xe6,10,[(0,slot),(0,203+seed),(0,seed)],[])
            case(0x42e591,0xe6,11,[(0,slot),(0,seed&1)],[(0x5aff98+slot*84,4,value)])
            for sub in range(2):
                case(0x424820,0x4b,sub,[(0,144),(0,value)],[(0x5b356c+144*68,4,seed)])
                case(0x42e0e6,0xe4,4,[(0,sub),(0,0)],[(0x6086b8,4,value),(0x6086bc,4,seed)])
    elif scope=="field":
        for seed in range(16):
            value=[0,1,3,255,65535,0x7fffffff,0x80000000,0xffffffff][seed%8]
            for sub in range(3):
                for item in [230,299,329]:
                    case(0x42e0e6,0xe4,sub,[(0,item),(0,value)],[(0x806e30+item*4,4,seed),(0x607cbc,4,230),(0x607cc0,4,230)])
            for sub in range(2):
                case(0x42f1b0,0xea,sub,[(0,630)],[(0x5b356c+630*68,4,value)])
                case(0x42b0a6,0x99,sub,[(0,0xffffffff if seed&1 else 39)],[(0x5d229c,4,30)])
                case(0x428357,0x6f,sub,[(0,value)],[(0x769424,4,seed)])
            for sub in range(3):case(0x42e591,0xe6,sub,[(0,seed%3)],[(0x5aff98+(seed%3)*84,4,value)])
            for sub in range(4):
                case(0x42093c,0x31,sub,[(0x41,0xe4),(0x42,0xe8)],[(0x6e143c,4,639+seed),(0x6e1440,4,449+seed),(0x7873c0,4,value),(0x7873c4,4,seed-8)])
            case(0x424232,0x48,0,[(0,0x8073d8),(0x41,0xe4),(0x42,0xe8),(0x44,0xec)],[(0x8073ec,4,value),(0x8073f0,4,seed*65536+32768),(0x8073f4,4,0xffff8000)])
    else:raise ValueError("unknown VM fixture scope")
    result=b'FSBFVM1\0'+struct.pack('<I',sum(counts.values()))+records;path=Path(output);path.write_bytes(result)
    report={'source_sha256':sha,'cases':sum(counts.values()),'entry_counts':counts,'scope':scope,'substituted_calls':[],'compared_bytes_per_case':len(initial),'fixture_sha256':hashlib.sha256(result).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main(*sys.argv[1:])
