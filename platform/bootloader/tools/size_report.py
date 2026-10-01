#!/usr/bin/env python3
"""Size report for a bootloader build: sections, flash image, RAM, headroom.

    python3 tools/size_report.py build/release/chgame_boot.elf [--objects]

--objects adds a per-object-file breakdown from the linker map next to the
ELF. With LTO the map attributes everything to the LTO partitions, so build
with ./build.sh --nolto for a meaningful per-object split.
"""
import argparse
import collections
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import chgame_map as M  # noqa: E402


def sections(elf, size_tool):
    out = subprocess.run([size_tool, "-A", str(elf)], capture_output=True, text=True, check=True).stdout
    sec = {}
    for line in out.splitlines():
        m = re.match(r"^(\.\S+)\s+(\d+)\s+(\d+)", line)
        if m:
            sec[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return sec


def objects(mapfile):
    text = pathlib.Path(mapfile).read_text()
    text = text[text.index("Linker script and memory map"):]
    cut = text.find("\n.debug")
    if cut > 0:
        text = text[:cut]
    keep = (".text", ".rodata", ".srodata", ".sdata", ".data", ".ramfunc", ".vector", ".init", ".sbss", ".bss")
    flash, ram = collections.Counter(), collections.Counter()
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r"^ (\.[A-Za-z_.$0-9]+)\s*$", line)
        if m and i + 1 < len(lines):
            m2 = re.match(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", lines[i + 1])
            if not m2:
                i += 1
                continue
            name, (addr, size, obj) = m.group(1), m2.groups()
            i += 2
        else:
            m2 = re.match(r"^ (\.[A-Za-z_.$0-9]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", line)
            i += 1
            if not m2:
                continue
            name, addr, size, obj = m2.groups()
        if not name.startswith(keep):
            continue
        a, s = int(addr, 16), int(size, 16)
        if not s:
            continue
        o = obj.split("/")[-1]
        if name.startswith((".bss", ".sbss")):
            ram[o] += s
        else:
            flash[o] += s
            if a >= 0x20000000:
                ram[o] += s
    return flash, ram


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--size-tool", default="riscv-none-embed-size")
    ap.add_argument("--objects", action="store_true")
    ap.add_argument("--margin", type=int, default=256, help="required free bytes in the boot reservation")
    a = ap.parse_args()
    elf = pathlib.Path(a.elf)
    sec = sections(elf, a.size_tool)
    binf = elf.with_suffix(".bin")
    image = binf.stat().st_size if binf.exists() else None
    resv = M.MAP["CHGAME_BOOT_SIZE"]
    ram_total = M.MAP["CHGAME_RAM_SIZE"]

    def get(n):
        return sec.get(n, (0, 0))[0]

    text = get(".init") + get(".vector") + get(".text") + get(".fini") + get(".init_array") + get(".fini_array")
    print(f".text (incl. .init/.vector): {text:6d} B")
    print(f".ramfunc                   : {get('.ramfunc'):6d} B (flash + RAM)")
    print(f".data                      : {get('.data'):6d} B (flash + RAM)")
    print(f".bss                       : {get('.bss'):6d} B (RAM)")
    print(f"stack                      : {get('.stack'):6d} B (RAM)")
    if image is not None:
        free = resv - image
        print(f"boot image                 : {image:6d} B of {resv} ({free} free, {100 * image // resv}% used)")
    ram = 16 + get(".ramfunc") + get(".data") + get(".bss") + get(".stack")
    print(f"RAM in use                 : {ram:6d} B of {ram_total}")
    print(f"application maximum        : {M.MAP['CHGAME_APP_MAX_SIZE']:6d} B")
    if a.objects:
        flash, ramc = objects(elf.with_suffix(".map"))
        print("\nflash by object (LTO builds put everything in the LTO partitions):")
        for k, v in sorted(flash.items(), key=lambda x: -x[1]):
            print(f"  {v:6d}  {k}")
        print("RAM by object:")
        for k, v in sorted(ramc.items(), key=lambda x: -x[1]):
            print(f"  {v:6d}  {k}")
    if image is not None and resv - image < a.margin:
        print(f"GATE A FAILED: only {resv - image} B free, {a.margin} required", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
