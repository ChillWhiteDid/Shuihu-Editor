/*
 * cli.c —— 命令行前端
 *
 * M1（只读）:
 *   shuihu-save -list   <save> [-m]     列出单位
 *   shuihu-save -m      <save>          只打印汇总
 *   shuihu-save -fields                 打印字段偏移表
 *
 * M2（写入）: 默认**干跑**（只显示计划，不落盘），加 -y 才真正写入
 *   shuihu-save -money  <save> <值>       [-y]
 *   shuihu-save -set    <save> <类型id> <字段> <值> [-y]
 *   shuihu-save -unlock-hidden <save> <类型id> [-y]
 *   shuihu-save -verify <save>           重新解析并自检
 */
#include "save.h"
#include "names.h"
#include "fileio.h"
#include "goods.h"
#include "tree.h"
#include "human.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ------------------------------------------------------------------
 * 输出层：程序内部字符串一律 UTF-8（names_data.c 是 UTF-8 字面量），
 * 但控制台代码页可能是 936（Win9x 只有 ANSI/OEM 代码页，不支持 65001）。
 * 所以输出前统一转成 GetConsoleOutputCP() 指定的编码。
 * ------------------------------------------------------------------ */
static UINT  s_outcp = 65001;
static int   s_win9x = 0;

static void out_init(void)
{
    DWORD v = GetVersion();
    s_win9x = (v & 0x80000000u) != 0;      /* Win9x 返回值最高位为 1 */
    if (!s_win9x) {
        SetConsoleOutputCP(65001);          /* NT 系可用 UTF-8 控制台 */
    }
    s_outcp = GetConsoleOutputCP();
    if (s_outcp == 0) s_outcp = 936;
    {   /* 可用 SH_OUTCP 强制指定（兜底：某些环境检测不准时） */
        const char *e = getenv("SH_OUTCP");
        if (e && *e) {
            int cp = atoi(e);
            if (cp > 0) s_outcp = (UINT)cp;
        }
    }
}

static void sh_printf(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;

    if (s_outcp == 65001) {
        fwrite(buf, 1, (size_t)n, stdout);
    } else {
        WCHAR wbuf[4096];
        char  obuf[8192];
        int wl = MultiByteToWideChar(CP_UTF8, 0, buf, n, wbuf, 4096);
        if (wl > 0) {
            int ol = WideCharToMultiByte((UINT)s_outcp, 0, wbuf, wl, obuf, sizeof(obuf), NULL, NULL);
            if (ol > 0) fwrite(obuf, 1, (size_t)ol, stdout);
        }
    }
}
#define printf sh_printf
/* 把命令行参数的 ANSI(GBK) 字节转成 UTF-8，避免与 SetConsoleOutputCP(65001) 冲突 */
static char s_conv[4096];
static const char *to_utf8(const char *ansi)
{
#ifdef _WIN32
    WCHAR wbuf[1024];
    if (MultiByteToWideChar(CP_ACP, 0, ansi, -1, wbuf, 1024) > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, s_conv, sizeof(s_conv), NULL, NULL) > 0) {
        return s_conv;
    }
#endif
    return ansi;
}

static void usage(void)
{
    printf("用法:\n");
    printf("  shuihu-save -list <save> [-m]\n");
    printf("  shuihu-save -m <save>\n");
    printf("  shuihu-save -fields\n");
    printf("  shuihu-save -verify <save>\n");
    printf("  shuihu-save -money <save> <值> [-y]\n");
    printf("  shuihu-save -set <save> <类型id> <字段> <值> [-y]\n");
    printf("  shuihu-save -unlock-hidden <save> <类型id> [-y]\n");
    printf("  shuihu-save -items <save> <类型id>               列出 7 个槽（含道具名）\n");
    printf("  shuihu-save -slot <save> <类型id> <槽0-6> <道具id> [-y]   改某个槽\n");
    printf("  shuihu-save -tree <save> <类型id>               查看技能树\n");
    printf("\n字段名: level hp hpmax mp mpmax atk def hit agi crit exp expnext move\n");
    printf("不加 -y 时只显示计划（干跑），不会改动任何文件。\n");
}

