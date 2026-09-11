#!/usr/bin/env python3
"""Recognise the skill presentation callbacks and describe them as data.

Shape, read from the original instruction bytes:

    phase = object[0x14c]
    if phase == K1: play cue C1            // returns the cue service result
    if phase == K2: timing mode = M2       // returns the phase, untouched in EAX
    ...
    otherwise:      action routine(object, script table, hit descriptor)

The cue phases sit at -1000 and below and are presentation only; the default
branch is the real action. Anything that does not match exactly is reported as
unrecognised rather than guessed at, so those bodies stay hand written.

Importable: `describe(insns)` returns the row, or None.
"""
import re

PHASE_FIELD = 0x14c
TIMING_MODE = 0x5d3a14
PLAY_CUE = 0x435373

NUMBER = r'(?:0x[0-9a-f]+|-?\d+)'
CMP = re.compile(rf'^eax, ({NUMBER})$')
LOAD_OBJECT = re.compile(rf'^(e[a-d]x|e[sd]i|ebp), dword ptr \[esp \+ ({NUMBER})\]$')
LOAD_PHASE = re.compile(rf'^eax, dword ptr \[(\w+) \+ ({NUMBER})\]$')
STORE_TIMING = re.compile(rf'^dword ptr \[({NUMBER})\], ({NUMBER})$')
TARGET = re.compile(r'^0x([0-9a-f]+)$')
CALLEE_SAVED = ('ebx', 'esi', 'edi', 'ebp')


def number(text):
    return int(text, 16) if text.startswith('0x') else int(text, 10)


def signed(value):
    value &= 0xffffffff
    return value - (1 << 32) if value >> 31 else value


def describe(insns):
    """Return {'object_register', 'phases': [...], 'default': {...}} or None."""
    at = {address: index for index, (address, _, _, _) in enumerate(insns)}
    body = [(a, m, o) for a, _, m, o in insns]

    index = 0
    saved = []
    while index < len(body) and body[index][1] == 'push' and body[index][2] in CALLEE_SAVED:
        saved.append(body[index][2])
        index += 1

    match = LOAD_OBJECT.match(body[index][2]) if body[index][1] == 'mov' else None
    if not match:
        return None
    obj = match.group(1)
    # arg0 sits at [esp + 4] on entry and each save above shifts it by four.
    if number(match.group(2)) != 4 + 4 * len(saved):
        return None
    index += 1
    # A prologue may interleave more saves after loading the object.
    while index < len(body) and body[index][1] == 'push' and body[index][2] in CALLEE_SAVED:
        saved.append(body[index][2])
        index += 1
    match = LOAD_PHASE.match(body[index][2]) if body[index][1] == 'mov' else None
    if not match or match.group(1) != obj or number(match.group(2)) != PHASE_FIELD:
        return None
    index += 1

    # The comparison chain. `test eax, eax` + `jne` is the same test against
    # zero, but it skips forward instead of branching away, so the block it
    # falls into runs and then continues into the action dispatch.
    phases, falls_through = [], None
    while index + 1 < len(body):
        if body[index][1] == 'cmp' and CMP.match(body[index][2]) and body[index + 1][1] == 'je':
            value = signed(number(CMP.match(body[index][2]).group(1)))
            target = TARGET.match(body[index + 1][2])
            if not target:
                return None
            phases.append((value, int(target.group(1), 16)))
            index += 2
            continue
        if body[index][1] == 'test' and body[index][2] == 'eax, eax' and body[index + 1][1] == 'jne':
            if falls_through is not None or not TARGET.match(body[index + 1][2]):
                return None
            falls_through = 0
            index += 2
            continue
        break
    if not phases and falls_through is None:
        return None

    def action_at(address):
        """Classify the block a phase branch lands on."""
        cursor = at.get(address)
        if cursor is None:
            return None
        _, mnemonic, operands = body[cursor]
        if mnemonic == 'mov' and (store := STORE_TIMING.match(operands)):
            if number(store.group(1)) != TIMING_MODE:
                return None
            return {'action': 'timing_mode', 'value': number(store.group(2))}
        if mnemonic == 'push' and (cue := re.match(rf'^({NUMBER})$', operands)):
            _, following, operand = body[cursor + 1]
            if following == 'jmp' and (jump := TARGET.match(operand)):
                landing = at.get(int(jump.group(1), 16))
                if landing is None or body[landing][1] != 'call':
                    return None
                _, _, called = body[landing]
            elif following == 'call':
                called = operand
            else:
                return None
            if not TARGET.match(called) or int(TARGET.match(called).group(1), 16) != PLAY_CUE:
                return None
            return {'action': 'play_cue', 'value': number(cue.group(1))}
        return None

    described = []
    for value, target in phases:
        action = action_at(target)
        if action is None:
            return None
        described.append({'phase': value, **action})

    # The zero-phase block sets the timing mode and then runs the dispatch too.
    if falls_through is not None:
        if body[index][1] != 'mov' or not (store := STORE_TIMING.match(body[index][2])):
            return None
        if number(store.group(1)) != TIMING_MODE:
            return None
        described.append({'phase': falls_through, 'action': 'timing_mode_then_dispatch',
                          'value': number(store.group(2))})
        index += 1

    # The fall-through is the action dispatch: constants, the object, one call.
    # Forty of the forwarders choose a descriptor by the acting actor's facing;
    # the same lookup appears here, as a load into eax before the push.
    pushes, chain = [], []
    while index < len(body) and body[index][1] in ('push', 'mov'):
        if body[index][1] == 'mov':
            if not body[index][2].startswith('eax, '):
                return None
            chain.append(body[index][2][5:])
        else:
            pushes.append(body[index][2])
        index += 1
    if index >= len(body) or body[index][1] != 'call':
        return None
    routine = TARGET.match(body[index][2])
    if not routine or not pushes or pushes[-1] != obj:
        return None
    constants = []
    for operand in reversed(pushes[:-1]):
        if re.match(rf'^({NUMBER})$', operand):
            constants.append({'kind': 'constant', 'value': '0x%x' % (number(operand) & 0xffffffff)})
        elif (table := re.match(rf'^dword ptr \[eax\*4 \+ ({NUMBER})\]$', operand)):
            constants.append({'kind': 'indexed_table', 'table': '0x%x' % number(table.group(1)),
                              'index_chain': chain})
        else:
            return None
    return {
        'object_register': obj,
        'saved_registers': saved,
        'phases': described,
        'routine': '0x%x' % int(routine.group(1), 16),
        'constants': constants,
    }
