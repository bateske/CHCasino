"""Whole-boot scenarios (test_boot.c): builds the package zoo and a FAT32
card, writes the manifest in the menu's display order, runs the scenarios,
converts the panel dumps to PNG and checks them against frames.json.

frames.json pins the SHA-256 of every frame. A change that alters a frame
fails until the new hashes are recorded (python3 run_tests.py --pin-frames),
CLAUDE.md rule 2: the pictures are the test."""
import hashlib
import json
import pathlib
import struct
import zlib

import chgpack
import fatimg

HERE = pathlib.Path(__file__).resolve().parent
TITLE_COLS = 19


def display_title(name, data):
    """What the menu shows: the header title, or the 8.3 name for a bad header."""
    try:
        chgpack.parse(data, check_payload=False)
        t = data[0x20:0x40].split(b"\0", 1)[0].decode("ascii")[:TITLE_COLS]
    except chgpack.ChgError:
        t = name.split(".")[0][:8]
    return t.ljust(TITLE_COLS)


def zoo(pk):
    z = {n: d for n, (d, _) in pk.items()}
    alpha = z["ALPHA.CHG"]
    # a bootloader image disguised as a game: valid header and CRCs, signature at payload offset 8
    p = bytearray(alpha[512:])
    struct.pack_into("<I", p, 8, chgpack.BOOT_SIG)
    h = bytearray(alpha[:512])
    h[0x20:0x40] = b"BOOT IMAGE".ljust(32, b"\0")
    struct.pack_into("<I", h, 0x14, zlib.crc32(bytes(p)) & 0xFFFFFFFF)
    struct.pack_into("<I", h, 0x1FC, zlib.crc32(bytes(h[:0x1FC])) & 0xFFFFFFFF)
    z["BOOTIMG.CHG"] = bytes(h) + bytes(p)
    # header fine, payload damaged
    h = bytearray(alpha[:512])
    h[0x20:0x40] = b"BAD PAYLOAD".ljust(32, b"\0")
    struct.pack_into("<I", h, 0x1FC, zlib.crc32(bytes(h[:0x1FC])) & 0xFFFFFFFF)
    p = bytearray(alpha[512:])
    p[5000] ^= 0x40
    z["BADPCRC.CHG"] = bytes(h) + bytes(p)
    return z


def run_all(build_dir, build, run, imgs, lay, pk, quick, pin=False):
    z = zoo(pk)
    pkdir = build_dir / "pk"
    pkdir.mkdir(exist_ok=True)
    files = {}
    for n, d in z.items():
        (pkdir / n).write_bytes(d)
        files["GAMES/" + n] = d
    files["WORDS.DIC"] = bytes(5000)
    card = build_dir / "boot_fat32.img"
    fatimg.build_image(str(card), files, fs="fat32", lfn=True, decoys=True,
                       fragment={"GAMES/ALPHA.CHG": 7, "GAMES/BRAVO.CHG": 3}, dir_pieces={"GAMES": 2})
    order = sorted(z, key=lambda n: display_title(n, z[n]))
    man = [f"img fat32 {card}", f"img fat16 {imgs['fat16']}", f"img nogames {imgs['nogames']}"]
    for n in order:
        d = z[n]
        try:
            info = chgpack.parse(d, check_payload=False)
            ln, crc = info["payload_bytes"], info["payload_crc32"]
        except chgpack.ChgError:
            ln, crc = 0, 0
        man.append(f"pkg {n} 0 {ln} {crc:x} {pkdir / n}")
    mpath = build_dir / "boot_manifest.txt"
    mpath.write_text("\n".join(man) + "\n")
    fdir = build_dir / "frames"
    fdir.mkdir(exist_ok=True)
    for old in fdir.glob("*"):
        old.unlink()
    from run_tests import CORE, MENU   # noqa: E402
    exe = build("boot", "test_boot.c", ["-DCHBOOT_MENU=1", "-DCHGAME_ALLOW_SELFUPDATE=1"], CORE + MENU)
    ok = run("boot", exe, mpath, fdir)
    for theme in ("plain", "casino"):           # the alternative colour themes still build and draw
        texe = build("boot_" + theme, "test_boot.c", ["-DCHBOOT_MENU=1", "-DCHGAME_ALLOW_SELFUPDATE=1",
                                                      "-DMENU_THEME=MENU_THEME_" + theme.upper()], CORE + MENU)
        ok &= run("boot_" + theme, texe, mpath, fdir, "themes", theme + "_")
    real = HERE.parents[3] / "out" / "sdcard.img"
    if real.exists():
        ok &= run_real(build_dir, exe, real, fdir)
    else:
        print(f"{'boot_real':12s} {'skip':6s} no out/sdcard.img (python tools/sdcard/mkcard.py --image out/sdcard.img)")
    ok &= check_frames(fdir, pin)
    return ok


def run_real(build_dir, exe, img, fdir):
    from run_tests import run   # noqa: E402
    vol = fatimg.FatVolume(str(img))
    g, _, _ = vol.find("GAMES", is_dir=True)
    pkgs = {}
    for raw, attr, _, _ in vol.listdir(g):
        if attr & 0x10 or raw[8:11] != b"CHG":
            continue
        name = raw[:8].decode().rstrip() + ".CHG"
        pkgs[name] = vol.read_file("GAMES/" + name)
    pkdir = build_dir / "realpk"
    pkdir.mkdir(exist_ok=True)
    man = [f"img fat32 {img}"]
    for n in sorted(pkgs, key=lambda n: display_title(n, pkgs[n])):
        (pkdir / n).write_bytes(pkgs[n])
        info = chgpack.parse(pkgs[n])
        man.append(f"pkg {n} 0 {info['payload_bytes']} {info['payload_crc32']:x} {pkdir / n}")
    mpath = build_dir / "real_manifest.txt"
    mpath.write_text("\n".join(man) + "\n")
    return run("boot_real", exe, mpath, fdir, "real")


def check_frames(fdir, pin):
    from PIL import Image
    pinned_path = HERE / "frames.json"
    pinned = json.loads(pinned_path.read_text()) if pinned_path.exists() else {}
    now = {}
    for ppm in sorted(fdir.glob("*.ppm")):
        im = Image.open(ppm).convert("RGB")
        im.save(ppm.with_suffix(".png"))
        now[ppm.stem] = hashlib.sha256(im.tobytes()).hexdigest()
    if pin or not pinned:
        if not pin:
            pinned.clear()
        pinned.update(now)
        pinned_path.write_text(json.dumps(pinned, indent=1, sort_keys=True) + "\n")
        print(f"{'frames':12s} {'pinned':6s} {len(now)} frames recorded in frames.json")
        return True
    # real_* frames come from out/sdcard.img, which only exists after mkcard.py
    keys = set(now) | {k for k in pinned if not k.startswith("real_")}
    diff = sorted(k for k in keys if now.get(k) != pinned.get(k))
    print(f"{'frames':12s} {'ok' if not diff else 'FAILED':6s} {len(now)} frames vs frames.json" + (f": changed {diff}" if diff else ""))
    return not diff
