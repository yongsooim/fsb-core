#!/usr/bin/env python3
"""Differential fixtures for actual battle AI and action resolution, no stubs."""
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG,UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EIP,UC_X86_REG_EFLAGS

def main(snapshot,output,scope="battle"):
    raw=Path(snapshot).read_bytes();cursor=12;regions=[];pages=set()
    if raw[:8]!=b'FSBDRAW1':raise ValueError('bad snapshot')
    for _ in range(struct.unpack_from('<I',raw,8)[0]):
        base,size=struct.unpack_from('<II',raw,cursor);cursor+=8;data=raw[cursor:cursor+size];cursor+=size
        regions.append((base,data));pages.update(range(base&~4095,(base+size+4095)&~4095,4096))
    u=Uc(UC_ARCH_X86,UC_MODE_32)
    for page in sorted(pages):u.mem_map(page,4096)
    u.mem_map(0x1000000,65536)
    for base,data in regions:u.mem_write(base,data)
    data_base,initial=next((b,d) for b,d in regions if b==0x4a5000)
    def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def word(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    audio_requests=collections.Counter()
    post_requests=collections.Counter()
    if scope=="shutdown":
        def post_boundary(uc,address,size,user):
            sp=uc.reg_read(UC_X86_REG_ESP);post_requests[tuple(read(sp+4+i*4) for i in range(3))]+=1
            uc.reg_write(UC_X86_REG_EAX,1);uc.reg_write(UC_X86_REG_EIP,read(sp));uc.reg_write(UC_X86_REG_ESP,sp+16)
        u.hook_add(UC_HOOK_CODE,post_boundary,begin=0x40110f,end=0x40110f)
    if scope in ["cart","item_effect"]:
        def audio_boundary(uc,address,size,user):
            sp=uc.reg_read(UC_X86_REG_ESP);audio_requests[(address,read(sp+4))]+=1;uc.reg_write(UC_X86_REG_EAX,0);uc.reg_write(UC_X86_REG_EIP,read(sp));uc.reg_write(UC_X86_REG_ESP,sp+8)
        for entry in ([0x435373,0x4353cb] if scope=="cart" else [0x435373]):u.hook_add(UC_HOOK_CODE,audio_boundary,begin=entry,end=entry)
    if scope=="setup":
        def viewport_boundary(uc,address,size,user):
            sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EAX,0);uc.reg_write(UC_X86_REG_EIP,read(sp));uc.reg_write(UC_X86_REG_ESP,sp+8)
        u.hook_add(UC_HOOK_CODE,viewport_boundary,begin=0x4320f6,end=0x4320f6)
    counts=collections.Counter();records=bytearray()
    def case(entry,args,writes,mask=255):
        nonlocal records
        u.mem_write(data_base,initial);u.mem_write(0x1000000,bytes(65536))
        for a,width,v in writes:u.mem_write(a,(v&((1<<(width*8))-1)).to_bytes(width,'little'))
        before=bytes(u.mem_read(data_base,len(initial)))
        for reg in [UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]:u.reg_write(reg,0)
        u.reg_write(UC_X86_REG_EFLAGS,2);u.reg_write(UC_X86_REG_ESP,0x100f000);word(0x100f000,0x1000000)
        for i,arg in enumerate(args):word(0x100f004+i*4,arg)
        if scope in ["distance","angle"]:
            u.reg_write(UC_X86_REG_FPCW,0x27f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
        try:u.emu_start(entry,0x1000000,count=5000000)
        except Exception as error:raise RuntimeError(f'entry {entry:x}, case {counts[entry]}, pc {u.reg_read(UC_X86_REG_EIP):x}: {error}') from error
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError(f'nonreturn at {u.reg_read(UC_X86_REG_EIP):x}')
        after=bytes(u.mem_read(data_base,len(initial)));changes=[]
        for start in range(0,len(initial),4096):
            if before[start:start+4096]==after[start:start+4096]:continue
            for i in range(start,min(start+4096,len(initial))):
                if before[i]!=after[i]:changes.append((data_base+i,after[i]))
        records+=struct.pack('<III',entry,mask,len(args))
        for arg in args:records+=struct.pack('<I',arg&0xffffffff)
        records+=struct.pack('<I',len(writes))
        for a,width,v in writes:records+=struct.pack('<III',a,width,v&0xffffffff)
        records+=struct.pack('<II',u.reg_read(UC_X86_REG_EAX)&mask,len(changes))
        for a,v in changes:records+=struct.pack('<IB',a,v)
        counts[entry]+=1
    def basic(seed,context,phase,action):
        writes=[(0x6d1bf0,4,seed),(0x7757e0,4,context),(0x77ebfc,4,phase),(0x774180,4,action),(0x7760d8,4,action),(0x77fc10,4,0),(0x77fc08,4,0),(0x77fc0c,4,0),(0x77ec4c,1,0)]
        writes += [(0x77fc18+i,1,4) for i in range(484)]
        writes += [(0x7764e0+i*4,4,0x80) for i in range(read(0x7760c4)*read(0x7760c8))]
        for i in range(16):writes.append((0x776418+i*4,4,read(0x607a24+i*188)))
        for i in range(read(0x776484)):writes.append((0x77a518+i*4,4,read(0x806b74+i*36)))
        return writes
    if scope=="setup":
        for map_id in [416,436,467,468]:
            for accessory in [0xffffffff,297,298,324]:
                case(0x44ab67,[],[(0x5d229c,4,map_id),(0x77ec58,4,5),(0x608358,4,accessory),(0x60835c,4,0xffffffff)],255)
    elif scope=="shutdown":
        for requested in [0,1]:
            for posted in [0,1]:
                for ready in [0,1]:
                    for idle in [0,1,490,491]:
                        for latch in [0,1]:
                            for active in [0,17]:
                                writes=[(0x6da558,4,requested),(0x6d66a0,4,posted),(0x6e0e0c,4,ready),(0x6d6b2c,4,idle),(0x6e1448,4,latch),(0x6d9e60,4,active)]
                                case(0x407ffd,[],writes,0)
    elif scope=="party_marker":
        actor=0x8073d8+120*428
        for lane in [0,1]:
            for owner in [457,458]:
                for state in [-2,-1,0,1]:
                    for x,y,z,facing in [(0x88000,0x94000,0x8000,0),(0xfffe8000,0x138000,0xffff8000,3)]:
                        writes=[(actor,4,120),(actor+4,4,0x1004e),(actor+0x148,4,0x494869),(actor+0x14c,4,state),(actor+0x160,4,owner),(actor+0x14,4,x),(actor+0x18,4,y),(actor+0x1c,4,z),(actor+0x110,4,facing),(actor+0x128,4,11),(actor+0x12c,4,17),(0x5d229c,4,457),(0x803a3c,4,lane),(0x5d0a40,4,120)]
                        case(0x494869,[actor],writes,0)
    elif scope=="item_effect":
        effect=0x8073d8+700*428;caster=0x8073d8;target=0x8073d8+428
        for entry in [0x476674,0x476a83,0x476bba]:
            for state in [-300,-200,-100,-2,-1,0,10,20,30]:
                for busy in [0,1]:
                    for facing in [0,3]:
                        writes=[(0x8059f0,4,caster),(0x8059f8,4,target),(0x805680,4,effect),(effect,4,700),(effect+4,4,0x10800),(effect+0x148,4,entry),(effect+0x14c,4,state),(caster+4,4,0x101ce),(caster+0x110,4,facing),(target+4,4,0x101ce|(busy*0x20000)),(target+0x110,4,facing)]
                        case(entry,[effect],writes,0)
        for item in [308,311,312]:case(0x461854,[3,item],[],0xffffffff)
    elif scope=="party_switch":
        rosters=[[1,3,10,12,5],[4,6,8,9,11]]
        for lane in [0,1]:
            for selected in [0,2]:
                for map_id in [456,457,466,467,468]:
                    for extra in [-1,120]:
                        for facing in [0,3]:
                            writes=[(0x803a3c,4,lane),(0x803a20,4,5),(0x803a1c,4,selected),(0x5d229c,4,map_id),(0x5d0a40,4,extra)]
                            for slot in range(10):
                                a=0x8073d8+slot*428
                                writes += [(a,4,slot),(a+4,4,0x101ce if slot<5 else 0),(a+0x148,4,0),(a+0x14c,4,0),(a+0x11c,4,0),(a+0x128,4,4+slot),(a+0x12c,4,12+slot),(a+0x1c,4,98304),(a+0x110,4,facing)]
                            for other in [0,1]:
                                writes += [(0x803a30+other*4,4,5),(0x803a08+other*4,4,selected),(0x8039d8+other*16,4,7+other),(0x8039dc+other*16,4,18+other),(0x8039e0+other*16,4,0),(0x8039e4+other*16,4,3-facing)]
                                writes += [(0x803960+other*40+i*4,4,id) for i,id in enumerate(rosters[other])]
                            writes += [(0x5d2258+i*4,4,id) for i,id in enumerate(rosters[lane])]
                            if extra>=0:writes += [(0x8073d8+extra*428,4,extra),(0x8073d8+extra*428+4,4,0)]
                            case(0x45f07e,[1-lane],writes,0)
    elif scope=="falling_floor":
        overlay=0x800dc8
        for actor_index in [0,4,9]:
            actor=0x8073d8+actor_index*428
            for patch in range(0x111,0x11d):
                for state in list(range(9))+[0xffffffff]:
                    writes=[(0x5d229c,4,424),(0x7760c4,4,27),(0x7760c8,4,36),(0x803a1c,4,actor_index),(0x776478,4,7),(overlay,4,0x300020f),(overlay+0x18,4,state),(overlay+0x20,4,patch),(actor+4,4,0x101ce),(actor+0x104,4,4),(actor+0x18,4,0x188000 if actor_index==0 else 0xfffffff0 if actor_index==4 else 0x7ffffff0)]
                    case(0x493a26,[overlay],writes,0)
    elif scope=="sleep_icon":
        effect=0x8073d8+700*428;anchor=0x8073d8+2*428
        for state in [-1,0,1]:
            for tick in [0,9,10,11,29,30,31]:
                for seed in [1,30]:
                    writes=[(effect+4,4,0x10800),(effect+0x14c,4,state),(effect+0x144,4,tick),(effect+0x198,4,10 if tick<20 else 30),(effect+0x160,4,anchor),(0x6d1bf0,4,seed)]
                    case(0x46274d,[effect],writes,0)
        for duplicate in [0,1]:
            writes=[(0x805880+i*4,4,0) for i in range(32)]+[(0x805a78+i*4,4,0) for i in range(320)]
            writes += [(0x805588,4,duplicate),(0x805880,4,anchor if duplicate else 0),(0x805a7c,4,effect if duplicate else 0)]
            case(0x4622c2,[anchor,2],writes)
    elif scope=="ownership":
        item=63
        base=[(0x607a08+i*188+0x74+j*4,4,0xffffffff) for i in range(16) for j in range(5)]
        for character in range(16):
            for slot in range(5):
                case(0x448a0a,[item],base+[(0x806e30+item*4,4,0),(0x607a08+character*188+0x74+slot*4,4,item)])
        for quantity in [0,1,0x7fffffff,0x80000000,0xffffffff]:
            case(0x448a0a,[item],base+[(0x806e30+item*4,4,quantity)])
    elif scope=="world_route":
        for entry in [0x4374c7,0x437993,0x437f38,0x438493]:
            #Phase1 initializes audio and redraws the label atlas. Its real
            #integration is covered by campaign112, not a draw-less oracle.
            for phase in [0,10,20,30,40,45,50,60,70,80,90]:
                for hold,busy,map_state in [(0,0,6),(19,2,2),(35,0,6)]:
                    writes=[(0x7714c4,4,0x6df400),(0x6df020,4,phase),(0x6df0e8,4,hold),(0x7714d0,4,busy),(0x7714a4,4,map_state)]
                    case(entry,[0x6df000],writes,0)
    elif scope=="gatewarp":
        effect=0x8073d8+90*428
        for entry in [0x4580f7,0x45826c,0x4583cd,0x458691,0x4588ee]:
            buckets=35 if entry in [0x4580f7,0x45826c,0x4583cd] else 77
            groups=[0,1] if buckets==35 else [0]
            for group in groups:
                for bucket in range(-1,buckets):
                    writes=[(0x803a1c,4,2),(0x5d0a40,4,4),(0x5d0768,4,max(bucket,0)*3+2),(effect+0x14c,4,-1 if bucket<0 else 0),(effect+0x160,4,group)]
                    for i in [2,4]:
                        actor=0x8073d8+i*428
                        writes += [(actor+offset,4,value) for offset,value in [(8,123456+i*65536),(12,987654+i*65536),(16,0x230000),(20,(i+1)*65536+32768),(24,(i+3)*65536+32768),(28,98304),(0x128,i+1),(0x12c,i+3)]]
                    case(entry,[effect],writes,0)
    elif scope=="cart":
        actor=0x8073d8
        for state in [5,8,9,10,11]:
            for facing in range(4):
                for frame in [1,7,14,15]:
                    for direction in range(5):
                        writes=[(actor+0x104,4,state),(actor+0x108,4,frame),(actor+0x10c,4,16),(actor+0x110,4,facing),(actor+0x114,4,(facing+2)%4),(actor+0x3c,2,0)]
                        writes += [(address,4,int(direction==i)) for i,address in enumerate([0x6da888,0x6da8a8,0x6da894,0x6da89c])]
                        writes += [(address,4,0) for address in [0x6da688,0x6da6a8,0x6da694,0x6da69c]]
                        case(0x45abf9,[actor],writes,0)
    elif scope=="actor_helpers":
        source,destination=0x8073d8,0x8073d8+428
        for x,y in [(0,0),(65536,98304),(-1,-49),(-65537,2147483647),(2147483647,-2147483648)]:
            writes=[(source+8,4,x),(source+12,4,y)]
            case(0x43029b,[source],writes,0)
        for seed in range(12):
            writes=[(source+offset,4,(seed*65537+offset*1009)^0x87654321) for offset in range(4,428,4)]
            writes += [(destination+offset,4,0x13579bdf+offset) for offset in range(4,428,4)]
            for actor_id,actor in [(3,source),(8,destination)]:
                writes += [(0x5b3560+actor_id*68,4,actor_id),(0x5b35a0+actor_id*68,4,actor)]
            case(0x4302d2,[source],writes,0xffffffff)
            case(0x4302d2,[3],writes,0xffffffff)
            case(0x430385,[destination,source],writes,0)
            case(0x430385,[8,3],writes,0)
            case(0x430385,[3,3],writes,0)
            for visible in [0,1]:case(0x42ff9b,[destination,visible],writes,0)
    elif scope=="variant3":
        actor=0x8073d8
        for mode in [3,9]:
            for state in [0,1,2,3,4,5,7,8,12,13,14,15,16]:
                for phase in range(20):
                    for facing in [0,3]:
                        writes=[(actor,4,0),(actor+4,4,0x10170),(actor+0x14,4,0x38000),(actor+0x18,4,0x68000),(actor+0x1c,4,0x8000),(actor+0x2c,4,1),(actor+0x104,4,state),(actor+0x108,4,1),(actor+0x10c,4,1),(actor+0x110,4,facing),(actor+0x130,4,8),(0x80465c,4,mode),(0x6da2d4,4,phase),(0x5d2258,4,3),(0x607a08+3*188+8,4,0),(0x607a08+3*188+24,4,100),(0x607a08+3*188+28,4,100)]
                        case(0x45cc1b,[actor],writes,0)
    elif scope=="angle":
        actor=0x8073d8
        coordinates=[-2147483648,-2147483647,-65537,-65536,-65535,-2,-1,0,1,2,65535,65536,65537,2147483646,2147483647]
        for entry in [0x463038,0x4630ed]:
            for x in coordinates:
                for y in coordinates:
                    writes=[(actor+8,4,0),(actor+12,4,0),(actor+0x184,4,x),(actor+0x188,4,y)]
                    case(entry,[actor],writes,0xffffffff)
    elif scope=="distance":
        actor=0x8073d8
        for mode in range(8):
            for x,y,z in [(0,0,0),(3,4,12),(-3,4,-12),(65536,98304,131072),(0x7fffffff,0x80000000,0xffffffff),(1234567,-2345678,3456789)]:
                writes=[(actor+8,4,x),(actor+12,4,y),(actor+16,4,z),(actor+0x184,4,1),(actor+0x188,4,2),(actor+0x18c,4,3),(actor+0x168,4,mode<<4)]
                case(0x462f5b,[actor],writes,0xffffffff)
    elif scope=="effect_callback":
        actor=0x8073d8+80*0x1ac
        for state in [0,1,10,0xffffffff]:
            for flags in [0,0x40,0x20000,0x1024e]:
                for link in [0,1,0xffffffff]:
                    writes=[(actor,4,80),(actor+4,4,flags),(actor+0x148,4,0x46dfc3),(actor+0x14c,4,state),(actor+0x11c,4,link)]
                    case(0x46dfc3,[actor],writes,0)
    elif scope=="poses":
        actor=0x808490
        for entry in [0x44828a,0x4483b5]:
            for facing in range(8):
                for tick in [0,1,5,11,31,127]:
                    for state in [0,0xfffffffe]:
                        writes=[(actor+4,4,0x1024e),(actor+0x14,4,0x58000),(actor+0x18,4,0x88000),(actor+0x1c,4,0x8000),(actor+0x110,4,facing),(actor+0x14c,4,state),(actor+0x144,4,tick),(actor+0x108,4,tick),(actor+0x2c,4,tick),(actor+0x30,4,tick)]
                        case(entry,[actor],writes,0)
    elif scope=="battle":
        for seed in range(1,33):
            for context,phase,action in [(0,1,8),(1,1,48),(0,2,10),(0,3,299),(0,3,302),(60,0,180),(62,0,177),(63,0,234),(63,0,235),(63,0,236)]:
                u.mem_write(data_base,initial)
                writes=basic(seed,context,phase,action)
                statuses=[0,0x100,0x200,0x800,0x4000,0x8000,0x80000,0x1000]
                if seed>8:
                    for i in [3,8]:writes.append((0x607a10+i*188,4,statuses[(seed+i)%len(statuses)]))
                    for i in range(5):writes.append((0x806b68+i*36,4,read(0x806b68+i*36)|statuses[(seed+i)%len(statuses)]))
                case(0x44e7e5,[],writes)
            for context in [60,62,63]:
                case(0x453b8d,[0x8073d8+context*428],[(0x6d1bf0,4,seed),(0x7757e0,4,context)],0)
        # Every additional enemy family in the original Ohaengsan battlesets,
        # including bite/poison/flame variants absent from the scripted encounter.
        for seed in range(1,17):
            for enemy,actions in [(126,[171]),(127,[171,172]),(129,[174]),(145,[190,191])]:
                u.mem_write(data_base,initial)
                definition=[(0x806b64,4,enemy)]
                case(0x453b8d,[0x8073d8+60*428],definition+[(0x6d1bf0,4,seed),(0x7757e0,4,60)],0)
                for action in actions:
                    u.mem_write(data_base,initial)
                    case(0x44e7e5,[],definition+basic(seed,60,0,action))
    else:raise ValueError("unknown battle reference scope")
    result=b'FSBACT1\0'+struct.pack('<I',sum(counts.values()))+records;path=Path(output);path.write_bytes(result)
    report={'input_snapshot_sha256':hashlib.sha256(raw).hexdigest(),'cases':sum(counts.values()),'entry_counts':{hex(k):v for k,v in counts.items()},'scope':scope,'substituted_calls':[],
            'comparison':'return value and complete guest .data, using sparse expected changes against each pre-call state','fixture_sha256':hashlib.sha256(result).hexdigest()}
    if scope=="variant3":report['stack_policy']='zero-filled emulated stack; original45cc1b only initializes entries0..15 except state7, which also initializes16..27; undefined entries16..19 are a documented portable visual policy, not established original values'
    if scope=="cart":report['substituted_calls']=['435373 and4353cb audio service boundaries return0; no DirectSound execution or sound-state comparison']
    if scope=="item_effect":
        report['substituted_calls']=['435373 audio cue boundary returns0; DirectSound/audio state is excluded from this effect-state comparison.']
        report['audio_requests']=[{'entry':hex(entry),'cue':hex(cue),'count':count} for (entry,cue),count in sorted(audio_requests.items())]
    if scope=="shutdown":
        report['substituted_calls']=['40110f PostMessage boundary records the three original arguments and returns1; no Windows message queue is executed.']
        report['post_requests']=[{'arguments':list(args),'count':count} for args,count in sorted(post_requests.items())]
    if scope=="setup":
        report['substituted_calls']=['4320f6 viewport transition boundary returns0; game data helpers are original']
        report['limits']='controlled map-id/accessory variants on the recorded Hector85 setup geometry; not full gameplay on maps436/467/468'
    if scope=="world_route":report['limits']='phase1 audio/label-atlas initialization excluded; covered by actual campaign112 replay instead. All tested phases execute original code without substituted calls.'
    path.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main(*sys.argv[1:])
