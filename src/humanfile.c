/*
 * humanfile.c —— HUMAN.HUM 职业表读写（V2.1）
 *
 * 只用 ANSI C + Win32 kernel32（与 Win9x 兼容，不用 comctl32）。
 */
#include "humanfile.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SH_SEP
#define SH_SEP '\\'
#endif

/* 7 组字段的记录内偏移：基础值 u16 / 成长率 u8 */
static const int k_base_off[SH_HF_COUNT] = {
    0x05, 0x07, 0x0A, 0x0D, 0x10, 0x13, 0x16
};
static const int k_growth_off[SH_HF_COUNT] = {
    0x03, 0x04, 0x0C, 0x0F, 0x12, 0x15, 0x18
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* ---------------------------------------------------------------- 加载 */
static int human_parse(sh_human *h);

int sh_human_load(const char *path, sh_human *h)
{
    FILE *f;
    long sz;
    size_t got;
    char safe[SH_HUMAN_PATH];

    if (!path || !h) return -1;

    strncpy(safe, path, sizeof(safe) - 1);
    safe[sizeof(safe) - 1] = '\0';

    memset(h, 0, sizeof(*h));

    f = fopen(safe, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    if (sz < SH_HUMAN_HEAD + SH_HUMAN_REC) { fclose(f); return -2; }

    h->data = (uint8_t *)malloc((size_t)sz);
    if (!h->data) { fclose(f); return -3; }

    got = fread(h->data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(h->data); h->data = NULL; return -1; }
    h->size = (size_t)sz;

    if (human_parse(h) != 0) return -4;

    strncpy(h->path, safe, SH_HUMAN_PATH - 1);
    h->path[SH_HUMAN_PATH - 1] = '\0';
    return 0;
}

/* 解析已就位的数据（调用方负责 h->data / h->size） */
static int human_parse(sh_human *h)
{
    uint32_t off;
    int i, count;

    if (h->data[0] != 'H' || h->data[1] != 'U' || h->data[2] != 'M' ||
        rd16(h->data + 5) != SH_HUMAN_MAX ||
        rd16(h->data + 7) != SH_HUMAN_REC ||
        rd32(h->data + 9) != SH_HUMAN_HEAD) {
        sh_human_free(h);
        return -4;
    }

    count = (int)rd16(h->data + 5);
    off = SH_HUMAN_HEAD;
    for (i = 0; i < count; i++) {
        int n;
        if ((size_t)off + SH_HUMAN_REC > h->size) { sh_human_free(h); return -5; }
        h->off[i] = off;
        n = (int)rd16(h->data + off + 0x2D);
        off += SH_HUMAN_REC + (uint32_t)n * SH_HUMAN_SUB;
    }
    if ((size_t)off != h->size) { sh_human_free(h); return -5; }
    h->count = count;
    return 0;
}

int sh_human_load_mem(const uint8_t *buf, size_t size, sh_human *h)
{
    if (!buf || !h || size < SH_HUMAN_HEAD + SH_HUMAN_REC) return -2;
    memset(h, 0, sizeof(*h));
    h->data = (uint8_t *)malloc(size);
    if (!h->data) return -3;
    memcpy(h->data, buf, size);
    h->size = size;
    if (human_parse(h) != 0) return -4;
    h->path[0] = '\0';
    return 0;
}

void sh_human_free(sh_human *h)
{
    if (!h) return;
    if (h->data) { free(h->data); h->data = NULL; }
    h->size = 0;
    h->count = 0;
}

/* ---------------------------------------------------------------- 字段 */

const char *sh_human_field_label(sh_human_field f)
{
    switch (f) {
    case SH_HF_HP:   return "最大体力";
    case SH_HF_MP:   return "最大气力";
    case SH_HF_ATK:  return "攻击";
    case SH_HF_DEF:  return "防御";
    case SH_HF_HIT:  return "命中";
    case SH_HF_AGI:  return "敏捷";
    case SH_HF_CRIT: return "必殺";
    default:         return "?";
    }
}

const char *sh_human_field_key(sh_human_field f)
{
    switch (f) {
    case SH_HF_HP:   return "hp";
    case SH_HF_MP:   return "mp";
    case SH_HF_ATK:  return "atk";
    case SH_HF_DEF:  return "def";
    case SH_HF_HIT:  return "hit";
    case SH_HF_AGI:  return "agi";
    case SH_HF_CRIT: return "crit";
    default:         return "";
    }
}

int sh_human_base_max(void)   { return 65535; }
int sh_human_growth_max(void) { return 255; }
int sh_human_move_max(void)   { return 255; }

static int valid_id(const sh_human *h, int id)
{
    return h && h->data && id >= 0 && id < h->count;
}

int sh_human_u8(const sh_human *h, int id, int off)
{
    if (!valid_id(h, id) || off < 0 || off >= SH_HUMAN_REC) return -1;
    return (int)h->data[h->off[id] + off];
}

int sh_human_u16(const sh_human *h, int id, int off)
{
    if (!valid_id(h, id) || off < 0 || off + 1 >= SH_HUMAN_REC) return -1;
    return (int)rd16(h->data + h->off[id] + off);
}

int sh_human_base(const sh_human *h, int id, sh_human_field f)
{
    if ((int)f < 0 || f >= SH_HF_COUNT) return -1;
    return sh_human_u16(h, id, k_base_off[f]);
}

int sh_human_growth(const sh_human *h, int id, sh_human_field f)
{
    if ((int)f < 0 || f >= SH_HF_COUNT) return -1;
    return sh_human_u8(h, id, k_growth_off[f]);
}

int sh_human_move(const sh_human *h, int id)
{
    return sh_human_u8(h, id, 0x09);
}

int sh_human_skill_count(const sh_human *h, int id)
{
    return sh_human_u16(h, id, 0x2D);
}

int sh_human_skill_level_max(void) { return 30; }

int sh_human_skill_off(const sh_human *h, int id, int i)
{
    int n;
    size_t off;

    if (!valid_id(h, id) || i < 0) return -1;
    n = (int)rd16(h->data + h->off[id] + 0x2D);
    if (i >= n || i >= SH_HUMAN_SKILL_MAX) return -1;

    off = (size_t)h->off[id] + SH_HUMAN_REC + (size_t)i * SH_HUMAN_SUB;
    if (off + SH_HUMAN_SUB > h->size) return -1;
    return (int)off;
}

int sh_human_skill_sid(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)rd16(h->data + off);
}

int sh_human_skill_level(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x11];
}

