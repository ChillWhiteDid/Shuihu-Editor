/*
 * grpimg.h -- GRP 图块库解码 + 调色板（关卡页真实地图渲染用）
 *
 * 依据分析报告 §4.17 / §4.34.3 与已有的 image-fmt.ps1 实现（同算法移植到 C）：
 *   GRP 头：'GRP' + u16 count@5 + u32 条目表偏移@7
 *   条目 20 B：+0 u32 载荷偏移 / +6 u16 W / +8 u16 H /
 *              +10 i16 锚点X / +12 i16 锚点Y / +14 u8 编码 / +16 u32 载荷长度
 *   编码：0 = 原样 W×H 字节；1 = RLE-A；2 = RLE-B（都逐行推进，行间不补齐）
 *   像素 = 8bpp 调色板索引（0 视为透明）
 */
#ifndef SH_GRPIMG_H
#define SH_GRPIMG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char     path[512];
    uint8_t *data;
    size_t   size;
    int      count;
    uint32_t table;
} sh_grp;

typedef struct {
    uint8_t rgb[256][3];
} sh_pal;

/* 读入 GRP；成功 0，失败负值（-1 打不开 -2 内存 -3 不是 GRP -4 结构不自洽） */
int  sh_grp_load(const char *path, sh_grp *g);
void sh_grp_free(sh_grp *g);

/* 解码条目 idx（0 基）：成功 0，像素指针写 px（调用方 free），尺寸写 w/h；
 * 失败 -1（越界/损坏）。 */
int  sh_grp_decode(const sh_grp *g, int idx, uint8_t **px, int *w, int *h);

/* 只读条目头里的宽高（不解码载荷）；成功 0 */
int  sh_grp_entry_wh(const sh_grp *g, int idx, int *w, int *h);

/* 768 B 调色板；成功 0 */
int  sh_pal_load(const char *path, sh_pal *p);

#endif /* SH_GRPIMG_H */
