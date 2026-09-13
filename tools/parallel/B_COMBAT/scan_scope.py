#!/usr/bin/env python3
"""Group the B_COMBAT original entries by their actual direct-call edges.

Reads the generated recovered bodies, whose comments carry the original
instruction bytes and mnemonics, and reports for each assigned entry the
functions it calls. Bundles start at the leaves so a reconstruction never
depends on an unreconstructed callee inside the same scope.
"""
import json,re,sys
from collections import defaultdict
from pathlib import Path

ENTRY=re.compile(r'^void RecoveredBattle::fn_([0-9a-f]+)\(\)\{')
CALL=re.compile(r'//\s*[0-9a-f]+\s+call\s+(?:0x([0-9a-f]+)|(.+))$')
FLOAT=re.compile(r'//\s*[0-9a-f]+\s+(f[a-z0-9]+)\s')

def scan(root):
    bodies={}
    for path in sorted((root/'src/recovered').glob('*.cpp')):
        current=None
        for line in path.read_text().splitlines():
            found=ENTRY.match(line)
            if found:current=int(found.group(1),16);bodies[current]={'file':path.name,'calls':[],'indirect':0,'float':0,'instructions':0}
            elif current is not None:
                if '// ' in line and re.search(r'//\s*[0-9a-f]+\s+\S',line):bodies[current]['instructions']+=1
                call=CALL.search(line.strip())
                if call:
                    if call.group(1):bodies[current]['calls'].append(int(call.group(1),16))
                    else:bodies[current]['indirect']+=1
                if FLOAT.search(line.strip()):bodies[current]['float']+=1
    return bodies

def main(root,scopes,output):
    root=Path(root);data=json.loads(Path(scopes).read_text())
    owner={}
    for name,worker in data['workers'].items():
        for entry in worker['entries']:owner[int(entry['entry'],16)]=name
    mine=[int(e['entry'],16) for e in data['workers']['B_COMBAT']['entries']]
    bodies=scan(root)
    native=set(int(a,16) for a in json.loads((root/'tools/native_reconstructions.json').read_text())['entries'])
    boundaries=set(int(a,16) for a in json.loads((root/'tools/original_service_boundaries.json').read_text())['entries'])
    report={}
    for entry in mine:
        body=bodies.get(entry)
        if body is None:report[hex(entry)]={'missing_body':True};continue
        calls=sorted(set(body['calls']))
        report[hex(entry)]={
            'file':body['file'],'instructions':body['instructions'],'indirect_calls':body['indirect'],'x87_instructions':body['float'],
            'calls':[{'target':hex(t),'owner':'native' if t in native else 'service_boundary' if t in boundaries else owner.get(t,'integration')} for t in calls],
        }
    Path(output).write_text(json.dumps(report,indent=1)+'\n')
    leaves=[a for a in report if not report[a].get('missing_body') and not report[a]['calls'] and not report[a]['indirect_calls']]
    internal=[a for a in report if not report[a].get('missing_body') and report[a]['calls'] and all(c['owner'] in ('B_COMBAT','native','service_boundary') for c in report[a]['calls']) and not report[a]['indirect_calls']]
    print(json.dumps({'entries':len(mine),'leaf_entries':len(leaves),'b_combat_only_callers':len(internal),
        'x87_entries':sum(1 for a in report if report[a].get('x87_instructions')),
        'indirect_call_entries':sum(1 for a in report if report[a].get('indirect_calls'))},indent=1))

if __name__=='__main__':main(*sys.argv[1:])
