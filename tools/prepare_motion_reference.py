#!/usr/bin/env python3
"""Execute original, self-contained x86 motion routines to produce test evidence.

Requires unicorn. Reads the EXE without modifying it. No imported Windows calls
are intercepted or replaced: the tested timer uses its internal progress lane.
This is an instruction-level oracle, not a recording of a complete game run.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG

def main(exe_path, output_path):
    data = Path(exe_path).read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':
        raise ValueError('reference requires the inspected FLYINGSB.EXE')
    pe = struct.unpack_from('<I', data, 60)[0]
    section = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, 0x600000)
    machine.mem_map(0x1000000, 0x10000)
    for index in range(struct.unpack_from('<H', data, pe + 6)[0]):
        _, address, size, offset = struct.unpack_from('<IIII', data, section + index * 40 + 8)
        machine.mem_write(0x400000 + address, data[offset:offset + size])
    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))
    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]
    def call(address, *args):
        # Original CRT __fpmath -> __setdefaultprecision selects PC53, nearest.
        machine.reg_write(UC_X86_REG_FPCW, 0x27f)
        machine.reg_write(UC_X86_REG_FPTAG, 0xffff)
        machine.reg_write(UC_X86_REG_FPSW, 0)
        machine.reg_write(UC_X86_REG_ESP, 0x100f000)
        write(0x100f000, 0x1000000, *args)
        machine.emu_start(address, 0x1000000, count=2000)
        if machine.reg_read(unicorn.x86_const.UC_X86_REG_EIP) != 0x1000000:
            raise RuntimeError('instruction watchdog without function return')
        return machine.reg_read(UC_X86_REG_EAX)
    output = bytearray(b'FSBMOT1\0')
    for phase in range(4096):
        output.extend(struct.pack('<II', call(0x408de1, phase * 16), call(0x408e69, phase * 16)))
    obj, actor, timer = 0x1001000, 0x1002000, 0x1003000
    fields = {0xfc: actor, 0x168: 7 * 0x400000, 0x16c: -3 * 0x300000,
              0x170: -17, 0x174: 23, 0x14c: 0xc000, 0x150: 0x8000,
              0x178: 0x1234567, 0x17c: 0xfffedcbb, 0x19c: 0, 0x1a4: timer}
    for offset, value in fields.items():
        write(obj + offset, value)
    for progress in range(30031):
        write(obj + 0x20, 1)
        write(timer, 0, 30030 - progress, 0, 0, 0, 0, 1)
        call(0x431273, obj)
        values = [read(actor + off) for off in (8, 12, 0x14, 0x18, 0x128, 0x12c)]
        values.extend(read(obj + off) for off in (0x158, 0x15c))
        output.extend(struct.pack('<8I', *values))
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)
    report = {'exe_sha256': sha, 'generator': 'Unicorn ' + unicorn.__version__, 'fpu_control_word': '0x027f',
              'trig_phase_cases': 4096, 'tile_tween_progress_cases': 30031,
              'executed_functions': ['0x408de1', '0x408e69', '0x431273 and its actual internal callees'],
              'timer_fixture': 'direct remaining-progress lane, zero step; no OS time import',
              'scope': 'isolated original instruction execution, not full-scene or input-timing proof',
              'binary_sha256': hashlib.sha256(output).hexdigest()}
    path.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: prepare_motion_reference.py FLYINGSB.EXE OUTPUT.bin')
    main(*sys.argv[1:])
