/*
 * save.h —— 《水浒传之梁山好汉》存档解析（核心层，不依赖任何 GUI/平台头）
 *
 * 依据：ShuiHu-分析报告.md v1.33 §4.26–4.32
 *   - 存档无校验和，可原位等长改写
 *   - 金钱 = u32 LE @ 0x1B
 *   - 角色记录：锚点 = 「生命cur/生命max/气力cur/气力max」四连 u16 的起点
 *               记录基址 = 锚点 - 0x40
 *   - 角色一律靠锚点扫描定位，**不能顺序遍历**（记录长度不固定）
 */
#ifndef SH_SAVE_H
#define SH_SAVE_H

#include <stdint.h>
#include <stddef.h>

#define SH_MAX_UNITS   256
#define SH_SLOT_COUNT  7
#define SH_HIDDEN_MAX  8
#define SH_SLOT_BASE    102   /* 槽区相对锚点的偏移 */
#define SH_SLOT_STRIDE  10    /* 每格字节数 */

/* 字段枚举；值与 sh_field_offset() 一一对应 */
typedef enum {
    SH_F_ID = 0,   /* 类型 id（0=武龍 … 17=劉唐，>=18 为 NPC） */
    SH_F_LEVEL,    /* 等级 1..30 */
    SH_F_HP,       /* 生命当前 */
    SH_F_HPMAX,    /* 生命上限 */
    SH_F_MP,       /* 气力当前 */
    SH_F_MPMAX,    /* 气力上限 */
    SH_F_ATK,      /* 攻击 */
    SH_F_DEF,      /* 防御 */
    SH_F_HIT,      /* 命中 */
    SH_F_AGI,      /* 敏捷 */
    SH_F_CRIT,     /* 必杀 */
    SH_F_EXP,      /* 经验 */
    SH_F_EXPNEXT,  /* 升级所需 */
    SH_F_MOVE,     /* 移动力 */
    SH_F_COUNT
} sh_field;

typedef struct {
    size_t   anchor;                 /* 锚点绝对偏移（用于写回时定位） */
    uint16_t id;                     /* 类型 id */
    uint16_t f[SH_F_COUNT];          /* 各字段值 */
    uint8_t  slots[SH_SLOT_COUNT];   /* 槽内 +0 = 1 基道具 id，0=空 */
    uint8_t  hidden[SH_HIDDEN_MAX];  /* 隐藏技标志块原样（0=已领悟，1=未领悟） */
    int      hidden_len;             /* 0 表示未找到标志块 */
} sh_unit;

typedef struct {
    uint8_t *data;                   /* 整个存档（写回时原地改） */
    size_t   size;
    uint32_t money;
    sh_unit  units[SH_MAX_UNITS];
    int      count;                  /* 实际解析到的单位数 */
} sh_save;

/* 读入并解析；成功返回 0，失败返回下列负值 */
#define SH_E_OPEN     (-1)   /* 打不开：文件不存在 / 路径错 / 无权限 */
#define SH_E_NOMEM    (-2)   /* 内存不足 */
#define SH_E_NOTSAVE  (-3)   /* 打开成功但不是存档（魔数不是 GAM） */
#define SH_E_EMPTY    (-4)   /* 文件为空 */
#define SH_E_READ     (-5)   /* 读取失败 */
int  sh_load(const char *path, sh_save *s);

/* 释放内部缓冲（不释放 s 本身） */
void sh_free(sh_save *s);

/* 是否可编辑角色（类型 id <= 17） */
int  sh_is_playable(const sh_unit *u);

/* 字段的用途字符串（中文，供界面/CLI 使用） */
const char *sh_field_label(sh_field f);

/* 字段相对**锚点**的字节偏移（报告中的 rel 口径）；未知返回 0 */
int  sh_field_offset(sh_field f);

/* 取记录基址；本实现返回锚点本身（anchor-relative 坐标） */
size_t sh_unit_base(const sh_unit *u);

/* ---- 编辑（M2）---- */

/* 按类型 id 找单位下标；找不到 -1，重复 -2 */
int sh_find_unit(const sh_save *s, int type_id);

/* 写入一个字段；成功 0，失败负值：
 *   -1 下标/字段非法  -2 值超范围  -3 会破坏 当前<=上限 的关系 */
/* 移动加成的相对偏移（unit+0x64） */
#define SH_MOVE_BONUS_REL 36
/* 本回合剩余移动力（unit+0x8C）：棋盘实际可走格数，回合开始时由 unit+0x8A 复制 */
#define SH_MOVE_LEFT_REL  76

int sh_set_field(sh_save *s, int idx, sh_field f, uint16_t v);
/* 同 sh_set_field，但不检查「当前 ≤ 上限」（批量写入用，调用方需自行校验） */
int sh_set_field_raw(sh_save *s, int idx, sh_field f, uint16_t v);

/* 写入金钱 */
int sh_set_money(sh_save *s, uint32_t v);

/* 写入第 slot 格（0..6）的道具 id（1 基，0=空） */
int sh_set_slot(sh_save *s, int idx, int slot, uint8_t item_id);

/* 写槽位的 4 个装备缓存（+2/+4/+6/+8 = GOODS +76/+82/+84/+90）。
 * 调用方负责从 GOODS 取值；道具被"获得"时游戏就是这么填的。 */
int sh_slot_cache_set4(sh_save *s, int idx, int slot, const unsigned short v[4]);

/* 槽位里的装备加成缓存（游戏按这俩值算面板，见 FUN_00408540 第 105 行）：
 *   which = SH_CACHE_ATK   -> 槽+2（攻击）
 *   which = SH_CACHE_DEF   -> 槽+6（防御）
 * 面板攻击/防御 = 基础 + 三个装备槽的缓存之和（游戏内夹到 255）。 */
#define SH_CACHE_ATK 0
#define SH_CACHE_DEF 1

int sh_slot_cache_get(const sh_save *s, int idx, int slot, int which);
int sh_slot_cache_set(sh_save *s, int idx, int slot, int which, int v);

/* 解锁该角色的隐藏必杀技：把 `3C 00 03 00` 之前的连续 0/1 字节清零 */
int sh_unlock_hidden(sh_save *s, int idx, int slots);

/* 字段的取值范围上限（供界面/CLI 校验）；未知返回 65535 */
int sh_field_max(sh_field f);

#endif /* SH_SAVE_H */
