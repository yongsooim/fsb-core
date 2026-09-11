#!/usr/bin/env python3
"""Original CRT sqrt result bits and guest errno, including exceptional domains."""
import hashlib,json,math,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG
from prepare_event0 import PE

def main(executable,output):
    pe=PE(executable);sha=hashlib.sha256(pe.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    initial=bytes(u.mem_read(0x4a5000,3882100));cases=[]
    # The return continuation stores ST0, then jumps to a distinct stop address.
    u.mem_write(0x1000000,b'\xdd\x1d\x00\x20\x00\x01\xe9\xf5\x00\x00\x00')
    for value in [0.,-0.,9.,math.inf,-1.,math.nan,-math.inf]:
        u.mem_write(0x4a5000,initial);u.mem_write(0x100f000,struct.pack('<Id',0x1000000,value))
        u.reg_write(UC_X86_REG_ESP,0x100f000);u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
        u.emu_start(0x498174,0x1000100,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000100:raise RuntimeError('original sqrt did not return')
        bits=bytes(u.mem_read(0x1002000,8))
        cases.append({'input':str(value),'input_bits':struct.pack('<d',value).hex(),'bits':bits.hex(),'result':str(struct.unpack('<d',bits)[0]),'errno':int.from_bytes(u.mem_read(0x8577fc,4),'little')})
    report={'source_sha256':sha,'entry':'0x498174','method':'Unicorn original CRT, FPCW0x27f; stores ST0 with FSTP after returning; includes guest errno','cases':cases}
    Path(output).write_text(json.dumps(report,indent=2)+'\n');print('original sqrt domain cases',len(cases))
if __name__=='__main__':main(*sys.argv[1:])
