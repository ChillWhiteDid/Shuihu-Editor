/*
 * human.c —— 职业技能表访问实现
 */
#include "human.h"

extern const int            SH_CLASS_COUNT;
extern const unsigned short SH_CLASS_OFF[];
extern const unsigned short SH_SK_SID[];
extern const unsigned char  SH_SK_LEVEL[];
extern const unsigned char  SH_SK_HIDDEN[];

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
