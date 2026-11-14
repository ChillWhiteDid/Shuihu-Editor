/*
 * mapfile.c -- 关卡数据（MAP 棋盘 / ACT 布阵表）读写（V2.3）
 *
 * 只用 ANSI C + Win32 kernel32（与 Win9x 兼容，不用 comctl32）。
 * 结构依据见 mapfile.h 的注释与分析报告 §4.11 / §4.19 / §4.36.2。
 */
#include "mapfile.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SH_SEP
#define SH_SEP '\\'
#endif

/* ------------------------------------------------------------------ 小工具 */

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

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

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

/* 备份：同目录 <原名>_YYYYMMDD-HHMMSS.bak，不覆盖已有文件。
 * 成功 0，失败 -1。 */
static int make_backup(const char *path, char *out, size_t outsz)
{
    char dir[SH_MAP_PATH], name[SH_MAP_PATH + 64], stamp[32];
    const char *base;
    time_t now;
    struct tm *tmv;
    int seq;
    FILE *in, *ot;

    if (!path || !path[0]) return -1;

    split_dir(path, dir, sizeof(dir));
    base = base_name(path);

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

    in = fopen(path, "rb");
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

/* ================================================================== MAP */

/* 从内存缓冲接管一份 MAP（供撤销/重做恢复用）：成功后 data 归 m 所有；
 * 失败时 data 被释放。返回码与 sh_map_load 相同（-3/-4/-5/-2）。 */
int sh_map_adopt(sh_map *m, uint8_t *data, size_t sz)
{
    uint32_t *co = NULL, *lo = NULL;
    int *lc = NULL;
    size_t off;
    int w, h, cells, n, i, li, cap;

    if (!m || !data) return -1;
    if (sz < SH_MAP_HDR + SH_MAP_REC) { free(data); return -1; }
    if (memcmp(data, "MAP", 3) != 0 || data[3] != 0 || data[4] != 1) { free(data); return -3; }

    w = (int)rd16(data + 5);
    h = (int)rd16(data + 7);
    if (w <= 0 || h <= 0 || w > SH_MAP_MAX_W || h > SH_MAP_MAX_H) { free(data); return -4; }
    cells = w * h;

    if ((sz - SH_MAP_HDR) % SH_MAP_REC != 0) { free(data); return -4; }
    n = (int)((sz - SH_MAP_HDR) / SH_MAP_REC);
    if (n < cells) { free(data); return -4; }

    co = (uint32_t *)malloc((size_t)cells * sizeof(uint32_t));
    if (!co) { free(data); return -2; }
    cap = n - cells;
    if (cap > 0) {
        lo = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));
        lc = (int *)malloc((size_t)cap * sizeof(int));
        if (!lo || !lc) { free(data); free(co); free(lo); free(lc); return -2; }
    }

    off = SH_MAP_HDR;
    li = 0;
    for (i = 0; i < cells; i++) {
        if (off + SH_MAP_REC > sz) goto bad5;
        co[i] = (uint32_t)off;
        while ((data[off + SH_MREC_FLAG] & 2) == 2) {
            off += SH_MAP_REC;
            if (off + SH_MAP_REC > sz) goto bad5;
            if (li >= cap) goto bad5;
            lo[li] = (uint32_t)off;
            lc[li] = i;
            li++;
        }
        off += SH_MAP_REC;
    }
    if (off != sz) goto bad5;

    m->data = data;
    m->size = sz;
    m->w = w;
    m->h = h;
    m->cells = cells;
    m->n = n;
    m->layers = li;
    m->cell_off = co;
    m->layer_off = lo;
    m->layer_cell = lc;
    return 0;

bad5:
    free(data);
    free(co);
    free(lo);
    free(lc);
    return -5;
}

int sh_map_load(const char *path, sh_map *m)
{
    FILE *f;
    long szl;
    size_t sz, got;
    uint8_t *data;

    if (!path || !m) return -1;
    memset(m, 0, sizeof(*m));
    strncpy(m->path, path, SH_MAP_PATH - 1);

    f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    szl = ftell(f);
    if (szl < SH_MAP_HDR + SH_MAP_REC) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    sz = (size_t)szl;

    data = (uint8_t *)malloc(sz);
    if (!data) { fclose(f); return -2; }
    got = fread(data, 1, sz, f);
    fclose(f);
    if (got != sz) { free(data); return -1; }

    return sh_map_adopt(m, data, sz);
}

