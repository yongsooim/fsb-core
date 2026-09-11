#!/usr/bin/env python3
"""Replay captured party lifecycle states in original x86; no calls substituted."""
import struct,sys,json,hashlib
from pathlib import Path
from unicorn import *
from unicorn.x86_const import *
b=Path(sys.argv[1]).read_bytes();p=12;regions=[];pages=set()
for _ in range(struct.unpack_from('<I',b,8)[0]):
 a,n=struct.unpack_from('<II',b,p);p+=8;regions.append((a,b[p:p+n]));pages.update(range(a&~4095,(a+n+4095)&~4095,4096));p+=n
u=Uc(UC_ARCH_X86,UC_MODE_32)
for page in sorted(pages):u.mem_map(page,4096)
u.mem_map(0x1000000,65536)
for a,data in regions:u.mem_write(a,data)
extra=len(sys.argv)>3 and sys.argv[3]=='extra';entry=0x4307e3 if extra else 0x430434;args=[0x30023,1] if extra else [1]
u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<'+'I'*(1+len(args)),0x1000000,*args))
u.emu_start(entry,0x1000000,count=1000000)
if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('original replacement did not return')
output=Path(sys.argv[2]);expected=bytes(u.mem_read(0x4a5000,3882100));output.write_bytes(expected)
report={'entry':hex(entry),'arguments':args,'substituted_calls':[],'input_sha256':hashlib.sha256(b).hexdigest(),'output_sha256':hashlib.sha256(expected).hexdigest(),'compared_bytes':len(expected)}
output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
