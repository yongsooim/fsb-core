#!/usr/bin/env python3
"""Execute4546f4's empty-record branch, without substituting any calls."""
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP


def main(snapshot, output):
    raw = Path(snapshot).read_bytes()
    if raw[:8] != b"FSBDRAW1":
        raise ValueError("invalid snapshot")
    cursor, regions, pages = 12, [], set()
    for _ in range(struct.unpack_from("<I", raw, 8)[0]):
        base, size = struct.unpack_from("<II", raw, cursor)
        cursor += 8
        regions.append((base, raw[cursor:cursor + size]))
        cursor += size
        pages.update(range(base & ~4095, (base + size + 4095) & ~4095, 4096))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    for page in sorted(pages):
        machine.mem_map(page, 4096)
    machine.mem_map(0x1000000, 65536)
    for base, data in regions:
        machine.mem_write(base, data)
    data_base, data = next((base, data) for base, data in regions if base == 0x4a5000)
    command, cases = 0x79b198, []
    def word(address, value):
        machine.mem_write(address, struct.pack("<I", value))
    for top, bottom in [(0, 0), (2, 3), (0x7fffffff, 0xffffffff), (0xffffffff, 0)]:
        machine.mem_write(command, bytes(84))
        word(command + 0x20, top)
        word(command + 0x28, bottom)
        word(0x787480, 1)
        word(0x787498, command)
        before = bytes(machine.mem_read(data_base, len(data)))
        word(0x100f000, 0x1000000)
        machine.reg_write(UC_X86_REG_ESP, 0x100f000)
        machine.emu_start(0x4546f4, 0x1000000, count=10000)
        returned = machine.reg_read(UC_X86_REG_EIP) == 0x1000000
        equal = before == bytes(machine.mem_read(data_base, len(data)))
        cases.append({"source_top": top, "source_bottom": bottom, "returned": returned, "data_equal": equal})
        if not returned or not equal:
            raise RuntimeError("original empty draw changed state or did not return")
    report = {"input_sha256": hashlib.sha256(raw).hexdigest(), "entry": "0x4546f4",
              "code_sha256": hashlib.sha256(machine.mem_read(0x4546f4, 0x2c7)).hexdigest(),
              "substituted_calls": [], "cases": cases}
    Path(output).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main(*sys.argv[1:])
