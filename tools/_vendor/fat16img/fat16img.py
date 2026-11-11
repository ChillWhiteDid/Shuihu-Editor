#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
fat16img.py — 离线读写 FAT16 磁盘镜像（DOSBox / Windows 9x / DOS）。

用途
----
不启动 VM / DOSBox，直接列出、抽取、替换 FAT16 镜像里的文件；适用于
DOSBox Win9x/DOS 磁盘、老游戏数据盘等。

设计要点
--------
* 只支持 FAT16（MBR 分区取第一个 FAT16；superfloppy 也支持）。
* M1 写路径只做「已存在文件的原位替换」：
    只写目标文件的数据簇 + 该文件的 32 B 目录项，**不改 FAT**；
    写前落 journal，写后回读校验；任一失败自动回滚。
* M2 写路径（建议先在镜像副本上验证）：
    按需分配/释放簇、新建 8.3 文件、删除文件；所有改动先落 journal。
* 受保护路径由 preset 提供（例如 win9x）：写/删拒绝，读允许。
* 写操作前检查 DOSBox 进程，并对镜像做独占打开（share mode = 0）。

用法
----
  python fat16img.py --image <img> info
  python fat16img.py --image <img> ls / --long
  python fat16img.py --image <img> get /path/file local.bin
  python fat16img.py --image <img> put local.bin /path/file
  python fat16img.py --image <img> check
  python fat16img.py --image <img> backup <dest.img>
  python fat16img.py --image <img> rollback <journal.bin>
  python fat16img.py --image <img> --protect-preset win9x put ...

退出码：0 成功；2 用法；3 镜像被占用/DOSBox 运行；4 不支持/结构损坏；
        5 校验失败已回滚；6 容量/空间不足；7 受保护路径（preset）。
