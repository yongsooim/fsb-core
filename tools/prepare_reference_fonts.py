#!/usr/bin/env python3
"""Read-only font extraction from the supplied original Windows FAT16 disk image."""
from pathlib import Path
import struct
import sys
if len(sys.argv)!=3:raise SystemExit('Usage: prepare_reference_fonts.py WINDOWS_FAT16_DISK_IMAGE OUTPUT_DIR')
image=Path(sys.argv[1])
f=image.open('rb');mbr=f.read(512);base=struct.unpack_from('<I',mbr,454)[0]*512
f.seek(base);b=f.read(512)
u16=lambda a:struct.unpack_from('<H',b,a)[0]
bps=u16(11);spc=b[13];reserved=u16(14);nfats=b[16];root_entries=u16(17);spf=u16(22)
root_start=base+(reserved+nfats*spf)*bps;data_start=root_start+((root_entries*32+bps-1)//bps)*bps
f.seek(base+reserved*bps);fat=f.read(spf*bps)
def chain(cluster):
 result=bytearray();seen=set()
 while cluster>=2 and cluster<0xfff8:
  if cluster in seen:raise ValueError('FAT cycle')
  seen.add(cluster);f.seek(data_start+(cluster-2)*spc*bps);result.extend(f.read(spc*bps));cluster=struct.unpack_from('<H',fat,cluster*2)[0]
 return bytes(result)
def entries(data):
 for at in range(0,len(data),32):
  e=data[at:at+32]
  if e[0]==0:break
  if e[0]==0xe5 or e[11]==15 or e[11]&8:continue
  name=e[:8].decode('ascii','replace').rstrip();ext=e[8:11].decode('ascii','replace').rstrip();name+=('.'+ext if ext else '')
  yield name,e[11],struct.unpack_from('<H',e,26)[0],struct.unpack_from('<I',e,28)[0]
f.seek(root_start);root=f.read(root_entries*32)
current=root
for component in ['WINDOWS','FONTS']:
 matches=[x for x in entries(current) if x[0].upper()==component and x[1]&16]
 if not matches:raise ValueError(f'missing {component}: {[x[0] for x in entries(current)]}')
 current=chain(matches[0][2])
fonts=[x for x in entries(current) if x[0].upper().endswith(('.TTF','.TTC','.FON'))]
print('Font directory found; extracting only Gulim/Batang collections')
if __name__=='__main__':
 import sys,json,hashlib
 if len(sys.argv)==3:
  out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True);manifest=[]
  for name,attr,cluster,size in fonts:
   if name.upper().startswith(('GULIM','BATANG','DOTUM','GUNGS')):
    data=chain(cluster)[:size];(out/name.lower()).write_bytes(data)
    manifest.append({'path':name.lower(),'original_path':'WINDOWS/FONTS/'+name,'size':size,'sha256':hashlib.sha256(data).hexdigest(),'source_image':str(image)})
  table=bytearray()
  for pair in range(65536):
   value=0
   if pair>=0x8000:
    try:
     decoded=bytes([pair>>8,pair&255]).decode('cp949')
     if len(decoded)==1:value=ord(decoded)
    except UnicodeDecodeError:pass
   table.extend(struct.pack('<H',value))
  (out/'cp949.bin').write_bytes(table)
  metadata={'usage':'Existing local Windows game-reference installation; no public redistribution.','source_image_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),'fonts':manifest,'cp949_mapping':{'path':'cp949.bin','source':'Python standard-library CP949 codec, complete16-bit pair lookup','sha256':hashlib.sha256(table).hexdigest()}}
  (out/'reference-font-provenance.json').write_text(json.dumps(metadata,indent=2)+'\n')
