#!/usr/bin/env python3
"""Original458ed7 field input/collision cases, with no substituted game calls."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from prepare_event0 import PE
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP


def main(executable, output, scope="field"):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    machine=Uc(UC_ARCH_X86,UC_MODE_32);machine.mem_map(0x400000,0x600000);machine.mem_map(0x1000000,0x10000)
    for _,va,n,off in pe.sections:machine.mem_write(pe.base+va,pe.data[off:off+n])
    def write(a,v):machine.mem_write(a,struct.pack('<I',v&0xffffffff))
    obj=0x8073d8
    ranges=[(obj,428),(0x800dc8,0xc00),(0x7abca0,0x8000),(0x7cbca0,0x8000),(0x7e4d30,0x4000),(0x6da568,0x800)]
    if scope=="horizontal":ranges.append((0x7e4d30+0x8028,0x4000))
    observed=[0x776478,0x804aac,0x77ece0,0x802c9c,0x802ca0]
    cases=[(d,d,fast,style,0) for d in range(4) for fast in range(2) for style in range(8)]
    cases += [(f,d,0,0,0) for f in range(8) for d in range(5) if f!=d]
    cases += [(d,d,0,0,key) for d in range(4) for key in [13,32,88,97]]
    if scope=="horizontal":cases=[(d,d,fast,style,0) for d in [2,3] for fast in [0,1] for style in [6,8]]
    elif scope!="field":raise ValueError("unknown field input scope")
    data=bytearray(b'FSBFIN1\0'+struct.pack('<III',len(cases),len(ranges),len(observed)))
    for a,n in ranges:data+=struct.pack('<II',a,n)
    for a in observed:data+=struct.pack('<I',a)
    dx=[0,0,-1,1];dy=[-1,1,0,0];hold=[0x6da888,0x6da8a8,0x6da894,0x6da89c]
    for facing,direction,fast,style,key in cases:
        for a,n in ranges:machine.mem_write(a,bytes(n))
        values={0x7760c4:12,0x7760c8:12,0x7e0d20:12,0x7e0d24:12,0x77e598:0,0x80465c:3,
                0x5d0768:-1,0x5f858c:-1,0x57fd1c:-1,0x803a1c:0,0x5d2258:3,0x776478:0,
                0x6da2dc:0x100 if key else 0,0x6d66b0:key,0x6d6688:0,0x74b474:0,
                0x802ca0:0,0x802c9c:0,0x8021d8:0,0x773000:0,0x77ecdc:0,0x6d66dc:fast,
                0x804aac:0,0x77ece0:0,0x6da2d4:0,
                0x800dc8:0x1000f0f,0x800dcc:4,0x800dd0:4,0x800dd4:6,0x800dd8:6}
        actor={0:0,4:0x101ce,0x14:0x58000,0x18:0x58000,0x1c:0x8000,0x128:5,0x12c:5,
               0x110:facing,0x130:8}
        values.update({obj+a:v for a,v in actor.items()})
        if direction<4:
            values[hold[direction]]=1;center=5*12+5;neighbor=center+dy[direction]*12+dx[direction]
            if style==1:values[0x7cbca0+neighbor*4]=0x200
            if style==2:values[0x7cbca0+center*4]=0x20<<direction
            if style==3:values[0x7abca0+neighbor*4]=0x10003
            if style==4:values.update({0x7cbca0+center*4:0x20400|(direction<<26),0x7cbca0+neighbor*4:0x200})
            if style==5:values[0x7cbca0+neighbor*4]=0x1000|(direction<<26)
            if style==6:values.update({0x7cbca0+center*4:0x1000|(direction<<26),0x7cbca0+neighbor*4:0x200})
            if style==7:values[0x7e4d30+center*4]=0x33
            if style==8:
                values.update({obj+0x1c:0x18000,0x77e598:1,0x7e0d20+0x8028:12,0x7e0d24+0x8028:12,
                               0x7cbca0+(4096+center)*4:0x1000|((3 if direction==2 else 2)<<26),
                               0x7cbca0+(4096+neighbor)*4:0x200})
        data+=struct.pack('<I',len(values))
        for a,v in values.items():write(a,v);data+=struct.pack('<II',a,v&0xffffffff)
        write(0x100f000,0x1000000);write(0x100f004,obj);machine.reg_write(UC_X86_REG_ESP,0x100f000)
        machine.emu_start(0x458ed7,0x1000000,count=20000)
        if machine.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError(f'field case did not return: {(facing,direction,fast,style,key)}')
        data+=bytes(machine.mem_read(obj,428))
        for a in observed:data+=bytes(machine.mem_read(a,4))
        data+=bytes(machine.mem_read(0x800dc8,0xc00))
    path=Path(output);path.write_bytes(data)
    report={'exe_sha256':sha,'cases':len(cases),'executed':'458ed7 and actual callees, including45c55c,45cc1b,457ba4 and empty-neighbor45d395','substituted_calls':[],
            'scope':'synthetic12x12 field grid with active trigger rectangle, directional inputs, turn, fast movement, collision, hop and height/stair tiles','fixture_sha256':hashlib.sha256(data).hexdigest()}
    if scope=="horizontal":report['scope']='horizontal height connectors: left/right, ascent/descent and ordinary/fast input; actual458ed7 player movement'
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main(*sys.argv[1:])
