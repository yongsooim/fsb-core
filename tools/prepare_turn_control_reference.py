#!/usr/bin/env python3
"""Turn entry/neighbor/cleanup contracts from original machine instructions."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
ACTOR,TRACE,CONFIG=0x8073d8,0x6dc000,0x6deb00
SERVICES={0x44ce7b:2,0x44956a:4,0x44d09c:2,0x44d102:2,0x45149b:2,
          0x43934c:1,0x439701:2,0x4490c7:8,0x451ec6:1,0x453b8d:1,0x45d91d:1}
def main(snapshot,output,mode='isolated'):
    o=Oracle(snapshot)
    def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def stop(u,entry,size,user):
        sp=u.reg_read(UC_X86_REG_ESP);n=SERVICES[entry];args=[o.read(sp+4+4*i) for i in range(n)]
        ordinal=o.read(TRACE);at=TRACE+4+ordinal*64
        values=[entry,n]+args+[0]*(8-n)+[o.read(ACTOR+4),o.read(0x7757e0),o.read(0x77ecdc),o.read(0x805508)]
        for i,v in enumerate(values):put(at+i*4,v)
        put(TRACE,ordinal+1);o.requests[(entry,tuple(args))]+=1
        result=0
        if entry==0x44ce7b:
            probe=o.read(CONFIG+4);put(CONFIG+4,probe+1);result=o.read(CONFIG+16+probe*4)
        if entry==0x44956a:result=o.read(CONFIG+32)
        if entry==0x44d09c:result=0x12
        if entry==0x44d102:result=0x34
        if o.read(CONFIG):
            if entry==0x44956a:put(ACTOR+60*0x1ac+0x118,1)
            if entry==0x44d09c:put(0x7757e0,1);put(ACTOR+0x110,3)
            if entry==0x4490c7:put(0x7757e0,1);put(ACTOR+4,0xaabbcc11);put(0x607a68,999)
            if entry in [0x43934c,0x439701,0x451ec6]:put(0x77ecdc,7)
        u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+4*n)
    if mode=='isolated':
        for entry in SERVICES:
            o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry);o.substituted[hex(entry)]='callback trace, seeded result and configured live-state mutations'
    def setup(mutation=0):
        w=[(TRACE+i*4,4,0) for i in range(512)]+[(CONFIG,4,mutation),(CONFIG+4,4,0),(CONFIG+32,4,1)]
        w += [(CONFIG+16+i*4,4,0xffffffff) for i in range(4)]
        w += [(0x7757e0,4,0),(0x77ecdc,4,0),(0x77a568,4,6),(ACTOR+4,4,0x100),
              (ACTOR+0x128,4,3),(ACTOR+0x12c,4,3),(ACTOR+0x110,4,0),
              (ACTOR+0x14c,4,0),(ACTOR+0x144,4,0),(0x805508,4,7),
              (0x5d2258,4,0),(0x5d225c,4,1),(0x607a68,4,55),(0x7760c4,4,8),(0x7760c8,4,8),
              (0x7755c4,4,0),(0x7755c8,4,0),(0x7755cc,4,7),(0x7755d0,4,7),(0x77e598,4,0)]
        for i in range(30):w += [(ACTOR+(60+i)*0x1ac+0x118,4,i),(0x806b68+i*36,4,0)]
        if mode=='integrated':
            w += [(0x607a90,4,0),(0x607a94,4,1),(0x608858,4,0),(0x608858+24,4,1)]
            for i in range(64):w += [(0x7abca0+i*4,4,0),(0x77a570+i*4,4,0),(0x7764e0+i*4,4,0),(0x7cbca0+i*4,4,0)]
        return w
    # Isolated probes include invalid IDs, each direction, noncanonical AL,
    # all status mask bits, coordinate wrap, and callback-updated record indices.
    if mode=='isolated':
        for probe in range(4):
            for occupant in [59,60,89,90,0xffffffff]:
                for passable in [0,1,0x100,0x180]:
                    for status in [0,0x200,0x800,0x200000,0x400000,0x100]:
                        w=setup()+[(CONFIG+16+probe*4,4,occupant),(CONFIG+32,4,passable)]
                        if 60<=occupant<90:w += [(0x806b68+(occupant-60)*36,4,status)]
                        o.case(0x44ceb2,[3,3],w,255)
        for xy in [(0,0),(0x7fffffff,0x80000000),(0xffffffff,0xffffffff)]:
            for mutation in [0,1]:
                w=setup(mutation)+[(CONFIG+16+i*4,4,60) for i in range(4)]+[(0x806b68,4,0x200)]
                o.case(0x44ceb2,xy,w,255)
        for input in [0,1,2,0xffffffff]:
            for phase in [0,1,0xffffffff]:
                for tick in [0,5,6,0x7fffffff,0x80000000]:
                    for mutation in [0,1]:
                        o.case(0x44cfe2,[input],setup(mutation)+[(0x77ecdc,4,phase),(0x77a568,4,tick),(CONFIG+16,4,60)],0)
        for flags in [0,0x100,0x400,0x500,0xffffffff]:
            for movement in [0,9,10,55,0xffffffff,0x80000000,0x7fffffff]:
                for mutation in [0,1]:
                    o.case(0x44d17b,[],setup(mutation)+[(ACTOR+4,4,flags),(0x607a68,4,movement),
                         (0x7755c4,4,0x7fffffff),(0x7755cc,4,2),(0x7755c8,4,0xffffffff),(0x7755d0,4,1)],0)
    else:
        for probe in range(4):
            tile=[(3,2),(3,4),(2,3),(4,3)][probe]
            for status in [0,0x200,0x800,0x200000,0x400000,0x100]:
                w=setup()+[(0x7abca0+(tile[1]*8+tile[0])*4,4,0x10000|60),(0x806b68,4,status)]
                o.case(0x44ceb2,[3,3],w,255)
                o.case(0x44cfe2,[1],w,0)
        for movement in [0,9,10,55,0xffffffff,0x80000000,0x7fffffff]:
            o.case(0x44d17b,[],setup()+[(0x607a68,4,movement)],0)
    for state in [0,1,10,0xffffffff]:
        for tick in [0,1,31,32,33,34,0x80000000,0xffffffff]:
            for pending in [0,1,0xffffffff]:
                o.case(0x461c66,[ACTOR],setup()+[(ACTOR+0x148,4,0x461c66),(ACTOR+4,4,0x400),(ACTOR+0x14c,4,state),(ACTOR+0x144,4,tick),(0x805508,4,pending)],0)
    o.write(output,'turn_control_'+mode,'Original instructions determine expectations. Isolated callbacks log order/arguments and mutate live context. Integrated mode executes actual grid/rule/object callees; command-menu and enemy AI dispatch branches are covered only by isolated contracts and campaign regression.')
if __name__=='__main__':main(*sys.argv[1:])
