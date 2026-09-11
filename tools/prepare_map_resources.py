#!/usr/bin/env python3
"""Prepare all currently registered map bundles from the original slot3 bank.

Leaves the old MAPSET2 evidence untouched; updates production catalog bindings
and the resource manifest. No guessed Order values or tile edits are applied.
"""
from pathlib import Path
import hashlib,json,sys
from prepare_event0 import PE,map_resource_library

def main(source,assets):
    source,assets=Path(source),Path(assets);exe=PE(source/'FLYINGSB.EXE')
    if exe.data!=(assets/'FLYINGSB.EXE').read_bytes():raise ValueError('different original EXE')
    library=map_resource_library(exe);bank=Path(library).stem;pe=PE(source/library)
    catalogs={};ids={469}
    for name in ['sequence-assets.tsv','field-assets.tsv']:
        rows=(assets/name).read_text().splitlines();updated=[]
        for row in rows:
            fields=row.split('\t')
            if fields[0]=='map':ids.add(int(fields[1]));fields[2]=bank
            updated.append('\t'.join(fields))
        catalogs[name]=updated
    expected=set()
    for i in ids:
        tile=exe.data[exe.offset(0x5c4f44+i*68-exe.base):][:9].split(b'\0')[0].decode().replace('#','').upper()
        layout=exe.data[exe.offset(0x5c4f4d+i*68-exe.base):][:9].split(b'\0')[0].decode().replace('#','').upper()
        expected.update((kind,tile+suffix) for kind in ['PCX','MAT'] for suffix in ['P','S'])
        expected.update((kind,layout+'P') for kind in ['MAP','MFO'])
    payloads={(kind.upper(),name.upper()):(lang,data) for kind,name,lang,data in pe.resources() if (kind.upper(),name.upper()) in expected}
    if expected-set(payloads):raise ValueError('original bank lacks '+repr(expected-set(payloads)))
    manifest=json.loads((assets/'manifest.json').read_text());entries={x['path']:x for x in manifest['assets']}
    for (kind,name),(lang,data) in sorted(payloads.items()):
        relative=f'{bank}/{name}.{kind.lower()}';p=assets/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        entries[relative]={'path':relative,'origin':f'{library}/{kind}/{name}/{lang}','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    manifest['source_hashes'][library]=hashlib.sha256(pe.data).hexdigest();manifest['assets']=list(entries.values())
    manifest['map_resource_binding']={'prefix':'#','slot':3,'filename_address':'0x5be844','bootstrap':'0x448739','prefix_resolver':'0x4036e8','library':library,'directory':bank,'maps':sorted(ids),'unused_comparison_bank':'MAPSET2'}
    (assets/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    for name,rows in catalogs.items():(assets/name).write_text('\n'.join(rows)+'\n')
    print(f'Prepared {len(payloads)} original map resources for {len(ids)} maps from {library}')
if __name__=='__main__':main(*sys.argv[1:])
