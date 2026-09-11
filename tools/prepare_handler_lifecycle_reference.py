#!/usr/bin/env python3
"""Original handler lifecycle instructions, isolated and with real pool/effect callees."""
from pathlib import Path
import sys,struct
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
CTRL,TRACE,CONFIG=0x6df000,0x6de000,0x6deb00
ACTOR=0x8073d8
SERVICES={0x45d89c:(1,4),0x401ad8:(1,0),0x464494:(1,4),0x447841:(2,8),0x4646be:(2,8),0x45d91d:(1,4),0x45d84b:(2,8)}
def main(snapshot,output,mode='isolated'):
    o=Oracle(snapshot)
    def put(a,v,n=4):o.u.mem_write(a,(v&((1<<(8*n))-1)).to_bytes(n,'little'))
    def stop(u,entry,size,user):
        sp=u.reg_read(UC_X86_REG_ESP);n,pop=SERVICES[entry];args=[o.read(sp+4+4*i) for i in range(n)]
        o.requests[(entry,tuple(args))]+=1
        row=TRACE+4+o.read(TRACE)*48
        values=[entry,n]+args+[0]*(2-n)+[o.read(0x5d2698),o.read(0x5d269c),o.read(0x805680),o.read(CTRL+0x14c),o.read(CTRL+0x78),o.read(ACTOR+4)]
        for i,v in enumerate(values):put(row+4*i,v)
        put(TRACE,o.read(TRACE)+1)
        mutate=o.read(CONFIG+4)
        if entry==0x447841 and mutate&1:put(CTRL+0x14c,77);put(0x7760d0,0x12345678)
        if entry==0x4646be and mutate&2:put(CTRL+0x14c,0xfffffffc)
        if entry==0x45d89c and mutate&4:put(0x5d2698,42);put(0x805680,0xbadcafe);put(0x5d269c,7)
        if entry==0x401ad8 and mutate&8:put(0x5d2698,77);put(0x805680,CTRL);put(0x5d269c,2)
        if entry==0x464494 and mutate&16:put(0x8064f4,0xabcd);put(0x5d2698,88)
        if entry==0x45d91d and mutate&32:put(0x805680,CTRL)
        u.reg_write(UC_X86_REG_EAX,o.read(CONFIG) if entry==0x45d89c else 0)
        u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+pop)
    for entry,abi in SERVICES.items():
        if mode=='integrated' and entry not in [0x401ad8,0x464494]:continue
        o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry)
        o.substituted[hex(entry)]=f'RET {abi[1]}; log arguments and pre-call state, apply configured mutation'
    if mode=='integrated':
        o.substitute_decimal_print(0x8594c0,'sprintf boundary: write original decimal text/length')
        o.substitute(0x435373,1,'audio device boundary; no state mutation')
    def setup(spawn=CTRL,mutation=0):
        writes=[(TRACE+i*4,4,0) for i in range(200)]
        writes += [(CONFIG,4,spawn),(CONFIG+4,4,mutation),(0x5d2698,4,0x80000000),(0x5d269c,4,0xffffffff),
                   (0x805680,4,CTRL),(0x8064f4,4,0),(0x773014,4,0),(0x7757e0,4,0),(0x7760d0,4,0xffffffff),
                   (CTRL,4,0xffffffff),(CTRL+4,4,1),(CTRL+0x148,4,0),(CTRL+0x14c,4,0),
                   (CTRL+0x144,4,10),(CTRL+0x78,4,0),(CTRL+0x16c,4,10),(ACTOR+0x110,4,0)]
        if mode=='integrated':writes += [(at,4,0 if spawn else 0x10840) for at in range(0x810a54,0x85712c,0x1ac)]
        return writes
    for phase in [0,1,2,3,4,0xffffffff]:
        for index in [0,3,0xffffffff]:
            for callback in [0,0x481b11]:
                for spawned in [0,CTRL]:
                    for mutation in [0,63]:
                        writes=setup(spawned,mutation)
                        writes += [((table+index*stride)&0xffffffff,4,callback) for table,stride in [(0x60885c,24),(0x60916c,44),(0x6131cc,76)]]
                        o.case(0x461854,[phase,index],writes,0xffffffff)
    for banners in [0,1]:
        for banner in [0,0xdeadbeef]:
            for action in [0,2,0xffffffff]:
                for callback in [0,0x481b11]:
                    for spawned in [0,CTRL]:
                        for mutation in [0,63]:
                            o.case(0x461903,[action],setup(spawned,mutation)+[(0x773014,4,banners),(0x8064f4,4,banner),((0x610c24+action*32)&0xffffffff,4,callback)],0xffffffff)
    for gate in [0,0xc0000000,0x40000000,0xffffffff]:
        for spawned in [0,CTRL]:
            for mutation in [0,63]:o.case(0x461a4f,[],setup(spawned,mutation)+[(0x77e570,4,gate)],0xffffffff)
    for banners in [0,1]:
        for banner in [0,0xdeadbeef]:
            for obj in [0,CTRL]:
                for action in [0xffffffff,0,2]:
                    for flags in [0,0x10840]:
                        for latch in [0,0x80000000]:
                            for callback in [0,0x481b11]:
                                o.case(0x461a91,[],setup(CTRL,63)+[(0x773014,4,banners),(0x8064f4,4,banner),(0x805680,4,obj),(0x5d269c,4,action),(CTRL+4,4,flags),(0x5d2698,4,latch),((0x610c24+action*32)&0xffffffff,4,callback)],0xffffffff)
    for phase in [-20,-1,0,10,20,30,7]:
        for tick in [9,10,11,0xffffffff]:
            for waiting in [0,1]:
                for mutation in [0,63]:
                    o.case(0x46196c,[CTRL],setup(CTRL,mutation)+[(CTRL+0x14c,4,phase),(CTRL+0x144,4,tick),(CTRL+0x78,2,waiting)],0)
    for count in [0,1,3]:
        for hp in [0xffffffff,0,1]:
            for gauge in [19,20,0xffffffff]:
                for flags in [0,0x80,0xffffffff]:
                    writes=setup()+[(0x803a20,4,count),(0x77a4fc,4,1)]
                    for i in range(3):
                        writes += [(0x5d2258+i*4,4,i),(0x607a08+i*0xbc+0x1c,4,hp),
                            (0x607a08+i*0xbc+0x30,4,gauge),(0x607a08+i*0xbc+4,4,0x1234+i),
                            (ACTOR+i*0x1ac+4,4,flags)]
                    o.case(0x44e085,[],writes,0)
    o.write(output,'handler_lifecycle_'+mode,'Original instructions determine data, return values and call trace. Integrated mode runs real allocation/effect/finalization callees; only log, banner, formatting and audio boundaries are substituted. Isolated mode also controls spawn failure and callback mutation ordering.')
if __name__=='__main__':main(*sys.argv[1:])
