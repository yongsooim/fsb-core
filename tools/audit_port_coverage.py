#!/usr/bin/env python3
"""Check actual fixed-code calls against explicit, implemented core adapters.

This is static linkage evidence, not a claim of full gameplay/pixel parity.
Data-pointer candidates are reported separately and require source inspection.
"""
import argparse,hashlib,json,re
from pathlib import Path
from prepare_event0 import PE

def audit(root,decompiled=None):
    manifest=json.loads((root/'reference/recovered-battle-manifest.json').read_text())
    profile=json.loads((root/'tools/original_service_boundaries.json').read_text());boundaries=profile['entries']
    pe=PE(root/'assets/FLYINGSB.EXE');exe_sha=hashlib.sha256(pe.data).hexdigest()
    if exe_sha!=manifest['source_sha256']:raise ValueError('original EXE revision differs from generated manifest')
    def cpp_code(text):
        return re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/',' ',text,flags=re.S)
    symbols={}
    for path in (root/'include').rglob('*.hpp'):
        for name,value in re.findall(r'constexpr\s+(?:auto|Address|unsigned)\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)',path.read_text()):
            symbols.setdefault(name,set()).add(int(value,16))
    def address(token):
        if token.startswith('0x'):return int(token.rstrip('uU'),16)
        values=symbols.get(token.split('::')[-1],set())
        return next(iter(values)) if len(values)==1 else None
    source_cases={}
    generated_sources=manifest.get('generated_sources',['src/recovered_battle_generated.cpp'])
    for path in (root/'src').rglob('*.cpp'):
        if path.relative_to(root).as_posix() in generated_sources:continue
        found={address(t) for t in re.findall(r'case\s+([\w:]+)\s*:',cpp_code(path.read_text()))}
        source_cases[path.name]=found-{None}
    code='\n'.join((root/path).read_text() for path in generated_sources)
    generated={int(f['entry'],16) for f in manifest['functions']}
    native=manifest.get('native_reconstructions',{})
    for entry,record in native.items():
        if int(entry,16) not in source_cases.get(record['bridge'],set()):raise ValueError('missing native reconstruction bridge '+entry)
        if f'void RecoveredBattle::fn_{int(entry,16):x}(' in code:raise ValueError('reconstructed function still emits register code '+entry)
    for entry,record in manifest.get('partial_native_reconstructions',{}).items():
        if entry in native:raise ValueError('partial entry counted as complete '+entry)
        if int(entry,16) not in source_cases.get(record['bridge'],set()):raise ValueError('missing partial bridge '+entry)
        selection=json.loads((root/record['selection']).read_text())
        if not selection['rows']:raise ValueError('empty partial selection '+entry)
        if selection['entry']!=entry:raise ValueError('wrong partial selection owner '+entry)
        expected=[(row['id'],int(row['korean'],16),int(row['other'],16),row['localized']) for row in selection['rows']]
        compiled=[(int(i,16),int(k,16),int(o,16),flag=='true') for i,k,o,flag in re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (true|false)\}',(root/record['selection_cpp']).read_text())]
        if compiled!=expected or len({row[0] for row in expected})!=len(expected):raise ValueError('partial compiled selection differs '+entry)

        if record['dispatcher']+'(entry)' not in code:raise ValueError('partial dispatcher is not wired '+entry)
        if f'void RecoveredBattle::fn_{int(entry,16):x}(' not in code:raise ValueError('partial remainder missing '+entry)
    builtin_code=code.split('void RecoveredBattle::dispatch(Address entry)',1)[1]
    builtins={int(t,16) for t in re.findall(r'case (0x[0-9a-f]+):',builtin_code)}-generated
    implemented={a for a,owner in boundaries.items() if int(a,16) in source_cases.get(owner,set()) or int(a,16) in builtins}
    omitted={a:owner for a,owner in boundaries.items() if 'intentionally excluded' in owner}
    external=set(manifest['external_services'])
    missing=sorted(external-implemented-set(omitted),key=lambda a:int(a,16))
    required_imports=set(re.findall(r'\bcall dword ptr \[(0x859[0-9a-f]+)\]',code))
    handled_imports={hex(a) for values in source_cases.values() for a in values}
    missing_imports=sorted(required_imports-handled_imports,key=lambda a:int(a,16))
    native_callbacks={int(a,16) for a in profile.get('native_callbacks',{})}
    missing_native_callbacks=sorted(hex(a) for a in native_callbacks-source_cases.get('runtime.cpp',set()))
    function_index={int(path.name[:8],16):path.name for path in decompiled.glob('*.c') if re.match(r'[0-9a-f]{8}_',path.name)} if decompiled else {}
    direct_edges=[];pointer_candidates=[];unclassified_pushes=[];owner=None;bodies={}
    for line in code.splitlines():
        match=re.match(r'void RecoveredBattle::fn_([0-9a-f]+)\(',line)
        if match:
            owner='0x'+match[1]
            if owner in bodies:raise ValueError('duplicate generated function '+owner)
            bodies[owner]={}
        match=re.match(r'L([0-9a-f]+): \{ // ([0-9a-f]+)  (\S+) (.*)',line)
        if not match:continue
        site,raw,mnemonic,operands=match.groups()
        original=bytes.fromhex(raw);at=pe.offset(int(site,16)-pe.base)
        if pe.data[at:at+len(original)]!=original:raise ValueError('generated instruction bytes differ at0x'+site)
        bodies[owner][int(site,16)]=original
        if mnemonic in ['call','jmp'] and re.fullmatch(r'0x[0-9a-f]+',operands):
            target=int(operands,16)
            if mnemonic=='call' or target in generated or operands in external:
                direct_edges.append({'caller':owner,'site':'0x'+site,'target':operands,'kind':mnemonic,'confidence':'EXTRACTED'})
        if mnemonic=='mov' and '[' in operands:
            target=re.search(r', (0x[0-9a-f]+)$',operands)
            if target and 0x401000<=int(target[1],16)<0x4971f0 and int(target[1],16) not in generated|native_callbacks and target[1] not in implemented:
                pointer_candidates.append({'caller':owner,'site':'0x'+site,'target':target[1],'operands':operands,'confidence':'AMBIGUOUS'})
        if mnemonic=='push' and re.fullmatch(r'0x[0-9a-f]+',operands):
            target=int(operands,16)
            if target in function_index and target not in generated|native_callbacks and operands not in implemented:
                unclassified_pushes.append({'caller':owner,'site':'0x'+site,'target':operands,'source':function_index[target],'confidence':'AMBIGUOUS'})
    for function in manifest['functions']:
        if function['entry'] in native:
            body={int(i['address'],16):bytes.fromhex(i['bytes']) for i in native[function['entry']]['source_instructions']}
            for address,raw in body.items():
                at=pe.offset(address-pe.base)
                if pe.data[at:at+len(raw)]!=raw:raise ValueError('native reconstruction source bytes differ')
        else:body=bodies[function['entry']]
        digest=hashlib.sha256(b''.join(body[a] for a in sorted(body))).hexdigest()
        if len(body)!=function['instructions'] or digest!=function['instruction_sha256']:raise ValueError('instruction manifest differs for'+function['entry'])
    if set(bodies)!={f['entry'] for f in manifest['functions']} - set(native):raise ValueError('unexpected generated function set')
    for entry,path in manifest.get('generated_owners',{}).items():
        if f'void RecoveredBattle::fn_{int(entry,16):x}(' not in (root/path).read_text():raise ValueError('wrong generated owner '+entry)
    script_callbacks=[]
    for _,virtual,size,offset in pe.sections:
        data=pe.data[offset:offset+size]
        for hit in re.finditer(b'\x1f[\x00\x01]',data):
            at=hit.start()
            if at+9>len(data):continue
            length=int.from_bytes(data[at+2:at+4],'little');descriptor=data[at+4]
            if length<9 or length>160 or (length-4)%5 or at+length>len(data) or descriptor&0x40 or descriptor&0x3f not in [0,1,2,4]:continue
            target=int.from_bytes(data[at+5:at+9],'little')
            if target not in function_index:continue
            script_callbacks.append({'site':hex(pe.base+virtual+at),'target':hex(target),'covered':target in generated or hex(target) in implemented,'confidence':'EXTRACTED','basis':'typed immediate opcode1f record; not a reachability claim'})
    return {'source_sha256':manifest['source_sha256'],'source_functions':len(generated),'generated_functions':len(generated)-len(native),'native_reconstructions':len(native),
            'partial_native_reconstructions':manifest.get('partial_native_reconstructions',{}),
            'external_functions':len(external),'implemented_external_functions':len(external&implemented),
            'intentionally_omitted':{a:omitted[a] for a in external if a in omitted},
            'unconnected_external_functions':missing,'required_imports':sorted(required_imports),
            'unconnected_imports':missing_imports,'direct_edges':direct_edges,
            'unclassified_code_pointer_writes':pointer_candidates,
            'unclassified_function_address_pushes':unclassified_pushes,
            'unconnected_native_callbacks':missing_native_callbacks,
            'typed_script_callback_records':script_callbacks,
            'source_instruction_bytes_and_manifest_verified':True,
            'decompiler_index_available':decompiled is not None,
            'scope':'Static calls and declared ABI adapters; pointer candidates are not inferred callbacks. Does not certify all dynamic branches or Windows pixel/time parity.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent);parser.add_argument('--decompiled',type=Path);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    result=audit(args.root,args.decompiled);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['direct_edges','unclassified_code_pointer_writes','unclassified_function_address_pushes','typed_script_callback_records','scope']},indent=2))
    print('Unclassified pointer writes:',len(result['unclassified_code_pointer_writes']))
    print('Typed script callbacks:',len(result['typed_script_callback_records']))
    if args.check and (result['unconnected_external_functions'] or result['unconnected_imports'] or result['unconnected_native_callbacks'] or any(not c['covered'] for c in result['typed_script_callback_records'])):raise SystemExit(1)
