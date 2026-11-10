/*
 * save.c —— 存档解析核心（纯 C，无平台依赖）
 *
 * 坐标系约定（重要）
 *   本实现一律使用「相对锚点」的偏移，与报告 §4.29 的 rel 一致：
 *     锚点 = 「生命当前 / 生命上限 / 气力当前 / 气力上限」四连 u16 的起点
 *     类型 id @ 锚点-28   等级 @ 锚点-4   生命 @ +0/+2   气力 @ +4/+6
 *     攻/防/命/敏/杀 @ +8/+10/+12/+14/+16
 *     经验 @ +54/+56     移动 @ +74      槽区 @ +102（7 格 × 10 字节）
 *     隐藏技标志块：`3C 00 03 00` 之前连续的 0/1 字节
 *   锚点本身等价于单位对象的 +0x40，所以 anchor + rel == unit + (0x40 + rel)。
 *
 * 锚点判定（实测校准）
 *   1. 四个 u16 满足：上限在 30..600，当前 ≤ 上限，当前 > 0，两个上限不相等
 *   2. 锚点-28 的类型 id ≤ 40（排除错位假锚点）
 *   3. 锚点-4 的等级在 1..30
 *   4. +8..+16 的五项属性都 ≤ 255
 *   三条后验条件同时成立才接受，可滤掉绝大多数误匹配。
 *
 * 说明：存档里只保存「本局出现过的角色」，未出场的角色没有记录；
 *       修改器的 16 人下拉框是固定花名册，选中缺席者会提示"没有找到此人物！！"。
 */
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 字段相对锚点的偏移（与报告 rel 同口径） */
static const int s_field_rel[SH_F_COUNT] = {
      -28,  /* SH_F_ID        */
       -4,  /* SH_F_LEVEL     */
        0,  /* SH_F_HP        */
        2,  /* SH_F_HPMAX     */
        4,  /* SH_F_MP        */
        6,  /* SH_F_MPMAX     */
        8,  /* SH_F_ATK       */
       10,  /* SH_F_DEF       */
       12,  /* SH_F_HIT       */
       14,  /* SH_F_AGI       */
       16,  /* SH_F_CRIT      */
       54,  /* SH_F_EXP       */
       56,  /* SH_F_EXPNEXT   */
       74   /* SH_F_MOVE      */
};

static const char *s_field_label[SH_F_COUNT] = {
    "类型id", "等级", "生命", "生命上限", "气力", "气力上限",
    "攻击", "防御", "命中", "敏捷", "必杀",
    "经验", "升级所需", "移动"
};

#define SH_HP_MIN        30
#define SH_HP_MAX        600
#define SH_ID_MAX        40
#define SH_LEVEL_MAX     30
#define SH_STAT_MAX      255
#define SH_MONEY_OFFSET  0x1B
#define SH_SCAN_START    0x100
#define SH_SCAN_GAP      0x60   /* 相邻两个已接受锚点的最小间距（去重） */

static uint16_t rd16(const uint8_t *p, size_t n, size_t off)
{
    if (off + 2 > n) return 0;
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1] << 8));
}

static uint32_t rd32(const uint8_t *p, size_t n, size_t off)
{
    if (off + 4 > n) return 0;
    return (uint32_t)p[off]
         | ((uint32_t)p[off + 1] << 8)
         | ((uint32_t)p[off + 2] << 16)
         | ((uint32_t)p[off + 3] << 24);
}

size_t sh_unit_base(const sh_unit *u)
{
    return u->anchor;   /* anchor-relative */
}

int sh_field_offset(sh_field f)
{
    if (f < 0 || f >= SH_F_COUNT) return 0;
    return s_field_rel[f];
}

const char *sh_field_label(sh_field f)
{
    if (f < 0 || f >= SH_F_COUNT) return "?";
    return s_field_label[f];
}

int sh_is_playable(const sh_unit *u)
{
    return u->id <= 17;
}

/* 找隐藏技标志块：`3C 00 03 00` 之前连续的 0/1 字节 */
static void find_hidden(const uint8_t *p, size_t n, sh_unit *u)
{
    size_t base = sh_unit_base(u);
    size_t from = base + 230;
    size_t to   = base + 270;
    size_t i, start, end;

    u->hidden_len = 0;
    if (to > n) to = n;

    for (i = from; i + 4 <= to; i++) {
        if (p[i] == 0x3C && p[i + 1] == 0x00 && p[i + 2] == 0x03 && p[i + 3] == 0x00) {
            end = i;
            start = end;
            while (start > base && (end - start) < SH_HIDDEN_MAX && p[start - 1] <= 1) {
                start--;
            }
            u->hidden_len = (int)(end - start);
            if (u->hidden_len > 0) {
                memcpy(u->hidden, p + start, (size_t)u->hidden_len);
            }
            return;
        }
    }
}

