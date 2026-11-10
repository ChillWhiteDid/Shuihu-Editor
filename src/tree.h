/*
 * tree.h —— 技能树文本生成（GUI 与 CLI 共用）
 */
#ifndef SH_TREE_H
#define SH_TREE_H

#include "save.h"

/* 把某角色的技能树写成文本；返回写入长度。cap 为缓冲区大小（建议 >= 8192） */
int sh_build_tree_text(const sh_unit *u, char *out, int cap);

#endif /* SH_TREE_H */
