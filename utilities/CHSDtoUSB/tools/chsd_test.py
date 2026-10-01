"""Hardware test for CHSDtoUSB, run from Windows against the attached CHGame.

    python tools/chsd_test.py                  all tests
    python tools/chsd_test.py scsi raw files   just these
    python tools/chsd_test.py --list

Most tests need the test build, which adds serial commands that stand in
for the buttons and inject faults (see CHSDtoUSB.ino, CHSD_TEST):

    arduino-cli compile -b CHGame:ch32v:CHGame --build-path build/test \
        --build-property build.extra_flags=-DCHSD_TEST=1 .

With the release build (compiled into build/release) the tests that need
those commands say so and skip. The "upload" tests re-upload whichever of
the two is running, from build/test or build/release.

What it touches on the card:
  * files, only inside \\CHSD_TEST on the drive (deleted at the end);
  * raw blocks, only in the unpartitioned gap between the MBR and the first
    partition (saved first, put back at the end) - skipped if the card has
    no such gap; and block 0, rewritten with its own contents.
No administrator rights are needed.
"""
import argparse
import ctypes
import hashlib
import os
import random
import re
import shutil
import subprocess
import sys
import threading
import time
from ctypes import wintypes as W
from pathlib import Path

import serial
import serial.tools.list_ports

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scsi  # noqa: E402

SKETCH = Path(__file__).resolve().parent.parent
TEST_BUILD = SKETCH / "build" / "test"
RELEASE_BUILD = SKETCH / "build" / "release"
RNG = random.Random(0xC5D)

# ---------------------------------------------------------------------------
# Device access
# ---------------------------------------------------------------------------


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x16C0 and p.pid == 0x27DD and (p.serial_number or "").startswith("CGM"):
            return p.device
    return None


def find_drive():
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    for i in range(26):
        if not mask & (1 << i):
            continue
        letter = chr(65 + i)
        if ctypes.windll.kernel32.GetDriveTypeW("%s:\\" % letter) != 2:   # DRIVE_REMOVABLE
            continue
        try:
            vendor, product, _ = scsi.device_ids(letter)
        except OSError:
            continue
        if vendor == "CHGame" and product.startswith("SD Card Reader"):
            return letter
    return None


class Device:
    def __init__(self):
        self.port = None
        self.ser = None
        self.letter = None

    def connect(self, timeout=20):
        """(Re)find the serial port and drive letter, e.g. after a reboot."""
        self.close()
        t0 = time.time()
        while time.time() - t0 < timeout:
            self.port = find_port()
            if self.port:
                try:
                    self.ser = serial.Serial(self.port, 115200, timeout=1)
                    break
                except serial.SerialException:
                    pass
            time.sleep(0.5)
        if not self.ser:
            raise RuntimeError("no CHSDtoUSB serial port (is the sketch running?)")
        while time.time() - t0 < timeout:
            self.letter = find_drive()
            if self.letter and volume_ready(self.letter):
                return
            time.sleep(0.5)
        raise RuntimeError("the CHSDtoUSB drive did not appear")

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except serial.SerialException:
                pass
        self.ser = None

    def send(self, c):
        """Send one command byte, return the status line as a dict."""
        self.ser.reset_input_buffer()
        self.ser.write(c.encode())
        line = self.ser.readline().decode("ascii", "replace").strip()
        return parse_status(line)

    def status(self):
        return self.send("?")

    def root(self):
        return Path("%s:\\" % self.letter)


def parse_status(line):
    m = re.match(r"R (\d+) W (\d+) RETRY (\d+) FAIL (\d+) WRETRY (\d+) WFAIL (\d+) CARD (\d+) (RO|RW) (\w+)( TEST)?$",
                 line)
    if not m:
        raise RuntimeError("bad status line: %r" % line)
    g = m.groups()
    return dict(read=int(g[0]), written=int(g[1]), retry=int(g[2]), fail=int(g[3]), wretry=int(g[4]),
                wfail=int(g[5]), card=int(g[6]), ro=g[7] == "RO", state=g[8], test=bool(g[9]), line=line)


def volume_ready(letter):
    buf = ctypes.create_unicode_buffer(64)
    return bool(ctypes.windll.kernel32.GetVolumeInformationW("%s:\\" % letter, buf, 64, None, None, None,
                                                             None, 0))