/* 在偏移 o 处尝试解析一条角色记录；成功返回 1 并填充 u */
static int try_unit(const uint8_t *p, size_t n, size_t o, sh_unit *u)
{
    uint16_t hp, hpmax, mp, mpmax, id, level;
    int k;

    if (o < 28 || o + 8 > n) return 0;

    hp    = rd16(p, n, o);
    hpmax = rd16(p, n, o + 2);
    mp    = rd16(p, n, o + 4);
    mpmax = rd16(p, n, o + 6);

    if (hpmax <= SH_HP_MIN || hpmax >= SH_HP_MAX) return 0;
    if (mpmax <= SH_HP_MIN || mpmax >= SH_HP_MAX) return 0;
    if (hp == 0 || hp > hpmax) return 0;
    if (mp == 0 || mp > mpmax) return 0;
    if (hpmax == mpmax) return 0;

    id = rd16(p, n, o - 28);
    if (id > SH_ID_MAX) return 0;

    level = rd16(p, n, o - 4);
    if (level < 1 || level > SH_LEVEL_MAX) return 0;

    for (k = 0; k < 5; k++) {
        if (rd16(p, n, o + 8 + 2 * (size_t)k) > SH_STAT_MAX) return 0;
    }

    memset(u, 0, sizeof(*u));
    u->anchor = o;
    u->id = id;
    for (k = 0; k < SH_F_COUNT; k++) {
        u->f[k] = rd16(p, n, o + (size_t)s_field_rel[k]);
    }
    for (k = 0; k < SH_SLOT_COUNT; k++) {
        size_t so = o + SH_SLOT_BASE + (size_t)k * SH_SLOT_STRIDE;
        u->slots[k] = (so < n) ? p[so] : 0;
    }
    find_hidden(p, n, u);
    return 1;
}

int sh_load(const char *path, sh_save *s)
{
    FILE *fp;
    long sz;
    size_t o, last = 0;
    int have_last = 0;

    if (!s) return -1;
    memset(s, 0, sizeof(*s));

    fp = fopen(path, "rb");
    if (!fp) return SH_E_OPEN;                 /* 不存在 / 无权限 / 路径错 */
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return SH_E_READ; }
    sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return SH_E_EMPTY; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return SH_E_READ; }

    s->data = (uint8_t *)malloc((size_t)sz);
    if (!s->data) { fclose(fp); return SH_E_NOMEM; }
    if (fread(s->data, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(s->data);
        s->data = NULL;
        return SH_E_READ;
    }
    fclose(fp);
    s->size = (size_t)sz;

    if (s->size < 0x20 || s->data[0] != 'G' || s->data[1] != 'A' || s->data[2] != 'M') {
        sh_free(s);
        return SH_E_NOTSAVE;
    }

    s->money = rd32(s->data, s->size, SH_MONEY_OFFSET);

    for (o = SH_SCAN_START; o + 8 <= s->size && s->count < SH_MAX_UNITS; ) {
        sh_unit tmp;
        if (try_unit(s->data, s->size, o, &tmp)) {
            if (have_last && (o - last) < SH_SCAN_GAP) {   /* 同一条记录的重复匹配 */
                o += 1;
                continue;
            }
            s->units[s->count++] = tmp;
            last = o;
            have_last = 1;
            o += 8;
        } else {
            o += 1;   /* 锚点常为奇数偏移，必须逐字节推进 */
        }
    }

    return 0;
}

void sh_free(sh_save *s)
{
    if (!s) return;
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->size = 0;
    s->count = 0;
}

/* ======================= 编辑（M2） ======================= */

static void wr16(uint8_t *p, size_t n, size_t off, uint16_t v)
{
    if (off + 2 > n) return;
    p[off] = (uint8_t)(v & 0xFF);
    p[off + 1] = (uint8_t)(v >> 8);
}

int sh_find_unit(const sh_save *s, int type_id)
{
    int i, found = -1;
    for (i = 0; i < s->count; i++) {
        if (s->units[i].id == type_id) {
            if (found >= 0) return -2;   /* 重复：不擅自选一个 */
            found = i;
        }
    }
    return found;
}