int sh_human_skill_hidden(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x05];
}

/* +0x02：技能类别（0..11）。FUN_00406150 按它 switch 选不同提示串 */
int sh_human_skill_cat(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x02];
}

/* +0x12：气力消耗（u16）。FUN_00406150 施放时从 unit+0x44 扣除 */
int sh_human_skill_cost(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)rd16(h->data + off + 0x12);
}

int sh_human_set_skill_cost(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 999) return -2;
    wr16(h->data + off + 0x12, (uint16_t)v);
    return 0;
}

int sh_human_set_skill_cat(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 11) return -2;
    h->data[off + 0x02] = (uint8_t)v;
    return 0;
}

/* +0x10：范围攻击宽度（格）。FUN_00403930 / FUN_004039E0 抄进动作对象 +0x41 */
int sh_human_skill_range(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x10];
}

int sh_human_set_skill_range(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 99) return -2;
    h->data[off + 0x10] = (uint8_t)v;
    return 0;
}

/* +0x06：范围半径。0 = 单体攻击；非 0 = 范围攻击（值越大范围搜索半径越大）。 */
int sh_human_skill_aoe(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)rd16(h->data + off + 0x06);
}

int sh_human_set_skill_aoe(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 999) return -2;
    wr16(h->data + off + 0x06, (uint16_t)v);
    return 0;
}

/* +0x0D：单体技射程（格）。隔山打虎 = 2（上下左右各两格）。 */
int sh_human_skill_reach(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x0D];
}

int sh_human_set_skill_reach(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 99) return -2;
    h->data[off + 0x0D] = (uint8_t)v;
    return 0;
}

/* ===== 战斗参数（报告 §4.33.1 / §4.33.3）===== */

/* +0x0A：附加状态 1 的概率（百分点）。FUN_00406C60 里与攻击者 +0x52 相加，
   再 rand()%100 < 该值 时给目标挂状态 1。原版取值 0/5/10/…/50。 */
int sh_human_skill_status(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)h->data[off + 0x0A];
}

int sh_human_set_skill_status(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 100) return -2;
    h->data[off + 0x0A] = (uint8_t)v;
    return 0;
}

/* +0x14：命中修正（有符号）。命中 = 攻方命中 - 守方敏捷/2 + 本值。
   原版出现 -5/-10/-15（重型武器），故按 int16 读。 */
int sh_human_skill_hit(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)(int16_t)rd16(h->data + off + 0x14);
}

int sh_human_set_skill_hit(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < -999 || v > 999) return -2;
    wr16(h->data + off + 0x14, (uint16_t)(int16_t)v);
    return 0;
}

/* +0x16：暴击修正。暴击率 = 攻方必殺/2 + 本值（原版 5..50）。 */
int sh_human_skill_crit(const sh_human *h, int id, int i)
{
    int off = sh_human_skill_off(h, id, i);
    return off < 0 ? -1 : (int)rd16(h->data + off + 0x16);
}

