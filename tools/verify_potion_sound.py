#!/usr/bin/env python3
"""Observe item301's original apply/cue block; does not run the encounter."""
import hashlib
import json
import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP
from prepare_event0 import PE


def main(executable, output):
    pe = PE(Path(executable))
    digest = hashlib.sha256(pe.data).hexdigest()
    assert digest == '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x400000, 0x600000)
    cpu.mem_map(0x1000000, 0x10000)
    for _, va, size, offset in pe.sections:
        cpu.mem_write(pe.base + va, pe.data[offset:offset + size])

    def put(address, value):
        cpu.mem_write(address, struct.pack('<I', value))

    def get(address):
        return struct.unpack('<I', cpu.mem_read(address, 4))[0]

    # Enter the real menu block at its CALL to apply_consumable_item_effect.
    # Menu input/layout is outside this fixture. Game apply code is not stubbed.
    put(0x607a20, 1000)
    put(0x607a24, 1)
    put(0x806e30 + 301 * 4, 1)
    stack = 0x100f000
    put(stack, 0)  # actor index
    put(stack + 4, 301)
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cues = []

    def observe_sound(machine, address, size, opaque):
        cues.append(get(machine.reg_read(UC_X86_REG_ESP) + 4))
        machine.emu_stop()  # Observe before DirectSound; no fake playback.

    cpu.hook_add(UC_HOOK_CODE, observe_sound, begin=0x435373, end=0x435373)
    cpu.emu_start(0x43e412, 0x43e429, count=10000)
    assert cues == [203]
    assert get(0x607a24) == 1000 and get(0x806e30 + 301 * 4) == 0
    result = {
        'exe_sha256': digest,
        'scope': 'original item301 apply and successful field-menu cue branch; not full encounter or device playback',
        'entry': '0x43e412', 'apply_function': '0x43d229',
        'cue_call_bytes': bytes(cpu.mem_read(0x43e41f, 10)).hex(),
        'sound_entry': '0x435373', 'observed_cues': cues,
        'item_id': 301, 'hp_before': 1, 'hp_after': get(0x607a24),
        'quantity_before': 1, 'quantity_after': get(0x806e30 + 301 * 4),
        'scene_pc': '0x6261ed',
        'stock_scene_bytes': bytes(cpu.mem_read(0x6261ed, 9)).hex(),
        'stock_scene_cue': get(0x6261f2),
    }
    Path(output).write_text(json.dumps(result, indent=2) + '\n')
    print('original item301: HP 1->1000, quantity 1->0, cue203')


if __name__ == '__main__':
    main(*sys.argv[1:])
