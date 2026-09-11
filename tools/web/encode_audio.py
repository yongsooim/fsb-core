#!/usr/bin/env python3
"""Re-encode the staged WAV resources as Opus for the browser deployment.

The game's sound is 8-bit 22 kHz PCM, which is 104 MiB of the 218 MiB a full
staged tree carries and does not compress well. Opus carries the same material
in about a fifth of that. The browser decodes it with its own codec and writes
the WAV back into the emscripten filesystem before the engine starts, so the
engine still reads exactly the resources it expects, at their original rate,
channel count and bit depth.

This is a deployment tradeoff, not a port decision: the encode is lossy, and the
native build keeps reading the original PCM.

    tools/web/encode_audio.py /tmp/web-assets /tmp/web-audio [--strip]
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

# Both directories register_runtime_assets() reads waves from.
SOURCES = ('audio', 'se_event')


def wave_format(path: Path) -> dict:
    """Read the fields the page needs to rebuild this file after decoding."""
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(f'{path} is not RIFF/WAVE')
    at, fmt, frames = 12, None, 0
    while at + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, at)
        at += 8
        if tag == b'fmt ':
            encoding, channels, rate, _bytes_per_second, align, bits = struct.unpack_from('<HHIIHH', data, at)
            if encoding != 1:
                raise ValueError(f'{path} is not PCM')
            fmt = {'channels': channels, 'rate': rate, 'bits': bits, 'align': align}
        elif tag == b'data' and fmt:
            frames = size // fmt['align']
        at += size + (size & 1)
    if not fmt or not frames:
        raise ValueError(f'{path} has no fmt/data pair')
    return {**fmt, 'frames': frames}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('staged', type=Path, help='directory produced by stage_assets.py')
    parser.add_argument('output', type=Path, help='where the .opus tree and manifest go')
    parser.add_argument('--bitrate', default='64k')
    parser.add_argument('--strip', action='store_true',
                        help='delete the WAVs from the staged tree once encoded')
    arguments = parser.parse_args()

    entries, source_bytes, encoded_bytes = [], 0, 0
    for folder in SOURCES:
        for wav in sorted((arguments.staged / folder).iterdir()) if (arguments.staged / folder).is_dir() else []:
            if not wav.is_file():
                continue
            fmt = wave_format(wav)
            relative = Path(folder) / wav.name
            opus = arguments.output / 'opus' / relative.with_suffix('.opus')
            opus.parent.mkdir(parents=True, exist_ok=True)
            # -vbr on is libopus' default; naming it keeps the command honest
            # about the bitrate being an average rather than a cap.
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav),
                            '-c:a', 'libopus', '-b:a', arguments.bitrate, '-vbr', 'on', str(opus)],
                           check=True)
            source_bytes += wav.stat().st_size
            encoded_bytes += opus.stat().st_size
            entries.append({'path': str(relative), 'opus': str(opus.relative_to(arguments.output)), **fmt})
            if arguments.strip:
                wav.unlink()

    if not entries:
        print(f'no WAV resources under {arguments.staged}', file=sys.stderr)
        return 1
    (arguments.output / 'audio-manifest.json').write_text(json.dumps(entries, indent=0))
    print(f'{len(entries)} files, {source_bytes / 1048576:.1f} MiB -> {encoded_bytes / 1048576:.1f} MiB'
          f' ({encoded_bytes / source_bytes:.0%}) in {arguments.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
