/*
 * manimg.c -- MAN.LIB 人物精灵（关卡页地图上显示单位模型用）
 *
 * 打开时只读文件头 + 19 B 索引表 + 两张 8B 区间表（约 500 KB），
 * 精灵载荷按需 fseek/fread，避免把 51 MB 的 MAN.LIB 整个读进内存（Win9x 友好）。
 */
#include "manimg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int sh_man_open(const char *path, sh_man *m)
{
    FILE *f;
    long size;
    uint8_t head[15];
    int count, na, nb;
    uint32_t x;

    if (!path || !m) return -1;
    memset(m, 0, sizeof(*m));

    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(head, 1, 15, f) != 15) { fclose(f); return -1; }
    if (memcmp(head, "LIB", 3) != 0) { fclose(f); return -1; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    size = ftell(f);
    if (size < 1000) { fclose(f); return -1; }

    count = (int)rd16(head + 5);
    x = rd32(head + 7);
    nb = (int)(x & 0xFFFF);
    na = (int)(x >> 16);
    if (count <= 0 || count > 65535 || na < 0 || nb <= 0) { fclose(f); return -1; }
    if ((long)count * 19 + 15 + (long)(na + nb) * 8 > size) { fclose(f); return -1; }

    m->count = count;
    m->na = na;
    m->nb = nb;
    m->table = (int)(size - (long)count * 19);
    m->fp = f;

    m->idx = (uint8_t *)malloc((size_t)count * 19);
    m->tblA = (uint8_t *)malloc((size_t)(na > 0 ? na : 1) * 8);
    m->tblB = (uint8_t *)malloc((size_t)nb * 8);
    if (!m->idx || !m->tblA || !m->tblB) { sh_man_close(m); return -2; }

    if (fseek(f, m->table, SEEK_SET) != 0 ||
        fread(m->idx, 19, (size_t)count, f) != (size_t)count) { sh_man_close(m); return -1; }
    {
        long base = (long)m->table - (long)(na + nb) * 8;
        if (base < 0) { sh_man_close(m); return -1; }
        if (fseek(f, base, SEEK_SET) != 0) { sh_man_close(m); return -1; }
        if (na > 0 && fread(m->tblA, 8, (size_t)na, f) != (size_t)na) { sh_man_close(m); return -1; }
        if (fread(m->tblB, 8, (size_t)nb, f) != (size_t)nb) { sh_man_close(m); return -1; }
    }
    return 0;
}

void sh_man_close(sh_man *m)
{
    if (!m) return;
    if (m->fp) fclose((FILE *)m->fp);
    free(m->idx);
    free(m->tblA);
    free(m->tblB);
    memset(m, 0, sizeof(*m));
}

/* 条目 → 索引像素（RLE-A / RLE-B 逐行解码） */
static int decode_entry(FILE *f, const uint8_t *e, uint8_t **out, int *ow, int *oh)
{
    uint32_t off;
    int w, h, flag, len, y;
    uint8_t *buf, *px;

    off  = rd32(e + 0);
    w    = (int)rd16(e + 4);
    h    = (int)rd16(e + 8);
    flag = (int)e[14];
    len  = (int)rd32(e + 15);   /* LIB/PIC：flag 单字节@14，长度是 u32@15（不是单字节） */
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return -1;
    if (len <= 0 || len > 262144) return -1;

    buf = (uint8_t *)malloc((size_t)len);
    px = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (!buf || !px) { free(buf); free(px); return -2; }
    if (fseek(f, (long)off, SEEK_SET) != 0 || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); free(px); return -1;
    }
    memset(px, 0, (size_t)w * (size_t)h);

    if (flag == 0) {
        if (len < w * h) { free(buf); free(px); return -1; }
        memcpy(px, buf, (size_t)w * h);
        free(buf);
        *out = px; *ow = w; *oh = h;
        return 0;
    }

    {
        int p = 0;
        for (y = 0; y < h; y++) {
            int x = 0;
            uint8_t *row = px + (size_t)y * w;
            while (x < w) {
                uint8_t tag;
                int nn, k;
                if (p >= len) { free(buf); free(px); return -1; }
                tag = buf[p];
                if (flag == 1) {
                    if ((tag & 0xC0) == 0xC0) {
                        nn = tag & 0x3F;
                        if (p + 1 >= len) { free(buf); free(px); return -1; }
                        for (k = 0; k < nn && x < w; k++) row[x++] = buf[p + 1];
                        p += 2;
                    } else {
                        row[x++] = tag;
                        p += 1;
                    }
                } else {
                    if (tag < 0x80) {
                        nn = tag + 1;
                        if (p + 1 + nn > len) { free(buf); free(px); return -1; }
                        for (k = 0; k < nn && x < w; k++) row[x++] = buf[p + 1 + k];
                        p += nn + 1;
                    } else if (tag == 0x80) {
                        row[x++] = 0x80;
                        p += 1;
                    } else {
                        nn = 0x101 - tag;
                        if (p + 1 >= len) { free(buf); free(px); return -1; }
                        for (k = 0; k < nn && x < w; k++) row[x++] = buf[p + 1];
                        p += 2;
                    }
                }
            }
        }
    }
    free(buf);
    *out = px; *ow = w; *oh = h;
    return 0;
}

int sh_man_sprite(sh_man *m, int cls, uint8_t **px, int *w, int *h, int *entry)
{
    int g, g0, g1, e0, e1;
    const uint8_t *e;

    if (!m || !m->fp || !m->idx || cls < 0 || cls >= m->na) return -1;
    g0 = (int)rd16(m->tblA + (size_t)cls * 8 + 0);
    g1 = (int)rd16(m->tblA + (size_t)cls * 8 + 2);
    g = g0;
    if (g < 0 || g >= m->nb) return -1;
    e0 = (int)rd16(m->tblB + (size_t)g * 8 + 0);
    e1 = (int)rd16(m->tblB + (size_t)g * 8 + 2);
    (void)g1; (void)e1;
    if (e0 < 0 || e0 >= m->count) return -1;
    e = m->idx + (size_t)e0 * 19;
    if (entry) *entry = e0;
    return decode_entry((FILE *)m->fp, e, px, w, h);
}