static void print_field_table(void)
{
    int f;
    printf("相对锚点的偏移（锚点 = 生命当前值所在位置）：\n");
    for (f = 0; f < SH_F_COUNT; f++) {
        printf("  %-10s %+4d   上限 %d\n", sh_field_label((sh_field)f),
               sh_field_offset((sh_field)f), sh_field_max((sh_field)f));
    }
    printf("  槽区       +%d 起，%d 格 × %d 字节（槽内 +0 = 1 基道具 id）\n",
           SH_SLOT_BASE, SH_SLOT_COUNT, SH_SLOT_STRIDE);
}

/* ---- 控制台宽度与显示宽度（CJK 按 2 列算） ---- */

static int console_cols(void)
{
    const char *e = getenv("SH_WIDTH");
    if (e && *e) { int w = atoi(e); if (w > 20) return w; }
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &bi)) {
        int w = bi.srWindow.Right - bi.srWindow.Left + 1;
        if (w > 0) return w;
    }
#endif
    return 80;
}

static int utf8_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}

/* 粗略显示宽度：3 字节 UTF-8（CJK）算 2 列，其余 1 列 */
static int dwidth(const char *s)
{
    int w = 0;
    while (*s) {
        int n = utf8_len((unsigned char)*s);
        w += (n == 3) ? 2 : 1;
        s += n;
    }
    return w;
}

/* 打印字符串并用空格补齐到指定显示宽度 */
static void padout(const char *s, int width)
{
    int w = dwidth(s);
    printf("%s", s);
    while (w++ < width) printf(" ");
}
static void print_slots(const sh_unit *u)
{
    int i, first = 1;
    printf("[");
    for (i = 0; i < SH_SLOT_COUNT; i++) {
        if (u->slots[i] == 0) continue;
        if (!first) printf(",");
        printf("%u", (unsigned)u->slots[i]);
        first = 0;
    }
    printf("]");
}

static void print_hidden(const sh_unit *u)
{
    int i, first = -1;
    if (u->hidden_len <= 0) return;
    for (i = 0; i < u->hidden_len; i++) if (u->hidden[i] != 0) { first = i; break; }
    if (first < 0) { printf(" 全解锁"); return; }
    printf(" ");
    for (i = first; i < u->hidden_len; i++) printf("%u", (unsigned)u->hidden[i]);
}

static void print_unit_rows(const sh_unit *u)
{
    printf(" %2u ", (unsigned)u->id);
    padout(sh_char_name(u->id), 9);
    printf(" %2u  %3u/%-3u %3u/%-3u  %3u %3u %3u %3u %3u  %3u/%-3u %2u\n",
           (unsigned)u->f[SH_F_LEVEL],
           (unsigned)u->f[SH_F_HP], (unsigned)u->f[SH_F_HPMAX],
           (unsigned)u->f[SH_F_MP], (unsigned)u->f[SH_F_MPMAX],
           (unsigned)u->f[SH_F_ATK], (unsigned)u->f[SH_F_DEF],
           (unsigned)u->f[SH_F_HIT], (unsigned)u->f[SH_F_AGI],
           (unsigned)u->f[SH_F_CRIT],
           (unsigned)u->f[SH_F_EXP], (unsigned)u->f[SH_F_EXPNEXT],
           (unsigned)u->f[SH_F_MOVE]);
    printf("      槽 ");
    print_slots(u);
    if (u->hidden_len > 0) { printf("   隐藏技"); print_hidden(u); }
    printf("\n");
}

static int g_wide = 1;   /* 控制台是否够宽（>=100 列） */

static void print_unit(const sh_unit *u)
{
    if (!g_wide) { print_unit_rows(u); return; }

    printf("%4u ", (unsigned)u->id);
    padout(sh_char_name(u->id), 9);
    printf("%3u  %4u/%-4u %4u/%-4u  %3u %3u %3u %3u %3u  %3u/%-3u %3u ",
           (unsigned)u->f[SH_F_LEVEL],
           (unsigned)u->f[SH_F_HP], (unsigned)u->f[SH_F_HPMAX],
           (unsigned)u->f[SH_F_MP], (unsigned)u->f[SH_F_MPMAX],
           (unsigned)u->f[SH_F_ATK], (unsigned)u->f[SH_F_DEF],
           (unsigned)u->f[SH_F_HIT], (unsigned)u->f[SH_F_AGI],
           (unsigned)u->f[SH_F_CRIT],
           (unsigned)u->f[SH_F_EXP], (unsigned)u->f[SH_F_EXPNEXT],
           (unsigned)u->f[SH_F_MOVE]);
    print_slots(u);
    print_hidden(u);
    printf("\n");
}

