#!/usr/bin/env python3
"""Differential fixtures for the B_COMBAT original entries.

Every case runs the unmodified original function under Unicorn from a recorded
battle snapshot, then records its return value and the complete guest .data
difference. No original call is replaced, so the expectations come from the
executable alone and never from the C++ reconstruction under test.

Output uses the existing FSBACT1 record layout so the same replay harness can
compare the generated body and the reconstruction against one baseline.
"""
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EFLAGS, UC_X86_REG_EIP,
                               UC_X86_REG_ESI, UC_X86_REG_ESP)

DATA_BASE = 0x4a5000
RETURN_MARK = 0x1000000

# Original record layout the cases seed. Offsets are byte offsets from a record.
PARTY_RECORD, PARTY_STRIDE = 0x607a08, 0xbc
ENEMY_RECORD, ENEMY_STRIDE = 0x806b60, 36
MONSTER_RECORD, MONSTER_STRIDE = 0x609ca0, 124
ACTOR_SLOT, ACTOR_STRIDE = 0x8073d8, 0x1ac
PARTY_COUNT, PARTY_IDS, ENEMY_COUNT = 0x803a20, 0x5d2258, 0x776484
GRID_WIDTH, GRID_HEIGHT = 0x7760c4, 0x7760c8
CURSOR_GRID, MOVE_COST_GRID, OCCUPANCY, ACTIVE_HEIGHT = 0x7764e0, 0x77a570, 0x7abca0, 0x77e598
VITALITY_PARTY, VITALITY_ENEMY = 0x776418, 0x77a518
MARKER_LIST, MARKER_COUNT, MARKER_STRIDE = 0x773f88, 0x77a50c, 0x10
RANDOM_STATE = 0x6d1bf0


class Oracle:
    def __init__(self, snapshot):
        raw = Path(snapshot).read_bytes()
        if raw[:8] != b'FSBDRAW1':
            raise ValueError('bad snapshot')
        cursor, regions, pages = 12, [], set()
        for _ in range(struct.unpack_from('<I', raw, 8)[0]):
            base, size = struct.unpack_from('<II', raw, cursor)
            cursor += 8
            regions.append((base, raw[cursor:cursor + size]))
            pages.update(range(base & ~4095, (base + size + 4095) & ~4095, 4096))
            cursor += size
        self.snapshot_sha = hashlib.sha256(raw).hexdigest()
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        for page in sorted(pages):
            self.u.mem_map(page, 4096)
        self.u.mem_map(RETURN_MARK, 65536)
        for base, data in regions:
            self.u.mem_write(base, data)
        self.initial = next(d for b, d in regions if b == DATA_BASE)
        self.records = bytearray()
        self.counts = collections.Counter()
        self.substituted = {}
        self.requests = collections.Counter()

    def read(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def substitute(self, entry, arguments, note, callee_pops=True):
        """Return from an original call without running it, recording the request.

        Used only for services outside the entry under test: heap release and
        the music transition. `callee_pops` follows the original's own calling
        convention, so the caller's stack is left exactly as the real call would.
        The note goes into the fixture report.
        """
        def boundary(u, address, size, user):
            sp = u.reg_read(UC_X86_REG_ESP)
            self.requests[(address, tuple(self.read(sp + 4 + i * 4) for i in range(arguments)))] += 1
            u.reg_write(UC_X86_REG_EAX, 0)
            u.reg_write(UC_X86_REG_EIP, self.read(sp))
            u.reg_write(UC_X86_REG_ESP, sp + 4 + (arguments * 4 if callee_pops else 0))
        self.u.hook_add(UC_HOOK_CODE, boundary, begin=entry, end=entry)
        self.substituted[hex(entry)] = note

    def substitute_decimal_print(self, slot, note):
        """Stand in for the imported decimal print the original calls.

        The import table entry has no code behind it under emulation, so the
        oracle writes the same decimal text the port's own service writes and
        returns the same length. Only the '%d' form the battle uses is handled;
        anything else stops the run rather than inventing output.
        """
        target = self.read(slot)

        def boundary(u, address, size, user):
            sp = u.reg_read(UC_X86_REG_ESP)
            out, fmt, value = (self.read(sp + 4 + i * 4) for i in range(3))
            spec = bytes(u.mem_read(fmt, 3))
            if spec[:3] != b'%d\0':
                raise RuntimeError(f'unexpected format at {fmt:x}: {spec!r}')
            text = str(struct.unpack('<i', struct.pack('<I', value))[0]).encode()
            u.mem_write(out, text + b'\0')
            self.requests[(address, (value,))] += 1
            u.reg_write(UC_X86_REG_EAX, len(text))
            u.reg_write(UC_X86_REG_EIP, self.read(sp))
            u.reg_write(UC_X86_REG_ESP, sp + 4)
        self.u.hook_add(UC_HOOK_CODE, boundary, begin=target, end=target)
        self.substituted[hex(slot)] = note

    def substitute_publishing(self, entry, arguments, pointer, words, scratch, note):
        """Substitute a service and copy its pointed-to argument into .data.

        The original hands these services a rectangle on its own CPU stack,
        which no .data comparison can see. Publishing the words at a scratch
        address inside the compared range puts them back under comparison, and
        the replay publishes the same words from the same service boundary.
        """
        def boundary(u, address, size, user):
            sp = u.reg_read(UC_X86_REG_ESP)
            args = [self.read(sp + 4 + i * 4) for i in range(arguments)]
            self.requests[(address, tuple(a for i, a in enumerate(args) if i != pointer))] += 1
            block = args[pointer]
            for i in range(words):
                u.mem_write(scratch + i * 4, struct.pack('<I', self.read(block + i * 4)))
            for i, a in enumerate(a for i2, a in enumerate(args) if i2 != pointer):
                u.mem_write(scratch + (words + i) * 4, struct.pack('<I', a))
            u.reg_write(UC_X86_REG_EAX, 0)
            u.reg_write(UC_X86_REG_EIP, self.read(sp))
            u.reg_write(UC_X86_REG_ESP, sp + 4 + arguments * 4)
        self.u.hook_add(UC_HOOK_CODE, boundary, begin=entry, end=entry)
        self.substituted[hex(entry)] = note

    def case(self, entry, args=(), writes=(), mask=0):
        u = self.u
        u.mem_write(DATA_BASE, self.initial)
        u.mem_write(RETURN_MARK, bytes(65536))
        for address, width, value in writes:
            u.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))
        before = bytes(u.mem_read(DATA_BASE, len(self.initial)))
        for register in [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                         UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI]:
            u.reg_write(register, 0)
        u.reg_write(UC_X86_REG_EFLAGS, 2)
        u.reg_write(UC_X86_REG_ESP, 0x100f000)
        u.mem_write(0x100f000, struct.pack('<' + 'I' * (1 + len(args)), RETURN_MARK,
                                           *[a & 0xffffffff for a in args]))
        try:
            u.emu_start(entry, RETURN_MARK, count=5000000)
        except Exception as error:
            raise RuntimeError(f'entry {entry:x} case {self.counts[entry]}: {error}') from error
        if u.reg_read(UC_X86_REG_EIP) != RETURN_MARK:
            raise RuntimeError(f'entry {entry:x} did not return')
        after = bytes(u.mem_read(DATA_BASE, len(self.initial)))
        changes = []
        for start in range(0, len(self.initial), 4096):
            if before[start:start + 4096] == after[start:start + 4096]:
                continue
            changes.extend((DATA_BASE + i, after[i])
                           for i in range(start, min(start + 4096, len(self.initial)))
                           if before[i] != after[i])
        self.records += struct.pack('<III', entry, mask, len(args))
        self.records += b''.join(struct.pack('<I', a & 0xffffffff) for a in args)
        self.records += struct.pack('<I', len(writes))
        self.records += b''.join(struct.pack('<III', a, w, v & 0xffffffff) for a, w, v in writes)
        self.records += struct.pack('<II', u.reg_read(UC_X86_REG_EAX) & mask, len(changes))
        self.records += b''.join(struct.pack('<IB', a, v) for a, v in changes)
        self.counts[entry] += 1

    def write(self, output, scope, notes):
        data = b'FSBACT1\0' + struct.pack('<I', sum(self.counts.values())) + bytes(self.records)
        path = Path(output)
        path.write_bytes(data)
        report = {'input_snapshot_sha256': self.snapshot_sha, 'scope': scope,
                  'cases': sum(self.counts.values()),
                  'entry_counts': {hex(k): v for k, v in sorted(self.counts.items())},
                  'substituted_calls': self.substituted,
                  'service_requests': [{'entry': hex(e), 'arguments': list(a), 'count': n}
                                       for (e, a), n in sorted(self.requests.items())],
                  'comparison': 'return value under the call site mask and the complete guest .data,'
                                ' as sparse changes against each pre-call state',
                  'notes': notes,
                  'fixture_sha256': hashlib.sha256(data).hexdigest()}
        path.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))


def party_records(oracle):
    return [PARTY_RECORD + oracle.read(PARTY_IDS + i * 4) * PARTY_STRIDE
            for i in range(oracle.read(PARTY_COUNT))]


