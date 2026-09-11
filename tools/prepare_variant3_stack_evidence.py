# Original x86 evidence: variant3 tail depends on caller stack contents.
import struct,json,sys,hashlib
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
from prepare_event0 import PE
p=PE(sys.argv[1]);assert hashlib.sha256(p.data).hexdigest()=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454';u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
for _,a,n,o in p.sections:u.mem_write(p.base+a,p.data[o:o+n])
def w(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
def r(a):return struct.unpack('<I',u.mem_read(a,4))[0]
actor=0x8073d8;result=[]
for state in [0,7]:
 for phase in [15,16,17,18,19]:
  outputs=[]
  for fill in [0,0x55]:
   u.mem_write(0x1000000,bytes([fill])*65536);u.mem_write(actor,bytes(428))
   for a,v in [(actor+4,0x10170),(actor+0x14,0x38000),(actor+0x18,0x68000),(actor+0x1c,0x8000),(actor+0x2c,1),(actor+0x104,state),(actor+0x108,1),(actor+0x130,8),(0x80465c,9),(0x6da2d4,phase),(0x5d2258,3),(0x607a08+3*188+8,0),(0x607a08+3*188+24,100),(0x607a08+3*188+28,100),(0x100f000,0x1000000),(0x100f004,actor)]:w(a,v)
   u.reg_write(UC_X86_REG_ESP,0x100f000);u.emu_start(0x45cc1b,0x1000000,count=10000)
   assert u.reg_read(UC_X86_REG_EIP)==0x1000000
   outputs.append(hex(r(actor+0x138)))
  result.append({'state':state,'phase':phase,'zero_stack':outputs[0],'poisoned_stack':outputs[1]})
print(json.dumps(result,indent=2));Path(sys.argv[2]).write_text(json.dumps({'source_sha256':'710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454','entry':'0x45cc1b','substituted_calls':[],'cases':result},indent=2)+'\n')
