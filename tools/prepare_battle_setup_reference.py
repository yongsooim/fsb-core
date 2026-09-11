#!/usr/bin/env python3
"""Run actual44ab67 on a recorded pre-setup guest snapshot (phase5 data path)."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP

def main(snapshot,output):
    data=Path(snapshot).read_bytes()
    if data[:8]!=b'FSBDRAW1':raise ValueError('wrong guest snapshot format')
    count=struct.unpack_from('<I',data,8)[0];cursor=12;regions=[];pages=set()
    for _ in range(count):
        base,size=struct.unpack_from('<II',data,cursor);cursor+=8;payload=data[cursor:cursor+size];cursor+=size
        if len(payload)!=size:raise ValueError('truncated snapshot')
        regions.append((base,payload));pages.update(range(base&~4095,(base+size+4095)&~4095,4096))
    u=Uc(UC_ARCH_X86,UC_MODE_32)
    for page in sorted(pages):u.mem_map(page,4096)
    u.mem_map(0x1000000,0x10000)
    for base,payload in regions:u.mem_write(base,payload)
    def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    viewport=[];calls=[]
    def hook(machine,a,size,user):
        if a==0x4320f6:
            stack=machine.reg_read(UC_X86_REG_ESP);viewport.append(read(stack+4))
            machine.reg_write(UC_X86_REG_EIP,read(stack));machine.reg_write(UC_X86_REG_ESP,stack+8);machine.reg_write(UC_X86_REG_EAX,0)
        if a in [0x4490c7,0x449fc2,0x448c6e,0x45df01,0x44a7a3,0x45d91d,0x44dd1c]:calls.append(hex(a))
    u.hook_add(UC_HOOK_CODE,hook)
    u.mem_write(0x100f000,struct.pack('<I',0x1000000));u.reg_write(UC_X86_REG_ESP,0x100f000)
    u.emu_start(0x44ab67,0x1000000,count=20000000)
    if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('setup did not return')
    watched=[(base,payload) for base,payload in regions if base==0x4a5000]
    if len(watched)!=1:raise ValueError('snapshot lacks expected guest data region')
    result=bytearray(b'FSBSET1\0'+struct.pack('<I',len(watched)))
    changed=0
    for base,payload in watched:
        after=bytes(u.mem_read(base,len(payload)));changed+=sum(a!=b for a,b in zip(payload,after))
        result+=struct.pack('<II',base,len(after))+after
    path=Path(output);path.write_bytes(result)
    report={'input_snapshot_sha256':hashlib.sha256(data).hexdigest(),'routine':'actual44ab67 phase5 and internal data callees','original_return':u.reg_read(UC_X86_REG_EAX),
            'isolated_component':'4320f6 viewport transition omitted here; the C++ controller invokes its independently ported viewport service before setup data',
            'viewport_requests':viewport,'selected_calls':calls,'guest_data_bytes_compared':sum(len(p) for _,p in watched),'changed_bytes':changed,'fixture_sha256':hashlib.sha256(result).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='selected_calls'}))

if __name__=='__main__':main(*sys.argv[1:])