int sh_human_set_skill_crit(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 999) return -2;
    wr16(h->data + off + 0x16, (uint16_t)v);
    return 0;
}

/* +0x1C 起 4 段攻击表，每段 6 B：+0 触发帧、+2 伤害倍率%、+4 方向偏移。
   FUN_004073E0 逐段取：倍率非 0 才出手（0 = 该段不存在）。 */
int sh_human_skill_seg(const sh_human *h, int id, int i, int seg, int field)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0 || seg < 0 || seg > 3 || field < 0 || field > 2) return -1;
    return (int)rd16(h->data + off + 0x1C + seg * 6 + field * 2);
}

int sh_human_set_skill_seg(sh_human *h, int id, int i, int seg, int field, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0 || seg < 0 || seg > 3 || field < 0 || field > 2) return -1;
    if (v < 0 || v > 9999) return -2;
    wr16(h->data + off + 0x1C + seg * 6 + field * 2, (uint16_t)v);
    return 0;
}

int sh_human_skill_hidden_count(const sh_human *h, int id)
{
    int i, n, c = 0;

    if (!valid_id(h, id)) return -1;
    n = (int)rd16(h->data + h->off[id] + 0x2D);
    for (i = 0; i < n && i < SH_HUMAN_SKILL_MAX; i++) {
        int off = sh_human_skill_off(h, id, i);
        if (off >= 0 && h->data[off + 0x05]) c++;
    }
    return c;
}

int sh_human_set_skill_sid(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 0 || v > 65535) return -2;
    wr16(h->data + off, (uint16_t)v);
    return 0;
}

int sh_human_set_skill_level(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v < 1 || v > sh_human_skill_level_max()) return -2;
    h->data[off + 0x11] = (uint8_t)v;
    return 0;
}

int sh_human_set_skill_hidden(sh_human *h, int id, int i, int v)
{
    int off = sh_human_skill_off(h, id, i);
    if (off < 0) return -1;
    if (v != 0 && v != 1) return -2;
    h->data[off + 0x05] = (uint8_t)v;
    return 0;
}

int sh_human_skill_copy(sh_human *dst, int dst_cls, int dst_i,
                        const sh_human *src, int src_cls, int src_i)
{
    int doff = sh_human_skill_off(dst, dst_cls, dst_i);
    int soff = sh_human_skill_off(src, src_cls, src_i);

    if (doff < 0 || soff < 0) return -1;
    memmove(dst->data + doff, src->data + soff, SH_HUMAN_SUB);
    return 0;
}

int sh_human_set_base(sh_human *h, int id, sh_human_field f, int v)
{
    if (!valid_id(h, id) || (int)f < 0 || f >= SH_HF_COUNT) return -1;
    if (v < 0 || v > sh_human_base_max()) return -2;
    wr16(h->data + h->off[id] + k_base_off[f], (uint16_t)v);
    return 0;
}

int sh_human_set_growth(sh_human *h, int id, sh_human_field f, int v)
{
    if (!valid_id(h, id) || (int)f < 0 || f >= SH_HF_COUNT) return -1;
    if (v < 0 || v > 255) return -2;
    h->data[h->off[id] + k_growth_off[f]] = (uint8_t)v;
    return 0;
}

int sh_human_set_move(sh_human *h, int id, int v)
{
    if (!valid_id(h, id)) return -1;
    if (v < 0 || v > 255) return -2;
    h->data[h->off[id] + 0x09] = (uint8_t)v;
    return 0;
}

/* ---------------------------------------------------------------- 定位 */

int sh_human_locate(const char *savePath, char *out, size_t n)
{
    char dir[SH_HUMAN_PATH];
    char cand[SH_HUMAN_PATH + 32];
    char *p;
    FILE *f;

    if (!savePath || !out || n < 8) return 0;

    strncpy(dir, savePath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    p = strrchr(dir, SH_SEP);
    if (p) *p = '\0'; else strcpy(dir, ".");

    sprintf(cand, "%s%c..%cData%cHUMAN.HUM", dir, SH_SEP, SH_SEP, SH_SEP);
    f = fopen(cand, "rb");
    if (f) { fclose(f); strncpy(out, cand, n - 1); out[n - 1] = '\0'; return 1; }

    sprintf(cand, "%s%cData%cHUMAN.HUM", dir, SH_SEP, SH_SEP);
    f = fopen(cand, "rb");
    if (f) { fclose(f); strncpy(out, cand, n - 1); out[n - 1] = '\0'; return 1; }

    sprintf(cand, "%s%cHUMAN.HUM", dir, SH_SEP);
    f = fopen(cand, "rb");
    if (f) { fclose(f); strncpy(out, cand, n - 1); out[n - 1] = '\0'; return 1; }

    return 0;
}

/* ---------------------------------------------------------------- 备份 */

static void split_dir(const char *path, char *dir, size_t n)
{
    const char *p;
    if (!path || !n) return;
    p = strrchr(path, SH_SEP);
    if (!p) { strncpy(dir, ".", n - 1); dir[n - 1] = '\0'; return; }
    {
        size_t len = (size_t)(p - path);
        if (len >= n) len = n - 1;
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
}

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, SH_SEP);
    return p ? p + 1 : path;
}

