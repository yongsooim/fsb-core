#!/usr/bin/env python3
"""Execute original4390e7/439237 pixel helpers, without replacement callees."""
import hashlib,json,random,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
from prepare_event0 import PE

def main(exe,out):
    pe=PE(exe);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x40000)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    rng=random.Random(439237);output=bytearray(b'FSBCHK1\0');count=192;output+=struct.pack('<I',count)
    for i in range(count):
        sw,sh,dw,dh=23,19,27,21;left,top=rng.randrange(-3,9),rng.randrange(-3,8)
        right,bottom=left+(i%21),top+(i%17);x,y=rng.randrange(-5,24),rng.randrange(-4,19)
        clip=(1,2,25,20);transparent=i&1;fill=[0,224,225,228][i%4]
        source=bytes(rng.randrange(9) for _ in range(sw*sh));target=bytes(rng.randrange(11) for _ in range(dw*dh))
        dx,dy=x-left,y-top;l=max(left,0,-dx,clip[0]-dx);t=max(top,0,-dy,clip[1]-dy)
        r=min(right,sw,dw-dx,clip[2]-dx);b=min(bottom,sh,dh-dy,clip[3]-dy)
        u.mem_write(0x1010000,source);u.mem_write(0x1020000,target)
        if r>l and b>t:
            args=[0x1010000,l,t,r-l,b-t,sw,0x1020000,l+dx,t+dy,dw]
            if not transparent:args.append(fill)
            u.reg_write(UC_X86_REG_ESP,0x103f000);u.mem_write(0x103f000,struct.pack('<'+'I'*(len(args)+1),0x1000000,*args))
            u.emu_start(0x439237 if transparent else 0x4390e7,0x1000000,count=1000000)
            if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise ValueError('helper did not return')
        output+=struct.pack('<16I',sw,sh,dw,dh,x&0xffffffff,y&0xffffffff,left&0xffffffff,top&0xffffffff,right&0xffffffff,bottom&0xffffffff,*clip,transparent,fill)
        output+=source+target+bytes(u.mem_read(0x1020000,dw*dh))
    Path(out).write_bytes(output)
    report={'exe_sha256':sha,'cases':count,'functions':['0x4390e7','0x439237'],'substituted_calls':[], 'scope':'original pixel helpers; source/destination clipping supplied by fixture','fixture_sha256':hashlib.sha256(output).hexdigest()}
    Path(out).with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main(*sys.argv[1:])
