#!/usr/bin/env python3
"""Build the SD card for the CHGame game menu.

    python tools/sdcard/mkcard.py                    # build everything, write out/sdcard/
    python tools/sdcard/mkcard.py --no-build         # pack the release builds already there
    python tools/sdcard/mkcard.py --image out/sdcard.img   # also a FAT32 card image
    python tools/sdcard/mkcard.py --only CHFour CHChess

For every program in games.json it builds the release image (each game's
tools/device.py build, or arduino-cli for the utilities), wraps it in a .CHG
package with tools/chgpack.py, and copies the data files the games read from
the card. Copy the CONTENTS of out/sdcard/ to the root of a FAT16/FAT32 card
(for example through utilities/CHSDtoUSB): GAMES/ holds the packages, the
data files stay where the games look for them.

The same inputs give the same bytes, so a card can be rebuilt and compared.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zlib

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import chgpack  # noqa: E402

RELEASE_FQBN = "CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly"


def version_of(d: pathlib.Path) -> str:
    cfg = d / "config.h"
    if cfg.exists():
        m = re.search(r'#define\s+\w+_VERSION\s+"([^"]+)"', cfg.read_text())
        if m:
            return m.group(1)[:7]
    return ""


def build(p: dict) -> pathlib.Path:
    d = REPO / p["dir"]
    name = d.name
    if (d / "tools" / "device.py").exists() and "fqbn" not in p:
        r = subprocess.run([sys.executable, "tools/device.py", "build"], cwd=d, capture_output=True, text=True)
        out = d / "build" / "release"
    else:
        out = d / "build" / "release"
        cmd = ["arduino-cli", "compile", "-b", p.get("fqbn", RELEASE_FQBN), "--build-path", str(out),
               "--library", str(REPO / "platform" / "libraries" / "CHGfx"), str(d)]
        r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:] + r.stderr[-3000:])
        raise SystemExit(f"{name}: build failed")
    return out / f"{name}.ino.bin"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(REPO / "out" / "sdcard"))
    ap.add_argument("--image", help="also write a FAT32 card image here")
    ap.add_argument("--no-build", action="store_true", help="use the existing build/release/*.ino.bin")
    ap.add_argument("--only", nargs="*", help="folder names to include (default: all)")
    a = ap.parse_args()
    progs = json.loads((pathlib.Path(__file__).parent / "games.json").read_text())["programs"]
    if a.only:
        progs = [p for p in progs if pathlib.Path(p["dir"]).name in a.only]
    out = pathlib.Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    (out / "GAMES").mkdir(parents=True)
    files = {}
    print(f"{'program':16s} {'file':13s} {'image':>7s}  crc32     title")
    for p in progs:
        d = REPO / p["dir"]
        binf = (d / "build" / "release" / f"{d.name}.ino.bin") if a.no_build else build(p)
        if not binf.exists():
            raise SystemExit(f"{binf}: missing (build it, or drop --no-build)")
        pkg = chgpack.pack(binf.read_bytes(), p["title"], version=version_of(d))
        info = chgpack.parse(pkg)
        (out / "GAMES" / p["file"]).write_bytes(pkg)
        files["GAMES/" + p["file"]] = pkg
        print(f"{d.name:16s} {p['file']:13s} {info['payload_bytes']:7d}  {info['payload_crc32']:08x}  {p['title']}")
        for rel in p.get("data", []):
            src = d / rel
            for f in ([src] if src.is_file() else sorted(src.rglob("*"))):
                if f.is_file():
                    dst_rel = f.relative_to(src.parent) if src.is_dir() else pathlib.Path(f.name)
                    dst = out / dst_rel
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(f, dst)
                    files[str(dst_rel).replace(os.sep, "/")] = f.read_bytes()
    print(f"\n{len(progs)} packages in {out / 'GAMES'}")
    if a.image:
        sys.path.insert(0, str(REPO / "platform" / "libraries" / "CHSd" / "tools"))
        import fatimg  # noqa: E402
        fatimg.build_image(a.image, files, fs="fat32", label="CHGAME")
        print(f"card image: {a.image}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
