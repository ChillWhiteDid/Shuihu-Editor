/*
 * human.c —— 职业技能表访问实现
 */
#include "human.h"

extern const int            SH_CLASS_COUNT;
extern const unsigned short SH_CLASS_OFF[];
extern const unsigned short SH_SK_SID[];
extern const unsigned char  SH_SK_LEVEL[];
extern const unsigned char  SH_SK_HIDDEN[];
extern const unsigned char  SH_CLASS_MOVE[];
extern const unsigned char  SH_HUMAN_ORIG[];
extern const int            SH_HUMAN_ORIG_SIZE;
extern const unsigned char  SH_SE_COUNT[100];
extern const unsigned short SH_SE_FIRST[100];
extern const unsigned char  SH_SE_CLASS[100];

/* SE 编号（子项 +0x06）的原始使用统计：多少条技能用它、首个技能的字符串 id */
int sh_se_used_count(int se)
{
    if (se < 0 || se > 99) return 0;
    return (int)SH_SE_COUNT[se];
}

int sh_se_first_sid(int se)
{
    if (se < 0 || se > 99) return -1;
    return (int)SH_SE_FIRST[se];
}

int sh_se_first_class(int se)
{
    if (se < 0 || se > 99) return -1;
    return (int)SH_SE_CLASS[se];
}

/* 构建期嵌入的原始 HUMAN.HUM（gen_data.py 从游戏数据生成；stub 构建时 size=0） */
const unsigned char *sh_human_orig(void)
{
    return SH_HUMAN_ORIG;
}

int sh_human_orig_size(void)
{
    return SH_HUMAN_ORIG_SIZE;
}

int sh_class_count(void)
{
    return SH_CLASS_COUNT;
}

static int valid(int cls, int i)
{
    int lo, hi;
    if (cls < 0 || cls >= SH_CLASS_COUNT) return 0;
    lo = (int)SH_CLASS_OFF[cls];
    hi = (int)SH_CLASS_OFF[cls + 1];
    if (i < 0 || lo + i >= hi) return 0;
    return 1;
}

/* 职业的基础移动（HUMAN 记录 +9，单字节） */
int sh_class_base_move(int cls)
{
    if (cls < 0 || cls >= SH_CLASS_COUNT) return 0;
    return (int)SH_CLASS_MOVE[cls];
}

int sh_class_skill_count(int cls)
{
    if (cls < 0 || cls >= SH_CLASS_COUNT) return 0;
    return (int)SH_CLASS_OFF[cls + 1] - (int)SH_CLASS_OFF[cls];
}

int sh_class_skill_sid(int cls, int i)
{
    if (!valid(cls, i)) return -1;
    return (int)SH_SK_SID[SH_CLASS_OFF[cls] + i];
}

int sh_class_skill_level(int cls, int i)
{
    if (!valid(cls, i)) return 0;
    return (int)SH_SK_LEVEL[SH_CLASS_OFF[cls] + i];
}

int sh_class_skill_hidden(int cls, int i)
{
    if (!valid(cls, i)) return 0;
    return (int)SH_SK_HIDDEN[SH_CLASS_OFF[cls] + i];
}

int sh_class_hidden_count(int cls)
{
    int i, n = 0, cnt = sh_class_skill_count(cls);
    for (i = 0; i < cnt; i++) {
        if (sh_class_skill_hidden(cls, i)) n++;
    }
    return n;
}

int sh_class_hidden_index(int cls, int k)
{
    int i, n = 0, cnt = sh_class_skill_count(cls);
    if (k < 0) return -1;
    for (i = 0; i < cnt; i++) {
        if (sh_class_skill_hidden(cls, i)) {
            if (n == k) return i;
            n++;
        }
    }
    return -1;
}
