#!/usr/bin/env python3
"""Report the stack bytes each assigned entry removes on return."""
import json,re,sys
from pathlib import Path
def main(scopes):
    entries={int(e['entry'],16) for e in json.loads(Path(scopes).read_text())['workers']['B_COMBAT']['entries']}
    rets={}
    for path in sorted(Path('src/recovered').glob('*.cpp')):
        current=None
        for line in path.read_text().splitlines():
            head=re.match(r'^void RecoveredBattle::fn_([0-9a-f]+)\(\)\{',line)
            if head:current=int(head.group(1),16)
            elif current in entries and ': { // ' in line:
                text=line.split(': { // ',1)[1]
                found=re.match(r'^[0-9a-f]+\s+ret\s*(0x[0-9a-f]+|\d*)\s*$',text)
                if found:rets.setdefault(current,set()).add(int(found.group(1),0) if found.group(1) else 0)
    print(json.dumps({hex(a):sorted(v) for a,v in sorted(rets.items())},indent=0))
if __name__=='__main__':main(*sys.argv[1:])
