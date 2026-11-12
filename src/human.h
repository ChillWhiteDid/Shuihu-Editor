/*
 * human.h —— 职业技能表访问（技能树用）
 *
 * 数据来自 human_data.c（构建期由 tools/gen_data.py 从 HUMAN.HUM 生成）：
 *   72 个职业记录，每条记录含若干 58 字节子项
 *   子项：技能字符串 id / 学习等级(1..30) / 是否隐藏技
 *
 * 隐藏技每职业恰好 3 段（LV1/LV2/LV3），与存档里 3 字节隐藏技标志块一一对应
 * （第 k 格 = 该职业第 k 条隐藏技，0=已领悟 / 1=未领悟）。
 */
#ifndef SH_HUMAN_H
#define SH_HUMAN_H

/* 职业记录数（当前 72） */
int sh_class_count(void);

/* 职业的基础移动（HUMAN 记录 +9） */
int sh_class_base_move(int cls);

/* 第 class 条记录的技能条数；越界返回 0 */
int sh_class_skill_count(int cls);

/* 第 class 条记录第 i 个技能的：字符串 id / 学习等级 / 是否隐藏技(1/0) */
int sh_class_skill_sid(int cls, int i);
int sh_class_skill_level(int cls, int i);
int sh_class_skill_hidden(int cls, int i);

/* 第 class 条记录里隐藏技的条数（通常 3，无隐藏技则 0） */
int sh_class_hidden_count(int cls);

/* 第 class 条记录第 k 个隐藏技在技能列表中的下标；无则 -1 */
int sh_class_hidden_index(int cls, int k);

/* 构建期嵌入的「原始 HUMAN.HUM」字节（tools/gen_data.py 生成）。
 * 用于导入窗口做原版参照；--stub 构建时 size 为 0。 */
const unsigned char *sh_human_orig(void);
int sh_human_orig_size(void);

/* 子项 +0x06（SE 编号）在原版数据里的使用情况：用它的技能条数、首个技能的 sid */
int sh_se_used_count(int se);
int sh_se_first_sid(int se);
int sh_se_first_class(int se);

#endif /* SH_HUMAN_H */
