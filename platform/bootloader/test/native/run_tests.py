#!/usr/bin/env python3
"""Native tests for the bootloader's portable code, on the PC.

    python3 run_tests.py [--quick] [-k NAME]

Builds the host harness (host_hal.c and the SD card and ST7735 models) with
the portable sources from ../../src, makes FAT card images with CHSd's
fatimg.py and packages with tools/chgpack.py, and runs:

  core_nomenu   update path, USB protocol, self-update, boot decision and a
                power cut at every flash operation of a USB upload, built
                without the menu (the HW2a image)
  core_menu     the same with the menu built in
  sd            the SD driver against the card model (SDSC v1/v2, SDHC, no
                card, slow or stuck cards) and the FAT reader against FAT16/
                FAT32 images (MBR and superfloppy, fragmented files and
                folders, corrupt chains, exFAT, blank cards), and package
                header checks
  boot          whole-boot scenarios: menu, installs, every package error,
                power cuts at every flash operation of an SD install, and
                the menu's frames against pinned hashes (frames.json)

Compiler: $CC, else cc. Built with -fsanitize=address,undefined.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
BL = HERE.parent.parent                     # platform/bootloader
REPO = BL.parent.parent
SRC, SHARED = BL / "src", BL / "shared"
BUILD = HERE / "build"
sys.path.insert(0, str(REPO / "platform" / "libraries" / "CHSd" / "tools"))
sys.path.insert(0, str(REPO / "tools"))
import fatimg   # noqa: E402
import chgpack  # noqa: E402
sys.path.insert(0, str(HERE))

HOST = ["host_hal.c", "sd_model.c", "lcd_model.c", "testlib.c"]
CORE = ["boot.c", "bootreq.c", "appmeta.c", "crc32.c", "crc16.c", "update.c", "proto.c"]
MENU = ["sd.c", "fat.c", "chg.c", "install.c", "lcd.c", "menu.c"]


def compiler():
    return os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")


def build(name, main, defs, srcs):
    out = BUILD / name
    cmd = [compiler(), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
           "-DCHBOOT_HOST", "-DF_CPU=48000000", *defs,
           f"-I{HERE}", f"-I{SRC}", f"-I{SHARED}", "-o", str(out),
           str(HERE / main), *[str(HERE / s) for s in HOST], *[str(SRC / s) for s in srcs]]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        raise SystemExit(f"build of {name} failed")
    return out


def run(name, exe, *args):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, env=env)
    out = (r.stdout + r.stderr).strip()
    status = "ok" if r.returncode == 0 else "FAILED"
    print(f"{name:12s} {status:6s} {out.splitlines()[-1] if out else ''}")
    if r.returncode:
        print(out)
    return r.returncode == 0


# ---- consistency of the constants shared with Python --------------------------

def defines(path):
    vals = {}
    for m in re.finditer(r"^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b", path.read_text(), re.M):
        vals[m.group(1)] = int(m.group(2), 0)
    return vals


def check_constants():
    f = defines(SHARED / "chg_format.h")
    m = defines(SRC / "chgame_map.h")
    pairs = [("CHG_MAGIC", chgpack.MAGIC), ("CHG_FORMAT_VERSION", chgpack.FORMAT_VERSION),
             ("CHG_HEADER_BYTES", chgpack.HEADER_BYTES), ("CHG_TARGET_ID", chgpack.TARGET_ID),
             ("CHG_LAYOUT_ID", chgpack.LAYOUT_ID)]
    bad = [n for n, v in pairs if f[n] != v]
    if m["CHGAME_BOOT_SIG"] != chgpack.BOOT_SIG:
        bad.append("CHGAME_BOOT_SIG")
    app_max = 0xF800 - 256 - 0x3000
    if app_max != chgpack.APP_MAX_SIZE:
        bad.append("APP_MAX_SIZE")
    print(f"{'constants':12s} {'ok' if not bad else 'FAILED':6s} chgpack.py vs chg_format.h/chgame_map.h {bad or ''}")
    return not bad


# ---- packages and card images ---------------------------------------------------

def payload(n, seed):
    rnd = random.Random(seed)
    b = bytearray(rnd.getrandbits(8) for _ in range(n))
    struct.pack_into("<III", b, 0, 0x0000006F | 0x100 << 20, 0x3000, 0)   # WCH startup shape
    return bytes(b)


def corrupt_header(pkg, off, value, fix_crc=True):
    h = bytearray(pkg)
    struct.pack_into("<I", h, off, value)
    if fix_crc:
        struct.pack_into("<I", h, 0x1FC, zlib.crc32(bytes(h[:0x1FC])) & 0xFFFFFFFF)
    return bytes(h)


def packages():
    """The package zoo: name -> (bytes, expected chg_check code)."""
    a = chgpack.pack(payload(20000, 1), "ALPHA GAME", "tester", "1.0")
    b = chgpack.pack(payload(4096, 2), "BRAVO")
    z = {
        "ALPHA.CHG": (a, 0),
        "BRAVO.CHG": (b, 0),
        "BADHCRC.CHG": (corrupt_header(a, 0x20, 0x41414141, fix_crc=False), 2),
        "NOTCHG.CHG": (bytes(random.Random(3).getrandbits(8) for _ in range(3000)), 1),
        "WRONGTGT.CHG": (corrupt_header(a, 0x08, 0x12345678), 4),
        "WRONGLAY.CHG": (corrupt_header(a, 0x0C, 0x002000F7), 4),
        "WRONGVER.CHG": (corrupt_header(a, 0x04, 2 | 512 << 16), 3),
        "ZEROLEN.CHG": (corrupt_header(a, 0x10, 0), 5),
        "ODDLEN.CHG": (corrupt_header(a, 0x10, 19999), 5),
        "TOOBIG.CHG": (corrupt_header(a, 0x10, 50948), 5),
        "TRUNC.CHG": (a[:512 + 10000], 5),
    }
    return z


def build_images(quick):
    BUILD.mkdir(exist_ok=True)
    pk = packages()
    files = {"GAMES/" + n: d for n, (d, _) in pk.items()}
    files["GAMES/README.TXT"] = b"not a package\n"
    files["WORDS.DIC"] = bytes(5000)
    imgs = {}
    lay = {}
    lay["fat32"] = fatimg.build_image(str(BUILD / "fat32.img"), files, fs="fat32", lfn=True, decoys=True,
                                      fragment={"GAMES/ALPHA.CHG": 6}, dir_pieces={"GAMES": 3},
                                      padding={"GAMES": 40})
    imgs["fat32"] = BUILD / "fat32.img"
    lay["fat16"] = fatimg.build_image(str(BUILD / "fat16.img"), files, fs="fat16", mbr=False,
                                      fragment={"GAMES/ALPHA.CHG": 5}, spc=8)
    imgs["fat16"] = BUILD / "fat16.img"
    lay["fat16p3"] = fatimg.build_image(str(BUILD / "fat16p3.img"), files, fs="fat16", part_slot=3)
    imgs["fat16p3"] = BUILD / "fat16p3.img"
    fatimg.build_image(str(BUILD / "nogames.img"), {"WORDS.DIC": bytes(100), "GAME/X.CHG": pk["BRAVO.CHG"][0]},
                       fs="fat16")
    imgs["nogames"] = BUILD / "nogames.img"
    # corrupt chains in copies of the FAT16 image: ALPHA's second cluster link
    a_cl = lay["fat16"].objects["GAMES/ALPHA.CHG"].clusters
    for tag, val in (("free", 0x0000), ("bad", 0xFFF7), ("short", 0xFFFF), ("range", 0xFF00)):
        p = BUILD / f"chain_{tag}.img"
        shutil.copy(imgs["fat16"], p)
        fatimg.set_fat_entry(str(p), lay["fat16"], a_cl[1], val)
        imgs["chain_" + tag] = p
    fatimg.make_exfat_stub(str(BUILD / "exfat.img"))
    imgs["exfat"] = BUILD / "exfat.img"
    (BUILD / "blank.img").write_bytes(bytes(1 << 20))
    imgs["blank"] = BUILD / "blank.img"
    return imgs, lay, pk


def sd_spec(imgs, lay, pk, quick):
    s = []

    def case(name, card, img, init="OK", sets=(), body=()):
        s.append(f"case {name}")
        s.append(f"card {card} {img}" if img else f"card {card}")
        s.extend(f"set {k} {v}" for k, v in sets)
        s.append(f"init {init}")
        s.extend(body)
        s.append("end")

    def full(img_key):
        body = ["mount 0", f"games {len(pk)}"]
        for n in ("ALPHA.CHG", "BRAVO.CHG"):
            d = pk[n][0]
            body.append(f"file {n} {len(d)} {zlib.crc32(d) & 0xFFFFFFFF:x}")
        body += [f"chg {n} {code}" for n, (_, code) in pk.items()]
        return body

    for card in ("sdhc", "sdsc2", "sdsc1"):
        case(f"fat32_{card}", card, imgs["fat32"], body=full("fat32"))
    case("fat16_superfloppy", "sdhc", imgs["fat16"], body=full("fat16"))
    case("fat16_mbr_slot3", "sdsc2", imgs["fat16p3"], body=full("fat16p3"))
    case("no_games_folder", "sdhc", imgs["nogames"], body=["mount 0", "games MISSING"])
    for tag in ("free", "bad", "short", "range"):
        case(f"chain_{tag}", "sdhc", imgs["chain_" + tag], body=["mount 0", "filebad ALPHA.CHG",
             f"file BRAVO.CHG {len(pk['BRAVO.CHG'][0])} {zlib.crc32(pk['BRAVO.CHG'][0]) & 0xFFFFFFFF:x}"])
    case("exfat", "sdhc", imgs["exfat"], body=["mount -3"])
    case("blank", "sdhc", imgs["blank"], body=["mount -2"])
    case("no_card", "none", None, init="FAIL 60")
    case("slow_init_ok", "sdhc", imgs["fat16"], sets=[("acmd41", 400)], body=["mount 0"])
    case("slow_init_timeout", "sdhc", imgs["fat16"], init="FAIL", sets=[("acmd41", 5000)])
    # a block never read since power-up can take a slow card ~0.8 s (CHSd notes);
    # 2 s is past the 1.5 s token limit.
    case("slow_reads", "sdhc", imgs["fat16"], sets=[("latency", 800000)], body=["mount 0", "games " + str(len(pk))])
    case("dead_slow_reads", "sdhc", imgs["fat16"], sets=[("latency", 2000000)], body=["mount -1"])
    a_lba = lay["fat16"].cluster_lba(lay["fat16"].objects["GAMES/ALPHA.CHG"].clusters[2])
    case("read_timeout", "sdhc", imgs["fat16"], sets=[("timeout_lba", a_lba)], body=["mount 0", "filebad ALPHA.CHG"])
    case("read_error", "sdhc", imgs["fat16"], sets=[("fail_lba", a_lba)], body=["mount 0", "filebad ALPHA.CHG"])
    case("stuck_multiread", "sdhc", imgs["fat16"], sets=[("state", "multiread")], body=["mount 0", "games " + str(len(pk))])
    case("stuck_writewait", "sdhc", imgs["fat16"], sets=[("state", "writewait")], body=["mount 0", "games " + str(len(pk))])
    path = BUILD / "sd_cases.txt"
    path.write_text("\n".join(s) + "\n")
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("-k", default="", help="run only suites whose name contains this")
    ap.add_argument("--pin-frames", action="store_true", help="record the current menu frames as the reference")
    a = ap.parse_args()
    BUILD.mkdir(exist_ok=True)
    ok = check_constants()
    menu_built = (SRC / "menu.c").exists()
    suites = [("core_nomenu", "test_core.c", ["-DCHBOOT_MENU=0", "-DCHGAME_ALLOW_SELFUPDATE=1"], CORE)]
    if menu_built:
        suites.append(("core_menu", "test_core.c", ["-DCHBOOT_MENU=1", "-DCHGAME_ALLOW_SELFUPDATE=1"], CORE + MENU))
        suites.append(("core_locked", "test_core.c", ["-DCHBOOT_MENU=1", "-DCHGAME_ALLOW_SELFUPDATE=0"], CORE + MENU))
    sd_srcs = CORE + MENU if menu_built else CORE + ["sd.c", "fat.c", "chg.c"]
    sd_defs = ["-DCHBOOT_MENU=" + ("1" if menu_built else "0")]
    for name, main_c, defs, srcs in suites:
        if a.k in name:
            ok &= run(name, build(name, main_c, defs, srcs))
    if a.k in "sd":
        imgs, lay, pk = build_images(a.quick)
        spec = sd_spec(imgs, lay, pk, a.quick)
        ok &= run("sd", build("sd", "test_sd.c", sd_defs, sd_srcs), spec)
    if menu_built and a.k in "app" and (BUILD / "boot_fat32.img").exists():
        app_srcs = ["boot.c", "bootreq.c", "appmeta.c", "crc32.c", "crc16.c", "sd.c", "fat.c", "chg.c", "install.c", "lcd.c", "menu.c"]
        exe = build("app", "test_app.c", ["-DCHBOOT_MENU=1", "-DCHBOOT_APP=1"], app_srcs)
        ok &= run("app", exe, BUILD / "boot_fat32.img")
    if menu_built and a.k in "boot":
        if a.k not in "sd":
            imgs, lay, pk = build_images(a.quick)
        import boot_cases  # noqa: E402
        ok &= boot_cases.run_all(BUILD, build, run, imgs, lay, pk, a.quick, a.pin_frames)
    print("ALL PASSED" if ok else "SOME TESTS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
