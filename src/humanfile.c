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

int sh_human_load(const char *path, sh_human *h)
{
    FILE *f;
    long sz;
    size_t got;
    char safe[SH_HUMAN_PATH];
    uint32_t off;
    int i;

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

    /* 头校验 */
    if (h->data[0] != 'H' || h->data[1] != 'U' || h->data[2] != 'M' ||
        rd16(h->data + 5) != SH_HUMAN_MAX ||
        rd16(h->data + 7) != SH_HUMAN_REC ||
        rd32(h->data + 9) != SH_HUMAN_HEAD) {
        sh_human_free(h);
        return -4;
    }

    /* 逐条走，确认正好用尽文件 */
    h->count = (int)rd16(h->data + 5);
    off = SH_HUMAN_HEAD;
    for (i = 0; i < h->count; i++) {
        int n;
        if ((size_t)off + SH_HUMAN_REC > h->size) { sh_human_free(h); return -5; }
        h->off[i] = off;
        n = (int)rd16(h->data + off + 0x2D);
        off += SH_HUMAN_REC + (uint32_t)n * SH_HUMAN_SUB;
    }
    if ((size_t)off != h->size) { sh_human_free(h); return -5; }

    strncpy(h->path, safe, SH_HUMAN_PATH - 1);
    h->path[SH_HUMAN_PATH - 1] = '\0';
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
    }
    sh_human_free(&chk);
    return 0;
}