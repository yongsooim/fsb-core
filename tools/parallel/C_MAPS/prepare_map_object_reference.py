#!/usr/bin/env python3
"""Original event-flag, map-patch and overlay-callback behaviour from FLYINGSB.EXE."""
import json,sys
from pathlib import Path
from x86_probe import Probe

PATCH_TABLE,PATCH_STRIDE,PATCH_RECORDS=0x604040,36,0x199
CURRENT_MAP,GRID_STRIDE,GRID_HEIGHT=0x5d229c,0x7760c4,0x7760c8
TILE_CODES,ATTR_INDEX,RUNTIME_ATTR=0x7e0d30,0x7e4d30,0x7cbca0
LAYER_STRIDE=0x8028
FLAG_BITS=0x806b28
OVERLAY,OVERLAY_STRIDE=0x800dc8,48
ACTORS,ACTOR_STRIDE=0x8073d8,0x1ac
ACTIVE_PARTY,FRAME_COUNTER,MARKER_GATE=0x803a1c,0x6da2d4,0x5f8588
SWITCH_BITS,LIFTED_BLOCKS,SWITCH_RECORDS=0x806b24,0x8073b0,0x5f85b0
LAYER_WIDTH,PARTY_COUNT,PARTY_IDS,PARTY_STATS=0x7e0d20,0x803a20,0x5d2258,0x607a08
RUNTIME_ATTR_PLANE,RNG_STATE,GATE_FRAMES=0x7cbca0,0x6d1bf0,0x5f8a08
CITY_TABLE,CITY_STRIDE,ROUTE_MUSIC=0x5aff98,0x54,0x5b0e50
ANIM_LOOKUP,EVENT_ENTRY,SWITCH_HOLD,ROUTE_SCREEN=0x7b3ca0,0x607cbc,0x8021b0,0x7714a4
FREE_POOL_FIRST,GATE_CROSSING,GATE_POSITIONS=90,0x5f858c,0x5f8590
WORLDMAP_MODE,GAME_MODE=0x5aff7c,0x80465c
FLAG_IDS=[0,1,31,32,33,63,64,100,0x198,0x1000,-1,-31,-32,-33,-64,-96]
SAMPLE_MAP=467
OVERLAY_END,CURRENT_RECORD=0x8019c8,0x806618
SETUP_TABLES='tools/parallel/C_MAPS/map_setup_tables.json'

