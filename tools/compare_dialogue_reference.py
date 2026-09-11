#!/usr/bin/env python3
"""Compare an explicitly aligned original SONA dialogue layer, not a full scene.
Usage: compare_dialogue_reference.py REFERENCE_12S_PNG DIALOGUE_MARKER010_BMP OUTPUT
The reference ZMBV decodes to BGR0; no original palette indices are assumed.
"""
from pathlib import Path
import hashlib
import json
import sys
from PIL import Image, ImageDraw


def main():
    reference_path, candidate_path, output = map(Path, sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=True)
    reference = Image.open(reference_path).convert('RGB')
    indexed = Image.open(candidate_path)
    if reference.size != (640, 480) or indexed.size != (640, 480) or indexed.mode != 'P':
        raise ValueError('expected original640x480 reference and indexed candidate')
    original_rect, candidate_rect = (104, 87, 328, 185), (104, 89, 328, 187)
    expected = reference.crop(original_rect)
    indices = list(indexed.crop(candidate_rect).getdata())
    colors = list(indexed.convert('RGB').crop(candidate_rect).getdata())
    converted = [tuple(((c >> 2) << 2) | (c >> 6) for c in color) for color in colors]
    actual = list(expected.getdata())
    tested = sum(index != 0 for index in indices)
    matched = sum(index != 0 and a == b for index, a, b in zip(indices, actual, converted))
    candidate = Image.new('RGB', expected.size); candidate.putdata(converted)
    difference = Image.new('RGB', expected.size)
    difference.putdata([tuple(abs(x-y) for x,y in zip(a,b)) if index else (0,0,0) for index,a,b in zip(indices,actual,converted)])
    contact = Image.new('RGB',(672,136),(12,16,28)); draw = ImageDraw.Draw(contact)
    for i,(label,im) in enumerate([('Original recording',expected),('Core + capture color model',candidate),('Difference on core ink',difference)]):
        draw.text((i*224+5,8),label,fill='white'); contact.paste(im,(i*224,30))
    contact.save(output/'dialogue-reference-comparison.png')
    report = {'scope':'Isolated original SONA first chunk with explicit anchor/input fixture; not a full Event0 run',
              'reference_seek_seconds':12,'reference_rect_half_open':original_rect,'candidate_rect_half_open':candidate_rect,
              'vertical_alignment_pixels':-2,'mask':'candidate palette index !=0; transparent backdrop excluded',
              'presentation_transform':'((channel >>2)<<2)|(channel >>6)',
              'matched_pixels':matched,'tested_pixels':tested,
              'reference_png_sha256':hashlib.sha256(reference_path.read_bytes()).hexdigest(),
              'candidate_bmp_sha256':hashlib.sha256(candidate_path.read_bytes()).hexdigest(),
              'reference_decoder_format':'bgr0 (do not treat an ffmpeg pal8 conversion as original indices)',
              'full_frame_parity':False,'event0_complete':False}
    (output/'dialogue-reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'original_dialogue_pixels={matched}/{tested}; full_frame_parity=false')
    return 0 if tested and matched==tested else 1


if __name__=='__main__':
    raise SystemExit(main())
