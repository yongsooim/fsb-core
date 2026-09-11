#!/usr/bin/env python3
"""Execute original palette operations, including partially aliased buffers."""
import hashlib
import json
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG
from prepare_event0 import PE


def main(executable, output):
    pe = PE(executable)
    sha = hashlib.sha256(pe.data).hexdigest()
    assert sha == '710881d024ab1fcf738b342c248631363c9de3a51b32a7e3d61781b791eff454'
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, 0x600000)
    u.mem_map(0x1000000, 65536)
    for _, va, size, offset in pe.sections:
        u.mem_write(pe.base + va, pe.data[offset:offset + size])
    original_data = bytes(u.mem_read(0x4a5000, 3882100))
    base, size = 0x1000400, 2048
    records, count = bytearray(), 0

    def case(entry, dst, src, first, length, delta, initial):
        nonlocal count, records
        args = [base + dst, base + src]
        args += [length] if entry == 0x404cf0 else [first, length] if entry == 0x404ed0 else [delta, length]
        u.mem_write(base, initial)
        u.reg_write(UC_X86_REG_ESP, 0x100f000)
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_FPSW, 0)
        u.reg_write(UC_X86_REG_FPTAG, 0xffff)
        u.mem_write(0x100f000, struct.pack('<' + 'I' * (len(args) + 1), 0x1000000, *args))
        u.emu_start(entry, 0x1000000, count=1000000)
        assert u.reg_read(UC_X86_REG_EIP) == 0x1000000
        assert bytes(u.mem_read(0x4a5000, len(original_data))) == original_data
        result = u.reg_read(UC_X86_REG_EAX)
        if entry == 0x404ed0 and length:
            result -= base  # Pointer return is compared after relocating the buffers.
        records += struct.pack('<7I', entry, dst, src, first, length, delta, result)
        records += initial + bytes(u.mem_read(base, size))
        count += 1

    patterns = [bytes([value]) * size for value in [0, 1, 127, 254, 255]]
    patterns += [bytes((i * 73 + i // 4 * 19) & 255 for i in range(size))]
    for initial in patterns:
        for dst, src in [(0, 0), (0, 512), (512, 0), (1, 0), (0, 1), (4, 0), (0, 4)]:
            for length in [0, 1, 256, 0xffffffff]:
                case(0x404cf0, dst, src, 0, length, 0, initial)
            for first, length in [(0, 0), (0, 1), (0, 256), (3, 16)]:
                case(0x404ed0, dst, src, first, length, 0, initial)
            for delta in [0, 1, 0xffffffff, 255, 0xffffff01, 0x7fffffff, 0x80000000]:
                case(0x404e4c, dst, src, 0, 16, delta, initial)
    data = b'FSBPAL1\0' + struct.pack('<II', count, size) + records
    path = Path(output)
    path.write_bytes(data)
    path.with_suffix('.json').write_text(json.dumps({
        'source_sha256': sha, 'cases': count, 'substituted_calls': [],
        'whole_data_unchanged_per_case': True, 'scratch_bytes_per_case': size,
        'fixture_sha256': hashlib.sha256(data).hexdigest(),
        'coverage': ['grayscale floating-point order and return', 'RGB wrapped signed clamp',
                     'same buffer', 'disjoint buffers', 'byte and word partial overlap',
                     'negative grayscale count', 'empty range', 'forward DWORD copy'],
    }, indent=2) + '\n')
    print('palette cases', count)


if __name__ == '__main__':
    main(*sys.argv[1:])