int sh_human_backup(const sh_human *h, char *out, size_t outsz)
{
    char dir[SH_HUMAN_PATH], name[SH_HUMAN_PATH + 64], stamp[32];
    const char *base;
    time_t now;
    struct tm *tmv;
    int seq;
    FILE *in, *ot;

    if (!h || !h->data || !h->size) return -1;

    split_dir(h->path, dir, sizeof(dir));
    base = base_name(h->path);

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv) {
        sprintf(stamp, "%04d%02d%02d-%02d%02d%02d",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        strcpy(stamp, "00000000-000000");
    }

    for (seq = 0; seq < 100; seq++) {
        if (seq == 0)
            sprintf(name, "%s%c%s_%s.bak", dir, SH_SEP, base, stamp);
        else
            sprintf(name, "%s%c%s_%s_%d.bak", dir, SH_SEP, base, stamp, seq + 1);
        in = fopen(name, "rb");
        if (in) { fclose(in); continue; }
        break;
    }

    in = fopen(h->path, "rb");
    if (!in) return -1;
    ot = fopen(name, "wb");
    if (!ot) { fclose(in); return -1; }

    {
        char buf[8192];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, got, ot) != got) {
                fclose(in); fclose(ot); remove(name); return -1;
            }
        }
    }
    fclose(in);
    fclose(ot);

    if (out && outsz) {
        strncpy(out, name, outsz - 1);
        out[outsz - 1] = '\0';
    }
    return 0;
}

/* ---------------------------------------------------------------- 写入 */

int sh_human_write(sh_human *h)
{
    sh_human chk;
    int i, rc;

    if (!h || !h->data || h->size == 0) return -1;

    if (sh_write_file(h->path, h->data, h->size) != 0) return -1;

    rc = sh_human_load(h->path, &chk);
    if (rc != 0) return -2;
    if (chk.size != h->size || chk.count != h->count) { sh_human_free(&chk); return -2; }

    for (i = 0; i < h->count; i++) {
        int f;
        for (f = 0; f < SH_HF_COUNT; f++) {
            if (sh_human_base(&chk, i, (sh_human_field)f) != sh_human_base(h, i, (sh_human_field)f) ||
                sh_human_growth(&chk, i, (sh_human_field)f) != sh_human_growth(h, i, (sh_human_field)f)) {
                sh_human_free(&chk);
                return -2;
            }
        }
        if (sh_human_move(&chk, i) != sh_human_move(h, i)) {
            sh_human_free(&chk);
            return -2;
        }
        {
            int n = sh_human_skill_count(h, i), k;
            for (k = 0; k < n; k++) {
                if (sh_human_skill_sid(&chk, i, k) != sh_human_skill_sid(h, i, k) ||
                    sh_human_skill_level(&chk, i, k) != sh_human_skill_level(h, i, k) ||
                    sh_human_skill_hidden(&chk, i, k) != sh_human_skill_hidden(h, i, k) ||
                    sh_human_skill_cost(&chk, i, k) != sh_human_skill_cost(h, i, k) ||
                    sh_human_skill_range(&chk, i, k) != sh_human_skill_range(h, i, k) ||
                    sh_human_skill_aoe(&chk, i, k) != sh_human_skill_aoe(h, i, k) ||
                    sh_human_skill_reach(&chk, i, k) != sh_human_skill_reach(h, i, k) ||
                    sh_human_skill_status(&chk, i, k) != sh_human_skill_status(h, i, k) ||
                    sh_human_skill_hit(&chk, i, k) != sh_human_skill_hit(h, i, k) ||
                    sh_human_skill_crit(&chk, i, k) != sh_human_skill_crit(h, i, k) ||
                    sh_human_skill_cat(&chk, i, k) != sh_human_skill_cat(h, i, k)) {
                    sh_human_free(&chk);
                    return -2;
                }
                {
                    int sg, fd;
                    for (sg = 0; sg < 4; sg++)
                        for (fd = 0; fd < 3; fd++)
                            if (sh_human_skill_seg(&chk, i, k, sg, fd) !=
                                sh_human_skill_seg(h, i, k, sg, fd)) {
                                sh_human_free(&chk);
                                return -2;
                            }
                }
            }
        }
    }
    sh_human_free(&chk);
    return 0;
}