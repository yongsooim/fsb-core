#!/usr/bin/env python3
"""Record x86 results for native scalar reconstructions without stubbing calls."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG
from prepare_event0 import PE

def main(executable,output):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest();assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    initial=bytes(u.mem_read(0x4a5000,3882100));records=bytearray();count=0
    from unicorn.x86_const import UC_X86_REG_EAX
    def case(entry,args):
        nonlocal records,count
        u.mem_write(0x4a5000,initial);u.reg_write(UC_X86_REG_ESP,0x100f000);u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
        u.mem_write(0x100f000,struct.pack('<'+'I'*(len(args)+1),0x1000000,*args));u.emu_start(entry,0x1000000,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('nonreturn')
        if bytes(u.mem_read(0x4a5000,len(initial)))!=initial:raise RuntimeError('scalar unexpectedly changed game data')
        records+=struct.pack('<II',entry,len(args))+b''.join(struct.pack('<I',v) for v in args)+struct.pack('<I',u.reg_read(UC_X86_REG_EAX));count+=1
    values=[0,1,7,8,15,16,0x3ff8,0x4000,0x7ff8,0x8000,0xc000,0xffff,0x10000,0x7fffffff,0x80000000,0xfffffff8,0xffffffff]
    for value in values:
        for entry in [0x408de1,0x408e69]:case(entry,[value])
        case(0x499150,[value])
    for selector in range(19):
        for left in [0,1,0xffffffff,0x80000000,0x7fffffff]:
            for right in [1,2,31,32,33,0xffffffff]:
                if selector in [11,12] and left==0x80000000 and right==0xffffffff:continue
                case(0x41a126,[selector,left,right])
    data=b'FSBSCL1\0'+struct.pack('<I',count)+records;p=Path(output);p.write_bytes(data);p.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'cases':count,'substituted_calls':[],'whole_data_unchanged_per_case':True,'fixture_sha256':hashlib.sha256(data).hexdigest()},indent=2)+'\n');print('scalar cases',count)
if __name__=='__main__':main(*sys.argv[1:])
