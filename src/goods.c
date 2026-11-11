/*
 * goods.c —— 道具表访问实现（M5）
 */
#include "goods.h"

extern const int          SH_ITEM_COUNT;
extern const char        *SH_ITEM_NAMES[];
extern const unsigned char SH_ITEM_EQ[];
extern const unsigned char SH_ITEM_MAJOR[];
extern const signed char   SH_ITEM_MOVE[];
extern const char *SH_ITEM_DESC[];

int sh_item_count(void)
{
    return SH_ITEM_COUNT;
}

const char *sh_item_name(int id)
{
    if (id < 1 || id > SH_ITEM_COUNT) return "";
    return SH_ITEM_NAMES[id - 1];
}

int sh_item_eqtype(int id)
{
    if (id < 1 || id > SH_ITEM_COUNT) return 0;
    return (int)SH_ITEM_EQ[id - 1];
}

int sh_item_major(int id)
{
    if (id < 1 || id > SH_ITEM_COUNT) return 0;
    return (int)SH_ITEM_MAJOR[id - 1];
}

int sh_item_kind(int id)
{
    int e;
    if (id < 1 || id > SH_ITEM_COUNT) return SH_KIND_ITEM;
    e = (int)SH_ITEM_EQ[id - 1];
    if (e >= 15) return SH_KIND_ITEM;
    if (e == 13) return SH_KIND_ARMOR;
    if (e == 14) return SH_KIND_HAT;
    return SH_KIND_WEAPON;
}

const char *sh_kind_label(int kind)
{
    switch (kind) {
    case SH_KIND_WEAPON: return "武器";
    case SH_KIND_ARMOR:  return "衣服";
    case SH_KIND_HAT:    return "帽子";
    default:             return "物品";
    }
}

/* 道具的移动加成（GOODS 记录 +104，有符号：实测存在 -1） */
int sh_item_move(int id)
{
    if (id < 1 || id > SH_ITEM_COUNT) return 0;
    return (int)SH_ITEM_MOVE[id - 1];
}

const char *sh_item_desc(int id)
{
    if (id < 1 || id > SH_ITEM_COUNT) return "";
    return SH_ITEM_DESC[id - 1];
}

int sh_slot_kind(int slot)
{
    if (slot == 0) return SH_KIND_WEAPON;
    if (slot == 1) return SH_KIND_ARMOR;
    if (slot == 2) return SH_KIND_HAT;
    return SH_KIND_ITEM;
}
