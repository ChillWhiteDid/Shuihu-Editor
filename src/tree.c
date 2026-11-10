/*
 * tree.c —— 技能树文本生成
 *
 * 数据来源
 *   职业技能表：HUMAN.HUM（human.c 访问，构建期内嵌）
 *   技能名    ：技能字符串表（names.c，索引 = 子项 +0）
 *   隐藏技状态：存档里该角色的隐藏技标志块（0=已领悟 1=未领悟）
 *
 * 隐藏技每职业恰好 3 段（LV1/LV2/LV3），第 k 格对应第 k 段。
 */
#include "tree.h"
#include "human.h"
#include "names.h"

#include <stdio.h>
#include <string.h>

#define SH_MAX_SKILLS 64

static void sort_by_level(int cls, int *idx, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        int key = idx[i];
        int kl = sh_class_skill_level(cls, key);
        j = i - 1;
        while (j >= 0 && sh_class_skill_level(cls, idx[j]) > kl) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }
}

int sh_build_tree_text(const sh_unit *u, char *out, int cap)
{
    int cls = (int)u->id;
    int n = sh_class_skill_count(cls);
    int idx[SH_MAX_SKILLS];
    int i, pos = 0, hidN, k;

    if (!out || cap <= 0) return 0;
    out[0] = '\0';

    if (n > SH_MAX_SKILLS) n = SH_MAX_SKILLS;
    for (i = 0; i < n; i++) idx[i] = i;

    pos += sprintf(out + pos,
        "%s（类型 id %d）\r\n"
        "职业技能表：记录 %d，共 %d 条    当前等级 %u\r\n",
        sh_char_name(cls), cls, cls, n, (unsigned)u->f[SH_F_LEVEL]);
    if (pos > cap - 64) { out[cap - 1] = '\0'; return pos; }

    if (n == 0) {
        pos += sprintf(out + pos, "\r\n（该职业没有技能数据）\r\n");
        return pos;
    }

    sort_by_level(cls, idx, n);

    /* ---- 普通必杀技 ---- */
    pos += sprintf(out + pos, "\r\n【普通必杀技】按学习等级排列\r\n");
    {
        int shown = 0;
        for (i = 0; i < n; i++) {
            int si = idx[i];
            const char *note = "";
            if (sh_class_skill_hidden(cls, si)) continue;
            if (si == 0) note = "   ←默认技，面板不显示";
            pos += sprintf(out + pos, "  Lv %2d   %s%s\r\n",
                           sh_class_skill_level(cls, si),
                           sh_skill_name(sh_class_skill_sid(cls, si)), note);
            shown++;
            if (pos > cap - 128) { out[cap - 1] = '\0'; return pos; }
        }
        if (shown == 0) pos += sprintf(out + pos, "  （无）\r\n");
    }

    /* ---- 隐藏必杀技 ---- */
    hidN = sh_class_hidden_count(cls);
    pos += sprintf(out + pos, "\r\n【隐藏必杀技】需「古武術祕笈」，该职业共 %d 段\r\n", hidN);
    if (hidN == 0) {
        pos += sprintf(out + pos, "  （该职业没有隐藏技）\r\n");
    } else {
        /* 标志块的格数 = 该职业隐藏技条数；真正的格是「标记前最后 hidN 字节」
         * （回溯时可能把更早的 0 也带进来，故用尾部对齐） */
        int hbase = (u->hidden_len >= hidN) ? (u->hidden_len - hidN) : 0;
        for (k = 0; k < hidN; k++) {
            int si = sh_class_hidden_index(cls, k);
            const char *st;
            if (si < 0) continue;
            if (hbase + k < u->hidden_len) {
                st = (u->hidden[hbase + k] == 0) ? "已领悟" : "未领悟";
            } else if (u->hidden_len == 0) {
                st = "未领悟（存档无标志块）";
            } else {
                st = "状态未知";
            }
            pos += sprintf(out + pos, "  Lv %2d   %s    %s\r\n",
                           sh_class_skill_level(cls, si),
                           sh_skill_name(sh_class_skill_sid(cls, si)), st);
        }
        {
            int first = -1;
            pos += sprintf(out + pos, "\r\n存档标志块：");
            if (u->hidden_len <= 0) {
                pos += sprintf(out + pos,
                    "（无）\r\n"
                    "  该角色在存档里没有隐藏技标志块 —— 通常是还没学过任何隐藏必杀技。\r\n"
                    "  提示：标志块的位置目前只在武龍等少数角色上确认过，\r\n"
                    "        其他角色若位置不同，本工具既不能显示也不能清零。\r\n");
            } else {
                for (k = hbase; k < hbase + hidN && k < u->hidden_len; k++) {
                    if (u->hidden[k] != 0 && first < 0) first = k;
                }
                if (first < 0) {
                    pos += sprintf(out + pos, "全 0（全部已领悟，共 %d 格）\r\n", hidN);
                } else {
                    for (k = first; k < hbase + hidN && k < u->hidden_len; k++)
                        pos += sprintf(out + pos, "%u", (unsigned)u->hidden[k]);
                    pos += sprintf(out + pos, "\r\n");
                }
                pos += sprintf(out + pos,
                    "  每格对应上面一段（0=已领悟 1=未领悟）。\r\n"
                    "  「解锁隐藏技」会把它们全部清零，且不消耗道具。\r\n");
            }
        }
    }
    if (pos > cap - 1) pos = cap - 1;
    out[pos] = '\0';
    return pos;
}