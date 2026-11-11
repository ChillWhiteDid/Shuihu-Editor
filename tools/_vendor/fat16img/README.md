# `_vendor\fat16img` —— FAT16 核心的内置副本

`tools\imgfs.py` 是 [fat16img](../../../../../tools/fat16img/README.md) 的兼容 shim。
它按顺序找核心：

1. `REA\tools\fat16img\fat16img.py`（工作区实时核心，开发时改这里）
2. `tools\_vendor\fat16img\fat16img.py`（本目录，仓库自带快照）

所以 `shuihu-edit` 单独 `git clone` 出来、或 `ShuiHu\` 被移出 REA 工作区时，
`imgfs.py`、`vmtest.ps1`、`imgfs-test.ps1` 仍然可用。

## 目录内容

| 文件 | 说明 |
|---|---|
| `fat16img.py` | 核心副本，与 `REA\tools\fat16img\fat16img.py` 逐字节相同 |
| `presets\win9x.json` | `win9x` 保护预设（`/WINDOWS`、`/PROGRAM FILES`、根系统文件） |

`presets\` 必须和 `fat16img.py` 同目录 —— 核心按 `__file__` 旁边的 `presets\` 查找预设。

## 同步（改了实时核心之后）

在 `ShuiHu\shuihu-edit` 下执行：

```powershell
copy ..\..\tools\fat16img\fat16img.py        tools\_vendor\fat16img\fat16img.py
copy ..\..\tools\fat16img\presets\win9x.json tools\_vendor\fat16img\presets\win9x.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools\doctor.ps1
```

`doctor.ps1` 的「内置 core 同步」一项会比较两份的 SHA-256：

- `[OK]` 一致，或当前没有实时核心（独立仓库模式）；
- `[?]` 不一致 —— 说明实时核心已更新但忘了同步，按上面的 copy 命令处理。

## 注意

- 本副本只随仓库分发，**不要**直接改它；改动请在 `REA\tools\fat16img\` 做，再同步。
- 核心版本以 `REA\tools\fat16img\fat16img.py` 为准；本目录没有独立的版本号。