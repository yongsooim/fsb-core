#!/usr/bin/env python3
"""Run the original tokenizer machine code. Decompiled tag labels are not an oracle."""
from pathlib import Path
import hashlib,json,re,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
from prepare_event0 import PE

def main(exe,output):
    pe=PE(exe);sha=hashlib.sha256(pe.data).hexdigest()
    if sha!='710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454':raise ValueError('wrong EXE')
    source=(Path(__file__).parent.parent/'src/markup.cpp').read_text()
    fixed=source.split('static constexpr Fixed fixed[] = {',1)[1].split('};',1)[0]
    inputs=['<'+tag+'>' for tag in re.findall(r'\{"([^"]+)"',fixed)]
    inputs+=['','@','@@','|',';','^','*','/','<possub_U>','<possub_D>','<possub_L>',
             '<F0B>','<F3T>','<Pos8H>','<W=20>','<W-12>','<D8>','<CS41>','<INDENT0>',
             '<$010>','<$SON5:3001>','<$SON5:3035>','<$SONA>','<#?>','<#>','<Dir2>','<Dir8:5>',
             '<ids_TEST>','<unknown>','<HMAX3>','<HMIN2>','<HEIGHT4>','<BGM8>','<SE90>',
             '<EventTitle_오행산 습격>','<EventTitle_>']
    # Every actual markup spelling in the five original Event0 dialogue streams.
    for address in (0x61ecc0,0x61ecc2,0x61ed85,0x61ef9d,0x61f03a):inputs+=re.findall(r'<[^>]*>',pe.string(address,'cp949'))
    inputs=list(dict.fromkeys(inputs))
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x600000);u.mem_map(0x1000000,0x10000)
    for _,va,n,off in pe.sections:u.mem_write(pe.base+va,pe.data[off:off+n])
    output_bytes=bytearray(b'FSBMARK1'+struct.pack('<I',len(inputs)));readable=[]
    for text in inputs:
        data=text.encode('cp949');u.mem_write(0x1001000,data+b'\0');u.mem_write(0x1001100,struct.pack('<III',1,0,0));u.reg_write(UC_X86_REG_ESP,0x100f000)
        u.mem_write(0x100f000,struct.pack('<5I',0x1000000,0x1001000,0x1001100,0x1001104,0x1001108));u.emu_start(0x40c632,0x1000000,count=100000)
        if u.reg_read(UC_X86_REG_EIP)!=0x1000000:raise RuntimeError('tokenizer did not return')
        code=u.reg_read(UC_X86_REG_EAX);length,arg,aux=struct.unpack('<III',u.mem_read(0x1001100,12))
        # The original leaves scratch/pointer debris in arguments its runtime
        # consumers never read. The portable token API normalizes these to0.
        ignored=code in (1,2,3,4,5,6,7,8,9,13,30,50,53)
        if ignored:arg=0
        relative=0x1001000<=arg<=0x1001000+len(data)
        if relative:arg-=0x1001000
        output_bytes+=struct.pack('<I',len(data))+data+struct.pack('<5I',code,length,arg,aux,int(relative)|(int(ignored)<<1))
        readable.append({'input':text,'code':code,'length':length,'argument':arg,'auxiliary':aux,'argument_is_input_offset':relative,'argument_used_by_runtime':not ignored})
    out=Path(output);out.write_bytes(output_bytes);out.with_suffix('.json').write_text(json.dumps({'exe_sha256':sha,'routine':'0x40c632 actual machine code','cases':readable,'binary_sha256':hashlib.sha256(output_bytes).hexdigest()},ensure_ascii=False,indent=2)+'\n')
    print('original tokenizer cases:',len(inputs))

if __name__=='__main__':main(*sys.argv[1:])
