#!/usr/bin/env python3
"""Emit the skill callback table, its phase lists and its ABI bridge.

Most of the D_EFFECTS skill entry points are one shape: hand the sequence object
plus one or two constants to a shared battle action routine, after handling any
cue phases the skill has. The constants are an effect script table and a hit
descriptor, and some rows pick the descriptor by the acting actor's facing.
That is data, so it is generated as data from the recorded instruction bytes.

Reads tools/parallel/D_EFFECTS/inventory.json and, for the bodies that also have
cue phases, the recorded disassembly. Writes into src/visual_effects/:
skill_callback_table.inc, skill_callback_phases.inc and d_effects_bridge.cpp.

Note: this reads the generated bodies in src/recovered/, which no longer contain
the entries already registered in tools/native_reconstructions.json. Rerun
inventory.py from the baseline commit if the inventory has to be rebuilt.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parent))
ROOT = HERE.parents[3]
CACHE = Path('/Users/ysim/repo/fsb/decompiled/functions')

from analyze_presentation import describe  # noqa: E402
from inventory import load_disassembly  # noqa: E402

ACTIVE_ACTOR_POINTER = '0x8059f0'
FACING_FIELD = 0x110
SKILL_ID = re.compile(r'skill id `(0x[0-9a-f]+)`(?: / `([^`]*)`)?')

KIND = {
    'play_cue': 'PhaseAction::Kind::PlayCue',
    'timing_mode': 'PhaseAction::Kind::SetTimingMode',
    'timing_mode_then_dispatch': 'PhaseAction::Kind::SetTimingModeThenDispatch',
}

BRIDGE = '''{header}
//
// Temporary ABI boundary for callers that still use the original execution
// model. The skill callbacks are gone from the generated product code; what is
// left here is reading the incoming argument and clearing it.
//
// The action routines the table forwards to belong to the battle scope and are
// still generated, so they keep running through the existing call path. Each is
// bound by its original address, which keeps the remaining dependency visible
// instead of hiding it inside a reconstructed body.
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/original_audio.hpp"
#include "fsb_core/visual_effects/skill_callbacks.hpp"

namespace fsb::core {{

bool RecoveredBattle::dispatch_d_effects(Address entry) {{
    switch (entry) {{
{labels}
        break;
    default:
        return false;
    }}
    visual_effects::SkillCallbacks callbacks(memory_);
    callbacks.play_cue = [this](unsigned cue) {{ return callback(original_audio::play_cue, {{cue}}); }};
    for (const auto& row : visual_effects::SkillCallbacks::rows()) {{
        const auto pushed = visual_effects::pushed_arguments(row);
        callbacks.bind(row.routine, [this, routine = row.routine, pushed](
                           Address sequence, std::int32_t first, std::int32_t second) {{
            std::vector<std::uint32_t> args{{sequence, std::uint32_t(first), std::uint32_t(second)}};
            args.resize(pushed);
            return callback(routine, args);
        }});
    }}
    result(callbacks.invoke(entry, argument(0)), 4);
    return true;
}}

}}
'''


def identity(source):
    """Skill id and label the recovery recorded for this callback row."""
    path = CACHE / source
    if not path.exists():
        return None, None
    match = SKILL_ID.search(path.read_text(errors='replace'))
    return (match.group(1), match.group(2)) if match else (None, None)


def argument(entry, kind, value, chain=None):
    """Render one forwarded constant."""
    if kind == 'constant':
        return f'{{DispatchArgument::Kind::Constant, {value}}}'
    if kind == 'indexed_table':
        expected = [f'dword ptr [{ACTIVE_ACTOR_POINTER}]', f'dword ptr [eax + {hex(FACING_FIELD)}]']
        if chain != expected:
            raise SystemExit(f'{entry}: unexpected facing lookup {chain}')
        return f'{{DispatchArgument::Kind::ActiveFacingTable, {value}}}'
    raise SystemExit(f'{entry}: unexpected argument kind {kind}')


def collect(inventory):
    """Every entry point this generator covers, as (entry, routine, args, phases)."""
    rows = []
    logic = [r for r in inventory['records'].values() if not r['forwarder']]
    disassembly = load_disassembly([{k: r[k] for k in
                                     ('entry', 'source', 'instructions', 'instruction_sha256', 'generated_file')}
                                    for r in logic])
    for record in inventory['records'].values():
        forwarder = record['forwarder']
        if forwarder:
            args = forwarder['arguments']
            if not args or args[0]['kind'] != 'incoming_argument' or args[0]['index'] != 0:
                continue  # 45d89c is reached with a constant callback: a spawn helper, not a skill row.
            rows.append((record, forwarder['target'], args[1:], []))
            continue
        described = describe(disassembly[record['entry']])
        if described:
            rows.append((record, described['routine'], described['constants'], described['phases']))
    return sorted(rows, key=lambda r: int(r[0]['entry'], 16))


def main():
    inventory = json.loads((HERE.parent / 'inventory.json').read_text())
    rows = collect(inventory)

    header = [
        '// Generated by tools/parallel/D_EFFECTS/generate_skill_callbacks.py.',
        '// Evidence: the original instruction bytes recorded in the battle manifest.',
        f"// Original EXE SHA256: {inventory['source_sha256']}",
        '// Edit the generator, not this file.',
    ]
    table, phase_lists, labels = [], [], []
    for record, routine, constants, phases in rows:
        entry = record['entry']
        rendered = []
        for constant in constants:
            if isinstance(constant, dict):
                rendered.append(argument(entry, constant['kind'],
                                         constant.get('value') or constant['table'],
                                         constant.get('index_chain')))
            else:
                rendered.append(argument(entry, constant['kind'], constant['value']))
        while len(rendered) < 2:
            rendered.append('{DispatchArgument::Kind::Absent, 0}')

        name = f'phases_{entry[2:]}'
        if phases:
            body = ', '.join(f'{{{p["phase"]}, {KIND[p["action"]]}, {hex(p["value"])}}}' for p in phases)
            phase_lists.append(f'constexpr PhaseAction {name}[] = {{{body}}};')
        comment = record['name']
        skill, label = identity(record['source'])
        if skill:
            comment += f' [skill {skill}{" " + label if label else ""}]'
        table.append(f'{{{entry}, {routine}, {{{rendered[0]}, {rendered[1]}}}, '
                     f'{name if phases else "nullptr"}, {len(phases)}}}, // {comment}')
        labels.append(f'    case {entry}u:')

    destination = ROOT / 'src/visual_effects'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'skill_callback_table.inc').write_text('\n'.join(header + table) + '\n')
    (destination / 'skill_callback_phases.inc').write_text('\n'.join(header + phase_lists) + '\n')
    # tools/audit_port_coverage.py checks that the file a registration names
    # really lists every entry, and it reads only .cpp text, so the labels have
    # to live in the bridge itself.
    (destination / 'd_effects_bridge.cpp').write_text(BRIDGE.format(
        header='\n'.join(header).replace('skill_callback_table.inc', 'd_effects_bridge.cpp'),
        labels='\n'.join(labels)))
    with_phases = sum(1 for _, _, _, p in rows if p)
    print(f'skill callback rows {len(rows)} ({with_phases} with cue phases)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
