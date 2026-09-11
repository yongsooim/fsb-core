#!/usr/bin/env python3
"""Original anchored effects: RNG, callback ordering and motion integration."""
from pathlib import Path
import sys,struct,json
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
P,A,O,T,C=0x8073d8,0x807584,0x810a50,0x6dc000,0x6deb00
CALLS={0x45d89c:1,0x45d91d:1,0x447841:2,0x45d208:1,0x465e0b:1,0x498090:0,0x4626ab:1}
OBS=[P+0x14c,P+0x16c,P+0x198,P+0x160,P+0x10,P+0x18c,P+0x3c,O+0x160,O+0x14c,O+4]
def main(snapshot,output,mode='isolated'):
 o=Oracle(snapshot);visited=set()
 ranges={0x4624ad:0x46256a,0x46256a:0x46265d,0x4626ab:0x46274d,0x46274d:0x462790,
         0x4629cb:0x462a49,0x462a49:0x462b50,0x462b50:0x462c06}
 instructions={a:{i.address for i in Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(o.u.mem_read(a,b-a)),a)} for a,b in ranges.items()}
 o.u.hook_add(UC_HOOK_CODE,lambda u,a,size,user:visited.add(a),begin=0x4624ad,end=0x462c05)
 def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
 def stop(u,entry,size,user):
  sp=u.reg_read(UC_X86_REG_ESP)
  if o.read(sp)==0x1000000:return
  n=CALLS[entry];args=[o.read(sp+4+i*4) for i in range(n)];ordinal=o.read(T)
  values=[entry,n]+args+[0]*(2-n)+[o.read(a) for a in OBS]
  for i,v in enumerate(values):put(T+4+ordinal*64+i*4,v)
  put(T,ordinal+1);o.requests[(entry,tuple(args))]+=1
  result=0
  if entry==0x45d89c:result=O+o.read(C+8)*0x1ac;put(C+8,o.read(C+8)+1)
  if entry==0x498090:result=o.read(C+4);put(C+4,result+1)
  if o.read(C):
   if entry==0x447841:put(P+0x14c,5);put(P+0x16c,100);put(args[0]+0x160,A+0x1ac)
   if entry==0x45d208:put(P+0x160,A+0x1ac);put(P+0x1a0,o.read(P+0x1a0)+3)
   if entry==0x498090:put(P+0x10,80<<16);put(P+0x18c,47<<16);put(A+8,o.read(A+8)+100)
   if entry==0x45d91d:put(P+0x40,O+3*0x1ac)
   if entry==0x4626ab:put(P+0x198,77)
  u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n*4)
 if mode=='isolated':
  for entry in CALLS:o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry);o.substituted[hex(entry)]='ordered trace, RNG stream and configured state mutation'
 def setup(state=0,mutation=0):
  w=[(T+i*4,4,0) for i in range(1024)]+[(C,4,mutation),(C+4,4,12345),(C+8,4,0)]
  w += [(0x80465c,4,9),(P+0x14c,4,state),(P+0x144,4,24),(P+0x160,4,A),
        (P+0x16c,4,24),(P+0x198,4,24),(P+4,4,0),(P+0x148,4,0),
        (P+0x10,4,64<<16),(P+0x18c,4,48<<16),(P+0x184,4,0xffff0000),(P+0x188,4,0x20000),
        (P+0x168,4,0),(P+0x174,4,0),(P+0x180,4,0),(P+0x190,4,32),(P+0x194,4,24),
        (P+0x19c,4,0),(P+0x1a0,4,0),(P+0x1a4,4,0)]
  for i in range(2):w += [(A+i*0x1ac+8,4,0x7fffffff),(A+i*0x1ac+12,4,0x80000000),(A+i*0x1ac+16,4,0),(A+i*0x1ac+28,4,i<<16)]
  for i in range(4):
   obj=O+i*0x1ac
   w += [(obj,4,0xffffffff),(obj+4,4,0),(obj+0x148,4,0),(obj+0x14c,4,0),
         (obj+0x160,4,A),(obj+0x168,4,0)]
  w += [(P+0x3c+i*4,4,O+i*0x1ac) for i in range(3)]
  if mode=='integrated':
   # These are spare slots; make their free state explicit while retaining IDs.
   w += [(at,4,0) for at in range(0x810a54,0x85712c,0x1ac)]
  return w
 def case(entry,state=0,w=(),mutation=0):o.case(entry,[P],setup(state,mutation)+list(w),0)
 for entry in ranges:
  for state in [0,10,1,0xffffffff,0xfffffffe]:
   for mutation in ([0,1] if mode=='isolated' else [0]):case(entry,state,mutation=mutation)
 for entry in [0x46256a,0x46274d]:
  for tick in [0,10,23,24,25,0xffffffff]:
   for due in [10,24,0xffffffff]:
    for mutation in ([0,1] if mode=='isolated' else [0]):case(entry,0,[(P+0x144,4,tick),(P+0x16c,4,due),(P+0x198,4,due)],mutation)
 for script_flag in [0,0x20000,0x200,0x2000000]:case(0x4624ad,0,[(P+4,4,script_flag)])
 for z,target in [(64,48),(48,48),(47,48),(-3,-2),(-1,-2)]:
  for random in [0,49,63,0x7fffffff]:
   for mutation in ([0,1] if mode=='isolated' else [0]):case(0x4624ad,10,[(P+0x10,4,(z<<16)&0xffffffff),(P+0x18c,4,(target<<16)&0xffffffff),(C+4,4,random)],mutation)
 for phase in ([0,0xfdff,0xfe00,0xffff,0x10000,0x20000,0x7fffffff,0x80000000,0xffffffff] if mode=='isolated' else [0,0xfdff,0xfe00,0xffff]):
  for mutation in ([0,1] if mode=='isolated' else [0]):case(0x4629cb,0,[(P+0x1a0,4,phase)],mutation)
 for entry in [0x462a49,0x462b50]:
  for mask in range(8):
   for mutation in ([0,1] if mode=='isolated' else [0]):case(entry,0xfffffffe,[(P+0x3c+i*4,4,(O+i*0x1ac) if mask&(1<<i) else 0) for i in range(3)],mutation)
 for entry in [0x4626ab,0x462a49,0x46256a]:
  for random in [0,999,1000,4095,4096,0x7fffffff,0x80000000,0xffffffff]:
   if mode=='isolated':case(entry,0,[(C+4,4,random)])
 # Shared45d208 regression: NEG(INT_MIN), velocity carry and absolute/relative wrap.
 for shape in [0x71,0x72,0x74]:
  for position in [0,0x7fffffff,0x80000000]:
   for step in [1,0x80000000]:
    w=[(P+0x168,4,shape),(P+0x19c,4,0x4000),(P+0x1a0,4,0),(P+0x1a4,4,0)]
    for at in [8,12,16,0x184,0x188,0x18c]:w += [(P+at,4,position)]
    for at in [0x16c,0x170,0x174]:w += [(P+at,4,0x7fffffff)]
    for at in [0x178,0x17c,0x180]:w += [(P+at,4,step)]
    case(0x45d208,0,w)
 o.write(output,'anchor_effects_'+mode,'Original instructions determine data and ordered call expectations. Isolated mode mutates live state and supplies signed RNG edge values. Integrated mode runs all original lower calls without substitution, including spawning, script, oscillation, sine/cosine, sparks and finalization.')
 report_path=Path(output).with_suffix('.json');report=json.loads(report_path.read_text())
 report['original_instruction_coverage']={hex(a):{'total':len(expected),'visited':len(expected&visited),'unvisited':[hex(v) for v in sorted(expected-visited)]} for a,expected in instructions.items()}
 report['division_faults']=[]
 if mode=='isolated':
  for z,target,random in [(0,0,0),(-1,-2,0x80000000)]:
   try:case(0x4624ad,10,[(P+0x10,4,(z<<16)&0xffffffff),(P+0x18c,4,(target<<16)&0xffffffff),(C+4,4,random)])
   except RuntimeError:
    pc=o.u.reg_read(UC_X86_REG_EIP);assert pc==0x462515,hex(pc)
    report['division_faults'].append({'height':z,'target':target,'random':random,'instruction':hex(pc)})
   else:raise RuntimeError('expected original division fault')
 report_path.write_text(json.dumps(report,indent=2)+'\n')
 print('original_instruction_coverage',report['original_instruction_coverage'])
if __name__=='__main__':main(*sys.argv[1:])
