/*
 * goods.h —— 道具表访问（M5）
 *
 * 数据来自 items_data.c（构建期由 tools/gen_data.py 从 GOODS.DAB 生成）：
 * 名字在生成时已由 Big5 解成 UTF-8，运行时不再依赖 950 代码页
 * （Win98 简体中文版通常没有 950，这点很重要）。
 *
 * id 为 **1 基**：1 = GOODS 记录 0；0 表示空槽。
 *
 * GOODS 记录 +0 的分类码（实测取值 0..24，按存档里的实际用法归纳）：
 *   0..12  武器/拳套类（如 5 鐵拳、1 皮手套、60 四竅八環刀）
 *   13     衣服       （如 103 熟皮甲、84 布衣、112 雁翎圈金甲）
 *   14     帽子       （如 127 紅纓氈笠、113 一字巾、134 鳳翅金盔）
 *   >=15   消耗品/特殊（如 177 人肉包子、192 金蠱蠱毒粉、150 古武術祕笈）
 */
#ifndef SH_GOODS_H
#define SH_GOODS_H

#define SH_KIND_WEAPON  0
#define SH_KIND_ARMOR   1
#define SH_KIND_HAT     2
#define SH_KIND_ITEM    3

/* 道具总数（当前 200） */
int sh_item_count(void);

/* id(1 基) -> 名字；越界返回 "" */
const char *sh_item_name(int id);

/* id(1 基) -> 分类码（GOODS 记录 +0） */
int sh_item_eqtype(int id);

/* id(1 基) -> 大类（GOODS 记录 +1） */
int sh_item_major(int id);

/* 道具的移动加成（有符号，可为负） */
int sh_item_move(int id);

/* 道具说明文本（记录 +26 起的 Big5，构建时转 UTF-8）；无说明返回 "" */
const char *sh_item_desc(int id);

/* id(1 基) -> 归一化类别（SH_KIND_*） */
int sh_item_kind(int id);

/* 类别 -> 中文标签 */
const char *sh_kind_label(int kind);

/* 槽序号(0..6) -> 期望类别：0 武器 / 1 衣服 / 2 帽子 / 3..6 物品 */
int sh_slot_kind(int slot);

#endif /* SH_GOODS_H */
