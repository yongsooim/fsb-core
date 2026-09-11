#!/usr/bin/env python3
"""Rebuild original phase-E draw queue from a real portable runtime snapshot."""
from pathlib import Path
import json,struct,hashlib,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
from prepare_event0 import PE
from verify_draw_pass_x86 import ranges

def main(executable,directory):
    directory=Path(directory);pe=PE(executable);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
    for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
    pages=set();snapshot=dict(ranges(directory/'state.bin'))
    for base,data in snapshot.items():
        if not 0x400000<=base<0xa00000:
            for p in range(base&~4095,(base+len(data)+4095)&~4095,4096):
                if p not in pages:u.mem_map(p,4096);pages.add(p)
        u.mem_write(base,data)
    def rd(p):return struct.unpack('<I',u.mem_read(p,4))[0]
    def wr(p,v):u.mem_write(p,struct.pack('<I',v&0xffffffff))
    def call(entry,*args):
        u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<'+'I'*(len(args)+1),0x1000000,*args));u.emu_start(entry,0x1000000,count=20000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError(f'{entry:x} did not return')
    if rd(0x80465c)!=3:raise ValueError('this oracle expects a field snapshot')
    substituted=[]
    def hook(uc,at,size,_):
        if at==0x404bb0:
            sp=uc.reg_read(UC_X86_REG_ESP);substituted.append(hex(at));uc.reg_write(UC_X86_REG_EIP,rd(sp));uc.reg_write(UC_X86_REG_ESP,sp+16);uc.reg_write(UC_X86_REG_EAX,0)
    u.hook_add(UC_HOOK_CODE,hook)
    begin,end=0x787480,0x7ab2d8
    before_data=bytes(u.mem_read(0x4a5000,3882100));before=bytes(u.mem_read(begin,end-begin));before_count=rd(begin);old_queue=[rd(0x787498+i*4) for i in range(before_count)]
    call(0x454652)
    for i in range((0x85712d-0x8073d8-4+427)//428):
        if rd(0x8073dc+i*428)&64:call(0x454cb5,i)
    for layer in range(rd(0x800dc0)):
        p=layer*2+1
        if layer<rd(0x800dbc):call(0x45555b,layer,0)
        wr(0x800da8+layer*4,rd(0x787488+layer*8))
        call(0x454b24,p);call(0x4577d6,layer,0)
        first=rd(begin);call(0x454c8f,p);call(0x45466b,p,first)
    actual=bytes(u.mem_read(begin,end-begin));(directory/'original-queue.bin').write_bytes(actual)
    actual_count=rd(begin);new_queue=[rd(0x787498+i*4) for i in range(actual_count)]
    diff=[i for i,(a,b) in enumerate(zip(before,actual)) if a!=b]
    data_after=bytes(u.mem_read(0x4a5000,3882100));data_diff=[i for i,(a,b) in enumerate(zip(before_data,data_after)) if a!=b]
    report={'whole_data_differences':len(data_diff),'whole_data_first_differences':[hex(0x4a5000+i) for i in data_diff[:16]],'exe_sha256':hashlib.sha256(pe.data).hexdigest(),'scope':'original phase-E command builders/flush/sort in original order, preloaded surfaces; final blitter not executed','substituted_platform_calls':sorted(set(substituted)),'cpp_queue_count':before_count,'original_queue_count':actual_count,'same_queue_order':old_queue==new_queue,'record_region_byte_differences':len(diff),'first_differences':[{'address':hex(begin+i),'cpp':before[i],'original':actual[i]} for i in diff[:32]]}
    (directory/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));return 0 if old_queue==new_queue and not diff else 1
if __name__=='__main__':raise SystemExit(main(*sys.argv[1:]))
