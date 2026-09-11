#!/usr/bin/env python3
"""Original reveal/scatter setup, RNG and blit geometry; timer/DDraw isolated."""
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
    def rd(a,n=1):return struct.unpack('<'+'I'*n,u.mem_read(a,n*4))
    def wr(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*(x&0xffffffff for x in v)))
    handles={0x1001100:1,0x1001500:2};state={};counts={0x401dcf:2,0x401f3a:0,0x405c8c:3,0x405db3:6,0x405f23:5,0x1001200:6,0x1001300:2}
    def hook(u,a,size,data):
        sp=u.reg_read(UC_X86_REG_ESP);args=rd(sp+4,counts[a]);result=0
        if a==0x1001300:u.mem_write(args[0],bytes(u.mem_read(args[1],16)));result=1
        elif a==0x405db3:
            src=rd(args[4],4);dst=(args[1],args[2],args[1]+src[2]-src[0],args[2]+src[3]-src[1]);state['ops'].append((0,1,2,*dst,*src))
        elif a==0x1001200:state['ops'].append((1,1,2,*rd(args[1],4),*rd(args[3],4)))
        elif a==0x405f23:state['ops'].append((1,0,1,*rd(args[0],4),*rd(args[2],4)))
        elif a==0x401f3a:result=state['progress']
        u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,rd(sp)[0]);u.reg_write(UC_X86_REG_ESP,sp+4+counts[a]*4)
    for a in counts:u.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
    wr(0x8594c4,[0x1001300]);wr(0x1001100,[0x1001000]);wr(0x1001014,[0x1001200]);wr(0x6d6284,[0x1001100]);wr(0x6db16c,[0x1001500])
    def call(a,args):
        u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
        u.reg_write(UC_X86_REG_ESP,0x100f000);wr(0x100f000,[0x1000000,*args]);u.emu_start(a,0x1000000,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('original callback did not return')
    cases=[]
    for kind in [1,2]:
      for height in [450,466,480]:
       top=(480-height)//2;view=[0,top,640,top+height]
       for progress in [0,1,751,7507,15015,22522,30029,30030]:
        seed=0x80000000+progress;wr(0x6d1bf0,[seed]);wr(0x1001400,view);state.update(progress=progress,ops=[])
        call(0x401fab if kind==1 else 0x402070,[1,0x1001400,2000]);points=rd(0x6e0e10,288) if kind==2 else ();rng=rd(0x6d1bf0)[0]
        call(0x401fd4 if kind==1 else 0x40213b,[])
        cases.append(([kind,1,progress,seed,*view,rng,len(points)],points,state['ops']))
    data=bytearray(b'FSBTRN2\0')+struct.pack('<I',len(cases))
    def words(values):return struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values))
    for header,points,ops in cases:
        data+=words(header)+words(points)+words([len(ops)])
        for op in ops:data+=words(op)
    path=Path(output);path.write_bytes(data);report={'source_sha256':sha,'cases':len(cases),'original_routines':['401fab','401fd4','402070','40213b','404343','498090'],'substitutions':['401dcf timer setup omitted','401f3a explicit progress','DirectDraw blits recorded','CopyRect copies16bytes'],'fixture_sha256':hashlib.sha256(data).hexdigest()};path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main(*sys.argv[1:])
