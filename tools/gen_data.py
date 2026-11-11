# -*- coding: utf-8 -*-
r"""
从游戏数据提取名字/道具/技能表，生成三个 C 源文件到 src/。

用法:
    完整数据（需要自己的游戏副本）:
        python tools/gen_data.py <ShuiHu.org 路径> <输出 src 目录>
        需要同目录下的 Data\GOODS.DAB 与 Data\HUMAN.HUM

    空表（用于在没有游戏时也能编译）:
        python tools/gen_data.py --stub <输出 src 目录>
        所有表为 0 条，程序照常运行，名字显示为 "?"。

生成内容:
    src/names_data.c
        SH_CHAR_NAMES[]   角色名（按类型 id，索引 0 = 武龍）
        SH_SKILL_NAMES[]  技能名（武功记录 +0 的字符串 id）
    src/items_data.c
        SH_ITEM_NAMES[]   道具名（200 条）
        SH_ITEM_EQ[]      分类码
        SH_ITEM_MAJOR[]   大类
SH_ITEM_CACHE[][4] 装备缓存四元组：+76 攻击 / +82 / +84 防御 / +90
    src/human_data.c
        SH_CLASS_OFF[]    72 个职业技能表的分段偏移
        SH_SK_SID[]       技能字符串 id
        SH_SK_LEVEL[]     学习等级(1..30)
        SH_SK_HIDDEN[]    是否隐藏技（1 = 需要「古武術祕笈」）

文字均为 Big5 解码后以 UTF-8 存进 C 字符串字面量；
编译器按字节原样编译，运行时再由 textconv.c 转成显示编码。
"""
import sys
import os
import struct

IMAGE_BASE = 0x400000
TBL_CHAR  = 0x443A5C   # 角色名表（按类型 id）
TBL_SKILL = 0x443BC4   # 技能名表（按字符串 id）
MAX_ENTRIES = 256


def read_cstr(data, va):
    """把 VA 处的 NUL 结尾字符串读出来（Big5 解码）。"""
    off = va - IMAGE_BASE
    if off < 0 or off >= len(data):
        return None
    end = data.find(b"\x00", off)
    if end < 0:
        end = len(data)
    try:
        return data[off:end].decode("big5")
    except UnicodeDecodeError:
        return data[off:end].decode("big5", errors="replace")


def read_table(data, table_va, limit=MAX_ENTRIES, monotonic=True):
    """读指针数组，返回 [名字]。

    终止条件（任一）：
      - 指针越界 / 指向非 .rdata
      - 指针开始增大 —— 这两张表的字符串是按地址从高到低紧凑排列的（同义项可共用指针，故允许相等），
        越过表尾后会读到相邻数组的指针，用递减性可以精确定位真正的表尾。
    """
    base = table_va - IMAGE_BASE
    out = []
    prev = None
    for i in range(limit):
        p = base + 4 * i
        if p + 4 > len(data):
            break
        va = int.from_bytes(data[p:p + 4], "little")
        if va < IMAGE_BASE or va >= IMAGE_BASE + 0x100000:
            break
        if monotonic and prev is not None and va > prev:
            break
        s = read_cstr(data, va)
        if s is None:
            break
        out.append(s)
        prev = va
    return out


def c_literal(s):
    out = []
    for ch in s:
        if ch in ('"', "\\"):
            out.append("\\" + ch)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def emit_table(fh, name, items, comment):
    fh.write("/* %s */\n" % comment)
    fh.write("const int %s_count = %d;\n" % (name, len(items)))
    fh.write("const char *%s[] = {\n" % name)
    for i, s in enumerate(items):
        fh.write("    %s,   /* %d */\n" % (c_literal(s), i))
    fh.write("};\n\n")



# ---------------------------------------------------------------------------
# GOODS.DAB —— 道具表（200 条 × 198 字节，11 字节头）
#   记录 +0 = 装备类型（0..11），+1 = 大类（0/1/2/4/5），+6 = 名字（char[20], Big5）
# 在构建期就把 Big5 解成 UTF-8，避免 Win98 上缺 950 代码页的问题。
# ---------------------------------------------------------------------------
GOODS_HEADER = 11
GOODS_STRIDE = 198
GOODS_NAME_OFF = 6
GOODS_NAME_LEN = 20


