#!/usr/bin/env python3
"""Original entrance, marker lifetime and status-visual cleanup contracts."""
from pathlib import Path
import sys,struct,json
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
A,OBJ,T,C=0x8073d8,0x810a50,0x6dc000,0x6deb00
CALLS={0x45d89c:1,0x45d91d:1,0x45d774:4,0x45d889:1,0x498090:0,0x44c1fc:2,
       0x4644d3:0,0x4337f4:6,0x4544cf:4,0x45451c:2,0x44ab05:0,0x462370:2}
OBS=[0x77ec04,0x77ecd8,0x803a20,0x776484,0x77ec60,0x607a10,0x806b68,0x7873b8]
def main(snapshot,output,mode='isolated'):
 o=Oracle(snapshot)
 ranges={0x44c1fc:0x44c2f4,0x44c2f4:0x44c313,0x44c313:0x44c337,0x44c337:0x44c3b6,0x44c3b6:0x44c6cb,0x44c954:0x44caee}
 instructions={start:{i.address for i in Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(o.u.mem_read(start,end-start)),start)} for start,end in ranges.items()}
 visited=set()
 o.u.hook_add(UC_HOOK_CODE,lambda u,a,size,user:visited.add(a),begin=0x44c1fc,end=0x44caed)
 def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
 def stop(u,entry,size,user):
  sp=u.reg_read(UC_X86_REG_ESP)
  if o.read(sp)==0x1000000:return #44c1fc is also an entry under test.
  n=CALLS[entry];args=[o.read(sp+4+i*4) for i in range(n)];ordinal=o.read(T)
  values=[entry,n]+args+[0]*(6-n)+[o.read(a) for a in OBS]
  for i,v in enumerate(values):put(T+4+ordinal*64+i*4,v)
  put(T,ordinal+1);o.requests[(entry,tuple(args))]+=1
  result={0x45d89c:OBJ,0x4644d3:41}.get(entry,0)
  if entry==0x498090:result=o.read(C+4)
  if o.read(C):
   if entry==0x45d774:
    put(0x775630,19);put(0x775634,20);put(0x77563c,3);put(0x7755dc,21);put(0x7755e0,22);put(0x803a20,2)
   if entry==0x45d91d:put(0x77ec60,OBJ+0x1ac)
   if entry==0x45d89c:put(0x77ec60,OBJ+0x1ac)
   if entry==0x45d889:put(0x806b64,2);put(0x609cb8,777)
   if entry==0x44c1fc:put(0x776484,2)
   if entry==0x462370:
    put(0x607a10,o.read(0x607a10)^0x3ff00);put(0x806b68,o.read(0x806b68)^0x3ff00)
    put(0x803a20,2);put(0x776484,2);put(0x5d2258,2);put(0x806b60,61)
   if entry in [0x45451c,0x4544cf]:put(0x77a4e0,99)
  u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n*4)
 for entry in CALLS if mode=='isolated' else [0x4337f4]:
  o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry);o.substituted[hex(entry)]='call trace and configured mutation' if mode=='isolated' else 'audio-device transition trace'
 def setup(mutation=0):
  w=[(T+i*4,4,0) for i in range(1024)]+[(C,4,mutation),(C+4,4,12345)]
  w += [(0x80465c,4,9),(0x77ec04,4,0),(0x77ecd8,4,0),(0x77ec50,4,0),
        (0x803a20,4,1),(0x776484,4,1),(0x787478,4,0),(0x7873b8,4,0),
        (0x77a500,4,0),(0x77a510,4,0),(0x803a1c,4,0),(0x77a4e0,4,0),(0x774168,4,0),
        (0x773004,4,0),(0x804aa8,4,2)]
  for i in range(4):
   w += [(0x5d2258+i*4,4,i),(0x607a10+i*0xbc,4,0),(0x806b60+i*36,4,60+i),
         (0x806b64+i*36,4,0),(0x806b68+i*36,4,0),(0x806b6c+i*36,4,0xaabbccdd),
         (0x775630+i*20,4,3+i),(0x775634+i*20,4,4+i),(0x775638+i*20,4,0),(0x77563c+i*20,4,2),
         (0x7755dc+i*16,4,3+i),(0x7755e0+i*16,4,4+i),(0x7755e4+i*16,4,0),(0x7755e8+i*16,4,2)]
  for i in [0,1,60,61,62,63]:
   a=A+i*0x1ac
   w += [(a+4,4,0x4000),(a+0x2c,4,0),(a+0x34,4,0),(a+0x38,4,21),(a+0x108,4,0),
         (a+0x110,4,0),(a+0x128,4,3),(a+0x12c,4,4)]
  w += [(OBJ,4,0xffffffff),(OBJ+4,4,0x40),(OBJ+0x108,4,0),(OBJ+0x148,4,0x44c313),(OBJ+0x14c,4,0)]
  w += [(0x77ec60+i*4,4,0) for i in range(30)]
  if mode=='integrated':
   w += [(0x805880+i*4,4,0) for i in range(32)]+[(0x805588+i*4,4,0) for i in range(32)]
   w += [(0x805a78+i*4,4,0) for i in range(320)]
  return w
 def case(entry,args=(),writes=(),mutation=0,mask=0):o.case(entry,args,setup(mutation)+list(writes),mask)
 for free in [0,1,29,30]:
  for mutation in ([0,1] if mode=='isolated' else [0]):
   case(0x44c337,[3,4,0],[(0x77ec60+i*4,4,OBJ) for i in range(free)],mutation,0xffffffff)
 for game in [0,3,9,0x109]:
  for slot in [0,1,29,0xffffffff]:
   for mutation in ([0,1] if mode=='isolated' else [0]):
    w=[(0x80465c,4,game),(OBJ+0x108,4,slot),(0x77ec60,4,OBJ)]
    case(0x44c313,[OBJ],w,mutation)
    case(0x44c2f4,[0],w,mutation)
 for kind in [0,23,1]:
  for refresh in [0,1,0x100,0x180]:
   for mutation in ([0,1] if mode=='isolated' else [0]):
    case(0x44c1fc,[0,refresh],[(0x806b64,4,kind)],mutation,0xffffffff)
 if mode=='isolated':
  for random in [0,19,20,0x7fffffff,0x80000000,0xffffffff]:case(0x44c1fc,[0,1],[(C+4,4,random)],0,0xffffffff)
 for phase in list(range(9))+[0xffffffff]:
  for staged in [0,1]:
   for immediate in [0,1]:
    for mutation in ([0,1] if mode=='isolated' else [0]):
     case(0x44c3b6,[staged,immediate],[(0x77ec04,4,phase)],mutation)
 for count in [0,2,0xffffffff]:
  for phase in [0,1]:case(0x44c3b6,[0,0],[(0x77ec04,4,phase),(0x803a20,4,count),(A+0x2c,4,1)])
 for pending in [0,1,0x100,0x101,2]:case(0x44c3b6,[0,0],[(0x77ec04,4,7),(0x7873b8,4,pending)])
 for ready in [0,1,0x100]:case(0x44c3b6,[0,0],[(0x77ec04,4,5),(0x77a500,4,ready)])
 for tick in list(range(33))+[0x7fffffff,0x80000000,0xffffffff]:case(0x44c3b6,[0,0],[(0x77ec04,4,6),(0x77a4e0,4,tick)])
 for phase in [3,4]:
  for done in [0,21]:
   for special in [0,23]:
    for count in [1,3]:case(0x44c3b6,[0,0],[(0x77ec04,4,phase),(A+60*0x1ac+4,4,0),
       (A+60*0x1ac+0x38,4,done),(0x806b64+36,4,special),(0x776484,4,count)])
 masks=[0,0xffffffff,0x600000,0x40000,0x40800]+[1<<i for i in range(8,22)]
 for flags in masks:
  for party in [0,1,2]:
   for mutation in ([0,1] if mode=='isolated' else [0]):
    w=[(0x607a10,4,flags),(0x806b68,4,flags),(0x803a20,4,party)]
    if mode=='integrated':
     for row,owner in enumerate([A,A+60*0x1ac]):
      w += [(0x805880+row*4,4,owner),(0x805588+row*4,4,10)]
      for kind in range(10):
       obj=OBJ+(row*10+kind)*0x1ac
       w += [(0x805a78+(row*10+kind)*4,4,obj),(0x805f78+(row*10+kind)*4,4,kind+1),
             (obj,4,0xffffffff),(obj+0x148,4,0),(obj+0x14c,4,0)]
    case(0x44c954,[],w,mutation)
 o.write(output,'entrance_'+mode,'Original instructions only. Isolated callbacks mutate stage rows, counts, identities and status bits. Integrated runs real spawning/finalizing, positioning, registry, RNG and camera; only audio transition is substituted. Return masks preserve activation quotient/staged Y and marker pointer.')
 report_path=Path(output).with_suffix('.json');report=json.loads(report_path.read_text())
 report['original_instruction_coverage']={hex(entry):{'total':len(expected),'visited':len(expected&visited),'unvisited':[hex(a) for a in sorted(expected-visited)]} for entry,expected in instructions.items()}
 report_path.write_text(json.dumps(report,indent=2)+'\n')
 print('original_instruction_coverage',report['original_instruction_coverage'])
if __name__=='__main__':main(*sys.argv[1:])
