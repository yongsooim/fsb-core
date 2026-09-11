#!/usr/bin/env python3
"""Read the original map setup routines and recover the records they register.

The map installers are straight-line code that fills a local rectangle and hands
it to the overlay registrar at 0x457b1b, sometimes writing extra fields into the
record the registrar just published. This decodes that shape directly out of
FLYINGSB.EXE and fails closed on anything it does not fully account for, so an
unrecognised instruction leaves the entry unported instead of guessed.

Emits tools/parallel/C_MAPS/map_setup_tables.json:
  entry -> [{'rect':[l,t,r,b],'callback':int,'flags':int,'fields':{offset:value}}]
plus the reason every rejected entry was rejected.
"""
import json,sys
from collections import defaultdict
from pathlib import Path
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from capstone.x86_const import X86_OP_IMM,X86_OP_REG,X86_OP_MEM
from prepare_event0 import PE

REGISTER_OVERLAY=0x457b1b
CURRENT_RECORD=0x806618      # The registrar publishes the slot it just took here.
SHA='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'

class Reject(Exception):pass

class Frame:
    """Constant tracking for one straight-line installer body."""
    def __init__(self):
        self.reg=defaultdict(lambda:None)   # register name -> constant
        self.local={}                       # ebp-relative slot -> constant
        self.stack=[]                       # pushed constants, most recent first
        self.records=[]
        self.pops=None

def slot(op,md):
    if op.type!=X86_OP_MEM or md.reg_name(op.mem.base)!='ebp' or op.mem.index:raise Reject('non-frame memory')
    return op.mem.disp

def published(op,i):
    """A read of globals::current_overlay_effect names the record just taken."""
    return op.type==X86_OP_MEM and op.mem.base==0 and op.mem.index==0 and (op.mem.disp&0xffffffff)==CURRENT_RECORD

def field(frame,op,i):
    """A write through the registrar's slot pointer, as (offset, record index)."""
    if op.type!=X86_OP_MEM or op.mem.index or not op.mem.base:return None
    held=frame.reg[i.reg_name(op.mem.base)]
    if not (isinstance(held,tuple) and held[0]=='record'):return None
    if held[1]<0:raise Reject('record field written before any registration')
    return op.mem.disp,held[1]

