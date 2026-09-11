#!/usr/bin/env python3
"""Run the ring effect and its sweep controller in the original and record them.

Covered entry points:
  467417  spawn one ring of four children around an owner object
  4672b5  one frame of one ring child
  4674de  the all-target sweep controller
  46dca5  one frame of a travelling hit particle
  46a8e9, 46b772, 46d904, 46dcd7, 46e344, 46e8b3, 46f684, 46f92f, 470b90
          the nine helpers that spawn children for 46dca5
  468790  one frame of a projectile
  4687f3, 46bd92, 47c807  the three launchers that spawn children for 468790
  46a1b6  one frame of a particle whose animation follows its elevation
  467db3, 4681d6, 4860ff  three small spawn helpers
  481b11, 46dfc3, 46c5bb, 4719e2, 4705c6, 478c12, 485c52, 47eb35, 4727b4,
  482bd8, 4682cc, 48613c  effects that step and then finish
  4692c6  stopping the running screen shake
  4692e5, 46d24c, 46ed0e, 46f1f6, 47fcb7, 47f526, 47dc33, 483380, 4823e1,
  484f62, 46e013, 4837df, 47219f  effects that travel and report on landing
  46dfdf  the trail sprite 46e013 lays down
  47be07, 485ccf  two target-fanout skill sequences
  47bde6, 485c80  the children those two give each target
  467be6, 46fffd  two more skill sequences
  467bcf, 46ff7a, 46ff40  their children
  483179, 482f4d  the single- and multi-target particle attacks, whose second
          argument is a weapon tier rather than a script table

The helpers these call belong to other scopes and are stopped at their entry,
except the shared CRT random stream at 498090, which runs for real so the
number of draws and the resulting stream state are compared too.

The object spawn at 45d89c is replaced by a fixture that hands out successive
addresses from a pre-zeroed scratch pool. The C++ side is given the same
substitute, so what the comparison covers is this scope's own writes and call
order, not 45d89c's allocation behaviour.

Expected values come from the original binary only.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2]))
from prepare_event0 import PE  # noqa: E402
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX  # noqa: E402

SOURCE_SHA = '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
DATA_BASE, DATA_BYTES = 0x4a5000, 3882100
STACK, RETURN_MARKER = 0x100f000, 0x1000000
OBJECT_BYTES = 428
SLOT = 0x8073d8

SPAWN_RING, TICK_CHILD, RUN_SWEEP = 0x467417, 0x4672b5, 0x4674de
TICK_TRAVEL = 0x46dca5
TRAVEL_SPAWNERS = [0x46a8e9, 0x46b772, 0x46d904, 0x46dcd7, 0x46e344,
                   0x46e8b3, 0x46f684, 0x46f92f, 0x470b90]
TICK_PROJECTILE = 0x468790
LAUNCHERS = [0x4687f3, 0x46bd92, 0x47c807]
ANIMATED_TICK = 0x46a1b6
SMALL_SPAWNERS = [0x467db3, 0x4681d6, 0x4860ff]
FINISHING = [0x481b11, 0x46dfc3, 0x46c5bb, 0x4719e2, 0x4705c6, 0x478c12,
             0x485c52, 0x47eb35, 0x4727b4, 0x482bd8, 0x4682cc, 0x48613c]
STOP_SHAKE = 0x4692c6
IMPACTS = [0x4692e5, 0x46d24c, 0x46ed0e, 0x46f1f6, 0x47fcb7, 0x47f526, 0x47dc33,
           0x483380, 0x4823e1, 0x484f62, 0x46e013, 0x4837df, 0x47219f]
TRAIL_SPRITE = 0x46dfdf
FANOUT_SEQUENCES = [0x47be07, 0x485ccf, 0x467be6, 0x46fffd]
FANOUT_CHILDREN = [0x47bde6, 0x485c80, 0x467bcf, 0x46ff7a]
DESCENDING_TICK = 0x46ff40
PARTICLE_ATTACKS = [0x483179, 0x482f4d]
FIRST_TARGET_BURST = 0x4693ef
EXPAND_SPAWNER = 0x469321
FLASH_DRIFT = 0x467ed6
FLASH_NOTIFY = [0x467e8c, 0x467e47]
ACTIVE_SCREEN_SHAKE = 0x806520

# Stopped helpers: address -> pushed argument count.
STOPPED = {
    0x45d89c: 1,   # spawn runtime callback object; the fixture supplies the result
    0x45d91d: 1,   # finalize runtime callback object
    0x45d208: 1,   # apply object oscillation motion
    0x466cfc: 1,   # spawn swirl particle
    0x461d25: 2,   # notify the active effect controller
    0x447841: 2,   # run a battle effect script
    0x435373: 1,   # play a sound cue
    0x4647ad: 2,   # spawn a floating number with a glyph set
    0x464c59: 2,   # battle screen flash transition
    0x462de2: 1,   # apply object homing motion clamp
    0x4353cb: 1,   # stop the last sound cue buffer
    0x464c27: 5,   # place an effect object at an explicit point
    0x4646be: 2,   # spawn a floating number
    0x45db6c: 1,   # restore an object's motion block
    0x464cf4: 6,   # place an effect object that reports when it finishes
}
RESULT_BASE = 0x5eed0000

ACTIVE_ACTOR_POINTER = 0x8059f0
TARGET_OBJECTS = 0x8059f8
TARGET_COUNT = 0x77a50c
RESULT_AMOUNTS = 0x773f90

PHASE, FLAGS = 0x14c, 4
OWNER_OBJECT, NOTIFY_TARGET = 0x1a8, 0x178
SCRIPT_INDEX = 0x110

# Scratch objects, past the slots the other fixtures use.
OWNER = SLOT + 690 * OBJECT_BYTES
CONTROLLER = SLOT + 691 * OBJECT_BYTES
ACTOR = SLOT + 692 * OBJECT_BYTES
TARGETS = [SLOT + (693 + i) * OBJECT_BYTES for i in range(3)]
POOL = [SLOT + (700 + i) * OBJECT_BYTES for i in range(40)]


def main(executable, output):
    pe = PE(executable)
    sha = hashlib.sha256(pe.data).hexdigest()
    assert sha == SOURCE_SHA, sha
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x400000, 0x600000)
    uc.mem_map(0x1000000, 65536)
    for _, va, size, offset in pe.sections:
        uc.mem_write(pe.base + va, pe.data[offset:offset + size])
    pristine = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))

    observed = []
    flash_result = [1]
    pool_cursor = [0]

    def stop(instance, address, size, _user):
        sp = instance.reg_read(UC_X86_REG_ESP)
        count = STOPPED[address]
        words = list(struct.unpack_from('<' + 'I' * count, instance.mem_read(sp + 4, 4 * count)))
        observed.append((address, words))
        if address == 0x45d89c:
            value = POOL[pool_cursor[0]]
            pool_cursor[0] += 1
        elif address == 0x464c59:
            value = flash_result[0]
        else:
            value = RESULT_BASE | (address & 0xffff)
        instance.reg_write(UC_X86_REG_EAX, value)
        instance.reg_write(UC_X86_REG_EIP, struct.unpack('<I', instance.mem_read(sp, 4))[0])
        instance.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * count)

    for target in STOPPED:
        uc.hook_add(UC_HOOK_CODE, stop, begin=target, end=target)

    def zero_pool():
        return [(address + offset, 0) for address in POOL for offset in range(0, OBJECT_BYTES, 4)]

    cases = []

    # 4672b5: one frame of a ring child, over its phases and boundaries.
    for phase in (-1, 0, 10, 20, 5):
        for notify in (0, 0x40000):
            for suppress in (0, 1):
                for elevation, radius, step in ((0, 0x18, 0x400), (0x5ffff0, 1, 0xff0),
                                                (0x600001, 0, 0x1000)):
                    setup = [(OWNER + PHASE, phase & 0xffffffff), (OWNER + FLAGS, notify),
                             (OWNER + 0x10, elevation), (OWNER + 0x144, suppress),
                             (OWNER + 0x190, radius), (OWNER + 0x194, radius),
                             (OWNER + 0x198, 3), (OWNER + 0x19c, 0x1234),
                             (OWNER + 0x1a0, 0xfff0), (OWNER + 0x1a4, step),
                             (OWNER + NOTIFY_TARGET, CONTROLLER)]
                    cases.append((TICK_CHILD, [OWNER], setup, 1))

    # Signed32 overflow is defined by the original ADD/SUB, not C++ signed math.
    for phase in (0, 10, 20):
        for value in (0x7fffffff, 0x80000000, 0xffffffff):
            setup = [(OWNER + PHASE, phase), (OWNER + FLAGS, 0),
                     (OWNER + 0x10, value), (OWNER + 0x144, 1),
                     (OWNER + 0x190, value), (OWNER + 0x194, value),
                     (OWNER + 0x198, value), (OWNER + 0x1a0, value),
                     (OWNER + 0x1a4, value), (OWNER + NOTIFY_TARGET, CONTROLLER)]
            cases.append((TICK_CHILD, [OWNER], setup, 1))

    # 467417: spawn a ring around an owner. Four children come from the pool.
    for x, y, sprite in ((0, 0, 0), (0x120000, -0x30000 & 0xffffffff, 7)):
        setup = zero_pool() + [(OWNER + 8, x), (OWNER + 0xc, y), (OWNER + 0x1c, sprite)]
        cases.append((SPAWN_RING, [OWNER], setup, 1))

    # 4674de: the sweep controller, over its states and notify codes.
    scripts = 0x5d2dd8  # Any readable table of script pointers works as the argument.
    for state in (-250, -200, -100, -20, 0, 10, 30, 40, 7):
        for count in (0, 2):
            for pending in (0, 1):
                for flash in (0, 1):
                    setup = zero_pool() + [
                        (CONTROLLER + PHASE, state & 0xffffffff),
                        (CONTROLLER + 0x78, pending), (CONTROLLER + 0x7a, pending),
                        (CONTROLLER + 0x3c, pending),
                        (CONTROLLER + OWNER_OBJECT, TARGETS[1]),
                        (TARGET_COUNT, count), (ACTIVE_ACTOR_POINTER, ACTOR),
                        (ACTOR + SCRIPT_INDEX, 1), (ACTOR + FLAGS, 0x10000),
                    ]
                    for index, target in enumerate(TARGETS):
                        setup += [(TARGET_OBJECTS + 4 * index, target),
                                  (target + SCRIPT_INDEX, index),
                                  (target + FLAGS, 0x100 if index else 0),
                                  (RESULT_AMOUNTS + 16 * index, 40 + index)]
                    cases.append((RUN_SWEEP, [CONTROLLER, scripts], setup, flash))

    # 46dca5: the travel tick. 462de2 is stopped, so the motion flags decide
    # whether the particle counts as still travelling.
    for phase in (0, 1):
        for flags in (0x31, 0x30, 0x01, 0x00, 0x11):
            cases.append((TICK_TRAVEL, [OWNER],
                          [(OWNER + PHASE, phase), (OWNER + 0x168, flags)], 1))

    # The nine helpers, over the facings that select a drift axis and past them.
    for entry in TRAVEL_SPAWNERS:
        for facing in range(6):
            setup = zero_pool() + [
                (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000),
                (OWNER + 0x1c, 5), (OWNER + 0x110, facing),
                (CONTROLLER + 8, -0x40000 & 0xffffffff), (CONTROLLER + 0xc, 0x90000),
                (CONTROLLER + 0x10, 0x70000),
            ]
            cases.append((entry, [OWNER, CONTROLLER], setup, 1))

    # Integration review: ADD of lift must wrap at the signed32 boundary.
    for entry in TRAVEL_SPAWNERS:
        for value in (0x7fffffff, 0x80000000, 0xffffffff):
            setup = zero_pool() + [(OWNER + 0x110, 0), (OWNER + 8, value),
                (OWNER + 0xc, value), (OWNER + 0x10, value),
                (CONTROLLER + 8, value), (CONTROLLER + 0xc, value),
                (CONTROLLER + 0x10, value)]
            cases.append((entry, [OWNER, CONTROLLER], setup, 1))

    # 468790: the projectile tick, over its phases and both report flags.
    for phase in (0, 10, 5):
        for flags in (0, 0x20000, 0x40000, 0x60000):
            for motion in (0x71, 0x70, 0x01, 0x00):
                cases.append((TICK_PROJECTILE, [OWNER],
                              [(OWNER + PHASE, phase), (OWNER + FLAGS, flags),
                               (OWNER + 0x168, motion), (OWNER + 0x160, CONTROLLER)], 1))

    # The three launchers. The anchor is kept far enough from the start that the
    # frame count never divides by zero, which is what the original would do.
    for entry in LAUNCHERS:
        for facing in range(6):
            setup = zero_pool() + [
                (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000),
                (OWNER + 0x1c, 5), (OWNER + 0x110, facing),
                (CONTROLLER + 8, 0x2110000), (CONTROLLER + 0xc, 0x1220000),
                (CONTROLLER + 0x10, 0x70000),
            ]
            cases.append((entry, [CONTROLLER, OWNER], setup, 1))

    # The effects that step and then finish, over phase, script flag, motion
    # flags, elevation and tick count.
    for entry in FINISHING:
        for phase in (0, 1):
            for flags in (0, 0x20000):
                for motion in (0x71, 0x30, 0x00):
                    for elevation in (0x10000, 0, -0x10000 & 0xffffffff):
                        for ticks in (0, 0x14):
                            cases.append((entry, [OWNER], zero_pool() + [
                                (OWNER + PHASE, phase), (OWNER + FLAGS, flags),
                                (OWNER + 0x168, motion), (OWNER + 0x10, elevation),
                                (OWNER + 0x144, ticks), (OWNER + 0x160, CONTROLLER),
                                (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000),
                                (OWNER + 0x1c, 5), (ACTIVE_ACTOR_POINTER, ACTOR),
                                (ACTOR + 0x1c, 9)], 1))

    # 46a1b6, over the elevations that pick each animation stage.
    for phase in (0, 1):
        for elevation in (0, 0x30000, 0x500000, 0x800000, 0x810000):
            for running in (0, 0x5d0000):
                cases.append((ANIMATED_TICK, [OWNER], [
                    (OWNER + PHASE, phase), (OWNER + 0x10, elevation),
                    (OWNER + 0x150, running)], 1))

    for entry in SMALL_SPAWNERS:
        setup = zero_pool() + [(OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000),
                               (OWNER + 0x10, 0x30000), (OWNER + 0x1c, 5),
                               (ACTIVE_ACTOR_POINTER, ACTOR), (ACTOR + 0x1c, 9)]
        cases.append((entry, [OWNER], setup, 1))

    # The impact effects, over phase, motion flags, elevation, tick count and
    # the owner-notify flag that 47219f reads.
    for entry in IMPACTS:
        for phase in (0, 10, 1):
            for motion in (0x31, 0x30, 0x01, 0x41):
                for elevation in (0, 0x1f40000, 0x2000000):
                    for ticks in (0, 0x3c):
                        for notify in (0, 0x40000):
                            cases.append((entry, [OWNER], zero_pool() + [
                                (OWNER + PHASE, phase), (OWNER + FLAGS, notify),
                                (OWNER + 0x168, motion), (OWNER + 0x10, elevation),
                                (OWNER + 0x144, ticks), (OWNER + 0x160, CONTROLLER),
                                (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000),
                                (OWNER + 0x1c, 5), (TARGET_OBJECTS, TARGETS[0])], 1))

    cases.append((TRAIL_SPRITE, [OWNER], zero_pool() + [
        (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000),
        (OWNER + 0x1c, 5)], 1))

    # The two fanout sequences, over every phase and both flash outcomes.
    for entry in FANOUT_SEQUENCES:
        for state in (-250, -200, -110, -100, -20, -11, -14, 0, 10, 30, 40, 20, 7):
            for count in (0, 2):
                for pending in (0, 1):
                    for flash in (0, 1):
                        setup = zero_pool() + [
                            (CONTROLLER + PHASE, state & 0xffffffff),
                            (CONTROLLER + 0x78, pending), (CONTROLLER + 0x7a, pending),
                            (CONTROLLER + 0x3c, pending), (CONTROLLER + 0x40, pending),
                            (CONTROLLER + OWNER_OBJECT, TARGETS[1]),
                            (TARGET_COUNT, count), (ACTIVE_ACTOR_POINTER, ACTOR),
                            (ACTOR + SCRIPT_INDEX, 1), (ACTOR + FLAGS, 0x10000),
                            (ACTOR + 0x1c, 9),
                        ]
                        for index, tgt in enumerate(TARGETS):
                            setup += [(TARGET_OBJECTS + 4 * index, tgt),
                                      (tgt + SCRIPT_INDEX, index), (tgt + FLAGS, 0x10000),
                                      (tgt + 8, 0x110000 + index), (tgt + 0xc, 0x220000),
                                      (tgt + 0x10, 0x30000), (tgt + 0x1c, 5)]
                        cases.append((entry, [CONTROLLER, 0x5d2dd8], setup, flash))

    for entry in FANOUT_CHILDREN:
        cases.append((entry, [OWNER], zero_pool() + [
            (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000),
            (OWNER + 0x1c, 5)], 1))

    # The particle attacks take a burst count as their second argument.
    for entry in PARTICLE_ATTACKS:
        for state in (-150, -100, -20, -11, -13, -1, 0, 20, 30, 7):
            for count in (0, 2):
                for cursor in (0, 2):
                    for pending in (0, 1):
                        setup = zero_pool() + [
                            (CONTROLLER + PHASE, state & 0xffffffff),
                            (CONTROLLER + 0x78, cursor), (CONTROLLER + 0x7c, pending),
                            (CONTROLLER + 0x3c, pending),
                            (CONTROLLER + OWNER_OBJECT, TARGETS[1]),
                            (TARGET_COUNT, count), (ACTIVE_ACTOR_POINTER, ACTOR),
                            (ACTOR + SCRIPT_INDEX, 1), (ACTOR + FLAGS, 0x10000), (ACTOR + 0x1c, 9),
                        ]
                        for index, tgt in enumerate(TARGETS):
                            setup += [(TARGET_OBJECTS + 4 * index, tgt),
                                      (tgt + SCRIPT_INDEX, index), (tgt + FLAGS, 0x10000),
                                      (tgt + 8, 0x110000 + index), (tgt + 0xc, 0x220000),
                                      (tgt + 0x10, 0x30000), (tgt + 0x1c, 5),
                                      (RESULT_AMOUNTS + 16 * index, 40 + index)]
                        cases.append((entry, [CONTROLLER, 2], setup, 1))

    for state in (-210, -200, -100, -20, -11, -13, 0, 10, 30, 40, 7):
        for count in (0, 2):
            for pending in (0, 1):
                for flash in (0, 1):
                    setup = zero_pool() + [
                        (CONTROLLER + PHASE, state & 0xffffffff),
                        (CONTROLLER + 0x78, pending), (CONTROLLER + 0x7a, pending),
                        (CONTROLLER + 0x7c, pending), (CONTROLLER + 0x3c, pending),
                        (CONTROLLER + OWNER_OBJECT, TARGETS[1]),
                        (TARGET_COUNT, count), (ACTIVE_ACTOR_POINTER, ACTOR),
                        (ACTOR + SCRIPT_INDEX, 1), (ACTOR + FLAGS, 0x10000), (ACTOR + 0x1c, 9),
                        (ACTOR + 8, 0x330000), (ACTOR + 0xc, 0x440000), (ACTOR + 0x10, 0x50000),
                    ]
                    for index, tgt in enumerate(TARGETS):
                        setup += [(TARGET_OBJECTS + 4 * index, tgt),
                                  (tgt + SCRIPT_INDEX, index), (tgt + FLAGS, 0x10000),
                                  (tgt + 8, 0x110000 + index), (tgt + 0xc, 0x220000),
                                  (tgt + 0x10, 0x30000), (tgt + 0x1c, 5),
                                  (RESULT_AMOUNTS + 16 * index, 40 + index)]
                    cases.append((FIRST_TARGET_BURST, [CONTROLLER, 0x5d2dd8], setup, flash))

    for state in (-300, -250, -200, -100, -20, -11, -13, -1, 0, 10, 30, 40, 50, 7):
        for count in (0, 2):
            for cursor in (0, 2):
                for ticks in (0, 8):
                    for flash in (0, 1):
                        setup = zero_pool() + [
                            (CONTROLLER + PHASE, state & 0xffffffff),
                            (CONTROLLER + 0x78, cursor), (CONTROLLER + 0x7a, 0),
                            (CONTROLLER + 0x7c, count), (CONTROLLER + 0x3c, 0),
                            (CONTROLLER + 0x144, ticks),
                            (CONTROLLER + OWNER_OBJECT, TARGETS[1]),
                            (TARGET_COUNT, count), (ACTIVE_ACTOR_POINTER, ACTOR),
                            (ACTOR + SCRIPT_INDEX, 1), (ACTOR + FLAGS, 0x10000), (ACTOR + 0x1c, 9),
                        ]
                        for index, tgt in enumerate(TARGETS):
                            setup += [(TARGET_OBJECTS + 4 * index, tgt),
                                      (tgt + SCRIPT_INDEX, index), (tgt + FLAGS, 0x10000),
                                      (tgt + 8, 0x110000 + index), (tgt + 0xc, 0x220000),
                                      (tgt + 0x10, 0x30000), (tgt + 0x1c, 5),
                                      (RESULT_AMOUNTS + 16 * index, 40 + index)]
                        cases.append((FLASH_DRIFT, [CONTROLLER, 0x5d2dd8], setup, flash))

    for entry in FLASH_NOTIFY:
        for phase in (0, 1):
            for ticks in (0, 2, 5):
                for flags in (0, 0x20000):
                    cases.append((entry, [OWNER], zero_pool() + [
                        (OWNER + PHASE, phase), (OWNER + 0x144, ticks), (OWNER + FLAGS, flags),
                        (OWNER + 0x160, CONTROLLER), (OWNER + 8, 0x110000),
                        (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000), (OWNER + 0x1c, 5)], 1))

    for facing in range(6):
        cases.append((EXPAND_SPAWNER, [CONTROLLER, OWNER], zero_pool() + [
            (OWNER + 8, 0x110000), (OWNER + 0xc, 0x220000), (OWNER + 0x10, 0x30000),
            (OWNER + 0x1c, 5), (OWNER + 0x110, facing),
            (CONTROLLER + 8, 0x330000), (CONTROLLER + 0xc, 0x440000), (CONTROLLER + 0x10, 0x50000)], 1))

    for phase in (-1, 0, 1):
        for elevation in (0x1880000, 0x1900000, 0):
            cases.append((DESCENDING_TICK, [OWNER],
                          [(OWNER + PHASE, phase & 0xffffffff), (OWNER + 0x10, elevation)], 1))

    for shake in (0, CONTROLLER):
        cases.append((STOP_SHAKE, [], [(ACTIVE_SCREEN_SHAKE, shake)], 1))

    records = bytearray()
    for entry, args, setup, flash in cases:
        flash_result[0] = flash
        pool_cursor[0] = 0
        observed.clear()
        uc.mem_write(DATA_BASE, pristine)
        for at, value in setup:
            uc.mem_write(at, struct.pack('<I', value & 0xffffffff))
        before = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))
        uc.reg_write(UC_X86_REG_ESP, STACK)
        uc.reg_write(UC_X86_REG_EAX, 0)
        uc.mem_write(STACK, struct.pack('<' + 'I' * (1 + len(args)), RETURN_MARKER, *args))
        try:
            uc.emu_start(entry, RETURN_MARKER, count=2000000)
        except Exception:
            print('failed at entry', hex(entry), 'flash', flash, 'setup head', setup[:4])
            raise
        assert uc.reg_read(UC_X86_REG_EIP) == RETURN_MARKER, hex(entry)
        assert uc.reg_read(UC_X86_REG_ESP) == STACK + 4 + 4 * len(args), hex(entry)
        after = bytes(uc.mem_read(DATA_BASE, DATA_BYTES))
        changes = [(DATA_BASE + i, after[i]) for i in range(DATA_BYTES) if before[i] != after[i]] \
            if before != after else []

        records += struct.pack('<4I', entry, flash, len(args), uc.reg_read(UC_X86_REG_EAX))
        records += b''.join(struct.pack('<I', a) for a in args)
        records += struct.pack('<I', len(setup))
        records += b''.join(struct.pack('<II', a, v & 0xffffffff) for a, v in setup)
        records += struct.pack('<I', len(observed))
        for address, words in observed:
            records += struct.pack('<II', address, len(words))
            records += b''.join(struct.pack('<I', w) for w in words)
        records += struct.pack('<I', len(changes))
        records += b''.join(struct.pack('<IB', a, v) for a, v in changes)

    data = b'FSBEFO1\0' + struct.pack('<I', len(cases)) + bytes(records)
    out = Path(output)
    out.write_bytes(data)
    out.with_suffix('.json').write_text(json.dumps({
        'source_sha256': sha,
        'cases': len(cases),
        'entry_points': ['0x467417', '0x4672b5', '0x4674de', hex(TICK_TRAVEL)]
                        + [hex(a) for a in TRAVEL_SPAWNERS]
                        + [hex(TICK_PROJECTILE)] + [hex(a) for a in LAUNCHERS]
                        + [hex(ANIMATED_TICK), hex(STOP_SHAKE)]
                        + [hex(a) for a in SMALL_SPAWNERS] + [hex(a) for a in FINISHING]
                        + [hex(a) for a in IMPACTS] + [hex(TRAIL_SPRITE)]
                        + [hex(a) for a in FANOUT_SEQUENCES] + [hex(a) for a in FANOUT_CHILDREN]
                        + [hex(DESCENDING_TICK)] + [hex(a) for a in PARTICLE_ATTACKS]
                        + [hex(FIRST_TARGET_BURST), hex(EXPAND_SPAWNER), hex(FLASH_DRIFT)]
                        + [hex(a) for a in FLASH_NOTIFY],
        'fixture_sha256': hashlib.sha256(data).hexdigest(),
        'compared_data_bytes': DATA_BYTES,
        'ran_for_real': {'0x498090': 'shared CRT random stream, so the draw count and '
                                     'resulting stream state are compared'},
        'stopped_calls': {hex(a): f'record {n} pushed arguments; RET {4 * n}'
                          for a, n in sorted(STOPPED.items())},
        'substituted_results': {
            '0x45d89c': 'returns successive addresses from a pre-zeroed scratch pool',
            '0x464c59': 'returns the per-case flash result, 0 or 1',
        },
        'scope': 'Field writes, helper call order and arguments, and the returned EAX for '
                 'the ring spawn, the ring child tick and the sweep controller. The helpers '
                 'belong to other scopes and are stopped at entry, so their own behaviour is '
                 'not covered here.',
    }, indent=2) + '\n')
    print(f'effect object cases {len(cases)}')


if __name__ == '__main__':
    main(*sys.argv[1:])
