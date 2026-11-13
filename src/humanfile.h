/*
 * humanfile.h —— HUMAN.HUM 职业表读写（V2.1）
 *
 * 格式（分析报告 §4.20）：
 *   13 字节头：'HUM\0' + u8 版本(1) + u16 count(72) + u16 recLen(51) + u32 头长(13)
 *   之后 72 条：51 B 定长记录 + n×58 B 技能子项（n 在记录 +0x2D）
 *   总长 13 + 72×51 + 302×58 = 21,201，尾部剩余 0
 *
 * 51 B 记录里可编辑的字段（REA 反编译 0x408540 确认）：
 *   +5  / +3   最大体力 基础 / 成长率%
 *   +7  / +4   最大气力 基础 / 成长率%
 *   +0xA/ +0xC 攻击     基础 / 成长率%
 *   +0xD/ +0xF 防御     基础 / 成长率%
 *   +0x10/+0x12 命中    基础 / 成长率%
 *   +0x13/+0x15 敏捷    基础 / 成长率%
 *   +0x16/+0x18 必殺    基础 / 成长率%
 *   +9          基础移动
 *   +0x19       可装备主类型（只读展示）
 *   +0x1D       未定，取值全是 50 的倍数（只读展示）
 *
 * 注意：改的是**游戏本体数据**，所有存档一起受影响；写入前必须备份。
 */
#ifndef SH_HUMANFILE_H
#define SH_HUMANFILE_H

#include <stddef.h>
#include <stdint.h>

#define SH_HUMAN_HEAD   13
#define SH_HUMAN_REC    51
#define SH_HUMAN_SUB    58
#define SH_HUMAN_MAX    72
#define SH_HUMAN_SIZE   21201
#define SH_HUMAN_PATH   512

/* 7 组「基础值 + 成长率」；移动单列 */
typedef enum {
    SH_HF_HP = 0,
    SH_HF_MP,
    SH_HF_ATK,
    SH_HF_DEF,
    SH_HF_HIT,
    SH_HF_AGI,
    SH_HF_CRIT,
    SH_HF_COUNT
} sh_human_field;

typedef struct {
    char     path[SH_HUMAN_PATH];
    uint8_t *data;
    size_t   size;
    int      count;
    uint32_t off[SH_HUMAN_MAX];   /* 每条记录在 data 里的起始偏移 */
} sh_human;

/* 读入整个文件；成功 0，失败负值
 *  -1 打不开  -2 文件太小  -3 内存不足
 *  -4 头/步长不对（不是 HUMAN.HUM 或已损坏）  -5 记录走完对不上文件长度 */
int  sh_human_load(const char *path, sh_human *h);
/* 从内存缓冲解析（会自己 malloc 一份副本；用于构建期嵌入的原始数据） */
int  sh_human_load_mem(const uint8_t *buf, size_t size, sh_human *h);
void sh_human_free(sh_human *h);

const char *sh_human_field_label(sh_human_field f);   /* 最大体力…必殺 */
const char *sh_human_field_key(sh_human_field f);     /* hp/mp/atk/def/hit/agi/crit */

/* 记录 id 合法范围 0..count-1 */
int  sh_human_base(const sh_human *h, int id, sh_human_field f);
int  sh_human_growth(const sh_human *h, int id, sh_human_field f);
int  sh_human_move(const sh_human *h, int id);
int  sh_human_skill_count(const sh_human *h, int id);
int  sh_human_u8(const sh_human *h, int id, int off);
int  sh_human_u16(const sh_human *h, int id, int off);

/* ---- 58 B 技能子项（每职业 n 条，n = 记录 +0x2D） ----
 * 子项内偏移：+0x00 u16 技能字符串 id、+0x05 隐藏标志(0/1)、+0x11 学习等级(1..30)
 * 注意：子项个数 n 属于文件布局，本模块只读不写。 */
#define SH_HUMAN_SKILL_MAX 64

