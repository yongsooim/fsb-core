#!/usr/bin/env python3
"""Compare C++ shadow/foreground mutations with actual original x86 routines.
Consumes explicit guest-state snapshots emitted by fsb_render_tests. Surface
pixels are not inputs: these routines construct draw commands, without OS calls.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
from prepare_event0 import PE

def ranges(path):
    data=Path(path).read_bytes()
    if data[:8]!=b'FSBDRAW1':raise ValueError('not a draw snapshot')
    count=struct.unpack_from('<I',data,8)[0];cursor=12;result=[]
    for _ in range(count):
        base,size=struct.unpack_from('<II',data,cursor);cursor+=8
        result.append((base,data[cursor:cursor+size]));cursor+=size
    if cursor!=len(data):raise ValueError('invalid draw snapshot length')
    return result

def main(exe,directory,output):
    pe=PE(exe);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    report={'exe_sha256':sha,'generator':'Unicorn '+unicorn.__version__,'routines':['0x454b24 shadows','0x4577d6 foreground'],
            'scope':'scene passes, synthetic occlusion and saturated shared-pool shadow/foreground fixtures; no full-game timing or final-frame parity claim','layers':[]}
    directory=Path(directory)
    for label,layer in [('layer-0',0),('layer-1',1),('occlusion',1),('crowded',0)]:
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
        for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
        before=dict(ranges(directory/f'{label}-before.bin'))
        for base,data in before.items():u.mem_write(base,data)
        for address,args in [(0x454b24,[layer*2+1]),(0x4577d6,[layer,0])]:
            u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<'+'I'*(len(args)+1),0x1000000,*args))
            u.emu_start(address,0x1000000,count=20000000)
            if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('original routine did not return')
        total=0;differences=[];mismatches=0;mutated=0
        for base,wanted in ranges(directory/f'{label}-after.bin'):
            actual=u.mem_read(base,len(wanted));total+=len(wanted)
            mutated+=sum(a!=b for a,b in zip(before[base],actual))
            for i,(a,b) in enumerate(zip(actual,wanted)):
                if a!=b:
                    mismatches+=1
                    if len(differences)<12:differences.append({'va':hex(base+i),'original':a,'cpp':b})
        report['layers'].append({'fixture':label,'layer':layer,'synthetic_occlusion':label in ('occlusion','crowded'),'compared_bytes':total,'mutated_bytes':mutated,'mismatches':mismatches,'first_differences':differences})
    report['match']=all(x['mismatches']==0 for x in report['layers'])
    Path(output).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
    return 0 if report['match'] else 1

if __name__=='__main__':
    if len(sys.argv)!=4:raise SystemExit('usage: verify_draw_pass_x86.py EXE RENDER_TEST_OUTPUT REPORT.json')
    raise SystemExit(main(*sys.argv[1:]))
