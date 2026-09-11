#!/usr/bin/env python3
"""Prepare original early-field resources without modifying the original game."""
import hashlib
import json
import re
from pathlib import Path
import sys
from prepare_event0 import PE, map_resource_library


def main(source, output):
    source, output = Path(source), Path(output)
    exe = PE(source / 'FLYINGSB.EXE')
    if exe.data != (output / 'FLYINGSB.EXE').read_bytes():
        raise ValueError('field assets must use the same original EXE')
    map_library = map_resource_library(exe)
    map_directory = Path(map_library).stem
    manifest = json.loads((output / 'manifest.json').read_text())
    entries = {entry['path']: entry for entry in manifest['assets']}
    catalog, map_names = [], set()
    for map_id in [*range(22, 48), 476]:
        for delta in [0, 9]:
            at = exe.offset(0x5c4f44 + map_id * 68 + delta - exe.base)
            map_names.add(exe.data[at:at+9].split(b'\0')[0].decode('ascii').replace('#', '').upper())
        catalog.append(f'map\t{map_id}\t{map_directory}')
    cues = {}
    # Early battles use data-driven effect callbacks and script cue operands.
    # Register the original cue table in full so changing a legal action needs
    # no host-side asset guess. Empty original rows remain empty.
    cue_ids=range(362)
    for cue in sorted(cue_ids):
        pointer = exe.u32(exe.offset(0x5ad388 + cue * 16 - exe.base))
        if not pointer:continue
        name = exe.string(pointer)
        bank = 'se_event.dll' if name.startswith('%') else 'Wav_eft.dll' if name.startswith('$') else None
        if not name:continue
        if not bank:raise ValueError(f'unrecognized sound resource bank: {name}')
        cues.setdefault((bank,Path(name[1:]).stem.upper()),[]).append(cue)

    def save(relative, data, origin):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        entries[relative] = {'path': relative, 'origin': origin, 'bytes': len(data),
                             'sha256': hashlib.sha256(data).hexdigest()}

    for filename in [map_library, 'se_event.dll', 'Wav_eft.dll', 'pcxset.dll']:
        pe = PE(source / filename)
        manifest['source_hashes'][filename] = hashlib.sha256(pe.data).hexdigest()
        for kind, name, language, data in pe.resources():
            if ((filename == map_library and name[:-1].upper() in map_names)
                    or ((filename,name.upper()) in cues) or (filename=='pcxset.dll' and kind.upper() in ['PCX','BMP'])):
                relative = f'{Path(filename).stem}/{name}.{kind.lower()}'
                save(relative, data, f'{filename}/{kind}/{name}/{language}')
                if (filename,name.upper()) in cues:
                    for cue in cues[filename,name.upper()]:catalog.append(f'cue\t{cue}\t{relative}')
    source_names = {p.name.upper(): p for p in source.iterdir() if p.is_file()}
    bgm_ids={2,23,24,27,30,33,39,40,41,45,46,51,56}
    bgm_ids.update(int(value) for value in re.findall(br'<BGM([0-9]+)>',exe.data,re.IGNORECASE) if 0<int(value)<59)
    for bgm in sorted(bgm_ids):
        pointer = exe.u32(exe.offset(0x5abbc0 + bgm * 4 - exe.base))
        name = exe.string(pointer)
        path = source_names[name.upper()]
        relative = 'audio/' + name
        save(relative, path.read_bytes(), path.name)
        catalog.append(f'bgm\t{bgm}\t{relative}')
    (output / 'field-assets.tsv').write_text('\n'.join(catalog) + '\n')
    manifest['assets'] = list(entries.values())
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(f'Prepared {len(catalog)} early-field resource bindings')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: prepare_field_assets.py ORIGINAL_GAME_DIRECTORY OUTPUT_ASSETS')
    main(*sys.argv[1:])
