#!/usr/bin/env python3
"""Print the original instructions of an assigned entry from the generated body."""
import re,sys
from pathlib import Path
INSTRUCTION=re.compile(r'^([0-9a-f]{2})+\s{2}\S')
def main(*entries):
    want={int(e,16) for e in entries};found={}
    for path in sorted(Path('src/recovered').glob('*.cpp')):
        current=None
        for line in path.read_text().splitlines():
            head=re.match(r'^void RecoveredBattle::fn_([0-9a-f]+)\(\)\{',line)
            if head:current=int(head.group(1),16)
            elif current in want and line.startswith('L') and '// ' in line:
                label,text=line.split(': { // ',1)
                if INSTRUCTION.match(text):found.setdefault(current,[]).append(f'{label[1:]:>8s}  {text}')
    for e in entries:
        print(f'===== 0x{int(e,16):x}')
        print('\n'.join(found.get(int(e,16),['<not found>'])))
if __name__=='__main__':main(*sys.argv[1:])
