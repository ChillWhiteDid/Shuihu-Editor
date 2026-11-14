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
#include "goodsfile.h"
#include "names.h"
#include "fileio.h"
#include "goods.h"
#include "tree.h"
#include "human.h"
#include "mapfile.h"

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
    printf("  shuihu-save -cache <save> <类型id> <槽0-6> <atk|def> <值> [-y]   改装备加成缓存\n");
    printf("  shuihu-save -tree <save> <类型id>               查看技能树\n");
    printf("\n-- 关卡数据（改游戏本体数据，对所有存档与新开局生效）--\n");
    printf("  shuihu-save -map <MAP>                          棋盘 MAP 汇总（尺寸/记录数/tileId 范围）\n");
    printf("  shuihu-save -map <MAP> <x> <y>                  看一个格（含附加层链）\n");
    printf("  shuihu-save -map-set <MAP> <x> <y> <tileId> [-y]  改格子的图块 id\n");
    printf("  shuihu-save -act <ACT>                          布阵表 ACT 全部记录\n");
    printf("  shuihu-save -act <ACT> <序号>                   看一条记录\n");
    printf("  shuihu-save -act-set <ACT> <序号> <字段> <值> [-y]\n");
    printf("    字段: type f2 f4 team x y fa level flag action f10 f12 f16 slot0..slot6\n");
    printf("\n-- 道具表（改游戏本体数据，不影响单个存档）--\n");
    printf("  shuihu-save -goods <GOODS.DAB>                  列出 200 件道具\n");
    printf("  shuihu-save -goods-set <GOODS.DAB> <id> <字段> <值> [-y]\n");
    printf("    字段: atk def agi crit move hpmax mpmax\n");
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
            printf("  -----------------------------------------------------------------------------\n");
        return;
    }
    printf("\n  id ");
    padout("名字", 9);
    printf("等  生命        气力        攻  防  命  敏  杀  经验     移  槽/隐藏技\n");
            printf("  -----------------------------------------------------------------------------\n");
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

/* 关卡数据：ACT 记录打印（-act / -act-set 共用） */
static const int s_slot_off[SH_AREC_SLOTN] = {
    SH_AREC_SLOT0,     SH_AREC_SLOT0 + 2,  SH_AREC_SLOT0 + 4,
    SH_AREC_SLOT0 + 6, SH_AREC_SLOT0 + 8,  SH_AREC_SLOT0 + 10,
    SH_AREC_SLOT0 + 12
};

