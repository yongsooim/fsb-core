#!/bin/sh
# Register entries, regenerate, rebuild and run the whole suite.
# Usage: register.sh <entry>=<implementation> ...
set -e
cd "$(dirname "$0")/../../.."
ORACLE=/Users/ysim/Documents/Codex/2026-09-08/repo-fsb-x20/work/x86-oracle-env/bin/python
DECOMPILED=/Users/ysim/repo/fsb/decompiled/functions
python3 - "$@" <<'PY'
import json, sys
from pathlib import Path
p = Path('handoff/B_COMBAT/registrations.json')
d = json.loads(p.read_text())
for pair in sys.argv[1:]:
    entry, implementation = pair.split('=', 1)
    d['entries'][entry] = {'implementation': implementation, 'bridge': 'b_combat_bridge.cpp',
                           'stage': 'native_logic_with_legacy_callers', 'worker': 'B_COMBAT'}
p.write_text(json.dumps(d, indent=2) + '\n')
print('fragment holds', len(d['entries']))
PY
python3 tools/parallel/B_COMBAT/merge_registrations.py tools/native_reconstructions.json handoff/B_COMBAT/registrations.json
PYTHONPATH=tools "$ORACLE" tools/translate_battle.py assets/FLYINGSB.EXE "$DECOMPILED" . \
  | python3 -c "import json,sys;d=json.loads(sys.stdin.read());print({k:d[k] for k in ['emitted_functions','native_reconstructions']})"
cmake -S . -B build-worker -DCMAKE_BUILD_TYPE=Release -DFSB_CORE_BUILD_TEXT_RENDERER=OFF -DFSB_CORE_BUILD_SDL_HOST=OFF >/dev/null
cmake --build build-worker -j 4 2>&1 | grep -E "error:" || true
ctest --test-dir build-worker -j 4 2>&1 | tail -3
