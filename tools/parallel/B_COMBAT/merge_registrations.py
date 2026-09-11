#!/usr/bin/env python3
"""Merge the B_COMBAT registration fragment into the shared registry."""
import json, sys
from pathlib import Path

def main(registry='tools/native_reconstructions.json',
         fragment='handoff/B_COMBAT/registrations.json'):
    target = Path(registry)
    base = json.loads(target.read_text())
    added = json.loads(Path(fragment).read_text())['entries']
    clash = {a for a in added if a in base['entries']
             and base['entries'][a].get('worker') != 'B_COMBAT'}
    if clash:
        raise SystemExit(f'entries already claimed by another session: {sorted(clash)}')
    base['entries'].update(added)
    target.write_text(json.dumps(base, indent=2) + '\n')
    print(f'merged {len(added)} entries; registry now holds {len(base["entries"])}')

if __name__ == '__main__':
    main(*sys.argv[1:])
