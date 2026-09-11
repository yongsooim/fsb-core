#!/usr/bin/env python3
"""Reproduce one explicitly aligned, map-only reference comparison.

Usage: compare_map_reference.py REFERENCE_12S_PNG CANDIDATE_MAP02_BMP OUTPUT_DIR
Extract the reference with: ffmpeg -ss 12 -i event0.avi -frames:v 1 reference.png
This is not an Event0 execution, camera timing, or full-frame parity test.
"""
from pathlib import Path
import hashlib
import json
import sys
from PIL import Image, ImageChops, ImageDraw


def main():
    reference_path, candidate_path, output = map(Path, sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=True)
    reference = Image.open(reference_path).convert('RGB')
    candidate = Image.open(candidate_path).convert('RGB')
    if reference.size != (640, 480) or candidate.size != (640, 480):
        raise ValueError('expected unscaled 640x480 images')
    # Original positions are preserved in the report; no inferred runtime
    # camera value is written back to the game core to force an image match.
    original_rect = (0, 335, 130, 455)
    candidate_rect = (0, 318, 130, 438)
    expected = reference.crop(original_rect)
    raw = candidate.crop(candidate_rect)
    raw_pixels = list(raw.getdata())
    # Empirical capture presentation model, separate from core palette bytes:
    # discard two low bits, then replicate the top two bits into them.
    presented = Image.new('RGB', raw.size)
    presented.putdata([tuple(((c >> 2) << 2) | (c >> 6) for c in rgb) for rgb in raw_pixels])
    expected_pixels, presented_pixels = list(expected.getdata()), list(presented.getdata())
    equal = sum(a == b for a, b in zip(expected_pixels, presented_pixels))
    count = len(expected_pixels)
    report = {
        'scope': 'One map-only background patch; spatial alignment, no runtime timing proof',
        'reference_seek_seconds': 12,
        'reference_png_sha256': hashlib.sha256(reference_path.read_bytes()).hexdigest(),
        'candidate_bmp_sha256': hashlib.sha256(candidate_path.read_bytes()).hexdigest(),
        'reference_rect_half_open': original_rect,
        'candidate_rect_half_open': candidate_rect,
        'candidate_translation_y_pixels': -17,
        'presentation_transform': '((channel >> 2) << 2) | (channel >> 6)',
        'transform_status': '6-bit DAC-style expansion inferred from capture colors; core palette remains 8-bit',
        'matched_pixels': equal, 'total_pixels': count, 'exact_rgb_match_fraction': equal / count,
        'full_frame_parity': False, 'event0_complete': False,
    }
    (output / 'map-reference-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    contact = Image.new('RGB', (780, 275), (12, 16, 28)); draw = ImageDraw.Draw(contact)
    for i, (label, im) in enumerate([('Original recording', expected), ('Core + capture color model', presented), ('Absolute difference', ImageChops.difference(expected, presented))]):
        draw.text((i * 260 + 8, 8), label, fill='white')
        contact.paste(im.resize((260, 240), Image.Resampling.NEAREST), (i * 260, 30))
    contact.save(output / 'map-reference-comparison.png')
    print(f'aligned_background_pixels={equal}/{count}; full_frame_parity=false')
    return 0 if equal == count else 1


if __name__ == '__main__':
    raise SystemExit(main())
