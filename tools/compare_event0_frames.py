#!/usr/bin/env python3
"""Find exact whole-frame matches against original capture samples.

Input reference PNGs are ffmpeg fps=4 samples, numbered from1. Core BMPs are
numbered from0. No geometry alignment, cropping or screenshot-derived assets.
Only the empirically verified64-level VGA presentation transform is applied.
Matching repeated scene states is not a reconstruction of the original inputs.
"""
from pathlib import Path
import bisect,csv,hashlib,json,sys
import numpy as np
from PIL import Image,ImageDraw

def main(core_path,reference_path,output_path):
    core=Path(core_path);reference=Path(reference_path);out=Path(output_path);out.parent.mkdir(parents=True,exist_ok=True)
    runtime=json.loads((core/'runtime.json').read_text())
    frames=list(csv.DictReader((core/'frames.tsv').open(),delimiter='\t'));times=[int(r['ms']) for r in frames]
    capture_rows=list(csv.DictReader((core/'captures.tsv').open(),delimiter='\t')) if (core/'captures.tsv').exists() else None
    if capture_rows is not None and len(capture_rows)!=runtime['captures']:raise ValueError('capture manifest count mismatch')
    references={};levels={(i<<2)|(i>>4) for i in range(64)};non_dac=0
    #0..66s includes the recorded Event0 portion; later events are excluded.
    for index in range(264):
        path=reference/f'frame-{index+1:04d}.png'
        if not path.exists():break
        rgb=np.array(Image.open(path).convert('RGB'));non_dac+=sum(int(v) not in levels for v in np.unique(rgb))
        digest=hashlib.sha256(rgb.tobytes()).hexdigest();references.setdefault(digest,[]).append(index)
    matches=[];seen=set();proof=[]
    for index in range(runtime['captures']):
        path=core/f'frame-{index:04d}.bmp';rgb=np.array(Image.open(path).convert('RGB'));rgb=((rgb>>2)<<2)|(rgb>>6)
        digest=hashlib.sha256(rgb.tobytes()).hexdigest()
        if digest not in references or digest in seen:continue
        # Exclude uniform/near-empty black/white screens from the exact-match count.
        if np.unique(rgb.reshape(-1,3),axis=0).shape[0]<16:continue
        seen.add(digest);stamp=int(capture_rows[index]['ms']) if capture_rows is not None else index*250
        position=bisect.bisect_left(times,stamp);state=frames[position]
        row={'core_file':path.name,'core_ms':times[position],'core_root_pc':state['root_pc'],
             'reference_sample_indices':references[digest],'reference_bucket_seconds':[i/4 for i in references[digest]],
             'rgb_sha256':digest,'matched_pixels':640*480,'tested_pixels':640*480,'geometry_translation':[0,0]}
        matches.append(row)
        if len(proof)<4 and times[position]>=7000:proof.append((rgb,np.array(Image.open(reference/f'frame-{references[digest][0]+1:04d}.png').convert('RGB')),row))
    report={'scope':'exact whole-frame image matches at corresponding visual states; original input chronology is not reconstructed',
            'core_runtime':runtime,'reference_sampling':'ffmpeg fps=4; bucket labels are approximate sampling times',
            'presentation_transform':'((channel>>2)<<2)|(channel>>6)','reference_non_DAC6_channel_values':non_dac,
            'unique_nontrivial_exact_frames':len(matches),'matches':matches,'full_timeline_parity_proven':False}
    out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    if proof:
        image=Image.new('RGB',(1280,len(proof)*506),'#242424');draw=ImageDraw.Draw(image)
        for i,(a,b,row) in enumerate(proof):
            y=i*506;draw.text((8,y+7),f'Core {row["core_ms"]}ms',fill='white');draw.text((648,y+7),f'Original {row["reference_bucket_seconds"][0]}s bucket - 307200/307200 pixels match',fill='white')
            image.paste(Image.fromarray(a),(0,y+26));image.paste(Image.fromarray(b),(640,y+26))
        image.save(out.with_suffix('.png'))
    print('unique nontrivial whole frames matching original:',len(matches),'non-DAC6 reference levels:',non_dac)

if __name__=='__main__':
    if len(sys.argv)!=4:raise SystemExit('usage: compare_event0_frames.py CORE_OUTPUT REFERENCE_4FPS_PNGS OUTPUT_STEM')
    main(*sys.argv[1:])