void sh_map_free(sh_map *m)
{
    if (!m) return;
    free(m->data);
    free(m->cell_off);
    free(m->layer_off);
    free(m->layer_cell);
    memset(m, 0, sizeof(*m));
}

static int cell_idx(const sh_map *m, int x, int y)
{
    if (!m || !m->data) return -2;
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) return -1;
    return y * m->w + x;
}

int sh_map_tile(const sh_map *m, int x, int y)
{
    int c = cell_idx(m, x, y);
    if (c < 0) return c;
    return (int)rd16(m->data + m->cell_off[c] + SH_MREC_TILE);
}

int sh_map_flag(const sh_map *m, int x, int y)
{
    int c = cell_idx(m, x, y);
    if (c < 0) return c;
    return (int)m->data[m->cell_off[c] + SH_MREC_FLAG];
}

int sh_map_sub(const sh_map *m, int x, int y)
{
    int c = cell_idx(m, x, y);
    if (c < 0) return c;
    return (int)m->data[m->cell_off[c] + SH_MREC_SUB];
}

int sh_map_layer_n(const sh_map *m, int x, int y)
{
    int c = cell_idx(m, x, y);
    int k, cnt = 0;
    if (c < 0) return c;
    for (k = 0; k < m->layers; k++)
        if (m->layer_cell[k] == c) cnt++;
    return cnt;
}

int sh_map_layer_tile(const sh_map *m, int x, int y, int k)
{
    int c = cell_idx(m, x, y);
    int i, seen = 0;
    if (c < 0) return c;
    for (i = 0; i < m->layers; i++) {
        if (m->layer_cell[i] != c) continue;
        if (seen == k) return (int)rd16(m->data + m->layer_off[i] + SH_MREC_TILE);
        seen++;
    }
    return -1;
}

int sh_map_set_tile(sh_map *m, int x, int y, int v)
{
    int c = cell_idx(m, x, y);
    if (c < 0) return -1;
    if (v < 0 || v > 65535) return -2;
    wr16(m->data + m->cell_off[c] + SH_MREC_TILE, (uint16_t)v);
    return 0;
}

int sh_map_backup(const sh_map *m, char *out, size_t outsz)
{
    if (!m || !m->data) return -1;
    return make_backup(m->path, out, outsz);
}

int sh_map_write(sh_map *m)
{
    sh_map chk;

    if (!m || !m->data) return -1;
    if (sh_write_file(m->path, m->data, m->size) != 0) return -1;

    if (sh_map_load(m->path, &chk) != 0) return -2;
    if (chk.size != m->size || chk.w != m->w || chk.h != m->h ||
        chk.n != m->n || chk.layers != m->layers) {
        sh_map_free(&chk);
        return -2;
    }
    if (memcmp(chk.data, m->data, m->size) != 0) { sh_map_free(&chk); return -2; }
    sh_map_free(&chk);
    return 0;
}

/* ================================================================== ACT */

int sh_act_load(const char *path, sh_act *a)
{
    FILE *f;
    long szl;
    size_t sz, got;
    uint8_t *data;
    int n;

    if (!path || !a) return -1;
    memset(a, 0, sizeof(*a));
    strncpy(a->path, path, SH_ACT_PATH - 1);

    f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    szl = ftell(f);
    if (szl < SH_ACT_HDR + SH_ACT_REC) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    sz = (size_t)szl;

    data = (uint8_t *)malloc(sz);
    if (!data) { fclose(f); return -2; }
    got = fread(data, 1, sz, f);
    fclose(f);
    if (got != sz) { free(data); return -1; }

    if (memcmp(data, "ACT", 3) != 0 || data[3] != 2 || data[4] != 1) {
        free(data);
        return -3;
    }
    n = (int)data[5];
    if (rd32(data + 7) != SH_ACT_HDR) { free(data); return -3; }
    if (sz != (size_t)SH_ACT_HDR + (size_t)SH_ACT_REC * (size_t)n) {
        free(data);
        return -4;
    }

    a->data  = data;
    a->size  = sz;
    a->count = n;
    return 0;
}

