#!/usr/bin/env python3
"""Original effect interpreter with explicit sound/isolated-callback boundaries."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
from prepare_event0 import PE

def main(executable,output):
    p=PE(executable);sha=hashlib.sha256(p.data).hexdigest()
    assert sha=='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,65536)
    for _,va,size,offset in p.sections:u.mem_write(p.base+va,p.data[offset:offset+size])
    base=0x4a5000;initial=bytes(u.mem_read(base,3882100));actor=0x8073d8+700*428;controller=actor+428;alternate=controller+428;pc=0x6df000
    records=bytearray();counts={};observations=[];callback_mode=0
    def read(at):return struct.unpack('<I',u.mem_read(at,4))[0]
    def write(at,value):u.mem_write(at,struct.pack('<I',value&0xffffffff))
    def boundary(uc,at,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);value=read(sp+4);kind={0x435373:1,0x4353cb:2,0x100e000:3}[at];observations.append((kind,value))
        if kind==3:
            if callback_mode==2:write(value+0x14c,read(value+0x14c)+1)
            if callback_mode==3:write(0x805680,alternate)
        uc.reg_write(UC_X86_REG_EAX,0);uc.reg_write(UC_X86_REG_EIP,read(sp));uc.reg_write(UC_X86_REG_ESP,sp+8)
    for at in [0x435373,0x4353cb,0x100e000]:u.hook_add(UC_HOOK_CODE,boundary,begin=at,end=at)
    def case(entry,args,writes=(),mask=0,mode=0):
        nonlocal records,callback_mode
        callback_mode=mode;observations.clear();u.mem_write(base,initial)
        for a,width,value in writes:u.mem_write(a,(value&((1<<(width*8))-1)).to_bytes(width,'little'))
        before=bytes(u.mem_read(base,len(initial)));u.reg_write(UC_X86_REG_ESP,0x100f000);u.reg_write(UC_X86_REG_EAX,0)
        u.mem_write(0x100f000,struct.pack('<'+'I'*(1+len(args)),0x1000000,*args));u.emu_start(entry,0x1000000,count=1000000)
        assert u.reg_read(UC_X86_REG_EIP)==0x1000000
        after=bytes(u.mem_read(base,len(initial)));changes=[]
        for block in range(0,len(initial),4096):
            if before[block:block+4096]!=after[block:block+4096]:changes.extend((base+i,after[i]) for i in range(block,min(block+4096,len(initial))) if before[i]!=after[i])
        records+=struct.pack('<4I',entry,mask,mode,len(args))+b''.join(struct.pack('<I',a) for a in args)
        records+=struct.pack('<I',len(writes))+b''.join(struct.pack('<III',a,w,v&0xffffffff) for a,w,v in writes)
        records+=struct.pack('<II',u.reg_read(UC_X86_REG_EAX)&mask,len(changes))+b''.join(struct.pack('<IB',a,v) for a,v in changes)
        records+=struct.pack('<I',len(observations))+b''.join(struct.pack('<II',*v) for v in observations)
        counts[hex(entry)]=counts.get(hex(entry),0)+1
    lengths=[2,2,2,2,10,10,10,8,4,2,2,10,10,2,4,6,6,4,4,4]
    for seed in [0,1,127,255,32768,65535]:
        setup=[(actor+i,4,(seed*0x1010101+i*65537)&0xffffffff) for i in range(0,428,4)]
        setup += [(actor+0x150,4,pc),(actor+0x158,4,0),(0x773f70,4,0),(0x805680,4,controller),(controller+0x148,4,0),(controller+0x14c,4,99)]
        for op,length in enumerate(lengths):
            entry=p.u32(p.offset(0x5be798+op*4-p.base))
            packet=bytes([op,length])+struct.pack('<4H',seed,seed^0xffff,seed,seed&3)
            case(entry,[actor],setup+[(pc+i,1,v) for i,v in enumerate(packet)])
        case(0x447898,[actor],setup,0xffffffff)
    for frames in [0,1,3]:
        # Initial execution, zero waits, subsequent delays and stop.
        packet=bytes([2,2,11,10])+struct.pack('<4H',1,0xffff,32768,0)+bytes([8,4])+struct.pack('<H',frames)+bytes([0,2])
        writes=[(pc+i,1,v) for i,v in enumerate(packet)]+[(actor+4,4,0x80000000)]
        case(0x447841,[actor,pc],writes,0xffffffff)
        for wait in [0,1,3]:
            setup=writes+[(actor+4,4,0x20000),(actor+0x150,4,pc),(actor+0x154,4,wait),(actor+0x158,4,12),
                          (controller+4,4,0x20000),(controller+0x150,4,pc),(controller+0x154,4,1),(controller+0x158,4,12)]
            case(0x4478ba,[],setup)
    for mode in [0,1,2,3]:
        for state in [0,1,0xffffffff,0xffff8000]:
            setup=[(0x805680,4,controller),(controller+0x148,4,0x100e000 if mode else 0),(controller+0x14c,4,77),
                   (alternate+0x14c,4,state),(alternate+0x1a8,4,123)]
            case(0x461d25,[actor,state],setup,0xffffffff,mode)
            packet=bytes([14,4])+struct.pack('<H',state&0xffff)
            case(p.u32(p.offset(0x5be798+14*4-p.base)),[actor],setup+[(actor+0x150,4,pc),(actor+0x158,4,0)]+[(pc+i,1,v) for i,v in enumerate(packet)],mode=mode)
    data=b'FSBEFX1\0'+struct.pack('<I',sum(counts.values()))+records;out=Path(output);out.write_bytes(data)
    out.with_suffix('.json').write_text(json.dumps({'source_sha256':sha,'cases':sum(counts.values()),'entries':counts,'fixture_sha256':hashlib.sha256(data).hexdigest(),
        'compared_data_bytes':len(initial),'substituted_calls':{'0x435373':'record play cue; RET4','0x4353cb':'record stop cue; RET4','0x0100e000':'isolated test controller; optionally change state/controller; RET4'},
        'scope':'Interpreter state and service request order; original sound service internals are covered separately.'},indent=2)+'\n')
    print('effect script cases',sum(counts.values()))
if __name__=='__main__':main(*sys.argv[1:])
