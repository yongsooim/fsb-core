#!/usr/bin/env python3
"""Copy original Event 0 evidence/assets with provenance. No game execution.
Usage: python3 tools/prepare_event0.py GAME_DIRECTORY OUTPUT_DIRECTORY
"""
from pathlib import Path
import hashlib
import json
import struct
import sys


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        pe = self.u32(0x3c)
        if self.data[:2] != b'MZ' or self.data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE')
        opt = pe + 24
        if self.u16(opt) != 0x10b:
            raise ValueError('expected PE32')
        self.base = self.u32(opt + 28)
        table = opt + self.u16(pe + 20)
        self.sections = [struct.unpack_from('<IIII', self.data, table + 40 * i + 8)
                         for i in range(self.u16(pe + 6))]
        rsrc = self.u32(opt + 112)
        self.rsrc = self.offset(rsrc) if rsrc else None

    def u16(self, off):
        return struct.unpack_from('<H', self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from('<I', self.data, off)[0]

    def offset(self, rva):
        for virtual_size, address, size, at in self.sections:
            if address <= rva < address + size:
                return at + rva - address
        raise ValueError(f'RVA {rva:x} not file-backed')

    def string(self, va, encoding='ascii'):
        at = self.offset(va - self.base)
        end = self.data.index(0, at)
        return self.data[at:end].decode(encoding)

    def resources(self):
        if self.rsrc is None:
            return

        def walk(off, keys):
            if len(keys) > 3:
                raise ValueError('invalid resource nesting')
            for i in range(self.u16(off + 12) + self.u16(off + 14)):
                key, child = struct.unpack_from('<II', self.data, off + 16 + i * 8)
                if key & 0x80000000:
                    at = self.rsrc + (key & 0x7fffffff)
                    key = self.data[at + 2:at + 2 + 2 * self.u16(at)].decode('utf-16le')
                else:
                    key = str(key)
                if child & 0x80000000:
                    yield from walk(self.rsrc + (child & 0x7fffffff), keys + [key])
                else:
                    rva, size = struct.unpack_from('<II', self.data, self.rsrc + child)
                    at = self.offset(rva)
                    if len(keys) != 2 or at + size > len(self.data):
                        raise ValueError('invalid resource payload')
                    yield keys[0], keys[1], key, self.data[at:at + size]
        yield from walk(self.rsrc, [])


def map_resource_library(exe):
    #448739 pushes the filename at5be844 into library slot3;4036e8 routes '#'
    #to that slot. MAPSET2 is a different bank with stripped Order bits.
    name = exe.string(0x5be844)
    if name.lower() != 'mapset.dll':
        raise ValueError('unexpected original map-resource slot3 binding: ' + name)
    return 'MAPSET.dll'


def main():
    source, output = map(Path, sys.argv[1:3])
    manifest = {'source_hashes': {}, 'assets': [], 'excluded': ['AVI movie playback'],
                'classification': 'Original bytes/tables. No captured raster reconstruction.'}

    def save(relative, data, origin):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        manifest['assets'].append({'path': relative, 'origin': origin, 'bytes': len(data),
                                   'sha256': hashlib.sha256(data).hexdigest()})

    for name in ['FLYINGSB.EXE', 'FlyingSB.ini']:
        save(name, (source / name).read_bytes(), name)
    exe = PE(source / 'FLYINGSB.EXE')
    texts = []
    for name, va in [('SB', 0x61ecc0), ('SONA', 0x61ecc2), ('SON5', 0x61ed85),
                     ('MES', 0x61ef9d), ('MES1', 0x61f03a)]:
        text = exe.string(va, 'cp949')
        texts.append({'channel': name, 'va': f'0x{va:x}', 'markup': text})
    save('event0-dialogues.json', json.dumps(texts, ensure_ascii=False, indent=2).encode(), 'FLYINGSB.EXE CP949 strings')

    cues = {}
    for cue in [90, 119, 120, 121, 122, 123, 124, 125, 126]:
        name_ptr = exe.u32(exe.offset(0x5ad388 + cue * 16 - exe.base))
        name = exe.string(name_ptr).lstrip('%')
        cues[Path(name).stem.upper()] = cue
    found_cues = set()
    map_library = map_resource_library(exe)
    for filename in [map_library, 'ASE_PS.dll', 'ASE_FM.dll', 'pcxset.dll', 'se_event.dll']:
        pe = PE(source / filename)
        manifest['source_hashes'][filename] = hashlib.sha256(pe.data).hexdigest()
        for kind, name, lang, data in pe.resources():
            upper = name.upper()
            wanted = (filename == map_library and upper.startswith('TCL0')) or \
                     (filename in ['ASE_PS.dll', 'ASE_FM.dll'] and kind == 'PCX' and (upper.startswith('CSON') or upper in ['ESON08', 'S3FEEL', 'SSG'])) or \
                     (filename == 'pcxset.dll' and kind == 'PCX' and upper in ['WHDLGBOX', 'WHARROW', 'NOIMAGE', 'SHADOW', 'SITEM', 'SPAN04']) or \
                     (filename == 'se_event.dll' and upper in cues)
            if wanted:
                save(f'{Path(filename).stem}/{name}.{kind.lower()}', data, f'{filename}/{kind}/{name}/{lang}')
                if filename == 'se_event.dll': found_cues.add(upper)
    if found_cues != set(cues):
        raise ValueError(f'Missing cue resources: {set(cues) - found_cues}')
    bgm = []
    source_names = {p.name.upper(): p for p in source.iterdir() if p.is_file()}
    for cue in [8, 49, 50]:
        name_ptr = exe.u32(exe.offset(0x5abbc0 + cue * 4 - exe.base))
        name = exe.string(name_ptr)
        path = source_names.get(name.upper())
        if path is None:
            raise ValueError(f'Missing BGM {name}')
        save(f'audio/{path.name}', path.read_bytes(), path.name)
        bgm.append({'id': cue, 'name': name})
    manifest['bgm'] = bgm
    manifest['sound_cues'] = [{'id': cue, 'name': name} for name, cue in cues.items()]
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
    print(f'Prepared {len(manifest["assets"])} original payloads')


if __name__ == '__main__':
    main()