def wait_for(cond, timeout, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return True
        time.sleep(step)
    return False


# Unbuffered file reads, so what is compared came from the card, not the cache.
_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.CreateFileW.restype = W.HANDLE
_k.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE]
_k.ReadFile.argtypes = [W.HANDLE, ctypes.c_void_p, W.DWORD, ctypes.POINTER(W.DWORD), W.LPVOID]
_k.VirtualAlloc.restype = ctypes.c_void_p
_k.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, W.DWORD, W.DWORD]
_k.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, W.DWORD]


def read_uncached(path, chunk=1 << 20):
    h = _k.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x20000000 | 0x08000000, None)
    if h in (None, scsi.INVALID):
        raise ctypes.WinError(ctypes.get_last_error())
    buf = _k.VirtualAlloc(None, chunk, 0x3000, 0x04)
    out = bytearray()
    try:
        n = W.DWORD()
        while True:
            if not _k.ReadFile(h, buf, chunk, ctypes.byref(n), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if not n.value:
                break
            out += ctypes.string_at(buf, n.value)
    finally:
        _k.VirtualFree(buf, 0, 0x8000)
        _k.CloseHandle(h)
    return bytes(out)


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def sha(b):
    return hashlib.sha256(b).hexdigest()


def rand_bytes(n):
    return RNG.randbytes(n)


# ---------------------------------------------------------------------------
# Test bookkeeping
# ---------------------------------------------------------------------------
class Checks:
    def __init__(self):
        self.failed = []
        self.passed = 0

    def check(self, cond, what):
        if cond:
            self.passed += 1
            print("    ok   " + what)
        else:
            self.failed.append(what)
            print("    FAIL " + what)
        return cond


C = Checks()
check = C.check


def with_volume(dev, fn):
    h = scsi.open_volume(dev.letter)
    try:
        return fn(h)
    finally:
        scsi.close(h)


def expect_sense(r, key, asc, ascq=0):
    return r.status == 2 and r.key == (key, asc, ascq)


def settle(h):
    """Clear a pending UNIT ATTENTION and make sure the drive answers."""
    for _ in range(3):
        if scsi.command(h, [0, 0, 0, 0, 0, 0]).ok:
            return True
    return False


# ---------------------------------------------------------------------------
# Raw scratch area: the gap between the MBR and the first partition
# ---------------------------------------------------------------------------
class Scratch:
    def __init__(self, h):
        mbr = scsi.read10(h, 0, 1).data
        self.mbr = mbr
        self.lba = self.count = 0
        self.saved = None
        if mbr[510:512] != b"\x55\xAA":
            return
        starts = []
        for i in range(4):
            e = mbr[446 + 16 * i:462 + 16 * i]
            if e[4]:
                starts.append(int.from_bytes(e[8:12], "little"))
        if not starts or any(e == 0xEE for e in mbr[450:512:16]):    # GPT: no gap to use
            return
        first = min(starts)
        if first >= 2048 + 256:
            self.lba = 2048
            self.count = min(first - 2048, 2048)

    def save(self, h):
        self.saved = read_blocks(h, self.lba, self.count)

    def restore(self, h):
        if self.saved is not None:
            write_blocks(h, self.lba, self.saved)
            ok = read_blocks(h, self.lba, self.count) == self.saved
            check(ok, "scratch area restored")


def read_blocks(h, lba, count, per=128):
    out = bytearray()
    while count:
        n = min(per, count)
        r = scsi.read10(h, lba, n)
        if not r.ok:
            raise RuntimeError("READ %d+%d failed: %r" % (lba, n, r))
        out += r.data
        lba += n
        count -= n
    return bytes(out)


def write_blocks(h, lba, data, per=128):
    for off in range(0, len(data), per * 512):
        part = data[off:off + per * 512]
        r = scsi.write10(h, lba + off // 512, part)
        if not r.ok:
            raise RuntimeError("WRITE %d+%d failed: %r" % (lba + off // 512, len(part) // 512, r))


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
def t_identify(dev):
    st = dev.status()
    print("    " + st["line"])
    check(st["state"] == "CONNECTED", "state CONNECTED")
    check(st["card"] > 0, "card present")

    def body(h):
        r = scsi.command(h, [0x12, 0, 0, 0, 36, 0], data_in=36)
        check(r.ok and r.data[8:36] == b"CHGame  SD Card Reader  1.00", "INQUIRY strings")
        check(r.ok and r.data[1] & 0x80, "INQUIRY says removable")
        cap = scsi.read_capacity(h)
        check(cap == (st["card"], 512), "READ CAPACITY %s matches the card (%d blocks)" % (cap, st["card"]))
    with_volume(dev, body)


def t_scsi(dev):
    def body(h):
        settle(h)
        blocks = scsi.read_capacity(h)[0]
        mbr = scsi.read10(h, 0, 1).data

        def alive(what):
            r = scsi.read10(h, 0, 1)
            check(r.ok and r.data == mbr, "still answers after " + what)

        r = scsi.command(h, [0, 0, 0, 0, 0, 0])
        check(r.ok, "TEST UNIT READY")
        r = scsi.command(h, [0x12, 0, 0, 0, 5, 0], data_in=5)
        check(r.ok and r.returned == 5, "INQUIRY, allocation length 5 -> 5 bytes")
        r = scsi.command(h, [0x12, 1, 0x80, 0, 64, 0], data_in=64)
        check(expect_sense(r, 5, 0x24), "INQUIRY VPD page -> ILLEGAL REQUEST / invalid field")
        r = scsi.command(h, [0x03, 0, 0, 0, 18, 0], data_in=18)
        check(r.ok and r.data[2] == 0, "REQUEST SENSE after an error reports it only once (now NO SENSE)")
        r = scsi.command(h, [0x1A, 0, 0x3F, 0, 192, 0], data_in=192)
        check(r.ok and r.returned == 4 and not r.data[2] & 0x80, "MODE SENSE (6): 4-byte header, not write-protected")
        r = scsi.command(h, [0x5A, 0, 0x3F, 0, 0, 0, 0, 0, 192, 0], data_in=192)
        check(r.ok and r.returned == 8 and not r.data[3] & 0x80, "MODE SENSE (10): 8-byte header, not write-protected")
        r = scsi.command(h, [0x23, 0, 0, 0, 0, 0, 0, 0, 252, 0], data_in=252)
        check(r.ok and r.returned == 12 and int.from_bytes(r.data[4:8], "big") == blocks,
              "READ FORMAT CAPACITIES")
        r = scsi.command(h, [0x2F, 0, 0, 0, 0, 0, 0, 0, 8, 0])
        check(r.ok, "VERIFY (10)")
        r = scsi.command(h, [0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        check(r.ok, "SYNCHRONIZE CACHE (10)")
        r = scsi.command(h, [0x1E, 0, 0, 0, 1, 0])
        r2 = scsi.command(h, [0x1E, 0, 0, 0, 0, 0])
        check(r.ok and r2.ok, "PREVENT / ALLOW MEDIUM REMOVAL")
        r = scsi.command(h, [0xE7, 0, 0, 0, 0, 0])
        check(expect_sense(r, 5, 0x20), "unknown opcode -> ILLEGAL REQUEST / invalid command")
        r = scsi.command(h, [0xE7, 0, 0, 0, 0, 0], data_in=100)
        check(expect_sense(r, 5, 0x20), "unknown opcode expecting 100 bytes in -> refused, empty data phase")
        alive("an unknown opcode with a data-in phase")
        r = scsi.command(h, [0xE7, 0, 0, 0, 0, 0], data_out=bytes(600))
        check(expect_sense(r, 5, 0x20), "unknown opcode sending 600 bytes -> data swallowed, refused")
        alive("an unknown opcode with a data-out phase")
        r = scsi.read10(h, blocks - 1, 1)
        check(r.ok, "READ of the last block")
        r = scsi.read10(h, blocks - 1, 2)
        check(expect_sense(r, 5, 0x21), "READ past the end -> LBA out of range")
        r = scsi.command(h, [0x28, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 2, 0], data_in=1024)
        check(expect_sense(r, 5, 0x21), "READ at LBA 0xFFFFFFFF, 2 blocks (wraps a 32-bit sum) -> out of range")
        r = scsi.command(h, [0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        check(r.ok, "READ of 0 blocks")
        r = scsi.read10(h, 0, 1, xfer_len=1024)
        check(expect_sense(r, 5, 0x24), "READ 1 block with a 1024-byte data phase -> refused")
        alive("a READ whose data phase was too long")
        r = scsi.command(h, [0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0], data_out=bytes(512))
        check(expect_sense(r, 5, 0x24), "READ with an OUT data phase -> data swallowed, refused")
        alive("a READ in the wrong direction")
        r = scsi.command(h, [0x2A, 0, 0, 0, 0, 0, 0, 0, 1, 0], data_in=512)
        check(expect_sense(r, 5, 0x24), "WRITE with an IN data phase -> refused, nothing written")
        alive("a WRITE in the wrong direction")
        r = scsi.command(h, [0x12, 0, 0, 0, 36, 0], data_out=bytes(36))
        check(expect_sense(r, 5, 0x24), "INQUIRY with an OUT data phase -> refused")
        alive("an INQUIRY in the wrong direction")
    with_volume(dev, body)


def t_raw(dev):
    def body(h):
        settle(h)
        sc = Scratch(h)
        if not sc.count:
            print("    (no unpartitioned gap before the first partition: raw write tests skipped)")
            return
        print("    scratch area: LBA %d..%d" % (sc.lba, sc.lba + sc.count - 1))
        sc.save(h)
        try:
            n = sc.count
            pat = rand_bytes(n * 512)
            t0 = time.perf_counter()
            write_blocks(h, sc.lba, pat)
            dt = time.perf_counter() - t0
            print("    raw write %d KB in 128-block commands: %.0f KB/s" % (n // 2, n / 2 / dt))
            check(read_blocks(h, sc.lba, n) == pat, "raw %d-block write reads back identical" % n)
            # Odd sizes and alignments, single and multi-block
            for lba_off, cnt in ((0, 1), (1, 1), (3, 7), (17, 64), (100, 127), (300, 128), (511, 100)):
                if lba_off + cnt > n:
                    continue
                d = rand_bytes(cnt * 512)
                write_blocks(h, sc.lba + lba_off, d, per=cnt)
                pat = pat[:lba_off * 512] + d + pat[(lba_off + cnt) * 512:]
            check(read_blocks(h, sc.lba, n, per=37) == pat, "odd-sized writes land where they should")
            # Many tiny commands back to back
            for i in range(64):
                d = rand_bytes(512)
                write_blocks(h, sc.lba + 600 + i, d)
                pat = pat[:(600 + i) * 512] + d + pat[(601 + i) * 512:]
            check(read_blocks(h, sc.lba, n, per=1)[600 * 512:664 * 512] == pat[600 * 512:664 * 512],
                  "64 single-block writes, read back one block at a time")
        finally:
            sc.restore(h)
        # Block 0 must be writable (partitioning, formatting). Rewrite it as is.
        before = scsi.read10(h, 0, 1).data
        r = scsi.write10(h, 0, before)
        check(r.ok and scsi.read10(h, 0, 1).data == before, "block 0 (MBR) can be written")
    with_volume(dev, body)


def t_faults(dev):
    if not dev.status()["test"]:
        print("    (needs the test build)")
        return

    def body(h):
        settle(h)
        sc = Scratch(h)
        if not sc.count:
            print("    (no scratch area: skipped)")
            return
        n = min(sc.count, 1024)
        sc.save(h)
        try:
            pat = rand_bytes(n * 512)
            write_blocks(h, sc.lba, pat)

            s0 = dev.send("r")                            # soft read faults on
            ok = read_blocks(h, sc.lba, n) == pat
            s1 = dev.send("r")                            # off
            check(ok, "reads with 1 block in 50 garbled on the wire come back correct")
            check(s1["retry"] - s0["retry"] >= n // 50 - 1 and s1["fail"] == s0["fail"],
                  "... by retrying (%d retries, %d failures)" % (s1["retry"] - s0["retry"], s1["fail"] - s0["fail"]))

            pat = rand_bytes(n * 512)
            s0 = dev.send("w")                            # soft write faults on
            write_blocks(h, sc.lba, pat)
            s1 = dev.send("w")
            check(read_blocks(h, sc.lba, n) == pat, "writes with 1 block in 50 sent with a bad CRC land correctly")
            check(s1["wretry"] - s0["wretry"] >= n // 50 - 1 and s1["wfail"] == s0["wfail"],
                  "... because the card rejected them and they were resent (%d retries)" % (s1["wretry"] - s0["wretry"]))

            # Control: with the card's CRC check off, the same bad CRCs go unnoticed.
            dev.send("n")
            s0 = dev.send("w")
            write_blocks(h, sc.lba, pat[:256 * 512])
            s1 = dev.send("w")
            dev.send("c")
            check(s1["wretry"] == s0["wretry"], "control: with CRC checking off in the card nothing is rejected")

            s0 = dev.send("R")                            # the next block fails every try
            r = scsi.read10(h, sc.lba, 16)
            s1 = dev.status()
            check(expect_sense(r, 3, 0x11), "an unreadable first block -> MEDIUM ERROR / unrecovered read error")
            check(s1["fail"] == s0["fail"] + 1 and s1["retry"] == s0["retry"] + 3,
                  "... reported after 4 tries")
            check(scsi.read10(h, sc.lba, 16).data == pat[:16 * 512], "... and the next read works")
            dev.send("M")                                 # the 21st block of the next read fails
            r = scsi.read10(h, sc.lba, 64)
            print("    (mid-run read failure: %d bytes reached the PC)" % r.returned)
            check(expect_sense(r, 3, 0x11), "a block failing 20 blocks into a READ -> MEDIUM ERROR")
            check(read_blocks(h, sc.lba, n) == pat, "... and the stream is closed cleanly: reads work after")

            fresh = rand_bytes(64 * 512)
            s0 = dev.send("W")                            # the next block written fails every try
            r = scsi.write10(h, sc.lba, fresh[:16 * 512])
            s1 = dev.status()
            check(expect_sense(r, 3, 0x0C), "an unwritable first block -> MEDIUM ERROR / write error")
            check(s1["wfail"] == s0["wfail"] + 1 and s1["wretry"] == s0["wretry"] + 3,
                  "... reported after 4 tries")
            dev.send("V")                                 # the 21st block of the next write fails
            r = scsi.write10(h, sc.lba, fresh)
            check(expect_sense(r, 3, 0x0C), "a block failing 20 blocks into a WRITE -> MEDIUM ERROR")
            check(read_blocks(h, sc.lba, 20) == fresh[:20 * 512],
                  "... the 20 blocks before it were written")
            r = scsi.write10(h, sc.lba, fresh)
            check(r.ok and read_blocks(h, sc.lba, 64) == fresh, "... and writing it all again works")
        finally:
            sc.restore(h)
    with_volume(dev, body)


FILE_SIZES = [0, 1, 511, 512, 513, 4095, 4096, 65535, 65536, 65537, 1 << 20, (3 << 20) + 7]


def make_tree(root):
    """Files of awkward sizes plus a nested tree of small ones; returns {path: sha}."""
    want = {}
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()
    for n in FILE_SIZES:
        p = root / ("size_%d.bin" % n)
        d = rand_bytes(n)
        write_file(p, d)
        want[p] = sha(d)
    for i in range(120):
        sub = root / ("dir%02d" % (i % 6)) / ("sub%d" % (i % 3))
        sub.mkdir(parents=True, exist_ok=True)
        p = sub / ("file with a long name %03d.txt" % i)
        d = rand_bytes(RNG.randrange(0, 3000))
        write_file(p, d)
        want[p] = sha(d)
    # rename some, delete some
    for i, p in enumerate(sorted(k for k in want if "long name" in k.name)):
        if i % 4 == 0:
            q = p.with_name(p.stem + " renamed.dat")
            p.rename(q)
            want[q] = want.pop(p)
        elif i % 4 == 1:
            p.unlink()
            want.pop(p)
    return want


def verify_tree(want, label):
    bad = [p for p, h in want.items() if not p.exists() or sha(read_uncached(p)) != h]
    check(not bad, "%s: %d files read back identical from the card%s" % (
        label, len(want), "" if not bad else " (%d differ, e.g. %s)" % (len(bad), bad[0])))
    return not bad


STATE = {}


def t_files(dev):
    root = dev.root() / "CHSD_TEST"
    want = make_tree(root)
    STATE["tree"] = want
    verify_tree(want, "after writing")
    big = rand_bytes(8 << 20)
    t0 = time.perf_counter()
    write_file(root / "big.bin", big)
    wt = time.perf_counter() - t0
    t0 = time.perf_counter()
    same = read_uncached(root / "big.bin") == big
    rt = time.perf_counter() - t0
    check(same, "8 MB file read back identical")
    print("    file write %.0f KB/s, uncached read %.0f KB/s" % (8192 / wt, 8192 / rt))
    want[root / "big.bin"] = sha(big)


def ensure_tree(dev):
    if "tree" not in STATE:
        STATE["tree"] = make_tree(dev.root() / "CHSD_TEST")
    return STATE["tree"]


def t_readonly(dev):
    if not dev.status()["test"]:
        print("    (needs the test build: START is simulated with 's')")
        return
    want = ensure_tree(dev)
    st = dev.send("s")
    check(st["ro"], "START toggles read-only")
    # Let Windows' once-a-second media poll pick up the UNIT ATTENTION itself
    # (a pass-through command sent first would consume it instead).
    time.sleep(2.5)
    wait_for(lambda: volume_ready(dev.letter), 15)
    try:
        write_file(dev.root() / "CHSD_TEST" / "should_fail.bin", b"x" * 1000)
        failed = False
    except OSError as e:
        failed = True
        print("    (Windows says: %s)" % e.strerror)
    check(failed, "Windows refuses to write a file")
    verify_tree(want, "read-only")

    def body(h):
        r = scsi.command(h, [0x1A, 0, 0x3F, 0, 192, 0], data_in=192)
        check(r.ok and r.data[2] & 0x80, "MODE SENSE reports write-protected")
        r = scsi.write10(h, 0, scsi.read10(h, 0, 1).data)
        check(expect_sense(r, 7, 0x27), "raw WRITE -> DATA PROTECT / write protected")
    with_volume(dev, body)
    st = dev.send("s")
    check(not st["ro"], "START again: read-write")
    # Windows notices at its next media poll, which after heavy I/O can take
    # a few seconds; until then its file system still says write-protected.
    t0 = time.time()

    def writable():
        try:
            write_file(dev.root() / "CHSD_TEST" / "after_ro.bin", b"y" * 1000)
            return True
        except OSError:
            return False
    ok = wait_for(writable, 15, step=0.5)
    check(ok, "writing works again (after %.1f s)" % (time.time() - t0))
    if ok:
        want[dev.root() / "CHSD_TEST" / "after_ro.bin"] = sha(b"y" * 1000)


def t_eject(dev):
    if not dev.status()["test"]:
        print("    (needs the test build: bringing the drive back is A, simulated with 'a')")
        return
    want = ensure_tree(dev)
    scsi.eject(dev.letter)
    ok = wait_for(lambda: dev.status()["state"] == "EJECTED", 15)
    check(ok, "Eject (as Explorer does it) -> the sketch shows EJECTED")
    time.sleep(2)
    check(not volume_ready(dev.letter), "Windows shows the drive empty")
    dev.send("a")                                          # the A button
    ok = wait_for(lambda: volume_ready(dev.letter), 20)
    check(ok, "A (rescan) brings the drive back without replugging")
    check(dev.status()["state"] == "CONNECTED", "state CONNECTED again")
    if ok:
        verify_tree(want, "after eject + rescan")


def t_swap(dev):
    if not dev.status()["test"]:
        print("    (needs the test build: card removal is simulated with 'x')")
        return
    want = ensure_tree(dev)
    dev.send("x")                                          # the card "goes" for 4 s
    gone = wait_for(lambda: dev.status()["card"] == 0, 3)
    check(gone, "a pulled card is noticed within a second or two")

    def tur():
        h = scsi.open_volume(dev.letter)
        try:
            return scsi.command(h, [0, 0, 0, 0, 0, 0])
        finally:
            scsi.close(h)
    try:
        r = tur()
        check(expect_sense(r, 2, 0x3A), "the PC is told MEDIUM NOT PRESENT")
    except OSError:
        check(True, "the PC dropped the volume (MEDIUM NOT PRESENT)")
    back = wait_for(lambda: dev.status()["card"] > 0, 10)
    check(back, "the card is picked up again when it returns (no button)")
    ok = wait_for(lambda: volume_ready(dev.letter), 20)
    check(ok, "Windows mounts it again")
    if ok:
        verify_tree(want, "after a card swap")


def image_for(dev):
    """The build to re-upload: the kind that is running now."""
    d = TEST_BUILD if dev.status()["test"] else RELEASE_BUILD
    return d if (d / "CHSDtoUSB.ino.bin").exists() else None


def upload(dev, image):
    dev.close()
    r = subprocess.run(["arduino-cli", "upload", "-b", "CHGame:ch32v:CHGame", "-p", dev.port,
                        "--input-dir", str(image), str(SKETCH)], capture_output=True, text=True, timeout=120)
    if r.returncode:
        print(r.stdout[-800:] + r.stderr[-800:])
    return r.returncode == 0


def t_upload(dev):
    image = image_for(dev)
    if not image:
        print("    (compile into build/test or build/release first: this re-uploads the running build)")
        return
    want = ensure_tree(dev)
    t0 = time.time()
    ok = upload(dev, image)
    check(ok, "arduino-cli upload through the sketch's own serial port, drive mounted")
    dev.connect(timeout=40)
    print("    upload to drive back: %.1f s" % (time.time() - t0))
    verify_tree(want, "after the upload")


def t_upload_busy(dev):
    """Upload request in the middle of a long read, then of a long write:
    the card must be left idle, so the new sketch finds it working."""
    image = image_for(dev)
    if not image:
        print("    (compile into build/test or build/release first)")
        return
    h = scsi.open_volume(dev.letter)
    settle(h)
    sc = Scratch(h)
    if sc.count:
        sc.count = min(sc.count, 128)                    # all this test writes
        sc.save(h)
    scsi.close(h)
    for kind in ("read", "write"):
        if kind == "write" and not sc.count:
            print("    (no scratch area: mid-write upload skipped)")
            continue
        stop = threading.Event()
        count = [0, 0]

        def hammer():
            try:
                hh = scsi.open_volume(dev.letter)
            except OSError:
                return
            try:
                while not stop.is_set():
                    if kind == "read":
                        r = scsi.read10(hh, 8192 + (count[0] % 64) * 128, 128)
                    else:
                        r = scsi.write10(hh, sc.lba, bytes(128 * 512))
                    count[r.ok] += 1
            except OSError:
                pass
            finally:
                scsi.close(hh)
        th = threading.Thread(target=hammer)
        th.start()
        time.sleep(1.0)
        ok = upload(dev, image)
        stop.set()
        th.join(30)
        check(ok, "upload while the PC is %s 64 KB at a time (%d commands done)" % (
            "reading" if kind == "read" else "writing", count[1]))
        dev.connect(timeout=40)
        st = dev.status()
        check(st["card"] > 0, "the card works after the reboot")

        def body(h):
            settle(h)
            d = rand_bytes(64 * 512)
            if sc.count:
                write_blocks(h, sc.lba, d)
                return read_blocks(h, sc.lba, 64) == d
            return scsi.read10(h, 0, 64).ok
        check(with_volume(dev, body), "... reads and writes normally")
    if sc.count:
        with_volume(dev, sc.restore)


def t_speed(dev):
    def body(h):
        settle(h)
        start = 8192
        t0 = time.perf_counter()
        for i in range(128):
            scsi.read10(h, start + i * 128, 128)
        dt = time.perf_counter() - t0
        print("    raw sequential read, 64 KB commands: %.0f KB/s" % (8192 / dt))
        st = dev.status()
        check(st["fail"] == 0, "no failed reads in the session (retries: %d)" % st["retry"])
    with_volume(dev, body)


def cleanup(dev):
    root = dev.root() / "CHSD_TEST"
    if root.exists():
        shutil.rmtree(root)
    print("    removed %s" % root)


TESTS = [("identify", t_identify), ("scsi", t_scsi), ("raw", t_raw), ("faults", t_faults),
         ("files", t_files), ("readonly", t_readonly), ("eject", t_eject), ("swap", t_swap),
         ("upload", t_upload), ("upload_busy", t_upload_busy), ("speed", t_speed)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tests", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", action="store_true", help="leave \\CHSD_TEST on the card")
    a = ap.parse_args()
    if a.list:
        print(" ".join(n for n, _ in TESTS))
        return
    names = a.tests or [n for n, _ in TESTS]
    dev = Device()
    dev.connect()
    print("CHSDtoUSB on %s, drive %s:" % (dev.port, dev.letter))
    if dev.status()["test"]:
        dev.send("z")
    for name, fn in TESTS:
        if name not in names:
            continue
        print("\n[%s]" % name)
        try:
            fn(dev)
        except Exception as e:                            # keep going; report it
            check(False, "%s raised %s: %s" % (name, type(e).__name__, e))
            try:
                dev.connect()
            except Exception:
                pass
    if not a.keep and ("files" in names or len(names) > 1):
        print("\n[cleanup]")
        try:
            cleanup(dev)
        except OSError as e:
            print("    could not remove the test folder: %s" % e)
    print("\n%d checks passed, %d failed" % (C.passed, len(C.failed)))
    for f in C.failed:
        print("  FAIL " + f)
    print(dev.status()["line"])
    sys.exit(1 if C.failed else 0)


if __name__ == "__main__":
    main()