def bitmap():
    return [(FLAG_BITS+i*4,4,(0x9e3779b9*(i+1))&0xffffffff) for i in range(0x199//32+2)]

def planes(stride=64):
    writes=[]
    for cell in range(0,4096,7):
        for layer in range(2):
            writes.append((TILE_CODES+layer*LAYER_STRIDE+cell*4,4,0x5a000000|(layer<<24)|((cell*7+layer*13)&0xffffff)))
            writes.append((ATTR_INDEX+layer*LAYER_STRIDE+cell*4,4,(cell*3+layer)&0xffff))
            writes.append((RUNTIME_ATTR+(layer*4096+cell)*4,4,0xa5000000|cell))
    return writes+[(GRID_STRIDE,4,stride),(GRID_HEIGHT,4,4096//stride)]

def rows(probe):
    return [[probe.word(PATCH_TABLE+i*PATCH_STRIDE+k*4) for k in range(9)] for i in range(PATCH_RECORDS)]

def slot_seed(index,patch,gate,ticks,scratch,frame,flags=0x300010f):
    base=OVERLAY+index*OVERLAY_STRIDE
    return [(base,4,flags),(base+4,4,1),(base+8,4,1),(base+12,4,2),(base+16,4,2),
            (base+24,4,ticks),(base+32,4,patch),(base+36,4,scratch),(base+40,4,frame),(base+44,4,gate)]

def event_flag_groups(probe,table):
    calls=[]
    for flag in FLAG_IDS:
        for entry in (0x497036,0x496fe8,0x497036,0x49700e,0x497036):calls.append((entry,[flag]))
    probe.group('event_flag_bitmap',bitmap(),calls)
    picked=0
    for map_id in sorted({r[0] for r in table}):
        ids=[i for i,r in enumerate(table) if r[0]==map_id]
        if len(ids)<8 or any((table[i][2]+table[i][5])*64+table[i][1]+table[i][4]>4096 for i in ids):continue
        other=next(i for i,r in enumerate(table) if r[0]!=map_id)
        calls=[(0x497065,[i,1]) for i in ids]+[(0x497065,[i,0]) for i in ids]
        calls+=[(0x497065,[other,1]),(0x497065,[other,0]),(0x497065,[-1,1])]
        probe.group(f'map_patch_{map_id}',planes()+bitmap()+[(CURRENT_MAP,4,map_id)],calls)
        picked+=1
        if picked>=3:break
    for map_id in (SAMPLE_MAP,1):
        probe.group(f'resync_{map_id}',planes()+bitmap()+[(CURRENT_MAP,4,map_id)],[(0x49711f,[map_id])])

def patch_object_groups(probe,table):
    """0x486c6f/0x486cf9/0x486d71/0x486dfb over their gate, tick and sound edges."""
    on_map=[i for i,r in enumerate(table) if r[0]==SAMPLE_MAP and (r[2]+r[5])*64+r[1]+r[4]<=4096]
    cued={cue:next((i for i in on_map if table[i][8]==cue),on_map[0]) for cue in (0x14f,0x15b,0x15d,0xffffffff)}
    off_map=next(i for i,r in enumerate(table) if r[0]!=SAMPLE_MAP)
    gates=[-1,-2,-5,7,8]   # 7 and 8 are ids the seeded bitmap sets and clears.
    for entry in (0x486c6f,0x486cf9,0x486d71,0x486dfb):
        for label,patch in list(cued.items())+[('off_map',off_map)]:
            seeds=planes()+bitmap()+[(CURRENT_MAP,4,SAMPLE_MAP),(FRAME_COUNTER,4,1000)]
            calls=[]
            index=0
            for gate in gates:
                for ticks in (0,1,4,8,9,20):
                    for scratch,frame in ((0,0),(3,999),(3,500),(7,999),(1,0)):
                        seeds+=slot_seed(index,patch,gate,ticks,scratch,frame)
                        calls.append((entry,[OVERLAY+index*OVERLAY_STRIDE]))
                        index+=1
            probe.group(f'{entry:x}_{label}',seeds,calls)

def platform_track_groups(probe):
    for entry in (0x488631,0x4886b4):
        seeds=[(ACTIVE_PARTY,4,3)]
        calls=[]
        for index,ticks in enumerate(range(0,12)):
            seeds+=slot_seed(index,40+index,-1,ticks,0,0)
            seeds+=[(ACTORS+(40+index)*ACTOR_STRIDE+0x108,4,0x1234)]
            calls.append((entry,[OVERLAY+index*OVERLAY_STRIDE]))
        probe.group(f'{entry:x}_track',seeds,calls)

def marker_groups(probe):
    """0x486b8c/0x486bfe with the router stubbed to each of its two outcomes."""
    for entry in (0x486b8c,0x486bfe):
        for gate in (0,1):
            for router in (0,1):
                seeds=[(MARKER_GATE,4,gate),(ACTIVE_PARTY,4,5)]
                actor=ACTORS+5*ACTOR_STRIDE
                # Negative and unaligned world positions exercise the two signed
                # divisions the original applies before storing the tile fields.
                seeds+=[(actor+8,4,-0x00123456),(actor+0xc,4,0x00987654),(actor+0x1c,4,0x00010000),
                        (actor+0x104,4,0x7fffffff)]
                seeds+=slot_seed(0,12,-1,4,0,0)
                calls=[(entry,[OVERLAY])]
                seeds2=list(seeds)
                seeds2+=[(actor+8,4,0x00246800),(actor+0xc,4,-0x00300001),(actor+0x1c,4,-0x00020000)]
                seeds2+=slot_seed(1,12,-1,4,0,0)
                probe.group(f'{entry:x}_gate{gate}_router{router}',seeds,calls,router=router)
                probe.group(f'{entry:x}_gate{gate}_router{router}_negative',seeds2,
                            [(entry,[OVERLAY+OVERLAY_STRIDE])],router=router)

def block_switch_groups(probe):
    """0x487122 across both switch states, a busy block and an empty lift count."""
    for raised in (0,1):
        for lifted in (0,1,3):
            for busy in (0,1):
                seeds=[(LAYER_WIDTH,4,64),(GRID_STRIDE,4,64),(GRID_HEIGHT,4,64),
                       (SWITCH_BITS,4,0xffffffff if raised else 0),(LIFTED_BLOCKS,4,lifted)]
                for cell in range(0,4096,3):seeds.append((TILE_CODES+cell*4,4,0x100+cell))
                calls=[]
                for index in range(6):
                    linked=30+index
                    seeds+=slot_seed(index,index,-1,0,linked,0)
                    base=OVERLAY+index*OVERLAY_STRIDE
                    seeds+=[(base+4,4,3+index),(base+8,4,5+index)]
                    actor=ACTORS+linked*ACTOR_STRIDE
                    seeds+=[(actor+0x104,4,busy),(actor+0x108,4,9),(actor+0x128,4,0x1234),(actor+0x12c,4,0x5678),
                            (actor+0x3c,4,0xaaaaaaaa),(actor+0x40,4,0xbbbbbbbb),(actor+0x44,4,0xcccccccc)]
                    calls.append((0x487122,[base]))
                probe.group(f'block_switch_raised{raised}_lifted{lifted}_busy{busy}',seeds,calls)

def step_and_vitals_groups(probe):
    """0x4870e2 word writes and the 0x486b53 party vitals refill."""
    seeds=[];calls=[]
    for index in range(4):
        actor=ACTORS+(60+index)*ACTOR_STRIDE
        seeds+=[(actor+0x128,4,0x11110000|(index*7)),(actor+0x12c,4,0x2222f000|(index*5)),
                (actor+0x104,4,0x55555555),(actor+0x108,4,0x66666666),
                (actor+0x3c,4,0xdeadbeef),(actor+0x40,4,0xfeedface),(actor+0x44,4,0x0badc0de)]
        calls.append((0x4870e2,[actor,0x9abc0000|index,0xdef00000|index]))
    probe.group('step_target',seeds,calls)
    for count in (0,1,5):
        seeds=[(PARTY_COUNT,4,count)]
        for member in range(6):
            seeds.append((PARTY_IDS+member*4,4,member*3+1))
            record=PARTY_STATS+(member*3+1)*188
            seeds+=[(record+24,4,300+member),(record+28,4,7),(record+32,4,40+member),(record+36,4,1)]
        probe.group(f'party_vitals_{count}',seeds,[(0x486b53,[])])

def installer_groups(probe):
    """Every recovered straight-line installer, from a marked but empty table.

    The table is pre-filled with a pattern that leaves the registered bit clear,
    so the comparison also shows which record words the installer leaves alone
    and which free slot the original registrar picks.
    """
    entries=sorted(int(a,16) for a in json.loads(Path(SETUP_TABLES).read_text())['accepted'])
    pattern=[(CURRENT_RECORD,4,0xfeedface)]
    for index,base in enumerate(range(OVERLAY,OVERLAY_END,OVERLAY_STRIDE)):
        for word in range(OVERLAY_STRIDE//4):
            pattern.append((base+word*4,4,(0x00abc000|index) if word==0 else (0xa5000000|(index<<8)|word)))
    for entry in entries:
        probe.group(f'installer_{entry:x}',pattern,[(entry,[])])

def actor_object_groups(probe):
    """0x4874a7/0x4897a3/0x489a60/0x489b53/0x48897a/0x488aa7/0x4870c6.

    The party actor drives every one of these, so each group pins the party's
    state, tile and world position and then runs the callback on its own object.
    """
    party_index=5
    party=ACTORS+party_index*ACTOR_STRIDE
    def party_seed(state,frame,facing,tile_x,tile_y,layer):
        return [(ACTIVE_PARTY,4,party_index),(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64),
                (GRID_STRIDE,4,64),(GRID_HEIGHT,4,64),(RNG_STATE,4,0x1234abcd),
                (party+0x104,4,state),(party+0x108,4,frame),(party+0x110,4,facing),
                (party+8,4,0x00123400),(party+0xc,4,0xffe70000),(party+0x10,4,0x00058000),
                (party+0x128,4,tile_x),(party+0x12c,4,tile_y),(party+0x1c,4,layer*0x10000)]
    def object_seed(index,map_id,state,motion,frame):
        base=ACTORS+(70+index)*ACTOR_STRIDE
        return base,[(base,4,70+index),(base+4,4,0x10040),(base+0x14c,4,state),(base+0x160,4,map_id),
                     (base+0x104,4,motion),(base+0x108,4,frame),(base+0x110,4,0x77777777),
                     (base+8,4,0xaaaa0000),(base+0xc,4,0xbbbb0000),(base+0x10,4,0xcccc0000),
                     (base+0x14,4,0x0004c000),(base+0x18,4,0x0007a000),
                     (base+0x128,4,0xdddd),(base+0x12c,4,0xeeee),(base+0x138,4,0x99999999)]
    for entry in (0x4874a7,0x489a60,0x489b53,0x48897a,0x488aa7):
        for state,frame,facing in ((0,1,0),(0xa,1,2),(0xb,1,3),(0xb,7,1),(0,4,0)):
            for on_belt in (0,1):
                seeds=party_seed(state,frame,facing,9,11,1)
                seeds+=[(GATE_FRAMES+i*4,4,0x40+i*0x10) for i in range(8)]
                cell=11*64+9+(1<<12)
                seeds.append((RUNTIME_ATTR_PLANE+cell*4,4,0x10000 if on_belt else 0x8000))
                calls=[]
                for index,(map_id,object_state) in enumerate(
                        [(SAMPLE_MAP,0),(SAMPLE_MAP,-1),(SAMPLE_MAP,1),(SAMPLE_MAP+1,0),(SAMPLE_MAP,-2)]):
                    base,extra=object_seed(index,map_id,object_state,0,3)
                    seeds+=extra;calls.append((entry,[base]))
                probe.group(f'{entry:x}_s{state}_f{frame}_d{facing}_belt{on_belt}',seeds,calls)
    for motion in (0,1):
        for start in (0,3,8):
            seeds=party_seed(0,1,0,9,11,1)
            calls=[]
            for index,switch in enumerate(range(4)):
                base,extra=object_seed(index,SAMPLE_MAP,0,motion,start)
                extra+=[(base+0x3c,2,switch),(base+0x3e,2,3+index),(base+0x40,2,5+index),
                        (base+0x42,2,9+index),(base+0x44,2,12+index)]
                seeds+=extra;calls.append((0x4897a3,[base]))
            probe.group(f'4897a3_m{motion}_t{start}',seeds,calls)
    seeds=[];calls=[]
    for index in range(4):
        seeds+=slot_seed(index,index,-1,7,0,0)
        base=OVERLAY+index*OVERLAY_STRIDE
        seeds+=[(base+4,4,11+index),(base+8,4,17+index)]
        calls.append((0x4870c6,[base]))
    probe.group('position_event',seeds,calls)

def worldmap_groups(probe):
    """The world map city/route helpers over a seeded table and its terminator."""
    def table(rows):
        seeds=[(WORLDMAP_MODE,4,0xdeadbeef),(GAME_MODE,4,3),
               (0x77149c,4,0x11111111),(0x7714a0,4,0x22222222)]
        for slot in range(rows+2):
            base=CITY_TABLE+slot*CITY_STRIDE
            present=1 if slot<rows else 0
            seeds+=[(base,4,0x1f0|slot),(base+4,4,present),
                    (base+8,4,100+slot),(base+12,4,slot),
                    (base+16,4,200+slot),(base+20,4,slot*2)]
        return seeds
    for rows in (0,1,5):
        seeds=table(rows)
        calls=[]
        for slot in range(3):
            calls+=[(0x435fac,[slot,700+slot,slot]),(0x435fca,[slot,1]),(0x435fca,[slot,0]),
                    (0x436060,[slot]),(0x4360be,[slot])]
            for state in (0,1,2):calls.append((0x435ffa,[slot,state]))
        calls+=[(0x4360be,[0x2c]),(0x4360be,[0x2b])]
        for map_id,entry in ((100,0),(200,0),(104,4),(204,8),(999,9)):
            calls.append((0x436077,[map_id,entry]))
        calls+=[(0x436100,[m]) for m in (-1,0,7,14,15,100)]
        calls.append((0x43700a,[]))
        calls.append((0x4384a0,[0x1234]))
        probe.group(f'worldmap_rows{rows}',seeds,calls)
    # Route music rows come from the original data; probe hits and the fallback.
    calls=[]
    for row in range(6):
        first=probe.word(ROUTE_MUSIC+row*0xc);second=probe.word(ROUTE_MUSIC+row*0xc+4)
        calls.append((0x43611c,[first,second]))
    calls+=[(0x43611c,[0xdead,0xbeef]),(0x43611c,[0,0])]
    probe.group('worldmap_route_music',[],calls)

def patch_handler_groups(probe,table):
    """The patch/gate handlers and the map-specific animation lookup scripts."""
    on_map=[i for i,r in enumerate(table) if r[0]==SAMPLE_MAP and (r[2]+r[5])*64+r[1]+r[4]<=4096]
    base=planes()+[(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64)]
    # Seed the whole animation lookup so an untouched cell is visible.
    base+=[(ANIM_LOOKUP+i*4,4,0x3c000000|i) for i in range(0,4096*3,5)]
    for pattern in (0,1,2):
        bits=[(FLAG_BITS+i*4,4,{0:0,1:0xffffffff}.get(pattern,(0x9e3779b9*(i+1))&0xffffffff))
              for i in range(0x199//32+2)]
        seeds=base+bits
        calls=[]
        index=0
        for entry in (0x493e8e,0x48bd7d,0x491b53,0x494066,0x49356f,0x48bea6):
            for patch in on_map[:3]:
                for gate in (-1,-2,7,0x11d,0x5c,0xd7):
                    seeds+=slot_seed(index,patch,gate,4,on_map[1],0)
                    calls.append((entry,[OVERLAY+index*OVERLAY_STRIDE]))
                    index+=1
        probe.group(f'patch_gate_handlers_{pattern}',seeds,calls)
        probe.group(f'anim_lookup_scripts_{pattern}',seeds,
                    [(e,[]) for e in (0x48c256,0x491d2b,0x496650,0x49667f)])
    for present in (0xe6,0x11,0xffffffff):
        seeds=[(EVENT_ENTRY,4,present),(EVENT_ENTRY+4,4,0xe6),(0x607cf4,4,0x1234),(0x8071c8,4,0x5678)]
        probe.group(f'event_entry_release_{present:x}',seeds,[(0x492a85,[])])
    for flags in (0x101ce,0x1014e):
        actor=ACTORS+5*ACTOR_STRIDE
        probe.group(f'switch_hold_{flags:x}',[(ACTIVE_PARTY,4,5),(actor+4,4,flags),(SWITCH_HOLD,4,0)],
                    [(0x494352,[0])])
    probe.group('route_screen_release',[(ROUTE_SCREEN,4,2)],[(0x438493,[0])])

def spawn_groups(probe):
    """The spawners, each from an empty object pool and a marked overlay table."""
    def pool(free_slots):
        seeds=[(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64),(GRID_STRIDE,4,64),(GRID_HEIGHT,4,64),
               (GATE_CROSSING,4,0x1234),(0x806618,4,0xfeedface)]
        # Mark every pool slot busy, then free the first few so the allocator has
        # a deterministic answer and the scan itself is part of the comparison.
        for slot in range(FREE_POOL_FIRST,FREE_POOL_FIRST+40):
            base=ACTORS+slot*ACTOR_STRIDE
            free=slot<FREE_POOL_FIRST+free_slots
            seeds+=[(base,4,slot),(base+4,4,0 if free else 0x40),(base+0x148,4,0),(base+0x14c,4,0),
                    (base+0x144,4,0x777),(base+0x108,4,0x888),(base+0x10c,4,0x999),
                    (base+0x110,4,0xaaa),(base+0x104,4,0xbbb),(base+0x138,4,0xccc),
                    (base+0x130,4,0xddd),(base+0x134,4,0xeee),(base+0x160,4,0xfff),
                    (base+0x1c,4,0x111),(base+0xc,4,0x222),(base+0x3c,4,0x33333333)]
        for index,base in enumerate(range(OVERLAY,OVERLAY_END,OVERLAY_STRIDE)):
            for word in range(OVERLAY_STRIDE//4):
                seeds.append((base+word*4,4,(0x00abc000|index) if word==0 else (0xa5000000|(index<<8)|word)))
        for cell in range(0,4096,3):seeds.append((TILE_CODES+cell*4,4,0x100+cell))
        for cell in range(0,4096*2,5):seeds.append((RUNTIME_ATTR_PLANE+cell*4,4,0x30000|cell))
        return seeds
    for raised in (0,1):
        base=pool(20)+[(SWITCH_BITS,4,0xffffffff if raised else 0),(LIFTED_BLOCKS,4,2)]
        probe.group(f'spawn_pushable_{raised}',base,[(0x4898d3,[4,6,0]),(0x4898d3,[7,9,1])])
        probe.group(f'spawn_pairs_{raised}',base,[(0x488bb6,[]),(0x489c47,[6,8,0])])
        probe.group(f'spawn_platform_{raised}',base,[(0x48880e,[])])
        probe.group(f'spawn_switch_{raised}',base,[(0x489929,[n]) for n in range(4)])
        probe.group(f'spawn_switch_range_{raised}',base,[(0x4899f6,[])])
        probe.group(f'spawn_switch_range_hi_{raised}',base,[(0x489a18,[]),(0x489a3c,[])])
        probe.group(f'spawn_gate_crossings_{raised}',base,
                    [(e,[]) for e in (0x489d3c,0x489e7f,0x489ed7,0x489ef5)])

def late_object_groups(probe,table):
    """0x488737, 0x48745b, 0x4874df and 0x486eb7."""
    party_index=5;party=ACTORS+party_index*ACTOR_STRIDE
    base=[(ACTIVE_PARTY,4,party_index),(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64),
          (GRID_STRIDE,4,64),(GRID_HEIGHT,4,64),
          (party+8,4,0x00123400),(party+0xc,4,0xffe70000),(party+0x10,4,0x00058000),
          (party+0x110,4,2)]
    for frame in (-1,-5,0,3,7):
        seeds=list(base);calls=[]
        for index,(map_id,state) in enumerate([(SAMPLE_MAP,0),(SAMPLE_MAP+1,0),(SAMPLE_MAP,-1)]):
            obj=ACTORS+(70+index)*ACTOR_STRIDE
            seeds+=[(obj,4,70+index),(obj+4,4,0x10040),(obj+0x14c,4,state),(obj+0x160,4,map_id),
                    (obj+0x108,4,frame),(obj+0x110,4,0x7777),(obj+0x138,4,0x8888),
                    (obj+8,4,0xaaaa0000),(obj+0xc,4,0xbbbb0000),(obj+0x10,4,0xcccc0000),
                    (obj+0x14,4,0x0004c000),(obj+0x18,4,0x0007a000),
                    (obj+0x128,4,0xdddd),(obj+0x12c,4,0xeeee),(obj+0x148,4,0)]
            calls.append((0x488737,[obj]))
        probe.group(f'platform_tick_{frame}',seeds,calls)
    # A timed chest fires once its counter passes 0x64.
    for elapsed in (0,0x63,0x64,0x65,0x200):
        seeds=[(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64),(GRID_STRIDE,4,64),(GRID_HEIGHT,4,64)]
        calls=[]
        for index in range(3):
            seeds+=slot_seed(index,0x00060004|(index<<20),-1,3,0x00020001,0x0007002a)
            b=OVERLAY+index*OVERLAY_STRIDE
            seeds+=[(b+44,4,elapsed)]
            calls.append((0x48745b,[b]))
        probe.group(f'chest_timer_{elapsed:x}',seeds,calls)
    # Marker spawn clamps its script index into the table.
    pool=[(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64),(GRID_STRIDE,4,64),(GRID_HEIGHT,4,64)]
    for slot in range(FREE_POOL_FIRST,FREE_POOL_FIRST+20):
        b=ACTORS+slot*ACTOR_STRIDE
        pool+=[(b,4,slot),(b+4,4,0),(b+0x148,4,0),(b+0x14c,4,0),(b+0x144,4,0x777)]
    probe.group('marker_spawn',pool,[(0x4874df,[3,4,9,1,s]) for s in (-4,0,3,7,8,40)])
    # The region clear walks the patch rectangle and rewrites the water cells.
    on_map=[i for i,r in enumerate(table) if r[0]==SAMPLE_MAP and (r[2]+r[5])*64+r[1]+r[4]<=4096]
    seeds=planes()+[(CURRENT_MAP,4,SAMPLE_MAP),(LAYER_WIDTH,4,64)]
    seeds+=[(ANIM_LOOKUP+i*4,4,[0x32,0x33,0x34,0x35][(i//3)%4] if i%3==0 else 0x5c000000|i)
            for i in range(0,4096*3,1)]
    calls=[]
    for index,patch in enumerate(on_map[:4]):
        seeds+=slot_seed(index,patch,1,0,0,0)
        calls.append((0x486eb7,[OVERLAY+index*OVERLAY_STRIDE]))
    seeds+=slot_seed(4,on_map[0],0,0,0,0)
    calls.append((0x486eb7,[OVERLAY+4*OVERLAY_STRIDE]))
    probe.group('region_clear',seeds,calls)

def main(executable,output):
    probe=Probe(executable);table=rows(probe)
    event_flag_groups(probe,table)
    patch_object_groups(probe,table)
    platform_track_groups(probe)
    block_switch_groups(probe)
    step_and_vitals_groups(probe)
    marker_groups(probe)
    actor_object_groups(probe)
    worldmap_groups(probe)
    patch_handler_groups(probe,table)
    spawn_groups(probe)
    late_object_groups(probe,table)
    installer_groups(probe)
    probe.write(output,['0x496fe8','0x49700e','0x497036','0x497065','0x49711f',
                        '0x486b8c','0x486bfe','0x486c6f','0x486cf9','0x486d71','0x486dfb',
                        '0x488631','0x4886b4','0x487122','0x4870e2','0x486b53','0x4874a7','0x4897a3','0x489a60',
                        '0x489b53','0x48897a','0x488aa7','0x4870c6','0x435fac','0x435fca','0x435ffa',
                        '0x436060','0x436077','0x4360be','0x436100','0x43611c','0x43700a','0x4384a0']
                        +sorted(json.loads(Path(SETUP_TABLES).read_text())['accepted']))
if __name__=='__main__':main(*sys.argv[1:])
