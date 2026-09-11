#!/usr/bin/env python3
"""Original target distance/heading, with the shipped CRT floating environment."""
from pathlib import Path
import sys,struct,json,random,math
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
ACTOR=0x8073d8

def main(snapshot,output):
 bootstrap=Oracle(snapshot);bootstrap.u.reg_write(UC_X86_REG_FPCW,0x37f);bootstrap.case(0x49a2f0)
 derived=bootstrap.u.reg_read(UC_X86_REG_FPCW);assert derived&0xf3f==0x23f
 control=derived|0x40 # x87 reserved bit6; effective precision/masks match the original initializer.
 o=Oracle(snapshot);visited=set()
 ranges={0x462f5b:0x463038,0x463038:0x4630ed,0x4630ed:0x4631a2}
 instructions={a:{i.address for i in Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(o.u.mem_read(a,b-a)),a)} for a,b in ranges.items()}
 o.u.hook_add(UC_HOOK_CODE,lambda u,a,size,user:visited.add(a),begin=0x462f5b,end=0x4631a1)
 constants={hex(a):{'value':struct.unpack('<d',o.u.mem_read(a,8))[0], 'bits':bytes(o.u.mem_read(a,8)).hex()} for a in range(0x4a2808,0x4a2830,8)}
 ordinal=0
 def case(entry,position,target,mode=0):
  nonlocal ordinal
  o.u.reg_write(UC_X86_REG_FPCW,control);o.u.reg_write(UC_X86_REG_FPSW,0);o.u.reg_write(UC_X86_REG_FPTAG,0xffff)
  writes=[(ACTOR+off,4,value&0xffffffff) for off,value in zip([8,12,16,0x184,0x188,0x18c],position+target)]
  writes += [(ACTOR+0x168,4,mode),(0x8577fc,4,0xaabbccdd if ordinal&1 else 0)]
  o.case(entry,[ACTOR],writes,0xffffffff);ordinal+=1
 edge=[0,1,-1,65535,65536,65537,0x7fffffff,-0x80000000,-0x7fffffff]
 for x in edge:
  for y in edge:
   for mode in range(0,128,16):case(0x462f5b,(x,y,17),(0,0,0),mode|0x8000010f)
 # Rounding near integer roots, plus unequal Z proving the composite XY path.
 for size in [3,4,46340,65535,65536,1000000,0x7ffffffe,0x7fffffff]:
  for dy in [0,1,-1,2,-2]:
   for mode in [0x30,0x50,0x60,0x70]:case(0x462f5b,(size,dy,-size),(0,0,0),mode)
 rng=random.Random(0x462f5b)
 for _ in range(256):
  position=tuple(rng.getrandbits(32) for _ in range(3));target=tuple(rng.getrandbits(32) for _ in range(3))
  for mode in range(0,128,16):case(0x462f5b,position,target,mode)
 for x in edge:
  for y in edge:
   for entry in [0x463038,0x4630ed]:case(entry,(0,0,0),(x,y,0))
 for angle in list(range(0,65536,2053))+[1,16383,16384,16385,32767,32768,49151,49152,65535]:
  radians=angle*6.283185307/65536
  for radius in [1024,1000000,2147483000]:
   x=round(math.sin(radians)*radius);y=round(-math.cos(radians)*radius)
   for jitter in [-1,0,1]:
    for entry in [0x463038,0x4630ed]:case(entry,(0,0,0),(x+jitter,y,0))
 for _ in range(512):
  position=tuple(rng.getrandbits(32) for _ in range(3));target=tuple(rng.getrandbits(32) for _ in range(3))
  for entry in [0x463038,0x4630ed]:case(entry,position,target)
 o.write(output,'target_geometry','Original three routines plus actual CRT abs/sqrt/atan/FTOL; no substituted calls. Per-case CRT precision53, clean FPU stack/status; compares full EAX and data/errno. Includes wrapped coordinate differences, INT_MIN magnitude ordering, coincident endpoints, modes and angular integer boundaries.')
 p=Path(output).with_suffix('.json');d=json.loads(p.read_text());d['fpu_initialization']={'initializer':'0x49a2f0','caller':'0x4980c0','derived_control_word':hex(derived),'case_control_word':hex(control),'fpsw':0,'fptag':'0xffff'}
 d['constants']=constants;d['original_instruction_coverage']={hex(a):{'total':len(v),'visited':len(v&visited),'unvisited':[hex(x) for x in sorted(v-visited)]} for a,v in instructions.items()};p.write_text(json.dumps(d,indent=2)+'\n')
 print('coverage',d['original_instruction_coverage'])
if __name__=='__main__':main(*sys.argv[1:])
