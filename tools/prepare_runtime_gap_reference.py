#!/usr/bin/env python3
"""Original x86 compact arbitration and newly recovered pure-helper fixtures."""
import hashlib,json,struct,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EFLAGS

def main(snapshot,output):
    raw=Path(snapshot).read_bytes();assert raw[:8]==b'FSBDRAW1'
    cursor=12;regions=[];pages=set()
    for _ in range(struct.unpack_from('<I',raw,8)[0]):
        base,size=struct.unpack_from('<II',raw,cursor);cursor+=8
        data=raw[cursor:cursor+size];cursor+=size;regions.append((base,data))
        pages.update(range(base&~4095,(base+size+4095)&~4095,4096))
    u=Uc(UC_ARCH_X86,UC_MODE_32)
    for page in sorted(pages):u.mem_map(page,4096)
    u.mem_map(0x1000000,65536)
    for base,data in regions:u.mem_write(base,data)
    data_base,old=next((b,d) for b,d in regions if b==0x4a5000)
    def word(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def invoke(entry,args):
        u.reg_write(UC_X86_REG_ESP,0x100f000);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
        word(0x100f000,0x1000000)
        for i,v in enumerate(args):word(0x100f004+i*4,v)
        u.emu_start(entry,0x1000000,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError(f'nonreturn {entry:x}')
        return u.reg_read(UC_X86_REG_EAX)
    # A valid empty arena is shared input. Run the original initializer rather
    # than inferring its free-list layout from native implementation code.
    u.mem_write(0x6d4b50,bytes(7*0x350));u.mem_write(0x6e1470,bytes(1024*424))
    for a in [0x6d9e60,0x6d9e64,0x6e12ac,0x6d9ea4,0x6e0e0c]:word(a,0)
    invoke(0x4070e7,[])
    initial=bytes(u.mem_read(data_base,len(old)))
    out=Path(output);out.parent.mkdir(parents=True,exist_ok=True)
    snapshot_out=out.with_name('runtime-gap-input.bin')
    state=bytearray(b'FSBDRAW1')+struct.pack('<I',len(regions))
    for b,d in regions:
        if b==data_base:d=initial
        state+=struct.pack('<II',b,len(d))+d
    snapshot_out.write_bytes(state)
    records=bytearray();counts={}
    # Callback contract only: this isolated object transitions to finished.
    # No game event, battle or quest is completed by this fixture hook.
    def finish(uc,address,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP);word(read(sp+4)+0x20,0)
        uc.reg_write(UC_X86_REG_EIP,read(sp));uc.reg_write(UC_X86_REG_ESP,sp+8)
    u.hook_add(UC_HOOK_CODE,finish,begin=0x100e000,end=0x100e000)
    def changes(before,after):
        result=[]
        for at in range(0,len(before),4096):
            if before[at:at+4096]==after[at:at+4096]:continue
            result.extend((data_base+i,after[i]) for i in range(at,min(at+4096,len(before))) if before[i]!=after[i])
        return result
    def case(entry,args,setup=(),writes=(),mask=0):
        nonlocal records
        u.mem_write(data_base,initial);u.mem_write(0x1000000,bytes(65536))
        for target,arguments in setup:invoke(target,arguments)
        for a,width,value in writes:u.mem_write(a,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
        before=bytes(u.mem_read(data_base,len(initial)));prepared=changes(initial,before)
        result=invoke(entry,args)&mask;after=bytes(u.mem_read(data_base,len(initial)));delta=changes(before,after)
        records+=struct.pack('<III',entry,mask,len(args))+b''.join(struct.pack('<I',a&0xffffffff) for a in args)
        records+=struct.pack('<I',len(prepared))+b''.join(struct.pack('<III',a,1,v) for a,v in prepared)
        records+=struct.pack('<II',result,len(delta))+b''.join(struct.pack('<IB',a,v) for a,v in delta)
        counts[hex(entry)]=counts.get(hex(entry),0)+1
    first=0x6e1618;second=first+424;third=second+424
    def alloc(group,flags):return (0x40277a,[0x6d4b50+group*0x350,0x100e000,flags,0x12345678])
    for flags in [0,2,0x10000,0x110000,0x110002,0x150000,0x190000,0x710000]:
        case(*alloc(0,flags),mask=0xffffffff)
    #402b67 walks physical links, even after the32-bit generation exceeds the
    #16 bits encodable in handles. It must not round-trip through402693.
    case(0x402b67,[0],setup=[alloc(0,0x190000)],writes=[(first+0x10,4,0x10002)],mask=0xffffffff)
    for immediate in [0,0x40000]:
        owner=0x190000|immediate
        for marker in [0,1,0xffffffff]:
            setup=[alloc(0,owner),alloc(1,0x190002),alloc(2,0x190002)]
            for pending in [0,third,0xffffffff]:
                writes=[(first+0x20,4,marker),(first+0xc,4,pending)]
                for entry,args,mask in [(0x402a6f,[first],0),(0x402a8f,[second],0),(0x402abb,[second],0xffffffff),(0x402abb,[0],0xffffffff),(0x402b67,[0],0xffffffff),(0x402b67,[first],0xffffffff)]:
                    case(entry,args,setup,writes,mask)
        for standby in [0,2]:
            case(*alloc(1,0x190000|standby),setup=[alloc(0,owner)],mask=0xffffffff)
    for successor in [0,second,0xffffffff]:
        for shutdown in [0,1]:
            for rearm in [0,0x10000]:
                for keep in [0,0x80000]:
                    setup=[alloc(0,0x100000|rearm|keep),alloc(1,0x190002)]
                    case(0x402832,[first],setup,[(first+0x20,4,1),(first+0xc,4,successor),(0x6e0e0c,4,shutdown)])
    for value in [0,1,0x100,0xf00,0x80000000,0x80f00000,0xffffffff]:
        case(0x411d15,[value],mask=0xffffffff);case(0x411d4b,[value],mask=0xffffffff)
    for flags in [0,4,8,0x14,0x28,0x204]:
        case(0x4490c7,[0,1,1,7,7,3,3,flags],writes=[(0x7760c4,4,9),(0x7760c8,4,9),(0x77e598,4,0)])
    sparkle=0x8073d8+700*428
    for phase in [-2,-1,0,10]:
        for height in [0,0x10000,0x20000,0x200000]:
            for same_map in [0,1]:
                case(0x455238,[sparkle],writes=[(sparkle,4,700),(sparkle+4,4,0x10800),(sparkle+0x10,4,height),(sparkle+0x148,4,0x455238),(sparkle+0x14c,4,phase),(sparkle+0x160,4,22+same_map),(sparkle+0x19c,4,3),(0x5d229c,4,22),(0x7ab560+12,4,sparkle)])
    palette=0x6d6288;source=palette+1024
    for delta in [-300,-1,0,1,300,0x7fffffff,-0x80000000]:
        writes=[(source+i*4,4,(i*0x01030507)&0xffffffff) for i in range(16)]
        case(0x404e4c,[palette,source,delta,16],writes=writes)
        case(0x404e4c,[source,source,delta,16],writes=writes)
    fixture=b'FSBACT1\0'+struct.pack('<I',sum(counts.values()))+records;out.write_bytes(fixture)
    report={'source_snapshot':snapshot,'source_snapshot_sha256':hashlib.sha256(raw).hexdigest(),'input_sha256':hashlib.sha256(state).hexdigest(),'fixture_sha256':hashlib.sha256(fixture).hexdigest(),'cases':sum(counts.values()),'entries':counts,'isolated_callback':'0x0100e000: set supplied compact lifecycle to0 and RET4; same test callback in C++','compared_region':{'base':hex(data_base),'bytes':len(initial)}}
    out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main(*sys.argv[1:])
