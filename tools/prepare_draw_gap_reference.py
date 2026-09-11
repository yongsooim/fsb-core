#!/usr/bin/env python3
"""Run original4546f4; retain its draw geometry, isolate DirectDraw pixel policy."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from prepare_event0 import PE

def main(executable,output):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,off in pe.sections:u.mem_write(pe.base+va,pe.data[off:off+size])
    def read(a,n=1):return struct.unpack('<'+'I'*n,u.mem_read(a,n*4))
    def write(a,values):u.mem_write(a,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
    source=[0 if i%3==0 else 5 for i in range(16)];calls=[];pixels=[]
    def draw(uc,entry,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);args=read(sp+4,6);rectangle=entry in [0x405eab,0x405ed1];clipped=entry in [0x405dd6,0x405ed1]
        sr=tuple(int(v if v<0x80000000 else v-0x100000000) for v in read(args[3] if rectangle else args[4],4))
        dr=read(args[1],4) if rectangle else (args[1],args[2],args[1]+sr[2]-sr[0],args[2]+sr[3]-sr[1])
        flags=args[4] if rectangle else args[5];key=bool(flags&(0x8000 if rectangle else 1));calls.append({'entry':hex(entry),'source':sr,'destination':dr,'flags':flags,'clipped':clipped})
        if dr[2]>dr[0] and dr[3]>dr[1]:
            for y in range(max(0,dr[1]),min(24,dr[3])):
                for x in range(max(0,dr[0]),min(24,dr[2])):
                    if clipped and not (8<=x<18 and 8<=y<18):continue
                    sx=sr[0]+(x-dr[0])*(sr[2]-sr[0])//(dr[2]-dr[0]);sy=sr[1]+(y-dr[1])*(sr[3]-sr[1])//(dr[3]-dr[1])
                    if 0<=sx<4 and 0<=sy<4:
                        color=source[sy*4+sx]
                        if not key or color:pixels[y*24+x]=color
        uc.reg_write(UC_X86_REG_EAX,0);uc.reg_write(UC_X86_REG_EIP,read(sp)[0]);uc.reg_write(UC_X86_REG_ESP,sp+28)
    for entry in [0x405db3,0x405dd6,0x405eab,0x405ed1]:u.hook_add(UC_HOOK_CODE,draw,begin=entry,end=entry)
    records=bytearray();audit=[]
    command=0x1001000;sheet=0x1003000
    for kind in list(range(4))+list(range(5,14)):
        for key in [0,1]:
            pixels[:]=[9]*576;calls.clear();u.mem_write(command,bytes(84));u.mem_write(sheet,bytes(68))
            write(0x787480,[1]);write(0x787498,[command]);write(0x6db16c,[0xdead0020]);write(0x6da548,[8,8,18,18])
            write(sheet+0x20,[1]);u.mem_write(sheet+0x2c,struct.pack('<HHH',4,4,1));write(sheet+0x40,[0xdead0010])
            flags=key*(0x8000 if kind in [5,6,7,8,11,12] else 1)
            write(command+4,[kind]);write(command+0x14,[6,6]);write(command+0x1c,[0,0,4,4]);write(command+0x2c,[6,6,14,14]);write(command+0x3c,[sheet,0,flags,0,0,0xdead0010])
            write(0x100f000,[0x1000000]);u.reg_write(UC_X86_REG_ESP,0x100f000);u.emu_start(0x4546f4,0x1000000,count=100000)
            if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('draw dispatcher did not return')
            records+=struct.pack('<II',kind,flags)+bytes(pixels);audit.append({'kind':kind,'flags':flags,'original_calls':list(calls)})
    data=b'FSBGFX1\0'+struct.pack('<I',len(audit))+records;path=Path(output);path.write_bytes(data)
    path.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'fixture_sha256':hashlib.sha256(data).hexdigest(),'cases':audit,'scope':'Original dispatcher geometry/clipping/flags. CPU nearest indexed sampling is the existing portable output policy, not a Windows driver pixel oracle. Type4 checker has its separate original fixture.'},indent=2)+'\n')
    print('original draw dispatcher cases',len(audit))
if __name__=='__main__':main(*sys.argv[1:])