def decode(pe,md,entry,end):
    data=pe.data[pe.offset(entry-pe.base):pe.offset(end-pe.base)]
    frame=Frame();pending=None
    for i in md.disasm(data,entry):
        name,ops=i.mnemonic,i.operands
        if name in ('push',):
            if ops[0].type==X86_OP_IMM:frame.stack.insert(0,ops[0].imm&0xffffffff)
            elif ops[0].type==X86_OP_REG:frame.stack.insert(0,frame.reg[i.reg_name(ops[0].reg)])
            elif ops[0].type==X86_OP_MEM and i.reg_name(ops[0].mem.base)=='ebp' and not ops[0].mem.index:
                frame.stack.insert(0,frame.local.get(ops[0].mem.disp))
            else:raise Reject('push '+i.op_str)
        elif name=='pop':
            if ops[0].type!=X86_OP_REG:raise Reject('pop '+i.op_str)
            if not frame.stack:raise Reject('pop below frame')
            frame.reg[i.reg_name(ops[0].reg)]=frame.stack.pop(0)
        elif name=='mov':
            if ops[0].type==X86_OP_REG:
                target=i.reg_name(ops[0].reg)
                if ops[1].type==X86_OP_IMM:frame.reg[target]=ops[1].imm&0xffffffff
                elif ops[1].type==X86_OP_REG:frame.reg[target]=frame.reg[i.reg_name(ops[1].reg)]
                elif published(ops[1],i):frame.reg[target]=('record',len(frame.records)-1)
                else:raise Reject('mov reg, '+i.op_str)
            elif published(ops[0],i):
                held=frame.reg[i.reg_name(ops[1].reg)] if ops[1].type==X86_OP_REG else None
                if not (isinstance(held,tuple) and held[0]=='record'):raise Reject('publishes a slot this decoder did not see')
                frame.records[held[1]]['publish']=True
            elif field(frame,ops[0],i) is not None:
                where,record=field(frame,ops[0],i)
                value=ops[1].imm&0xffffffff if ops[1].type==X86_OP_IMM else frame.reg[i.reg_name(ops[1].reg)] if ops[1].type==X86_OP_REG else None
                if not isinstance(value,int):raise Reject('record field is not constant')
                frame.records[record]['fields'][str(where)]=value
            elif ops[0].type==X86_OP_MEM:
                where=slot(ops[0],i)
                value=ops[1].imm&0xffffffff if ops[1].type==X86_OP_IMM else frame.reg[i.reg_name(ops[1].reg)] if ops[1].type==X86_OP_REG else None
                if value is None:raise Reject('mov local, '+i.op_str)
                frame.local[where]=value
            else:raise Reject('mov '+i.op_str)
        elif name=='lea':
            if ops[1].type!=X86_OP_MEM:raise Reject('lea '+i.op_str)
            base=i.reg_name(ops[1].mem.base)
            if base=='ebp' and not ops[1].mem.index:frame.reg[i.reg_name(ops[0].reg)]=('local',ops[1].mem.disp)
            elif base=='esp':raise Reject('lea from esp')
            else:raise Reject('lea '+i.op_str)
        elif name=='movsd':
            # The compiler copies the rectangle into the outgoing argument slot.
            # Both pointers are frame locals, so mirror the copy and step them.
            source,destination=frame.reg['esi'],frame.reg['edi']
            if not (isinstance(source,tuple) and isinstance(destination,tuple)):raise Reject('movsd outside the frame')
            frame.local[destination[1]]=frame.local.get(source[1])
            frame.reg['esi']=('local',source[1]+4);frame.reg['edi']=('local',destination[1]+4)
        elif name=='call':
            if ops[0].type!=X86_OP_IMM:raise Reject('indirect call')
            target=ops[0].imm&0xffffffff
            if target!=REGISTER_OVERLAY:raise Reject('call 0x%x'%target)
            if len(frame.stack)<3:raise Reject('short registrar call')
            rect,callback,flags=frame.stack[0],frame.stack[1],frame.stack[2]
            frame.stack=frame.stack[3:]
            if not isinstance(rect,tuple):raise Reject('registrar rectangle is not a frame local')
            values=[frame.local.get(rect[1]+k*4) for k in range(4)]
            if any(v is None for v in values):raise Reject('registrar rectangle is not constant')
            if not isinstance(callback,int) or not isinstance(flags,int):raise Reject('registrar callback or flags is not constant')
            frame.records.append({'rect':values,'callback':callback,'flags':flags,'fields':{}})
            frame.reg['eax']=('record',len(frame.records)-1) # The registrar returns the slot.
        elif name=='xor' and ops[0].type==X86_OP_REG and ops[1].type==X86_OP_REG and ops[0].reg==ops[1].reg:
            frame.reg[i.reg_name(ops[0].reg)]=0
        elif name in ('and','or') and ops[0].type==X86_OP_MEM and ops[1].type==X86_OP_IMM:
            immediate=ops[1].imm&0xffffffff
            if name=='and' and immediate!=0:raise Reject('partial and through memory')
            if name=='or' and immediate!=0xffffffff:raise Reject('partial or through memory')
            value=0 if name=='and' else 0xffffffff
            target=field(frame,ops[0],i)
            if target is not None:frame.records[target[1]]['fields'][str(target[0])]=value
            else:frame.local[slot(ops[0],i)]=value
        elif name in ('sub','add') and ops[0].type==X86_OP_REG and i.reg_name(ops[0].reg)=='esp':
            if ops[1].type!=X86_OP_IMM:raise Reject('variable stack adjust')
            if name=='add':  # cdecl cleanup of arguments this decoder already read
                for _ in range((ops[1].imm&0xffffffff)//4):
                    if frame.stack:frame.stack.pop(0)
        elif name=='ret':
            frame.pops=(ops[0].imm&0xffffffff) if ops and ops[0].type==X86_OP_IMM else 0
            break
        elif name=='leave':continue
        elif name in ('nop',):continue
        else:raise Reject(name+' '+i.op_str)
    if not frame.records:raise Reject('no registrar call')
    if frame.pops is None:raise Reject('no return reached')
    return {'pops':frame.pops,'records':frame.records}

def main(executable,decompiled,scopes,output):
    pe=PE(executable)
    md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    paths={int(f.name[:8],16):f for f in Path(decompiled).glob('*.c') if f.name[:8].isalnum() and len(f.name)>9 and f.name[8]=='_'}
    starts=sorted(paths);ends=dict(zip(starts,starts[1:]))
    scope=json.loads(Path(scopes).read_text())['workers']['C_MAPS']['entries']
    tables,rejected={},{}
    for record in scope:
        entry=int(record['entry'],16)
        try:tables[record['entry']]=decode(pe,md,entry,ends.get(entry,entry+0x2000))
        except Reject as reason:rejected[record['entry']]=str(reason)
        except Exception as reason:rejected[record['entry']]='decoder: '+str(reason)
    Path(output).write_text(json.dumps({'source_sha256':SHA,'registrar':hex(REGISTER_OVERLAY),
        'accepted':tables,'rejected':rejected},indent=1)+'\n')
    print('pure registration installers',len(tables),'of',len(scope),
          'records',sum(len(v['records']) for v in tables.values()),
          'argument bytes',sorted({v['pops'] for v in tables.values()}))
    counts=defaultdict(int)
    for reason in rejected.values():counts[reason.split(' ')[0]]+=1
    for reason,count in sorted(counts.items(),key=lambda kv:-kv[1])[:12]:print('  rejected %-28s %d'%(reason,count))
if __name__=='__main__':main(*sys.argv[1:])
