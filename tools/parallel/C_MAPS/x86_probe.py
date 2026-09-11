#!/usr/bin/env python3
"""Record original C_MAPS entry-point behaviour straight out of FLYINGSB.EXE.

A probe runs the original instructions in Unicorn. Nothing is produced by the
C++ reconstruction. Two original routines are replaced by recording stubs and
the substitution is written into the fixture metadata, so a group that uses one
is a contract on the entry under test, not a whole-program comparison:

  0x435373  declared audio cue service (tools/original_service_boundaries.json)
  0x412181  field position event trigger router, owned by the integrator session
  0x413787  map position event trigger, owned by the integrator session

Fixture layout, little endian:
  'FSBCMAPO' u32 group_count
  group: u32 router_return u32 seed_count (u32 address,u32 width,u32 value)*
         u32 call_count
         call: u32 entry,u32 arg_count,u32 eax (u32 arg)*
               u32 change_count (u32 address,u8 value)*
               u32 service_count (u32 entry,u32 arg_count,(u32 argument)*)*
"""
import hashlib,json,struct
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
from prepare_event0 import PE

SHA='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
REGION,REGION_SIZE=0x4a5000,3882100
STACK,RETURN=0x100f000,0x1000000
MAGIC=b'FSBCMAPO'
SOUND_CUE,POSITION_ROUTER=0x435373,0x412181
DESPAWN,POSITION_EVENT=0x45d91d,0x413787   # owned by A_ACTORS and the integrator
# 0x45d91d is not stubbed: it is plain teardown, so both sides run the original.
STUBS={SOUND_CUE:(b'\xc2\x04\x00',1),POSITION_EVENT:(b'\xc2\x08\x00',2),POSITION_ROUTER:(b'\xc3',0)}

class Probe:
    def __init__(self,executable):
        self.pe=PE(executable);self.sha=hashlib.sha256(self.pe.data).hexdigest()
        if self.sha!=SHA:raise ValueError('wrong executable revision')
        self.u=Uc(UC_ARCH_X86,UC_MODE_32)
        self.u.mem_map(0x400000,0x600000);self.u.mem_map(RETURN,0x10000)
        for _,va,size,offset in self.pe.sections:self.u.mem_write(self.pe.base+va,self.pe.data[offset:offset+size])
        self.pristine=bytes(self.u.mem_read(REGION,REGION_SIZE))
        self.services=[]
        for address in STUBS:self.u.hook_add(UC_HOOK_CODE,self._record,begin=address,end=address)
        # Both stubs are a bare return written once. The value the router hands
        # back comes from the hook, because rewriting an immediate would leave
        # the emulator running its cached translation of the previous group.
        for address,(code,_) in STUBS.items():self.u.mem_write(address,code)
        self.router=0
        self.groups=[];self.blob=bytearray();self.calls=0
    def set_router(self,value):self.router=value&0xffffffff
    def _record(self,u,address,size,user):
        esp=u.reg_read(UC_X86_REG_ESP)
        arguments=[struct.unpack('<I',bytes(u.mem_read(esp+4+i*4,4)))[0] for i in range(STUBS[address][1])]
        if address==POSITION_ROUTER:u.reg_write(UC_X86_REG_EAX,self.router)
        self.services.append((address,arguments))
    def word(self,address):return self.pe.u32(self.pe.offset(address-self.pe.base))
    def group(self,name,seeds,calls,router=0,note=None):
        self.u.mem_write(REGION,self.pristine);self.set_router(router)
        for address,width,value in seeds:self.u.mem_write(address,(value&0xffffffff).to_bytes(width,'little'))
        self.blob+=struct.pack('<II',router&0xffffffff,len(seeds))
        for address,width,value in seeds:self.blob+=struct.pack('<III',address,width,value&0xffffffff)
        self.blob+=struct.pack('<I',len(calls))
        for entry,args in calls:
            before=bytes(self.u.mem_read(REGION,REGION_SIZE));self.services=[]
            self.u.reg_write(UC_X86_REG_ESP,STACK-4*len(args))
            self.u.mem_write(STACK-4*len(args),struct.pack('<I',RETURN)+b''.join(struct.pack('<I',a&0xffffffff) for a in args))
            self.u.emu_start(entry,RETURN,count=20000000)
            if self.u.reg_read(UC_X86_REG_EIP)!=RETURN:raise ValueError('original call did not return')
            after=bytes(self.u.mem_read(REGION,REGION_SIZE))
            changes=[(i,after[i]) for i in range(REGION_SIZE) if after[i]!=before[i]]
            self.blob+=struct.pack('<III',entry,len(args),self.u.reg_read(UC_X86_REG_EAX))
            self.blob+=b''.join(struct.pack('<I',a&0xffffffff) for a in args)
            self.blob+=struct.pack('<I',len(changes))+b''.join(struct.pack('<IB',REGION+i,v) for i,v in changes)
            self.blob+=struct.pack('<I',len(self.services))
            for called,arguments in self.services:
                self.blob+=struct.pack('<II',called,len(arguments))+b''.join(struct.pack('<I',a) for a in arguments)
            self.calls+=1
        self.groups.append({'name':name,'calls':len(calls),'router_returns':router,**({'note':note} if note else {})})
    def write(self,output,entries):
        data=MAGIC+struct.pack('<I',len(self.groups))+bytes(self.blob)
        out=Path(output);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
        out.with_suffix('.json').write_text(json.dumps({
            'source_sha256':self.sha,'fixture_sha256':hashlib.sha256(data).hexdigest(),
            'entries':entries,'groups':self.groups,'calls':self.calls,
            'compared':'EAX, the substituted call log, and every byte of %d..%d'%(REGION,REGION+REGION_SIZE),
            'substituted_calls':{
                hex(SOUND_CUE):'audio cue service; recording stub that returns immediately',
                hex(POSITION_ROUTER):'field position event router; stub returning the group value',
                hex(POSITION_EVENT):'map position event trigger; recording stub that returns immediately'},
            'not_claimed':'Groups whose entry reaches a substituted call are contracts on that entry, not a whole-program original comparison.'},indent=2)+'\n')
        print('original C_MAPS calls',self.calls,'groups',len(self.groups))
