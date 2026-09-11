#!/usr/bin/env python3
"""Prepare original shared sprites, maps and audio used by Events2..8.

Run after prepare_event0.py. Sources are read-only; only OUTPUT is written.
Shared sprite libraries are copied as encoded PCX resources, not screenshot art.
"""
import hashlib
import json
from pathlib import Path
import sys
from prepare_event0 import PE, map_resource_library


def main(source, output):
    source, output = Path(source), Path(output)
    exe = PE(source / 'FLYINGSB.EXE')
    if exe.data != (output / 'FLYINGSB.EXE').read_bytes():
        raise ValueError('sequence assets must use the same original EXE')
    map_library = map_resource_library(exe)
    map_directory = Path(map_library).stem
    manifest = json.loads((output / 'manifest.json').read_text())
    entries = {entry['path']: entry for entry in manifest['assets']}
    catalog = []
    map_names = set()
    for map_id in range(470, 475):
        for delta in [0, 9]:
            at = exe.offset(0x5c4f44 + map_id * 68 + delta - exe.base)
            map_names.add(exe.data[at:at+9].split(b'\0')[0].decode('ascii').replace('#', '').upper())
        catalog.append(f'map\t{map_id}\t{map_directory}')
    cue_ids = [17, 20, 23, 61, 96, 113]
    cues = {}
    for cue in cue_ids:
        pointer = exe.u32(exe.offset(0x5ad388 + cue * 16 - exe.base))
        name = exe.string(pointer).lstrip('%')
        cues[Path(name).stem.upper()] = (cue, name)

    def save(relative, data, origin):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        entries[relative] = {'path': relative, 'origin': origin, 'bytes': len(data),
                             'sha256': hashlib.sha256(data).hexdigest()}

    for filename in [map_library, 'ASE_PS.dll', 'ASE_FM.dll', 'se_event.dll']:
        pe = PE(source / filename)
        manifest['source_hashes'][filename] = hashlib.sha256(pe.data).hexdigest()
        for kind, name, language, data in pe.resources():
            wanted = ((filename == map_library and name[:-1].upper() in map_names)
                      or (filename in ['ASE_PS.dll', 'ASE_FM.dll'] and kind == 'PCX')
                      or (filename == 'se_event.dll' and name.upper() in cues))
            if wanted:
                relative = f'{Path(filename).stem}/{name}.{kind.lower()}'
                save(relative, data, f'{filename}/{kind}/{name}/{language}')
                if filename == 'se_event.dll':
                    catalog.append(f'cue\t{cues[name.upper()][0]}\t{relative}')
    source_names = {p.name.upper(): p for p in source.iterdir() if p.is_file()}
    for bgm in [3, 13, 16]:
        pointer = exe.u32(exe.offset(0x5abbc0 + bgm * 4 - exe.base))
        name = exe.string(pointer)
        path = source_names[name.upper()]
        relative = 'audio/' + name
        save(relative, path.read_bytes(), path.name)
        catalog.append(f'bgm\t{bgm}\t{relative}')
    (output / 'sequence-assets.tsv').write_text('\n'.join(catalog) + '\n')
    manifest['assets'] = list(entries.values())
    manifest['sequence_scope'] = 'Events2..8 maps470..474 and initial audio requirements; full shared PCX sprite libraries'
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(f'Prepared {len(entries)} total original payloads; {len(catalog)} sequence resource bindings')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: prepare_sequence_assets.py ORIGINAL_GAME_DIRECTORY OUTPUT_ASSETS')
    main(*sys.argv[1:])
