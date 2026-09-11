#!/usr/bin/env python3
"""Compare actual battle tile emitters with C++ on a real replay snapshot."""
from pathlib import Path
import hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
from prepare_event0 import PE

def main(exe,directory,output):
    pe=PE(exe);directory=Path(directory);rows=[]
    for name,entry in [('background',0x45555b),('foreground',0x4577d6)]:
        before=(directory/f'{name}-before.bin').read_bytes();expected=(directory/f'{name}-after.bin').read_bytes()
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
        for _,va,size,offset in pe.sections:u.mem_write(pe.base+va,pe.data[offset:offset+size])
        u.mem_write(0x4a5000,before);u.reg_write(UC_X86_REG_ESP,0x100f000);u.mem_write(0x100f000,struct.pack('<III',0x1000000,0,1))
        u.emu_start(entry,0x1000000,count=20000000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('original emitter did not return')
        actual=bytes(u.mem_read(0x4a5000,len(expected)));diff=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
        rows.append({'name':name,'entry':hex(entry),'compared_bytes':len(expected),'mismatches':len(diff),'first_differences':[{'address':hex(0x4a5000+i),'original':actual[i],'cpp':expected[i]} for i in diff[:16]]})
    report={'exe_sha256':hashlib.sha256(pe.data).hexdigest(),'scope':'background/foreground command generation from actual movement-range replay state; entire guest .data','substituted_calls':[],'results':rows,'match':all(not r['mismatches'] for r in rows)}
    Path(output).parent.mkdir(parents=True,exist_ok=True);Path(output).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));return 0 if report['match'] else 1
if __name__=='__main__':raise SystemExit(main(*sys.argv[1:]))
