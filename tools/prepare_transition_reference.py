#!/usr/bin/env python3
"""Record radial strip geometry from the actual402569 x86 callback.

The timer helper supplies explicit progress for this isolated geometry fixture.
DirectDraw calls are recorded, not executed. No runtime assets or game waits
are replaced by these fixtures. Requires Unicorn; source EXE is read-only.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX, UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG
from prepare_event0 import PE


def main(exe_path, output):
    pe = PE(exe_path)
    sha = hashlib.sha256(pe.data).hexdigest()
    if sha != '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':
        raise ValueError('wrong original executable')
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, 0x600000)
    machine.mem_map(0x1000000, 0x10000)
    for _, va, size, offset in pe.sections:
        machine.mem_write(pe.base + va, pe.data[offset:offset+size])
    current = {'progress': 0, 'strips': []}

    def words(at, count):
        return struct.unpack('<' + 'I' * count, machine.mem_read(at, count*4))

    def write(at, values):
        machine.mem_write(at, struct.pack('<' + 'I' * len(values), *(x & 0xffffffff for x in values)))

    def host_call(machine, address, size, opaque):
        stack = machine.reg_read(UC_X86_REG_ESP)
        counts = {0x401f3a: 0, 0x405c8c: 3, 0x405db3: 6, 0x405f23: 5}
        arguments = words(stack+4, counts[address])
        if address == 0x405db3:
            current['strips'].append(words(arguments[4], 4))
        machine.reg_write(UC_X86_REG_EAX, current['progress'] if address == 0x401f3a else 0)
        machine.reg_write(UC_X86_REG_EIP, words(stack, 1)[0])
        machine.reg_write(UC_X86_REG_ESP, stack+4+counts[address]*4)

    for address in [0x401f3a, 0x405c8c, 0x405db3, 0x405f23]:
        machine.hook_add(UC_HOOK_CODE, host_call, begin=address, end=address)
    cases = []
    for top, height in [(7, 466), (15, 450)]:
        view = [0, top, 640, top+height]
        for ax, ay in [(80, 150), (320, 225), (650, 450)]:
            for mode in [0, 1]:
                dx, dy = (640, height) if mode == 0 else (max(ax, 640-ax), max(ay-top, top+height-ay))
                radius = math.sqrt(dx*dx+dy*dy)
                for progress in [1, 7507, 15015, 22522, 30030]:
                    write(0x6da51c, view)
                    write(0x6da52c, [640, height])
                    write(0x6da564, [0x1002000])
                    write(0x1002000, [ax, ay])
                    machine.mem_write(0x6e0e00, struct.pack('<d', radius))
                    current.update(progress=progress, strips=[])
                    machine.reg_write(UC_X86_REG_FPCW, 0x27f)
                    machine.reg_write(UC_X86_REG_FPSW, 0)
                    machine.reg_write(UC_X86_REG_FPTAG, 0xffff)
                    machine.reg_write(UC_X86_REG_ESP, 0x100f000)
                    write(0x100f000, [0x1000000])
                    machine.emu_start(0x402569, 0x1000000, count=1000000)
                    if machine.reg_read(UC_X86_REG_EIP) != 0x1000000:
                        raise RuntimeError('original radial callback did not return')
                    cases.append((view+[ax, ay, mode, progress], current['strips']))
    data = bytearray(b'FSBRAD1\0') + struct.pack('<I', len(cases))
    for header, strips in cases:
        data.extend(struct.pack('<9I', *header, len(strips)))
        for strip in strips:
            data.extend(struct.pack('<4I', *strip))
    output = Path(output)
    output.write_bytes(data)
    report = {'exe_sha256': sha, 'generator': 'Unicorn '+unicorn.__version__,
              'routine': '0x402569 and actual CRT math callees', 'fpu_control_word': '0x027f',
              'scope': 'geometry only: explicit progress replaces401f3a, DirectDraw calls are recorded',
              'cases': len(cases), 'strip_rectangles': sum(len(s) for h, s in cases),
              'math_correction': 'actual498504 is asin; the cached acos label is wrong',
              'sha256': hashlib.sha256(data).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: prepare_transition_reference.py FLYINGSB.EXE OUTPUT.bin')
    main(*sys.argv[1:])
