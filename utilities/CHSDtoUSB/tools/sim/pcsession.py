"""A pretend PC using the card: the block commands it would send.

    python tools/sim/pcsession.py OUTDIR [--scenario NAME]

Builds a 16 GB FAT32 card (MBR, 32 KB clusters, as SD cards come) in
memory and then plays a scenario on it the way Windows Explorer does:
mount (boot sector, FSInfo, the whole FAT, the root), browse, copy files
in (directory entry first, clusters preallocated, data in 64 KB writes,
entry finished last), copy out, rename, move, delete, make folders, eject.
Every command goes into OUTDIR/trace.txt, the data of each metadata write
into OUTDIR/blob.bin and the card as it was before the scenario into
OUTDIR/card.bin. tools/sim/sim.py feeds the three to the simulator.

The file system code is written for this from the FAT specification; it
is not a general FAT driver (no FAT12/16, one partition, no fragmentation
beyond what next-fit allocation produces).

trace.txt, one command a line:
    card BLOCKS            the card in the slot (first line)
    usb                    the PC enumerates the reader
    R LBA N                READ (10)
    W LBA N @OFF           WRITE (10), data at OFF in blob.bin
    W LBA N ~              WRITE (10), file data (the simulator makes it up)
    wait MS                the PC does nothing for a while
    eject                  the PC ejects the medium
    pull / insert          the card comes out / goes back in
    fault LBA              the next read of LBA fails its CRC once (a retry fixes it)
    press BUTTON MS        a button held for MS
    snap NAME              screenshot
    rec MS / stop          record a frame every MS of time / stop
    note TEXT              shown in the simulator's log
"""
import argparse
import random
import struct
from pathlib import Path

SECTOR = 512
CARD_BLOCKS = 31_116_288          # a "16 GB" card: 15.9 GB
PART_LBA = 8192                   # 4 MB aligned, as SD formatters do
SPC = 64                          # 32 KB clusters
RSV = 32
NFATS = 2
EOC = 0x0FFFFFFF
ATTR_DIR, ATTR_ARCH, ATTR_LFN, ATTR_LABEL = 0x10, 0x20, 0x0F, 0x08
MAX_XFER = 128                    # blocks per command (64 KB), as Windows sends


def lfn_checksum(sfn):
    s = 0
    for c in sfn:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


