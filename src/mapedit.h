/*
 * mapedit.h -- 「关卡编辑」页（MAP 棋盘 / ACT 布阵表）（V2.3）
 *
 * 独立成模块，避免在 ui.c（4000+ 行）里再插大段代码。
 * 页面是主窗口的子窗口，自己创建并管理控件。
 */
#ifndef SH_MAPEDIT_H
#define SH_MAPEDIT_H

#include <windows.h>

/* 创建页面（隐藏状态）；rc 为主窗口客户区矩形（用于定位）。
 * 成功返回页面窗口句柄，失败返回 NULL。 */
HWND sh_mapedit_create(HWND parent, HINSTANCE hi, const RECT *rc);

/* 显示/隐藏页面（配合页签切换） */
void sh_mapedit_show(HWND page, int show);

/* 页面是否已创建 */
int  sh_mapedit_alive(void);

/* 自检：返回失败项数（0 = 全过）。无样本文件时返回 0 并输出 SKIP。
 * 只在临时副本上写，不碰游戏目录。 */
int  sh_mapedit_selftest(HWND parent);

#endif /* SH_MAPEDIT_H */
