#!/usr/bin/env python3
"""Execute original field motion/frame routines, recording sound requests only."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from prepare_event0 import PE
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP


def main(executable, output):
    pe = PE(executable)
    sha = hashlib.sha256(pe.data).hexdigest()
    if sha != '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':
        raise ValueError('reference requires inspected executable')
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, 0x600000)
    machine.mem_map(0x1000000, 0x10000)
    for _, address, size, offset in pe.sections:
        machine.mem_write(pe.base+address, pe.data[offset:offset+size])
    def write(address, *values):
        machine.mem_write(address, struct.pack('<'+'I'*len(values), *(v & 0xffffffff for v in values)))
    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]
    cues = []
    def hook(uc, address, size, user):
        if address == 0x435373:
            stack = uc.reg_read(UC_X86_REG_ESP)
            cues.append(read(stack+4))
            uc.reg_write(UC_X86_REG_EIP, read(stack))
            uc.reg_write(UC_X86_REG_ESP, stack+8)
            uc.reg_write(UC_X86_REG_EAX, 0)
    machine.hook_add(UC_HOOK_CODE, hook)
    records = bytearray()
    count = [0, 0]
    obj = 0x8073d8
    for kind, function, states in [(0, 0x45c55c, [0,1,2,3,5,7,12,13,14,15,16]),
                                    (1, 0x45cc1b, [0,1,2,3,4,5,7,8,12,13,14,15,16])]:
        for state in states:
            limit = {1:8,2:4,3:16,4:8,7:8,8:16,13:16,14:32,15:64,16:128}.get(state, 8)
            for facing in range(4 if kind == 1 and state == 7 else 8):
                for frame in sorted({0,1,limit//2,limit-1}):
                    for variant in range(3 if kind else 1):
                        initial = bytearray(428)
                        values = {0:0,4:0x10140|(variant<<4),8:123,12:456,16:0x200000,
                                  0x14:0x38000,0x18:0x68000,0x1c:0x8000,
                                  0x2c:1,0x30:2,0x34:4,0x38:3,0x3c:3,
                                  0x104:state,0x108:frame,0x10c:1,0x110:facing,0x114:3,
                                  0x130:8,0x134:0xabcdef,0x138:0x123456}
                        for offset, value in values.items():
                            struct.pack_into('<I', initial, offset, value)
                        machine.mem_write(obj, bytes(initial))
                        write(0x803a1c, 0);write(0x5d2258, 3)
                        machine.mem_write(0x607a12+3*0xbc, b'\x20')
                        write(0x804aac, 7);write(0x77ece0, 0xabc);write(0x80465c, 3);write(0x6da2d4, 3)
                        write(0x100f000, 0x1000000, obj);machine.reg_write(UC_X86_REG_ESP, 0x100f000)
                        cues.clear();machine.emu_start(function, 0x1000000, count=5000)
                        if machine.reg_read(UC_X86_REG_EIP) != 0x1000000:
                            raise RuntimeError('original function did not return')
                        if len(cues)>1:
                            raise RuntimeError('unexpected multiple sound requests')
                        records += struct.pack('<I', kind) + initial + bytes(machine.mem_read(obj, 428))
                        records += struct.pack('<4I', machine.reg_read(UC_X86_REG_EAX)&255,
                                               read(0x804aac), read(0x77ece0), cues[0] if cues else 0xffffffff)
                        count[kind] += 1
    data = b'FSBFLD1\0'+struct.pack('<I', sum(count))+records
    path = Path(output);path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(data)
    report = {'exe_sha256':sha,'stepper_cases':count[0],'frame_cases':count[1],
              'functions':['0x45c55c','0x45cc1b','actual 0x45d82d callee'],
              'sound_hook':'0x435373: record cue argument and return; no audio device execution',
              'scope':'field-mode actor-local routines; not field input, NPC AI or combat proof',
              'fixture_sha256':hashlib.sha256(data).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n');print(json.dumps(report))


if __name__ == '__main__':
    main(*sys.argv[1:])
