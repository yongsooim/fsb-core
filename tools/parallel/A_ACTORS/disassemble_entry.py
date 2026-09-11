#!/usr/bin/env python3
"""Print the original machine code of an entry so reconstructions cite instructions.

Usage: disassemble_entry.py EXECUTABLE ADDRESS [ADDRESS ...]

Follows the same linear/branch walk the generator uses, so the listing covers
exactly the basic blocks that make up the original function body.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from prepare_event0 import PE

STOP = {'ret', 'retf', 'jmp', 'int3'}


def body(pe, md, entry):
    """Walk basic blocks from entry, returning {address: instruction}."""
    found, pending = {}, [entry]
    while pending:
        at = pending.pop()
        while at not in found:
            try:
                offset = pe.offset(at - pe.base)
            except ValueError:
                break
            block = list(md.disasm(pe.data[offset:offset + 16], at))
            if not block:
                break
            instruction = block[0]
            found[at] = instruction
            mnemonic = instruction.mnemonic
            if mnemonic.startswith('j') or mnemonic == 'loop':
                target = instruction.operands[0]
                if target.type == 2:  # immediate branch target
                    pending.append(target.imm)
            if mnemonic in STOP:
                break
            at += instruction.size
    return found


def main(executable, *addresses):
    pe = PE(executable)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    for text in addresses:
        entry = int(text, 16)
        found = body(pe, md, entry)
        print(f'==== {entry:08x}  ({len(found)} instructions)')
        for at in sorted(found):
            instruction = found[at]
            print(f'{at:08x}  {instruction.bytes.hex():<20} {instruction.mnemonic} {instruction.op_str}')


if __name__ == '__main__':
    main(*sys.argv[1:])