def emit_items(goods_path, src_dir):
    data = open(goods_path, "rb").read()
    if len(data) < GOODS_HEADER + GOODS_STRIDE:
        raise SystemExit("GOODS.DAB 太小")
    count = (len(data) - GOODS_HEADER) // GOODS_STRIDE
    names, eq, major = [], [], []
    for i in range(count):
        off = GOODS_HEADER + i * GOODS_STRIDE
        rec = data[off:off + GOODS_STRIDE]
        raw = rec[GOODS_NAME_OFF:GOODS_NAME_OFF + GOODS_NAME_LEN].split(b"\x00")[0]
        try:
            nm = raw.decode("big5")
        except UnicodeDecodeError:
            nm = raw.decode("big5", errors="replace")
        names.append(nm)
        eq.append(rec[0])
        major.append(rec[1])

    out_path = os.path.join(src_dir, "items_data.c")
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py 自动生成，请勿手工编辑。 */\n")
        fh.write("/* 来源: %s （%d 条，1 基 id：id-1 为下标） */\n\n"
                 % (os.path.basename(goods_path), count))
        fh.write("const int SH_ITEM_COUNT = %d;\n\n" % count)
        fh.write("const char *SH_ITEM_NAMES[] = {\n")
        for i, nm in enumerate(names):
            fh.write("    %s,   /* %d */\n" % (c_literal(nm), i + 1))
        fh.write("};\n\n")
        fh.write("const unsigned char SH_ITEM_EQ[] = {\n")
        for i, v in enumerate(eq):
            fh.write("    %d,%s" % (v, "" if (i + 1) % 20 else "\n"))
        fh.write("\n};\n\n")
        fh.write("const unsigned char SH_ITEM_MAJOR[] = {\n")
        for i, v in enumerate(major):
            fh.write("    %d,%s" % (v, "" if (i + 1) % 20 else "\n"))
        fh.write("\n};\n\n")
        # 移动加成（记录 +104，**有符号**：实测存在 -1）
        fh.write("const signed char SH_ITEM_MOVE[] = {\n")
        for i in range(count):
            off2 = GOODS_HEADER + i * GOODS_STRIDE
            mv = struct.unpack_from("<h", data, off2 + 104)[0]
            if mv < -128: mv = -128
            if mv > 127: mv = 127
            fh.write("    %d,%s" % (mv, "" if (i + 1) % 20 else "\n"))
        fh.write("\n};\n\n")
        # 装备缓存四元组：槽 +2/+4/+6/+8 分别从 GOODS +76/+82/+84/+90 复制
        fh.write("const unsigned short SH_ITEM_CACHE[][4] = {\n")
        for i in range(count):
            o2 = GOODS_HEADER + i * GOODS_STRIDE
            v = [struct.unpack_from("<H", data, o2 + k)[0] for k in (76, 82, 84, 90)]
            fh.write("    { %5d, %5d, %5d, %5d },   /* %d */\n" % (v[0], v[1], v[2], v[3], i + 1))
        fh.write("};\n")
        # 说明文本（记录 +26 起，Big5，NUL 结尾）—— 里面直接写着效果
        fh.write("\nconst char *SH_ITEM_DESC[] = {\n")
        for i in range(count):
            off2 = GOODS_HEADER + i * GOODS_STRIDE
            r2 = data[off2:off2 + GOODS_STRIDE]
            raw2 = r2[26:60].split(b"\x00")[0]
            try:
                dz = raw2.decode("big5")
            except UnicodeDecodeError:
                dz = raw2.decode("big5", errors="replace")
            fh.write("    %s,   /* %d */\n" % (c_literal(dz), i + 1))
        fh.write("};\n")
    print("items      :", count)
    return out_path


# ---------------------------------------------------------------------------
# HUMAN.HUM —— 职业技能表（72 条记录，每条 51 B 定长 + n × 58 B 子项）
#   子项 +0 = 技能字符串 id，+17 = 学习等级(1..30)，+5 = 是否隐藏技(1=隐藏)
# 隐藏技每职业恰好 3 段（LV1/LV2/LV3），与存档里 3 字节标志块一一对应。
# ---------------------------------------------------------------------------
HUMAN_HDR = 13
HUMAN_BASE = 51
HUMAN_SUB = 58


