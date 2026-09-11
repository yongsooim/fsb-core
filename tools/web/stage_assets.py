#!/usr/bin/env python3
"""Collect only the assets a browser build needs into one directory.

The full tree is 229 MB, most of it maps and music the requested boundary never
touches. Emscripten packages whatever it is pointed at, so pointing it at a
staged subset is the whole of the size story.

    tools/web/stage_assets.py assets /tmp/web-assets [--through-event N]
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

# Loaded unconditionally by register_runtime_assets().
SPRITE_BANKS = ('ASE_PS', 'ASE_FM', 'pcxset')
FONTS = ('gulim.ttc', 'batang.ttc', 'cp949.bin')


def stage(source: Path, target: Path, through_event: int) -> dict:
    manifest = json.loads((source / 'manifest.json').read_text())
    wanted = {Path('FLYINGSB.EXE'), Path('manifest.json')}
    for bank in SPRITE_BANKS:
        wanted |= {Path(bank) / f.name for f in (source / bank).iterdir()
                   if f.suffix.lower() in ('.pcx', '.bmp')}
    wanted |= {Path('fonts') / name for name in FONTS}
    wanted |= {Path('audio') / entry['name'] for entry in manifest['bgm']}
    wanted |= {Path('se_event') / f"{entry['name']}.wav" for entry in manifest['sound_cues']}

    # Map bundles are six files sharing a prefix. Event 0 opens map 469 only;
    # later boundaries pull their maps from the prepared catalogues.
    prefixes = {'TCL0___'}
    if through_event >= 2:
        for name in ('sequence-assets.tsv', 'field-assets.tsv', 'campaign-assets.tsv'):
            catalogue = source / name
            if not catalogue.exists():
                continue
            wanted.add(Path(name))
            for line in catalogue.read_text().splitlines():
                if not line.strip():
                    continue
                kind, _, path = line.split('\t')
                if kind == 'map':
                    wanted.add(Path(path))     # a directory; expanded below
                else:
                    wanted.add(Path(path))
    directories = {p for p in wanted if (source / p).is_dir()}
    for directory in directories:
        wanted.discard(directory)
        wanted |= {directory / f.name for f in (source / directory).iterdir() if f.is_file()}
    for prefix in prefixes:
        wanted |= {Path('MAPSET') / f.name for f in (source / 'MAPSET').iterdir()
                   if f.name.startswith(prefix)}

    shutil.rmtree(target, ignore_errors=True)
    total, missing = 0, []
    for relative in sorted(wanted):
        origin = source / relative
        if not origin.is_file():
            missing.append(str(relative))
            continue
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(origin, destination)
        total += origin.stat().st_size
    return {'files': len(wanted) - len(missing), 'bytes': total, 'missing': missing}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--through-event', type=int, default=0)
    arguments = parser.parse_args()
    report = stage(arguments.source, arguments.target, arguments.through_event)
    print(f"{report['files']} files, {report['bytes'] / 1e6:.1f} MB -> {arguments.target}")
    if report['missing']:
        print(f"missing {len(report['missing'])}: {report['missing'][:5]}", file=sys.stderr)
    return 1 if report['missing'] else 0


if __name__ == '__main__':
    sys.exit(main())
