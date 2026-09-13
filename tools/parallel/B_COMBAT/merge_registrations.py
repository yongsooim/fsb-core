#!/usr/bin/env python3
"""Merge the B_COMBAT registration fragment into the shared registry."""
import argparse, json
from pathlib import Path

def main(registry, fragment):
    target = Path(registry)
    base = json.loads(target.read_text())
    added = json.loads(Path(fragment).read_text())['entries']
    clash = {a for a in added if a in base['entries']
             and base['entries'][a].get('worker') != 'B_COMBAT'}
    if clash:
        raise SystemExit(f'entries already registered outside B_COMBAT: {sorted(clash)}')
    base['entries'].update(added)
    target.write_text(json.dumps(base, indent=2) + '\n')
    print(f'merged {len(added)} entries; registry now holds {len(base["entries"])}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('registry', type=Path)
    parser.add_argument('fragment', type=Path)
    args = parser.parse_args()
    main(args.registry, args.fragment)