def emit_human(human_path, src_dir):
    data = open(human_path, "rb").read()
    if len(data) < HUMAN_HDR + HUMAN_BASE:
        raise SystemExit("HUMAN.HUM 太小")

    off = HUMAN_HDR
    recs = []            # [(n, [(sid, lv, hidden), ...])]
    while off + HUMAN_BASE <= len(data) and len(recs) < 200:
        n = int.from_bytes(data[off + 45:off + 47], "little")
        base = off + HUMAN_BASE
        if base + n * HUMAN_SUB > len(data):
            break
        subs = []
        for k in range(n):
            s = base + k * HUMAN_SUB
            sid = int.from_bytes(data[s:s + 2], "little")
            lv = data[s + 17]
            hid = 1 if data[s + 5] == 1 else 0
            subs.append((sid, lv, hid))
        recs.append(subs)
        off = base + n * HUMAN_SUB

    flat = []
    offs = [0]
    for subs in recs:
        flat.extend(subs)
        offs.append(len(flat))

    out = os.path.join(src_dir, "human_data.c")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py 自动生成，请勿手工编辑。 */\n")
        fh.write("/* 来源: %s （%d 个职业，%d 条技能） */\n\n"
                 % (os.path.basename(human_path), len(recs), len(flat)))
        fh.write("const int SH_CLASS_COUNT = %d;\n\n" % len(recs))
        fh.write("const unsigned short SH_CLASS_OFF[] = {\n")
        for i, v in enumerate(offs):
            fh.write("    %d,%s" % (v, "\n" if (i + 1) % 16 == 0 else ""))
        fh.write("\n};\n\n")
        fh.write("const unsigned short SH_SK_SID[] = {\n")
        for i, (sid, lv, hid) in enumerate(flat):
            fh.write("    %d,%s" % (sid, "\n" if (i + 1) % 16 == 0 else ""))
        fh.write("\n};\n\n")
        fh.write("const unsigned char SH_SK_LEVEL[] = {\n")
        for i, (sid, lv, hid) in enumerate(flat):
            fh.write("    %d,%s" % (lv, "\n" if (i + 1) % 16 == 0 else ""))
        fh.write("\n};\n\n")
        fh.write("const unsigned char SH_SK_HIDDEN[] = {\n")
        for i, (sid, lv, hid) in enumerate(flat):
            fh.write("    %d,%s" % (hid, "\n" if (i + 1) % 16 == 0 else ""))
        fh.write("\n};\n")
    # 每个职业的基础移动（记录 +9，单字节）
    moves = []
    off2 = HUMAN_HDR
    for subs in recs:
        moves.append(data[off2 + 9])
        off2 = off2 + HUMAN_BASE + len(subs) * HUMAN_SUB
    out2 = os.path.join(src_dir, "human_data.c")
    with open(out2, "a", encoding="utf-8", newline="\n") as fh:
        fh.write("\nconst unsigned char SH_CLASS_MOVE[] = {\n")
        for i, v in enumerate(moves):
            fh.write("    %d,%s" % (v, "" if (i + 1) % 20 else "\n"))
        fh.write("\n};\n")
    print("human      :", len(recs), "职业 /", len(flat), "条技能")
    return out

def resolve_game_exe(explicit):
    """确定 ShuiHu.org 的路径。

    优先用显式参数；否则读环境变量 SHUIHU_DATA（指向游戏根目录），
    再取其中的 ShuiHu.org。找不到返回 None。
    """
    if explicit and explicit not in ("--auto", ""):
        return explicit
    env = os.environ.get("SHUIHU_DATA")
    if env:
        p = os.path.join(env, "ShuiHu.org")
        if os.path.isfile(p):
            print("使用环境变量 SHUIHU_DATA:", env)
            return p
        print("SHUIHU_DATA 已设置但其中没有 ShuiHu.org:", env)

    # 回退：仓库默认布局 <repo>\..\ShuiHu\（与 tools/selftest.ps1 的探测一致）
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    guess = os.path.abspath(os.path.join(here, "..", "ShuiHu"))
    p = os.path.join(guess, "ShuiHu.org")
    if os.path.isfile(p):
        print("使用默认布局:", guess)
        return p
    return None

