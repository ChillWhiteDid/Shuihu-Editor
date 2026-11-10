# Logo —— 水字朱印

《水浒传之梁山好汉》存档编辑器的标识。主体字为「水」，取水浒之「水」、梁山泊之「水」。

## 设计

| 项 | 说明 |
|---|---|
| 形式 | 圆形印章，白文「水」（朱红底、纸色字），双细环 |
| 字形 | 楷体（simkai）—— 有笔锋而不做作，接近明清水浒版画的标题字 |
| 气质 | 老派、克制，不做现代扁平化 |

配色：

| 色 | 值 | 用途 |
|---|---|---|
| 朱印红 | `#9E1B1B` | 印面、标题 |
| 宣纸米 | `#F2E8D5` | 印文 |
| 暗金 | `#C9A227` | 组合标分隔线 |

选印章的理由：它是水浒题材最直接的符号，且**在 16×16 下仍可辨**——应用图标的第一要求。

## 小尺寸处理

16/24/32 px 单独绘制**简化印**：

- 去掉内侧细环（缩小后会糊成一团）
- 字形放大到 0.66 倍并加粗描边
- 去掉印泥斑驳（缩小时变成脏点）

48 px 以上才用带双环与斑驳的精细版。`logo.ico` 内两种版本按尺寸混装。

## 文件

| 文件 | 用途 |
|---|---|
| `logo-1024.png` … `logo-16.png` | 多尺寸 PNG 导出 |
| `logo.ico` | 程序图标（16/24/32 简化版 + 48/64/128/256 精细版） |
| `logo-lockup.png` | 横向组合标：印章 + 中英文字 |
| `logo-sizes.png` | 16/24/32/48/64 实际像素预览 |
| `design_logo.py` | 生成脚本 |

## 重新生成

```bat
python design_logo.py
```

4 倍超采样后缩小，边缘干净。改 `CRIMSON` / `PAPER` / `GOLD` 可整体换色；
改 `FONT_CANDS` 可换字形。

## 在程序里使用

程序**不做资源内嵌**（TCC 无法编译 `.rc`），而是**运行时读取**：

```c
GetModuleFileNameA  -> 取 exe 目录
LoadImageA(NULL, "<目录>\logo.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE)  // ICON_BIG
LoadImageA(NULL, "<目录>\logo.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE)  // ICON_SMALL
WM_SETICON
```

- `logo.ico` 与 exe 同目录即生效
- 文件缺失时**静默降级**为无图标（实测 `GetLastError = 2`），不影响任何功能
- Win98 支持 `LR_LOADFROMFILE`
