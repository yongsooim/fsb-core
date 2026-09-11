#!/usr/bin/env python3
"""Run every D_EFFECTS skill callback in the original and record what it does.

These entry points handle a few presentation phases of the sequence object and
otherwise hand it, plus one or two constants, to a shared battle action routine.
Both the action routines and the sound cue service belong to other scopes, so
they are stopped at their entry: the fixture records which one was reached, the
arguments the original actually pushed, the value the callback returns, and the
whole comparison window of .data.

Expected values come from the original binary only. Nothing here executes or
consults the C++ reconstruction.
"""
import hashlib
import json
import struct
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2]))
sys.path.insert(0, str(HERE.parent))
from prepare_event0 import PE  # noqa: E402
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX  # noqa: E402

SOURCE_SHA = '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
DATA_BASE, DATA_BYTES = 0x4a5000, 3882100
STACK, RETURN_MARKER = 0x100f000, 0x1000000
ACTIVE_ACTOR_POINTER = 0x8059f0
FACING_FIELD, PHASE_FIELD = 0x110, 0x14c
PLAY_CUE = 0x435373
# Distinct EAX per stopped routine, so a wrong target cannot look right.
RESULT_BASE = 0x5eed0000
# Scratch objects, past the actor slots the fixtures use.
SCRATCH_ACTOR = 0x8073d8 + 700 * 428
SCRATCH_SEQUENCE = 0x8073d8 + 701 * 428


def main(executable, table_path, output):
    phase_arrays = {}
    for declaration in Path(table_path).with_name("skill_callback_phases.inc").read_text().splitlines():
        found = re.search(r"PhaseAction (phases_[0-9a-f]+)\[\]", declaration)
        if found: phase_arrays[found[1]] = [int(v) for v in re.findall(r"\{(-?\d+), PhaseAction::", declaration)]
    rows = []
    for line in Path(table_path).read_text().splitlines():
        if not line.startswith('{0x'):
            continue
        entry = int(line.split(',', 1)[0].lstrip('{'), 16)
        phase_name = re.search(r'phases_[0-9a-f]+', line)
        phases = phase_arrays[phase_name[0]] if phase_name else []
        indexed = 'ActiveFacingTable' in line
        rows.append((entry, phases, indexed))
    rows.sort()

    pe = PE(executable)
    sha = hashlib.sha256(pe.data).hexdigest()
    assert sha == SOURCE_SHA, sha
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x400000, 0x600000)
    uc.mem_map(0x1000000, 65536)
    for _, va, size, offset in pe.sections:
        uc.mem_write(pe.base + va, pe.data[offset:offset + size])
    pristine = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))

    # Every routine the table forwards to, and how many arguments it is passed.
    argument_count = {PLAY_CUE: 1}
    for line in Path(table_path).read_text().splitlines():
        if not line.startswith('{0x'):
            continue
        fields = line.split(',')
        routine = int(fields[1], 16)
        pushed = 1 + line.count('Kind::Constant') + line.count('Kind::ActiveFacingTable')
        argument_count[routine] = max(argument_count.get(routine, 0), pushed)

    reached = []

    def stop(instance, address, size, _user):
        sp = instance.reg_read(UC_X86_REG_ESP)
        count = argument_count[address]
        words = struct.unpack_from('<' + 'I' * count, instance.mem_read(sp + 4, 4 * count))
        reached.append((address, list(words)))
        instance.reg_write(UC_X86_REG_EAX, RESULT_BASE | (address & 0xffff))
        instance.reg_write(UC_X86_REG_EIP, struct.unpack('<I', instance.mem_read(sp, 4))[0])
        instance.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * count)

    for target in sorted(argument_count):
        uc.hook_add(UC_HOOK_CODE, stop, begin=target, end=target)

    records = bytearray()
    cases = 0
    for entry, phases, indexed in rows:
        # Each row's own phases, plus values that must miss every branch.
        probes = sorted(set(phases) | {0, 1, -1, -999, -1006})
        for facing in (range(8) if indexed else (0,)):
            object_probes = [(SCRATCH_SEQUENCE, phase) for phase in probes]
            if not phases: object_probes += [(0, 0), (0x2000000, 0)]
            for sequence, phase in object_probes:
                setup = [(ACTIVE_ACTOR_POINTER, SCRATCH_ACTOR),
                         (SCRATCH_ACTOR + FACING_FIELD, facing),
                         (SCRATCH_SEQUENCE + PHASE_FIELD, phase & 0xffffffff)]
                uc.mem_write(DATA_BASE, pristine)
                for at, value in setup:
                    uc.mem_write(at, struct.pack('<I', value))
                before = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))
                reached.clear()
                uc.reg_write(UC_X86_REG_ESP, STACK)
                uc.reg_write(UC_X86_REG_EAX, 0)
                uc.mem_write(STACK, struct.pack('<II', RETURN_MARKER, sequence))
                uc.emu_start(entry, RETURN_MARKER, count=1000000)
                assert uc.reg_read(UC_X86_REG_EIP) == RETURN_MARKER, hex(entry)
                assert uc.reg_read(UC_X86_REG_ESP) == STACK + 8, hex(entry)
                after = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))
                changes = [(DATA_BASE + i, after[i]) for i in range(DATA_BYTES)
                           if before[i] != after[i]] if before != after else []
                records += struct.pack('<4I', entry, sequence, phase & 0xffffffff,
                                       uc.reg_read(UC_X86_REG_EAX))
                records += struct.pack('<I', len(reached))
                for routine, words in reached:
                    records += struct.pack('<II', routine, len(words))
                    records += b''.join(struct.pack('<I', w) for w in words)
                records += struct.pack('<I', len(setup))
                records += b''.join(struct.pack('<II', a, v & 0xffffffff) for a, v in setup)
                records += struct.pack('<I', len(changes))
                records += b''.join(struct.pack('<IB', a, v) for a, v in changes)
                cases += 1

    data = b'FSBDSK2\0' + struct.pack('<I', cases) + bytes(records)
    out = Path(output)
    out.write_bytes(data)
    out.with_suffix('.json').write_text(json.dumps({
        'source_sha256': sha,
        'cases': cases,
        'entry_points': len(rows),
        'entry_points_with_cue_phases': sum(1 for _, p, _ in rows if p),
        'fixture_sha256': hashlib.sha256(data).hexdigest(),
        'compared_data_bytes': DATA_BYTES,
        'stopped_calls': {hex(a): f'record {n} pushed arguments; set EAX marker; RET {4 * n}'
                          for a, n in sorted(argument_count.items())},
        'scope': 'Which cue phase each skill callback recognises, the sound cue or timing '
                 'mode it publishes, which action routine it selects, the constants it '
                 'forwards, the facing-indexed descriptor lookup, the returned EAX, and '
                 'every byte it writes in the compared window. The action routines and the '
                 'sound cue service belong to other scopes and are stopped at entry.',
    }, indent=2) + '\n')
    print(f'skill callback cases {cases} over {len(rows)} entry points')


if __name__ == '__main__':
    main(*sys.argv[1:])
