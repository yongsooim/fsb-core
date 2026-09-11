#!/usr/bin/env python3
"""Original413da7 literal/locale arms, outputs and caller-stack pointer domains."""
from pathlib import Path
import sys,struct,json
sys.path.insert(0,str(Path(__file__).parent/'parallel/B_COMBAT'))
from prepare_combat_reference import Oracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
T,C,OUT,PUBLISH=0x6dc000,0x6ddb00,0x6ddd00,0x6dde00

def main(snapshot,rows_file,output,mode='isolated'):
 rows=json.loads(Path(rows_file).read_text())['rows'];o=Oracle(snapshot)
 def put(a,v):o.u.mem_write(a,struct.pack('<I',v&0xffffffff))
 def service(u,entry,size,user):
  sp=u.reg_read(UC_X86_REG_ESP);n=0 if entry==0x457e56 else 3;args=[o.read(sp+4+i*4) for i in range(n)]
  count=o.read(T);values=[entry,n]+args+[0]*(3-n)+[o.read(0x7686bc),o.read(OUT),o.read(OUT+4),o.read(OUT+8),o.read(OUT+12)]
  for i,v in enumerate(values):put(T+4+count*48+i*4,v)
  put(T,count+1);o.requests[(entry,tuple(args))]+=1
  if o.read(C):put(0x7686bc,o.read(0x7686bc)^0x80000000);put(OUT+12,99)
  result=2 if entry==0x457e56 else o.read(C+4)
  u.reg_write(UC_X86_REG_EAX,result);u.reg_write(UC_X86_REG_EIP,o.read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+n*4)
 def publish(u,entry,size,user):
  sp=u.reg_read(UC_X86_REG_ESP)
  for i,arg in enumerate([2,3,4,5]):
   pointer=o.read(sp+4+arg*4);put(PUBLISH+i*4,o.read(pointer) if pointer else 0xfeedface)
 o.u.hook_add(UC_HOOK_CODE,service,begin=0x4139b6,end=0x4139b6);o.substituted['0x4139b6']='dialog creation boundary: record request and configured result/mutation'
 if mode=='isolated':o.u.hook_add(UC_HOOK_CODE,service,begin=0x457e56,end=0x457e56);o.substituted['0x457e56']='prefix observer with configured language mutation'
 o.u.hook_add(UC_HOOK_CODE,publish,begin=0x418f9d,end=0x418f9d)
 def case(ident,language,success,mutation,layout):
  base=0x1008100 if layout==3 else OUT
  outputs=[base,base+4,base+8]
  if layout==1:outputs=[0,0,0]
  if layout==2:outputs=[base,base,base]
  w=[(T+i*4,4,0) for i in range(96)]+[(C,4,mutation),(C+4,4,0x12345678 if success else 0),
      (0x7686bc,4,language),(0x803a1c,4,0),(0x5d2258,4,2),(OUT,4,11),(OUT+4,4,22),(OUT+8,4,33),(OUT+12,4,44)]
  w += [(PUBLISH+i*4,4,0) for i in range(4)]
  if layout==3:w += [(base+i*4,4,11*(i+1)) for i in range(4)]
  o.case(0x413da7,[ident,0x10203,*outputs,base+12],w,0xffffffff)
 for row in rows:
  for language in [0xffffffff,0]:
   for success in [0,1]:
    for mutation in [0,1]:case(row['id'],language,success,mutation,row['id']%4)
 for ident in [rows[0]['id'],rows[-1]['id']]:
  for layout in range(4):
   for language in [0x80000000,0x7fffffff]:case(ident,language,1,0,layout)
 if mode=='integrated':
  for ident in [0x7777,0x80000000,0xfffffff0]:case(ident,0,1,0,0)
 o.write(output,'dialog_templates_'+mode,'Original413da7 runs unchanged. All selected IDs and both language signs, creation success/failure, callback language mutation, nullable/aliased/data/guest-stack outputs. Original RET publishes stack outputs into compared data. Integrated retains real player-prefix function and tests explicit legacy default IDs; dialog factory is substituted in both modes.')
if __name__=='__main__':main(*sys.argv[1:])
