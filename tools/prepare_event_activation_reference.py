#!/usr/bin/env python3
"""Execute original event activation helpers; record .data and service order.
Uses the common FSBACT1 snapshot recorder from the existing combat oracle.
Lower-level services are explicitly substituted; this isolates helper contracts.
"""
from pathlib import Path
import sys, struct
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
TRACE,CONFIG,OBJECT=0x6de000,0x6deb00,0x6df000
SERVICES={0x41a00b:(2,8),0x457e56:(0,0),0x4300c7:(2,8),0x42feb9:(1,4),
          0x457ec9:(1,4),0x430059:(2,8),0x4026c1:(1,4),0x4139b6:(3,12),
          0x401a02:(2,0),0x412017:(2,8),0x44e085:(0,0)}
def main(snapshot,output,mode="isolated"):
    o=Oracle(snapshot)
    def put(at,value):o.u.mem_write(at,struct.pack('<I',value&0xffffffff))
    def service(u,entry,size,user):
        sp=u.reg_read(UC_X86_REG_ESP);n,pop=SERVICES[entry]
        args=[o.read(sp+4+i*4) for i in range(n)]
        o.requests[(entry,tuple(args))]+=1
        count=o.read(TRACE);row=TRACE+4+count*48
        words=[entry,n]+args+[0]*(3-n)+[o.read(0x57fd1c),o.read(0x57fd28),o.read(OBJECT+0xf8),o.read(OBJECT+0xfc),o.read(OBJECT+0xe4)]
        for i,v in enumerate(words):put(row+i*4,v)
        put(TRACE,count+1)
        mutation=o.read(CONFIG+20)
        if entry==0x412017 and mutation&1:put(0x57fd28,0x1234);put(0x57fd1c,0x4321)
        if entry==0x44e085 and mutation&2:put(0x57fd28,0xbeef)
        if entry==0x42feb9 and mutation&4:put(OBJECT+0xf8,0x9876)
        if entry==0x41a00b and mutation&8:put(0x57fd1c,0x222)
        returns={0x41a00b:0,0x457e56:4,0x42feb9:8,0x457ec9:8,0x4026c1:12,0x4139b6:0,0x412017:16}
        value=o.read(CONFIG+returns[entry]) if entry in returns else 0
        u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+pop)
    for entry,abi in SERVICES.items():
        if mode=="integrated" and entry in [0x457e56,0x457ec9,0x44e085]:continue
        o.u.hook_add(UC_HOOK_CODE,service,begin=entry,end=entry)
        o.substituted[hex(entry)]=f'log arguments and pre-call scene/object fields; configured result; RET {abi[1]}'
    def setup(handle=0x1234,started=1,mutation=0):
        return ([(TRACE+i*4,4,0) for i in range(145)]+
                [(CONFIG+i*4,4,v) for i,v in enumerate([handle,0x1111,0x8073d8,OBJECT,started,mutation])]+
                [(OBJECT+off,4,0x55555555) for off in [0xe4,0xf8,0xfc,0x19c]]+
                [(0x57fd1c,4,9),(0x57fd28,4,0xffffffff)])
    for event in [0,7,0x10002,0xffffffff]:
        for definition in [0,0x600000]:
            for handle in [0,0x1234]:
                for actor in [0xffffffff,0,3,0x80000000]:
                    for trigger in [0,0xffffffff,0x12345678]:
                        for mutation in [0,12]:
                            writes=setup(handle,mutation=mutation)+[((0x6d08cc+event*4)&0xffffffff,4,definition)]
                            o.case(0x4120c7,[event,actor,trigger],writes,0xffffffff)
    for markup in [0,0x57fc28,0x10001234]:
        for handle in [0,0x1234,0x80000000]:
            o.case(0x412147,[markup],setup(handle),0xffffffff)
    for event in [0xffffffff,0,8,0x1000e]:
        for started in [0,1,0x80000000]:
            for mutation in [0,1,2,3]:
                o.case(0x4138ee,[],setup(started=started,mutation=mutation)+[(0x57fd28,4,event)],0xffffffff)
    o.write(output,'event_activation_'+mode,'Original helper instructions execute. In integrated mode457e56/457ec9/44e085 and their descendants also execute original code. Lower services log full arguments and pre-call scene/object state; configured callbacks may mutate fields. Zero handles still resolve to the configured scratch object in this isolated fixture; real runtime handle validity is checked separately by existing services.')
if __name__=='__main__':main(*sys.argv[1:])
