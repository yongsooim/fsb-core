#!/usr/bin/env python3
"""Regenerate the parts of the handoff that follow from the current state.

Rewrites handoff/D_EFFECTS/remaining.json, the three marked tables in
REPORT.md, and both patches, so the numbers in the handoff never drift from
what is actually registered. Everything it writes is derived from
tools/parallel/D_EFFECTS/inventory.json, handoff/D_EFFECTS/registrations.json
and the skill callback table.

The prose in REPORT.md, STATE_CONTRACT.md and CHECKS.md stays hand written.
"""
import collections
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HANDOFF = ROOT / 'handoff/D_EFFECTS'
BASELINE = '116ab45b536648a9e6c9bace77397976efb40ba5'
OWNED = ['include/fsb_core/visual_effects', 'src/visual_effects', 'tests/parallel/D_EFFECTS',
         'tools/parallel/D_EFFECTS', 'reference/parallel/D_EFFECTS']
INTEGRATION = ['CMakeLists.txt', 'include/fsb_core/recovered_battle.hpp', 'src/native_reconstructed.cpp']

# Group label for each reconstruction, in the order the report lists them.
GROUPS = [
    ('스킬 콜백 표 (전달만)', lambda k, r: k == 'SkillCallbacks' and r['forwarder']),
    ('스킬 콜백 표 (큐 단계 포함)', lambda k, r: k == 'SkillCallbacks' and not r['forwarder']),
    ('링 이펙트·스윕 컨트롤러', lambda k, r: k in ('RingEffect', 'SweepController')),
    ('이동 타격 파티클과 생성 helper', lambda k, r: k == 'DirectionalHitParticles'),
    ('투사체와 발사 helper', lambda k, r: k == 'Projectiles'),
    ('종료·충돌 처리 이펙트', lambda k, r: k == 'FinishingEffects'),
    ('스킬 시퀀스와 그 자식', lambda k, r: k in ('TargetFanoutSequence', 'RecoverTargetChild',
                                                 'StatusBuffChild', 'LiftNumberChild', 'TargetFlashNotify')),
    ('작은 생성 helper·파티클', lambda k, r: k in ('DriftEffect', 'DebrisEffect', 'RisingEffect',
                                                   'AnimatedParticle', 'TrailSprite', 'DescendingBurst')),
]


def replace_marked(text, name, body):
    start, end = f'<!-- {name}:begin -->', f'<!-- {name}:end -->'
    head, _, rest = text.partition(start)
    _, _, tail = rest.partition(end)
    return f'{head}{start}\n{body}\n{end}{tail}'


def main():
    inventory = json.loads((ROOT / 'tools/parallel/D_EFFECTS/inventory.json').read_text())['records']
    registered = json.loads((HANDOFF / 'registrations.json').read_text())['entries']
    remaining = {a: r for a, r in inventory.items() if a not in registered}

    modules = collections.Counter(r['generated_file'].split('/')[-1].replace('.cpp', '')
                                  for r in remaining.values())
    (HANDOFF / 'remaining.json').write_text(json.dumps({
        'remaining': len(remaining),
        'remaining_instructions': sum(r['instructions'] for r in remaining.values()),
        'by_module': dict(modules),
        'distinct_shapes': len({r['shape'] for r in remaining.values()}),
        'entries': sorted(({'entry': r['entry'], 'name': r['name'], 'instructions': r['instructions'],
                            'module': r['generated_file'].split('/')[-1].replace('.cpp', ''),
                            'calls_outside_scope': r['calls_outside_scope'], 'note': r['note']}
                           for r in remaining.values()), key=lambda e: int(e['entry'], 16)),
    }, indent=2, ensure_ascii=False) + '\n')

    instructions = lambda entries: sum(inventory[a]['instructions'] for a in entries)
    rows, placed = [], set()
    for label, belongs in GROUPS:
        group = [a for a, record in registered.items()
                 if a not in placed and belongs(record['implementation'].split('::')[0], inventory[a])]
        placed.update(group)
        if group:
            rows.append(f'| 완료: {label} | {len(group)} | {instructions(group):,} |')
    missing = set(registered) - placed
    if missing:
        raise SystemExit(f'ungrouped reconstructions: {sorted(missing)}')
    progress = ('| 구분 | 진입점 | 원본 명령 |\n|---|---:|---:|\n' + '\n'.join(rows) +
                f'\n| **완료 합계** | **{len(registered)}** | **{instructions(registered):,}** |'
                f'\n| 미완료 | {len(remaining)} | '
                f'{sum(r["instructions"] for r in remaining.values()):,} |')

    module_table = ('| 모듈 | 남은 진입점 |\n|---|---:|\n' +
                    '\n'.join(f'| {name} | {count} |' for name, count in modules.most_common()))

    table = [line for line in (ROOT / 'src/visual_effects/skill_callback_table.inc').read_text().splitlines()
             if line.startswith('{0x')]
    targets = collections.Counter(line.split(',')[1].strip() for line in table)
    left = [(t, n) for t, n in targets.most_common() if t in remaining]
    routines = ('| routine | 명령 | 표의 행 |\n|---|---:|---:|\n' +
                '\n'.join(f"| `{t[2:]}` {remaining[t]['name'][:52]} | {remaining[t]['instructions']} | {n} |"
                          for t, n in left))

    report = HANDOFF / 'REPORT.md'
    text = report.read_text()
    text = replace_marked(text, 'progress', progress)
    text = replace_marked(text, 'modules', module_table)
    text = replace_marked(text, 'routines', routines)
    report.write_text(text)

    def patch(name, paths):
        subprocess.run(['git', 'reset', '-q'], cwd=ROOT, check=True)
        subprocess.run(['git', 'add', '-A', '--'] + paths, cwd=ROOT, check=True)
        with (HANDOFF / name).open('wb') as out:
            subprocess.run(['git', 'diff', '--cached', '--binary', BASELINE, '--'] + paths,
                           cwd=ROOT, stdout=out, check=True)
        subprocess.run(['git', 'reset', '-q'], cwd=ROOT, check=True)

    patch('owned.patch', OWNED)
    patch('integration.patch', INTEGRATION)

    print(f'registered {len(registered)} / {len(inventory)}, remaining {len(remaining)} '
          f'({sum(r["instructions"] for r in remaining.values()):,} instructions), '
          f'{len(left)} table routines left')
    return 0


if __name__ == '__main__':
    sys.exit(main())
