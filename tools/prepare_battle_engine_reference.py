#!/usr/bin/env python3
"""Original44d45a phase contracts; expected data is produced only by x86."""
from pathlib import Path
import struct,sys,json
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
ACTOR,TRACE,CONFIG=0x8073d8,0x6dc000,0x6deb00
SERVICES={0x44c6eb:0,0x44cbd2:0,0x44d389:1,0x44d40a:1,0x44d233:0,0x44cd05:1,
 0x4544cf:4,0x44d297:0,0x44cb26:2,0x44cb7a:1,0x44d345:2,0x461a4f:0,0x44d17b:0,
 0x461a91:0,0x44d2d8:0,0x43955f:0,0x4394ea:0,0x40bb53:0,0x45149b:2,0x44cdf3:4,
 0x451746:3,0x44d102:2,0x44e7e5:0,0x461d82:1,0x461854:2,0x461903:1,0x461b11:1,
 0x461ca2:0,0x44c954:0,0x44ab05:0,0x44ab2e:0}
OBSERVED=[0x775cac,0x775cb0,0x7757e0,0x77ec4c,0x77a568,0x77a50c,0x77e570,0x774188,0x804a60,ACTOR+4]
RETURNS={0x44d233:8,0x44cd05:12,0x44cb26:20,0x44cb7a:24,0x461a91:28,0x461ca2:32,0x43955f:36,0x44d102:40}
def main(snapshot,output,mode='isolated'):
 o=Oracle(snapshot)
 instructions={i.address for i in Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(o.u.mem_read(0x44d45a,0x886)),0x44d45a)}
 visited=set()
 o.u.hook_add(UC_HOOK_CODE,lambda u,a,size,user:visited.add(a),begin=0x44d45a,end=0x44dcdf)
 def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
 def stop(u,entry,size,user):
  sp=u.reg_read(UC_X86_REG_ESP);n=SERVICES[entry];args=[o.read(sp+4+i*4) for i in range(n)]
  ordinal=o.read(TRACE);values=[entry,n]+args+[0]*(4-n)+[o.read(a) for a in OBSERVED]
  for i,v in enumerate(values):put(TRACE+4+ordinal*64+i*4,v)
  put(TRACE,ordinal+1);o.requests[(entry,tuple(args))]+=1
  result=o.read(CONFIG+RETURNS[entry]) if entry in RETURNS else 0
  if entry==0x44cd05:put(args[0],o.read(CONFIG+16))
  if o.read(CONFIG):
   put(0x775cac,0x80+ordinal);put(0x7757e0,1);put(0x787478,1)
   put(0x77ec4c,o.read(0x77ec4c)^0x10);put(0x77a568,o.read(0x77a568)+1)
   put(ACTOR+0x110,3)
   if entry==0x44cdf3:put(ACTOR+0x104,o.read(ACTOR+0x104)^1)
   if entry==0x461d82:put(0x77ebfc,1)
   if entry==0x461ca2:put(0x774188,o.read(CONFIG+44))
  u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n*4)
 if mode=='isolated':
  for entry in SERVICES:
   o.u.hook_add(UC_HOOK_CODE,stop,begin=entry,end=entry);o.substituted[hex(entry)]='typed call trace plus configured returns and live-state mutations'
 def setup(phase=0,mutation=0):
  w=[(TRACE+i*4,4,0) for i in range(512)]+[(CONFIG+i*4,4,0) for i in range(16)]
  w += [(CONFIG,4,mutation),(CONFIG+8,4,0),(CONFIG+12,4,1),(CONFIG+16,4,0),(CONFIG+24,4,17),
        (CONFIG+28,4,1),(CONFIG+32,4,1),(CONFIG+36,4,1),(CONFIG+40,4,0),(CONFIG+44,4,0xffffffff)]
  w += [(0x775cac,4,phase),(0x775cb0,4,2),(0x77ece0,4,0),(0x7757e0,4,0),
        (0x787478,4,0),(0x7873b8,4,0),(0x77a50c,4,0),(0x774188,4,0xffffffff),
        (0x77ecdc,4,0),(0x77a568,4,6),(0x77ebfc,4,0),(0x774180,4,0),(0x7760d8,4,0xffffffff),
        (0x77a4f8,4,0),(0x77ec38,4,0xffffffff),(0x77ec44,4,1),(0x77a500,4,0),
        (0x77a510,4,0),(0x77a4e0,4,0),(0x774168,4,0),(0x77ec4c,4,0x12345678),
        (0x804a60,4,0xaabbcc02),(0x803a20,4,1),(0x776484,4,1),(0x5d2258,4,0),(0x5d225c,4,1),
        (0x607a10,4,0),(0x607a24,4,100),(0x607a38,4,0),(0x607a68,4,50),
        (0x607a90,4,0),(0x607a94,4,1),(0x608858,4,0),(0x608870,4,1),
        (0x806b68,4,0),(0x806b74,4,100),(0x806b80,4,0),(0x77a518,4,500),
        (0x776418,4,100),(0x7760c4,4,8),(0x7760c8,4,8),(0x77e598,4,0),
        (0x7755c4,4,0),(0x7755c8,4,0),(0x7755cc,4,7),(0x7755d0,4,7),
        (0x5d2698,4,0),(0x5d269c,4,0xffffffff),(0x805680,4,0),(0x8064f4,4,0),
        (0x8064f0,4,0),(0x805508,4,0)]
  for i in [0,1,60]:
   a=ACTOR+i*0x1ac
   w += [(a,4,i),(a+4,4,0x180 if i<16 else 0x400),(a+8,4,i*200),(a+12,4,i*300),
         (a+0x2c,4,0),(a+0x104,4,0),(a+0x110,4,0),(a+0x118,4,0),(a+0x128,4,3),(a+0x12c,4,3)]
  for i in range(30):w += [(0x77ec60+i*4,4,0)]
  if mode=='integrated':
   for i in range(64):
    for base in [0x7abca0,0x7cbca0,0x77a570,0x7764e0]:w += [(base+i*4,4,0)]
  return w
 def case(phase,w=(),mutation=0):o.case(0x44d45a,[],setup(phase,mutation)+list(w),0)
 if mode=='isolated':
  for major in [0,1,2,3,0xffffffff]:
   for phase in list(range(16))+[0xffffffff]:
    for force in [0,1]:
     for mutation in [0,1]:case(phase,[(0x775cb0,4,major),(0x77ece0,1,force)],mutation)
  for count in [0,1,10,0xffffffff,0x80000000]:case(0,[(0x775cb0,4,0),(0x77a4f8,4,count)])
  for pre in [0,1,2,3,0xffffffff]:
   for selected in [0,1,0x100,0x180]:
    for slot in [0,1]:
     for mutation in [0,1]:case(0,[(CONFIG+8,4,pre),(CONFIG+12,4,selected),(CONFIG+16,4,slot)],mutation)
  for status in [0,1,0x100,0x180]:
   for delta in [0,17,0x80000000,0xffffffff]:
    for mutation in [0,1]:case(1,[(CONFIG+20,4,status),(CONFIG+24,4,delta)],mutation)
  for p in [2,3,6,9]:
   for ready in [0,1,0x100,0xffffffff]:
    for mutation in [0,1]:case(p,[(CONFIG+28,4,ready),(0x7873b8,4,ready)],mutation)
  for p in [7,10]:
   for ready in [0,1,0x100]:
    for selected in [0,1,0xffffffff,0x80000000]:
     for mutation in [0,1]:case(p,[(CONFIG+32,4,ready),(0x774188,4,selected),(CONFIG+44,4,selected)],mutation)
  for flags in [0,0x400,0x100,0x180,0x2180]:
   for wait in [0,1,2]:
    for motion in [0,1]:
     for ticks in [0,5,6,19,20,21,0xffffffff,0x7fffffff]:
      for mutation in [0,1]:case(4,[(ACTOR+4,4,flags),(ACTOR+0x2c,4,wait),(ACTOR+0x104,4,motion),(0x77a568,4,ticks),(0x7760d8,4,0)],mutation)
  for command in [0,1,2,4,5,6,0xffff]:
   for selection in [0,1,0x133,0x134,0x137,0x139]:
    for mutation in [0,1]:
     w=[(0x77ecdc,4,1),(CONFIG+36,4,(selection<<16)|command)]
     if command==4:w += [(0x609168+selection*44,4,0)]
     case(4,w,mutation)
  for phase in [2,0xffffffff]:case(4,[(0x77ecdc,4,phase)])
  for marker in [0,29]:
   for hp in [0,443,444,445,0x80000000]:
    for block in [0,0x10000000]:
     case(4,[(ACTOR+4,4,0x400),(0x77ec60+marker*4,4,ACTOR+60*0x1ac),(0x77a518,4,hp),(0x806b68,4,block)])
  for p in [5,8]:
   for targets in [0,1,0xffffffff]:
    for slot in [0,15,16,60,0xffffffff]:
     for action in [0,1,0xffffffff]:
      for mutation in [0,1]:case(p,[(0x77a50c,4,targets),(0x7757e0,4,slot),(0x77ebfc,4,action),(0x7760d8,4,action)],mutation)
  for ready in [0,1,0x100]:
   for slot in [0,1,0xffffffff]:
    for party_id in [0,12,13,0xffffffff]:
     for trigger in [0,1]:case(12,[(0x77a500,4,ready),(0x77a510,4,slot),(0x5d2258,4,party_id),(0x77ec44,4,trigger)])
  for p in [13,14]:
   for tick in list(range(34))+[0x7fffffff,0x80000000,0xffffffff]:
    for mutation in [0,1]:case(p,[(0x77a4e0,4,tick)],mutation)
 else:
  # Connected original data/rule/control paths; no replacement of lower calls.
  for major in [0,1,3,0xffffffff]:
   for ticks in [0,1,10,0xffffffff]:case(0,[(0x775cb0,4,major),(0x77a4f8,4,ticks)])
  for phase in [0,1,2,5,6,7,9,10,12,15,0xffffffff]:
   for variant in [0,1]:case(phase,[(0x7873b8,1,variant),(0x5d2698,4,variant),(0x774188,4,0xffffffff)])
  for flags in [0x100,0x180,0x2180]:
   for motion in [0,1]:case(4,[(ACTOR+4,4,flags),(ACTOR+0x104,4,motion),(0x77a568,4,0)])
  for p in [13,14]:
   for tick in range(30):case(p,[(0x77a4e0,4,tick)])
 o.write(output,'battle_engine_'+mode,'Unmodified44d45a instructions; callback order/data compared in isolated mode, live lower functions in integrated mode. Explicit same-tick polls, signed gates, byte flags, marker damage, packed menu commands and mutation/reload ordering.')
 report_path=Path(output).with_suffix('.json');report=json.loads(report_path.read_text())
 report['original_instruction_coverage']={'total':len(instructions),'visited':len(visited&instructions),'unvisited':[hex(a) for a in sorted(instructions-visited)]}
 report_path.write_text(json.dumps(report,indent=2)+'\n')
 print('original_instruction_coverage',report['original_instruction_coverage'])
if __name__=='__main__':main(*sys.argv[1:])
