#!/usr/bin/env python3
"""Original shared CRT random stream; no calls or instructions are substituted."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from prepare_event0 import PE

def main(executable,output):
    p=PE(executable);sha=hashlib.sha256(p.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in p.sections:u.mem_write(p.base+va,p.data[offset:offset+size])
    base=0x4a5000;state=0x6d1bf0;initial=bytes(u.mem_read(base,3882100))
    seeds=[0,1,0x7fff,0x7fffffff,0x80000000,0xffffffff];draws=64;records=bytearray()
    for seed in seeds:
        u.mem_write(base,initial);u.mem_write(state,struct.pack('<I',seed));records+=struct.pack('<II',seed,draws)
        for _ in range(draws):
            before=bytearray(u.mem_read(base,len(initial)));u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<I',0x1000000));u.emu_start(0x498090,0x1000000,count=1000)
            assert u.reg_read(UC_X86_REG_EIP)==0x1000000
            next_state=bytes(u.mem_read(state,4));before[state-base:state-base+4]=next_state
            assert bytes(u.mem_read(base,len(initial)))==bytes(before)
            records+=struct.pack('<I',u.reg_read(UC_X86_REG_EAX))+next_state
    data=b'FSBRNG1\0'+struct.pack('<I',len(seeds))+records;out=Path(output);out.write_bytes(data)
    out.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'fixture_sha256':hashlib.sha256(data).hexdigest(),'cases':len(seeds)*draws,'seeds':len(seeds),'draws_per_seed':draws,'substituted_calls':[],'only_changed_data_field':hex(state)},indent=2)+'\n')
    print('original random draws',len(seeds)*draws)
if __name__=='__main__':main(*sys.argv[1:])
