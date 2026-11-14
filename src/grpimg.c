/*
 * grpimg.c -- GRP 图块库解码 + 调色板（关卡页真实地图渲染用）
 *
 * 只用 ANSI C + stdio/malloc（不碰 GDI，Win9x 通用）。
 */
#include "grpimg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int sh_grp_load(const char *path, sh_grp *g)
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
    if (sz < 20) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    g->data = (uint8_t *)malloc((size_t)sz);
    if (!g->data) { fclose(f); return -2; }
    got = fread(g->data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(g->data); g->data = NULL; return -1; }
    g->size = (size_t)sz;

    if (memcmp(g->data, "GRP", 3) != 0) { free(g->data); g->data = NULL; return -3; }
    g->count = (int)rd16(g->data + 5);
    g->table = rd32(g->data + 7);
    if (g->count <= 0 || g->count > 4096) { free(g->data); g->data = NULL; return -4; }
    if ((size_t)g->table + (size_t)g->count * 20 > g->size) {
        free(g->data); g->data = NULL; return -4;
    }
    strncpy(g->path, path, sizeof(g->path) - 1);
    return 0;
}

void sh_grp_free(sh_grp *g)
{
    if (!g) return;
    free(g->data);
    memset(g, 0, sizeof(*g));
}

/* RLE-A / RLE-B 逐行解码（与 image-fmt.ps1 一致） */
static int decode_entry(const uint8_t *b, size_t size, uint32_t off, int w, int h,
                        int flag, uint32_t len, uint8_t **out)
{
    uint8_t *px;
    size_t p;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    if ((size_t)off >= size) return -1;
    px = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (!px) return -1;

    if (flag == 0) {
        if (len != (uint32_t)(w * h) || (size_t)off + len > size) { free(px); return -1; }
        memcpy(px, b + off, (size_t)w * h);
        *out = px;
        return 0;
    }

    p = off;
    {
        int y;
        for (y = 0; y < h; y++) {
            int x = 0;
            uint8_t *row = px + (size_t)y * w;
            while (x < w) {
                uint8_t tag;
                int n, k;
                if (p >= size) { free(px); return -1; }
                tag = b[p];
                if (flag == 1) {
                    if ((tag & 0xC0) == 0xC0) {
                        uint8_t v;
                        n = tag & 0x3F;
                        if (p + 1 >= size) { free(px); return -1; }
                        v = b[p + 1];
                        p += 2;
                        for (k = 0; k < n && x < w; k++) row[x++] = v;
                    } else {
                        row[x++] = tag;
                        p += 1;
                    }
                } else if (flag == 2) {
                    if (tag < 0x80) {
                        n = tag + 1;
                        if (p + 1 + (size_t)n > size) { free(px); return -1; }
                        for (k = 0; k < n && x < w; k++) row[x++] = b[p + 1 + k];
                        p += (size_t)n + 1;
                    } else if (tag == 0x80) {
                        row[x++] = 0x80;
                        p += 1;
                    } else {
                        n = 0x101 - tag;
                        if (p + 1 >= size) { free(px); return -1; }
                        {
                            uint8_t v = b[p + 1];
                            p += 2;
                            for (k = 0; k < n && x < w; k++) row[x++] = v;
                        }
                    }
                } else {
                    free(px);
                    return -1;
                }
            }
        }
    }
    *out = px;
    return 0;
}

int sh_grp_decode(const sh_grp *g, int idx, uint8_t **px, int *w, int *h)
{
    const uint8_t *e;
    uint32_t off, len;
    int ew, eh, flag;

    if (!g || !g->data || idx < 0 || idx >= g->count) return -1;
    e = g->data + g->table + (size_t)idx * 20;
    off  = rd32(e + 0);
    ew   = (int)rd16(e + 6);
    eh   = (int)rd16(e + 8);
    flag = (int)e[14];
    len  = rd32(e + 16);

    if (decode_entry(g->data, g->size, off, ew, eh, flag, len, px) != 0) return -1;
    if (w) *w = ew;
    if (h) *h = eh;
    return 0;
}

int sh_grp_entry_wh(const sh_grp *g, int idx, int *w, int *h)
{
    const uint8_t *e;
    if (!g || !g->data || idx < 0 || idx >= g->count) return -1;
    e = g->data + g->table + (size_t)idx * 20;
    if (w) *w = (int)rd16(e + 6);
    if (h) *h = (int)rd16(e + 8);
    return 0;
}

int sh_pal_load(const char *path, sh_pal *p)
{
    FILE *f;
    size_t got;
    if (!path || !p) return -1;
    f = fopen(path, "rb");
    if (!f) return -1;
    got = fread(p->rgb, 1, 768, f);
    fclose(f);
    return got == 768 ? 0 : -1;
}
