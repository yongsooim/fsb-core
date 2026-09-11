#!/usr/bin/env python3
"""Classify the D_EFFECTS original entry points from their recorded machine code.

Evidence is the disassembly the generator embedded in src/recovered/*.cpp, which
carries the original bytes for every instruction. Decompiler cache names are read
only as a naming hint; nothing here decides semantics from a name.

Output: tools/parallel/D_EFFECTS/inventory.json
"""
import collections
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCOPES = ROOT / 'tasks/parallel/scopes.json'
CACHE = Path('/Users/ysim/repo/fsb/decompiled/functions')

FUNC_SPLIT = re.compile(r'\nvoid RecoveredBattle::(fn_[0-9a-f]+)\(\)\{\n')
INSN = re.compile(r'^L([0-9a-f]+): \{ // ([0-9a-f]+)\s+(\S+)(?: (.*))?$', re.M)


def load_disassembly(entries):
    """Return {entry address: [(address, bytes, mnemonic, operands)]}."""
    bodies = {}
    for path in sorted({e['generated_file'] for e in entries}):
        parts = FUNC_SPLIT.split((ROOT / path).read_text())
        for i in range(1, len(parts), 2):
            bodies['0x' + parts[i][3:]] = parts[i + 1]
    out = {}
    for e in entries:
        body = bodies[e['entry']]
        insns = [(int(a, 16), b, m, (o or '').strip()) for a, b, m, o in INSN.findall(body)]
        if len(insns) != e['instructions']:
            raise SystemExit(f"{e['entry']}: parsed {len(insns)} instructions, manifest says {e['instructions']}")
        digest = hashlib.sha256(b''.join(bytes.fromhex(b) for _, b, _, _ in insns)).hexdigest()
        if digest != e['instruction_sha256']:
            raise SystemExit(f"{e['entry']}: instruction bytes do not match the manifest hash")
        out[e['entry']] = insns
    return out


NUMBER = r'(?:0x[0-9a-f]+|-?\d+)'
CALL = re.compile(r'^0x([0-9a-f]+)$')
PUSH_IMM = re.compile(rf'^({NUMBER})$')
PUSH_STACK = re.compile(rf'^dword ptr \[esp(?: \+ ({NUMBER}))?\]$')
PUSH_TABLE = re.compile(rf'^dword ptr \[eax\*4 \+ ({NUMBER})\]$')
PUSH_GLOBAL = re.compile(rf'^dword ptr \[({NUMBER})\]$')


def number(text):
    return int(text, 16) if text.startswith('0x') else int(text, 10)


def classify(insns):
    """Recognise the pure forwarding wrappers; everything else is 'logic'."""
    mnemonics = [m for _, _, m, _ in insns]
    tail = insns[-1]
    if tail[2] != 'ret' or mnemonics.count('call') != 1 or insns[-2][2] != 'call':
        return None
    call_target = CALL.match(insns[-2][3])
    if not call_target:
        return None
    # The callee reads arg0 at [esp + 4] on entry, so each push shifts the view by four.
    args, loads, pushes = [], [], 0
    for _, _, mnemonic, operands in insns[:-2]:
        if mnemonic == 'mov' and operands.startswith('eax, '):
            loads.append(operands[5:])
            continue
        if mnemonic != 'push':
            return None
        if (match := PUSH_STACK.match(operands)):
            offset = number(match.group(1)) if match.group(1) else 0
            index = (offset - 4 - 4 * pushes) // 4
            if index < 0 or (offset - 4 - 4 * pushes) % 4:
                return None
            args.append({'kind': 'incoming_argument', 'index': index})
        elif (match := PUSH_TABLE.match(operands)):
            args.append({'kind': 'indexed_table', 'table': hex(number(match.group(1))), 'index_chain': list(loads)})
        elif (match := PUSH_GLOBAL.match(operands)):
            args.append({'kind': 'global', 'address': hex(number(match.group(1)))})
        elif (match := PUSH_IMM.match(operands)):
            args.append({'kind': 'constant', 'value': hex(number(match.group(1)) & 0xffffffff)})
        else:
            return None
        pushes += 1
    # Pushes happen right to left, so the callee's first argument is the last push.
    return {
        'target': '0x' + call_target.group(1),
        'arguments': list(reversed(args)),
        'stack_bytes_released': number(tail[3]) if tail[3] else 0,
    }


def readability(source):
    """Pull the cache's READABILITY note, when the cache has one."""
    path = CACHE / source
    if not path.exists():
        return None
    text = path.read_text(errors='replace')
    block = re.search(r'/\*\n \* READABILITY:\n(.*?)\n \*/', text, re.S)
    if not block:
        return None
    lines = [re.sub(r'^ \* ?', '', line) for line in block.group(1).split('\n')]
    return ' '.join(l.strip() for l in lines if l.strip())


def main():
    scopes = json.loads(SCOPES.read_text())
    entries = scopes['workers']['D_EFFECTS']['entries']
    mine = {e['entry'] for e in entries}
    disassembly = load_disassembly(entries)

    shapes = collections.defaultdict(list)
    records = {}
    for e in entries:
        insns = disassembly[e['entry']]
        shape = hashlib.sha256(' '.join(m for _, _, m, _ in insns).encode()).hexdigest()[:12]
        shapes[shape].append(e['entry'])
        # A jmp inside the function's own address span is local control flow, not a callee.
        span = range(insns[0][0], insns[-1][0] + 1)
        calls = sorted({'0x' + m.group(1) for _, _, mn, op in insns if mn in ('call', 'jmp')
                        for m in [CALL.match(op)] if m and int(m.group(1), 16) not in span})
        records[e['entry']] = {
            'entry': e['entry'],
            'name': re.sub(r'\.c$', '', e['source']).split('_', 1)[1],
            'source': e['source'],
            'instructions': e['instructions'],
            'instruction_sha256': e['instruction_sha256'],
            'generated_file': e['generated_file'],
            'shape': shape,
            'calls': calls,
            'calls_outside_scope': [c for c in calls if c not in mine],
            'forwarder': classify(insns),
            'note': readability(e['source']),
        }

    callers = collections.defaultdict(list)
    for entry, record in records.items():
        for target in record['calls']:
            callers[target].append(entry)
    for entry, record in records.items():
        record['callers_in_scope'] = sorted(callers.get(entry, []))

    forwarders = [r for r in records.values() if r['forwarder']]
    families = collections.Counter(r['forwarder']['target'] for r in forwarders)
    out = {
        'source_sha256': scopes['source_sha256'],
        'entries': len(records),
        'distinct_shapes': len(shapes),
        'forwarders': len(forwarders),
        'forwarder_targets': dict(families.most_common()),
        'shape_clusters': {k: v for k, v in sorted(shapes.items(), key=lambda kv: -len(kv[1])) if len(v) > 1},
        'records': records,
    }
    destination = Path(__file__).with_name('inventory.json')
    destination.write_text(json.dumps(out, indent=2) + '\n')
    print(f'entries {len(records)} shapes {len(shapes)} forwarders {len(forwarders)} '
          f'forwarder targets {len(families)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