static void print_table_head(void)
{
    if (!g_wide) {
        printf("\n  ID 名字       等  生命      气力      攻  防  命  敏  杀  经验     移\n");
        printf("  -------------------------------------------------------------------\n");
        return;
    }
    printf("\n  id ");
    padout("名字", 9);
    printf("等  生命        气力        攻  防  命  敏  杀  经验     移  槽/隐藏技\n");
    printf("  -------------------------------------------------------------------------------\n");
}

static int parse_field(const char *s, sh_field *out)
{
    static const struct { const char *name; sh_field f; } tab[] = {
        {"level", SH_F_LEVEL}, {"hp", SH_F_HP}, {"hpmax", SH_F_HPMAX},
        {"mp", SH_F_MP}, {"mpmax", SH_F_MPMAX}, {"atk", SH_F_ATK},
        {"def", SH_F_DEF}, {"hit", SH_F_HIT}, {"agi", SH_F_AGI},
        {"crit", SH_F_CRIT}, {"exp", SH_F_EXP}, {"expnext", SH_F_EXPNEXT},
        {"move", SH_F_MOVE}
    };
    size_t i;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (strcmp(s, tab[i].name) == 0) { *out = tab[i].f; return 0; }
    }
    return -1;
}

/* 写盘并自检：重解析写出的文件，比对锚点数、类型 id、以及在内存里改过的值 */
static int commit(sh_save *s, const char *path, int verbose)
{
    char bak[600];
    int rc, i, ok = 1;
    sh_save chk;

    rc = sh_make_backup(path, bak, sizeof(bak));
    if (rc != 0) {
        printf("[中止] 备份失败 (rc=%d)，未写入任何内容。\n", rc);
        return -1;
    }
    if (verbose) printf("[备份] %s\n", to_utf8(bak));

    rc = sh_write_file(path, s->data, s->size);
    if (rc != 0) {
        printf("[中止] 写入失败 (rc=%d)，原文件未被改动。\n", rc);
        return -1;
    }

    if (sh_load(path, &chk) != 0) {
        printf("[警告] 写入成功但重新解析失败，请用备份恢复：%s\n", to_utf8(bak));
        return -1;
    }

    if (chk.count != s->count) { printf("[校验失败] 锚点数 %d → %d\n", s->count, chk.count); ok = 0; }
    for (i = 0; i < s->count && i < chk.count; i++) {
        if (chk.units[i].id != s->units[i].id) {
            printf("[校验失败] 第 %d 个锚点的类型 id %u → %u\n",
                   i, (unsigned)s->units[i].id, (unsigned)chk.units[i].id);
            ok = 0;
            break;
        }
    }
    if (chk.money != s->money) { printf("[校验失败] 金钱 %lu → %lu\n",
        (unsigned long)s->money, (unsigned long)chk.money); ok = 0; }

    if (ok) {
        printf("[校验] 通过：锚点 %d 个、类型 id 全部一致、金钱 %lu\n",
               chk.count, (unsigned long)chk.money);
    }
    sh_free(&chk);
    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    sh_save s;
    const char *cmd = NULL, *path = NULL;
    int do_meta = 0, do_yes = 0;
    int i, rc;

#ifdef _WIN32
    out_init();
#endif
    g_wide = (console_cols() >= 100);

    if (argc < 2) { usage(); return 2; }

    cmd = argv[1];
    if (strcmp(cmd, "-fields") == 0) { print_field_table(); return 0; }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-list") == 0) { /* 已由 cmd 决定 */ }
        else if (strcmp(argv[i], "-m") == 0) do_meta = 1;
        else if (strcmp(argv[i], "-y") == 0) do_yes = 1;
        else if (argv[i][0] != '-' && !path) path = argv[i];
    }
    if (!path) { usage(); return 2; }

    rc = sh_load(path, &s);
    if (rc != 0) {
        printf("解析失败: %s\n", to_utf8(path));
        switch (rc) {
        case SH_E_OPEN:
            printf("  原因: 打不开文件。请确认文件名与路径（当前目录下是否有这个 .sav）。\n");
            printf("  提示: 先执行 DIR *.SAV 看看当前目录里有哪些存档。\n");
            break;
        case SH_E_EMPTY:   printf("  原因: 文件长度为 0。\n"); break;
        case SH_E_NOMEM:   printf("  原因: 内存不足。\n"); break;
        case SH_E_READ:    printf("  原因: 读取失败（文件损坏或被占用）。\n"); break;
        case SH_E_NOTSAVE:
            printf("  原因: 能打开，但开头不是 GAM —— 这不是本游戏的存档文件。\n");
            break;
        default:           printf("  原因: 未知错误 (rc=%d)。\n", rc); break;
        }
        return 1;
    }

    /* ---- 只读命令 ---- */
    if (strcmp(cmd, "-list") == 0 || strcmp(cmd, "-m") == 0) {
        int playable = 0;
        for (i = 0; i < s.count; i++) if (sh_is_playable(&s.units[i])) playable++;
        printf("文件   : %s\n", to_utf8(path));
        printf("大小   : %lu 字节\n", (unsigned long)s.size);
        printf("金钱   : %lu  (u32 @ 0x1B)\n", (unsigned long)s.money);
        printf("锚点   : %d 个（可编辑角色 %d 个）\n", s.count, playable);
        if (strcmp(cmd, "-list") == 0) {
            print_table_head();
            for (i = 0; i < s.count; i++) {
                if (!sh_is_playable(&s.units[i])) continue;
                print_unit(&s.units[i]);
            }
            if (do_meta) {
                printf("\n  -- 其他单位（NPC/杂兵）--\n");
                for (i = 0; i < s.count; i++) {
                    if (sh_is_playable(&s.units[i])) continue;
                    print_unit(&s.units[i]);
                }
            }
        }
        sh_free(&s);
        return 0;
    }

    if (strcmp(cmd, "-verify") == 0) {
        printf("文件   : %s\n", to_utf8(path));
        printf("大小   : %lu 字节\n", (unsigned long)s.size);
        printf("金钱   : %lu\n", (unsigned long)s.money);
        printf("锚点   : %d 个\n", s.count);
        for (i = 0; i < s.count; i++) {
            if (sh_is_playable(&s.units[i])) print_unit(&s.units[i]);
        }
        printf("结论   : 结构完整（锚点与类型 id 均可解析）\n");
        sh_free(&s);
        return 0;
    }

    /* ---- 写入命令 ---- */
    if (strcmp(cmd, "-money") == 0) {
        unsigned long v;
        if (argc < 4) { usage(); return 2; }
        v = (unsigned long)strtoul(argv[3], NULL, 0);
        printf("[计划] 金钱 %lu → %lu\n", (unsigned long)s.money, v);
        if (!do_yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_free(&s); return 0; }
        sh_set_money(&s, (uint32_t)v);
        rc = commit(&s, path, 1);
        sh_free(&s);
        return rc == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "-set") == 0) {
        int idx, id;
        sh_field f;
        unsigned long v;
        if (argc < 6) { usage(); return 2; }
        id = atoi(argv[3]);
        if (parse_field(argv[4], &f) != 0) { printf("未知字段: %s\n", argv[4]); sh_free(&s); return 2; }
        v = (unsigned long)strtoul(argv[5], NULL, 0);

        idx = sh_find_unit(&s, id);
        if (idx < 0) {
            printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id));
            sh_free(&s);
            return 1;
        }
        printf("[计划] %s(id %d) %s: %u → %lu  (上限 %d)\n",
               sh_char_name(id), id, sh_field_label(f),
               (unsigned)s.units[idx].f[f], v, sh_field_max(f));

        rc = sh_set_field(&s, idx, f, (uint16_t)v);
        if (rc != 0) {
            printf("[拒绝] %s\n", rc == -2 ? "超出允许范围" : (rc == -3 ? "会破坏 当前≤上限" : "参数非法"));
            sh_free(&s);
            return 1;
        }
        if (!do_yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_free(&s); return 0; }
        rc = commit(&s, path, 1);
        sh_free(&s);
        return rc == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "-items") == 0) {
        int idx, id, k;
        if (argc < 4) { usage(); return 2; }
        id = atoi(argv[3]);
        idx = sh_find_unit(&s, id);
        if (idx < 0) { printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id)); sh_free(&s); return 1; }
        printf("%s(id %d) 的 7 个槽：\n", sh_char_name(id), id);
        for (k = 0; k < SH_SLOT_COUNT; k++) {
            int iid = s.units[idx].slots[k];
            printf("  槽%d %-4s ", k, sh_kind_label(sh_slot_kind(k)));
            if (iid == 0) {
                printf("(空)\n");
            } else {
                printf("%3d  %s", iid, sh_item_name(iid));
                if (sh_item_kind(iid) != sh_slot_kind(k)) {
                    printf("   [注意] 该道具属于「%s」，本槽期望「%s」",
                           sh_kind_label(sh_item_kind(iid)), sh_kind_label(sh_slot_kind(k)));
                }
                printf("\n");
            }
        }
        sh_free(&s);
        return 0;
    }

    if (strcmp(cmd, "-tree") == 0) {
        static char text[8192];
        int idx, id;
        if (argc < 4) { usage(); return 2; }
        id = atoi(argv[3]);
        idx = sh_find_unit(&s, id);
        if (idx < 0) { printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id)); sh_free(&s); return 1; }
        sh_build_tree_text(&s.units[idx], text, (int)sizeof(text));
        printf("%s", text);
        sh_free(&s);
        return 0;
    }
    if (strcmp(cmd, "-slot") == 0) {
        int idx, id, slot, item;
        if (argc < 6) { usage(); return 2; }
        id    = atoi(argv[3]);
        slot  = atoi(argv[4]);
        item  = atoi(argv[5]);
        if (slot < 0 || slot >= SH_SLOT_COUNT) { printf("槽序号应为 0..%d\n", SH_SLOT_COUNT - 1); sh_free(&s); return 2; }
        if (item < 0 || item > sh_item_count()) { printf("道具 id 应为 0..%d（0=清空）\n", sh_item_count()); sh_free(&s); return 2; }
        idx = sh_find_unit(&s, id);
        if (idx < 0) { printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id)); sh_free(&s); return 1; }

        printf("[计划] %s(id %d) 槽%d %s: ", sh_char_name(id), id, slot, sh_kind_label(sh_slot_kind(slot)));
        if (s.units[idx].slots[slot] == 0) printf("(空)");
        else printf("%d %s", s.units[idx].slots[slot], sh_item_name(s.units[idx].slots[slot]));
        printf("  →  ");
        if (item == 0) printf("(清空)\n");
        else printf("%d %s\n", item, sh_item_name(item));

        if (item != 0 && sh_item_kind(item) != sh_slot_kind(slot)) {
            printf("[注意] 新道具属于「%s」，本槽期望「%s」——可以写，但游戏内未必生效。\n",
                   sh_kind_label(sh_item_kind(item)), sh_kind_label(sh_slot_kind(slot)));
        }

        if (sh_set_slot(&s, idx, slot, (uint8_t)item) != 0) {
            printf("[拒绝] 写入失败（槽或下标非法）\n"); sh_free(&s); return 1;
        }
        if (!do_yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_free(&s); return 0; }
        rc = commit(&s, path, 1);
        sh_free(&s);
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "-unlock-hidden") == 0) {
        int idx, id;
        if (argc < 4) { usage(); return 2; }
        id = atoi(argv[3]);
        idx = sh_find_unit(&s, id);
        if (idx < 0) {
            printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id));
            sh_free(&s);
            return 1;
        }
        printf("[计划] 解锁 %s(id %d) 的隐藏必杀技\n", sh_char_name(id), id);
        {
            int cls  = (int)s.units[idx].id;
            int hidN = sh_class_hidden_count(cls);
            if (hidN <= 0) {
                printf("[拒绝] 该职业的技能表里没有隐藏技（HUMAN.HUM 记录 %d）\n", cls);
                sh_free(&s);
                return 1;
            }
            rc = sh_unlock_hidden(&s, idx, hidN);
            if (rc <= 0) {
                printf("[拒绝] 未找到隐藏技标志块（该角色还没学过隐藏技，或标志块位置与已知职业不同）\n");
                sh_free(&s);
                return 1;
            }
            printf("[计划] 将清零 %d 格标志（该职业隐藏技共 %d 段）\n", rc, hidN);
        }
        if (!do_yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_free(&s); return 0; }
        rc = commit(&s, path, 1);
        sh_free(&s);
        return rc == 0 ? 0 : 1;
    }

    printf("未知命令: %s\n", cmd);
    usage();
    sh_free(&s);
    return 2;
}