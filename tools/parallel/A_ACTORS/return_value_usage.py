#!/usr/bin/env python3
"""Decide, per entry, whether any original caller reads EAX after the call.

Scans the whole .text for direct `call <entry>` sites and follows the fall
through path. EAX counts as used if an instruction reads it before something
overwrites it whole. Anything the scan cannot decide - an indirect branch, a
call, a return, or running out of budget - counts as used, so the answer is
never optimistically "unused".

Usage: return_value_usage.py EXECUTABLE ADDRESS [ADDRESS ...]
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_REG, X86_REG_AH, X86_REG_AL, X86_REG_AX, X86_REG_EAX
from prepare_event0 import PE

ACCUMULATOR = {X86_REG_EAX, X86_REG_AX, X86_REG_AL, X86_REG_AH}
BUDGET = 12
# Writes the whole register, so nothing of the returned value survives.
FULL_WRITE = {'mov', 'lea', 'pop', 'xor', 'movzx', 'movsx'}


def decode_all(pe, md):
    """Linear sweep of every executable section, keyed by address."""
    found = {}
    for virtual_size, address, size, offset in pe.sections:
        start = pe.base + address
        for at in range(size):
            block = list(md.disasm(pe.data[offset + at:offset + at + 16], start + at, count=1))
            if block:
                found[start + at] = block[0]
    return found


def call_sites(instructions, entry):
    for at, instruction in instructions.items():
        if instruction.mnemonic == 'call' and instruction.operands \
                and instruction.operands[0].type != X86_OP_REG \
                and instruction.op_str == f'0x{entry:x}':
            yield at + instruction.size


def reads_accumulator(instruction):
    read, _ = instruction.regs_access()
    return any(register in ACCUMULATOR for register in read)


def overwrites_accumulator(instruction):
    if instruction.mnemonic not in FULL_WRITE:
        return False
    destination = instruction.operands[0] if instruction.operands else None
    return destination is not None and destination.type == X86_OP_REG \
        and destination.reg in {X86_REG_EAX}


def used_at(instructions, start):
    at = start
    for _ in range(BUDGET):
        instruction = instructions.get(at)
        if instruction is None:
            return True
        mnemonic = instruction.mnemonic
        if mnemonic in ('call', 'ret', 'retf', 'int3') or mnemonic.startswith('j') or mnemonic == 'loop':
            # A tail call, a return or a branch can carry EAX onward.
            return True
        if reads_accumulator(instruction):
            return True
        if overwrites_accumulator(instruction):
            return False
        at += instruction.size
    return True


def main(executable, *addresses):
    pe = PE(executable)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    instructions = decode_all(pe, md)
    report = {}
    for text in addresses:
        entry = int(text, 16)
        sites = list(call_sites(instructions, entry))
        used = [hex(site) for site in sites if used_at(instructions, site)]
        report[hex(entry)] = {'call_sites': len(sites), 'sites_reading_eax': used,
                              'return_value_used': bool(used)}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main(*sys.argv[1:])
