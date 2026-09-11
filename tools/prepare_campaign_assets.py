#!/usr/bin/env python3
"""Prepare the remaining original campaign catalog; copying assets is not gameplay completion."""
from pathlib import Path
import hashlib,json,sys
from prepare_event0 import PE,map_resource_library

def main(source,assets):
    source,assets=Path(source),Path(assets);exe=PE(source/'FLYINGSB.EXE')
    if exe.data!=(assets/'FLYINGSB.EXE').read_bytes():raise ValueError('campaign EXE differs')
    library=map_resource_library(exe);bank=Path(library).stem;pe=PE(source/library)
    payloads={(kind.upper(),name.upper()):(language,data) for kind,name,language,data in pe.resources()}
    manifest=json.loads((assets/'manifest.json').read_text());entries={v['path']:v for v in manifest['assets']};catalog=[];wanted=set();skipped=[];ids=[]
    for i in range(500):
        a=exe.string(0x5c4f44+i*68);b=exe.string(0x5c4f4d+i*68)
        if not(a.startswith('#') and b.startswith('#')):skipped.append(i);continue
        tile,layout=a[1:].upper(),b[1:].upper();required={(kind,tile+suffix) for kind in ['PCX','MAT'] for suffix in ['P','S']}|{(kind,layout+'P') for kind in ['MAP','MFO']}
        if required-set(payloads):skipped.append(i);continue
        wanted.update(required);catalog.append(f'map\t{i}\t{bank}');ids.append(i)
    def save(relative,data,origin):
        p=assets/relative;p.parent.mkdir(parents=True,exist_ok=True)
        if not p.exists() or p.read_bytes()!=data:p.write_bytes(data)
        entries[relative]={'path':relative,'origin':origin,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    for kind,name in sorted(wanted):
        lang,data=payloads[kind,name];save(f'{bank}/{name}.{kind.lower()}',data,f'{library}/{kind}/{name}/{lang}')
    files={p.name.upper():p for p in source.iterdir() if p.is_file()}
    for id in range(1,59):
        pointer=exe.u32(exe.offset(0x5abbc0+id*4-exe.base))
        if not pointer:continue
        name=exe.string(pointer)
        if not name:continue
        file=files[name.upper()];relative='audio/'+file.name;save(relative,file.read_bytes(),file.name);catalog.append(f'bgm\t{id}\t{relative}')
    (assets/'campaign-assets.tsv').write_text('\n'.join(catalog)+'\n');manifest['assets']=list(entries.values());manifest['source_hashes'][library]=hashlib.sha256(pe.data).hexdigest();manifest['campaign_catalog']={'prepared_maps':ids,'missing_or_unused_map_ids':skipped,'resource_only_not_gameplay_verified':True}
    (assets/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n');print(f'Prepared {len(ids)} map bindings, {len(wanted)} payloads and all original BGM slots; skipped={skipped}')
if __name__=='__main__':main(*sys.argv[1:])
