/*
 * mapfile.h -- 关卡数据：棋盘 MAP 与布阵表 ACT 读写（V2.3）
 *
 * 格式（分析报告 §4.11.1 / §4.11.3 / §4.19 / §4.36.2，均已 63/63 闭合）：
 *
 * MAP:
 *   9 B 头 = 'M','A','P',0 + u8 ver(=1) + u16 w + u16 h
 *   之后 n x 5 B 记录，size == 9 + 5n，且 n >= w*h
 *   记录流 = w*h 个「格记录」+ 穿插在格之间的「附加层记录」：
 *       for (i = 0; i < w*h; i++) {
 *           rec = read5();                    // 该格的记录
 *           while (rec[+2] & 2) rec = read5(); // 该格还有附加层，链式继续
 *       }
 *   5 B 记录：+0 u16 tileId(全库 0..1017) / +2 u8 flag(只取 0/2)
 *             +3 u8 sub(只取 0/0x40/0x80/0xC0) / +4 u8 恒 0
 *
 * ACT:
 *   11 B 头 = 'A','C','T' + u8 ver(=2) + u8 sub(=1) + u8 n + u8 0 + u32 dataOffset(=11)
 *   n x 38 B 记录，size == 11 + 38n（n 范围 4..103，全库 63 图共 2,228 条）
 *   字段见 SH_AREC_*；+6/+8 是格坐标，63/63 严格落在对应 MAP 的 w x h 内
 *
 * 这两个文件是**游戏本体数据**：改了对所有存档、所有新开局都生效，写入前必须备份。
 */
#ifndef SH_MAPFILE_H
#define SH_MAPFILE_H

#include <stddef.h>
#include <stdint.h>

#define SH_MAP_HDR      9
#define SH_MAP_REC      5
#define SH_MAP_PATH     512
#define SH_MAP_MAX_W    255
#define SH_MAP_MAX_H    255
#define SH_MAP_MAX_TILE 1017    /* 全库实测最大值；工具允许到 65535，仅作提示用 */

#define SH_ACT_HDR      11
#define SH_ACT_REC      38
#define SH_ACT_PATH     512
#define SH_ACT_MAX      255

/* MAP 5 B 记录内的字段偏移 */
#define SH_MREC_TILE    0       /* u16 图块 id */
#define SH_MREC_FLAG    2       /* u8  bit1 = 该格后还有附加层 */
#define SH_MREC_SUB     3       /* u8  0/0x40/0x80/0xC0 */
#define SH_MREC_TAIL    4       /* u8  恒 0 */

/* ACT 38 B 记录内的字段偏移（分析报告 §4.19，U18 已结案） */
#define SH_AREC_TYPE    0x00    /* u16 单位类型 id        */
#define SH_AREC_F2      0x02    /* u16 -> 单位 +0x26      */
#define SH_AREC_F4      0x04    /* u8  0..3 -> 单位 +0x122 */
#define SH_AREC_TEAM    0x05    /* u8  队别 0 我 / 1 中 / 2 敌 / 4 特殊 */
#define SH_AREC_X       0x06    /* u16 格坐标 x */
#define SH_AREC_Y       0x08    /* u16 格坐标 y */
#define SH_AREC_FA      0x0A    /* u16 第三坐标分量（实测恒 0） */
#define SH_AREC_LEVEL   0x0C    /* u16 布置等级 0..42 */
#define SH_AREC_FLAG    0x0E    /* u8  布尔 -> 单位标志位 0x20000 */
#define SH_AREC_ACTION  0x0F    /* u8  动作/状态码 0..9 */
#define SH_AREC_F10     0x10    /* u16 -> 单位 +0x96 */
#define SH_AREC_F12     0x12    /* u32 -> 单位 +0x98 */
#define SH_AREC_F16     0x16    /* u16 -> 单位 +0x9C */
#define SH_AREC_SLOT0   0x18    /* 7 x u16 物品/装备槽（1 基道具 id，0=空） */
#define SH_AREC_SLOTN   7

typedef struct {
    char     path[SH_MAP_PATH];
    uint8_t *data;
    size_t   size;
    int      w, h;
    int      n;          /* 记录总数 */
    int      cells;      /* w * h */
    int      layers;     /* n - cells：附加层总数 */
    uint32_t *cell_off;  /* cells 项：每个格记录在 data 内的偏移 */
    uint32_t *layer_off; /* layers 项：附加层记录偏移 */
    int      *layer_cell;/* layers 项：该附加层挂在哪个格（0..cells-1） */
} sh_map;

typedef struct {
    char     path[SH_ACT_PATH];
    uint8_t *data;
    size_t   size;
    int      count;
} sh_act;

/* ---- MAP ----
 * 读入并校验（9+5n、n>=w*h、记录流恰好铺满）；成功 0，失败负值
 *   -1 打不开/太短  -2 内存不足  -3 魔数或版本不对  -4 尺寸/长度不自洽  -5 记录流走不完 */
int  sh_map_load(const char *path, sh_map *m);
/* 从内存缓冲接管一份 MAP（撤销/重做用；成功后 data 归 m 所有，失败时已释放） */
int  sh_map_adopt(sh_map *m, uint8_t *data, size_t sz);
void sh_map_free(sh_map *m);

int  sh_map_tile(const sh_map *m, int x, int y);            /* -1 越界；-2 未载入 */
int  sh_map_flag(const sh_map *m, int x, int y);
int  sh_map_sub(const sh_map *m, int x, int y);
int  sh_map_layer_n(const sh_map *m, int x, int y);         /* 该格的附加层条数 */
int  sh_map_layer_tile(const sh_map *m, int x, int y, int k);

/* 只改内存。成功 0；-1 参数/越界；-2 值超 u16 */
int  sh_map_set_tile(sh_map *m, int x, int y, int v);

/* 备份到同目录 <原名>_YYYYMMDD-HHMMSS.bak（不覆盖已有文件）；成功 0 */
int  sh_map_backup(const sh_map *m, char *out, size_t outsz);
/* 原子写入 + 重读校验（长度、w/h、每格 tile、每格附加层数与 tile）；成功 0，-2 校验不一致 */
int  sh_map_write(sh_map *m);

/* ---- ACT ---- */
int  sh_act_load(const char *path, sh_act *a);              /* 同上，-3 魔数/版本，-4 长度 */
void sh_act_free(sh_act *a);

/* size = 1/2/4 字节的字段读写；off 为记录内偏移 */
int  sh_act_get(const sh_act *a, int idx, int off, int size);
int  sh_act_set(sh_act *a, int idx, int off, int size, int v);

int  sh_act_backup(const sh_act *a, char *out, size_t outsz);
int  sh_act_write(sh_act *a);
/* 删除一条记录（内存内移动，记录数与文件长度都减 38 B；写入前需自行确认） */
int  sh_act_delete(sh_act *a, int idx);
/* 在 idx 处插入一条 38 B 记录（idx = count 表示追加；文件长度 +38 B） */
int  sh_act_insert(sh_act *a, int idx, const uint8_t rec[SH_ACT_REC]);

#endif /* SH_MAPFILE_H */
