#!/usr/bin/env python3
"""Read the whole boot region (0x0000-0x2FFF) back over USB and compare it
with a bootloader image: the invariant of spec section 39 ("malformed input
never modifies the boot region"), checked after the hardware tests.

    python platform/bootloader/tools/bootcheck.py platform/bootloader/release/chgame_sdboot.bin [--port COMx]

The board must be in the bootloader: at the game menu, or in USB mode. Exit
status 0 when the region matches the image (and is 0xFF after it), 1 if not.
"""
import argparse
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "host" / "py"))
from chgame.client import Client, find_ports  # noqa: E402
from chgame.protocol import CMD_READ  # noqa: E402

BOOT_SIZE = 0x3000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--port")
    a = ap.parse_args()
    want = pathlib.Path(a.image).read_bytes()
    want = want + b"\xff" * (BOOT_SIZE - len(want))
    port = a.port or (find_ports() or [None])[0]
    if not port:
        print("no CHGame port found", file=sys.stderr)
        return 2
    got = bytearray()
    with Client(port, timeout=2.0) as c:
        mp = c.hello().max_payload
        step = mp - 1
        while len(got) < BOOT_SIZE:
            n = min(step, BOOT_SIZE - len(got))
            r = c.check(CMD_READ, struct.pack("<IH", len(got), n))
            got += r[1:1 + n]
    bad = [i for i in range(BOOT_SIZE) if got[i] != want[i]]
    if bad:
        print(f"boot region differs at {len(bad)} bytes, first at 0x{bad[0]:04X}")
        return 1
    print(f"boot region 0x0000-0x2FFF matches {a.image} byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