void sh_act_free(sh_act *a)
{
    if (!a) return;
    free(a->data);
    memset(a, 0, sizeof(*a));
}

int sh_act_get(const sh_act *a, int idx, int off, int size)
{
    uint8_t *r;
    if (!a || !a->data) return -2;
    if (idx < 0 || idx >= a->count) return -1;
    if (off < 0 || size < 1 || size > 4 || off + size > SH_ACT_REC) return -1;
    r = a->data + SH_ACT_HDR + (size_t)idx * SH_ACT_REC + off;
    if (size == 1) return (int)r[0];
    if (size == 2) return (int)rd16(r);
    if (size == 4) return (int)rd32(r);
    return -1;
}

int sh_act_set(sh_act *a, int idx, int off, int size, int v)
{
    uint8_t *r;
    if (!a || !a->data) return -2;
    if (idx < 0 || idx >= a->count) return -1;
    if (off < 0 || size < 1 || size > 4 || off + size > SH_ACT_REC) return -1;
    if (size == 1 && (v < 0 || v > 255)) return -2;
    if (size == 2 && (v < 0 || v > 65535)) return -2;
    r = a->data + SH_ACT_HDR + (size_t)idx * SH_ACT_REC + off;
    if (size == 1) r[0] = (uint8_t)v;
    else if (size == 2) wr16(r, (uint16_t)v);
    else wr32(r, (uint32_t)v);
    return 0;
}

int sh_act_backup(const sh_act *a, char *out, size_t outsz)
{
    if (!a || !a->data) return -1;
    return make_backup(a->path, out, outsz);
}

/* 删除一条记录：后续记录整体前移，count 与 size 同步减 38 B，并回写 count 字节 */
int sh_act_delete(sh_act *a, int idx)
{
    if (!a || !a->data || idx < 0 || idx >= a->count) return -1;
    if (a->count <= 1) return -2;   /* 至少保留 1 条，避免 0 长度文件 */
    memmove(a->data + SH_ACT_HDR + (size_t)idx * SH_ACT_REC,
            a->data + SH_ACT_HDR + (size_t)(idx + 1) * SH_ACT_REC,
            (size_t)(a->count - idx - 1) * SH_ACT_REC);
    a->count--;
    a->size -= SH_ACT_REC;
    a->data[5] = (uint8_t)a->count;
    return 0;
}

/* 在 idx 处插入一条记录：缓冲区扩大 38 B，后续记录整体后移，count 与 size 同步加 38 */
int sh_act_insert(sh_act *a, int idx, const uint8_t rec[SH_ACT_REC])
{
    uint8_t *nb;

    if (!a || !a->data || idx < 0 || idx > a->count) return -1;
    if (a->count >= SH_ACT_MAX) return -3;
    nb = (uint8_t *)realloc(a->data, a->size + SH_ACT_REC);
    if (!nb) return -2;
    a->data = nb;
    memmove(a->data + SH_ACT_HDR + (size_t)(idx + 1) * SH_ACT_REC,
            a->data + SH_ACT_HDR + (size_t)idx * SH_ACT_REC,
            (size_t)(a->count - idx) * SH_ACT_REC);
    memcpy(a->data + SH_ACT_HDR + (size_t)idx * SH_ACT_REC, rec, SH_ACT_REC);
    a->count++;
    a->size += SH_ACT_REC;
    a->data[5] = (uint8_t)a->count;
    return 0;
}

int sh_act_write(sh_act *a)
{
    sh_act chk;

    if (!a || !a->data) return -1;
    if (sh_write_file(a->path, a->data, a->size) != 0) return -1;

    if (sh_act_load(a->path, &chk) != 0) return -2;
    if (chk.size != a->size || chk.count != a->count) { sh_act_free(&chk); return -2; }
    if (memcmp(chk.data, a->data, a->size) != 0) { sh_act_free(&chk); return -2; }
    sh_act_free(&chk);
    return 0;
}
