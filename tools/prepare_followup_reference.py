#!/usr/bin/env python3
"""Original follow-up wave/context logic and its callback-visible mutations."""
from pathlib import Path
import sys,struct
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
ACTOR,TRACE,CONFIG=0x8073d8,0x6dc000,0x6deb00
SERVICES={0x45db53:(1,4),0x45db6c:(1,4),0x4622c2:(2,8),0x4623dd:(1,4),0x447841:(2,8)}
def main(snapshot,output,mode='isolated'):
    o=Oracle(snapshot)
    def put(at,value):o.u.mem_write(at,struct.pack('<I',value&0xffffffff))
    def stop(u,entry,size,user):
        sp=u.reg_read(UC_X86_REG_ESP);n,pop=SERVICES[entry];args=[o.read(sp+4+4*i) for i in range(n)]
        ordinal=o.read(TRACE);at=TRACE+4+ordinal*48
        values=[entry,n]+args+[0]*(2-n)+[o.read(0x8064f0),o.read(0x8059f0),o.read(0x8059f8),o.read(0x77a50c),o.read(0x773f8c),o.read(0x805608)]
        for i,v in enumerate(values):put(at+i*4,v)
        put(TRACE,ordinal+1);o.requests[(entry,tuple(args))]+=1
        mutation=o.read(CONFIG)
        if mutation and entry==0x45db53:
            put(0x8059f0,ACTOR+3*0x1ac)
            if ordinal==0:put(0x77a50c,2)
            put(0x773f8c,o.read(0x773f8c)^0x300)
        if mutation and entry==0x45db6c:put(0x8059f0,ACTOR+3*0x1ac);put(0x8059f8,ACTOR+0x1ac)
        if mutation and entry==0x4622c2:
            put(0x773f8c,o.read(0x773f8c)^0x8400);put(0x8059f8,ACTOR+0x1ac)
        if mutation and entry==0x447841:
            put(0x8064f0,2);put(0x773f8c,o.read(0x773f8c)|0x08080000)
        if mutation and entry==0x4623dd:put(0x8064f0,1);put(0x8059f0,ACTOR+3*0x1ac)
        u.reg_write(UC_X86_REG_EAX,1);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+pop)
    if mode=='isolated':
        for entry,abi in SERVICES.items():
            o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry);o.substituted[hex(entry)]=f'log pre-call state, configured mutation, RET {abi[1]}'
    else:
        for entry,n in [(0x435373,1),(0x4353cb,1)]:o.substitute(entry,n,'audio device boundary')
        o.substitute(0x401ad8,1,'error report boundary',False)
        o.substitute_decimal_print(0x8594c0,'decimal text boundary')
    def setup(count,flags,mutation,selected=0):
        w=[(TRACE+i*4,4,0) for i in range(500)]+[(CONFIG,4,mutation)]
        w += [(0x7757e0,4,2),(0x8059f0,4,ACTOR+2*0x1ac),(0x77a50c,4,count),(0x774188,4,selected),
            (0x7764d8,4,7),(0x7760d0,4,33),(0x7760cc,4,44),(0x805878,4,0xcccccccc),
            (0x8064f0,4,7),(0x805508,4,9)]
        for i in range(4):
            obj=ACTOR+i*0x1ac
            w += [(obj+4,4,0x10000),(obj+0x110,4,i),(obj+0x114,4,(i+1)%4),
                  (0x8059f8+i*4,4,obj),(0x5d2258+i*4,4,i),(0x607a08+i*0xbc+9,1,0x20),
                  (0x773f88+i*16,4,i),(0x773f8c+i*16,4,flags),(0x805608+i*4,4,0xaabbccdd)]
        # Selected third row is an enemy-side actor; first row stays slot0 so
        # the original's first-row restore index differs from the selected one.
        w += [(0x773f88+32,4,16)]
        if mode=='integrated':
            w += [(0x805880+i*4,4,0) for i in range(32)]+[(0x805588+i*4,4,0) for i in range(32)]
            w += [(0x805a78+i*4,4,0) for i in range(320)]
            w += [(0x5d26a0+i*4,4,0x481b11) for i in range(10)]
            w += [(at,4,0) for at in range(0x810a54,0x85712c,0x1ac)]
        return w
    patterns=[0,0xffffffff,0x70000000,0x30080003]+[1<<bit for bit in range(32)]
    for stage in [0,1,2,3,4,0xffffffff]:
        for count in [0,1,3]:
            for flags in patterns:
                for mutation in [0,1]:
                    # Context swapping is valid independently of target count.
                    o.case(0x461d82,[stage],setup(count,flags,mutation,2 if flags&1 else 0),0)
    for wave in [0,1,0xffffffff]:
        for count in [0,1,3]:
            for flags in [0,0x40000,0x80000,0xc0000,0x4000000,0x8000000,0xc000000,0xc0c0000]:
                for mutation in [0,1]:o.case(0x461b11,[wave],setup(count,flags,mutation),0)
    o.write(output,'followup_'+mode,'Original follow-up instructions run; flags exercise all bits, primary/counter contexts and two waves. Isolated callbacks mutate live rows/counts to test reload order. Integrated mode uses real snapshots, script start and attached-effect groups, with inert constructor481b11 for registered types and declared device boundaries.')
if __name__=='__main__':main(*sys.argv[1:])
