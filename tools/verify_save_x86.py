#!/usr/bin/env python3
"""Compare portable Save1.dat with original writer; substitute only I/O/heap/time."""
from pathlib import Path
import hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from prepare_event0 import PE

def main(exe,snapshot,expected,out):
    pe=PE(exe);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000);u.mem_map(0x2000000,0x200000)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    data=Path(snapshot).read_bytes();cursor=8;count=struct.unpack_from('<I',data,cursor)[0];cursor+=4;pages=set()
    for _ in range(count):
        base,size=struct.unpack_from('<II',data,cursor);cursor+=8
        if not 0x400000<=base<0xa00000:
            for page in range(base&~4095,(base+size+4095)&~4095,4096):
                if page not in pages:u.mem_map(page,4096);pages.add(page)
        u.mem_write(base,data[cursor:cursor+size]);cursor+=size
    def word(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    def string(at):
        s=bytearray()
        while True:
            b=bytes(u.mem_read(at,1));at+=1
            if b==b'\0':return s.decode('ascii')
            s+=b
    streams={};files={};heap=0x2000000;calls=[]
    def hook(uc,address,size,_):
        nonlocal heap
        if address not in [0x498670,0x4985c0,0x498870,0x498690,0x4974a0,0x4972b0,0x1008000]:return
        sp=uc.reg_read(UC_X86_REG_ESP);arg=lambda n:word(sp+4+n*4);result=0
        if address==0x498670:
            name,mode=string(arg(0)),string(arg(1))
            if mode=='wb':result=1;streams[result]=(name,bytearray());files[name]=b''
        elif address==0x498870:
            name,b=streams[arg(3)];b+=bytes(uc.mem_read(arg(0),arg(1)*arg(2)));result=arg(2)
        elif address==0x4985c0:
            name,b=streams.pop(arg(0));files[name]=bytes(b)
        elif address==0x4974a0:result=heap;heap+=(arg(0)+15)&~15
        elif address==0x1008000:result=word(0x6da2d8)
        calls.append(hex(address));uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_EIP,word(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
    u.mem_write(0x8594a0,struct.pack('<I',0x1008000));u.hook_add(UC_HOOK_CODE,hook)
    u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<II',0x1000000,1));u.emu_start(0x460e58,0x1000000,count=5000000)
    actual=files.get('Save1.dat',b'');wanted=Path(expected).read_bytes()
    result={'exe_sha256':hashlib.sha256(pe.data).hexdigest(),'writer':'0x460e58','original_return':u.reg_read(UC_X86_REG_EAX),'original_bytes':len(actual),'portable_bytes':len(wanted),'byte_identical':actual==wanted,'substituted_boundaries':sorted(set(calls)),'first_differences':[i for i,(a,b) in enumerate(zip(actual,wanted)) if a!=b][:16]}
    Path(out).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));return 0 if actual==wanted else 1
if __name__=='__main__':raise SystemExit(main(*sys.argv[1:]))