def status_rules(oracle):
    """Turn bookkeeping, status records, relation tables and grid queries."""
    o = oracle
    enemies = o.read(ENEMY_COUNT)
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    statuses = [0, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x3000, 0x4000,
                0x200000, 0x400000, 0x600000, 0x600a00, 0x1ff00, 0xffffffff]

    def seeded(variant):
        """Vary status words, timers, gauges and vitality across all records."""
        writes = [(RANDOM_STATE, 4, variant * 2654435761 & 0xffffffff)]
        for index, party_id in enumerate(party_ids):
            record = PARTY_RECORD + party_id * PARTY_STRIDE
            writes += [(record + 8, 4, statuses[(variant + index) % len(statuses)]),
                       (record + 12, 4, [0, 0x11111111, 0x23456789, 0xfedcba98][(variant + index) % 4]),
                       (record + 16, 4, [0, 0x111, 0x321][(variant + index) % 3]),
                       (record + 0x1c, 4, [0, 1, 40, 250][(variant + index) % 4]),
                       (record + 0x2c, 4, [0, 14, 15, 16, 0xffffffff][variant % 5]),
                       (record + 0x30, 4, (variant + index) * 7 % 60),
                       (record + 0x70, 4, 10 + index * 6)]
            for slot in range(5):
                writes.append((record + 0x74 + slot * 4,
                               4, [0xffffffff, 0x146, 0x145, 0xf4][(variant + slot) % 4]))
        for index in range(enemies):
            record = ENEMY_RECORD + index * ENEMY_STRIDE
            writes += [(record + 8, 4, statuses[(variant + index + 3) % len(statuses)]),
                       (record + 12, 4, [0, 0x22222222, 0x13579bdf][(variant + index) % 3]),
                       (record + 16, 4, [0, 0x321, 0x999][(variant + index) % 3]),
                       (record + 20, 4, [0, 1, 32, 400][(variant + index) % 4]),
                       (record + 32, 4, (variant + index) * 11 % 60)]
        return writes

    for variant in range(24):
        writes = seeded(variant)
        for entry in [0x44caee, 0x44c6eb, 0x44cbd2, 0x44d233, 0x44d297, 0x44d2d8, 0x44dd1c]:
            o.case(entry, [], writes, 0xffffffff if entry == 0x44d233 else 0)
        o.case(0x44cd05, [0x8059f0], writes + [(0x8059f0, 4, 0xdeadbeef)], 0xff)
        for use_active in [0, 1]:
            o.case(0x44d389, [use_active], writes, 0)
            o.case(0x44d40a, [use_active], writes, 0)
        # Context indices of the recorded encounter: party slots and enemy actors.
        for context in [0, 1, 60, 62, 63]:
            for mask in [0x100, 0x600a00, 0x4000, 0xffffffff]:
                o.case(0x44cb26, [context, mask], writes, 0xff)
            o.case(0x44cb7a, [context], writes, 0xffffffff)
            for delta in [0, 5, 0xfffffff0, 0x7fffffff]:
                o.case(0x44d345, [context, delta], writes, 0)
            for secondary in [0, 1]:
                o.case(0x44d09c, [context, secondary], writes, 0xffffffff)
                o.case(0x44d102, [context, secondary], writes, 0xffffffff)

    # Status application covers every slot plus the out-of-range guard.
    for party_id in party_ids[:3]:
        for slot in range(7):
            o.case(0x44e689, [party_id, slot], seeded(slot), 0)
    for index in range(min(enemies, 4)):
        for slot in range(8):
            o.case(0x44e56d, [index, slot], seeded(slot), 0)
    for party_id in party_ids:
        for variant in range(4):
            o.case(0x44e46e, [party_id], seeded(variant), 0)
        for item in [0xffffffff, 0x146, 0x145, 0xf4, 63]:
            o.case(0x4489d0, [party_id, item], seeded(1), 0xff)

    # Experience thresholds around each table boundary and the level cap.
    for party_id in party_ids[:3]:
        record = PARTY_RECORD + party_id * PARTY_STRIDE
        for level in [0, 1, 50, 97, 98, 99]:
            threshold = o.read(0x5c0238 + level * 4)
            for gain in [0, 1, threshold, threshold + 1, 0x7fffffff, 0xfffffff6]:
                o.case(0x44de15, [party_id, gain],
                       [(record + 0x28, 4, level), (record + 0x34, 4, 0), (record + 0x38, 4, 0)], 0xff)
    for amount in [0, 1, 0x10270e, 0x10270f, 0x102710, 0x7fffffff, 0x80000000, 0xffffffff]:
        o.case(0x44e7d3, [amount], [], 0xffffffff)

    # Relation tables. Facings and offsets follow the original lookup ranges.
    attacker, target = ACTOR_SLOT, ACTOR_SLOT + ACTOR_STRIDE
    for facing in range(4):
        for x in range(-11, 12, 2):
            for y in range(-11, 12, 2):
                o.case(0x44e16c, [attacker, target],
                       [(attacker + 0x128, 4, 20), (attacker + 0x12c, 4, 20),
                        (target + 0x128, 4, 20 + x), (target + 0x12c, 4, 20 + y),
                        (target + 0x110, 4, facing)], 0xffffffff)
    for relation in range(5):
        o.case(0x44e200, [relation], [], 0xffffffff)
        o.case(0x44e231, [relation], [], 0xffffffff)
        for row in range(3):
            o.case(0x44e265, [row, relation], [], 0xffffffff)
    for a in range(5):
        for b in range(5):
            o.case(0x44e2de, [(1 << a) >> 1 << 24, (1 << b) >> 1 << 24], [], 0xffffffff)
    for a in range(4):
        for b in range(4):
            o.case(0x44e775, [a << 12, b << 12], [], 0xffffffff)

    # Grid queries over the recorded map dimensions.
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    for height_layer in [0, 1]:
        for x, y, occupant in [(0, 0, 0x10000 | 60), (3, 4, 0x10000 | 1), (5, 6, 0), (width - 1, height - 1, 0x1ffff)]:
            cell = height_layer * 4096 + y * width + x
            o.case(0x44ce7b, [x, y],
                   [(ACTIVE_HEIGHT, 4, height_layer), (OCCUPANCY + cell * 4, 4, occupant)], 0xffffffff)
    for radius in [0, 1, 3, 20]:
        for x, y in [(0, 0), (5, 6), (width - 1, height - 1)]:
            for keep in [0, 0xf0, 0xffffffff]:
                writes = [(CURSOR_GRID + i * 4, 4, 0xffffffff) for i in range(width * height)]
                o.case(0x44cdf3, [x, y, radius, keep], writes, 0)
    for maximum in [0, 1, 4, 255, 0x7fffffff, 0xffffffff]:
        writes = [(MOVE_COST_GRID + i * 4, 4, [0xffffffff, 0, 1, 4, 255, 0x100, 0x7fffffff][i % 7])
                  for i in range(width * height)]
        writes += [(CURSOR_GRID + i * 4, 4, 0x5a) for i in range(width * height)]
        o.case(0x451ec6, [maximum], writes, 0)

    # Battle markers standing in front of an actor.
    for facing in range(4):
        for count in [0, 1, 4]:
            writes = [(MARKER_COUNT, 4, count), (ACTOR_SLOT + 0x110, 4, facing),
                      (ACTOR_SLOT + 0x128, 4, 10), (ACTOR_SLOT + 0x12c, 4, 12)]
            for i in range(4):
                actor = ACTOR_SLOT + (2 + i) * ACTOR_STRIDE
                writes += [(MARKER_LIST + i * MARKER_STRIDE, 4, 2 + i),
                           (actor + 0x128, 4, 10 + (1 if i == 2 else 0) * (facing == 3) - (1 if i == 2 else 0) * (facing == 2)),
                           (actor + 0x12c, 4, 12 - (1 if i == 2 else 0) * (facing == 0) + (1 if i == 2 else 0) * (facing == 1))]
            o.case(0x44e3e1, [ACTOR_SLOT], writes, 0xffffffff)


# Pose callbacks. Each entry is one original battle appearance routine.
FIXED_POSES = [0x447e31, 0x447ea3, 0x447ef9, 0x447f4c, 0x447f9f, 0x447fef, 0x448042,
               0x448095, 0x4481a9, 0x44828a, 0x4483b5]
FACING_POSES = [0x44821f, 0x44830f, 0x44844e, 0x4484f4]


def poses(oracle):
    """Battle pose callbacks over positions, appearance flags and facings."""
    o = oracle
    actor = ACTOR_SLOT + 700 * ACTOR_STRIDE
    positions = [(0, 0), (0x58000, 0x88000), (0xfffe8000, 0x138000),
                 (0x7fffffff, 0x80000000), (0xffffffff, 1)]
    flag_sets = [0, 0x40, 0x1024e, 0x40000000, 0x4001024e, 0xffffffff]
    def base(flags, x, y):
        return [(actor, 4, 700), (actor + 4, 4, flags), (actor + 8, 4, x), (actor + 12, 4, y),
                (actor + 0x14, 4, x), (actor + 0x18, 4, y),
                (actor + 0x120, 4, 0x5a5a5a5a), (actor + 0x124, 4, 0x5a5a5a5a),
                (actor + 0x128, 4, 0x5a5a5a5a), (actor + 0x12c, 4, 0x5a5a5a5a),
                (actor + 0x134, 4, 0x5a5a5a5a), (actor + 0x138, 4, 0x5a5a5a5a)]
    for flags in flag_sets:
        for x, y in positions:
            writes = base(flags, x, y)
            for state in [0xffffffff, 0, 5]:
                for link in [0, 1, 7]:
                    seeded = writes + [(actor + 0x14c, 4, state), (actor + 0x11c, 4, link)]
                    for entry in FIXED_POSES:
                        o.case(entry, [actor], seeded, 0)
            for facing in range(4):
                for target in range(4):
                    for tick in [0, 1, 4, 5, 14, 15, 0xffffffff]:
                        seeded = writes + [(actor + 0x110, 4, facing), (actor + 0x114, 4, target),
                                           (actor + 0x144, 4, tick), (actor + 0x11c, 4, 1)]
                        for entry in FACING_POSES:
                            o.case(entry, [actor], seeded, 0)
            # 4480e8 places from world pixels and tolerates a fifth facing.
            for facing in range(5):
                o.case(0x4480e8, [actor], writes + [(actor + 0x110, 4, facing)], 0)


CANDIDATES, CANDIDATE_STRIDE = 0x7824c0, 16
REGION_FIRST, REGION_LENGTH = 0x7824b0, 0x781260
BEFORE_REGION_FIRST, BEFORE_REGION_LENGTH = 0x7824ac, 0x78125c
ACTIVE_REGION, CANDIDATE_COUNT, REACH_OVERLAY = 0x780250, 0x77fffc, 0x780260
SKILL_SLOTS, SKILL_SLOT_COUNT = 0x780240, 0x780254
HANDLER_ROWS, HANDLER_ROW_STRIDE = 0x5c208c, 24
HURT_RATIO, HURT_COUNT = 0x7865d0, 0x78739c
LINE_EFFECTS = 0x7873d8


