#!/usr/bin/env python3
"""Show each original call site of an entry with the instructions that follow.

The return-value mask of a reconstruction has to come from what callers read,
so print enough of the continuation to see whether EAX, AX or AL is used.
"""
import re,sys
from pathlib import Path
def main(after,*entries):
    after=int(after)
    lines=[]
    for path in sorted(Path('src/recovered').glob('*.cpp')):
        for line in path.read_text().splitlines():
            if line.startswith('L') and ': { // ' in line:
                label,text=line.split(': { // ',1);lines.append((path.name,label[1:],text))
    for e in entries:
        target=f'call 0x{int(e,16):x}'
        print(f'===== callers of 0x{int(e,16):x}')
        for i,(f,label,text) in enumerate(lines):
            if text.endswith(' '+target) or text.endswith(target):
                print(f'-- {f} {label}')
                for j in range(i,min(i+after+1,len(lines))):print(f'   {lines[j][1]:>8s}  {lines[j][2]}')
if __name__=='__main__':main(*sys.argv[1:])
