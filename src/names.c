/*
 * names.c —— 名字表访问实现
 *
 * names_data.c 由 tools/gen_data.py 生成，内容是从 ShuiHu.org 里提取的
 * 两张字符串表（Big5 解码后以 UTF-8 字面量保存）：
 *   SH_CHAR_NAMES[]  索引 = 类型 id
 *   SH_SKILL_NAMES[] 索引 = 武功记录 +0 的字符串 id
 */
#include "names.h"

extern const int   SH_CHAR_NAMES_count;
extern const char *SH_CHAR_NAMES[];
extern const int   SH_SKILL_NAMES_count;
extern const char *SH_SKILL_NAMES[];

const char *sh_char_name(int id)
{
    if (id < 0 || id >= SH_CHAR_NAMES_count) return "?";
    return SH_CHAR_NAMES[id];
}

const char *sh_skill_name(int id)
{
    if (id < 0 || id >= SH_SKILL_NAMES_count) return "?";
    return SH_SKILL_NAMES[id];
}

int sh_char_name_count(void)
{
    return SH_CHAR_NAMES_count;
}

int sh_skill_name_count(void)
{
    return SH_SKILL_NAMES_count;
}
