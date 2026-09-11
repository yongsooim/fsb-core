#!/usr/bin/env python3
"""Resolve guest addresses to whatever the repository already knows about them.

Reads nothing but reference/recovered-battle-manifest.json, include/fsb_core/symbols.hpp
and the hand-written sources. Builds nothing and changes nothing.

    tools/whatis.py 0x4808c7 0x804660
    fsb_event0_run ... 2>&1 | tools/whatis.py

Addresses may be bare or embedded in text, so a pasted fault line works as-is:
    Original calls (innermost first): 0x45733a 0x4808c7
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def semantic(source):
    # 004015c2_clip_rect_to_bounds_mc_4015c2.c -> clip_rect_to_bounds
    match = re.match(r'^[0-9a-f]{8}_(.+?)(_mc_[0-9a-f]+)?\.c$', source or '')
    return match.group(1) if match else None


def load():
    manifest = json.loads((ROOT / 'reference/recovered-battle-manifest.json').read_text())
    known = {}

    def note(address, kind, text):
        known.setdefault(int(address, 16), []).append((kind, text))

    for function in manifest['functions']:
        name = semantic(function.get('source'))
        note(function['entry'], 'recovered function',
             f"{name}  ({function['instructions']} instructions)" if name else function.get('source', ''))
    for address in manifest['external_services']:
        note(address, 'external service', None)
    for address, name in manifest['import_services'].items():
        note(address, 'Win32 import', name)
    for address, text in manifest['development_override_entries'].items():
        note(address, 'development override', text)
    for address, text in manifest['bounded_ai_raster_entries'].items():
        note(address, 'bounded AI raster', text)
    for owner, table in manifest['jump_tables'].items():
        note(table['address'], 'jump table', f"used by {owner}, {len(table['targets'])} targets")

    # symbols.hpp carries the data-side names, grouped by namespace.
    namespace = ''
    for line in (ROOT / 'include/fsb_core/symbols.hpp').read_text().splitlines():
        opened = re.match(r'namespace (\w+) \{', line)
        if opened and opened.group(1) != 'core':
            namespace = opened.group(1)
        constant = re.match(r'inline constexpr auto (\w+)\s*=\s*(0x[0-9a-fA-F]+)', line)
        if constant:
            note(constant.group(2), 'symbol', f'{namespace}::{constant.group(1)}')
    return known


def handler(address):
    """Which hand-written file answers this service, if any."""
    pattern = f'case 0x{address:x}'
    found = subprocess.run(['grep', '-rn', '-e', pattern, '-e', pattern + 'u', 'src/'],
                           cwd=ROOT, capture_output=True, text=True).stdout.splitlines()
    return [line.split(':')[0] + ':' + line.split(':')[1]
            for line in found if 'recovered_battle_generated' not in line]


def report(address, known):
    print(f'0x{address:x}')
    roles = known.get(address, [])
    sites = handler(address)
    for kind, text in roles:
        print(f'  {kind:22} {text}' if text else f'  {kind}')
    if not roles:
        section = ('.text' if 0x401000 <= address < 0x4a2000 else
                   '.rdata' if address < 0x4a5000 else
                   '.data' if address < 0x859000 else
                   '.idata' if address < 0x85a000 else None)
        print(f'  {"unnamed":22} {"inside " + section if section else "outside the mapped image"}')
    for site in sites:
        print(f'  {"handled in":22} {site}')
    # The fault a service raises is "unconnected", so say plainly when that is why.
    if any(kind == 'external service' for kind, _ in roles) and not sites:
        print(f'  {"not connected":22} no case label for it in src/')


def main(argv):
    text = ' '.join(argv[1:]) if len(argv) > 1 else sys.stdin.read()
    addresses = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{4,8})\b', text)]
    if not addresses:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    known = load()
    seen = set()
    for address in addresses:
        if address in seen:
            continue
        seen.add(address)
        report(address, known)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
