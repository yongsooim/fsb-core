#!/usr/bin/env python3
"""Verify a numeric-symbol refactor against a pre-refactor source snapshot."""
import argparse
import hashlib
import json
from pathlib import Path
import re

PROTECTED = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
NUMBER = r'\b(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?)(?:[uUlL]*)\b'
LEXER = re.compile(PROTECTED + '|' + NUMBER + r'|[A-Za-z_]\w*|\S')
INTEGER = re.compile(r'(0[xX][0-9a-fA-F]+|[0-9]+)([uUlL]*)\Z')


def tokens(source):
    result = []
    for match in LEXER.finditer(source):
        token = match[0]
        number = INTEGER.fullmatch(token)
        if number:
            value = int(number[1], 16 if number[1].lower().startswith('0x') else 10)
            # All substituted unsuffixed integers fit in int. Retain suffixes
            # so int and unsigned cannot silently become interchangeable.
            token = f'number:{value}:{number[2].lower()}'
        result.append(token)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('--core', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    catalog = json.loads((args.core / 'reports/symbolization-catalog.json').read_text())
    values = {symbol['name']: symbol['value'] for symbol in catalog['symbols']}
    header = (args.core / 'include/fsb_core/symbols.hpp').read_text()
    # Check the actual declarations, not just the migration report.
    for name, value in values.items():
        namespace, short = name.split('::')
        body = re.search(r'namespace ' + namespace + r' \{([\s\S]*?)\n\}', header)[1]
        assert f'inline constexpr auto {short} = {value};' in body, name
        assert 0 <= int(value, 0) <= 0x7fffffff, name
    symbol_pattern = '|'.join(re.escape(name) for name in sorted(values, key=len, reverse=True))
    expand_pattern = re.compile(PROTECTED + r'|unsigned\((?:' + symbol_pattern + r')\)|\b(?:' + symbol_pattern + r')\b')

    def expand(match):
        text = match[0]
        if text.startswith('unsigned('):
            return values[text[9:-1]] + 'u'
        return values.get(text, text)

    checked = []
    for folder in ('src', 'include', 'tools', 'tests'):
        for old in sorted((args.before / folder).rglob('*')):
            if not old.is_file():
                continue
            relative = old.relative_to(args.before)
            new = args.core / relative
            assert new.is_file(), f'missing {relative}'
            before = old.read_bytes()
            after = new.read_bytes()
            if folder == 'src' and new.suffix == '.cpp':
                source = after.decode().replace('#include "fsb_core/symbols.hpp"\n', '')
                original, restored = tokens(before.decode()), tokens(expand_pattern.sub(expand, source))
                assert original == restored, f'non-symbol change: {relative}'
            else:
                assert before == after, f'unexpected change: {relative}'
            checked.append({'file': str(relative), 'changed': before != after,
                            'before_sha256': hashlib.sha256(before).hexdigest(),
                            'after_sha256': hashlib.sha256(after).hexdigest()})
    assert (args.before / 'CMakeLists.txt').read_bytes() == (args.core / 'CMakeLists.txt').read_bytes()
    report = {'equal_after_expanding_symbols': True, 'comments_and_strings_unchanged': True,
              'integer_values_and_suffixes_unchanged': True, 'symbols': len(values),
              'files_checked': len(checked), 'files': checked}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'files'}))


if __name__ == '__main__':
    main()
