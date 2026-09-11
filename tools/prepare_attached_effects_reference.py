#!/usr/bin/env python3
"""Original grouped-effect registration/removal contracts with callback observations."""
from pathlib import Path
import sys,struct
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
OWNERS,COUNTS,OBJECTS,KINDS,CALLBACKS=0x805880,0x805588,0x805a78,0x805f78,0x5d26a0
CTRL,OTHER,CONFIG,TRACE=0x6df000,0x6df200,0x6de800,0x6dc000
OWNER=0x8073d8

def main(snapshot,output,mode='isolated'):
    o=Oracle(snapshot)
    def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def hook(u,entry,size,user):
        sp=u.reg_read(UC_X86_REG_ESP);arg=o.read(sp+4);ordinal=o.read(TRACE)
        row=o.read(CONFIG+4);slot=o.read(CONFIG+8);start=TRACE+4+ordinal*32
        values=[entry,arg,o.read(OWNERS+row*4),o.read(COUNTS+row*4),o.read(OBJECTS+slot*4),o.read(KINDS+slot*4),o.read(CTRL+0x160)]
        for i,v in enumerate(values):put(start+i*4,v)
        put(TRACE,ordinal+1);o.requests[(entry,(arg,))]+=1
        mutate=o.read(CONFIG)
        if entry==0x45d89c and mutate&1:
            put(COUNTS+row*4,0xffffffff);put(OWNERS+row*4,0x3333);put(OBJECTS+slot*4,OTHER)
        if entry==0x45d91d and mutate&2:
            put(COUNTS+row*4,7);put(OWNERS+row*4,0x2222);put(OBJECTS+slot*4,OTHER);put(KINDS+slot*4,0x12345678)
        if entry==0x45d91d and mutate&4 and ordinal==0 and slot<319:
            put(OBJECTS+(slot+1)*4,OTHER)
        u.reg_write(UC_X86_REG_EAX,CTRL if entry==0x45d89c else 0)
        u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+(0 if entry==0x401ad8 else 4))
    for entry in [0x45d89c,0x45d91d,0x401ad8]:
        if mode=='integrated' and entry!=0x401ad8:continue
        o.u.hook_add(UC_HOOK_CODE,hook,begin=entry,end=entry)
        o.substituted[hex(entry)]='record argument/pre-call tables; configured mutation; spawn returns6df000; logger cdecl, other services stdcall4'
    def setup(layout=0,mutation=0):
        row=31 if layout==2 else 0
        w=[(TRACE+i*4,4,0) for i in range(520)]
        w += [(CONFIG,4,mutation),(CONFIG+4,4,row),(CONFIG+8,4,row*10)]
        w += [(OWNERS+i*4,4,0) for i in range(32)]+[(COUNTS+i*4,4,0) for i in range(32)]
        w += [(OBJECTS+i*4,4,0) for i in range(320)]+[(KINDS+i*4,4,0xaabbccdd) for i in range(320)]
        w += [(CALLBACKS+i*4,4,0x481b11) for i in range(10)]
        for obj in [CTRL,OTHER]:
            w += [(obj+i*4,4,0) for i in range(107)]
            w += [(obj,4,0xffffffff),(obj+4,4,0x10840),(obj+0x148,4,0x481b11),(obj+0x160,4,0x55555555)]
        if layout in [1,2,4]:w += [(OWNERS+row*4,4,OWNER),(COUNTS+row*4,4,1)]
        if layout==3:w += [(OWNERS+i*4,4,0x1000+i) for i in range(32)]
        if layout==4:w += [(OWNERS+31*4,4,OWNER)]
        if layout==5:w += [(OBJECTS,4,CTRL),(COUNTS,4,1)] # zero owner with a live pointer
        if mode=='integrated':w += [(at,4,0) for at in range(0x810a54,0x85712c,0x1ac)]
        return w,row
    for layout in range(6):
        for owner in [OWNER,0,0x12345678]:
            for kind in [-1,0,1,5,10,11,-2147483648]:
                for mutation in [0,7]:
                    w,row=setup(layout,mutation)
                    o.case(0x4622c2,[owner,kind],w,0xffffffff)
        for kind in [1,5,10]:
            for present in [0,CTRL]:
                for count in [0,1,2,0xffffffff]:
                    w,row=setup(layout,7);index=row*10+kind-1
                    w += [(OBJECTS+index*4,4,present),(COUNTS+row*4,4,count),(CONFIG+8,4,index)]
                    o.case(0x462370,[OWNER,kind],w,0xffffffff)
        for owner in [OWNER,0,0x12345678]:
            for mutation in [0,7]:
                w,row=setup(layout,mutation);w += [(OBJECTS+(row*10)*4,4,CTRL),(OBJECTS+(row*10+3)*4,4,OTHER)]
                o.case(0x4623dd,[owner],w,0xffffffff)
        for mutation in [0,7]:
            w,row=setup(layout,mutation);w += [(OBJECTS,4,CTRL),(OBJECTS+319*4,4,OTHER)]
            o.case(0x462452,[],w,0xffffffff)
    # Detach deliberately permits wrapped out-of-range indices, unlike attach.
    for kind in [-2147483648,-1,0,11]:
        w,row=setup(1,0);index=(kind-1)&0xffffffff
        w += [((OBJECTS+index*4)&0xffffffff,4,CTRL)]
        o.case(0x462370,[OWNER,kind],w,0xffffffff)
    o.write(output,'attached_effects_'+mode,'Original functions execute. Isolated spawn/release callbacks observe registry mutation order and can modify later slots. Integrated mode executes real pool allocation/finalization with an inert original callback481b11. Invalid-kind logger alone is substituted there. Types are full dwords, verified from MOV/AND instruction widths.')
if __name__=='__main__':main(*sys.argv[1:])