int sh_field_max(sh_field f)
{
    switch (f) {
    case SH_F_LEVEL:   return SH_LEVEL_MAX;      /* 30  */
    case SH_F_HP:
    case SH_F_HPMAX:
    case SH_F_MP:
    case SH_F_MPMAX:   return 510;               /* 代码里的夹取值 */
    case SH_F_ATK:
    case SH_F_DEF:
    case SH_F_HIT:
    case SH_F_AGI:
    case SH_F_CRIT:    return SH_STAT_MAX;       /* 255 */
    case SH_F_MOVE:    return 255;              /* 单位对象 +0x8A 按 char 写入 */
    case SH_F_EXP:
    case SH_F_EXPNEXT: return 65535;
    default:           return 65535;
    }
}

int sh_set_field(sh_save *s, int idx, sh_field f, uint16_t v)
{
    sh_unit *u;
    uint16_t hp, hpmax, mp, mpmax, old;

    if (!s || idx < 0 || idx >= s->count) return -1;
    if (f < 0 || f >= SH_F_COUNT) return -1;
    if (v > (uint16_t)sh_field_max(f)) return -2;

    u = &s->units[idx];
    old = u->f[f];
    wr16(s->data, s->size, u->anchor + (size_t)s_field_rel[f], v);
    u->f[f] = v;

    /* 维持「当前 ≤ 上限」：越界则回滚内存并报错（调用方据此放弃写入） */
    hp    = u->f[SH_F_HP];
    hpmax = u->f[SH_F_HPMAX];
    mp    = u->f[SH_F_MP];
    mpmax = u->f[SH_F_MPMAX];
    if (hp > hpmax || mp > mpmax) {
        wr16(s->data, s->size, u->anchor + (size_t)s_field_rel[f], old);
        u->f[f] = old;
        return -3;
    }

    return 0;
}

int sh_set_money(sh_save *s, uint32_t v)
{
    if (!s || !s->data) return -1;
    s->data[SH_MONEY_OFFSET + 0] = (uint8_t)(v & 0xFF);
    s->data[SH_MONEY_OFFSET + 1] = (uint8_t)((v >> 8) & 0xFF);
    s->data[SH_MONEY_OFFSET + 2] = (uint8_t)((v >> 16) & 0xFF);
    s->data[SH_MONEY_OFFSET + 3] = (uint8_t)((v >> 24) & 0xFF);
    s->money = v;
    return 0;
}

int sh_set_slot(sh_save *s, int idx, int slot, uint8_t item_id)
{
    sh_unit *u;
    size_t off;

    if (!s || idx < 0 || idx >= s->count) return -1;
    if (slot < 0 || slot >= SH_SLOT_COUNT) return -1;

    u = &s->units[idx];
    off = u->anchor + SH_SLOT_BASE + (size_t)slot * SH_SLOT_STRIDE;
    if (off >= s->size) return -1;

    s->data[off] = item_id;
    u->slots[slot] = item_id;
    return 0;
}

/* 解锁隐藏必杀技：把标志块清零（0 = 已领悟）。
 * slots > 0 时只清「标记前最后 slots 格」（= 该职业隐藏技条数），
 * slots <= 0 时清空整个回溯到的块。
 * 返回实际清零的格数；0 表示没找到标志块。 */
int sh_unlock_hidden(sh_save *s, int idx, int slots)
{
    sh_unit *u;
    size_t base, from, to, i, start, end;

    if (!s || idx < 0 || idx >= s->count) return -1;
    u = &s->units[idx];
    base = u->anchor;
    from = base + 230;
    to   = base + 270;
    if (to > s->size) to = s->size;

    for (i = from; i + 4 <= to; i++) {
        if (s->data[i] == 0x3C && s->data[i + 1] == 0x00 &&
            s->data[i + 2] == 0x03 && s->data[i + 3] == 0x00) {

            size_t cnt, clear_from, cleared;

            end = i;
            start = end;
            while (start > base && (end - start) < SH_HIDDEN_MAX && s->data[start - 1] <= 1) {
                start--;
            }
            if (start == end) return 0;       /* 没有标志块 */

            cnt = end - start;
            clear_from = start;
            if (slots > 0 && (size_t)slots < cnt) {
                clear_from = end - (size_t)slots;   /* 只清最后 slots 格 */
            }
            cleared = end - clear_from;

            memset(s->data + clear_from, 0, cleared);  /* 清零 = 已领悟 */
            u->hidden_len = (int)cleared;
            memset(u->hidden, 0, sizeof(u->hidden));
            return (int)cleared;
        }
    }
    return 0;
}