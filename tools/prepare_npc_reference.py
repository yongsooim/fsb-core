#!/usr/bin/env python3
"""Run actual45b8a0 with its actual shared CRT random generator."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from prepare_event0 import PE
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP

def main(executable,output):
    p=PE(executable);sha=hashlib.sha256(p.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
    for _,va,n,off in p.sections:u.mem_write(p.base+va,p.data[off:off+n])
    def w(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    obj=0x808490
    ranges=[(obj,428),(0x800dc8,0xc00),(0x7abca0,0x8000),(0x7cbca0,0x8000),(0x7e4d30,0x4000),(0x8019d0,2000)]
    observed=[0x6d1bf0,0x804aac,0x77ece0]
    cases=[(seed,speed,mode) for seed in range(1,33) for speed in [0,0x100,0x200,0x300,0x400] for mode in range(6)]
    data=bytearray(b'FSBFIN1\0'+struct.pack('<III',len(cases),len(ranges),len(observed)))
    for a,n in ranges:data+=struct.pack('<II',a,n)
    for a in observed:data+=struct.pack('<I',a)
    for seed,speed,mode in cases:
        for a,n in ranges:u.mem_write(a,bytes(n))
        values={0x6d1bf0:seed,0x80465c:3,0x7e0d20:12,0x7e0d24:12,0x77ecdc:0,0x804aac:0,0x77ece0:0,
                0x8019e8:2,0x8019ec:2,0x8019f0:10,0x8019f4:10,0x5b359c+17*68:speed,0x6da2d4:0}
        actor={0:10,4:0x1024e,0x14:0x58000,0x18:0x58000,0x1c:0x8000,0x110:1,0x118:0,0x11c:17,0x130:8}
        if mode==1:values.update({0x8019e8:5,0x8019ec:5,0x8019f0:5,0x8019f4:5})
        if mode==2:actor.update({0x30:3,0x34:2})
        if mode==3:values[0x7cbca0+(6*12+5)*4]=0x200
        if mode==4:values[0x7e4d30+(5*12+5)*4]=0x33;actor.update({0x110:2,0x30:2,0x34:2})
        if mode==5:actor[4]|=0x40000000
        values.update({obj+a:v for a,v in actor.items()});data+=struct.pack('<I',len(values))
        for a,v in values.items():w(a,v);data+=struct.pack('<II',a,v&0xffffffff)
        w(0x100f000,0x1000000);w(0x100f004,obj);u.reg_write(UC_X86_REG_ESP,0x100f000);u.emu_start(0x45b8a0,0x1000000,count=20000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('NPC routine did not return')
        data+=bytes(u.mem_read(obj,428))
        for a in observed:data+=bytes(u.mem_read(a,4))
        data+=bytes(u.mem_read(0x800dc8,0xc00))
    path=Path(output);path.write_bytes(data)
    report={'exe_sha256':sha,'cases':len(cases),'routine':'45b8a0 with actual498090 RNG,45c55c motion and45cc1b frame resolver','substituted_calls':[],
            'scope':'single NPC tick across32seeds,five speed masks,six movement/boundary states','fixture_sha256':hashlib.sha256(data).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main(*sys.argv[1:])
