/*
 * names.h —— 名字表访问（数据由 tools/gen_data.py 生成的 names_data.c 提供）
 */
#ifndef SH_NAMES_H
#define SH_NAMES_H

/* 类型 id -> 角色名；未知返回 "?" */
const char *sh_char_name(int id);

/* 字符串 id -> 技能名；未知返回 "?" */
const char *sh_skill_name(int id);

/* 表容量（供界面做下拉/校验） */
int sh_char_name_count(void);
int sh_skill_name_count(void);

#endif /* SH_NAMES_H */
