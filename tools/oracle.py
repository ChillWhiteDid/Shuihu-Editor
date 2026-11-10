# -*- coding: utf-8 -*-
"""
oracle.py —— src/save.c 的 Python 孪生实现（独立验证用）

本机暂无 C 编译器，先用它证明「锚点扫描 + 字段偏移 + 过滤条件」这套算法
在真实存档上能得到与游戏截图一致的数字；C 实现按同一规则编写，编译后同批回归。

用法:
    python tools/oracle.py <ShuiHu.org> <save.sav> [<save.sav> ...]
"""
import sys
import os

IMAGE_BASE = 0x400000
TBL_CHAR = 0x443A5C

# 与 save.c 完全一致的常量
HP_MIN, HP_MAX = 30, 600
ID_MAX = 40
LEVEL_MAX = 30
STAT_MAX = 255
MONEY_OFF = 0x1B
SLOT_BASE, SLOT_STRIDE, SLOT_COUNT = 102, 10, 7
SCAN_START = 0x100
SCAN_GAP = 0x60

FIELD_REL = [
    ("id", -28), ("level", -4),
    ("hp", 0), ("hpmax", 2), ("mp", 4), ("mpmax", 6),
    ("atk", 8), ("def", 10), ("hit", 12), ("agi", 14), ("crit", 16),
    ("exp", 54), ("expnext", 56), ("move", 74),
]

CHAR_NAMES = []


def load_char_names(exe_path):
    data = open(exe_path, "rb").read()
    base = TBL_CHAR - IMAGE_BASE
    prev = None
    for i in range(256):
        va = int.from_bytes(data[base + 4 * i: base + 4 * i + 4], "little")
        if va < IMAGE_BASE or va >= IMAGE_BASE + 0x100000:
            break
        if prev is not None and va > prev:
            break
        off = va - IMAGE_BASE
        end = data.find(b"\x00", off)
        CHAR_NAMES.append(data[off:end].decode("big5", "replace"))
        prev = va


def u16(b, o):
    if o + 2 > len(b):
        return 0
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return int.from_bytes(b[o:o + 4], "little")


def try_unit(b, o):
    """与 save.c: try_unit() 等价。返回 dict 或 None。"""
    n = len(b)
    if o < 28 or o + 8 > n:
        return None
    hp, hpmax, mp, mpmax = u16(b, o), u16(b, o + 2), u16(b, o + 4), u16(b, o + 6)
    if not (HP_MIN < hpmax < HP_MAX):
        return None
    if not (HP_MIN < mpmax < HP_MAX):
        return None
    if hp == 0 or hp > hpmax:
        return None
    if mp == 0 or mp > mpmax:
        return None
    if hpmax == mpmax:
        return None
    uid = u16(b, o - 28)
    if uid > ID_MAX:
        return None
    lv = u16(b, o - 4)
    if lv < 1 or lv > LEVEL_MAX:
        return None
    for k in range(5):
        if u16(b, o + 8 + 2 * k) > STAT_MAX:
            return None

    u = {"anchor": o}
    for name, rel in FIELD_REL:
        u[name] = u16(b, o + rel)
    u["slots"] = [b[o + SLOT_BASE + i * SLOT_STRIDE] if o + SLOT_BASE + i * SLOT_STRIDE < n else 0
                  for i in range(SLOT_COUNT)]
    u["hidden"] = None
    for p in range(o + 230, min(o + 270, n - 4) + 1):
        if b[p:p + 4] == b"\x3C\x00\x03\x00":
            s = p
            while s > o and (p - s) < 8 and b[s - 1] <= 1:
                s -= 1
            u["hidden"] = list(b[s:p])
            break
    return u


def parse(path):
    b = open(path, "rb").read()
    units = []
    o = SCAN_START
    last = None
    while o + 8 <= len(b):
        u = try_unit(b, o)
        if u is not None:
            if last is not None and (o - last) < SCAN_GAP:
                o += 1
                continue
            units.append(u)
            last = o
            o += 8
        else:
            o += 1
    return {"size": len(b), "money": u32(b, MONEY_OFF), "units": units}


def name_of(uid):
    return CHAR_NAMES[uid] if 0 <= uid < len(CHAR_NAMES) else "?"


def show(path):
    r = parse(path)
    playable = [u for u in r["units"] if u["id"] <= 17]
    print("=" * 100)
    print("文件 %s  (%d 字节)  金钱 %d  单位 %d 个（可编辑 %d）"
          % (os.path.basename(path), r["size"], r["money"], len(r["units"]), len(playable)))
    print("  id 名字      等  生命       气力       攻  防  命  敏  杀  经验     移  槽                      隐藏块")
    print("  " + "-" * 96)
    for u in playable:
        slots = ",".join(str(s) for s in u["slots"] if s)
        if u["hidden"] is None:
            hid = "-"
        else:
            s = "".join(str(x) for x in u["hidden"]).lstrip("0")
            hid = s if s else "已全解锁"
        print("  %2d %-8s %3d  %4d/%-4d %4d/%-4d %3d %3d %3d %3d %3d  %3d/%-3d %3d  %-22s %s"
              % (u["id"], name_of(u["id"]), u["level"],
                 u["hp"], u["hpmax"], u["mp"], u["mpmax"],
                 u["atk"], u["def"], u["hit"], u["agi"], u["crit"],
                 u["exp"], u["expnext"], u["move"], "[" + slots + "]", hid))
    others = [u for u in r["units"] if u["id"] > 17]
    if others:
        ids = {}
        for u in others:
            ids[u["id"]] = ids.get(u["id"], 0) + 1
        print("  其他单位: " + ", ".join("%s×%d" % (name_of(i), c) for i, c in sorted(ids.items())))
    return r


def main():
    if len(sys.argv) < 3:
        print("usage: oracle.py <ShuiHu.org> <save.sav> [...]")
        return 2
    load_char_names(sys.argv[1])
    print("角色名表: %d 条" % len(CHAR_NAMES))
    for p in sys.argv[2:]:
        if os.path.exists(p):
            show(p)
        else:
            print("跳过（不存在）:", p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
