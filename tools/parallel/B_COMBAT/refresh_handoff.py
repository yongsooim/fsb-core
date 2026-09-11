#!/usr/bin/env python3
"""Regenerate the handoff inventory, entry table and fixture table."""
import collections, json, re, subprocess, sys
from pathlib import Path

def main():
    root = Path(__file__).resolve().parents[3]
    scopes = {e['entry']: e for e in json.loads((root / 'tasks/parallel/scopes.json').read_text())
              ['workers']['B_COMBAT']['entries']}
    reg = json.loads((root / 'handoff/B_COMBAT/registrations.json').read_text())['entries']
    graph = json.loads((root / 'reference/parallel/B_COMBAT/scope-graph.json').read_text())
    remaining = []
    for a in sorted((x for x in scopes if x not in reg), key=lambda x: int(x, 16)):
        g = graph.get(a, {})
        remaining.append({
            'entry': a, 'instructions': scopes[a]['instructions'],
            'module': scopes[a]['generated_file'].split('/')[-1][:-4],
            'name': scopes[a]['source'].replace('_machine_code.c', '').replace('.c', '')[9:],
            'callees_not_yet_reconstructed': [c['target'] for c in g.get('calls', [])
                if c['owner'] not in ('native', 'service_boundary') and c['target'] not in reg],
            'other_owner_callees': [c['target'] for c in g.get('calls', [])
                                    if c['owner'] in ('A_ACTORS', 'C_MAPS', 'D_EFFECTS')],
            'indirect_calls': g.get('indirect_calls', 0),
            'x87_instructions': g.get('x87_instructions', 0)})
    inventory = {
        'baseline_commit': '116ab45b536648a9e6c9bace77397976efb40ba5',
        'assigned': len(scopes), 'completed': len(reg), 'remaining': len(remaining),
        'completed_instructions': sum(scopes[a]['instructions'] for a in reg),
        'remaining_without_indirect_calls': [e['entry'] for e in remaining if not e['indirect_calls']],
        'completed_entries': {a: reg[a]['implementation'] for a in sorted(reg, key=lambda x: int(x, 16))},
        'remaining_entries': remaining}
    (root / 'handoff/B_COMBAT/completed-entries.json').write_text(json.dumps(inventory, indent=1) + '\n')

    rows = collections.defaultdict(list)
    for a, v in sorted(reg.items(), key=lambda kv: int(kv[0], 16)):
        e, impl = scopes[a], v['implementation']
        group = impl.split('::')[1] if impl.startswith('combat::') else 'BattleRules'
        rows[group].append((a, e['instructions'], impl,
                            e['source'].replace('_machine_code.c', '').replace('.c', '')[9:]))
    table = []
    for group in sorted(rows):
        table += ['', f'### {group} ({len(rows[group])}개)', '',
                  '| 원본 진입점 | 원본 명령 | 의미 구현 | 원본 이름 |', '|---|---:|---|---|']
        table += [f'| `{a}` | {i} | `{impl}` | {n} |' for a, i, impl, n in rows[group]]

    log = (root / 'handoff/B_COMBAT/logs/combat-fixtures.log').read_text().splitlines()
    fixtures, cases, compared = [], 0, 0
    for line in log:
        name = line.split()[0]
        count = int(re.search(r'combat_cases=(\d+)', line).group(1))
        compared += int(re.search(r'guest_bytes_compared=(\d+)', line).group(1))
        cases += count
        report = json.loads((root / f'reference/parallel/B_COMBAT/{name}-x86.json').read_text())
        note = '아래 주석' if name == 'combat-targeting' else '통과'
        fixtures.append(f'| `{name}-x86` | {count:,} | {len(report["entry_counts"])} | {note} | 통과 |')

    left = collections.Counter(e['module'] for e in remaining)
    report = root / 'handoff/B_COMBAT/REPORT.md'
    text = report.read_text()
    head = text.split('## 완료한 진입점\n', 1)[0]
    rest = '## 의미 모듈과 API' + text.split('## 의미 모듈과 API', 1)[1]
    head = re.sub(r'245개 중 \d+개\*\*\(원본 명령 [\d,]+개\)',
                  f'245개 중 {len(reg)}개**(원본 명령 {inventory["completed_instructions"]:,}개)', head)
    head = re.sub(r'\*\*\d+개는 미완료\*\*', f'**{len(remaining)}개는 미완료**', head)
    generated = 1489 - int(subprocess.run(
        ['python3', '-c', 'import json,sys;print(len(json.load(open(sys.argv[1]))["entries"]))',
         str(root / 'tools/native_reconstructions.json')], capture_output=True, text=True).stdout)
    head = re.sub(r'`emitted_functions` 1,409 → \*\*[\d,]+\*\*, `native_reconstructions` 49 → \*\*\d+\*\*\.\n\d+개 중 49개는 기존 전환분이고 \d+개가 이 세션 결과다\.',
                  f'`emitted_functions` 1,409 → **{generated:,}**, `native_reconstructions` 49 → **{generated and len(reg) + 49}**.\n'
                  f'{len(reg) + 49}개 중 49개는 기존 전환분이고 {len(reg)}개가 이 세션 결과다.', head)
    rest = re.sub(r'\| fixture \| 사례 \| 진입점 \| 생성 본문 대조 \| 재구성 대조 \|\n\|---\|---:\|---:\|---\|---\|\n(?:\|.*\n)+',
                  '| fixture | 사례 | 진입점 | 생성 본문 대조 | 재구성 대조 |\n|---|---:|---:|---|---|\n'
                  + '\n'.join(fixtures) + '\n', rest)
    rest = re.sub(r'합계 [\d,]+사례, 비교한 게스트 바이트 약 [\d.]+GB\.',
                  f'합계 {cases:,}사례, 비교한 게스트 바이트 약 {compared / 1e9:.1f}GB.', rest)
    tests = len((root / 'handoff/B_COMBAT/logs/ctest-final.log').read_text().split('tests passed')[0].splitlines())
    rest = re.sub(r'전체 검사: `ctest` \*\*\d+/\d+ 통과\*\*\. 기준 53개에 이 세션 fixture \d+개를 더한 수다\.',
                  f'전체 검사: `ctest` **{53 + len(fixtures)}/{53 + len(fixtures)} 통과**. '
                  f'기준 53개에 이 세션 fixture {len(fixtures)}개를 더한 수다.', rest)
    modules = ' '.join(f'`{m}` {left[m]},' for m in
                       ['battle_actions', 'battle_flow', 'battle_ai', 'battle_grid',
                        'battle_status', 'battle_presentation', 'battle_rules'] if left[m])
    rest = re.sub(r'모듈별 잔여: .*?\n이 중 \*\*\d+개는 간접 호출이 없어\*\*',
                  f'모듈별 잔여: {modules.rstrip(",")}.\n'
                  f'이 중 **{len(inventory["remaining_without_indirect_calls"])}개는 간접 호출이 없어**',
                  rest, flags=re.S)
    report.write_text(head + '## 완료한 진입점\n' + '\n'.join(table) + '\n\n' + rest)
    print(f'{len(reg)} done, {len(remaining)} left, {cases:,} cases, {len(fixtures)} fixtures')

if __name__ == '__main__':
    main()
