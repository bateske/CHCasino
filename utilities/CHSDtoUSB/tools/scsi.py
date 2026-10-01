"""Raw SCSI commands to a USB drive on Windows (IOCTL_SCSI_PASS_THROUGH_DIRECT).

Works without administrator rights on a removable drive's volume handle
(\\\\.\\G:). Used by chsd_test.py to reach the protocol corners a file
system never exercises: bad opcodes, data phases that do not match the
command, reads past the end.
"""
import ctypes
import time
from ctypes import wintypes as W

_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.CreateFileW.restype = W.HANDLE
_k.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE]
_k.DeviceIoControl.argtypes = [W.HANDLE, W.DWORD, W.LPVOID, W.DWORD, W.LPVOID, W.DWORD,
                               ctypes.POINTER(W.DWORD), W.LPVOID]
_k.VirtualAlloc.restype = ctypes.c_void_p
_k.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, W.DWORD, W.DWORD]
_k.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, W.DWORD]

IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014
IOCTL_STORAGE_QUERY_PROPERTY = 0x2D1400
DATA_OUT, DATA_IN, DATA_NONE = 0, 1, 2
INVALID = W.HANDLE(-1).value


class SPTD(ctypes.Structure):
    _fields_ = [("Length", ctypes.c_ushort), ("ScsiStatus", ctypes.c_ubyte),
                ("PathId", ctypes.c_ubyte), ("TargetId", ctypes.c_ubyte), ("Lun", ctypes.c_ubyte),
                ("CdbLength", ctypes.c_ubyte), ("SenseInfoLength", ctypes.c_ubyte),
                ("DataIn", ctypes.c_ubyte), ("DataTransferLength", ctypes.c_ulong),
                ("TimeOutValue", ctypes.c_ulong), ("DataBuffer", ctypes.c_void_p),
                ("SenseInfoOffset", ctypes.c_ulong), ("Cdb", ctypes.c_ubyte * 16)]


class SPTDSense(ctypes.Structure):
    _fields_ = [("sptd", SPTD), ("filler", ctypes.c_ulong), ("sense", ctypes.c_ubyte * 32)]


def open_volume(letter, access=0xC0000000):
    """Handle to \\\\.\\X: (read/write by default, 0 for queries only)."""
    h = _k.CreateFileW("\\\\.\\%s:" % letter, access, 3, None, 3, 0, None)
    if h in (None, INVALID):
        raise OSError(ctypes.get_last_error(), "cannot open volume %s:" % letter)
    return h


def close(h):
    _k.CloseHandle(h)


def device_ids(letter):
    """(vendor, product, revision) from the drive's INQUIRY, via the storage stack."""
    h = open_volume(letter, 0)
    try:
        q = (ctypes.c_ulong * 3)(0, 0, 0)         # StorageDeviceProperty, PropertyStandardQuery
        out = ctypes.create_string_buffer(1024)
        n = W.DWORD()
        if not _k.DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, q, ctypes.sizeof(q), out, 1024,
                                  ctypes.byref(n), None):
            raise OSError(ctypes.get_last_error(), "IOCTL_STORAGE_QUERY_PROPERTY")
        raw = out.raw

        def s(off):
            o = int.from_bytes(raw[off:off + 4], "little")
            return raw[o:raw.index(b"\0", o)].decode("ascii", "replace").strip() if o else ""
        return s(12), s(16), s(20)
    finally:
        close(h)


class Result:
    def __init__(self, ok, status, data, sense, returned):
        self.ok, self.status, self.data, self.sense, self.returned = ok, status, data, sense, returned

    @property
    def key(self):
        return (self.sense[2] & 0x0F, self.sense[12], self.sense[13]) if self.sense else None

    def __repr__(self):
        k = self.key
        ks = "" if not k or self.status == 0 else " sense %02X/%02X/%02X" % k
        return "<ok=%s status=%d len=%d%s>" % (self.ok, self.status, self.returned, ks)


