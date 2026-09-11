#!/usr/bin/env python3
"""Actual0x40758d keyboard hook, with explicit GetKeyState observations.
The only substituted API reads the supplied Shift/Control state. No script,
timer or completion callback is replaced.
"""
from pathlib import Path
import hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP,UC_X86_REG_ESP
from prepare_event0 import PE

def main(exe,output):
    p=PE(exe);sha=hashlib.sha256(p.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
    for _,va,n,off in p.sections:u.mem_write(p.base+va,p.data[off:off+n])
    # cmp [esp+4],VK_SHIFT; return supplied shift/control state, stdcall.
    u.mem_write(0x1000800,bytes.fromhex('837c2404107508a100090001c20400a104090001c20400'))
    u.mem_write(0x859520,struct.pack('<I',0x1000800))
    cases=[(13,0x1c,1,0,0,0,0),(13,0x1c,1,0,0,0,1),(13,0x1c,0,0,0,0,0),
           (32,0x39,1,0,0,0,0),(32,0x39,0,0,0,0,0),(16,0x2a,1,1,0,0,0),
           (13,0x1c,1,1,0,0,0),(13,0x1c,0,1,0,0,0),(16,0x2a,0,0,0,0,0),
           (17,0x1d,1,0,1,0,0),(38,0xc8,1,0,1,0,0),(38,0xc8,0,0,1,0,0),(17,0x1d,0,0,0,0,0),
           (18,0x38,1,0,0,1,0),(65,0x1e,1,0,0,1,0),(65,0x1e,0,0,0,1,0),(18,0x38,0,0,0,0,0),
           (101,0x4c,1,0,0,0,0),(101,0x4c,0,0,0,0,0),(88,0x2d,1,0,0,0,0),(88,0x2d,0,0,0,0,0),
           (97,0x4f,1,0,0,0,0),(97,0x4f,0,0,0,0,0)]
    addresses=[0x6d66dc,0x74b474,0x6e12b8,0x4a53c0,0x6da2c8,0x6d9d24,0x6d9d44]+list(range(0x6da568,0x6da968,4))+list(range(0x6dad68,0x6db168,4))
    blob=bytearray(b'FSBKEY1\0'+struct.pack('<II',len(cases),len(addresses))+struct.pack('<'+'I'*len(addresses),*addresses))
    for key,scan,down,shift,control,alt,repeat in cases:
        flags=1|((scan&127)<<16)|(0x1000000 if scan&128 else 0)|(0x20000000 if alt else 0)|(0x40000000 if repeat or not down else 0)|(0x80000000 if not down else 0)
        u.mem_write(0x1000900,struct.pack('<II',0x8000 if shift else 0,0x8000 if control else 0));u.reg_write(UC_X86_REG_ESP,0x100f000)
        u.mem_write(0x100f000,struct.pack('<4I',0x1000000,0,key,flags));u.emu_start(0x40758d,0x1000000,count=10000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('keyboard hook did not return')
        blob+=struct.pack('<7I',key,scan,down,shift,control,alt,repeat)
        for address in addresses:blob+=u.mem_read(address,4)
    out=Path(output);out.write_bytes(blob);report={'exe_sha256':sha,'routine':'0x40758d actual keyboard hook','GetKeyState':'explicit supplied modifier observations','sequence_cases':len(cases),'words_compared_per_case':len(addresses),'binary_sha256':hashlib.sha256(blob).hexdigest()};out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main(*sys.argv[1:])
