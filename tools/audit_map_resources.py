#!/usr/bin/env python3
"""Audit original '#' module binding and every currently registered map payload."""
from pathlib import Path
import sys,json,hashlib,struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from prepare_event0 import PE,map_resource_library

def audit(source,assets,output):
    source,assets=Path(source),Path(assets);exe=PE(assets/'FLYINGSB.EXE');library=map_resource_library(exe)
    dis=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(exe.data[exe.offset(0x448739-exe.base):exe.offset(0x448a00-exe.base)],0x448739))
    binding=[]
    for i,ins in enumerate(dis):
        if ins.mnemonic=='call' and ins.op_str=='0x403982' and i>=2 and dis[i-1].mnemonic=='push' and dis[i-1].op_str=='3':
            if dis[i-2].op_str!='0x5be844':raise ValueError('module3 filename instruction changed')
            binding=[{'address':hex(v.address),'bytes':v.bytes.hex(),'instruction':v.mnemonic+' '+v.op_str} for v in dis[i-2:i+1]]
    if not binding:raise ValueError('no original slot3 loader call')
    primary=PE(source/library);legacy=PE(source/'MAPSET2.dll');original={(k.upper(),n.upper()):d for k,n,l,d in primary.resources()};other={(k.upper(),n.upper()):d for k,n,l,d in legacy.resources()}
    manifest=json.loads((assets/'manifest.json').read_text());records=[a for a in manifest['assets'] if a['path'].startswith('MAPSET/')];maps=[];same=0
    for row in records:
        p=Path(row['path']);key=p.suffix[1:].upper(),p.stem;payload=(assets/p).read_bytes()
        if payload!=original[key] or hashlib.sha256(payload).hexdigest()!=row['sha256']:raise ValueError('prepared payload differs from original: '+str(p))
        old=other[key]
        if payload==old:same+=1;continue
        if key[0]!='MAP' or payload[:480]!=old[:480]:raise ValueError('unexpected non-Order resource difference')
        at=480;cells=0
        for layer in range(2):
            w,h=struct.unpack_from('<II',payload,layer*48+24);n=w*h
            if payload[at:at+n*4]!=old[at:at+n*4]:raise ValueError('tile code difference')
            at+=n*4
            for i in range(n):
                a=struct.unpack_from('<I',payload,at+i*4)[0];b=struct.unpack_from('<I',old,at+i*4)[0]
                if (a^b)&~0x1f0000:raise ValueError('movement or non-Order bits differ')
                cells+=a!=b
            at+=n*4
        maps.append({'path':str(p),'changed_order_cells':cells,'original_sha256':hashlib.sha256(payload).hexdigest(),'legacy_sha256':hashlib.sha256(old).hexdigest()})
    for name in ['field-assets.tsv','sequence-assets.tsv']:
        for line in (assets/name).read_text().splitlines():
            fields=line.split('\t')
            if fields[0]=='map' and fields[2]!='MAPSET':raise ValueError('wrong catalog bank')
    result={'exe_sha256':hashlib.sha256(exe.data).hexdigest(),'prefix':'#','slot':3,'filename':library,'bootstrap_instructions':binding,'source_sha256':hashlib.sha256(primary.data).hexdigest(),'legacy_source_sha256':hashlib.sha256(legacy.data).hexdigest(),'prepared_resources_verified':len(records),'identical_resources':same,'changed_map_count':len(maps),'changed_order_cells':sum(m['changed_order_cells'] for m in maps),'only_bits_16_to_20_differ':True,'maps':maps}
    Path(output).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='maps'}))
if __name__=='__main__':audit(*sys.argv[1:])
