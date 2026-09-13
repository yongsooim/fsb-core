#!/bin/sh
# Register entries, regenerate, rebuild and run the whole suite.
# Usage: register.sh <entry>=<implementation> ...
# FSB_ORACLE_PYTHON: Python executable with the x86 reference dependencies.
# FSB_DECOMPILED_DIR: decompiled function directory.
# FSB_REGISTRATIONS_FILE: existing B_COMBAT registration fragment.
# Relative input paths are resolved from the repository root.
set -e
cd "$(dirname "$0")/../../.."
: "${FSB_ORACLE_PYTHON:?Set FSB_ORACLE_PYTHON to the reference Python executable}"
: "${FSB_DECOMPILED_DIR:?Set FSB_DECOMPILED_DIR to the decompiled function directory}"
: "${FSB_REGISTRATIONS_FILE:?Set FSB_REGISTRATIONS_FILE to the registration fragment}"
test -d "$FSB_DECOMPILED_DIR"
test -f "$FSB_REGISTRATIONS_FILE"
command -v "$FSB_ORACLE_PYTHON" >/dev/null
python3 - "$@" <<'PY'
import json, os, sys
from pathlib import Path
p = Path(os.environ['FSB_REGISTRATIONS_FILE'])
d = json.loads(p.read_text())
for pair in sys.argv[1:]:
    entry, implementation = pair.split('=', 1)
    d['entries'][entry] = {'implementation': implementation, 'bridge': 'b_combat_bridge.cpp',
                           'stage': 'native_logic_with_legacy_callers', 'worker': 'B_COMBAT'}
p.write_text(json.dumps(d, indent=2) + '\n')
print('fragment holds', len(d['entries']))
PY
python3 tools/parallel/B_COMBAT/merge_registrations.py tools/native_reconstructions.json "$FSB_REGISTRATIONS_FILE"
PYTHONPATH=tools "$FSB_ORACLE_PYTHON" tools/translate_battle.py assets/FLYINGSB.EXE "$FSB_DECOMPILED_DIR" .
cmake -S . -B build-worker -DCMAKE_BUILD_TYPE=Release -DFSB_CORE_BUILD_TEXT_RENDERER=OFF -DFSB_CORE_BUILD_SDL_HOST=OFF >/dev/null
cmake --build build-worker --parallel 4
ctest --test-dir build-worker --parallel 4 --output-on-failure