"""

from __future__ import print_function

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import time

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PRESET_DIR = os.path.join(SCRIPT_DIR, "presets")

SECTOR = 512
CLUSTER_END = 0xFFF8          # FAT16：>= 0xFFF8 视为链尾
BAD_CLUSTER = 0xFFF7


class ProtectPolicy(object):
    """写/删保护策略：preset（如 win9x）+ 调用方追加的目录/根文件。"""

    def __init__(self, preset="none", dirs=None, root_files=None, attr_mask=0):
        self.preset = preset or "none"
        self.dirs = set(x.upper() for x in (dirs or []))
        self.root_files = set(x.upper() for x in (root_files or []))
        self.attr_mask = int(attr_mask or 0)


def load_preset(name):
    name = (name or "none").strip()
    if name in ("", "none"):
        return ProtectPolicy()
    path = os.path.join(PRESET_DIR, name + ".json")
    if not os.path.isfile(path):
        raise UsageError("找不到保护 preset：%s（应位于 %s）" % (name, PRESET_DIR))
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return ProtectPolicy(name, data.get("dirs") or [],
                         data.get("root_files") or [],
                         data.get("attr_mask", 0))


def load_protect(args):
    policy = ProtectPolicy() if args.no_protect else load_preset(args.protect_preset)
    for d in (args.protect_dir or []):
        policy.dirs.add(d.upper())
    for f in (args.protect_file or []):
        policy.root_files.add(f.upper())
    return policy

JOURNAL_MAGIC = b"IMGFSJ1\n"
JOURNAL_VERSION = 1
JOURNAL_KEEP = 20
JOURNAL_DIRNAME = ".imgfs"

_83_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")


# ---------------------------------------------------------------------------
# 异常
# ---------------------------------------------------------------------------

class ImgError(Exception):
    code = 4


class UsageError(ImgError):
    code = 2


class GuardError(ImgError):
    code = 3


class CapacityError(ImgError):
    code = 6


class VerifyError(ImgError):
    code = 5


class ProtectedError(ImgError):
    code = 7


# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def read_local_file(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as ex:
        raise ImgError("读本地文件失败：%s（%s）" % (path, ex))


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def dos_datetime(now=None):
    """返回 (wrtTime, wrtDate)。"""
    t = time.localtime(now if now is not None else time.time())
    year = max(1980, min(2107, t.tm_year))
    date = ((year - 1980) << 9) | ((t.tm_mon & 0x0F) << 5) | (t.tm_mday & 0x1F)
    tm = ((t.tm_hour & 0x1F) << 11) | ((t.tm_min & 0x3F) << 5) | ((t.tm_sec // 2) & 0x1F)
    return tm, date


def format_dos_datetime(tm, date):
    if not date:
        return "-"
    y = (date >> 9) + 1980
    mo = (date >> 5) & 0x0F
    d = date & 0x1F
    h = (tm >> 11) & 0x1F
    mi = (tm >> 5) & 0x3F
    s = (tm & 0x1F) * 2
    return "%04d-%02d-%02d %02d:%02d:%02d" % (y, mo, d, h, mi, s)


def format_bytes(n):
    if n >= 1024 * 1024:
        return "%.1f MB" % (n / 1048576.0)
    if n >= 1024:
        return "%.1f KB" % (n / 1024.0)
    return "%d B" % n


# ---------------------------------------------------------------------------
# Windows 进程检查 / 独占打开
# ---------------------------------------------------------------------------

def dosbox_running():
    """枚举进程，发现 dosbox*.exe 返回进程名，否则 None。"""
    if os.name != "nt":
        return None
    try:
        from ctypes import wintypes

        class PROCESSENTRY32W(ctypes.Structure):
            _fields_ = [
                ("dwSize", wintypes.DWORD),
                ("cntUsage", wintypes.DWORD),
                ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t),
                ("th32ModuleID", wintypes.DWORD),
                ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD),
                ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wintypes.DWORD),
                ("szExeFile", wintypes.WCHAR * 260),
            ]

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        k32.Process32FirstW.restype = wintypes.BOOL
        k32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
        k32.Process32NextW.restype = wintypes.BOOL
        k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
        k32.CloseHandle.argtypes = [wintypes.HANDLE]

        snap = k32.CreateToolhelp32Snapshot(0x00000002, 0)
        if not snap or snap == ctypes.c_void_p(-1).value:
            return None
        pe = PROCESSENTRY32W()
        pe.dwSize = ctypes.sizeof(pe)
        found = None
        try:
            ok = k32.Process32FirstW(snap, ctypes.byref(pe))
            while ok:
                exe = pe.szExeFile.lower()
                if exe.startswith("dosbox"):
                    found = pe.szExeFile
                    break
                ok = k32.Process32NextW(snap, ctypes.byref(pe))
        finally:
            k32.CloseHandle(snap)
        return found
    except Exception:
        return None


def exclusive_open(path):
    """以 share mode = 0 打开镜像；任何其他进程（含 DOSBox）持有即失败。"""
    if os.name != "nt":
        f = open(path, "r+b", buffering=0)
        try:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except Exception:
            f.close()
            raise GuardError("镜像被其他进程占用，无法独占打开：%s" % path)
        return f

    from ctypes import wintypes
    import msvcrt

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    FILE_ATTRIBUTE_NORMAL = 0x80
    INVALID = ctypes.c_void_p(-1).value

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]

    h = None
    err = 0
    attempts = 6
    for attempt in range(attempts):
        h = k32.CreateFileW(
            path, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, None
        )
        if h and h != INVALID:
            break
        err = ctypes.get_last_error()
        if err != 32:
            break
        if attempt < attempts - 1:
            # 刚写完/杀软/OneDrive 可能短暂持有句柄：短暂重试几次
            time.sleep(0.25)
    if not h or h == INVALID:
        if err == 32:
            raise GuardError(
                "镜像正被其他进程占用（DOSBox 没关？或 WinImage/杀软/OneDrive 在扫描；已重试 %d 次）：%s"
                % (attempts, path)
            )
        raise ImgError("打开镜像失败：WinError %d（%s）" % (err, path))
    try:
        fd = msvcrt.open_osfhandle(h, os.O_RDWR | os.O_BINARY)
    except Exception:
        k32.CloseHandle(h)
        raise
    return os.fdopen(fd, "r+b", buffering=0)


# ---------------------------------------------------------------------------
# 目录项
# ---------------------------------------------------------------------------

class DirEntry(object):
    __slots__ = ("name83", "lfn", "attr", "cluster", "size", "entry_offset",
                 "parent", "is_dir", "wtime", "wdate")

    def __init__(self, name83, lfn, attr, cluster, size, entry_offset,
                 parent, wtime=0, wdate=0):
        self.name83 = name83
        self.lfn = lfn
        self.attr = attr
        self.cluster = cluster
        self.size = size
        self.entry_offset = entry_offset
        self.parent = parent
        self.is_dir = bool(attr & 0x10)
        self.wtime = wtime
        self.wdate = wdate

    @property
    def display(self):
        return self.lfn or self.name83

    @property
    def path(self):
        if not self.parent:
            return "/" + self.display
        return "/" + "/".join(self.parent + [self.display])

    def __repr__(self):
        return "<DirEntry %s attr=%02X cluster=%d size=%d>" % (
            self.path, self.attr, self.cluster, self.size)


# ---------------------------------------------------------------------------
# journal
# ---------------------------------------------------------------------------

class Journal(object):
    """写前把受影响的 512 B 扇区原字节落盘；失败时按偏移恢复。"""

    def __init__(self, image):
        self.image = image
        self.sectors = {}          # absolute offset -> old 512 bytes
        self.notes = []
        self.path = None

    def add_range(self, offset, length):
        if length <= 0:
            return
        if offset < 0 or offset + length > self.image.size:
            raise ImgError("journal 范围越界：offset=%d len=%d" % (offset, length))
        start = offset - (offset % SECTOR)
        end = offset + length
        pos = start
        while pos < end:
            if pos not in self.sectors:
                self.sectors[pos] = self.image.read_at(pos, SECTOR)
            pos += SECTOR

    def add_note(self, text):
        self.notes.append(text)

    def save(self):
        if self.path:
            return self.path
        jdir = self.image.journal_dir()
        if not os.path.isdir(jdir):
            os.makedirs(jdir)
        payload_parts = []
        entries = []
        cursor = 0
        for off in sorted(self.sectors):
            data = self.sectors[off]
            payload_parts.append(data)
            entries.append({
                "offset": off,
                "length": len(data),
                "data_offset": cursor,
                "sha256": sha256_bytes(data),
            })
            cursor += len(data)
        payload = b"".join(payload_parts)
        header = {
            "version": JOURNAL_VERSION,
            "created": time.strftime("%Y-%m-%d %H:%M:%S"),
            "image": self.image.path,
            "image_size": self.image.size,
            "identity": self.image.identity,
            "notes": self.notes,
            "entries": entries,
        }
        hbytes = json.dumps(header, ensure_ascii=False, sort_keys=True).encode("utf-8")
        stamp = time.strftime("%Y%m%d-%H%M%S")
        name = "journal-%s-%d.bin" % (stamp, os.getpid())
        path = os.path.join(jdir, name)
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(JOURNAL_MAGIC)
            f.write(struct.pack("<I", len(hbytes)))
            f.write(hbytes)
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        self.path = path
        return path

    def rollback(self, image=None, force=False):
        img = image or self.image
        header, payload = load_journal(self.path)
        if not force:
            if header.get("image_size") != img.size:
                raise VerifyError("journal 与镜像大小不符，拒绝回滚（可用备份恢复）")
            if header.get("identity") != img.identity:
                raise VerifyError(
                    "镜像在 journal 之后被改动过（身份校验不符），拒绝回滚；"
                    "请用 2 GB 备份恢复"
                )
        for ent in header.get("entries", []):
            off = int(ent["offset"])
            length = int(ent["length"])
            data = payload[int(ent["data_offset"]):int(ent["data_offset"]) + length]
            if len(data) != length:
                raise VerifyError("journal 载荷损坏：offset=%d" % off)
            img.write_at(off, data)
        img.flush()
        return len(header.get("entries", []))

    def prune(self):
        jdir = self.image.journal_dir()
        try:
            files = [
                os.path.join(jdir, n) for n in os.listdir(jdir)
                if n.startswith("journal-") and n.endswith(".bin")
            ]
            files.sort(key=lambda p: os.path.getmtime(p), reverse=True)
            for p in files[JOURNAL_KEEP:]:
                os.remove(p)
        except Exception:
            pass


def load_journal(path):
    with open(path, "rb") as f:
        raw = f.read()
    if not raw.startswith(JOURNAL_MAGIC):
        raise ImgError("不是 imgfs journal：%s" % path)
    pos = len(JOURNAL_MAGIC)
    (hlen,) = struct.unpack_from("<I", raw, pos)
    pos += 4
    header = json.loads(raw[pos:pos + hlen].decode("utf-8"))
    payload = raw[pos + hlen:]
    if header.get("version") != JOURNAL_VERSION:
        raise ImgError("journal 版本不支持：%r" % header.get("version"))
    return header, payload


# ---------------------------------------------------------------------------
# 写计划：先收集所有写操作与旧扇区，再一次性应用
# ---------------------------------------------------------------------------

class WritePlan(object):
    def __init__(self, image):
        self.image = image
        self.journal = Journal(image)
        self.ops = []              # [(offset, bytes)]

    def write(self, offset, data):
        if not data:
            return
        self.ops.append((offset, bytes(data)))
        self.journal.add_range(offset, len(data))

    def save_journal(self):
        return self.journal.save()

    def apply(self):
        for off, data in self.ops:
            self.image.write_at(off, data)
        self.image.flush()

    def rollback(self):
        return self.journal.rollback(self.image)


# ---------------------------------------------------------------------------
# 镜像
# ---------------------------------------------------------------------------

class Image(object):
    def __init__(self, path, protect=None):
        self.path = os.path.abspath(path)
        if not os.path.isfile(self.path):
            raise ImgError("找不到镜像：%s" % self.path)
        self.protect = protect or ProtectPolicy()
        # DOSBox 进程只用于提示：真正的门是下面的独占打开。
        # DOSBox 打开的是“别的镜像”（例如测试克隆）时，不应阻止我们操作目标镜像。
        self.dosbox = dosbox_running()
        self.size = os.path.getsize(self.path)
        try:
            self.f = exclusive_open(self.path)
        except GuardError as ex:
            if self.dosbox:
                raise GuardError("%s（检测到 %s 正在运行；如果它打开的就是这个镜像，请先退出 DOSBox）"
                                 % (ex, self.dosbox))
            raise
        self.fat_mismatch = False
        self._parse_mbr()
        self._parse_bpb()
        self._load_fats()
        self._read_root()
        self.identity = self._identity()

    # -- 基础 IO ----------------------------------------------------------
    def read_at(self, offset, length):
        if offset < 0 or offset + length > self.size:
            raise ImgError("读越界：offset=%d len=%d size=%d" % (offset, length, self.size))
        self.f.seek(offset)
        data = self.f.read(length)
        if len(data) != length:
            raise ImgError("短读：offset=%d 期望=%d 实际=%d" % (offset, length, len(data)))
        return data

    def write_at(self, offset, data):
        if offset < 0 or offset + len(data) > self.size:
            raise ImgError("写越界：offset=%d len=%d size=%d" % (offset, len(data), self.size))
        self.f.seek(offset)
        n = self.f.write(data)
        if n != len(data):
            raise ImgError("短写：offset=%d 期望=%d 实际=%d" % (offset, len(data), n or 0))

    def flush(self):
        try:
            self.f.flush()
            os.fsync(self.f.fileno())
        except Exception:
            pass

    def close(self):
        if self.f:
            try:
                self.f.close()
            except Exception:
                pass
            self.f = None

    # -- 几何 ------------------------------------------------------------
    def _parse_mbr(self):
        mbr = self.read_at(0, 512)
        self.part_offset = 0
        self.part_sectors = self.size // SECTOR
        if mbr[510:512] == b"\x55\xAA":
            parts = []
            for i in range(4):
                e = mbr[0x1BE + i * 16:0x1BE + (i + 1) * 16]
                ptype = e[4]
                lba = u32(e, 8)
                nsec = u32(e, 12)
                if ptype and nsec:
                    parts.append((ptype, lba, nsec))
            fat16 = [p for p in parts if p[0] in (0x06, 0x0E)]
            if fat16:
                ptype, lba, nsec = fat16[0]
                self.part_type = ptype
                self.part_offset = lba * SECTOR
                self.part_sectors = nsec
                return
            if parts:
                raise ImgError(
                    "分区表里没有 FAT16（发现类型 %s）；本工具只支持 FAT16" %
                    ", ".join("0x%02X" % p[0] for p in parts)
                )
            # 没有分区表：按 superfloppy 处理，后面用 offset 0 的 BPB 判断
        self.part_type = 0x06

    def _parse_bpb(self):
        bs = self.read_at(self.part_offset, 512)
        self.oem = bs[3:11].decode("ascii", "replace").strip("\x00 ")
        self.bps = u16(bs, 0x0B)
        self.spc = bs[0x0D]
        self.rsvd = u16(bs, 0x0E)
        self.nfats = bs[0x10]
        self.rootent = u16(bs, 0x11)
        tot16 = u16(bs, 0x13)
        self.media = bs[0x15]
        fsz16 = u16(bs, 0x16)
        tot32 = u32(bs, 0x20)
        fsz32 = u32(bs, 0x24)
        self.fsz = fsz16 or fsz32
        self.total_sectors = tot32 or tot16
        fstype = bs[0x36:0x3E].decode("ascii", "replace").strip("\x00 ")

        if self.bps != SECTOR or self.spc == 0 or self.rsvd == 0 or self.nfats == 0:
            raise ImgError("BPB 异常：bps=%d spc=%d rsvd=%d nfats=%d" %
                           (self.bps, self.spc, self.rsvd, self.nfats))
        if self.fsz == 0 or self.total_sectors == 0:
            raise ImgError("BPB 异常：fsz=%d total=%d" % (self.fsz, self.total_sectors))
        if self.spc & (self.spc - 1):
            raise ImgError("扇区/簇不是 2 的幂：%d" % self.spc)
        if self.part_offset + self.total_sectors * SECTOR > self.size:
            raise ImgError(
                "分区超出镜像末尾：partition=%d+%d sectors, image=%d B" %
                (self.part_offset // SECTOR, self.total_sectors, self.size)
            )
        self.cluster_bytes = self.spc * SECTOR
        self.root_sec = self.rsvd + self.nfats * self.fsz
        self.data_sec = self.root_sec + self.rootent * 32 // self.bps
        if self.data_sec >= self.total_sectors:
            raise ImgError("数据区起点超出分区：data_sec=%d total=%d" %
                           (self.data_sec, self.total_sectors))
        self.nclusters = (self.total_sectors - self.data_sec) // self.spc
        if self.nclusters <= 4085:
            raise ImgError("簇数 %d 落在 FAT12 范围；本工具只支持 FAT16" % self.nclusters)
        if self.nclusters >= 65525:
            raise ImgError("簇数 %d 超出 FAT16 上限" % self.nclusters)
        if fsz16 == 0:
            raise ImgError("FAT 大小字段为 32 位，疑似 FAT32；本工具只支持 FAT16")
        self.fstype = fstype or "FAT16"
        self.root_off = self.part_offset + self.root_sec * SECTOR
        self.data_off = self.part_offset + self.data_sec * SECTOR
        self.fat_off = [
            self.part_offset + self.rsvd * SECTOR + i * self.fsz * SECTOR
            for i in range(self.nfats)
        ]

    def _load_fats(self):
        self.fat0 = bytearray(self.read_at(self.fat_off[0], self.fsz * SECTOR))
        self.fat1 = bytearray(
            self.read_at(self.fat_off[1], self.fsz * SECTOR)
        ) if self.nfats > 1 else bytearray(self.fat0)
        self.fat_mismatch = (bytes(self.fat0) != bytes(self.fat1))
        count = self.fsz * SECTOR // 2
        self.fat = [0] * max(count, self.nclusters + 2)
        for i in range(1, count):
            self.fat[i] = u16(self.fat0, i * 2)

    def _read_root(self):
        data = self.read_at(self.root_off, self.rootent * 32)
        self.root_entries = self._parse_dir(data, [(0, self.root_off, len(data))], [])

    def reload(self):
        self._load_fats()
        self._read_root()

    def _identity(self):
        head_len = min(1024 * 1024, self.size)
        tail_len = min(1024 * 1024, self.size)
        return {
            "size": self.size,
            "mbr_sha256": sha256_bytes(self.read_at(0, 512)),
            "bpb_sha256": sha256_bytes(self.read_at(self.part_offset, 512)),
            "head_sha256": sha256_bytes(self.read_at(0, head_len)),
            "tail_sha256": sha256_bytes(self.read_at(self.size - tail_len, tail_len)),
        }

    def fat_sha(self):
        return sha256_bytes(bytes(self.fat0) + bytes(self.fat1))

    def journal_dir(self):
        base = os.path.splitext(os.path.basename(self.path))[0]
        return os.path.join(os.path.dirname(self.path), JOURNAL_DIRNAME, base)

    # -- 簇 --------------------------------------------------------------
    def cluster_offset(self, c):
        return self.data_off + (c - 2) * self.cluster_bytes

    def chain(self, start):
        out = []
        seen = set()
        c = start
        while True:
            if c in (0, 1):
                break
            if c >= CLUSTER_END:
                break
            if c > self.nclusters + 1:
                raise ImgError("簇号越界：%d（有效 2..%d）" % (c, self.nclusters + 1))
            if c in seen:
                raise ImgError("簇链有环：cluster=%d" % c)
            seen.add(c)
            out.append(c)
            if len(out) > self.nclusters + 2:
                raise ImgError("簇链过长")
            nxt = self.fat[c]
            if nxt == BAD_CLUSTER:
                raise ImgError("簇链遇到坏簇标记 0xFFF7（cluster=%d）" % c)
            if 0xFFF0 <= nxt <= 0xFFF6:
                raise ImgError("簇链遇到保留值 0x%04X（cluster=%d）" % (nxt, c))
            if nxt == 0:
                raise ImgError("簇链在中途指向 0（cluster=%d 未正确结束）" % c)
            c = nxt
        return out

    def read_chain(self, chain):
        parts = []
        for c in chain:
            parts.append(self.read_at(self.cluster_offset(c), self.cluster_bytes))
        return b"".join(parts)

    def read_file(self, entry):
        if entry.size == 0:
            return b""
        chain = self.chain(entry.cluster)
        cap = len(chain) * self.cluster_bytes
        if entry.size > cap:
            raise ImgError(
                "文件大小 %d 超过链容量 %d（簇链损坏？）：%s" %
                (entry.size, cap, entry.path)
            )
        return self.read_chain(chain)[:entry.size]

    # -- 目录 ------------------------------------------------------------
    def _parse_dir(self, data, segments, parent):
        out = []
        lfn_parts = []
        for i in range(0, len(data) - 31, 32):
            e = data[i:i + 32]
            first = e[0]
            if first == 0x00:
                break
            if first == 0xE5:
                lfn_parts = []
                continue
            attr = e[11]
            if attr == 0x0F:
                seq = first & 0x1F
                text = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16le", "replace")
                lfn_parts.append((seq, text))
                continue
            if attr & 0x08:            # 卷标
                lfn_parts = []
                continue
            name = e[0:8].decode("cp437", "replace").rstrip()
            ext = e[8:11].decode("cp437", "replace").rstrip()
            if first == 0x05:
                name = "\u00e5" + name[1:]
            if name in (".", ".."):
                lfn_parts = []
                continue
            ntres = e[12]
            if ntres & 0x08:
                name = name.lower()
            if ntres & 0x10:
                ext = ext.lower()
            name83 = name + ("." + ext if ext else "")
            lfn = ""
            if lfn_parts:
                lfn = "".join(t for _, t in sorted(lfn_parts)).split("\x00")[0]
                lfn = lfn.replace("\uffff", "")
            lfn_parts = []
            abs_off = self._abs_of(i, segments)
            out.append(DirEntry(
                name83=name83, lfn=lfn, attr=attr,
                cluster=u16(e, 26), size=u32(e, 28),
                entry_offset=abs_off, parent=list(parent),
                wtime=u16(e, 22), wdate=u16(e, 24),
            ))
        return out

    @staticmethod
    def _abs_of(data_offset, segments):
        for start, abs_start, length in segments:
            if start <= data_offset < start + length:
                return abs_start + (data_offset - start)
        raise ImgError("目录数据偏移无法映射：%d" % data_offset)

    def list_dir(self, entry):
        if entry is None:
            return self.root_entries
        if not entry.is_dir:
            return []
        chain = self.chain(entry.cluster)
        if not chain:
            return []
        segs = []
        pos = 0
        for c in chain:
            segs.append((pos, self.cluster_offset(c), self.cluster_bytes))
            pos += self.cluster_bytes
        data = b"".join(
            self.read_at(self.cluster_offset(c), self.cluster_bytes) for c in chain
        )
        parent = entry.parent + [entry.display]
        return self._parse_dir(data, segs, parent)

    @staticmethod
    def split_path(path):
        p = path.replace("\\", "/")
        m = re.match(r"^([A-Za-z]):", p)
        if m:
            p = p[2:]
        if "*" in p or "?" in p:
            raise UsageError("路径不支持通配符：%s" % path)
        comps = []
        for part in p.split("/"):
            if part in ("", "."):
                continue
            if part == "..":
                if comps:
                    comps.pop()
                continue
            comps.append(part)
        return comps

    def _find(self, entries, comp):
        cands = [comp]
        stripped = comp.rstrip(" .")
        if stripped != comp:
            cands.append(stripped)
        for cand in cands:
            up = cand.upper()
            for e in entries:
                if e.name83.upper() == up:
                    return e
                if e.lfn and e.lfn.casefold() == cand.casefold():
                    return e
        return None

    def resolve(self, path):
        comps = self.split_path(path)
        entries = self.root_entries
        chain = []
        for comp in comps:
            found = self._find(entries, comp)
            if found is None:
                return chain, None
            chain.append(found)
            entries = self.list_dir(found) if found.is_dir else []
        return chain, (chain[-1] if chain else None)

    # -- 保护策略 --------------------------------------------------------
    def protected_reason(self, parent_comps, final_name, target=None):
        for e in parent_comps:
            if e.attr & self.protect.attr_mask:
                return "路径含 SYSTEM/卷标属性项：%s" % e.path
        if target is not None and (target.attr & self.protect.attr_mask):
            return "目标带 SYSTEM/卷标属性：%s" % target.path
        if parent_comps:
            first = parent_comps[0]
            names = {first.name83.upper(), (first.lfn or "").upper()}
            if names & self.protect.dirs:
                return "受保护目录：%s" % first.path
        if not parent_comps and final_name:
            up = final_name.strip().upper()
            if up in self.protect.root_files:
                return "受保护根文件：%s" % final_name
        return None

    # -- 读取命令 --------------------------------------------------------
    def info(self):
        free = sum(1 for c in range(2, self.nclusters + 2) if self.fat[c] == 0)
        used = self.nclusters - free
        backup_dir = os.path.join(os.path.dirname(self.path), "backup")
        backups = []
        if os.path.isdir(backup_dir):
            backups = sorted(
                [n for n in os.listdir(backup_dir)
                 if n.lower().endswith(".img") and not n.endswith(".tmp")],
                reverse=True,
            )
        return {
            "image": self.path,
            "size": self.size,
            "partition": {
                "offset": self.part_offset,
                "type": "0x%02X" % self.part_type,
                "sectors": self.part_sectors,
            },
            "fat": {
                "type": self.fstype,
                "bytes_per_sector": self.bps,
                "sectors_per_cluster": self.spc,
                "cluster_bytes": self.cluster_bytes,
                "num_fats": self.nfats,
                "root_entries": self.rootent,
                "clusters": self.nclusters,
                "fat_sectors": self.fsz,
            },
            "fat_copies_identical": not self.fat_mismatch,
            "fat_sha256": self.fat_sha(),
            "free_clusters": free,
            "used_clusters": used,
            "free_bytes": free * self.cluster_bytes,
            "dosbox": (self.dosbox or "not running"),
            "exclusive_lock": "acquired",
            "backups": backups[:5],
            "protected_preset": self.protect.preset,
            "protected_dirs": sorted(self.protect.dirs),
            "protected_root_files": sorted(self.protect.root_files),
        }

    def check(self):
        self.reload()
        problems = []
        used = {}
        dirs_seen = set()

        def mark(chain, who):
            for c in chain:
                used[c] = used.get(c, 0) + 1
                if used[c] > 1:
                    problems.append("交叉链：cluster %d 同时属于 %s" % (c, who))

        def walk(entries, path):
            for e in entries:
                ep = path + "/" + e.display if path else "/" + e.display
                if e.is_dir:
                    if e.cluster == 0:
                        continue
                    if e.cluster in dirs_seen:
                        problems.append("目录簇重复：%s cluster=%d" % (ep, e.cluster))
                        continue
                    dirs_seen.add(e.cluster)
                    try:
                        ch = self.chain(e.cluster)
                    except ImgError as ex:
                        problems.append("目录 %s：%s" % (ep, ex))
                        continue
                    mark(ch, "目录 " + ep)
                    try:
                        walk(self.list_dir(e), ep)
                    except ImgError as ex:
                        problems.append("目录 %s：%s" % (ep, ex))
                else:
                    if e.size == 0 and e.cluster == 0:
                        continue
                    try:
                        ch = self.chain(e.cluster)
                    except ImgError as ex:
                        problems.append("文件 %s：%s" % (ep, ex))
                        continue
                    mark(ch, "文件 " + ep)
                    cap = len(ch) * self.cluster_bytes
                    if e.size > cap:
                        problems.append(
                            "文件 %s 大小 %d 超过链容量 %d" % (ep, e.size, cap)
                        )

        walk(self.root_entries, "")
        lost = 0
        bad = []
        for c in range(2, self.nclusters + 2):
            v = self.fat[c]
            if v == 0:
                continue
            if v == BAD_CLUSTER:
                bad.append(c)
                continue
            if 0xFFF0 <= v <= 0xFFF6:
                bad.append(c)
                continue
            if v >= CLUSTER_END:
                if c not in used:
                    lost += 1
                continue
            if c not in used:
                lost += 1
        if self.fat_mismatch:
            problems.append("FAT0 与 FAT1 不一致")
        if bad:
            problems.append("FAT 中存在坏簇/保留值项 %d 个（例：%s）" %
                            (len(bad), ", ".join(str(x) for x in bad[:8])))
        free = sum(1 for c in range(2, self.nclusters + 2) if self.fat[c] == 0)
        return {
            "clusters": self.nclusters,
            "cluster_bytes": self.cluster_bytes,
            "free": free,
            "used": self.nclusters - free,
            "reachable": len(used),
            "lost": lost,
            "fat_copies_identical": not self.fat_mismatch,
            "dirs": len(dirs_seen),
            "problems": problems,
        }

    # -- 写：目录项/FAT --------------------------------------------------
    def _dirent_bytes(self, entry, size=None, start=None, keep_time=False):
        raw = bytearray(self.read_at(entry.entry_offset, 32))
        if start is not None:
            struct.pack_into("<H", raw, 26, start & 0xFFFF)
            struct.pack_into("<H", raw, 20, 0)
        if size is not None:
            struct.pack_into("<I", raw, 28, size & 0xFFFFFFFF)
        if not keep_time:
            tm, dt = dos_datetime()
            struct.pack_into("<H", raw, 22, tm)
            struct.pack_into("<H", raw, 24, dt)
        return bytes(raw)

    def _plan_fat_values(self, plan, updates):
        """updates: {cluster: newvalue}，同时写两份 FAT。"""
        if not updates:
            return
        f0 = bytearray(self.fat0)
        f1 = bytearray(self.fat1)
        sectors = set()
        for c, val in updates.items():
            struct.pack_into("<H", f0, c * 2, val & 0xFFFF)
            struct.pack_into("<H", f1, c * 2, val & 0xFFFF)
            self.fat[c] = val
            sectors.add(c * 2 // SECTOR)
        for s in sorted(sectors):
            start = s * SECTOR
            plan.write(self.fat_off[0] + start, f0[start:start + SECTOR])
            if self.nfats > 1:
                plan.write(self.fat_off[1] + start, f1[start:start + SECTOR])
        self.fat0 = f0
        self.fat1 = f1

    def _alloc(self, plan, n):
        if n <= 0:
            return []
        free = [c for c in range(2, self.nclusters + 2) if self.fat[c] == 0]
        if len(free) < n:
            raise CapacityError(
                "镜像空闲簇不足：需要 %d，只有 %d" % (n, len(free))
            )
        chosen = free[:n]
        updates = {}
        for i, c in enumerate(chosen):
            updates[c] = chosen[i + 1] if i + 1 < len(chosen) else 0xFFFF
        self._plan_fat_values(plan, updates)
        return chosen

    def _free_chain(self, plan, chain):
        if not chain:
            return
        self._plan_fat_values(plan, {c: 0 for c in chain})

    def _find_free_dirent(self, parent_entry):
        """在父目录现有数据里找一个空 32 B 槽；返回绝对偏移。"""
        if parent_entry is None:
            data = self.read_at(self.root_off, self.rootent * 32)
            segs = [(0, self.root_off, len(data))]
        else:
            chain = self.chain(parent_entry.cluster)
            if not chain:
                raise CapacityError("父目录没有数据簇：%s" % parent_entry.path)
            data = b"".join(
                self.read_at(self.cluster_offset(c), self.cluster_bytes) for c in chain
            )
            segs = []
            pos = 0
            for c in chain:
                segs.append((pos, self.cluster_offset(c), self.cluster_bytes))
                pos += self.cluster_bytes
        for i in range(0, len(data) - 31, 32):
            if data[i] in (0x00, 0xE5):
                return self._abs_of(i, segs)
        raise CapacityError(
            "目标目录没有空闲目录项（M2 不扩目录）；请先删除一个文件或改用已有文件"
        )

    @staticmethod
    def _make_83(name):
        if name in ("", ".", ".."):
            raise UsageError("非法文件名：%r" % name)
        if name.count(".") > 1:
            raise UsageError("新文件名只支持 8.3（至多一个点）：%s" % name)
        if "." in name:
            base, ext = name.rsplit(".", 1)
        else:
            base, ext = name, ""
        if not (1 <= len(base) <= 8 and len(ext) <= 3):
            raise UsageError("新文件名必须是 8.3 格式（如 PROBE.SAV）：%s" % name)
        up = (base + ext).upper()
        for ch in up:
            if ch not in _83_CHARS:
                raise UsageError(
                    "新文件名含 8.3 不允许的字符 %r；请改用 A-Z 0-9 $%%'-_@~!(){}^#&：%s" %
                    (ch, name)
                )
        return base.upper().ljust(8), ext.upper().ljust(3)

    # -- 写：put ---------------------------------------------------------
    def put(self, local, imgpath, dry_run=False, keep_time=False):
        comps_all = self.split_path(imgpath)
        if not comps_all:
            raise UsageError("不能把文件写到根目录本身")
        comps, entry = self.resolve(imgpath)
        if entry is not None:
            if len(comps) != len(comps_all):
                raise ImgError("路径解析结果不一致：%s" % imgpath)
            parent_comps = comps[:-1]
            final_name = entry.display
        else:
            if len(comps) != len(comps_all) - 1:
                raise ImgError("父目录不存在：%s" % "/".join(comps_all[:-1]))
            parent_comps = comps
            final_name = comps_all[-1]

        reason = self.protected_reason(parent_comps, final_name, entry)
        if reason:
            raise ProtectedError("%s 是受保护路径（只读）：%s" % (imgpath, reason))

        if entry is not None and entry.is_dir:
            raise ImgError("目标是目录，不能用 put：%s" % imgpath)

        data = read_local_file(local)
        src_sha = sha256_bytes(data)
        if entry is None:
            return self._put_create(imgpath, data, src_sha, dry_run, keep_time)

        chain = self.chain(entry.cluster) if entry.cluster else []
        cap = len(chain) * self.cluster_bytes
        if len(data) <= cap:
            return self._put_inplace(entry, chain, data, src_sha,
                                     dry_run, keep_time, imgpath)
        return self._put_realloc(entry, chain, data, src_sha,
                                 dry_run, keep_time, imgpath)

    def _put_inplace(self, entry, chain, data, src_sha, dry_run, keep_time, imgpath):
        plan = WritePlan(self)
        cs = self.cluster_bytes
        for i, c in enumerate(chain):
            chunk = data[i * cs:(i + 1) * cs]
            if not chunk:
                break
            plan.write(self.cluster_offset(c), chunk)
        plan.write(entry.entry_offset,
                   self._dirent_bytes(entry, size=len(data), keep_time=keep_time))
        if dry_run:
            self._print_plan("inplace", imgpath, entry.size, len(data), plan)
            return {"mode": "inplace", "image_path": imgpath, "size": len(data),
                    "sha256": src_sha, "dry_run": True, "changed_sectors": len(plan.journal.sectors)}

        fat_before = self.fat_sha()
        journal = plan.save_journal()
        try:
            plan.apply()
            fat_after = self.fat_sha()
            if fat_after != fat_before:
                raise VerifyError("原位写入后 FAT 发生变化（不应发生）")
            self.reload()
            new_entry = self._resolve_file(imgpath)
            got = self.read_file(new_entry)
            if sha256_bytes(got) != src_sha:
                raise VerifyError("回读哈希与源文件不一致：%s" % imgpath)
            report = self.check()
            if report["problems"]:
                raise VerifyError("写入后 check 失败：%s" % "; ".join(report["problems"][:3]))
        except Exception:
            try:
                plan.rollback()
            except Exception as ex2:
                raise VerifyError(
                    "写入失败且回滚也失败：%s；请用备份恢复（journal: %s）" % (ex2, journal)
                )
            raise
        plan.journal.prune()
        print("put %s: %d -> %d B  mode=inplace  changed_sectors=%d  journal=%s" %
              (imgpath, entry.size, len(data), len(plan.journal.sectors), journal))
        print("sha256=%s" % src_sha)
        return {"mode": "inplace", "image_path": imgpath, "size": len(data),
                "sha256": src_sha, "journal": journal,
                "changed_sectors": len(plan.journal.sectors)}

    def _put_realloc(self, entry, old_chain, data, src_sha, dry_run, keep_time, imgpath):
        cs = self.cluster_bytes
        need = (len(data) + cs - 1) // cs
        plan = WritePlan(self)
        new_chain = self._alloc(plan, need)
        for i, c in enumerate(new_chain):
            chunk = data[i * cs:(i + 1) * cs]
            if chunk:
                plan.write(self.cluster_offset(c), chunk)
        new_start = new_chain[0] if new_chain else 0
        plan.write(entry.entry_offset,
                   self._dirent_bytes(entry, size=len(data), start=new_start,
                                      keep_time=keep_time))
        self._free_chain(plan, old_chain)
        if dry_run:
            self._print_plan("realloc", imgpath, entry.size, len(data), plan)
            return {"mode": "realloc", "image_path": imgpath, "size": len(data),
                    "sha256": src_sha, "dry_run": True,
                    "changed_sectors": len(plan.journal.sectors)}

        journal = plan.save_journal()
        try:
            plan.apply()
            self.reload()
            new_entry = self._resolve_file(imgpath)
            got = self.read_file(new_entry)
            if sha256_bytes(got) != src_sha:
                raise VerifyError("回读哈希与源文件不一致：%s" % imgpath)
            report = self.check()
            if report["problems"]:
                raise VerifyError("写入后 check 失败：%s" % "; ".join(report["problems"][:3]))
        except Exception:
            try:
                plan.rollback()
            except Exception as ex2:
                raise VerifyError(
                    "写入失败且回滚也失败：%s；请用备份恢复（journal: %s）" % (ex2, journal)
                )
            raise
        plan.journal.prune()
        print("put %s: %d -> %d B  mode=realloc  changed_sectors=%d  journal=%s" %
              (imgpath, entry.size, len(data), len(plan.journal.sectors), journal))
        print("sha256=%s" % src_sha)
        return {"mode": "realloc", "image_path": imgpath, "size": len(data),
                "sha256": src_sha, "journal": journal,
                "changed_sectors": len(plan.journal.sectors)}

    def _put_create(self, imgpath, data, src_sha, dry_run, keep_time):
        comps_all = self.split_path(imgpath)
        name = comps_all[-1]
        n83, e83 = self._make_83(name)
        parent_comps, parent_entry = self.resolve("/" + "/".join(comps_all[:-1])) \
            if len(comps_all) > 1 else ([], None)
        if len(parent_comps) != len(comps_all) - 1:
            raise ImgError("父目录不存在：%s" % "/".join(comps_all[:-1]))
        reason = self.protected_reason(parent_comps, name, None)
        if reason:
            raise ProtectedError("%s 是受保护路径（只读）：%s" % (imgpath, reason))
        slot = self._find_free_dirent(parent_entry)
        cs = self.cluster_bytes
        need = (len(data) + cs - 1) // cs
        plan = WritePlan(self)
        new_chain = self._alloc(plan, need)
        for i, c in enumerate(new_chain):
            chunk = data[i * cs:(i + 1) * cs]
            if chunk:
                plan.write(self.cluster_offset(c), chunk)
        tm, dt = dos_datetime()
        raw = bytearray(32)
        raw[0:8] = n83.encode("ascii")
        raw[8:11] = e83.encode("ascii")
        raw[11] = 0x20
        raw[12] = 0
        struct.pack_into("<H", raw, 14, tm)      # 创建时间
        struct.pack_into("<H", raw, 16, dt)      # 创建日期
        struct.pack_into("<H", raw, 18, dt)      # 访问日期
        struct.pack_into("<H", raw, 20, 0)
        struct.pack_into("<H", raw, 22, tm)
        struct.pack_into("<H", raw, 24, dt)
        struct.pack_into("<H", raw, 26, new_chain[0] if new_chain else 0)
        struct.pack_into("<I", raw, 28, len(data))
        plan.write(slot, bytes(raw))
        if dry_run:
            self._print_plan("create", imgpath, 0, len(data), plan)
            return {"mode": "create", "image_path": imgpath, "size": len(data),
                    "sha256": src_sha, "dry_run": True,
                    "changed_sectors": len(plan.journal.sectors)}

        journal = plan.save_journal()
        try:
            plan.apply()
            self.reload()
            new_entry = self._resolve_file(imgpath)
            got = self.read_file(new_entry)
            if sha256_bytes(got) != src_sha:
                raise VerifyError("回读哈希与源文件不一致：%s" % imgpath)
            report = self.check()
            if report["problems"]:
                raise VerifyError("写入后 check 失败：%s" % "; ".join(report["problems"][:3]))
        except Exception:
            try:
                plan.rollback()
            except Exception as ex2:
                raise VerifyError(
                    "写入失败且回滚也失败：%s；请用备份恢复（journal: %s）" % (ex2, journal)
                )
            raise
        plan.journal.prune()
        print("put %s: 0 -> %d B  mode=create  changed_sectors=%d  journal=%s" %
              (imgpath, len(data), len(plan.journal.sectors), journal))
        print("sha256=%s" % src_sha)
        return {"mode": "create", "image_path": imgpath, "size": len(data),
                "sha256": src_sha, "journal": journal,
                "changed_sectors": len(plan.journal.sectors)}

    def _resolve_file(self, imgpath):
        comps_all = self.split_path(imgpath)
        comps, entry = self.resolve(imgpath)
        if entry is None or len(comps) != len(comps_all) or entry.is_dir:
            raise VerifyError("写入后无法解析目标：%s" % imgpath)
        return entry

    def _print_plan(self, mode, imgpath, old_size, new_size, plan):
        print("dry-run: %s %s  %d -> %d B  mode=%s  sectors=%d" %
              ("put", imgpath, old_size, new_size, mode, len(plan.journal.sectors)))
        for off, data in plan.ops[:12]:
            print("  write offset=%d len=%d" % (off, len(data)))
        if len(plan.ops) > 12:
            print("  ... 共 %d 个写操作" % len(plan.ops))

    # -- 写：rm ----------------------------------------------------------
    def remove(self, imgpath):
        comps_all = self.split_path(imgpath)
        comps, entry = self.resolve(imgpath)
        if entry is None or len(comps) != len(comps_all):
            raise ImgError("文件不存在：%s" % imgpath)
        if entry.is_dir:
            raise ImgError("rm 只支持文件，不支持目录：%s" % imgpath)
        parent_comps = comps[:-1]
        reason = self.protected_reason(parent_comps, entry.display, entry)
        if reason:
            raise ProtectedError("%s 是受保护路径（只读）：%s" % (imgpath, reason))
        chain = self.chain(entry.cluster) if entry.cluster else []
        plan = WritePlan(self)
        self._free_chain(plan, chain)
        raw = bytearray(self.read_at(entry.entry_offset, 32))
        raw[0] = 0xE5
        plan.write(entry.entry_offset, bytes(raw))
        journal = plan.save_journal()
        try:
            plan.apply()
            self.reload()
            _c, e2 = self.resolve(imgpath)
            if e2 is not None:
                raise VerifyError("删除后仍能解析到目标：%s" % imgpath)
            report = self.check()
            if report["problems"]:
                raise VerifyError("删除后 check 失败：%s" % "; ".join(report["problems"][:3]))
        except Exception:
            try:
                plan.rollback()
            except Exception as ex2:
                raise VerifyError("删除失败且回滚也失败：%s；journal=%s" % (ex2, journal))
            raise
        plan.journal.prune()
        print("rm %s: freed %d clusters  journal=%s" %
              (imgpath, len(chain), journal))
        return {"image_path": imgpath, "freed_clusters": len(chain), "journal": journal}

    # -- 读取 ------------------------------------------------------------
    def get(self, imgpath, local):
        comps_all = self.split_path(imgpath)
        comps, entry = self.resolve(imgpath)
        if entry is None or len(comps) != len(comps_all):
            raise ImgError("文件不存在：%s" % imgpath)
        if entry.is_dir:
            raise ImgError("目标是目录，不能用 get：%s" % imgpath)
        data = self.read_file(entry)
        d = os.path.dirname(os.path.abspath(local))
        if d and not os.path.isdir(d):
            os.makedirs(d)
        tmp = local + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, local)
        digest = sha256_bytes(data)
        print("get %s -> %s  %d B  sha256=%s" %
              (imgpath, local, len(data), digest))
        return {"image_path": imgpath, "local": os.path.abspath(local),
                "size": len(data), "sha256": digest}

    def diff(self, imgpath, local):
        comps_all = self.split_path(imgpath)
        comps, entry = self.resolve(imgpath)
        if entry is None or len(comps) != len(comps_all) or entry.is_dir:
            print("missing %s" % imgpath)
            return 2
        data = self.read_file(entry)
        local_data = read_local_file(local)
        if sha256_bytes(data) == sha256_bytes(local_data):
            print("same %s (%d B)" % (imgpath, len(data)))
            return 0
        print("different %s: image=%d B local=%d B" %
              (imgpath, len(data), len(local_data)))
        return 1

    # -- 备份 ------------------------------------------------------------
    def backup(self, dest):
        dest = os.path.abspath(dest)
        if os.path.exists(dest):
            raise ImgError("备份目标已存在：%s" % dest)
        d = os.path.dirname(dest)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        usage = shutil.disk_usage(d or ".")
        if usage.free < self.size + 64 * 1024 * 1024:
            raise CapacityError("磁盘空间不足：需要 %s，剩余 %s" %
                                (format_bytes(self.size), format_bytes(usage.free)))
        tmp = dest + ".tmp"
        h = hashlib.sha256()
        self.f.seek(0)
        with open(tmp, "wb") as out:
            while True:
                chunk = self.f.read(8 * 1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
                h.update(chunk)
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, dest)
        h2 = hashlib.sha256()
        with open(dest, "rb") as f:
            while True:
                chunk = f.read(8 * 1024 * 1024)
                if not chunk:
                    break
                h2.update(chunk)
        if h.hexdigest() != h2.hexdigest():
            raise VerifyError("备份校验失败：%s" % dest)
        print("backup %s -> %s  %d B  sha256=%s" %
              (self.path, dest, self.size, h2.hexdigest()))
        return {"source": self.path, "dest": dest, "size": self.size,
                "sha256": h2.hexdigest()}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    p = argparse.ArgumentParser(
        description="离线读写 FAT16 磁盘镜像（DOSBox/Win9x/DOS；可选保护 preset）"
    )
    p.add_argument("--image", required=True,
                   help="镜像路径（必填）")
    p.add_argument("--protect-preset", default="none",
                   help="保护 preset（none/win9x，或 presets/<name>.json；默认 none）")
    p.add_argument("--protect-dir", action="append", default=[],
                   help="额外受保护根级目录名（可重复）")
    p.add_argument("--protect-file", action="append", default=[],
                   help="额外受保护根级文件名（可重复）")
    p.add_argument("--no-protect", action="store_true",
                   help="不使用 preset（仍保留 --protect-dir/--protect-file）")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("info", help="镜像几何/空闲/FAT/黑名单")
    sp.add_argument("--json", action="store_true")

    sp = sub.add_parser("ls", help="列目录")
    sp.add_argument("path")
    sp.add_argument("--long", action="store_true")
    sp.add_argument("--json", action="store_true")

    sp = sub.add_parser("get", help="从镜像取文件")
    sp.add_argument("imgpath")
    sp.add_argument("local")

    sp = sub.add_parser("put", help="写入/替换文件（原位优先）")
    sp.add_argument("local")
    sp.add_argument("imgpath")
    sp.add_argument("--dry-run", action="store_true")
    sp.add_argument("--keep-time", action="store_true")

    sp = sub.add_parser("rm", help="删除文件（M2）")
    sp.add_argument("imgpath")
    sp.add_argument("--yes", action="store_true")

    sp = sub.add_parser("diff", help="比较镜像文件与本地文件")
    sp.add_argument("imgpath")
    sp.add_argument("local")

    sp = sub.add_parser("check", help="全树 FAT 一致性检查")
    sp.add_argument("--json", action="store_true")
    sp.add_argument("--strict", action="store_true")

    sp = sub.add_parser("backup", help="整镜像备份 + SHA-256 校验")
    sp.add_argument("dest")

    sp = sub.add_parser("rollback", help="按 journal 回滚")
    sp.add_argument("journal")

    args = p.parse_args(argv)
    img = None
    try:
        img = Image(args.image, protect=load_protect(args))
        if args.cmd == "info":
            info = img.info()
            if args.json:
                print(json.dumps(info, ensure_ascii=False, indent=2))
            else:
                print("image      : %s" % info["image"])
                print("size       : %d B (%s)" % (info["size"], format_bytes(info["size"])))
                print("partition  : type=%s lba=%d sectors=%d offset=%d" % (
                    info["partition"]["type"],
                    info["partition"]["offset"] // SECTOR,
                    info["partition"]["sectors"],
                    info["partition"]["offset"]))
                print("fat        : %s  %d B/sector  %d sectors/cluster (%d B)  %d FATs" % (
                    info["fat"]["type"], info["fat"]["bytes_per_sector"],
                    info["fat"]["sectors_per_cluster"], info["fat"]["cluster_bytes"],
                    info["fat"]["num_fats"]))
                print("clusters   : %d  (root entries %d)" % (
                    info["fat"]["clusters"], info["fat"]["root_entries"]))
                print("fat copies : %s  sha256=%s" % (
                    "identical" if info["fat_copies_identical"] else "MISMATCH",
                    info["fat_sha256"]))
                print("free       : %d clusters (%s)" % (
                    info["free_clusters"], format_bytes(info["free_bytes"])))
                print("used       : %d clusters" % info["used_clusters"])
                print("dosbox     : %s" % info["dosbox"])
                print("backups    : %s" % (", ".join(info["backups"]) or "(none)"))
                print("protected  : preset=%s dirs=%s" % (
                    info.get("protected_preset"), ", ".join(info["protected_dirs"])))
                print("             files=%s" % ", ".join(info["protected_root_files"]))
            return 0

        if args.cmd == "ls":
            comps, entry = img.resolve(args.path)
            if entry is None and len(comps) != len(img.split_path(args.path)):
                raise ImgError("路径不存在：%s" % args.path)
            if entry is not None and not entry.is_dir:
                entries = [entry]
            else:
                entries = img.list_dir(entry)
            rows = []
            for e in entries:
                try:
                    chain_len = len(img.chain(e.cluster)) if (e.is_dir or e.size) and e.cluster else 0
                except ImgError:
                    chain_len = -1
                rows.append({
                    "name": e.display,
                    "short": e.name83,
                    "attr": "0x%02X" % e.attr,
                    "dir": e.is_dir,
                    "size": e.size,
                    "cluster": e.cluster,
                    "clusters": chain_len,
                    "capacity": chain_len * img.cluster_bytes if chain_len >= 0 else -1,
                    "time": format_dos_datetime(e.wtime, e.wdate),
                })
            if args.json:
                print(json.dumps(rows, ensure_ascii=False, indent=2))
            else:
                for r in rows:
                    print("%s %-46s %-14s %8d  clus=%-6d n=%-4d cap=%-8d %s" % (
                        "d" if r["dir"] else "-", r["name"], r["short"], r["size"],
                        r["cluster"], r["clusters"], r["capacity"], r["time"]))
            return 0

        if args.cmd == "get":
            img.get(args.imgpath, args.local)
            return 0

        if args.cmd == "put":
            img.put(args.local, args.imgpath,
                    dry_run=args.dry_run, keep_time=args.keep_time)
            return 0

        if args.cmd == "rm":
            if not args.yes:
                raise UsageError("rm 需要 --yes（破坏性操作）")
            img.remove(args.imgpath)
            return 0

        if args.cmd == "diff":
            return img.diff(args.imgpath, args.local)

        if args.cmd == "check":
            report = img.check()
            if args.json:
                print(json.dumps(report, ensure_ascii=False, indent=2))
            else:
                print("clusters  : %d x %d B" % (report["clusters"], report["cluster_bytes"]))
                print("free/used : %d / %d" % (report["free"], report["used"]))
                print("reachable : %d  dirs=%d" % (report["reachable"], report["dirs"]))
                print("fat copies: %s" % ("identical" if report["fat_copies_identical"] else "MISMATCH"))
                print("lost      : %d" % report["lost"])
                print("problems  : %d" % len(report["problems"]))
                for pr in report["problems"][:20]:
                    print("  - %s" % pr)
            if report["problems"]:
                return 4
            if args.strict and report["lost"]:
                return 4
            return 0

        if args.cmd == "backup":
            img.backup(args.dest)
            return 0

        if args.cmd == "rollback":
            header, _payload = load_journal(args.journal)
            if header.get("image") and os.path.abspath(header["image"]) != img.path:
                raise VerifyError(
                    "journal 属于另一个镜像：%s（当前 %s）" %
                    (header["image"], img.path))
            j = Journal(img)
            j.path = args.journal
            n = j.rollback(img)
            print("rollback %s: restored %d sectors" % (args.journal, n))
            report = img.check()
            print("check: problems=%d lost=%d" % (len(report["problems"]), report["lost"]))
            if report["problems"]:
                return 4
            return 0

        raise UsageError("未知命令：%s" % args.cmd)
    except ImgError as ex:
        print("[错误] %s" % ex, file=sys.stderr)
        return ex.code
    except KeyboardInterrupt:
        print("[中断]", file=sys.stderr)
        return 130
    except Exception as ex:
        print("[内部错误] %s: %s" % (type(ex).__name__, ex), file=sys.stderr)
        return 1
    finally:
        if img is not None:
            img.close()


if __name__ == "__main__":
    sys.exit(main())
