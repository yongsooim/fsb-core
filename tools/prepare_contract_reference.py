#!/usr/bin/env python3
"""Record original4022b3 rectangle geometry; isolate timer and DirectDraw only."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG
from prepare_event0 import PE

def main(executable,output):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong original EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    def read(at,n):return struct.unpack('<'+'I'*n,u.mem_read(at,n*4))
    def write(at,values):u.mem_write(at,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
    state={};counts={0x401f3a:0,0x405c8c:3,0x405ed1:6,0x405f23:5}
    def hook(u,address,size,opaque):
        stack=u.reg_read(UC_X86_REG_ESP);args=read(stack+4,counts[address])
        if address==0x405ed1:state['strips'].append(read(args[1],4)+read(args[3],4))
        u.reg_write(UC_X86_REG_EAX,state['progress'] if address==0x401f3a else 0)
        u.reg_write(UC_X86_REG_EIP,read(stack,1)[0]);u.reg_write(UC_X86_REG_ESP,stack+4+counts[address]*4)
    for address in counts:u.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    records=[]
    for height in [450,466,480]:
        top=(480-height)//2;view=[0,top,640,top+height]
        for progress in [0,1,751,7507,15015,22522,30029,30030]:
            write(0x6da51c,view+[640,height]);write(0x6da534,view);state.update(progress=progress,strips=[])
            u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
            u.reg_write(UC_X86_REG_ESP,0x100f000);write(0x100f000,[0x1000000]);u.emu_start(0x4022b3,0x1000000,count=1000000)
            if u.reg_read(UC_X86_REG_EIP)!=0x1000000 or len(state['strips'])!=48:raise RuntimeError('incomplete original callback')
            records.append((view+[progress],state['strips']))
    result=bytearray(b'FSBCTR1\0')+struct.pack('<I',len(records))
    for header,strips in records:
        result+=struct.pack('<5I',*header)
        for strip in strips:result+=struct.pack('<8I',*strip)
    path=Path(output);path.write_bytes(result);report={'source_sha256':sha,'cases':len(records),'rectangles':len(records)*48,'isolated_calls':{hex(k):v for k,v in counts.items()},'fixture_sha256':hashlib.sha256(result).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main(*sys.argv[1:])
