#!/usr/bin/env python3
"""Execute original battle leaf rules without replacing any game calls."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from prepare_event0 import PE
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP

def main(executable,output):
    p=PE(executable);sha=hashlib.sha256(p.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
    for _,va,n,off in p.sections:u.mem_write(p.base+va,p.data[off:off+n])
    def write(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def original(a):return p.u32(p.offset(a-p.base))
    functions=[0x44d233,0x44c6eb,0x44cbd2,0x44cd05,0x44dd1c,0x44e16c,0x44e200,0x44e231,0x44e265,0x44e2de,0x44e775]
    records=bytearray();counts=[0]*len(functions)
    def case(kind,args,writes,observed):
        nonlocal records
        for a,v in writes.items():write(a,v)
        write(0x100f000,0x1000000)
        for i,v in enumerate(args):write(0x100f004+i*4,v)
        u.reg_write(UC_X86_REG_ESP,0x100f000);u.emu_start(functions[kind],0x1000000,count=20000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError(f'nonreturning battle rule {kind}')
        result=u.reg_read(UC_X86_REG_EAX)
        if kind in [0,3]:result&=255
        if kind in [1,2,4]:result=0
        records+=struct.pack('<II',kind,len(args))
        for a in args:records+=struct.pack('<I',a&0xffffffff)
        records+=struct.pack('<I',len(writes))
        for a,v in writes.items():records+=struct.pack('<II',a,v&0xffffffff)
        records+=struct.pack('<II',result,len(observed))
        for a in observed:records+=struct.pack('<II',a,read(a))
        counts[kind]+=1
    party=[0x607a08+i*188 for i in [3,11]];enemies=[0x806b60+i*36 for i in range(5)];types=[135,135,132,189,189]
    def initial(seed=1):
        writes={0x803a20:2,0x5d2258:3,0x5d225c:11,0x776484:5,0x6d1bf0:seed,0x8059f0:0xdeadbeef}
        for index,record in enumerate(party):
            writes.update({record+i*4:0 for i in range(47)});writes.update({record+0x1c:25+index*20,record+0x70:24+index*6})
            for i in range(5):writes[record+0x74+i*4]=0xffffffff
        for index,record in enumerate(enemies):
            writes.update({record+i*4:0 for i in range(9)});writes.update({record:60+index,record+4:types[index],record+20:32,record+32:15})
            writes[0x8073d8+(60+index)*428+0x118]=index
        for id in set(types):
            for offset in [0x68,0x6c,0x70,0x74]:
                a=0x609ca0+id*124+offset;writes[a]=original(a)
        return writes
    observed=[a+i*4 for a in party for i in range(47)]+[a+i*4 for a in enemies for i in range(9)]+[0x6d1bf0]
    statuses=[0,0x100,0x200,0x400,0x800,0x1000,0x3000,0x600000,0x200000,0x400000,0x1ff00,0xffffffff]
    for variant in range(64):
        writes=initial(variant+1)
        for index,record in enumerate(party+enemies):
            writes[record+8]=statuses[(variant+index)%len(statuses)]
            writes[record+12]=[0,0x11111111,0x23456789,0xfedcba98][(variant+index)%4]
            writes[record+16]=[0,0x111,0x321][(variant+index)%3]
        case(0,[],writes,observed)
        case(1,[],writes,observed)
        for index,record in enumerate(party):
            writes[record+0x30]=(variant+index)*3;writes[record+0x2c]=[0,14,15,16,0xffffffff][variant%5]
            if variant&1:writes[record+0x74]=0x146
            if variant&2:writes[record+0x78]=0x145
            if variant&4:writes[record+0x7c]=0xf4
            if variant&8:writes[record+0x1c]=0
        case(2,[],writes,observed)
        for index,record in enumerate(enemies):writes[record+32]=[0,19,20,21,40][(variant+index)%5]
        case(3,[0x8059f0],writes,observed+[0x8059f0])
        writes=initial(variant+1)
        if variant&1:
            for id in set(types):writes[0x609ca0+id*124+0x74]=1000
        reward_observed=[0x77e5a8+i*4 for i in range(5)]+[0x774170+i*4 for i in range(5)]+[0x77ebf8,0x77ec00,0x775c84,0x6d1bf0]
        case(4,[],writes,reward_observed)
    attacker,target=0x8073d8,0x807584
    for facing in range(8):
        for x in range(-11,12):
            for y in range(-11,12):case(5,[attacker,target],{attacker+0x128:20,attacker+0x12c:20,target+0x128:20+x,target+0x12c:20+y,target+0x110:facing},[])
    for relation in range(5):case(6,[relation],{},[]);case(7,[relation],{},[])
    for row in range(3):
        for relation in range(5):case(8,[row,relation],{},[])
    for a in range(16):
        for b in range(16):case(9,[a<<24,b<<24],{},[])
    for a in range(4):
        for b in range(4):case(10,[a<<12,b<<12],{},[])
    data=b'FSBBAT1\0'+struct.pack('<I',sum(counts))+records;path=Path(output);path.write_bytes(data)
    report={'exe_sha256':sha,'case_counts':dict(zip([hex(f) for f in functions],counts)),'cases':sum(counts),'substituted_calls':[],
            'scope':'battle rule routines and actual internal callees; not a completed combat or animation run','fixture_sha256':hashlib.sha256(data).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main(*sys.argv[1:])
