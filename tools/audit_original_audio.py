#!/usr/bin/env python3
"""Rebuild/check original-audio provenance from PE bytes and decompiler boundaries.

Development-only Capstone audit; the game has no decoder dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from prepare_event0 import PE


def audit(core, executable, decompiled):
    pe = PE(executable)
    names = dict((name, int(address, 16)) for name, address in re.findall(
        r'inline constexpr Address (\w+)=0x([0-9a-f]+);',
        (core / 'include/fsb_core/original_audio.hpp').read_text()))
    functions = {int(p.name[:8], 16): p for p in decompiled.glob('*.c')
                 if re.match(r'^[0-9a-f]{8}_', p.name)}
    starts = sorted(functions)
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    rows = []
    for name, entry in sorted(names.items(), key=lambda item: item[1]):
        source = functions[entry]
        end = next(address for address in starts if address > entry)
        body = pe.data[pe.offset(entry - pe.base):pe.offset(end - pe.base)]
        instructions = list(decoder.disasm(body, entry))
        returns = [{'address': hex(i.address), 'stack_pop_bytes': int(i.op_str, 0) if i.op_str else 0}
                   for i in instructions if i.mnemonic == 'ret']
        tail_contract = None
        if not returns and len(instructions) == 1 and instructions[0].mnemonic == 'jmp':
            #43356e is a five-byte tail thunk, not an unbalanced function.
            target = int(instructions[0].op_str, 0)
            target_end = next(address for address in starts if address > target)
            target_body = pe.data[pe.offset(target - pe.base):pe.offset(target_end - pe.base)]
            returns = [{'address': hex(i.address), 'stack_pop_bytes': int(i.op_str, 0) if i.op_str else 0}
                       for i in decoder.disasm(target_body, target) if i.mnemonic == 'ret']
            tail_contract = {'entry': hex(target), 'range_end': hex(target_end),
                             'decompiled_file': functions[target].name,
                             'machine_range_sha256': hashlib.sha256(target_body).hexdigest()}
        if not returns or len({r['stack_pop_bytes'] for r in returns}) != 1:
            raise ValueError(f'{entry:x}: return cleanup needs manual control-flow analysis')
        rows.append({'symbol': name, 'entry': hex(entry), 'range_end': hex(end),
                     'decompiled_file': source.name,
                     'decompiled_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                     'machine_range_sha256': hashlib.sha256(body).hexdigest(),
                     'return_sites': returns,
                     'adapter': 'src/original_audio.cpp',
                     'semantic_service': 'Audio'})
        if tail_contract: rows[-1]['tail_return_contract'] = tail_contract
    return {'schema': 1, 'exe_sha256': hashlib.sha256(pe.data).hexdigest(),
            'scope': 'Audio service extraction; addresses and RET cleanup checked against original bytes',
            'limits': ['Function range ends come from decompiler entry boundaries.',
                       'This is not execution of the original DirectSound implementation.',
                       'Void-function EAX values are not proved by a RET-immediate audit.'],
            'services': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('decompiled', type=Path)
    parser.add_argument('--core', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    result = audit(args.core, args.executable, args.decompiled)
    path = args.core / 'reference/original-audio-services.json'
    if args.check:
        if json.loads(path.read_text()) != result:
            raise SystemExit('Original audio provenance changed; inspect the differences before updating.')
    else:
        path.write_text(json.dumps(result, indent=2) + '\n')
    print(f"original_audio_services={len(result['services'])} provenance_check=passed")