class Trace:
    def __init__(self):
        self.lines = []
        self.blob = bytearray()

    def add(self, line):
        self.lines.append(line)

    def read(self, lba, n):
        while n:
            k = min(n, MAX_XFER)
            self.add(f"R {lba} {k}")
            lba += k
            n -= k

    def write(self, lba, data):
        assert len(data) % SECTOR == 0
        for i in range(0, len(data) // SECTOR, MAX_XFER):
            chunk = data[i * SECTOR:(i + MAX_XFER) * SECTOR]
            self.add(f"W {lba + i} {len(chunk) // SECTOR} @{len(self.blob)}")
            self.blob += chunk

    def write_filedata(self, lba, n):
        while n:
            k = min(n, MAX_XFER)
            self.add(f"W {lba} {k} ~")
            lba += k
            n -= k


class Card:
    """The card's blocks (sparse) plus the FAT32 volume on it."""

    def __init__(self, trace, label="CHGAME"):
        self.t = trace
        self.blocks = {}
        self.part_blocks = CARD_BLOCKS - PART_LBA
        # FAT size: enough entries for every cluster the rest leaves room for.
        fat = 1
        while True:
            clusters = (self.part_blocks - RSV - NFATS * fat) // SPC
            need = (clusters + 2) * 4 // SECTOR + 1
            if need <= fat:
                break
            fat = need
        self.fat_size, self.clusters = fat, clusters
        self.fat_lba = PART_LBA + RSV
        self.data_lba = self.fat_lba + NFATS * fat
        self.fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF, 2: EOC}
        self.next_free = 3
        self.free = clusters - 1
        self.time = (12 << 11) | (34 << 5)          # 12:34:00
        self.date = ((2026 - 1980) << 9) | (10 << 5) | 2
        self.dirs = {"": [2]}                       # path -> cluster chain
        self.entries = {"": []}                     # path -> list of 32-byte slots
        self.label = label
        self._format()

    # ---- raw blocks ----
    def get(self, lba):
        return self.blocks.get(lba, bytes(SECTOR))

    def put(self, lba, data):
        if any(data):
            self.blocks[lba] = bytes(data)
        else:
            self.blocks.pop(lba, None)

    def cluster_lba(self, c):
        return self.data_lba + (c - 2) * SPC

    # ---- formatting ----
    def _format(self):
        mbr = bytearray(SECTOR)
        struct.pack_into("<BBBBBBBBII", mbr, 446, 0x00, 0xFE, 0xFF, 0xFF, 0x0C, 0xFE, 0xFF, 0xFF,
                         PART_LBA, self.part_blocks)
        mbr[510:512] = b"\x55\xAA"
        self.put(0, mbr)
        b = bytearray(SECTOR)
        b[0:3] = b"\xEB\x58\x90"
        b[3:11] = b"MSDOS5.0"
        struct.pack_into("<HBHBHHBHHHII", b, 11, 512, SPC, RSV, NFATS, 0, 0, 0xF8, 0, 63, 255,
                         PART_LBA, self.part_blocks)
        struct.pack_into("<IHHIHH", b, 36, self.fat_size, 0, 0, 2, 1, 6)
        b[64] = 0x80
        b[66] = 0x29
        struct.pack_into("<I", b, 67, 0x1234ABCD)
        b[71:82] = self.label.ljust(11).encode()
        b[82:90] = b"FAT32   "
        b[510:512] = b"\x55\xAA"
        self.put(PART_LBA, b)
        self.put(PART_LBA + 6, b)
        self.put(PART_LBA + 1, self._fsinfo())
        self.put(PART_LBA + 7, self._fsinfo())
        self._flush_fat(range(0, 1), trace=False)
        label = bytearray(32)
        label[0:11] = self.label.ljust(11).encode()
        label[11] = ATTR_LABEL
        self.entries[""].append(bytes(label))
        self._flush_dir("", trace=False)

    def _fsinfo(self):
        f = bytearray(SECTOR)
        struct.pack_into("<I", f, 0, 0x41615252)
        struct.pack_into("<III", f, 484, 0x61417272, self.free, self.next_free)
        f[510:512] = b"\x55\xAA"
        return f

    # ---- FAT ----
    def _fat_block(self, s):
        b = bytearray(SECTOR)
        for i in range(128):
            v = self.fat.get(s * 128 + i, 0)
            struct.pack_into("<I", b, i * 4, v)
        return b

    def _flush_fat(self, sectors, trace=True):
        """Write FAT sectors (both copies) whose content changed."""
        for copy in range(NFATS):
            runs = []
            for s in sorted(set(sectors)):
                lba = self.fat_lba + copy * self.fat_size + s
                new = self._fat_block(s)
                if new == self.get(lba):
                    continue
                self.put(lba, new)
                if runs and runs[-1][0] + len(runs[-1][1]) // SECTOR == lba:
                    runs[-1][1].extend(new)
                else:
                    runs.append([lba, bytearray(new)])
            if trace:
                for lba, data in runs:
                    self.t.write(lba, data)

    def alloc(self, n):
        """n clusters, next-fit like Windows, chained. Returns (first, touched FAT sectors)."""
        chain = []
        c = self.next_free
        while len(chain) < n:
            if c >= self.clusters + 2:
                c = 2
            if self.fat.get(c, 0) == 0:
                chain.append(c)
            c += 1
        self.next_free = c
        for a, b in zip(chain, chain[1:] + [None]):
            self.fat[a] = b if b else EOC
        self.free -= n
        return chain[0], {x // 128 for x in chain}

    def chain(self, c):
        out = []
        while 2 <= c < 0x0FFFFFF8:
            out.append(c)
            c = self.fat.get(c, 0)
        return out

    def runs(self, first, size):
        """The file's blocks as (lba, count) runs of contiguous clusters."""
        blocks = (size + SECTOR - 1) // SECTOR
        out = []
        for c in self.chain(first):
            k = min(SPC, blocks)
            if k <= 0:
                break
            lba = self.cluster_lba(c)
            if out and out[-1][0] + out[-1][1] == lba:
                out[-1][1] += k
            else:
                out.append([lba, k])
            blocks -= k
        return out

    def release(self, c):
        ch = self.chain(c)
        for x in ch:
            self.fat.pop(x, None)
        self.free += len(ch)
        return {x // 128 for x in ch}

    # ---- directories ----
    @staticmethod
    def split(path):
        d, _, n = path.rpartition("/")
        return d, n

    def short_name(self, d, name):
        base, dot, ext = name.rpartition(".") if "." in name else (name, "", "")
        ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")
        nt = 0
        for part, flag in ((base, 0x08), (ext, 0x10)):
            if part and part == part.lower() and part != part.upper():
                nt |= flag
        if (base and len(base) <= 8 and len(ext) <= 3 and set(base.upper() + ext.upper()) <= ok
                and (base == base.upper() or base == base.lower())
                and (ext == ext.upper() or ext == ext.lower())):
            return (base.upper().ljust(8) + ext.upper().ljust(3)).encode(), nt, False
        clean = "".join(ch for ch in base.upper() if ch in ok)[:6] or "FILE"
        cext = "".join(ch for ch in ext.upper() if ch in ok)[:3]
        taken = {e[0:11] for e in self.entries[d] if e[0] not in (0, 0xE5)}
        for k in range(1, 10):
            sfn = (f"{clean}~{k}".ljust(8) + cext.ljust(3)).encode()
            if sfn not in taken:
                return sfn, 0, True
        raise RuntimeError("too many similar names")

    def make_entries(self, d, name, attr, cluster, size):
        sfn, nt, lfn = self.short_name(d, name)
        e = bytearray(32)
        e[0:11] = sfn
        e[11] = attr
        e[12] = nt
        struct.pack_into("<BHHHHHHHI", e, 13, 0, self.time, self.date, self.date, cluster >> 16,
                         self.time, self.date, cluster & 0xFFFF, size)
        out = []
        if lfn:
            chars = [ord(c) for c in name]
            if len(chars) % 13:
                chars += [0] + [0xFFFF] * (12 - len(chars) % 13)
            parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
            ck = lfn_checksum(sfn)
            for i, p in enumerate(parts):
                le = bytearray(32)
                le[0] = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
                struct.pack_into("<5H", le, 1, *p[0:5])
                le[11] = ATTR_LFN
                le[13] = ck
                struct.pack_into("<6H", le, 14, *p[5:11])
                struct.pack_into("<2H", le, 28, *p[11:13])
                out.append(bytes(le))
            out = out[::-1]
        return out + [bytes(e)]

    def dir_lba(self, d, slot):
        per = SPC * 16
        c = self.dirs[d][slot // per]
        return self.cluster_lba(c) + (slot % per) // 16

    def _flush_dir(self, d, trace=True):
        """Write the directory's blocks that changed, one command per run."""
        ents = self.entries[d]
        need = (len(ents) + 1 + SPC * 16 - 1) // (SPC * 16)
        while len(self.dirs[d]) < need:                    # the directory grows a cluster
            c, secs = self.alloc(1)
            self.fat[self.dirs[d][-1]] = c
            self.dirs[d].append(c)
            if trace:
                self.t.write(self.cluster_lba(c), bytes(SPC * SECTOR))
                self._flush_fat(secs | {self.dirs[d][-2] // 128})
            for s in range(SPC):
                self.put(self.cluster_lba(c) + s, bytes(SECTOR))
        runs = []
        for blk in range((len(ents) + 15) // 16 + 1):
            lba = self.dir_lba(d, blk * 16) if blk * 16 < len(self.dirs[d]) * SPC * 16 else None
            if lba is None:
                break
            data = b"".join(ents[blk * 16:blk * 16 + 16]).ljust(SECTOR, b"\0")
            if data == self.get(lba):
                continue
            self.put(lba, data)
            if runs and runs[-1][0] + len(runs[-1][1]) // SECTOR == lba:
                runs[-1][1].extend(data)
            else:
                runs.append([lba, bytearray(data)])
        if trace:
            for lba, data in runs:
                self.t.write(lba, data)

    def find(self, path):
        d, n = self.split(path)
        ents = self.entries[d]
        for i, e in enumerate(ents):
            if e[0] in (0, 0xE5) or e[11] == ATTR_LFN or e[11] & ATTR_LABEL:
                continue
            if self.long_name(ents, i) == n:
                return d, i
        raise FileNotFoundError(path)

    @staticmethod
    def long_name(ents, i):
        chars = []
        j = i - 1
        while j >= 0 and ents[j][11] == ATTR_LFN and ents[j][0] != 0xE5:
            e = ents[j]
            for off in (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30):
                (c,) = struct.unpack_from("<H", e, off)
                if c in (0, 0xFFFF):
                    return "".join(chars)
                chars.append(chr(c))
            j -= 1
        if chars:
            return "".join(chars)
        e = ents[i]
        base, ext = e[0:8].decode().rstrip(), e[8:11].decode().rstrip()
        if e[12] & 0x08:
            base = base.lower()
        if e[12] & 0x10:
            ext = ext.lower()
        return base + ("." + ext if ext else "")

    def span(self, d, i):
        """Slots of entry i with its long-name parts."""
        j = i
        while j > 0 and self.entries[d][j - 1][11] == ATTR_LFN and self.entries[d][j - 1][0] != 0xE5:
            j -= 1
        return j, i

    def place(self, d, new):
        """Put entries in the first run of free slots (or the end)."""
        ents = self.entries[d]
        run = 0
        for i, e in enumerate(ents):
            run = run + 1 if e[0] == 0xE5 else 0
            if run == len(new):
                ents[i - run + 1:i + 1] = new
                return i
        ents.extend(new)
        return len(ents) - 1

    # ---- what the PC does ----
    def tick(self, seconds=2):
        s = (self.time & 31) * 2 + seconds
        self.time = (self.time & ~31) | ((s // 2) & 31)

    def mount(self):
        t = self.t
        t.read(0, 1)
        t.read(PART_LBA, 1)
        t.read(PART_LBA + 1, 1)
        t.read(self.fat_lba, self.fat_size)                # the free-space scan
        self.list_dir("")

    def list_dir(self, d):
        for c in self.dirs[d]:
            used = (len(self.entries[d]) + 15) // 16 if c == self.dirs[d][-1] else SPC
            self.t.read(self.cluster_lba(c), max(1, min(SPC, used + 1)))

    def mkdir(self, path):
        d, n = self.split(path)
        c, secs = self.alloc(1)
        dot = bytearray(32)
        dot[0:11] = b".          "
        dot[11] = ATTR_DIR
        struct.pack_into("<HHHHHHHI", dot, 14, self.time, self.date, self.date, c >> 16,
                         self.time, self.date, c & 0xFFFF, 0)
        dotdot = bytearray(dot)
        dotdot[0:11] = b"..         "
        parent = self.dirs[d][0] if d else 0
        struct.pack_into("<H", dotdot, 20, parent >> 16)
        struct.pack_into("<H", dotdot, 26, parent & 0xFFFF)
        self.dirs[path] = [c]
        self.entries[path] = [bytes(dot), bytes(dotdot)]
        first = bytearray(SPC * SECTOR)
        first[0:64] = dot + dotdot
        for s in range(SPC):
            self.put(self.cluster_lba(c) + s, first[s * SECTOR:(s + 1) * SECTOR])
        self.t.write(self.cluster_lba(c), first)
        self._flush_fat(secs)
        self.place(d, self.make_entries(d, n, ATTR_DIR, c, 0))
        self._flush_dir(d)

    def copy_in(self, path, size, snap=None):
        d, n = self.split(path)
        self.tick()
        i = self.place(d, self.make_entries(d, n, ATTR_ARCH, 0, 0))
        self._flush_dir(d)                                  # 1. the entry, empty
        nclus = max(1, (size + SPC * SECTOR - 1) // (SPC * SECTOR)) if size else 0
        first = 0
        if nclus:
            first, secs = self.alloc(nclus)                 # 2. clusters reserved
            self._flush_fat(secs)
            e = bytearray(self.entries[d][i])
            struct.pack_into("<H", e, 20, first >> 16)
            struct.pack_into("<H", e, 26, first & 0xFFFF)
            struct.pack_into("<I", e, 28, size)
            self.entries[d][i] = bytes(e)
            self._flush_dir(d)                              # 3. size and cluster set
            mark = len(self.t.lines)
            for lba, n in self.runs(first, size):           # 4. the data, 64 KB at a time
                self.t.write_filedata(lba, n)
            if snap:                                        # a screenshot halfway through the data
                k = (mark + len(self.t.lines)) // 2
                self.t.lines.insert(k, f"snap {snap}")
        self.tick()
        e = bytearray(self.entries[d][i])
        struct.pack_into("<HH", e, 22, self.time, self.date)
        self.entries[d][i] = bytes(e)
        self._flush_dir(d)                                  # 5. closed: modification time

    def copy_out(self, path):
        d, i = self.find(path)
        e = self.entries[d][i]
        c = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
        (size,) = struct.unpack_from("<I", e, 28)
        for lba, n in self.runs(c, size):
            self.t.read(lba, n)

    def delete(self, path):
        d, i = self.find(path)
        a, b = self.span(d, i)
        e = self.entries[d][i]
        c = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
        for k in range(a, b + 1):
            self.entries[d][k] = b"\xE5" + self.entries[d][k][1:]
        self._flush_dir(d)
        if c:
            self._flush_fat(self.release(c))

    def rename(self, path, new_path):
        d, i = self.find(path)
        nd, nn = self.split(new_path)
        a, b = self.span(d, i)
        e = bytearray(self.entries[d][i])
        attr = e[11]
        c = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
        (size,) = struct.unpack_from("<I", e, 28)
        for k in range(a, b + 1):
            self.entries[d][k] = b"\xE5" + self.entries[d][k][1:]
        new = self.make_entries(nd, nn, attr, c, size)
        last = bytearray(new[-1])
        last[13:32] = e[13:32]                              # times, cluster, size as they were
        new[-1] = bytes(last)
        if nd != d:
            self._flush_dir(d)
        self.place(nd, new)
        self._flush_dir(nd)

    def boot_dirty(self, dirty):
        """Linux style: the boot sector's 'dirty' flag set on mount, cleared on unmount."""
        b = bytearray(self.get(PART_LBA))
        b[0x41] = (b[0x41] | 1) if dirty else (b[0x41] & ~1)
        self.put(PART_LBA, b)
        self.t.write(PART_LBA, b)

    def reformat(self, label):
        """A quick format: new boot sector (new serial), FSInfo, empty FATs and root."""
        t = self.t
        self.fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF, 2: EOC}
        self.next_free, self.free = 3, self.clusters - 1
        self.dirs, self.entries, self.label = {"": [2]}, {"": []}, label
        b = bytearray(self.get(PART_LBA))
        struct.pack_into("<I", b, 67, 0x5EED0001)
        b[71:82] = label.ljust(11).encode()
        self.put(PART_LBA, b)
        t.write(PART_LBA, b)
        t.write(PART_LBA + 1, self._fsinfo())
        for copy in range(NFATS):                          # the used part of each FAT, cleared
            lba = self.fat_lba + copy * self.fat_size
            t.write(lba, self._fat_block(0) + bytes(15 * SECTOR))
        lab = bytearray(32)
        lab[0:11] = label.ljust(11).encode()
        lab[11] = ATTR_LABEL
        self.entries[""].append(bytes(lab))
        root = self.cluster_lba(2)
        data = bytes(lab).ljust(SPC * SECTOR, b"\0")
        for s in range(SPC):
            self.put(root + s, data[s * SECTOR:(s + 1) * SECTOR])
        t.write(root, data)

    def eject(self):
        lba = PART_LBA + 1
        f = self._fsinfo()
        if f != self.get(lba):
            self.put(lba, f)
            self.t.write(lba, f)
        self.t.add("eject")


def scenario_demo(t, card):
    """A PC session: mount, a folder of photos, small files, reading back,
    renames, a move, deletes, the other pages, eject, the card pulled."""
    t.add("rec 100")
    t.add("note card in, PC asleep")
    t.add("wait 1200")
    t.add("snap standby")
    t.add("usb")
    t.add("wait 400")
    t.add("note mount")
    card.mount()
    t.add("wait 700")
    card.mkdir("Holiday 2026")
    t.add("wait 300")
    t.add("note copy photos")
    rng = random.Random(7)
    for k in range(1, 6):
        card.copy_in(f"Holiday 2026/IMG_{4100 + k}.JPG", rng.randrange(1_200_000, 2_600_000),
                     snap="copying" if k == 4 else None)
    t.add("wait 600")
    t.add("note small files")
    for name in ("notes.txt", "Packing list.docx", "README.TXT", "map.png"):
        card.copy_in(name, rng.randrange(3_000, 90_000))
    t.add("wait 600")
    t.add("note read back")
    card.copy_out("Holiday 2026/IMG_4102.JPG")
    card.copy_out("Holiday 2026/IMG_4103.JPG")
    t.add("wait 500")
    card.rename("notes.txt", "Trip notes.txt")
    t.add("wait 300")
    card.rename("map.png", "Holiday 2026/map.png")
    t.add("wait 300")
    card.delete("README.TXT")
    t.add("wait 300")
    card.delete("Holiday 2026/IMG_4105.JPG")
    t.add("wait 1200")
    t.add("snap events")
    t.add("press RIGHT 100")
    t.add("wait 800")
    t.add("snap stats")
    t.add("press RIGHT 100")
    t.add("wait 800")
    t.add("snap card")
    t.add("press RIGHT 100")
    t.add("wait 500")
    card.eject()
    t.add("wait 1500")
    t.add("snap ejected")
    t.add("pull")
    t.add("wait 1500")
    t.add("snap nocard")
    t.add("stop")


def scenario_edge(t, card):
    """The awkward cases: a CRC retry, Linux's dirty flag, read-only, a card
    pulled and put back, a quick format."""
    t.add("usb")
    t.add("wait 300")
    card.mount()
    t.add(f"fault {card.cluster_lba(2)}")
    card.list_dir("")                                       # a block that needs a second try
    card.boot_dirty(True)                                   # not a format
    card.copy_in("a.txt", 1000)
    card.boot_dirty(False)
    t.add("press START 100")                                # read-only on
    t.add("press START 100")                                # and off
    t.add("pull")
    t.add("wait 1500")
    t.add("insert")
    t.add("wait 1500")
    card.mount()
    card.reformat("FRESH")
    t.add("wait 500")
    card.mount()
    card.mkdir("New folder")
    card.copy_in("New folder/b.bin", 300_000)
    card.rename("New folder/b.bin", "New folder/c.bin")
    card.delete("New folder/c.bin")
    t.add("wait 500")
    t.add("snap edge")


SCENARIOS = {"demo": scenario_demo, "edge": scenario_edge}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--scenario", default="demo", choices=sorted(SCENARIOS))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    t = Trace()
    card = Card(t)
    start = dict(card.blocks)
    t.add(f"card {CARD_BLOCKS}")
    SCENARIOS[a.scenario](t, card)
    with open(out / "card.bin", "wb") as f:
        for lba in sorted(start):
            f.write(struct.pack("<I", lba) + start[lba])
    (out / "blob.bin").write_bytes(bytes(t.blob))
    (out / "trace.txt").write_text("\n".join(t.lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(t.lines)} lines, {len(t.blob) // SECTOR} metadata blocks, card {len(start)} blocks")


if __name__ == "__main__":
    main()
