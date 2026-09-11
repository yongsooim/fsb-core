#!/usr/bin/env python3
"""Generate fixed C++ battle functions from the audited FLYINGSB instruction CFG.

Capstone is a development-only decoder. Runtime code has fixed C++ statements,
labels and a finite call table. Unsupported instructions/targets fail closed.
The manifest records source bytes, entry points and explicit service boundaries.
"""
import argparse
import bisect
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from capstone.x86_const import X86_OP_IMM,X86_OP_REG,X86_OP_MEM
from prepare_event0 import PE
from recovered_modules import emit_modules

SHA='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
REG={}
for n,names in enumerate([['eax','ax','al','ah'],['ecx','cx','cl','ch'],['edx','dx','dl','dh'],['ebx','bx','bl','bh'],['esp','sp'],['ebp','bp'],['esi','si'],['edi','di']]):
    for j,name in enumerate(names):REG[name]=(n,[4,2,1,1][j],8 if j==3 else 0)
CONDS=dict(zip(['e','ne','l','ge','le','g','b','ae','be','a','s','ns','o','no','p','np'],range(16)))
def u(n):return f'0x{n&0xffffffff:x}u'
def write_changed(path,text):
    if not path.exists() or path.read_text()!=text:path.write_text(text)

def main(executable,decompiled,output):
    p=PE(executable)
    if hashlib.sha256(p.data).hexdigest()!=SHA:raise ValueError('wrong executable revision')
    imports={};pe_header=p.u32(0x3c);descriptor=p.offset(p.u32(pe_header+24+104))
    while p.u32(descriptor+12):
        lookup=p.u32(descriptor) or p.u32(descriptor+16);iat=p.u32(descriptor+16);slot=0
        while p.u32(p.offset(lookup+slot*4)):
            name=p.u32(p.offset(lookup+slot*4));imports[p.base+iat+slot*4]=str(name&65535) if name&0x80000000 else p.string(p.base+name+2);slot+=1
        descriptor+=20
    paths={int(f.name[:8],16):f for f in Path(decompiled).glob('*.c') if re.match(r'[0-9a-f]{8}_',f.name)}
    # A direct call is an original control-flow edge regardless of subsystem.
    # Stop only at explicit core/platform adapters, not historical battle ranges.
    boundary_data=json.loads((Path(__file__).parent/'original_service_boundaries.json').read_text())
    service_boundaries={int(a,16) for a in boundary_data['entries']}
    native_data=json.loads((Path(__file__).parent/'native_reconstructions.json').read_text())['entries']
    native_entries={int(a,16) for a in native_data}
    partial_data=json.loads((Path(__file__).parent/'partial_native_reconstructions.json').read_text())['entries']
    if set(partial_data)&set(native_data):raise ValueError('partial entry also marked complete')
    starts=sorted(paths);ends=dict(zip(starts,starts[1:]))
    entries=[a for a in starts if 0x44e16c<=a<=0x453b8d and a not in [0x450b1c,0x450d7a,0x45149b]]
    entries+= [0x4489d0,0x448a0a,0x448d90,0x4490c7,0x44956a,0x45149b,0x498090,0x499150,0x499160]
    entries += [a for a in starts if 0x44c6eb<=a<=0x44de92 or 0x461854<=a<=0x461f44]
    entries += [0x44aaa1,0x44ab05,0x44ab2e,0x45befc,0x45c14f,0x458ed7,0x45abf9,0x45d89c,0x45d91d,0x45db53,0x45db6c,0x45d774,0x45d82d,0x45da86,0x45da95,0x45d208,0x462150,0x462203,0x4622b6,0x457ba4,0x408de1,0x408e69,0x45c3a9]
    # This original passive callback is absent from the decompiler entry index.
    paths[0x45c110]=Path("0045c110_passive_battle_actor_machine_code.c");ends[0x45c110]=0x45c14f
    entries += [0x45c063,0x45c110]
    paths[0x461c66]=Path("00461c66_defeated_enemy_cleanup_machine_code.c");ends[0x461c66]=0x461ca2
    entries += [0x461c66]
    entries += [0x447841,0x447898,0x4478ba]
    effect_ops=[p.u32(p.offset(0x5be798+i*4-p.base)) for i in range(20)]
    for i,a in enumerate(effect_ops):
        if a not in paths:
            paths[a]=Path(f'{a:08x}_effect_script_op{i:02x}_machine_code.c')
            ends[a]=min([v for v in starts+effect_ops if v>a])
    entries += effect_ops
    entries += [0x47aaa5,0x480827,0x46e6b4,0x46e79c,0x46e7f3,0x470925,0x470948,0x47096b,0x47aaf5,0x474aef,0x4753ea]
    entries += [0x4646be,0x450b1c,0x450d7a,0x4138ee,0x44e085,0x449083,0x448a39,0x449005]
    entries += [0x4454a0,0x44569f,0x4015c2,0x4471c3,0x4458d2,0x4473b4,0x44725c,0x439701,0x43955f,0x43d655,0x43f8bc,0x44601e,0x4639fc,0x463ee9,0x463258,0x463bf8,0x463dea,0x464102]
    entries += [0x45d395,0x413b74,0x413d1e,0x413da7,0x419f54,0x419f74,0x4306cb,0x448a98,0x455099,0x455363,0x457e56,0x457ec9,0x45dd8b,0x45de29,0x45e139,0x486b53,0x42feaa,0x45f528]
    paths[0x45e5f9]=Path("0045e5f9_item_shop_body_machine_code.c");ends[0x45e5f9]=0x45ef2d
    entries += [0x45ef2d,0x45e5f9,0x45dcc8,0x45dc7f,0x45dc18,0x45e167,0x45e1a1,0x45e1dc,0x45e231,0x45e404,0x45e421]
    paths[0x4874a7]=Path("004874a7_map_marker_tick_machine_code.c");ends[0x4874a7]=0x48752d
    paths[0x4874df]=Path("004874df_spawn_map_marker_machine_code.c");ends[0x4874df]=0x48752d
    entries += [0x4874df]
    entries += [0x404cf0,0x41a0fb,0x41a126,0x43070b,0x43075b,0x4307ac,0x431700,0x431710,0x4120c7]
    # Later actor opcodes resolve a selected object, synchronize its pixel/tile
    # coordinates, or copy its pose. Keep these original helpers
    # shared by the opcode bodies rather than duplicating Event44 behavior.
    entries += [0x43029b,0x4302d2,0x430385]
    #45f82d handles D in the paired-party tower. The same swap is called by
    #458ed7's linked exits; keep both paths on the original snapshot routines.
    entries += [0x45ef3f,0x45efb6,0x45f07e]
    #Ending E2/999 requests this original compact drain / WM_CLOSE sequence.
    entries += [0x407ffd,0x4029db]
    # The event VM has a fixed opcode->native-handler table. Late campaign
    # variants can use these original bodies through the core executor while
    # already-audited handwritten contracts remain the normal dispatch path.
    vm_handlers={p.u32(p.offset(0x5aa410+opcode*4-p.base)) for opcode in range(256)}-{0}
    vm_starts=sorted(set(paths)|vm_handlers)
    for entry in sorted(vm_handlers):
        if entry not in paths:
            paths[entry]=Path(f'{entry:08x}_event_opcode_handler_machine_code.c')
            ends[entry]=vm_starts[bisect.bisect_right(vm_starts,entry)]
        entries.append(entry)
    entries += [0x46018b,0x412ff8,0x413afb,0x4321ab,0x4328ab,0x4325b4,0x432742,0x436077,0x448d72,0x454180,0x454220,0x454f71,0x454f9d,0x457432,0x457445,0x457700,0x457b8f,0x45d84b,0x45d9c8,0x458dc4,0x4874a7,0x49711f,0x404ed0,0x497036,0x45b8a0,0x431739,0x431749,0x4317e8]
    entries += [a for a in starts if 0x435740<=a<=0x4384a0]
    #435d41 selects a compact callback from this20-slot route-animation table.
    #Four entry boundaries are absent from the decompiler index.
    world_routes={p.u32(p.offset(0x5b1150+i*4-p.base)) for i in range(20)}
    world_starts=sorted(set(paths)|world_routes)
    for entry in sorted(world_routes):
        if entry not in paths:
            paths[entry]=Path(f'{entry:08x}_worldmap_route_callback_machine_code.c')
            ends[entry]=world_starts[bisect.bisect_right(world_starts,entry)]
        entries.append(entry)
    # Original title art, selection/fades and four deferred draw callbacks.
    paths[0x438833]=Path("00438833_title_background_draw_machine_code.c");ends[0x438833]=0x438860
    entries += [0x4384a3,0x43859d,0x4385e9,0x438833,0x438860,0x438990,0x438abb,0x431869]
    entries += [a for a in starts if 0x448aca<=a<=0x44c6eb]
    paths[0x447e31]=Path("00447e31_bound_son_pose_machine_code.c");ends[0x447e31]=0x447ea3
    entries += [0x447e31,0x420c84,0x448977,0x44899e,0x44df90,0x45584f,0x45df01]
    # Field ESC menu callbacks are data edges, not necessarily direct callees.
    # Keep the original root/party/equipment/save/load/settings/confirm machines.
    entries += [p.u32(p.offset(0x5b1698+i*12-p.base)) for i in range(18)]
    entries += [a for a in starts if 0x460deb<=a<0x461854]
    entries += [0x431e95,0x431ede,0x431f06,0x460528,0x431893,0x439935,0x45f0e9,0x45f47e,0x45f495]
    paths[0x44064b]=Path("0044064b_field_equip_populate_layout_records_machine_code.c");ends[0x44064b]=0x440702
    entries += [0x44064b,0x458d78,0x458da9]
    # Static NPC pose entries omitted from the decompiler index. Boundaries
    # are the consecutive RET4-terminated bodies; callback table5be718 owns them.
    pose_starts=[0x447ea3,0x447ef9,0x447f4c,0x447f9f,0x447fef,0x448042,0x448095,0x4480e8,0x4481a9,0x44821f]
    for entry,end in zip(pose_starts,pose_starts[1:]):
        paths[entry]=Path(f'{entry:08x}_static_npc_pose_machine_code.c');ends[entry]=end
    # All32 pose callback slots precede the effect opcode table at5be798.
    # Some are selected only by script-driven pose changes, and44828a/4483b5
    # have no standalone decompiler file. Do not silently drop those data edges.
    actor_callbacks={p.u32(p.offset(0x5be718+kind*4-p.base)) for kind in range(32)}-{0}
    actor_starts=sorted(set(paths)|actor_callbacks)
    for callback in sorted(actor_callbacks):
        if callback not in paths:
            paths[callback]=Path(f'{callback:08x}_actor_pose_callback_machine_code.c')
            ends[callback]=actor_starts[bisect.bisect_right(actor_starts,callback)]
        entries.append(callback)
    # Campaign activation/router and map callback roots are data edges.
    entries += [0x412181,0x412147,0x413787,0x430aa6,0x4552a3,0x45733a,0x457b7c,0x45852e,0x458b79]
    # The two gatewarp factories pass these callback addresses as arguments to
    #45d89c. They are data edges, including the paired screen effect family.
    entries += [0x4580f7,0x45826c,0x4583cd,0x458691,0x4588ee]
    map_callbacks={p.u32(p.offset(0x5c4f60+map_id*68-p.base)) for map_id in range(500)}-{0,0x496bee}
    all_starts=sorted(set(paths)|map_callbacks)
    for callback in sorted(map_callbacks):
        if callback not in paths:
            paths[callback]=Path(f'{callback:08x}_map_setup_callback_machine_code.c')
            ends[callback]=all_starts[bisect.bisect_right(all_starts,callback)]
        entries.append(callback)
    # All300 original monster skill records (248 nonempty callback rows): data
    # callback roots are edges, so a
    # direct-CALL closure alone misses tournament and later campaign attacks.
    for action in range(300):
        callback=p.u32(p.offset(0x610c24+action*32-p.base))
        if callback:entries.append(callback)
    for attack in range(96): # Twelve party weapon families, eight attack rows each.
        callback=p.u32(p.offset(0x60885c+attack*24-p.base))
        if callback:entries.append(callback)
    for skill in range(66): # Character skill records609148..609c9f, 44bytes.
        callback=p.u32(p.offset(0x60916c+skill*44-p.base))
        if callback:entries.append(callback)
    #461854 mode3 dispatches consumable effects from the360 item records.
    #308/311/312 point to code missing from the decompiler entry index.
    item_callbacks={p.u32(p.offset(0x6131cc+item*76-p.base)) for item in range(360)}-{0}
    item_starts=sorted(set(paths)|item_callbacks)
    for entry in sorted(item_callbacks):
        if entry not in paths:
            paths[entry]=Path(f'{entry:08x}_item_effect_callback_machine_code.c')
            ends[entry]=item_starts[bisect.bisect_right(item_starts,entry)]
        entries.append(entry)
    # Effect scripts also store callback pointers in their byte records (for
    # example4627eb), outside the direct CALL/immediate closure. Include the
    # audited battle/effect function range; retain the native banner boundary.
    entries += [a for a in starts if 0x4622c2<=a<0x486b53 and a not in [0x464494,0x464158]]
    #4622c2 allocates attached status visuals through ten table slots. The
    #sleep emitter46274d is a real boundary omitted by the decompiler index.
    status_callbacks={p.u32(p.offset(0x5d26a0+i*4-p.base)) for i in range(10)}
    status_starts=sorted(set(paths)|status_callbacks)
    for entry in sorted(status_callbacks):
        if entry not in paths:
            paths[entry]=Path(f'{entry:08x}_attached_status_callback_machine_code.c')
            ends[entry]=status_starts[bisect.bisect_right(status_starts,entry)]
        entries.append(entry)
    #493acc registers this code pointer for twelve falling-floor overlays.
    #The cache labels it as data, but the PE has an eight-way code dispatch
    #and RET4 before the jump table at493aac.
    paths[0x493a26]=Path("00493a26_falling_floor_callback_machine_code.c")
    ends[0x493a26]=0x493acc
    entries.append(0x493a26)
    entries=sorted(set(entries))
    cs=Cs(CS_ARCH_X86,CS_MODE_32);cs.detail=True
    def raw(a,n):o=p.offset(a-p.base);return p.data[o:o+n]
    def dis(a):return next(cs.disasm(raw(a,15),a),None)
    def addr(i,o):
        if o.mem.segment:raise ValueError(f'segmented address {i.address:x}')
        terms=[u(o.mem.disp)]
        if o.mem.base:terms.append('r[%d]'%REG[i.reg_name(o.mem.base)][0])
        if o.mem.index:terms.append('r[%d]*%du'%(REG[i.reg_name(o.mem.index)][0],o.mem.scale))
        return '('+'+'.join(terms)+')'
    def value(i,o):
        if o.type==X86_OP_IMM:return u(o.imm)
        if o.type==X86_OP_REG:return 'get(%d,%d,%d)'%REG[i.reg_name(o.reg)]
        if o.type==X86_OP_MEM:return f'read({addr(i,o)},{o.size})'
        raise ValueError('unknown operand')
    def assign(i,o,v):
        if o.type==X86_OP_REG:
            n,w,s=REG[i.reg_name(o.reg)];return f'put({n},{v},{w},{s});'
        if o.type==X86_OP_MEM:return f'write({addr(i,o)},{v},{o.size});'
        raise ValueError('nonwritable destination')
    functions={};external=set();mn=collections.Counter();tables={};discovered_callbacks={};discovered_direct_calls={}
    def follow_call(target,site):
        if target in entries:return
        if target in service_boundaries or not 0x400000<=target<0x4971f0:
            external.add(target);return
        if target not in paths:
            # The CALL/JMP itself proves this entry; an arbitrary code-looking
            # immediate or data word is never enough to introduce a function.
            paths[target]=Path(f'{target:08x}_direct_callee_machine_code.c')
            ends[target]=min(v for v in paths if v>target)
        entries.append(target);discovered_direct_calls.setdefault(target,hex(site))
    for entry in entries:
        body={};pending=[entry]
        while pending:
            a=pending.pop()
            if a in body:continue
            if not entry<=a<ends[entry]:raise ValueError(f'branch outside function {entry:x} -> {a:x}')
            i=dis(a)
            if not i:raise ValueError(f'undecodable instruction {a:x}')
            body[a]=i;mn[i.mnemonic]+=1;op=i.operands;nxt=a+i.size
            # 45d89c takes a callback pointer. Some tiny callbacks live after
            # an earlier RET and have no standalone decompiler-cache file.
            # Discover this concrete registration edge, not arbitrary constants.
            if i.mnemonic=='push' and op[0].type==X86_OP_IMM and 0x400000<=op[0].imm<0x4971f0:
                following=dis(nxt);target=op[0].imm
                if following and following.mnemonic=='call' and following.operands[0].type==X86_OP_IMM and following.operands[0].imm==0x45d89c:
                    if target not in paths:
                        paths[target]=Path(f'{target:08x}_registered_effect_callback_machine_code.c')
                        ends[target]=min(v for v in starts if v>target)
                    discovered_callbacks.setdefault(target,[]).append(hex(a))
                    if target not in entries:entries.append(target)
            # Callback addresses stored/passed by these fixed routines are
            # original control-flow edges too, even when invocation is deferred.
            for operand in op:
                if operand.type==X86_OP_IMM:
                    target=operand.imm
                    if target in paths and target not in entries and (0x461c66<=target<=0x486b53 or 0x439ca5<=target<=0x4473b4 or 0x435740<=target<=0x4384a0 or 0x486b53<=target<=0x4971f0) and target not in [0x464494,0x4646be,0x464158]:entries.append(target)
            if i.mnemonic=='ret':continue
            if i.mnemonic=='jmp':
                if op[0].type==X86_OP_IMM:
                    if entry<=op[0].imm<ends[entry]:pending.append(op[0].imm)
                    else:follow_call(op[0].imm,a)
                else:
                    if op[0].type!=X86_OP_MEM or op[0].mem.base or op[0].mem.scale!=4:raise ValueError(f'unsupported indirect jump {a:x}')
                    table=op[0].mem.disp;targets=[]
                    for index in range(1024):
                        target=struct.unpack('<I',raw(table+index*4,4))[0]
                        if not entry<=target<ends[entry]:break
                        targets.append(target)
                    if not targets:raise ValueError(f'empty branch table {a:x}')
                    tables[a]=(table,targets);pending.extend(targets)
                continue
            if i.mnemonic.startswith('j'):pending.append(op[0].imm)
            if i.mnemonic=='call' and op[0].type==X86_OP_IMM and op[0].imm not in entries:
                follow_call(op[0].imm,a)
            pending.append(nxt)
        functions[entry]=body
    entries=sorted(entries)
    external.difference_update(entries)
    preamble=['// Generated by tools/translate_battle.py; edit the generator, not this file.',f'// Original EXE SHA256: {SHA}','#include "fsb_core/recovered_battle.hpp"','#include <cstring>','#include <cmath>','namespace fsb::core {']
    emitted_bodies={}
    for entry,body in functions.items():
        if entry in native_entries:continue
        lines=[]
        lines+=[f'// {paths[entry].stem}',f'void RecoveredBattle::fn_{entry:x}(){{',f'goto L{entry:x};']
        ordered=sorted(body)
        for index,a in enumerate(ordered):
            i=body[a];m=i.mnemonic;op=i.operands;nxt=a+i.size
            lines.append(f'L{a:x}: {{ // {i.bytes.hex()}  {m} {i.op_str}')
            v=[] if m.startswith('f') or m.split()[-1] in ['movsd','stosd','movsb','stosb','movsw','stosw'] else [value(i,o) for o in op];w=op[0].size if op else 4
            if m=='mov':code=assign(i,op[0],v[1])
            elif m=='movzx':code=assign(i,op[0],v[1])
            elif m=='movsx':
                sign=1<<(op[1].size*8-1);code=assign(i,op[0],f'(({v[1]}^{u(sign)})-{u(sign)})')
            elif m=='lea':code=assign(i,op[0],addr(i,op[1]))
            elif m in ['add','sub','adc','sbb']:
                method='add' if m in ['add','adc'] else 'sub';code=assign(i,op[0],f'{method}({v[0]},{v[1]},{w},{str(m in ["adc","sbb"]).lower()})')
            elif m=='cmp':code=f'sub({v[0]},{v[1]},{w});'
            elif m in ['and','or','xor','test']:
                operator={'and':'&','or':'|','xor':'^','test':'&'}[m];expr=f'logic({v[0]}{operator}{v[1]},{w})';code=expr+';' if m=='test' else assign(i,op[0],expr)
            elif m in ['inc','dec']:code=assign(i,op[0],f'inc({v[0]},{w},{1 if m=="inc" else -1})')
            elif m=='neg':code=assign(i,op[0],f'sub(0,{v[0]},{w})')
            elif m=='not':code=assign(i,op[0],f'~{v[0]}')
            elif m in ['shl','sal','shr','sar']:code=assign(i,op[0],f'shift({v[0]},{v[1]},{w},{0 if m in ["shl","sal"] else 1 if m=="shr" else 2})')
            elif m=='imul' and len(op)>=2:code=assign(i,op[0],f'multiply({v[-2]},{v[-1]},{w})')
            elif m in ['div','idiv']:code=f'divide({v[0]},{str(m=="idiv").lower()});'
            elif m=='cdq':code='r[2]=(r[0]&0x80000000u)?0xffffffffu:0;'
            elif m=='push':code=f'push({v[0]});'
            elif m=='pop':code=assign(i,op[0],'pop()')
            elif m=='leave':code='r[4]=r[5];r[5]=pop();'
            elif m=='call':
                target=v[0]
                if op[0].type==X86_OP_MEM and not op[0].mem.base and not op[0].mem.index and op[0].mem.disp in imports:target=u(op[0].mem.disp)
                code=f'push({u(nxt)});dispatch({target});'
            elif m=='ret':code=f'pop();r[4]+={v[0] if v else 0};return;'
            elif m=='jmp':
                if op[0].type==X86_OP_IMM:code=f'goto L{op[0].imm:x};' if op[0].imm in body else f'dispatch({v[0]});return;'
                else:
                    targets=tables[a][1];code=f'switch({v[0]}){{'+''.join(f'case {u(t)}:goto L{t:x};' for t in sorted(set(targets)))+f'default:throw Fault({u(a)},"unresolved recovered battle jump");}}'
            elif m.startswith('j') and m[1:] in CONDS:code=f'if(condition({CONDS[m[1:]]}))goto L{op[0].imm:x};'
            elif m.startswith('set') and m[3:] in CONDS:code=assign(i,op[0],f'condition({CONDS[m[3:]]})')
            elif m.split()[-1] in ['movsd','stosd','movsb','stosb','movsw','stosw']:
                operation=m.split()[-1];stride={'b':1,'w':2,'d':4}[operation[-1]]
                code=f'write(r[7],'+(f'read(r[6],{stride})' if operation.startswith('movs') else 'r[0]')+f',{stride});'
                if operation.startswith('movs'):code+=f'r[6]+={stride};'
                code+=f'r[7]+={stride};'
                if m.startswith('rep '):code='while(r[1]){'+code+'--r[1];}'
            elif m=='stosd':code='write(r[7],r[0]);r[7]+=4;'
            elif m=='rep stosd':code='while(r[1]){write(r[7],r[0]);r[7]+=4;--r[1];}'
            elif m=='rep movsd':code='while(r[1]){write(r[7],read(r[6]));r[6]+=4;r[7]+=4;--r[1];}'
            elif m=='nop':code=''
            elif m=='fild' and op[0].size==4:code=f'fp_.push_back(double(signed32({value(i,op[0])})));'
            elif m in ['fidiv','fimul','fiadd','fisub'] and op[0].size==4:
                code=f'fp_.back(){dict(fidiv="/=",fimul="*=",fiadd="+=",fisub="-=")[m]}double(signed32({value(i,op[0])}));'
            elif m=='fld' and op[0].type==X86_OP_REG:code=f'fp_.push_back(fp_.at(fp_.size()-1-{i.op_str[3:-1]}));'
            elif m in ['fmul','fadd','fsub','fdiv'] and len(op)==1 and op[0].type==X86_OP_REG:
                code=f'fp_.back(){dict(fmul="*=",fadd="+=",fsub="-=",fdiv="/=")[m]}fp_.at(fp_.size()-1-{i.op_str[3:-1]});'
            elif m in ['fmulp','faddp','fsubp','fdivp'] and len(op)==1 and op[0].type==X86_OP_REG:
                code=f'fp_.at(fp_.size()-1-{i.op_str[3:-1]}){dict(fmulp="*=",faddp="+=",fsubp="-=",fdivp="/=")[m]}fp_.back();fp_.pop_back();'
            elif m=='fchs':code='fp_.back()=-fp_.back();'
            elif m in ['fst','fstp'] and op[0].type==X86_OP_REG:
                code=f'fp_.at(fp_.size()-1-{i.op_str[3:-1]})=fp_.back();'
                if m=='fstp':code+='fp_.pop_back();'
            elif m in ['fst','fstp'] and op[0].type==X86_OP_MEM and op[0].size==8:
                address=addr(i,op[0]);code=f'std::uint64_t bits;const double value=fp_.back();std::memcpy(&bits,&value,8);write({address},std::uint32_t(bits));write({address}+4,std::uint32_t(bits>>32));'
                if m=='fstp':code+='fp_.pop_back();'
            elif m in ['fld1','fldz']:code=f'fp_.push_back({1 if m=="fld1" else 0});'
            elif m in ['fld','fmul','fadd','fsub','fdiv'] and op[0].type==X86_OP_MEM and op[0].size==4:
                code=f'const auto bits=read({addr(i,op[0])});float value;std::memcpy(&value,&bits,4);'
                code+='fp_.push_back(double(value));' if m=='fld' else f'fp_.back(){dict(fmul="*=",fadd="+=",fsub="-=",fdiv="/=")[m]}double(value);'
            elif m in ['fld','fmul','fadd','fsub','fdiv','fsubr','fdivr'] and op[0].type==X86_OP_MEM and op[0].size==8:
                address=addr(i,op[0]);code=f'const auto bits=std::uint64_t(read({address}))|(std::uint64_t(read({address}+4))<<32);double value;std::memcpy(&value,&bits,8);'
                code+='fp_.push_back(value);' if m=='fld' else f'fp_.back()=value{dict(fsubr="-",fdivr="/")[m]}fp_.back();' if m in ['fsubr','fdivr'] else f'fp_.back(){dict(fmul="*=",fadd="+=",fsub="-=",fdiv="/=")[m]}value;'
            else:raise ValueError(f'unsupported {a:x} {m} {i.op_str}')
            lines.append(code);lines.append('}')
            if m not in ['jmp','ret'] and (index+1==len(ordered) or ordered[index+1]!=nxt):lines.append(f'goto L{nxt:x};')
        lines.append('}')
        emitted_bodies[entry]=lines
    lines=[]
    lines+=['bool RecoveredBattle::has_entry(Address entry){switch(entry){']
    for a in entries:lines.append(f'case {u(a)}:return true;')
    lines+=['default:return false;}}','bool RecoveredBattle::is_import(Address entry){switch(entry){']
    for a in imports:lines.append(f'case {u(a)}:return true;')
    lines+=['default:return false;}}','void RecoveredBattle::dispatch(Address entry){','if(depth_>=128)throw Fault(entry,"recovered battle call depth exceeded");','++depth_;try{switch(entry){','case 0x498130:float_to_integer();break;','case 0x498174:float_square_root();break;','case 0x499394:float_arctangent();break;']
    for a in entries:
        # The default remains the exact original routine. Runtime may opt into
        # the requested development XP policy or the bounded AI region raster.
        opt_in='if(!service||!service(entry,*this))' if a in [0x44de15,0x44de92,0x452168,0x452255] else ''
        if a in native_entries:
            lines.append(f'case {u(a)}:{opt_in}if(!dispatch_native(entry))throw Fault(entry,"missing reconstructed native entry");break;')
        elif hex(a) in partial_data:
            partial=partial_data[hex(a)]
            lines.append(f'case {u(a)}:if(!{partial["dispatcher"]}(entry))fn_{a:x}();break; // Explicit partial migration; selection is recorded in manifest.')
        else:lines.append(f'case {u(a)}:{opt_in}fn_{a:x}();break;')
    lines+=['case 0x497d70: { const auto destination=argument(0),value=argument(1)&255,count=argument(2);for(unsigned i=0;i<count;++i)write(destination+i,value,1);result(destination);break; }','case 0x497790: {const auto dst=argument(0),src=argument(1),size=argument(2);std::vector<std::uint8_t> bytes(size);for(unsigned i=0;i<size;++i)bytes[i]=std::uint8_t(read(src+i,1));for(unsigned i=0;i<size;++i)write(dst+i,bytes[i],1);result(dst);break;}','case 0x4973f0: {const auto src=argument(0);unsigned count=0;while(read(src+count,1))++count;result(count);break;}','case 0x497300: {const auto dst=argument(0),src=argument(1);unsigned i=0;do{const auto v=read(src+i,1);write(dst+i,v,1);if(!v)break;++i;}while(true);result(dst);break;}','case 0x497310: {const auto dst=argument(0),src=argument(1);unsigned count=0,index=0;while(read(dst+count,1))++count;do{const auto value=read(src+index,1);write(dst+count+index,value,1);if(!value)break;++index;}while(true);result(dst);break;}','default:if(!service||!service(entry,*this))throw Fault(entry,"unconnected recovered battle service");break;','}if(after_call)after_call(entry,*this);}catch(Fault& fault){fault.recovered_calls.push_back(entry);--depth_;throw;}catch(...){--depth_;throw;}--depth_;','}','} // namespace fsb::core']
    root=Path(output)
    generated_sources,generated_owners,module_coupling=emit_modules(root,preamble,emitted_bodies,lines,functions,write_changed)
    monolith=root/'src/recovered_battle_generated.cpp'
    if monolith.exists():
        if not monolith.read_text().startswith('// Generated by tools/translate_battle.py;'):raise ValueError('refusing to remove a non-generated monolith')
        monolith.unlink()
    write_changed(root/'include/fsb_core/recovered_battle_entries.inc','// Generated fixed entry list.\n'+''.join(f'    void fn_{a:x}();\n' for a in entries if a not in native_entries))
    manifest={'source_sha256':SHA,'method':'fixed C++ integer/control-flow translation; no runtime decoder, interpreter or JIT','functions':[{ 'entry':hex(a),'source':paths[a].name,'instructions':len(body),'instruction_sha256':hashlib.sha256(b''.join(body[k].bytes for k in sorted(body))).hexdigest()} for a,body in functions.items()],
              'development_override_entries':{'0x44de15':'optional DebugRewards multi-level XP; exact original function when disabled','0x44de92':'optional DebugRewards payout wrapper; exact original function when disabled'},'bounded_ai_raster_entries':{'0x452168':'single region clipped to map cells; original default retained for comparison','0x452255':'union of regions clipped to map cells; original default retained for comparison'},'external_services':[hex(a) for a in sorted(external)],'discovered_callback_edges':{hex(k):v for k,v in discovered_callbacks.items()},'import_services':{hex(k):v for k,v in imports.items()},'jump_tables':{hex(a):{'address':hex(t),'targets':[hex(v) for v in vals]} for a,(t,vals) in tables.items()},'mnemonics':dict(mn)}
    manifest['discovered_direct_call_edges']={hex(k):v for k,v in discovered_direct_calls.items()}
    if not native_entries<=set(functions):raise ValueError('reconstruction refers to an undiscovered original function')
    manifest['native_reconstructions']={hex(a):dict(native_data[hex(a)],source_instructions=[{'address':hex(site),'bytes':functions[a][site].bytes.hex()} for site in sorted(functions[a])]) for a in sorted(native_entries)}
    manifest['partial_native_reconstructions']=partial_data
    manifest['emitted_functions']=len(functions)-len(native_entries)
    manifest['generated_sources']=generated_sources
    manifest['generated_owners']=generated_owners
    manifest['module_coupling']=module_coupling
    write_changed(root/'reference/recovered-battle-manifest.json',json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'functions':len(functions),'emitted_functions':len(functions)-len(native_entries),'native_reconstructions':len(native_entries),'instructions':sum(mn.values()),'external_services':manifest['external_services']}))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('decompiled');parser.add_argument('output');a=parser.parse_args();main(a.executable,a.decompiled,a.output)