int  sh_human_skill_off(const sh_human *h, int id, int i);   /* data 内偏移；非法 -1 */
int  sh_human_skill_sid(const sh_human *h, int id, int i);
int  sh_human_skill_level(const sh_human *h, int id, int i);
int  sh_human_skill_hidden(const sh_human *h, int id, int i);
int  sh_human_skill_cat(const sh_human *h, int id, int i);    /* +0x02 技能类别 */
int  sh_human_skill_cost(const sh_human *h, int id, int i);   /* +0x12 气力消耗 u16 */
int  sh_human_skill_range(const sh_human *h, int id, int i);  /* +0x10 范围宽度（格） */
int  sh_human_skill_aoe(const sh_human *h, int id, int i);    /* +0x06 范围半径：0=单体 */
int  sh_human_skill_reach(const sh_human *h, int id, int i);  /* +0x0D 射程（格） */
/* --- 战斗参数（报告 §4.33.1 / §4.33.3） --- */
int  sh_human_skill_status(const sh_human *h, int id, int i); /* +0x0A 附加状态1概率 % 0..100 */
int  sh_human_skill_hit(const sh_human *h, int id, int i);    /* +0x14 命中修正（有符号：负值减命中） */
int  sh_human_skill_crit(const sh_human *h, int id, int i);   /* +0x16 暴击修正 */
int  sh_human_skill_seg(const sh_human *h, int id, int i, int seg, int field);
                                              /* +0x1C 起 4 段表：field 0=触发帧 1=伤害倍率% 2=方向 */
int  sh_human_set_skill_status(sh_human *h, int id, int i, int v);  /* 0..100 */
int  sh_human_set_skill_hit(sh_human *h, int id, int i, int v);     /* -999..999 */
int  sh_human_set_skill_crit(sh_human *h, int id, int i, int v);    /* 0..999 */
int  sh_human_set_skill_seg(sh_human *h, int id, int i, int seg, int field, int v); /* 0..9999 */
int  sh_human_skill_hidden_count(const sh_human *h, int id);
int  sh_human_skill_level_max(void);   /* 30 */

/* 只改内存；成功 0，参数非法 -1，超范围 -2 */
int  sh_human_set_skill_sid(sh_human *h, int id, int i, int v);
int  sh_human_set_skill_level(sh_human *h, int id, int i, int v);
int  sh_human_set_skill_hidden(sh_human *h, int id, int i, int v);
int  sh_human_set_skill_cost(sh_human *h, int id, int i, int v);   /* 0..999 */
int  sh_human_set_skill_cat(sh_human *h, int id, int i, int v);    /* 0..11 类别 */
int  sh_human_set_skill_range(sh_human *h, int id, int i, int v);  /* 0..99 格 */
int  sh_human_set_skill_aoe(sh_human *h, int id, int i, int v);    /* 0..999；0=单体 */
int  sh_human_set_skill_reach(sh_human *h, int id, int i, int v);  /* 0..99 格 */

/* 整条导入：把 src 职业 src_cls 的第 src_i 条 58 字节原样复制到
 * dst 职业 dst_cls 的第 dst_i 条（同一文件内也允许，内部用 memmove）。
 * 成功 0，参数/越界 -1。 */
int  sh_human_skill_copy(sh_human *dst, int dst_cls, int dst_i,
                         const sh_human *src, int src_cls, int src_i);

/* 只改内存。成功 0，失败负值：-1 参数非法，-2 超范围 */
int  sh_human_set_base(sh_human *h, int id, sh_human_field f, int v);
int  sh_human_set_growth(sh_human *h, int id, sh_human_field f, int v);
int  sh_human_set_move(sh_human *h, int id, int v);

/* 上限（界面提示用） */
int  sh_human_base_max(void);     /* 65535 */
int  sh_human_growth_max(void);   /* 255 */
int  sh_human_move_max(void);     /* 255 */

/* 从存档路径推导 HUMAN.HUM：<存档目录>\..\Data\ 、<存档目录>\Data\ 、<存档目录>\ */
int  sh_human_locate(const char *savePath, char *out, size_t n);

/* 备份到同目录 <原名>_YYYYMMDD-HHMMSS.bak（不覆盖已有文件） */
int  sh_human_backup(const sh_human *h, char *out, size_t outsz);

/* 原子写入 + 重读校验（大小、count、每条的 15 个可编辑字段） */
int  sh_human_write(sh_human *h);

#endif /* SH_HUMANFILE_H */