static void act_print_record(const sh_act *a, int i)
{
    int k;
    printf("  [%d] 类型 %-4d(%-6s) 队别 %d  坐标 (%d,%d)  等级 %-2d  动作 %d  标志 %d\n",
           i,
           sh_act_get(a, i, SH_AREC_TYPE, 2),
           sh_char_name(sh_act_get(a, i, SH_AREC_TYPE, 2)),
           sh_act_get(a, i, SH_AREC_TEAM, 1),
           sh_act_get(a, i, SH_AREC_X, 2),
           sh_act_get(a, i, SH_AREC_Y, 2),
           sh_act_get(a, i, SH_AREC_LEVEL, 2),
           sh_act_get(a, i, SH_AREC_ACTION, 1),
           sh_act_get(a, i, SH_AREC_FLAG, 1));
    printf("        槽:");
    for (k = 0; k < SH_AREC_SLOTN; k++) {
        int id = sh_act_get(a, i, s_slot_off[k], 2);
        if (id > 0) printf(" %d:%s", id, sh_item_name(id));
        else printf(" -");
    }
    printf("\n");
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
    /* ---------------- 道具表（GOODS.DAB） ----------------
     * 这两个命令不碰存档，必须在 sh_load 之前分流，
     * 否则 GOODS.DAB 会被当成存档解析而报「不是存档」。 */
    if (strcmp(cmd, "-goods") == 0 || strcmp(cmd, "-goods-set") == 0) {
        sh_goods gb;
        const char *gp = NULL;
        int gi, yes = 0;

        for (gi = 2; gi < argc; gi++) {
            if (strcmp(argv[gi], "-y") == 0) yes = 1;
            else if (argv[gi][0] != '-' && !gp) gp = argv[gi];
        }
        if (!gp) { printf("缺少 GOODS.DAB 路径。\n"); return 1; }

        rc = sh_goods_load(gp, &gb);
        if (rc != 0) {
            printf("无法读入道具表：%s\n",
                   rc == -4 ? "长度不是 11+n×198（不是 GOODS.DAB？）"
                            : "文件打不开或太小");
            return 1;
        }

        if (strcmp(cmd, "-goods") == 0) {
            int id;
            /* -goods <DAB> <id>：只看一件，连说明一起打印 */
            if (argc >= 4 && argv[3][0] >= '0' && argv[3][0] <= '9') {
                int want = atoi(argv[3]);
                if (want < 1 || want > gb.count) {
                    printf("道具 id 必须 1..%d\n", gb.count);
                    sh_goods_free(&gb);
                    return 1;
                }
                printf("ID    : %d\n", want);
                printf("名字  : %s\n", sh_item_name(want));
                printf("说明  : %s\n", sh_item_desc(want));
                printf("攻击  : %d\n", sh_goods_get(&gb, want, SH_GF_ATK));
                printf("防御  : %d\n", sh_goods_get(&gb, want, SH_GF_DEF));
                printf("敏捷  : %d\n", sh_goods_get(&gb, want, SH_GF_AGI));
                printf("必殺  : %d\n", sh_goods_get(&gb, want, SH_GF_CRIT));
                printf("移动  : %d\n", sh_goods_get(&gb, want, SH_GF_MOVE));
                printf("最大体: %d\n", sh_goods_get(&gb, want, SH_GF_HPMAX));
                printf("最大气: %d\n", sh_goods_get(&gb, want, SH_GF_MPMAX));
                sh_goods_free(&gb);
                return 0;
            }
            printf("文件   : %s\n", gp);
            printf("大小   : %lu 字节（11 + %d × 198）\n",
                   (unsigned long)gb.size, gb.count);
            printf("\n  ID 名字                          类型   攻击  防御  敏捷  必殺  移动  最大体 最大气\n");
            printf("  -----------------------------------------------------------------------------\n");
            for (id = 1; id <= gb.count; id++) {
                printf("  %3d %-28s %4d %5d %5d %5d %5d %5d %5d %5d\n",
                       id, sh_item_name(id),
                       (int)gb.data[SH_GOODS_HEAD + (size_t)(id - 1) * SH_GOODS_REC],
                       sh_goods_get(&gb, id, SH_GF_ATK),
                       sh_goods_get(&gb, id, SH_GF_DEF),
                       sh_goods_get(&gb, id, SH_GF_AGI),
                       sh_goods_get(&gb, id, SH_GF_CRIT),
                       sh_goods_get(&gb, id, SH_GF_MOVE),
                       sh_goods_get(&gb, id, SH_GF_HPMAX),
                       sh_goods_get(&gb, id, SH_GF_MPMAX));
            }
            printf("\n改道具请用 -goods-set（会先备份原文件）。\n");
            sh_goods_free(&gb);
            return 0;
        }

        /* -goods-set */
        {
            sh_goods_field fld = SH_GF_ATK;
            int id, v, fi, found = -1;

            if (argc < 6) {
                printf("用法: -goods-set <GOODS.DAB> <id> <字段> <值> [-y]\n");
                printf("字段: atk def agi crit move hpmax mpmax\n");
                sh_goods_free(&gb);
                return 1;
            }
            for (fi = 0; fi < SH_GF_COUNT; fi++) {
                if (strcmp(argv[4], sh_goods_field_key((sh_goods_field)fi)) == 0) {
                    found = fi;
                    break;
                }
            }
            if (found < 0) {
                printf("未知字段: %s（可用 atk def agi crit move hpmax mpmax）\n", argv[4]);
                sh_goods_free(&gb);
                return 1;
            }
            fld = (sh_goods_field)found;
            id = atoi(argv[3]);
            v = atoi(argv[5]);

            if (id < 1 || id > gb.count) {
                printf("道具 id 必须 1..%d\n", gb.count);
                sh_goods_free(&gb);
                return 1;
            }

            printf("[计划] 道具 %d「%s」 %s: %d → %d  (范围 %d..%d)\n",
                   id, sh_item_name(id), sh_goods_field_label(fld),
                   sh_goods_get(&gb, id, fld), v,
                   sh_goods_field_min(fld), sh_goods_field_max(fld));

            if (sh_goods_set(&gb, id, fld, v) != 0) {
                printf("[拒绝] 超出允许范围。\n");
                sh_goods_free(&gb);
                return 1;
            }
            if (!yes) {
                printf("[干跑] 未加 -y，未改动任何文件。\n");
                sh_goods_free(&gb);
                return 0;
            }
            {
                char bak[600];
                if (sh_goods_backup(&gb, bak, sizeof(bak)) != 0) {
                    printf("[失败] 备份失败，已中止（原文件未改动）。\n");
                    sh_goods_free(&gb);
                    return 1;
                }
                printf("[备份] %s\n", bak);
            }
            rc = sh_goods_write(&gb);
            if (rc != 0) {
                printf("[失败] %s\n", rc == -1 ? "写入失败" : "写后校验不一致");
                sh_goods_free(&gb);
                return 1;
            }
            printf("[校验] 通过：重读后 %d 件道具字段一致\n", gb.count);
            sh_goods_free(&gb);
            return 0;
        }
    }

    /* ---------------- 关卡数据：MAP（棋盘）/ ACT（布阵） ----------------
     * 与 GOODS 一样不碰存档，必须在 sh_load 之前分流。 */
    if (strcmp(cmd, "-map") == 0 || strcmp(cmd, "-map-set") == 0) {
        const char *pos[4];
        int np = 0, gi, yes = 0;
        sh_map mp;
        int lrc;

        for (gi = 2; gi < argc; gi++) {
            if (strcmp(argv[gi], "-y") == 0) { yes = 1; continue; }
            if (argv[gi][0] == '-') continue;
            if (np < 4) pos[np] = argv[gi];
            np++;
        }
        if (np < 1) { usage(); return 2; }

        lrc = sh_map_load(pos[0], &mp);
        if (lrc != 0) {
            printf("无法读入棋盘 MAP：%s\n",
                   lrc == -3 ? "魔数/版本不对（不是 MAP，或 ver != 1）"
                 : lrc == -4 ? "长度不是 9+5n，或 n < w*h"
                 : lrc == -5 ? "按「格记录 + 附加层」解析走不完文件"
                             : "文件打不开或太小");
            return 1;
        }

        if (strcmp(cmd, "-map-set") == 0) {
            int x, y, tv, old;
            if (np < 4) {
                printf("用法: -map-set <MAP> <x> <y> <tileId> [-y]\n");
                sh_map_free(&mp);
                return 2;
            }
            x = atoi(pos[1]); y = atoi(pos[2]); tv = atoi(pos[3]);
            if (x < 0 || y < 0 || x >= mp.w || y >= mp.h) {
                printf("[拒绝] 坐标越界：本图 %d x %d（x 0..%d, y 0..%d）\n",
                       mp.w, mp.h, mp.w - 1, mp.h - 1);
                sh_map_free(&mp);
                return 1;
            }
            old = sh_map_tile(&mp, x, y);
            printf("[计划] 格 (%d,%d) tileId: %d -> %d\n", x, y, old, tv);
            if (tv > SH_MAP_MAX_TILE)
                printf("[注意] 超过原版实测最大值 %d（多半显示为空图块）\n", SH_MAP_MAX_TILE);
            if (sh_map_set_tile(&mp, x, y, tv) != 0) {
                printf("[拒绝] tileId 必须在 0..65535\n");
                sh_map_free(&mp);
                return 1;
            }
            if (!yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_map_free(&mp); return 0; }
            {
                char bak[600];
                if (sh_map_backup(&mp, bak, sizeof(bak)) != 0) {
                    printf("[失败] 备份失败，已中止（原文件未改动）。\n");
                    sh_map_free(&mp);
                    return 1;
                }
                printf("[备份] %s\n", to_utf8(bak));
            }
            lrc = sh_map_write(&mp);
            if (lrc != 0) {
                printf("[失败] %s\n", lrc == -1 ? "写入失败" : "写后校验不一致");
                sh_map_free(&mp);
                return 1;
            }
            printf("[校验] 通过：重读后 %d x %d、%d 条记录、该格 tileId=%d\n",
                   mp.w, mp.h, mp.n, sh_map_tile(&mp, x, y));
            sh_map_free(&mp);
            return 0;
        }

        if (np >= 3) {
            int x = atoi(pos[1]), y = atoi(pos[2]), k, ln;
            if (x < 0 || y < 0 || x >= mp.w || y >= mp.h) {
                printf("[拒绝] 坐标越界：本图 %d x %d（x 0..%d, y 0..%d）\n",
                       mp.w, mp.h, mp.w - 1, mp.h - 1);
                sh_map_free(&mp);
                return 1;
            }
            ln = sh_map_layer_n(&mp, x, y);
            printf("格 (%d,%d)  tileId=%d  flag=0x%02X  sub=0x%02X  附加层 %d 条\n",
                   x, y, sh_map_tile(&mp, x, y), sh_map_flag(&mp, x, y),
                   sh_map_sub(&mp, x, y), ln);
            for (k = 0; k < ln; k++)
                printf("  [附加层 %d] tileId=%d\n", k, sh_map_layer_tile(&mp, x, y, k));
            sh_map_free(&mp);
            return 0;
        }

        {
            int x, y, tmin = 65536, tmax = -1, withlay = 0;
            for (y = 0; y < mp.h; y++) {
                for (x = 0; x < mp.w; x++) {
                    int t = sh_map_tile(&mp, x, y);
                    if (t < tmin) tmin = t;
                    if (t > tmax) tmax = t;
                    if (sh_map_flag(&mp, x, y) & 2) withlay++;
                }
            }
            printf("棋盘  : %s\n", to_utf8(pos[0]));
            printf("尺寸  : %d x %d = %d 格\n", mp.w, mp.h, mp.cells);
            printf("记录  : %d 条（格 %d + 附加层 %d）\n", mp.n, mp.cells, mp.layers);
            printf("tileId: %d .. %d（原版全库实测 0..%d）\n", tmin, tmax, SH_MAP_MAX_TILE);
            printf("附加层: %d 个格带附加层\n", withlay);
        }
        sh_map_free(&mp);
        return 0;
    }

    if (strcmp(cmd, "-act") == 0 || strcmp(cmd, "-act-set") == 0) {
        const char *pos[5];
        int np = 0, gi, yes = 0;
        sh_act ac;
        int lrc;

        for (gi = 2; gi < argc; gi++) {
            if (strcmp(argv[gi], "-y") == 0) { yes = 1; continue; }
            if (argv[gi][0] == '-') continue;
            if (np < 5) pos[np] = argv[gi];
            np++;
        }
        if (np < 1) { usage(); return 2; }

        lrc = sh_act_load(pos[0], &ac);
        if (lrc != 0) {
            printf("无法读入布阵表 ACT：%s\n",
                   lrc == -3 ? "魔数/版本/dataOffset 不对（不是 ACT 2/1，或 dataOffset != 11）"
                 : lrc == -4 ? "长度不是 11+38n，或 n 与文件长度对不上"
                             : "文件打不开或太小");
            return 1;
        }

        if (strcmp(cmd, "-act-set") == 0) {
            static const struct { const char *name; int off, size, lo, hi; } ftab[] = {
                {"type",   SH_AREC_TYPE,   2, 0, 65535},
                {"f2",     SH_AREC_F2,     2, 0, 65535},
                {"f4",     SH_AREC_F4,     1, 0, 3},
                {"team",   SH_AREC_TEAM,   1, 0, 4},
                {"x",      SH_AREC_X,      2, 0, 255},
                {"y",      SH_AREC_Y,      2, 0, 255},
                {"fa",     SH_AREC_FA,     2, 0, 65535},
                {"level",  SH_AREC_LEVEL,  2, 0, 60},
                {"flag",   SH_AREC_FLAG,   1, 0, 1},
                {"action", SH_AREC_ACTION, 1, 0, 9},
                {"f10",    SH_AREC_F10,    2, 0, 65535},
                {"f12",    SH_AREC_F12,    4, 0, 2147483647},
                {"f16",    SH_AREC_F16,    2, 0, 65535},
                {"slot0",  SH_AREC_SLOT0,      2, 0, 255},
                {"slot1",  SH_AREC_SLOT0 + 2,  2, 0, 255},
                {"slot2",  SH_AREC_SLOT0 + 4,  2, 0, 255},
                {"slot3",  SH_AREC_SLOT0 + 6,  2, 0, 255},
                {"slot4",  SH_AREC_SLOT0 + 8,  2, 0, 255},
                {"slot5",  SH_AREC_SLOT0 + 10, 2, 0, 255},
                {"slot6",  SH_AREC_SLOT0 + 12, 2, 0, 255}
            };
            int fi, nf, idx, val, old;
            const char *fname;
            if (np < 4) {
                printf("用法: -act-set <ACT> <序号> <字段> <值> [-y]\n");
                printf("字段: type f2 f4 team x y fa level flag action f10 f12 f16 slot0..slot6\n");
                sh_act_free(&ac);
                return 2;
            }
            idx = atoi(pos[1]);
            fname = pos[2];
            val = atoi(pos[3]);
            if (idx < 0 || idx >= ac.count) {
                printf("[拒绝] 序号越界：本表 %d 条（0..%d）\n", ac.count, ac.count - 1);
                sh_act_free(&ac);
                return 1;
            }
            nf = (int)(sizeof(ftab) / sizeof(ftab[0]));
            for (fi = 0; fi < nf; fi++)
                if (strcmp(fname, ftab[fi].name) == 0) break;
            if (fi >= nf) {
                printf("[拒绝] 未知字段「%s」\n", fname);
                sh_act_free(&ac);
                return 2;
            }
            old = sh_act_get(&ac, idx, ftab[fi].off, ftab[fi].size);
            printf("[计划] 记录 %d 的 %s: %d -> %d\n", idx, ftab[fi].name, old, val);
            if (val < ftab[fi].lo || val > ftab[fi].hi) {
                printf("[拒绝] %s 允许范围 %d..%d\n", ftab[fi].name, ftab[fi].lo, ftab[fi].hi);
                sh_act_free(&ac);
                return 1;
            }
            if (sh_act_set(&ac, idx, ftab[fi].off, ftab[fi].size, val) != 0) {
                printf("[拒绝] 写入失败\n");
                sh_act_free(&ac);
                return 1;
            }
            if (!yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_act_free(&ac); return 0; }
            {
                char bak[600];
                if (sh_act_backup(&ac, bak, sizeof(bak)) != 0) {
                    printf("[失败] 备份失败，已中止（原文件未改动）。\n");
                    sh_act_free(&ac);
                    return 1;
                }
                printf("[备份] %s\n", to_utf8(bak));
            }
            lrc = sh_act_write(&ac);
            if (lrc != 0) {
                printf("[失败] %s\n", lrc == -1 ? "写入失败" : "写后校验不一致");
                sh_act_free(&ac);
                return 1;
            }
            printf("[校验] 通过：重读后 %d 条记录、记录 %d 的 %s = %d\n",
                   ac.count, idx, ftab[fi].name,
                   sh_act_get(&ac, idx, ftab[fi].off, ftab[fi].size));
            sh_act_free(&ac);
            return 0;
        }

        if (np >= 2) {
            int idx = atoi(pos[1]);
            if (idx < 0 || idx >= ac.count) {
                printf("[拒绝] 序号越界：本表 %d 条（0..%d）\n", ac.count, ac.count - 1);
                sh_act_free(&ac);
                return 1;
            }
            act_print_record(&ac, idx);
            sh_act_free(&ac);
            return 0;
        }

        {
            int k;
            printf("布阵表: %s\n", to_utf8(pos[0]));
            printf("记录  : %d 条（11 B 头 + 38 B x n）\n", ac.count);
            for (k = 0; k < ac.count; k++) act_print_record(&ac, k);
        }
        sh_act_free(&ac);
        return 0;
    }

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
            {
                /* 同一 type id 可能有多条记录（实测存档里史進有 2 条），提示一下 */
                int a, b, any = 0;
                for (a = 0; a < s.count; a++) {
                    int n = 0;
                    if (!sh_is_playable(&s.units[a])) continue;
                    for (b = 0; b < s.count; b++) {
                        if (s.units[b].id == s.units[a].id) n++;
                    }
                    if (n > 1) {
                        int first = 1, c;
                        for (c = 0; c < a; c++) {
                            if (s.units[c].id == s.units[a].id) { first = 0; break; }
                        }
                        if (first) {
                            printf("\n  [注意] type id %d（%s）在存档里有 %d 条记录，\n",
                                   (int)s.units[a].id, sh_char_name((int)s.units[a].id), n);
                            printf("上面的表会重复列出；用 -tree %d 可逐条查看差异。\n",
                                   (int)s.units[a].id);
                            any = 1;
                        }
                    }
                }
                (void)any;
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

        if (f == SH_F_MOVE) {
            printf("[提示] 移动是每回合重算的派生值（职业基础+加成+装备）；\n");
            printf("       已把差值折算进 rel+36，并同步写剩余移动力 rel+76，\n");
            printf("       面板与棋盘都立即生效（否则棋盘要等下一回合）。\n");
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
        int i, id, hits = 0;
        if (argc < 4) { usage(); return 2; }
        id = atoi(argv[3]);
        /* 同 id 可能有多个记录（实测存档里存在两条史進），逐个列出而不是报错 */
        for (i = 0; i < s.count; i++) {
            if ((int)s.units[i].id != id) continue;
            hits++;
            if (hits > 1) printf("\r\n===== 同 id 的第 %d 条记录（等级 %u）=====\r\n",
                                hits, (unsigned)s.units[i].f[SH_F_LEVEL]);
            sh_build_tree_text(&s.units[i], text, (int)sizeof(text));
            printf("%s", text);
        }
        if (hits == 0) {
            printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id));
            sh_free(&s);
            return 1;
        }
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

        {
            int olditem = (int)s.units[idx].slots[slot];
            if (sh_set_slot(&s, idx, slot, (uint8_t)item) != 0) {
                printf("[拒绝] 写入失败（槽或下标非法）\n"); sh_free(&s); return 1;
            }
            /* 道具换了就把槽位缓存按 GOODS 重填（等同游戏“获得道具”）；
             * 否则新装装备的加成为 0，进游戏会看到“装上了攻击力却没变”。 */
            if (item != olditem) {
                char gpath[600];
                sh_goods gb;
                if (sh_goods_locate(path, gpath, sizeof(gpath)) &&
                    sh_goods_load(gpath, &gb) == 0) {
                    unsigned short c4[4];
                    int k;
                    static const int offs[4] = { 76, 82, 84, 90 };
                    for (k = 0; k < 4; k++)
                        c4[k] = (unsigned short)sh_goods_get_u16(&gb, item, offs[k]);
                    sh_slot_cache_set4(&s, idx, slot, c4);
                    sh_goods_free(&gb);
                    printf("[缓存] 装备加成已按 GOODS 重填：攻击 +%u\n", (unsigned)c4[0]);
                } else {
                    unsigned short zero[4] = { 0, 0, 0, 0 };
                    sh_slot_cache_set4(&s, idx, slot, zero);
                    printf("[警告] 找不到 GOODS.DAB，槽位缓存已清零（可用 -cache 手动设置）\n");
                }
            }
        }
        if (!do_yes) { printf("[干跑] 未加 -y，未改动任何文件。\n"); sh_free(&s); return 0; }
        rc = commit(&s, path, 1);
        sh_free(&s);
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "-cache") == 0) {
        int idx, id, slot, which, v, old;
        if (argc < 7) {
            printf("用法: -cache <save> <类型id> <槽0-6> <atk|def> <值> [-y]\n");
            sh_free(&s);
            return 2;
        }
        id    = atoi(argv[3]);
        slot  = atoi(argv[4]);
        if (strcmp(argv[5], "atk") == 0) which = SH_CACHE_ATK;
        else if (strcmp(argv[5], "def") == 0) which = SH_CACHE_DEF;
        else { printf("第 5 个参数应为 atk 或 def\n"); sh_free(&s); return 2; }
        v = atoi(argv[6]);
        if (slot < 0 || slot >= SH_SLOT_COUNT) {
            printf("槽序号应为 0..%d\n", SH_SLOT_COUNT - 1); sh_free(&s); return 2;
        }
        idx = sh_find_unit(&s, id);
        if (idx < 0) {
            printf("存档里没有类型 id = %d 的角色（%s）\n", id, sh_char_name(id));
            sh_free(&s); return 1;
        }
        old = sh_slot_cache_get(&s, idx, slot, which);
        printf("[计划] %s(id %d) 槽%d %s缓存: %d → %d",
               sh_char_name(id), id, slot,
               which == SH_CACHE_ATK ? "攻击" : "防御", old, v);
        if (s.units[idx].slots[slot] == 0) {
            printf("  [注意] 该槽是空的，缓存不参与面板计算");
        }
        printf("\n");
        if (v < 0 || v > 65535) {
            printf("[拒绝] 超出范围 0..65535\n"); sh_free(&s); return 1;
        }
        if (sh_slot_cache_set(&s, idx, slot, which, v) != 0) {
            printf("[拒绝] 写入失败\n"); sh_free(&s); return 1;
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
