#!/usr/bin/env python3
"""Build everything a static host needs to serve the browser port.

Stages the assets the requested boundary opens, re-encodes their sound as Opus,
splits what is left into emscripten packages small enough for a host with a
per-file limit, and copies the module and the page next to them.

    tools/web/package_deploy.py assets build-web-nodata /tmp/deploy \\
        --through-event -1 --limit 24

The module has to come from a build configured with -DFSB_WEB_ASSETS= so that
nothing is embedded in it; the packages here are what gets mounted instead.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stage_assets import stage  # noqa: E402

MODULE_FILES = ('fsb_event0_web.js', 'fsb_event0_web.wasm')
PAGE_FILES = ('index.html', 'audio-worklet.js')


def emscripten_python() -> str:
    """emscripten's own tools need 3.10 or newer, which the system python on a
    Mac is not. emsdk_env exports the interpreter it installed for exactly this."""
    return os.environ.get('EMSDK_PYTHON') or sys.executable


def file_packager() -> Path:
    """emscripten ships the packager next to emcc; emsdk exports its path."""
    root = os.environ.get('EMSDK')
    candidates = [Path(root) / 'upstream/emscripten/tools/file_packager.py'] if root else []
    emcc = shutil.which('emcc')
    if emcc:
        candidates.append(Path(emcc).resolve().parent / 'tools/file_packager.py')
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit('file_packager.py not found; run this inside an activated emsdk')


def groups(staged: Path, limit: int) -> list[list[Path]]:
    """Greedy bin packing, biggest first, so no package crosses the limit."""
    files = sorted((p for p in staged.rglob('*') if p.is_file()),
                   key=lambda p: p.stat().st_size, reverse=True)
    packed: list[tuple[int, list[Path]]] = []
    for path in files:
        size = path.stat().st_size
        if size > limit:
            raise SystemExit(f'{path} is {size / 1048576:.1f} MiB, over the {limit / 1048576:.0f} MiB limit')
        for index, (total, members) in enumerate(packed):
            if total + size <= limit:
                packed[index] = (total + size, members + [path])
                break
        else:
            packed.append((size, [path]))
    return [members for _total, members in packed]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('assets', type=Path)
    parser.add_argument('module', type=Path, help='build directory holding the assets-free module')
    parser.add_argument('output', type=Path)
    parser.add_argument('--through-event', type=int, default=-1,
                        help='-1 follows the whole original event chain')
    parser.add_argument('--limit', type=int, default=24, help='package size limit in MiB')
    parser.add_argument('--bitrate', default='64k')
    parser.add_argument('--work', type=Path, default=Path('/tmp/fsb-deploy-staging'))
    arguments = parser.parse_args()

    page = Path(__file__).resolve().parent
    staged = arguments.work
    boundary = arguments.through_event if arguments.through_event >= 0 else 168
    report = stage(arguments.assets, staged, boundary)
    if report['missing']:
        print(f"missing {len(report['missing'])}: {report['missing'][:5]}", file=sys.stderr)
        return 1

    shutil.rmtree(arguments.output, ignore_errors=True)
    arguments.output.mkdir(parents=True)
    subprocess.run([sys.executable, str(page / 'encode_audio.py'), str(staged),
                    str(arguments.output), '--bitrate', arguments.bitrate, '--strip'], check=True)

    packages = []
    limit = arguments.limit * 1048576
    for index, members in enumerate(groups(staged, limit)):
        name = f'pkg{index}'
        preloads = []
        for path in members:
            preloads += ['--preload', f'{path}@/assets/{path.relative_to(staged)}']
        subprocess.run([emscripten_python(), str(file_packager()), f'{name}.data', '--js-output=' + f'{name}.js']
                       + preloads, check=True, cwd=arguments.output)
        packages.append(f'{name}.js')

    for name in MODULE_FILES:
        shutil.copyfile(arguments.module / name, arguments.output / name)
    for name in PAGE_FILES:
        shutil.copyfile(page / name, arguments.output / name)
    # The page ships with both lists empty so a development build asks a static
    # host for nothing it never generated. Fill them in for this deployment.
    marker = 'const PACKAGES = [], AUDIO_MANIFEST = null;'
    source = (arguments.output / 'index.html').read_text()
    if marker not in source:
        raise SystemExit(f'index.html no longer contains: {marker}')
    (arguments.output / 'index.html').write_text(source.replace(
        marker, f'const PACKAGES = {json.dumps(packages)}, AUDIO_MANIFEST = "audio-manifest.json";'))

    total = sum(p.stat().st_size for p in arguments.output.rglob('*') if p.is_file())
    print(f'{arguments.output}: {len(packages)} packages, {total / 1048576:.1f} MiB total')
    return 0


if __name__ == '__main__':
    sys.exit(main())
