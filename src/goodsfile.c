/*
 * goodsfile.c —— GOODS.DAB 道具表读写（V2.1）
 *
 * 只用 ANSI C + Win32 kernel32（与 Win9x 兼容，不用 comctl32）。
 */
#include "goodsfile.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SH_SEP
#define SH_SEP '\\'
#endif

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* ---------------------------------------------------------------- 加载 */

int sh_goods_load(const char *path, sh_goods *g)
{
    FILE *f;
    long sz;
    size_t got;

    if (!path || !g) return -1;
    memset(g, 0, sizeof(*g));

    f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }

    if (sz < SH_GOODS_HEAD + SH_GOODS_REC) { fclose(f); return -2; }

    g->data = (uint8_t *)malloc((size_t)sz);
    if (!g->data) { fclose(f); return -3; }

    got = fread(g->data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(g->data); g->data = NULL; return -1; }

    g->size = (size_t)sz;

    /* 结构校验：必须是 11 + n×198 正好用完 */
    if ((g->size - SH_GOODS_HEAD) % SH_GOODS_REC != 0) {
        free(g->data); g->data = NULL; g->size = 0;
        return -4;
    }
    g->count = (int)((g->size - SH_GOODS_HEAD) / SH_GOODS_REC);
    if (g->count > SH_GOODS_MAX) g->count = SH_GOODS_MAX;

    strncpy(g->path, path, SH_GOODS_PATH - 1);
    g->path[SH_GOODS_PATH - 1] = '\0';
    return 0;
}

void sh_goods_free(sh_goods *g)
{
    if (!g) return;
    if (g->data) { free(g->data); g->data = NULL; }
    g->size = 0;
    g->count = 0;
}

/* ---------------------------------------------------------------- 字段 */

const char *sh_goods_field_label(sh_goods_field f)
{
    switch (f) {
    case SH_GF_ATK:   return "攻击力";
    case SH_GF_DEF:   return "防御力";
    case SH_GF_MOVE:  return "移动加成";
    case SH_GF_HPMAX: return "最大体力";
    case SH_GF_MPMAX: return "最大气力";
    default:          return "?";
    }
}

const char *sh_goods_field_key(sh_goods_field f)
{
    switch (f) {
    case SH_GF_ATK:   return "atk";
    case SH_GF_DEF:   return "def";
    case SH_GF_MOVE:  return "move";
    case SH_GF_HPMAX: return "hpmax";
    case SH_GF_MPMAX: return "mpmax";
    default:          return "";
    }
}

int sh_goods_field_off(sh_goods_field f)
{
    switch (f) {
    case SH_GF_ATK:   return 76;
    case SH_GF_DEF:   return 84;
    case SH_GF_MOVE:  return 104;
    case SH_GF_HPMAX: return 110;
    case SH_GF_MPMAX: return 112;
    default:          return 0;
    }
}

int sh_goods_field_min(sh_goods_field f)
{
    if (f == SH_GF_MOVE) return -30;   /* 只出现小负值（实测 -1） */
    return 0;
}

int sh_goods_field_max(sh_goods_field f)
{
    switch (f) {
    case SH_GF_ATK:
    case SH_GF_DEF:   return 255;      /* 角色属性上限 */
    case SH_GF_MOVE:  return 30;
    case SH_GF_HPMAX:
    case SH_GF_MPMAX: return 510;
    default:          return 0;
    }
}

static uint8_t *rec_of(const sh_goods *g, int id)
{
    if (!g || !g->data) return NULL;
    if (id < 1 || id > g->count) return NULL;
    return g->data + SH_GOODS_HEAD + (size_t)(id - 1) * SH_GOODS_REC;
}

int sh_goods_get(const sh_goods *g, int id, sh_goods_field f)
{
    uint8_t *r = rec_of(g, id);
    if (!r || f < 0 || f >= SH_GF_COUNT) return 0;

    if (f == SH_GF_MOVE) return (int)(int16_t)rd16(r + sh_goods_field_off(f));
    return (int)rd16(r + sh_goods_field_off(f));
}

int sh_goods_set(sh_goods *g, int id, sh_goods_field f, int v)
{
    uint8_t *r = rec_of(g, id);
    if (!r || f < 0 || f >= SH_GF_COUNT) return -1;
    if (v < sh_goods_field_min(f) || v > sh_goods_field_max(f)) return -2;

    wr16(r + sh_goods_field_off(f), (uint16_t)(int16_t)v);
    return 0;
}

/* ---------------------------------------------------------------- 备份 */

/* 路径拆目录 */
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

int sh_goods_backup(const sh_goods *g, char *out, size_t outsz)
{
    char dir[SH_GOODS_PATH], name[SH_GOODS_PATH + 64], stamp[32];
    const char *base;
    time_t now;
    struct tm *tmv;
    int seq;
    FILE *in, *ot;

    if (!g || !g->data || !g->size) return -1;

    split_dir(g->path, dir, sizeof(dir));
    base = base_name(g->path);

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv) {
        sprintf(stamp, "%04d%02d%02d-%02d%02d%02d",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        strcpy(stamp, "00000000-000000");
    }

    /* 选一个不存在的名字（同一秒重复时加 _2/_3…） */
    for (seq = 0; seq < 100; seq++) {
        if (seq == 0)
            sprintf(name, "%s%c%s_%s.bak", dir, SH_SEP, base, stamp);
        else
            sprintf(name, "%s%c%s_%s_%d.bak", dir, SH_SEP, base, stamp, seq + 1);
        in = fopen(name, "rb");
        if (in) { fclose(in); continue; }
        break;
    }

    in = fopen(g->path, "rb");
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

int sh_goods_write(sh_goods *g)
{
    sh_goods chk;
    int i, rc;

    if (!g || !g->data || g->size == 0) return -1;

    /* 原子写（fileio 里的实现：先写 .tmp，再改名覆盖） */
    if (sh_write_file(g->path, g->data, g->size) != 0) return -1;

    /* 重读校验：大小一致 + 每个道具的 5 个字段与内存一致 */
    rc = sh_goods_load(g->path, &chk);
    if (rc != 0) return -2;
    if (chk.size != g->size || chk.count != g->count) { sh_goods_free(&chk); return -2; }

    for (i = 1; i <= g->count; i++) {
        sh_goods_field f;
        for (f = 0; f < SH_GF_COUNT; f++) {
            if (sh_goods_get(&chk, i, f) != sh_goods_get(g, i, f)) {
                sh_goods_free(&chk);
                return -2;
            }
        }
    }
    sh_goods_free(&chk);
    return 0;
}