def write_stub(src_dir):
    """生成空表（--stub）：让仓库在没有游戏数据时也能编译。

    程序对越界 id 一律返回 "?" / 空串，所以空表不会崩，只是看不到名字。
    真正使用时仍应从自己的游戏副本生成完整数据。
    """
    os.makedirs(src_dir, exist_ok=True)

    with open(os.path.join(src_dir, "names_data.c"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py --stub 生成（空表）。 */\n")
        fh.write("/* 这是占位数据：程序可以编译运行，但所有名字显示为 ? 。 */\n")
        fh.write("/* 要得到真实名字，请用你自己的游戏副本运行: */\n")
        fh.write('/*   python tools/gen_data.py "<游戏目录>\\ShuiHu.org" src */\n\n')
        fh.write("const int SH_CHAR_NAMES_count = 0;\n")
        fh.write("const char *SH_CHAR_NAMES[] = { 0 };\n\n")
        fh.write("const int SH_SKILL_NAMES_count = 0;\n")
        fh.write("const char *SH_SKILL_NAMES[] = { 0 };\n")

    with open(os.path.join(src_dir, "items_data.c"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py --stub 生成（空表）。 */\n")
        fh.write("const int SH_ITEM_COUNT = 0;\n\n")
        fh.write("const char *SH_ITEM_NAMES[] = { 0 };\n")
        fh.write("const unsigned char SH_ITEM_EQ[] = { 0 };\n")
        fh.write("const unsigned char SH_ITEM_MAJOR[] = { 0 };\n")
        fh.write("const unsigned short SH_ITEM_CACHE[][4] = { { 0, 0, 0, 0 } };\n")

    with open(os.path.join(src_dir, "human_data.c"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py --stub 生成（空表）。 */\n")
        fh.write("const int SH_CLASS_COUNT = 0;\n\n")
        fh.write("const unsigned short SH_CLASS_OFF[] = { 0 };\n")
        fh.write("const unsigned short SH_SK_SID[] = { 0 };\n")
        fh.write("const unsigned char SH_SK_LEVEL[] = { 0 };\n")
        fh.write("const unsigned char SH_SK_HIDDEN[] = { 0 };\n")

    print("已生成空表（stub）到", src_dir)
    print("  注意: 道具表/技能表为空，程序可用但看不到名字。")
    return 0


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--stub":
        return write_stub(sys.argv[2])

    if len(sys.argv) >= 3 and sys.argv[1] == "--auto":
        src_dir = sys.argv[2]
        exe_path = resolve_game_exe(None)
        if not exe_path:
            print("[错误] 无法自动定位游戏目录。")
            print("  请设置环境变量 SHUIHU_DATA 指向游戏根目录，例：")
            print("      set SHUIHU_DATA=C:\\ShuiHu")
            print("  或直接给出路径：")
            print('      python tools\\gen_data.py "C:\\ShuiHu\\ShuiHu.org" src')
            return 2
    elif len(sys.argv) >= 3:
        exe_path, src_dir = sys.argv[1], sys.argv[2]
    else:
        print("usage:")
        print("    gen_data.py <ShuiHu.org> <src_dir>     从游戏提取完整数据")
        print("    gen_data.py --auto <src_dir>           用环境变量 SHUIHU_DATA 定位")
        print("    gen_data.py --stub <src_dir>           生成空表（仅用于能编译）")
        return 2

    # 允许直接给游戏目录：自动取其中的 ShuiHu.org
    if os.path.isdir(exe_path):
        cand = os.path.join(exe_path, "ShuiHu.org")
        if os.path.isfile(cand):
            print("参数是目录，自动使用其中的 ShuiHu.org")
            exe_path = cand

    if not os.path.isfile(exe_path):
        print("[错误] 找不到文件:", exe_path)
        print("  请确认指向游戏根目录下的 ShuiHu.org（原始未改动的 exe）。")
        print("  常见写法:")
        print('      python tools\\gen_data.py "C:\\ShuiHu\\ShuiHu.org" src')
        print("  只想先编过、不需要名字的，用: python tools\\gen_data.py --stub src")
        return 2

    with open(exe_path, "rb") as f:
        data = f.read()

    # 角色表：指针整体递减，可用单调性精确定位表尾（实测 40 条：武龍…教頭）
    chars = read_table(data, TBL_CHAR, limit=MAX_ENTRIES, monotonic=True)
    # 技能表：按职业分组、多个角色共用同名串，指针会重复/回跳，不能用单调性；
    # 改为固定上限 + 非法指针即停（HUMAN 里 +0 的最大值 227 < 256）
    skills = read_table(data, TBL_SKILL, limit=MAX_ENTRIES, monotonic=False)

    goods = os.path.join(os.path.dirname(exe_path), "Data", "GOODS.DAB")
    human = os.path.join(os.path.dirname(exe_path), "Data", "HUMAN.HUM")
    missing = [p for p in (goods, human) if not os.path.exists(p)]
    if missing:
        print("")
        print("[警告] 以下文件不存在，道具名 / 职业技能表无法提取：")
        for p in missing:
            print("        ", p)
        print("  这样生成的 exe 会缺道具名与技能树（不是完整版）。")
        print("  确实要这样，请加 --allow-partial。")
        print("  只想先编过、不需要名字，请改用 --stub。")
        if "--allow-partial" not in sys.argv:
            return 1

    os.makedirs(src_dir, exist_ok=True)
    out_path = os.path.join(src_dir, "names_data.c")
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("/* 由 tools/gen_data.py 自动生成，请勿手工编辑。 */\n")
        fh.write("/* 来源: %s */\n\n" % os.path.basename(exe_path))
        emit_table(fh, "SH_CHAR_NAMES", chars, "角色名表（索引 = 类型 id）")
        emit_table(fh, "SH_SKILL_NAMES", skills, "技能名表（索引 = 武功记录 +0 的字符串 id）")

    if os.path.exists(goods):
        emit_items(goods, src_dir)
    if os.path.exists(human):
        emit_human(human, src_dir)

    print("char names :", len(chars))
    print("skill names:", len(skills))
    print("written    :", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())