def ai_scoring(oracle):
    """Enemy turn candidate bookkeeping, scoring curves and mask helpers."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    enemies = o.read(ENEMY_COUNT)
    scratch = 0x8059f0

    def candidate_field(count, variant):
        writes = []
        for i in range(count):
            record = CANDIDATES + i * CANDIDATE_STRIDE
            writes += [(record + 0, 4, (i * 3 + variant) % width),
                       (record + 4, 4, (i * 5 + variant) % height),
                       (record + 8, 4, 0),
                       (record + 0xc, 4, [7, 0, 0xffffffff, 3 + i, 0x7fffffff, 0x80000000][(i + variant) % 6])]
        for cell in range(width * height):
            writes.append((REACH_OVERLAY + cell, 1, (cell + variant) % 3 != 0))
        return writes

    for variant in range(6):
        for count in [0, 1, 5, 12]:
            base = candidate_field(max(count, 1) + 4, variant)
            for region in range(3):
                writes = base + [(ACTIVE_REGION, 4, region), (CANDIDATE_COUNT, 4, count)]
                writes += [(REGION_FIRST + r * 4, 4, r) for r in range(4)]
                writes += [(REGION_LENGTH + r * 4, 4, count) for r in range(4)]
                writes += [(BEFORE_REGION_FIRST + r * 4, 4, r) for r in range(4)]
                writes += [(BEFORE_REGION_LENGTH + r * 4, 4, count) for r in range(4)]
                o.case(0x452372, [], writes, 0xffffffff)
                o.case(0x4523b9, [region], writes, 0xffffffff)
                o.case(0x45240c, [region], writes, 0xffffffff)
                o.case(0x45244d, [region], writes, 0xffffffff)
                o.case(0x453273, [], writes, 0xffffffff)

    # Remaining-vitality percentages over the recorded enemy party.
    for variant in range(8):
        writes = []
        for index in range(enemies):
            record = ENEMY_RECORD + index * ENEMY_STRIDE
            maximum = o.read(MONSTER_RECORD + o.read(record + 4) * MONSTER_STRIDE + 24)
            values = [0, 1, maximum // 3, maximum - 1, maximum, maximum + 5, 0xffffffff, 0x7fffffff]
            writes += [(record + 20, 4, values[(variant + index) % len(values)]),
                       (record + 8, 4, [0, 0x6000, 0x2000, 0x4000][(variant + index) % 4])]
        writes += [(HURT_COUNT, 4, variant)]
        writes += [(HURT_RATIO + i * 4, 4, 0x5a5a5a5a) for i in range(enemies + 2)]
        for slot in range(enemies + 1):
            o.case(0x452c4d, [slot], writes, 0xff)

    # Priority skill lookup over collected slots and monster action rows.
    for count in [0, 1, 4, 8]:
        for variant in range(4):
            writes = [(SKILL_SLOT_COUNT, 4, count)]
            writes += [(SKILL_SLOTS + i * 4, 4, (i + variant) % 0x1f) for i in range(max(count, 1))]
            for monster in [126, 135, 189]:
                o.case(0x452c0c, [monster], writes, 0xffffffff)

    # Enemy action hand-off.
    actor = ACTOR_SLOT + 60 * ACTOR_STRIDE
    for action in [0xffffffff, 0xfffffffe, 0, 1, 200, 0x7fffffff]:
        for flags in [0, 0x4000, 0x10000, 0x1424e, 0xffffffff]:
            writes = [(actor + 4, 4, flags), (actor + 0x2c, 4, 9), (actor + 0x38, 4, 9),
                      (actor + 0x148, 4, 0x5a5a5a5a), (0x77ec4c, 4, 0), (0x77ecdc, 4, 9),
                      (0x7760d8, 4, 0x5a5a5a5a)]
            o.case(0x452a00, [actor, action], writes, 0)

    # Region count selection and the line-effect table.
    for handler in range(24):
        o.case(0x451e1e, [handler], [(0x77fff8, 4, 0x5a5a5a5a)], 0)
    o.case(0x454220, [], [(LINE_EFFECTS + i * 4, 4, 0x5a5a5a5a) for i in range(40)], 0)

    # 11x11 action mask offsets.
    mask = 0x77fc18
    for variant in range(8):
        writes = [(mask + i, 1, [0, 4, 2, 6, 1][(i * 7 + variant * 13) % 5]) for i in range(121)]
        writes += [(scratch + i * 4, 4, 0x5a5a5a5a) for i in range(2 + 121 * 2)]
        o.case(0x45297e, [mask, scratch + 8, scratch], writes, 0)

    # Byte grid to row spans, over the recorded map dimensions.
    spans, out_count = 0x7870b8, 0x78025c
    for variant in range(6):
        for bit in [1, 4]:
            writes = [(REACH_OVERLAY + cell, 1, [0, 1, 4, 5, 7][(cell * 3 + variant * 5) % 5])
                      for cell in range(width * height)]
            writes += [(spans + i * 4, 4, 0x5a5a5a5a) for i in range(3 * 64)]
            writes += [(out_count, 4, 0x5a5a5a5a)]
            for origin in [(0, 0), (3, 5)]:
                o.case(0x451e3f, [width, height, REACH_OVERLAY, origin[0], origin[1], out_count, spans, bit],
                       writes, 0xffffffff)

    # Scoring curves. The tier argument indexes a three-entry table.
    for tier in range(3):
        for hurt in [0, 1, 4, 5, 6, 99, 100, 0x7ffffff0, 0xfffffffb, 0x80000000]:
            for entry in [0x452e79, 0x452eb4, 0x452eea]:
                o.case(entry, [0, 0, 0, hurt, tier], [], 0xffffffff)
    for seed in [1, 12345, 0x7fffffff, 0xdeadbeef]:
        for entry in [0x452ce2, 0x452cf2]:
            o.case(entry, [0, 0, 0, 0, 0], [(RANDOM_STATE, 4, seed)], 0xffffffff)


PARTY_TILES, TARGET_TABLES = 0x776488, [0x774190, 0x775960, 0x7760f0]
BATTLE_ACTOR_RECORDS = 0x7744b8
SKILL_SCHEDULE, CHARACTER_SKILLS, LEARNED_SKILLS = 0x5bfd30, 0x6085c8, 0x607a9c
DIFFICULTY_RECORD, DIFFICULTY_TEMPLATE = 0x608394, 0x6082dc
ENCOUNTER_OVERRIDE = 0x5bf4d8
FOOTPRINT_SLOTS, FOOTPRINT_SHAPE_FOR_SLOT = 0x775888, 0x77ec08
MAP_ORIGIN_X, MAP_ORIGIN_Y = 0x77e58c, 0x77e590
ACTIVE_PARTY_INDEX, CURRENT_MAP = 0x803a1c, 0x5d229c
TILE_ATTRIBUTES = 0x7cbca0


def grid_tables(oracle):
    """Battle grid bookkeeping, tile reservation and difficulty scaling."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    scratch = 0x8059f0

    o.case(0x448d72, [0], [(0x7e0d20, 4, 33), (0x7e0d24, 4, 21), (0x77ec48, 4, 0x5a5a5a5a),
                           (GRID_WIDTH, 4, 1), (GRID_HEIGHT, 4, 1)], 0)
    o.case(0x4499cb, [], [(t + i * 4, 4, 0x5a5a5a5a) for t in TARGET_TABLES for i in range(0xc9)], 0)
    o.case(0x449f5b, [], [(BATTLE_ACTOR_RECORDS + i * 4, 4, 0x5a5a5a5a) for i in range(8 * 0x220 // 4)], 0)
    o.case(0x44a8d7, [], [(0x77ec54, 4, 0x5a5a5a5a)], 0)

    for variant in range(4):
        writes = [(PARTY_TILES + i * 8, 4, (i * 3 + variant) % width) for i in range(8)]
        writes += [(PARTY_TILES + i * 8 + 4, 4, (i * 5 + variant) % height) for i in range(8)]
        writes += [(MOVE_COST_GRID + c * 4, 4, 0x01020304) for c in range(width * height)]
        for count in [0, 1, 5]:
            o.case(0x449999, [], writes + [(PARTY_COUNT, 4, count)], 0)

    # Blocking-attribute scan around the active party member.
    for index in [0, 1]:
        actor = ACTOR_SLOT + index * ACTOR_STRIDE
        for variant in range(3):
            writes = [(ACTIVE_PARTY_INDEX, 4, index), (actor + 0x128, 4, 6), (actor + 0x12c, 4, 8),
                      (ACTIVE_HEIGHT, 4, 0)]
            writes += [(TILE_ATTRIBUTES + c * 4, 4, 0x180000 if (c * 7 + variant * 11) % 13 == 0 else 0)
                       for c in range(width * height)]
            for radius in [0, 1, 2, 4]:
                o.case(0x448aca, [radius], writes, 0xff)

    # Footprint reservation over the recorded placement tables.
    for index in [0, 1]:
        actor = ACTOR_SLOT + index * ACTOR_STRIDE
        for facing in range(4):
            for slot in range(4):
                for shape in range(3):
                    for free, cost in [(0x5a, 1), (0x5a, 9), (2, 1)]:
                        writes = [(ACTIVE_PARTY_INDEX, 4, index), (actor + 0x110, 4, facing),
                                  (FOOTPRINT_SHAPE_FOR_SLOT + slot * 4, 4, shape % 3),
                                  (MAP_ORIGIN_X, 4, 5), (MAP_ORIGIN_Y, 4, 5)]
                        writes += [(FOOTPRINT_SLOTS + c * 4, 4, free) for c in range(81)]
                        writes += [(MOVE_COST_GRID + c * 4, 4, cost) for c in range(width * height)]
                        writes += [(PARTY_TILES + slot * 8, 4, 0x5a5a5a5a),
                                   (PARTY_TILES + slot * 8 + 4, 4, 0x5a5a5a5a)]
                        o.case(0x448c6e, [slot, shape], writes, 0xffffffff)

    # Encounter overrides, for the current map and a named one.
    for map_id in [0xffffffff, 0, 416, 468]:
        writes = [(CURRENT_MAP, 4, 457)] + [(ENCOUNTER_OVERRIDE + i * 4, 4, 0x5a5a5a5a) for i in range(500)]
        o.case(0x448a98, [map_id], writes, 0)
        o.case(0x448ab1, [map_id], writes, 0)

    # Skill unlocks at, before and after each scheduled level.
    for party_id in range(4):
        schedule = SKILL_SCHEDULE + party_id * 80
        levels = [o.read(schedule + pair * 8) for pair in range(10)]
        for level in sorted({0, 1, 99} | {v for v in levels if 0 < v < 100}):
            writes = [(LEARNED_SKILLS + (party_id * 0x2f + s) * 4, 4, 0x5a5a5a5a) for s in range(10)]
            o.case(0x448a39, [party_id, level], writes, 0xffffffff)

    # Stat ordering and lookup.
    for a in range(4):
        for b in range(4):
            o.case(0x448977, [scratch, scratch + 4], [(scratch, 4, a), (scratch + 4, 4, b)], 0)
    for row in range(4):
        for kind in range(4):
            for column in range(3):
                o.case(0x44899e, [row, kind, column], [], 0xffffffff)

    # Difficulty scaling: build the record, then publish it.
    for step in range(6):
        writes = [(DIFFICULTY_RECORD + step * 0xbc + i * 4, 4, 0x5a5a5a5a) for i in range(0xbc // 4)]
        o.case(0x44a93e, [step], writes, 0)
        for scaled in [0, 1, 2, 0x7fffffff, 0xffffffff]:
            o.case(0x44aaa1, [step], [(DIFFICULTY_RECORD + step * 0xbc + 0x1c, 4, scaled)], 0)

    # Entrance marker actor.
    for index in [0, 2]:
        for base in [0, 1, 30]:
            actor = ACTOR_SLOT + index * ACTOR_STRIDE
            o.case(0x44ab05, [], [(0x77a510, 4, index), (actor, 4, index), (0x774168, 4, base),
                                  (0x7760d4, 4, 0)], 0)


ACTION_MASK, TARGET_MASK = 0x77fc18, 0x77fe00
MASK_FACING, MASK_SHIFT_X, MASK_SHIFT_Y = 0x77fc10, 0x77fc08, 0x77fc0c
CURSOR_FLAGS = 0x7764e0


def targeting(oracle):
    """Action footprint publication and the two target collectors."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    scratch = 0x8059f0
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]

    # 45149b publishes an action footprint for one handler and facing.
    for handler in range(24):
        for facing in range(4):
            writes = [(ACTION_MASK + i, 1, 0x5a) for i in range(484)]
            writes += [(TARGET_MASK + i, 1, 0x5a) for i in range(484)]
            writes += [(MASK_FACING, 4, 0x5a5a5a5a), (MASK_SHIFT_X, 4, 0x5a5a5a5a),
                       (MASK_SHIFT_Y, 4, 0x5a5a5a5a)]
            o.case(0x45149b, [handler, facing], writes, 0)

    # Target collection over the recorded encounter's occupancy grid.
    actor = ACTOR_SLOT + 60 * ACTOR_STRIDE
    occupants = [(0, 0x100), (1, 0x100), (60, 0x400), (62, 0x400), (700, 0x800)]
    # Each case carries a full mask and cursor grid, so keep the variant count
    # to what actually changes the outcome rather than sweeping every product.
    for variant in range(3):
        for facing in range(4):
            writes = [(actor + 0x128, 4, 8), (actor + 0x12c, 4, 9), (actor + 0x1c, 4, 0),
                      (MASK_FACING, 4, facing),
                      (MASK_SHIFT_X, 4, [0, 1, 0xffffffff][variant % 3]),
                      (MASK_SHIFT_Y, 4, [0, 0xffffffff, 1][variant % 3])]
            writes += [(ACTION_MASK + i, 1, 4 if (i * 5 + variant) % 3 else 0) for i in range(484)]
            writes += [(CURSOR_FLAGS + c * 4, 4, (c * 0x10 + variant * 0x30) & 0xf0)
                       for c in range(width * height)]
            for index, (occupant, flags) in enumerate(occupants):
                cell = (7 + index) + (8 + index) * width
                writes += [(OCCUPANCY + cell * 4, 4, 0x10000 | occupant),
                           (ACTOR_SLOT + occupant * ACTOR_STRIDE + 4, 4, flags),
                           (ACTOR_SLOT + occupant * ACTOR_STRIDE, 4, occupant)]
            for index, party_id in enumerate(party_ids):
                writes.append((PARTY_RECORD + party_id * PARTY_STRIDE + 8, 4,
                               [0, 0x100, 0x200000, 0x4000][(variant + index) % 4]))
            for index in range(o.read(ENEMY_COUNT)):
                writes.append((ENEMY_RECORD + index * ENEMY_STRIDE + 8, 4,
                               [0, 0x800, 0x200000, 0x1000][(variant + index) % 4]))
            writes += [(scratch + i * 4, 4, 0x5a5a5a5a) for i in range(1 + 121)]
            for mask in [0x400, 0x500, 0xffffffff]:
                for excluded in [0, 0x200000]:
                    o.case(0x4519dd, [actor, scratch, scratch + 4, mask, excluded], writes, 0)
                    for required in [0, 0x800]:
                        o.case(0x451885, [actor, scratch, scratch + 4, mask, excluded, required],
                               writes, 0)


PENDING_ACTORS, PENDING_COUNT, OUTSTANDING = 0x806478, 0x8064f0, 0x805508
FADE_STATE, CURRENT_EVENT = 0x6d9e70, 0x57fd1c
SCRIPTED_PENDING, SCRIPTED_PHASE, SCRIPTED_GROUP, SCRIPTED_MUSIC = 0x77ec54, 0x77ec58, 0x77ec30, 0x77ec50
ANCHOR_OWNER = 0x160


def progression(oracle):
    """Battle progression, scripted hand-off and anchored effect objects."""
    o = oracle
    # Heap release and the music transition are not part of these entries.
    o.substitute(0x4972b0, 1, 'CRT free (cdecl); the released block is recorded, no heap runs',
                 callee_pops=False)
    o.substitute(0x4337f4, 6, 'music transition service; arguments recorded, no audio device runs')

    for state in [0, 1, 0xffffffff]:
        o.case(0x4622b6, [], [(FADE_STATE, 4, state)], 0xff)

    # Pending follow-up steps over party, enemy and unowned queue entries.
    slots = [0, 1, 60, 62, 700]
    for variant in range(8):
        for count in [0, 1, 5]:
            writes = [(PENDING_COUNT, 4, count), (OUTSTANDING, 4, variant % 4)]
            for index, slot in enumerate(slots):
                actor = ACTOR_SLOT + slot * ACTOR_STRIDE
                flags = [0x100, 0x400, 0x20100, 0x800, 0][(variant + index) % 5]
                writes += [(PENDING_ACTORS + index * 4, 4, 0 if (variant + index) % 7 == 0 else actor),
                           (actor + 4, 4, flags), (actor + 0x148, 4, 0x5a5a5a5a),
                           (actor + 0x144, 4, 0x5a5a5a5a), (actor + 0x14c, 4, 0x5a5a5a5a)]
            o.case(0x461ca2, [], writes, 0xff)

    # Scripted battle entry, for the random-encounter event and a scripted one.
    for event in [167, 100]:
        for seed in [1, 12345, 0xdeadbeef]:
            for group in [0, 1, 3]:
                writes = [(CURRENT_EVENT, 4, event), (RANDOM_STATE, 4, seed),
                          (SCRIPTED_PENDING, 4, 0), (SCRIPTED_PHASE, 4, 0x5a5a5a5a),
                          (SCRIPTED_GROUP, 4, 0x5a5a5a5a), (SCRIPTED_MUSIC, 4, 0)]
                o.case(0x44a8df, [group], writes, 0)

    # Retiring the entrance actor after its difficulty row is published.
    for index in [0, 2]:
        for step in range(4):
            actor = ACTOR_SLOT + index * ACTOR_STRIDE
            writes = [(0x77a510, 4, index), (actor, 4, index), (0x774168, 4, step),
                      (0x7760d4, 4, 1), (PARTY_IDS + index * 4, 4, 0x5a5a5a5a)]
            writes += [(0x608394 + step * 0xbc + i * 4, 4, 1000 + i) for i in range(0xbc // 4)]
            o.case(0x44ab2e, [], writes, 0)

    # Releasing the collected target lists.
    for held in [0, 1]:
        writes = [(0x774190 + i * 4, 4, 0x30000000 + i * 64 if held and i % 5 == 0 else 0)
                  for i in range(0xc9)]
        writes += [(0x775960 + i * 4, 4, 0x5a5a5a5a) for i in range(0xc9)]
        writes += [(0x7760f0 + i * 4, 4, 0x5a5a5a5a) for i in range(0xc9)]
        o.case(0x4499f0, [], writes, 0)

    # Anchored effect objects following their owner.
    effect = ACTOR_SLOT + 700 * ACTOR_STRIDE
    owner = ACTOR_SLOT + 2 * ACTOR_STRIDE
    for state in [0xffffffff, 0, 5, 10, 11]:
        for x, y, z, elevation in [(0, 0, 0, 0), (0x88000, 0x94000, 0x8000, 0x30000),
                                   (0xfffe8000, 0x138000, 0xffff8000, 0xfff00000)]:
            writes = [(effect + 0x14c, 4, state), (effect + ANCHOR_OWNER, 4, owner),
                      (effect, 4, 700), (effect + 4, 4, 0x10800),
                      (effect + 8, 4, 0x5a5a5a5a), (effect + 0xc, 4, 0x5a5a5a5a),
                      (effect + 0x10, 4, 0x5a5a5a5a), (effect + 0x1c, 4, 0x5a5a5a5a),
                      (owner + 8, 4, x), (owner + 0xc, 4, y), (owner + 0x10, 4, elevation),
                      (owner + 0x1c, 4, z)]
            o.case(0x462790, [effect], writes, 0)

    # Item ownership across equipment and the bag.
    base = [(0x607a08 + i * 0xbc + 0x74 + j * 4, 4, 0xffffffff) for i in range(16) for j in range(5)]
    for item in [63, 0x146]:
        for quantity in [0, 1, 0xffffffff, 0x7fffffff]:
            o.case(0x448a0a, [item], base + [(0x806e30 + item * 4, 4, quantity)], 0xff)
        for character in [0, 5, 12, 13, 15]:
            writes = base + [(0x806e30 + item * 4, 4, 0),
                             (0x607a08 + character * 0xbc + 0x74, 4, item)]
            o.case(0x448a0a, [item], writes, 0xff)


TILE_CHOICE, TILE_RECORDS, TILE_RECORD_COUNT = 0x7814b0, 0x786620, 0x787398
ROTATION_OFFSETS, ROTATION_OFFSET_COUNT = 0x77ece8, 0x77ffe4


def ai_helpers(oracle):
    """Distance, direction, candidate tile listing and per-rotation offsets."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    coordinates = [0, 1, 5, 0xffffffff, 0xfffffffb, 0x7fffffff, 0x80000000]
    for ax in coordinates:
        for ay in coordinates:
            o.case(0x451b30, [ax, ay, 3, 4], [], 0xffffffff)
            o.case(0x4532a5, [ax, ay], [], 0xffffffff)

    # Candidate tile listing over the recorded map dimensions. The original
    # writes its records straight up to its own counter with no bound, so keep
    # the accepted tiles under that capacity rather than recording an overflow.
    capacity = (0x787398 - TILE_RECORDS) // 24
    for both in [0, 1]:
        for variant in range(4):
            accepted = 0
            writes = []
            for c in range(width * height):
                value = [0, 1, 2, 3, 5][(c * 3 + variant * 7) % 5]
                if value & 1:
                    if accepted >= capacity - 4:
                        value = 0
                    else:
                        accepted += 1
                writes.append((TILE_CHOICE + c, 1, value))
            writes += [(TILE_RECORD_COUNT, 4, 0x5a5a5a5a)]
            writes += [(TILE_RECORDS + i * 4, 4, 0x5a5a5a5a) for i in range(24 * 64 // 4)]
            o.case(0x452014, [both], writes, 0xffffffff)

    # Per-rotation offset lists built from a published footprint.
    for handler in range(24):
        writes = [(ROTATION_OFFSETS + i * 4, 4, 0x5a5a5a5a) for i in range(0x3c8)]
        writes += [(ROTATION_OFFSET_COUNT, 4, 0x5a5a5a5a)]
        o.case(0x4529c2, [handler], writes, 0xffffffff)

    # Status rolls. The chance roll always runs; the slot roll only for a
    # random-slot attack, so the consumption order is part of the comparison.
    for seed in [1, 7, 12345, 0x7fffffff, 0xdeadbeef]:
        for chance in [0, 1, 50, 100, 0xffffffff]:
            for flags in [0, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x300, 0x1f00, 0x2000, 0x2100]:
                for immune in [0, 1, 2, 0x1f, 0xffffffff]:
                    o.case(0x44e500, [flags, immune, chance], [(RANDOM_STATE, 4, seed)], 0xffffffff)

    # Battle scene track: a fixed selector, then the random range.
    for selector in [0, 1, 2, 0xffffffff]:
        for span in [0, 1, 2]:
            for seed in [1, 12345, 0xdeadbeef]:
                o.case(0x4644d3, [], [(0x773004, 4, selector), (0x804aa8, 4, span),
                                      (RANDOM_STATE, 4, seed)], 0xffffffff)

    # Target weight of a candidate's occupant. The roll is consumed on one path.
    scratch = 0x8059f0
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    for slot, party_id in enumerate(party_ids):
        for status in [0, 0x100, 0x800, 0xa00, 0x200000, 0x400000, 0x600000, 0x600a00]:
            for seed in [1, 12345, 0xdeadbeef]:
                o.case(0x451b57, [scratch],
                       [(scratch, 4, slot), (RANDOM_STATE, 4, seed),
                        (PARTY_RECORD + party_id * PARTY_STRIDE + 8, 4, status)], 0xffffffff)


REGION_SEGMENTS, REGION_SPANS, REGION_SPAN_STRIDE = 0x7865c0, 0x780000, 144
AI_SPANS, AI_SPAN_COUNT, AI_REGION_COUNT = 0x7870b8, 0x78025c, 0x77fff8


def ai_regions(oracle):
    """Footprint rasterisation into the reach overlay and its row spans.

    The reconstruction clips footprints to the map, while the original stamps
    past the overlay when one crosses an edge and corrupts the AI metadata
    behind it. These cases keep footprints inside the map, where the two agree
    exactly; they do not cover the out-of-bounds behaviour.
    """
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    for candidates in [0, 1, 3, 6]:
        for segments in [0, 1, 3]:
            for variant in range(3):
                writes = [(AI_SPAN_COUNT, 4, 0x5a5a5a5a)]
                writes += [(REACH_OVERLAY + c, 1, 0x5a) for c in range(width * height)]
                # The span table holds exactly its own capacity; writing past it
                # would land on the candidate counter that follows.
                writes += [(AI_SPANS + i * 4, 4, 0x5a5a5a5a) for i in range(60 * 3)]
                writes.append((TILE_RECORD_COUNT, 4, candidates))
                for i in range(max(candidates, 1)):
                    record = TILE_RECORDS + i * 24
                    # Interior tiles only, so no footprint reaches an edge.
                    writes += [(record, 4, 5 + (i * 3 + variant) % (width - 12)),
                               (record + 4, 4, 5 + (i * 2 + variant) % (height - 12))]
                for region in range(4):
                    writes.append((REGION_SEGMENTS + region * 4, 4, segments))
                    for j in range(max(segments, 1)):
                        span = REGION_SPANS + region * REGION_SPAN_STRIDE + j * 12
                        writes += [(span, 4, (j + region) % 3 - 1),
                                   (span + 4, 4, -1 + variant % 2),
                                   (span + 8, 4, 1 + (j + variant) % 3)]
                for region in range(4):
                    o.case(0x452168, [region], writes, 0xffffffff)
                for regions in [1, 4]:
                    o.case(0x452255, [], writes + [(AI_REGION_COUNT, 4, regions)], 0xffffffff)


LEVELLED_ROW, SOLE_RECIPIENT, EARNED_EXPERIENCE = 0x77e578, 0x7757d8, 0x77ebf8
MOVE_REGIONS, MOVE_SPAN_COUNT, MOVE_SPANS = 0x780250, 0x77ffe8, 0x781270
TARGET_MASK_BASE, REGION_SPANS, REGION_SEGMENT_COUNT = 0x77fe00, 0x780000, 0x7865c0
SKILL_WEIGHTS, SKILL_WEIGHT_TOTAL = 0x787388, 0x780258


def turn_setup(oracle):
    """Experience payout, enemy skill list, region registration, move overlay."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    enemies = o.read(ENEMY_COUNT)

    # Experience payout: a named recipient, a downed one, and the plain split.
    for recipient in [0xffffffff, 0, 1]:
        for earned in [0, 1, 7, 1000, 0x7fffffff]:
            for pattern in range(4):
                writes = [(SOLE_RECIPIENT, 4, recipient), (EARNED_EXPERIENCE, 4, earned)]
                writes += [(LEVELLED_ROW + i, 1, 0x5a) for i in range(10)]
                for index, party_id in enumerate(party_ids):
                    record = PARTY_RECORD + party_id * PARTY_STRIDE
                    writes += [(record + 8, 4, [0, 0x200000, 0x600000, 0][(pattern + index) % 4]),
                               (record + 0x28, 4, [1, 5, 98, 99][(pattern + index) % 4]),
                               (record + 0x34, 4, 0), (record + 0x38, 4, 0)]
                    for slot in range(5):
                        writes.append((record + 0x74 + slot * 4,
                                       4, 0xe1 if (pattern + index + slot) % 5 == 0 else 0xffffffff))
                o.case(0x44de92, [], writes, 0)

    # Enemy skill list: affordable skills only, with running weights.
    for slot in range(min(enemies, 3)):
        for monster in [126, 135, 189]:
            for resource in [0, 1, 50, 0x7fffffff]:
                writes = [(ENEMY_RECORD + slot * ENEMY_STRIDE + 0x18, 4, resource),
                          (SKILL_WEIGHT_TOTAL, 4, 0x5a5a5a5a), (0x780254, 4, 0x5a5a5a5a)]
                writes += [(SKILL_SLOTS + i * 4, 4, 0x5a5a5a5a) for i in range(0x1f)]
                writes += [(SKILL_WEIGHTS + i * 4, 4, 0x5a5a5a5a) for i in range(0x1f)]
                o.case(0x452b60, [monster, slot], writes, 0)

    # Region registration over published footprints.
    for regions in [0, 1, 2, 4]:
        for variant in range(3):
            writes = [(MOVE_REGIONS, 4, regions), (AI_REGION_COUNT, 4, regions)]
            # One contiguous run per row keeps the span count inside the
            # original's own limit of twelve; a scattered mask trips its
            # assertion instead of producing a comparable result.
            def run(row, r):
                start = (row + r + variant) % 6
                return range(start, start + 2 + (row + variant) % 3)
            for r in range(4):
                writes += [(ACTION_MASK + r * 0x79 + row * 11 + column, 1,
                            4 if column in run(row, r) else 0)
                           for row in range(11) for column in range(11)]
                writes += [(TARGET_MASK_BASE + r * 0x79 + row * 11 + column, 1,
                            2 if column in run(row, r + 1) else 0)
                           for row in range(11) for column in range(11)]
                writes += [(MOVE_SPAN_COUNT + r * 4, 4, 0x5a5a5a5a),
                           (REGION_SEGMENT_COUNT + r * 4, 4, 0x5a5a5a5a)]
                writes += [(MOVE_SPANS + r * 0x90 + i * 4, 4, 0x5a5a5a5a) for i in range(36)]
                writes += [(REGION_SPANS + r * 0x90 + i * 4, 4, 0x5a5a5a5a) for i in range(36)]
            o.case(0x4520a0, [], writes, 0)

    # Movement overlay for each side.
    for side in [0, 1, 2, 3]:
        for variant in range(3):
            writes = [(CURSOR_GRID + c * 4, 4, (c * 7 + variant * 5) & 0xff)
                      for c in range(width * height)]
            writes += [(TILE_CHOICE + c, 1, 0x5a) for c in range(width * height)]
            for index in range(len(party_ids)):
                actor = ACTOR_SLOT + index * ACTOR_STRIDE
                writes += [(actor + 0x128, 4, 4 + index * 2), (actor + 0x12c, 4, 5 + index),
                           (PARTY_RECORD + party_ids[index] * PARTY_STRIDE + 8, 4,
                            [0, 0x600a00, 0x200][(variant + index) % 3])]
            for index in range(enemies):
                record = ENEMY_RECORD + index * ENEMY_STRIDE
                context = o.read(record)
                actor = ACTOR_SLOT + context * ACTOR_STRIDE
                writes += [(actor + 0x128, 4, 9 + index), (actor + 0x12c, 4, 8 + index),
                           (record + 8, 4, [0, 0x600a00, 0x800][(variant + index) % 3])]
            o.case(0x451f0d, [side], writes, 0)


VIEW_WIDTH, VIEW_HEIGHT = 0x6e12b0, 0x6e1440
BOUND_LEFT, BOUND_TOP, BOUND_RIGHT, BOUND_BOTTOM = 0x6e1468, 0x74b470, 0x74b4ac, 0x74b6c0
TRANSITION_SCRATCH = 0x8059f0


def camera(oracle):
    """View framing and the AI crowding scores."""
    o = oracle
    o.substitute_publishing(0x401d66, 4, 1, 4, TRANSITION_SCRATCH,
                            'screen transition (stdcall); its rectangle and remaining arguments '
                            'are published at 8059f0 so the comparison covers them, and no '
                            'renderer runs')
    for view in [(320, 240), (640, 480), (1067, 600), (1, 1)]:
        for bounds in [(0, 0, 1000, 800), (-100, -50, 200, 150), (0, 0, 0, 0)]:
            for x, y in [(0, 0), (500, 400), (-300, -200), (2000, 1600)]:
                writes = [(VIEW_WIDTH, 4, view[0]), (VIEW_HEIGHT, 4, view[1]),
                          (BOUND_LEFT, 4, bounds[0]), (BOUND_TOP, 4, bounds[1]),
                          (BOUND_RIGHT, 4, bounds[2]), (BOUND_BOTTOM, 4, bounds[3])]
                writes += [(TRANSITION_SCRATCH + i * 4, 4, 0x5a5a5a5a) for i in range(8)]
                o.case(0x462150, [x, y], writes, 0)
                o.case(0x462203, [x, y], writes, 0)

    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    for variant in range(4):
        for seed in [1, 12345]:
            writes = [(RANDOM_STATE, 4, seed)]
            for index, party_id in enumerate(party_ids):
                actor = ACTOR_SLOT + index * ACTOR_STRIDE
                writes += [(actor + 0x128, 4, 3 + index * 4 + variant),
                           (actor + 0x12c, 4, 2 + index * 3),
                           (PARTY_RECORD + party_id * PARTY_STRIDE + 8, 4,
                            [0, 0x600a00, 0x200, 0][(variant + index) % 4])]
            for x, y in [(0, 0), (5, 5), (20, 20), (-4, 30)]:
                o.case(0x452d02, [0, x, y, 0, 0], writes, 0xffffffff)
                o.case(0x452db8, [0, x, y, 0, 0], writes, 0xffffffff)


GROUP_FREE, GROUP_SIZE, GROUP_CELLS = 0x7760f0, 0x775960, 0x774190
STEPS_SINCE, ENCOUNTER_RARITY, FORCED_PHASE = 0x804aac, 0x773f80, 0x804aa4


def placement(oracle):
    """Threat pressure, random encounters and random placement cell picks."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    enemies = o.read(ENEMY_COUNT)

    for consider in [0, 1]:
        for variant in range(6):
            for seed in [1, 12345, 0xdeadbeef]:
                writes = [(RANDOM_STATE, 4, seed)]
                for index, party_id in enumerate(party_ids):
                    actor = ACTOR_SLOT + index * ACTOR_STRIDE
                    writes += [(actor + 0x128, 4, 4 + index * 3 + variant),
                               (actor + 0x12c, 4, 6 + index * 2),
                               (PARTY_RECORD + party_id * PARTY_STRIDE + 8, 4,
                                [0, 0x600a00, 0x200][(variant + index) % 3])]
                for index in range(enemies):
                    record = ENEMY_RECORD + index * ENEMY_STRIDE
                    actor = ACTOR_SLOT + o.read(record) * ACTOR_STRIDE
                    writes += [(actor + 0x128, 4, 5 + index * 2), (actor + 0x12c, 4, 7 + index),
                               (HURT_RATIO + index * 4, 4,
                                [0, 1, 3, 40, 60, 100, 0xffffffff][(variant + index) % 7])]
                o.case(0x45316e, [consider], writes, 0xff)

    # Random encounters: the charm, the free-record gate, the step count, the
    # blocked region, the rarity roll and every opening band.
    for seed in [1, 3, 7, 12345, 0xdeadbeef, 0x7fffffff]:
        for group in [0xffffffff, 0, 3]:
            for steps in [0, 14, 40, 60, 1000]:
                for rarity in [0, 1, 4]:
                    for forced in [0, 2]:
                        for charmed in [0, 1]:
                            writes = [(RANDOM_STATE, 4, seed), (CURRENT_MAP, 4, 457),
                                      (ENCOUNTER_OVERRIDE + 457 * 4, 4, group),
                                      (STEPS_SINCE, 4, steps), (ENCOUNTER_RARITY, 4, rarity),
                                      (FORCED_PHASE, 4, forced), (0x77ec58, 4, 0x5a5a5a5a),
                                      (0x77ec30, 4, 0x5a5a5a5a), (0x5c0230, 4, 0x5a5a5a5a),
                                      (ACTIVE_PARTY_INDEX, 4, 0),
                                      (ACTOR_SLOT + 0x110, 4, 2), (ACTIVE_HEIGHT, 4, 0),
                                      (ACTOR_SLOT + 0x128, 4, 6), (ACTOR_SLOT + 0x12c, 4, 8)]
                            writes += [(TILE_ATTRIBUTES + c * 4, 4, 0) for c in range(width * height)]
                            writes += [(BATTLE_ACTOR_RECORDS + g * 0x220, 4, 0xffffffff) for g in range(8)]
                            for index, party_id in enumerate(party_ids):
                                for slot in range(5):
                                    writes.append((PARTY_RECORD + party_id * PARTY_STRIDE + 0x74 + slot * 4,
                                                   4, 0x142 if charmed and index == 0 and slot == 0 else 0xffffffff))
                            o.case(0x448b4c, [], writes, 0xff)

    # Random placement over three groups of cells, some already taken.
    scratch = 0x8059f0
    groups = 3
    for first in range(2):
        for count in [0, 1, 2]:
            for variant in range(4):
                for seed in [1, 999, 0xdeadbeef]:
                    writes = [(RANDOM_STATE, 4, seed), (scratch, 4, 0x5a5a5a5a), (scratch + 4, 4, 0x5a5a5a5a)]
                    writes += [(MOVE_COST_GRID + c * 4, 4, 1 if (c + variant) % 3 else 0x20001)
                               for c in range(width * height)]
                    for g in range(groups + 2):
                        size = [0, 2, 4][(g + variant) % 3]
                        # Inside the compared range, so the cell pairs the pick
                        # walks are part of the comparison rather than heap the
                        # replay would have to allocate the same way.
                        block = 0x786620 + g * 128
                        writes += [(GROUP_SIZE + g * 4, 4, size),
                                   (GROUP_FREE + g * 4, 4, size),
                                   (GROUP_CELLS + g * 4, 4, block)]
                        for e in range(max(size, 1)):
                            writes += [(block + e * 8, 4, 1 + (e + variant) % (width - 2)),
                                       (block + e * 8 + 4, 4, 1 + (e + g) % (height - 2))]
                    o.case(0x44a513, [first, count, scratch, scratch + 4], writes, 0xffffffff)


def reach(oracle):
    """Which candidate tiles can cover a given target tile."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    for regions in [0, 1, 2, 4]:
        for segments in [0, 1, 3, 5]:
            for variant in range(3):
                writes = [(AI_REGION_COUNT, 4, regions), (TILE_RECORD_COUNT, 4, 6)]
                writes += [(REGION_FIRST + r * 4, 4, 0x5a5a5a5a) for r in range(4)]
                writes += [(REGION_LENGTH + r * 4, 4, 0x5a5a5a5a) for r in range(4)]
                writes += [(CANDIDATES + i * 4, 4, 0x5a5a5a5a) for i in range(16 * 40 // 4)]
                for r in range(4):
                    writes.append((REGION_SEGMENT_COUNT + r * 4, 4, segments))
                    for j in range(12):
                        span = REGION_SPANS + r * 0x90 + j * 12
                        writes += [(span, 4, (j + r) % 3 - 1),
                                   (span + 4, 4, -2 + (j + variant) % 2),
                                   (span + 8, 4, 1 + (j + r + variant) % 3)]
                for i in range(6):
                    record = TILE_RECORDS + i * 24
                    writes += [(record, 4, 4 + (i * 3 + variant) % 10),
                               (record + 4, 4, 4 + (i * 2 + variant) % 8)]
                for target in [(0, 0), (6, 5), (8, 6), (12, 9), (30, 20)]:
                    o.case(0x452497, [target[0], target[1]], writes, 0xffffffff)
                    for region in range(4):
                        o.case(0x4525e0, [target[0], target[1], region], writes, 0xffffffff)


def leave(oracle):
    """Leaving the battle scene and handing the party back to the field."""
    o = oracle
    # The viewport hand-back belongs to the host; the two actor restores are
    # original code and run for real on both sides.
    o.substitute(0x4320f6, 1, 'viewport transition boundary; no display mode change runs')
    o.substitute(0x43208d, 1, 'viewport release boundary; no display mode change runs')
    party_ids = [o.read(PARTY_IDS + i * 4) for i in range(o.read(PARTY_COUNT))]
    for map_id in [457, 424]:
        for ready in [0, 1]:
            for leader in range(len(party_ids)):
                for variant in range(4):
                    writes = [(CURRENT_MAP, 4, map_id), (0x769440, 4, ready),
                              (0x77a4fc, 4, leader), (0x80465c, 4, 9),
                              (0x5c4f5c + map_id * 0x44, 4, 1 if variant % 2 else 0)]
                    for index, party_id in enumerate(party_ids):
                        actor = ACTOR_SLOT + index * ACTOR_STRIDE
                        record = PARTY_RECORD + party_id * PARTY_STRIDE
                        writes += [(actor + 4, 4, [0x1024e, 0x400ff, 0x101ce, 0xffffffff][(variant + index) % 4]),
                                   (actor + 0x148, 4, 0x5a5a5a5a),
                                   (record + 4, 4, 0x1234 + index),
                                   (record + 8, 4, 0x5a5a5a5a),
                                   (record + 0x1c, 4, [0, 1, 40, 0xffffffff][(variant + index) % 4]),
                                   (record + 0x30, 4, [0, 0x13, 0x14, 0x40][(variant + index) % 4])]
                    for entry in [0x44df90, 0x44ab62]:
                        o.case(entry, [], writes, 0)


def motion(oracle):
    """Effect object motion toward a target point."""
    o = oracle
    obj = ACTOR_SLOT + 700 * ACTOR_STRIDE
    values = [0, 1, -1, 0x8000, -0x8000, 0x7ffffff0, -0x7ffffff0]
    for mode in [0, 1, 0x11, 0x21, 0x41, 0x71, 0x70, 0xffffffff]:
        for position in values:
            for target in values:
                for velocity, acceleration in [(0, 0), (0, 0x400), (0x1000, 0x400),
                                               (-0x1000, 0x400), (0x1000, 0)]:
                    writes = [(obj + 0x168, 4, mode)]
                    for axis, (pos, vel, acc, tgt) in enumerate(
                            [(8, 0x16c, 0x178, 0x184), (0xc, 0x170, 0x17c, 0x188),
                             (0x10, 0x174, 0x180, 0x18c)]):
                        writes += [(obj + pos, 4, position + axis),
                                   (obj + vel, 4, velocity), (obj + acc, 4, acceleration),
                                   (obj + tgt, 4, target - axis)]
                    writes += [(obj + 0x14, 4, 0x5a5a5a5a), (obj + 0x18, 4, 0x5a5a5a5a)]
                    o.case(0x462de2, [obj], writes, 0)


def effect_objects(oracle):
    """Spawning and retiring the battle's own effect objects."""
    o = oracle
    o.substitute(0x435373, 1, 'sound cue boundary; the cue is recorded, no audio device runs')
    o.substitute(0x4353cb, 1, 'sound stop boundary; the cue is recorded, no audio device runs')
    o.substitute_decimal_print(0x8594c0, 'imported decimal print (cdecl); the oracle writes the same '
                                         'decimal text and length the port service writes')
    obj = ACTOR_SLOT + 700 * ACTOR_STRIDE
    anchor = ACTOR_SLOT + 2 * ACTOR_STRIDE
    script = 0x5d26e0
    # Spawns: the object pool and the effect interpreter are original code here.
    for x, y, z in [(0, 0, 0), (0x88000, 0x94000, 0x8000), (0xfffe8000, 0x138000, 0xffff8000)]:
        for layer in [0, 0x8000, 0xffff8000]:
            for state in [0xffffffff, 0, 7]:
                writes = [(0x805680, 4, obj), (0x773f70, 4, 0)]
                o.case(0x464c27, [x, y, z, layer, script], writes, 0)
                o.case(0x464cf4, [x, y, z, layer, script, state], writes, 0)
    # Ticks: every combination of callback state and running flag.
    for state in [0xffffffff, 0, 1, 0x7fffffff]:
        for flags in [0, 0x20000, 0x10800, 0x30800, 0xffffffff]:
            for elevation in [0, 0x8000, 0xfffe0000]:
                writes = [(obj, 4, 700), (obj + 4, 4, flags), (obj + 0x14c, 4, state),
                          (obj + 0x10, 4, elevation), (obj + 0x190, 4, 5),
                          (0x805680, 4, obj + ACTOR_STRIDE), (0x773f70, 4, 0)]
                for entry in [0x464c05, 0x464d2f, 0x46521e, 0x4653a5]:
                    o.case(entry, [obj], writes + [(obj + 0x160, 4, anchor),
                                                   (anchor + 0x148, 4, 0),
                                                   (anchor + 0x14c, 4, 0)], 0)

    # Effect ticks with their own work fields and lifetimes.
    for seed in [1, 12345]:
        for state in [0xffffffff, 0, 1, 10]:
            for age in [0, 8, 9, 0x18, 0x19, 0x30, 0x31, 0x50, 0x51]:
                for elevation in [0, 1, 0xffffffff, 0x30000]:
                    writes = [(RANDOM_STATE, 4, seed), (obj, 4, 700), (obj + 4, 4, 0x10840),
                              (obj + 0x14c, 4, state), (obj + 0x144, 4, age),
                              (obj + 0x10, 4, elevation), (obj + 0x160, 4, anchor),
                              (obj + 0x16c, 4, age), (obj + 0x178, 4, 0),
                              (obj + 0x19c, 4, 0x1000), (obj + 0x1a4, 4, 0x5a5a5a5a),
                              (0x806514, 4, age % 2), (0x805680, 4, obj + ACTOR_STRIDE),
                              (0x773f70, 4, 0), (anchor, 4, 2),
                              (anchor + 8, 4, 0x88000), (anchor + 0xc, 4, 0x94000),
                              (anchor + 0x10, 4, 0x8000), (anchor + 0x1c, 4, 0)]
                    for entry in [0x46686d, 0x466cc9, 0x465dc2, 0x46265d]:
                        o.case(entry, [obj], writes, 0)
    # The debris burst emits on a schedule and marks its last particle.
    for emitted in range(5):
        for age in [0, 4, 8]:
            writes = [(RANDOM_STATE, 4, 7), (obj, 4, 700), (obj + 4, 4, 0x10840),
                      (obj + 0x14c, 4, 0), (obj + 0x144, 4, age), (obj + 0x16c, 4, age),
                      (obj + 0x178, 4, emitted), (obj + 0x160, 4, anchor),
                      (0x805680, 4, obj + ACTOR_STRIDE), (0x773f70, 4, 0), (anchor, 4, 2),
                      (anchor + 8, 4, 0x88000), (anchor + 0xc, 4, 0x94000),
                      (anchor + 0x10, 4, 0x8000), (anchor + 0x1c, 4, 0)]
            o.case(0x465b14, [obj], writes, 0)

    # Falling strikes, debris and the explosion run.
    for seed in [1, 12345]:
        for state in [0xffffffff, 0, 10, 1]:
            for age in [0, 0x10, 0x20]:
                for marked in [0, 4]:
                    for anchor_x in [0x88000, 0x188000]:
                        writes = [(RANDOM_STATE, 4, seed), (obj, 4, 700),
                                  (obj + 4, 4, 0x10840 | (marked << 16)),
                                  (obj + 0x14c, 4, state), (obj + 0x144, 4, age),
                                  (obj + 0x16c, 4, age), (obj + 0x170, 4, age % 11),
                                  (obj + 0x120, 4, 0xffffff00 if age else 0),
                                  (obj + 8, 4, anchor_x), (obj + 0xc, 4, 0x94000),
                                  (obj + 0x10, 4, 0x8000), (obj + 0x1c, 4, 0),
                                  (obj + 0x190, 4, anchor_x - 0x80000),
                                  (obj + 0x160, 4, anchor), (0x80650c, 4, 0x5a5a5a5a),
                                  (0x7873c0, 4, 0x180), (0x805680, 4, obj + ACTOR_STRIDE),
                                  (0x773f70, 4, 0), (anchor, 4, 2),
                                  (anchor + 8, 4, anchor_x), (anchor + 0xc, 4, 0x94000),
                                  (anchor + 0x10, 4, 0x8000), (anchor + 0x1c, 4, 0),
                                  (anchor + 0x18, 4, 0x94000), (anchor + 0x148, 4, 0),
                                  (anchor + 0x14c, 4, 0)]
                        for entry in [0x46528d, 0x466015]:
                            o.case(entry, [obj], writes, 0)
                        # 465abd hands the new particle back to its caller.
                        o.case(0x465abd, [anchor], writes, 0xffffffff)
                        o.case(0x46532a, [anchor], writes, 0)

    # Ticks with their own schedules and phase changes.
    for seed in [1, 12345]:
        for state in [0xffffffff, 0, 10, 20, 30, 1]:
            for age in [0, 1, 7, 8, 0x20, 0x30, 0x31, 0x40, 0x41]:
                for marked in [0, 4]:
                    for gate in [0, 1]:
                        writes = [(RANDOM_STATE, 4, seed), (obj, 4, 700),
                                  (obj + 4, 4, 0x10840 | (marked << 16)),
                                  (obj + 0x14c, 4, state), (obj + 0x144, 4, age),
                                  (obj + 0x10, 4, 0x300000 if gate else 0),
                                  (obj + 0x16c, 4, age % 5), (obj + 0x170, 4, age),
                                  (obj + 0x174, 4, gate * 0x1000),
                                  (obj + 0x180, 4, 0x8000), (obj + 0x160, 4, anchor),
                                  (0x80650c, 4, gate), (0x806514, 4, 0x5a5a5a5a),
                                  (0x8059f8, 4, anchor if gate else 0),
                                  (0x805680, 4, obj + ACTOR_STRIDE), (0x773f70, 4, 0),
                                  (anchor, 4, 2), (anchor + 8, 4, 0x88000),
                                  (anchor + 0xc, 4, 0x94000), (anchor + 0x10, 4, 0x8000),
                                  (anchor + 0x1c, 4, 0), (anchor + 0x148, 4, 0),
                                  (anchor + 0x14c, 4, 0)]
                        writes2 = writes + [(obj + 0x190, 4, age * 8), (obj + 0x194, 4, age),
                                            (obj + 0x198, 4, 0x80000), (obj + 0x19c, 4, 0),
                                            (obj + 0x1a0, 4, 0), (obj + 0x184, 4, 0),
                                            (obj + 0x188, 4, 0), (0x806510, 4, gate)]
                        writes3 = writes2 + [(obj + 0x1a0, 4, age * 0x1000),
                                             (obj + 0x1a4, 4, 0x400 + age * 0x40),
                                             (obj + 0x178, 4, anchor)]
                        writes4 = writes3 + [(obj + 0x18c, 4, 0x1b00000 if gate else 0),
                                             (obj + 0x190, 4, age * 4),
                                             (obj + 0x194, 4, age * 8)]
                        # Floating numbers keep their digits as text at +3c.
                        text = b'123' if gate else b'0'
                        writes5 = writes4 + [(obj + 0x3c + i, 1, v) for i, v in enumerate(text + b'\0')]
                        writes5 += [(obj + 0x190, 4, age % 4), (obj + 0x194, 4, 0x88000),
                                    (obj + 0x198, 4, anchor), (obj + 0x1a8, 4, anchor),
                                    (obj + 0x10, 4, [0, 0x1a80000, 0x1c00000, 0x1cc0000][age % 4])]
                        for entry in [0x465ee1, 0x466da7, 0x466545, 0x466992, 0x46596b,
                                      0x466309, 0x466ed1, 0x46562e, 0x46570a,
                                      0x4645e7, 0x464512, 0x46474d, 0x464a9d]:
                            o.case(entry, [obj], writes5, 0)
                        for amount in [0xffffffff, 0, 7, 1234, 0x10270f]:
                            for entry in [0x4646be, 0x464b74, 0x4647ad, 0x464925]:
                                o.case(entry, [anchor, amount], writes5, 0)

    # Scattered spawners: each draws its own rolls in its own order.
    for seed in [1, 5, 12345, 0xdeadbeef, 0x7fffffff]:
        for ax, ay, az, layer in [(0, 0, 0, 0), (0x88000, 0x94000, 0x8000, 0x30000),
                                  (0xfffe8000, 0x138000, 0xffff8000, 0xffff0000),
                                  (0x188000, 0xfffe0000, 0x40000, 0)]:
            writes = [(RANDOM_STATE, 4, seed), (0x805680, 4, obj), (0x773f70, 4, 0),
                      (anchor, 4, 2), (anchor + 8, 4, ax), (anchor + 0xc, 4, ay),
                      (anchor + 0x10, 4, az), (anchor + 0x1c, 4, layer)]
            o.case(0x465f7e, [anchor], writes, 0xffffffff)
            for reach in [0, 1, 0x20, 0x3e, 0x3f, 0x40, 0x7fffffff]:
                o.case(0x46648b, [anchor, reach], writes, 0xffffffff)
            for entry in [0x465e0b, 0x466df3, 0x467040, 0x4657d7]:
                o.case(entry, [anchor], writes, 0)
            for entry in [0x46688c, 0x46691c, 0x466cfc]:
                o.case(entry, [anchor], writes, 0)

    # Anchored spawners: the object pool, the interpreter and the cue boundary.
    for seed in [1, 12345, 0xdeadbeef]:
        for x, y, z, layer in [(0, 0, 0, 0), (0x88000, 0x94000, 0x8000, 0x30000),
                               (0xfffe8000, 0x138000, 0xffff8000, 0xffff0000)]:
            writes = [(RANDOM_STATE, 4, seed), (0x805680, 4, obj), (0x773f70, 4, 0),
                      (anchor + 8, 4, x), (anchor + 0xc, 4, y), (anchor + 0x10, 4, z),
                      (anchor + 0x1c, 4, layer), (anchor, 4, 2)]
            for entry in [0x465b66, 0x4665ca, 0x466a98, 0x4660a3, 0x465241, 0x4653d9]:
                o.case(entry, [anchor], writes, 0)


def placement_entry(oracle):
    """Run the original enemy-placement caller with its actual stack locals."""
    o = oracle
    width, height = o.read(GRID_WIDTH), o.read(GRID_HEIGHT)
    block = 0x786620
    for seed in [1, 999, 0xdeadbeef]:
        for facing in [0, 6]:
            for state in [0, 1, 2]:
                writes = [(RANDOM_STATE, 4, seed), (ENEMY_COUNT, 4, 2),
                          (GROUP_SIZE, 4, 4), (GROUP_FREE, 4, 4), (GROUP_CELLS, 4, block)]
                writes += [(MOVE_COST_GRID + cell * 4, 4, 1) for cell in range(width * height)]
                for index in range(4):
                    writes += [(block + index * 8, 4, 2 + index), (block + index * 8 + 4, 4, 3)]
                for index in range(2):
                    writes += [(0x806b64 + index * 36, 4, index)]
                # Stride zero directs every disposition to the prepared bucket.
                o.case(0x44a623, [0, 0, 1, 0, facing, state], writes, 0xffffffff)


def action_controllers(oracle):
    """Battle action controllers stepped by the states their effects report."""
    o = oracle
    o.substitute(0x435373, 1, 'sound cue boundary; the cue is recorded, no audio device runs')
    o.substitute(0x4353cb, 1, 'sound stop boundary; the cue is recorded, no audio device runs')
    o.substitute_decimal_print(0x8594c0, 'imported decimal print (cdecl); the oracle writes the same '
                                         'decimal text and length the port service writes')
    # The screen flash itself is original code on both sides; only the palette
    # device calls inside it are substituted, so its own state machine and the
    # controller's are compared while no display runs.
    for entry, args in [(0x404c56, 3), (0x404dbb, 4), (0x40536b, 4), (0x405572, 0)]:
        o.substitute(entry, args, 'palette device boundary (stdcall); arguments are recorded and '
                                  'the poll reports "settled", so no display runs')
    controller = ACTOR_SLOT + 700 * ACTOR_STRIDE
    caster = ACTOR_SLOT + 2 * ACTOR_STRIDE
    target = ACTOR_SLOT + 3 * ACTOR_STRIDE
    # A per-facing script table of the shape the battle passes in, placed in
    # scratch inside the compared range.
    scripts = 0x786620
    for state in [0xffffff38, 0xffffff06, 0xfffffefc, 0xffffff9c, 0xffffffec,
                  0xfffffff6, 0xfffffffe, 0xffffffff, 0, 10, 30, 40, 20]:
        for facing in range(4):
            writes = [(controller, 4, 700), (controller + 4, 4, 0x10800),
                      (controller + 0x14c, 4, state),
                      (ACTING_ACTOR, 4, caster), (ACTION_TARGET, 4, target),
                      (caster, 4, 2), (caster + 4, 4, 0x1024f), (caster + 0x110, 4, facing),
                      (target, 4, 3), (target + 4, 4, 0x1024e),
                      (target + 8, 4, 0x88000), (target + 0xc, 4, 0x94000),
                      (target + 0x10, 4, 0x8000), (target + 0x1c, 4, 0),
                      (0x805680, 4, controller), (0x773f70, 4, 0)]
            writes += [(scripts + i * 4, 4, 0x5d26e0) for i in range(4)]
            o.case(0x4658de, [controller, scripts], writes, 0)
            # The target-effect controllers walk a real target list.
            targets = [ACTOR_SLOT + (3 + i) * ACTOR_STRIDE for i in range(3)]
            for listed in [0, 1, 3]:
                shared = writes + [(TARGET_COUNT, 4, listed)]
                shared += [(ACTION_TARGET + i * 4, 4, targets[i]) for i in range(3)]
                for i, target in enumerate(targets):
                    shared += [(target, 4, 3 + i), (target + 4, 4, 0x1024e),
                               (target + 8, 4, 0x88000 + i * 0x10000), (target + 0xc, 4, 0x94000),
                               (target + 0x10, 4, 0x8000), (target + 0x1c, 4, 0),
                               (target + 0x110, 4, i % 4), (target + 4, 4, 0x1024e | (0x100 * (i % 2))),
                               (target + 0x148, 4, 0),
                               (target + 0x14c, 4, 0)]
                # A reporter outside the list makes the original index one past
                # its end and read whatever follows, so only listed reporters
                # are compared here.
                for owner in [targets[0], targets[2]]:
                    for pending, reports, flag in [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)]:
                        case_writes = shared + [(controller + 0x1a8, 4, owner),
                                                (controller + 0x78, 2, pending),
                                                (controller + 0x7a, 2, reports)]
                        case_writes += [(controller + 0x3c + i * 2, 2, flag) for i in range(4)]
                        case_writes += [(controller + 0x7c, 2, pending),
                                        (0x806510, 4, 0x5a5a5a5a), (0x806514, 4, 0x5a5a5a5a)]
                        case_writes += [(0x773f90 + i * 0x10, 4, 10 + i * 7) for i in range(4)]
                        for entry in [0x465b7d, 0x4660c4, 0x466aaf, 0x4665e1, 0x467111]:
                            o.case(entry, [controller, scripts], case_writes, 0)


ACTING_ACTOR, ACTION_TARGET, TARGET_COUNT = 0x8059f0, 0x8059f8, 0x77a50c

SCOPES = {'status_rules': status_rules, 'poses': poses, 'ai_scoring': ai_scoring,
          'grid': grid_tables, 'targeting': targeting, 'flow': progression,
          'ai_helpers': ai_helpers, 'ai_regions': ai_regions, 'turn_setup': turn_setup,
          'camera': camera, 'placement': placement, 'reach': reach, 'leave': leave,
          'motion': motion, 'placement_entry': placement_entry,
          'effect_objects': effect_objects, 'actions': action_controllers}


def main(snapshot, output, scope='status_rules'):
    oracle = Oracle(snapshot)
    SCOPES[scope](oracle)
    oracle.write(output, scope,
                 'Original battle status, turn, relation and grid-query entries executed from a '
                 'recorded encounter. Every callee inside these entries is original code.')


if __name__ == '__main__':
    main(*sys.argv[1:])
