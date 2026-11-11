/*
 * goodsfile.h —— GOODS.DAB 道具表读写（V2.1）
 *
 * 格式（分析报告 §4.18）：
 *   11 字节头 + N × 198 字节定长记录，1 基 id（id-1 为下标）
 *   +0   装备类型（1..14；13/14 = 饰品槽）
 *   +6   名称（Big5，构建时已转成 UTF-8 表）
 *   +76  攻击力   u16
 *   +84  防御力   u16
 *   +104 移动加成 s16（有符号，实测存在 -1）
 *   +110 最大体力 u16
 *   +112 最大气力 u16
 *
 * 注意：改的是**游戏本体数据**，所有存档一起受影响，
 *       所以写入前必须备份，写完要重读校验。
 */
#ifndef SH_GOODSFILE_H
#define SH_GOODSFILE_H

#include <stddef.h>
#include <stdint.h>

#define SH_GOODS_HEAD 11
#define SH_GOODS_REC  198
#define SH_GOODS_MAX  200          /* 记录数上限（防御性） */
#define SH_GOODS_PATH 512

typedef enum {
    SH_GF_ATK = 0,   /* +76  u16 攻击力 */
    SH_GF_DEF,       /* +84  u16 防御力 */
    SH_GF_AGI,       /* +96  u16 敏捷（v2.4） */
    SH_GF_CRIT,      /* +100 u16 必殺（v2.4） */
    SH_GF_MOVE,      /* +104 s16 移动加成 */
    SH_GF_HPMAX,     /* +110 u16 最大体力 */
    SH_GF_MPMAX,     /* +112 u16 最大气力 */
    SH_GF_COUNT
} sh_goods_field;

typedef struct {
    char     path[SH_GOODS_PATH];
    uint8_t *data;
    size_t   size;
    int      count;
} sh_goods;

/* 读入整个文件；成功 0，失败负值
 *  -1 打不开  -2 文件太小  -3 内存不足  -4 长度不是 11+n×198（结构异常） */
int  sh_goods_load(const char *path, sh_goods *g);
void sh_goods_free(sh_goods *g);

const char *sh_goods_field_label(sh_goods_field f);
const char *sh_goods_field_key(sh_goods_field f);     /* 命令行用的英文键 */
int  sh_goods_field_off(sh_goods_field f);
int  sh_goods_field_min(sh_goods_field f);
int  sh_goods_field_max(sh_goods_field f);

int  sh_goods_get(const sh_goods *g, int id, sh_goods_field f);
/* 直接按记录内偏移读一个 u16（用于访问未枚举的字段，如 +82/+90） */
int  sh_goods_get_u16(const sh_goods *g, int id, int off);

/* 从存档路径推断 GOODS.DAB：<存档目录>\..\Data\、<存档目录>\Data\、<存档目录>\。
 * 成功返回 1 并写入 out。 */
int  sh_goods_locate(const char *savePath, char *out, size_t n);
/* 只改内存（不落盘）。成功 0；-1 参数非法；-2 超范围 */
int  sh_goods_set(sh_goods *g, int id, sh_goods_field f, int v);

/* 备份到同目录 <原名>_YYYYMMDD-HHMMSS.bak（不覆盖已有文件）；
 * 返回 0 成功，-1 失败。out 可为 NULL。 */
int  sh_goods_backup(const sh_goods *g, char *out, size_t outsz);

/* 原子写入（临时文件 + 改名字），再重读校验大小与所有已改字段。
 * 返回 0 成功；-1 写失败；-2 校验失败。 */
int  sh_goods_write(sh_goods *g);

#endif /* SH_GOODSFILE_H */