def command(h, cdb, data_in=0, data_out=None, direction=None, xfer_len=None, timeout=10):
    """Send one CDB. data_in: bytes expected back; data_out: bytes to send.
    direction / xfer_len override what goes into the CBW, to build mismatches."""
    n = len(data_out) if data_out is not None else data_in
    if xfer_len is None:
        xfer_len = n
    if direction is None:
        direction = DATA_OUT if data_out is not None else (DATA_IN if data_in else DATA_NONE)
    size = max(xfer_len, 4096)
    buf = _k.VirtualAlloc(None, size, 0x3000, 0x04)
    try:
        if data_out is not None:
            ctypes.memmove(buf, bytes(data_out), len(data_out))
        s = SPTDSense()
        s.sptd.Length = ctypes.sizeof(SPTD)
        s.sptd.CdbLength = len(cdb)
        s.sptd.SenseInfoLength = 32
        s.sptd.DataIn = direction
        s.sptd.DataTransferLength = xfer_len
        s.sptd.TimeOutValue = timeout
        s.sptd.DataBuffer = buf if xfer_len else None
        s.sptd.SenseInfoOffset = SPTDSense.sense.offset
        for i, b in enumerate(cdb):
            s.sptd.Cdb[i] = b
        ret = W.DWORD()
        ok = bool(_k.DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH_DIRECT, ctypes.byref(s), ctypes.sizeof(s),
                                     ctypes.byref(s), ctypes.sizeof(s), ctypes.byref(ret), None))
        if not ok:
            raise OSError(ctypes.get_last_error(), "DeviceIoControl(SPTD)")
        got = s.sptd.DataTransferLength
        data = ctypes.string_at(buf, got) if (got and direction == DATA_IN) else b""
        sense = bytes(s.sense) if s.sptd.ScsiStatus else b""
        return Result(s.sptd.ScsiStatus == 0, s.sptd.ScsiStatus, data, sense, got)
    finally:
        _k.VirtualFree(buf, 0, 0x8000)


def eject(letter):
    """What Explorer's Eject does: lock and dismount the volume, allow
    removal, then IOCTL_STORAGE_EJECT_MEDIA (START STOP UNIT to the drive)."""
    h = open_volume(letter)
    try:
        n = W.DWORD()
        # The lock fails while anything has a file open on the volume (right
        # after writing, that is usually the virus scanner): keep trying.
        for _ in range(40):
            if _k.DeviceIoControl(h, 0x90018, None, 0, None, 0, ctypes.byref(n), None):   # FSCTL_LOCK_VOLUME
                break
            time.sleep(0.25)
        else:
            raise OSError(ctypes.get_last_error(), "volume in use: cannot lock it")
        for code, arg in ((0x90020, None),                                # FSCTL_DISMOUNT_VOLUME
                          (0x2D4804, ctypes.c_ubyte(0)), (0x2D4808, None)):
            p = ctypes.byref(arg) if arg is not None else None
            if not _k.DeviceIoControl(h, code, p, 1 if arg is not None else 0, None, 0, ctypes.byref(n), None):
                raise OSError(ctypes.get_last_error(), "DeviceIoControl(%#x)" % code)
    finally:
        close(h)


def read10(h, lba, count, **kw):
    cdb = [0x28, 0, *lba.to_bytes(4, "big"), 0, *count.to_bytes(2, "big"), 0]
    return command(h, cdb, data_in=count * 512, **kw)


def write10(h, lba, data, **kw):
    count = len(data) // 512
    cdb = [0x2A, 0, *lba.to_bytes(4, "big"), 0, *count.to_bytes(2, "big"), 0]
    return command(h, cdb, data_out=data, **kw)


def read_capacity(h):
    r = command(h, [0x25] + [0] * 9, data_in=8)
    if not r.ok:
        return None
    return int.from_bytes(r.data[0:4], "big") + 1, int.from_bytes(r.data[4:8], "big")
