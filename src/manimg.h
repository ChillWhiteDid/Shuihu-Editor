/*
 * manimg.h -- MAN.LIB 人物精灵（关卡页地图上显示单位模型用）
 *
 * 依据分析报告 §4.36.3 的 LIB 8B 双层区间表：
 *   dataEnd 起：表A = u16@+9 条（MAN: 72），每项 4×u16 的 [start,end] 铺满表B 下标
 *              表B = u16@+7 条（MAN: 1620），每项 [start,end] 铺满文件条目
 *   所以：职业/形象 cls(0..71) → 表A 得「动作组」区间 → 取第一个动作组 → 表B 得「条目」区间
 *         → 取该组第一帧 = 站立帧（已用 0..47 类视觉对照验证：16=魯智深、18=王母、34=弓箭手…）
 *   条目仍是 LIB 的 19 B 尾部索引：+0 off / +4 W / +8 H / +14 flag / +15 len
 */
#ifndef SH_MANIMG_H
#define SH_MANIMG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void   *fp;        /* FILE* */
    uint8_t *idx;      /* 19 B 索引表（count*19） */
    uint8_t *tblA;     /* 72*8 */
    uint8_t *tblB;     /* 1620*8 */
    int     count;
    int     na, nb;
    int     table;     /* 索引表文件偏移 */
} sh_man;

/* 打开 MAN.LIB（只读索引与两张 8B 表，条目载荷按需 seek 读）；成功 0 */
int  sh_man_open(const char *path, sh_man *m);
void sh_man_close(sh_man *m);

/* 取「形象 cls」的站立帧：成功 0，像素写入 *px（调用方 free），尺寸 *w/*h，条目号 *entry */
int  sh_man_sprite(sh_man *m, int cls, uint8_t **px, int *w, int *h, int *entry);

#endif /* SH_MANIMG_H */
