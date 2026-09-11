#!/usr/bin/env python3
"""Compare a replay to the original recording without resizing or time warping.

The supplied fixed offset is an explicit hypothesis, not recovered input data.
Exact state matches are listed separately from chronological sample comparisons.
ffmpeg must emit full-size BGR0: its small nearest-neighbour RGB output changes
some channel values on this recording, invalidating exact pixel comparisons.
"""
import argparse
import csv
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image


def read_frame(pipe, size):
    data = bytearray()
    while len(data) < size:
        chunk = pipe.read(size - len(data))
        if not chunk:
            if data:
                raise ValueError('truncated decoded reference frame')
            return None
        data.extend(chunk)
    return data


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('core', type=Path)
    parser.add_argument('recording', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('--offset-ms', type=int, required=True)
    parser.add_argument('--reference-end-seconds', type=float, default=66)
    args = parser.parse_args()
    info = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height,r_frame_rate', '-of', 'json',
        str(args.recording)]))['streams'][0]
    if (info['width'], info['height']) != (640, 480):
        raise ValueError('expected original 640x480 recording')
    fps = Fraction(info['r_frame_rate'])
    rows = list(csv.DictReader((args.core / 'captures.tsv').open(), delimiter='\t'))
    runtime = json.loads((args.core / 'runtime.json').read_text())
    if len(rows) != runtime['captures']:
        raise ValueError('capture manifest and runtime count disagree')
    samples = []
    states = {}
    wanted_frames = {}
    for row in rows:
        rgb = np.array(Image.open(args.core / row['file']).convert('RGB'))
        rgb = ((rgb >> 2) << 2) | (rgb >> 6)
        raw = np.zeros((480, 640, 4), dtype=np.uint8)
        raw[:, :, :3] = rgb[:, :, ::-1]
        digest = hashlib.sha256(raw).hexdigest()
        sample = {'core_ms': int(row['ms']), 'core_file': row['file'],
                  'root_pc': row['root_pc'], 'bgr0_sha256': digest}
        samples.append(sample)
        state = states.setdefault(digest, {'core_samples': [], 'reference_frames': []})
        state['core_samples'].append(len(samples) - 1)
        # Only nontrivial frames count as state-match evidence.
        if 'nontrivial' not in state:
            colors = Image.fromarray(rgb).getcolors(maxcolors=256)
            state['nontrivial'] = colors is None or len(colors) >= 16
        number = round(Fraction(sample['core_ms'] + args.offset_ms, 1000) * fps)
        if number >= 0:
            wanted_frames.setdefault(number, []).append((sample, rgb))

    limit = round(args.reference_end_seconds * fps)
    process = subprocess.Popen([
        'ffmpeg', '-v', 'error', '-i', str(args.recording), '-map', '0:v:0',
        '-frames:v', str(limit), '-f', 'rawvideo', '-pix_fmt', 'bgr0', '-'],
        stdout=subprocess.PIPE)
    count = 0
    try:
        for number in range(limit):
            data = read_frame(process.stdout, 640 * 480 * 4)
            if data is None:
                break
            count += 1
            # The fourth BGR0 byte is padding, not a color/alpha observation.
            pixels = np.frombuffer(data, np.uint8).reshape(480, 640, 4)
            pixels[:, :, 3] = 0
            digest = hashlib.sha256(data).hexdigest()
            if digest in states and states[digest]['nontrivial']:
                states[digest]['reference_frames'].append(number)
            if number in wanted_frames:
                reference = pixels[:, :, 2::-1]
                for sample, core in wanted_frames.pop(number):
                    delta = core.astype(np.int16) - reference.astype(np.int16)
                    exact = int(np.count_nonzero(np.all(delta == 0, axis=2)))
                    sample.update(reference_frame=number,
                                  reference_ms=float(number * 1000 / fps),
                                  matched_pixels=exact, tested_pixels=640 * 480,
                                  exact_pixel_fraction=exact / (640 * 480),
                                  mean_absolute_channel_error=float(np.mean(np.abs(delta))))
    finally:
        process.stdout.close()
        status = process.wait()
    if status:
        raise RuntimeError(f'ffmpeg exited with {status}')

    matched = [dict(bgr0_sha256=digest, **state)
               for digest, state in states.items() if state['reference_frames']]
    windows = []
    for begin in range(0, runtime['last_ms'], 4000):
        group = [s for s in samples if begin <= s['core_ms'] < begin + 4000 and 'matched_pixels' in s]
        if group:
            windows.append({'core_start_ms': begin, 'core_end_ms': begin + 4000,
                            'samples': len(group),
                            'mean_exact_pixel_fraction': float(np.mean([s['exact_pixel_fraction'] for s in group])),
                            'mean_absolute_channel_error': float(np.mean([s['mean_absolute_channel_error'] for s in group]))})
    report = {'scope': 'fixed-offset chronological full-frame samples plus separate exact visual-state matches',
              'input_provenance': 'see replay input fixture; inferred inputs are not original observations',
              'full_timeline_parity_proven': False,
              'recording_sha256': file_hash(args.recording),
              'replay_inputs_sha256': file_hash(args.core / 'inputs.tsv'),
              'reference_fps': str(fps), 'decoded_reference_frames': count,
              'reference_end_seconds_requested': args.reference_end_seconds,
              'fixed_offset_ms': args.offset_ms,
              'geometry_transform': 'none',
              'state_hash_format': 'BGR0 with non-color fourth byte normalized to zero',
              'presentation_transform': '((channel>>2)<<2)|(channel>>6)',
              'core_runtime': runtime, 'chronological_windows': windows,
              'chronological_samples': samples,
              'unique_nontrivial_exact_states': len(matched), 'exact_states': matched}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'compared_samples': sum('matched_pixels' in s for s in samples),
                      'unique_nontrivial_exact_states': len(matched), 'windows': windows}))


if __name__ == '__main__':
    main()
