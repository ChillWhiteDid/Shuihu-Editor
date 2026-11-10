# shuihu-edit —— 《水浒传之梁山好汉》存档编辑器

面向 **Windows 98 实机**与 Windows 10/11 的存档编辑工具。
数据格式依据见 [`../ShuiHu-分析报告.md`](../ShuiHu-分析报告.md) v1.38；实现约定见 [`../开发说明.md`](../开发说明.md)。

**状态：V1.7**，Win98 实机验收通过（读 / 改 / 写 / 备份 / 恢复全流程）。
**许可证：[MIT](LICENSE)** —— 仅覆盖本仓库原创内容，不含游戏本体与游戏数据。

> **平台限制：本仓库只在 Windows 上构建与运行**（Win98 / 2000 / XP / 10 / 11）。
> 工具链为 TinyCC / MinGW-w64 的 Windows 版本，UI 是 Win32 原生控件，不使用 Linux/macOS 下可编译的跨平台库。

---

## 零、五分钟跑起来

> **命令在哪执行**：本文命令除注明 `powershell` 外，都在 **cmd.exe** 里执行。
> PowerShell 里设环境变量要写 `$env:SHUIHU_DATA = "C:\ShuiHu"`，用 `set` 是无效的。

```bat
:: 1) 生成数据表（有游戏就提取完整数据）
python tools\gen_data.py "C:\ShuiHu" src

::    没有游戏时，只能生成空表先把代码编过（名字显示 ?，不是可用版本）：
::    python tools\gen_data.py --stub src

:: 2) 构建（需要 TinyCC，见「二、外部依赖一览」）
build\build-tcc.bat

:: 3) 运行（用空表编的版本，界面里看不到名字）
bin\shuihu-edit-GUI.exe
```

发布用的打包在 `release\`（已被 `.gitignore` 排除，需自行拷贝分发）；
图标源文件在 `assets\logo\logo.ico`。

## 一、目录结构

```
shuihu-edit/
├─ src/                      源码（纯 C，无 C++、无第三方库）
│  ├─ save.c / save.h        存档解析与编辑核心（不依赖 windows.h，可单独回归）
│  ├─ fileio.c / fileio.h    备份（backup_save\）与安全写入
│  ├─ goods.c / goods.h      道具表访问
│  ├─ human.c / human.h      职业技能表访问（技能树用）
│  ├─ tree.c / tree.h        技能树文本生成（GUI 与 CLI 共用）
│  ├─ textconv.c / textconv.h UTF-8 -> 系统 ANSI（整段一次转换）
│  ├─ names.c / names.h      角色名 / 技能名访问
│  ├─ ui.c                   Win32 GUI
│  ├─ cli.c                  命令行前端
│  ├─ names_data.c           【生成】角色名 40 条 + 技能名 256 条
│  ├─ items_data.c           【生成】道具 200 条（名字 + 分类码）
│  ├─ human_data.c           【生成】72 个职业 × 302 条技能（等级/隐藏标志）
│  └─ comdlg32.def           【TCC 用】comdlg32 导入定义
├─ build/
│  ├─ build-tcc.bat          TinyCC 构建 → Win98 实机用
│  └─ build-w11.bat          MinGW-w64 构建 → Win10/11 用
├─ tools/
│  ├─ gen_data.py            从游戏数据生成三个 *_data.c
│  ├─ oracle.py              save.c 的 Python 孪生实现（无编译器时验证算法）
│  ├─ selftest.ps1           回归自检（19 项）
│  ├─ doctor.ps1             环境自检（依赖/数据/产物/样本）
│  ├─ doccheck.ps1           文档一致性自检（16 项断言）
│  └─ reportcheck.ps1        对账提醒（提交关键词 ↔ 分析报告）
├─ bin/                      构建输出（可随时删除，构建脚本会重建）
├─ assets/logo/              Logo 源文件与多尺寸导出（见其 README）
├─ LICENSE                  MIT（仅覆盖原创内容，见「十八、许可」）
├─ .gitignore               排除构建产物与游戏数据生成物
├─ .gitattributes           换行与二进制资产处理
└─ release/                  发布包（**不入库**，见「十四、仓库说明」；发布时自行打包）
```

工具链不在本目录，位于工作区 `tools\`：

| 工具链 | 路径 | 用途 |
|---|---|---|
| TinyCC 0.9.27 | `..\..\tools\tcc\` | Win98 构建 |
| MinGW-w64 GCC 10.4.0 | `..\..\tools\mingw\mingw32\` | Win10/11 构建 |

---

## 二、外部依赖一览

仓库本身只含源码、构建脚本、文档与 Logo。**运行和构建所需的外部东西都在这里列清楚。**

> 首次准备需要**联网**（下载 Python 与 TinyCC）。准备完成后可完全离线构建与运行。

### 运行环境

| 依赖 | 何时需要 | 说明 |
|---|---|---|
| **Python 3.6+** | 只在「生成数据」这一步 | `tools\gen_data.py` 只用标准库，无需 pip；**构建和运行 exe 都不需要 Python** |

### 构建工具链（二选一）

| 工具链 | 用途 | 默认查找顺序 |
|---|---|---|
| **TinyCC 0.9.27** | 产出 Win98 + Win11 通用 exe（推荐） | ① `SHUIHU_TCC` 环境变量 ② `..\..\tools\tcc\tcc\tcc.exe` ③ PATH 上的 `tcc` |
| **MinGW-w64**（i686, msvcrt） | 仅 Win10/11 开发调试 | ① `SHUIHU_GCC` 环境变量 ② `..\..\tools\mingw\mingw32\bin\gcc.exe` ③ PATH 上的 `gcc` |

TinyCC 还需要完整 Windows 头文件，用 `SHUIHU_WINAPI` 指向其 `include\winapi` 目录：

- 编译器：<https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win32-bin.zip>
- 头文件：<https://download.savannah.gnu.org/releases/tinycc/winapi-full-for-0.9.27.zip>

设置示例（两种 shell 写法不同，别混用）：

```bat
:: cmd.exe
set SHUIHU_TCC=C:\tcc\tcc.exe
set SHUIHU_WINAPI=C:\tcc\include\winapi
build\build-tcc.bat
```

```powershell
# PowerShell
$env:SHUIHU_TCC    = 'C:\tcc\tcc.exe'
$env:SHUIHU_WINAPI = 'C:\tcc\include\winapi'
build\build-tcc.bat
```

winapi 包解压后应得到 `<解压目录>\include\winapi\windows.h`，
`SHUIHU_WINAPI` 指向这个 **`include\winapi`** 目录（不是 `include`，也不是 `windows.h` 本身）。

> 为什么不用 MinGW 做正式版：它的 CRT 启动代码调用 `GetModuleHandleW` / `VirtualProtect`，
> Win9x 没有这些接口，实机会直接报「执行了非法操作」。

### 游戏数据（不随仓库分发）

需要**你自己拥有的**游戏副本，共三个文件（`ShuiHu.org`、`Data\GOODS.DAB`、
`Data\HUMAN.HUM`）。提取方式、路径怎么给、没有游戏时怎么编，
见下一节 **三、构建前提**。

没有游戏也能编译（空表，名字显示 `?`）：

```bat
python tools\gen_data.py --stub src
```

### 回归测试用的存档样本（可选）

`tools\selftest.ps1` 分两组：

- **A 组 通用**：任意合法存档都能跑（干跑、金钱、越界拦截、备份、长度不变）
- **B 组 样本专属**：依赖已知样本的固定偏移，**哈希匹配才执行**，否则跳过

样本查找顺序：命令行参数 → 环境变量 `SHUIHU_DATA`（指向游戏根目录）→ 仓库默认布局 `<repo>\..\..\ShuiHu\`。

```bat
:: 全自动
powershell -NoProfile -ExecutionPolicy Bypass -File tools\selftest.ps1

:: 指定存档（B 组会因哈希不符而跳过）
powershell ... -File tools\selftest.ps1 -Save "D:\my\save.sav"

:: 指定游戏目录
set SHUIHU_DATA=C:\ShuiHu
```

B 组还会用到一份「游戏内已验证」的对比基准 `probe_stats.sav`，默认在 `<repo>\..\out\`，
不存在则跳过该项。
## 三、构建前提：游戏数据怎么给

### 机制：编译期一次性提取，运行期不再需要游戏

程序把三类表**编译进 exe**，所以运行时完全不访问游戏目录——你把 exe 拷到任何一台
没有装游戏的机器上都能用。

```
游戏文件 ──gen_data.py──► src\*_data.c ──编译器──► exe（内含全部名字）
   ↑ 只在构建时读一次                          ↑ 运行时只读写 .sav 存档
```

需要从游戏里取的三样东西：

| 文件 | 位置 | 提取出的内容 | 生成的源文件 |
|---|---|---|---|
| `ShuiHu.org` | 游戏根目录 | 角色名 40 条、技能名 256 条 | `src\names_data.c` |
| `Data\GOODS.DAB` | 游戏目录 | 200 件道具的名字与分类 | `src\items_data.c` |
| `Data\HUMAN.HUM` | 游戏目录 | 72 个职业 × 302 条技能 | `src\human_data.c` |

后两个自动从 `ShuiHu.org` 所在目录下的 `Data\` 找，不用单独指定。

| 阶段 | 谁执行 | 需要游戏目录吗 | 需要 `SHUIHU_DATA` 吗 |
|---|---|---|---|
| **生成数据** | `tools\gen_data.py`（手动跑一次） | ✅ 需要，它要读文件 | 可选（`--auto` 时用） |
| **编译** | `build\build-tcc.bat` | ❌ **不需要** | ❌ **不读** |
| **运行** | `bin\shuihu-edit-GUI.exe` | ❌ **不需要** | ❌ **不读** |

### 游戏数据放哪里

原则：**放在仓库外面**。工具只要一个路径，不假设你把它放哪。

> **关键：这个路径只在「生成数据」那一步用一次。**
> 生成完 `src\*_data.c` 之后，编译就只读这些 `.c` 文件——**构建脚本从不访问游戏目录，
> 也不读 `SHUIHU_DATA`**。所以"放哪"只影响你敲 `gen_data.py` 那一条命令时写的路径。

**必需的只有三个文件，共约 0.4 MB**（整个游戏目录不必拷）：

```
<任意目录>\
├─ ShuiHu.org          304 KB   ← 角色名、技能名
└─ Data\
   ├─ GOODS.DAB       38.7 KB   ← 200 件道具
   └─ HUMAN.HUM       20.7 KB   ← 72 个职业技能表
```

`Data\` 这一层**不能省**：脚本按 `ShuiHu.org` 所在目录去找 `Data\`。
只要这三个文件齐，提取结果与完整游戏目录**逐字节相同**（已验证）。

#### 三种常见布局

**A. 直接用游戏安装目录**（最省事，不用复制）

```
C:\ShuiHu\
├─ ShuiHu.org
├─ Data\GOODS.DAB
├─ Data\HUMAN.HUM
└─ …（游戏其它文件）

set SHUIHU_DATA=C:\ShuiHu
python tools\gen_data.py --auto src
```

**B. 只抽三个文件做成"数据夹"**（可放 U 盘 / 网盘，换机器只带 0.4 MB）

```
D:\shuihu-data\
├─ ShuiHu.org
└─ Data\
   ├─ GOODS.DAB
   └─ HUMAN.HUM

set SHUIHU_DATA=D:\shuihu-data
python tools\gen_data.py --auto src
```

**C. 多人协作 / CI**：数据放仓库外，用环境变量注入

```
D:\work\shuihu-edit\     ← 仓库，只有代码
D:\work\ci-data\         ← 三个文件
set SHUIHU_DATA=D:\work\ci-data
```

#### 千万不要放进仓库

`.gitignore` 已做兜底（`ShuiHu.org`、`GOODS.DAB`、`HUMAN.HUM`、`/Data/`、`/gamedata/`），
但**不要依赖它**：一旦被 `git add -f` 或改过规则就会混进去，而这类文件一旦进了历史，
清理起来很麻烦。养成"数据在仓库外"的习惯。
### `gen_data.py` 用法摘要

```
python tools\gen_data.py <路径> <输出目录>            从游戏提取完整数据
python tools\gen_data.py <路径> src --allow-partial   允许缺 GOODS.DAB / HUMAN.HUM 继续
python tools\gen_data.py --auto   <输出目录>          用 SHUIHU_DATA 或默认布局定位
python tools\gen_data.py --stub   <输出目录>          生成空表（0 条）
```

- `<路径>` 可以是 `ShuiHu.org` 文件，也可以是游戏目录（自动取其 `ShuiHu.org`）
- 输出目录**必须给**，通常就是 `src`；重复执行会**直接覆盖**同名文件
- 退出码：`0` 成功，`1` 缺 Data 文件（未加 `--allow-partial`），`2` 参数或路径错误

### 三种给路径的方式

**① 直接给路径**（最直白）—— 给文件或给**目录**都行

```bat
python tools\gen_data.py "C:\ShuiHu\ShuiHu.org" src    :: 指向 ShuiHu.org
python tools\gen_data.py "C:\ShuiHu" src               :: 给目录，自动取其中的 ShuiHu.org
```

路径含空格时**必须加引号**，例如 `"D:\My Games\ShuiHu"`。

**② 用环境变量 `SHUIHU_DATA`**（推荐，省得每次敲）

```bat
set SHUIHU_DATA=C:\ShuiHu
python tools\gen_data.py --auto src
```

`--auto` 会去 `%SHUIHU_DATA%\ShuiHu.org` 找；找不到会明确告诉你它试过哪里。
这个变量和 `tools\selftest.ps1` 共用，设一次两边都省事。

**③ 相对路径**

```bat
cd /d C:\ShuiHu
python D:\repos\shuihu-edit\tools\gen_data.py ShuiHu.org D:\repos\shuihu-edit\src
```

### 生成物里不会留下你的路径

`*_data.c` 头部只记录**文件名**：

```c
/* 由 tools/gen_data.py 自动生成，请勿手工编辑。 */
/* 来源: ShuiHu.org */
```

不含绝对路径，提交仓库不会泄露目录结构（这三个文件本来也进了 `.gitignore`）。

### 没有游戏时

```bat
python tools\gen_data.py --stub src
```

生成空表，代码可以编译运行，但**所有名字显示为 `?`、道具下拉框为空、技能树为空**。
数值编辑功能照常可用。这条路径只用于验证仓库完整性，不能当作正式版本使用。

### 在一台没有游戏的机器上编译，会发生什么

**不会产生半成品**——两道检查都在"真正开始编译"之前，`bin\` 目录甚至不会被创建。

第 1 道：找不到编译器

```
[ERROR] TinyCC not found.
  Set SHUIHU_TCC to your tcc.exe, or install it so that "tcc" is on PATH.
    tcc      : https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win32-bin.zip
    headers  : https://download.savannah.gnu.org/releases/tinycc/winapi-full-for-0.9.27.zip
```

第 2 道：编译器就绪，但缺生成的数据表

```
[ERROR] generated sources are missing:
          src\names_data.c  src\items_data.c  src\human_data.c

  Two ways to create them:

  1) full data, from YOUR OWN copy of the game (needs ShuiHu.org +
     Data\GOODS.DAB + Data\HUMAN.HUM):
         python tools\gen_data.py "C:\path\to\ShuiHu.org" src

  2) placeholder tables, so the code compiles without the game
      (all names show as "?"):
         python tools\gen_data.py --stub src
```

此时你有三种选择：

| 选择 | 命令 | 结果 |
|---|---|---|
| **装游戏，完整提取** | 见上面的 1) | 功能完整的正式版 |
| **只求能编过** | `gen_data.py --stub src` | 能跑、能改数，名字显示 `?` |
| **先不编** | — | 仓库内容本身可读，文档说明格式 |

如果只给了 `ShuiHu.org` 而 `Data\` 下缺文件，`gen_data.py` 会**报错退出**（而不是
悄悄生成一套缺道具名的数据）；确实要那样请加 `--allow-partial`。
### 为什么不把数据随仓库分发

游戏数据（exe、DAB、HUM、图片）版权属于原开发商；生成的 `*_data.c` 里含游戏内文本
（道具名、技能名、角色名），属于衍生内容。仓库只放**提取工具**，不放**提取结果**。

## 四、构建

> 本节不需要游戏目录。它只编译 `src\` 下已有的 `.c`（含上一步生成的 `*_data.c`）。
> 如果构建提示 `generated sources are missing`，说明第三节的「生成数据」还没做。

```bat
:: 通用版（推荐）：TinyCC 构建，Win98 与 Win11 都能跑，产物约 40 KB
:: 工具链可用环境变量指定：SHUIHU_TCC / SHUIHU_WINAPI / SHUIHU_GCC
build\build-tcc.bat
::  → bin\shuihu-edit-GUI.exe（图形界面）
::  → bin\shuihu-edit-CLI.exe（命令行）

:: 备选：MinGW 构建，仅供 Win10/11（Win98 上会崩），产物约 130 KB
build\build-w11.bat
::  → bin\shuihu-edit-GUI-mingw.exe
::  → bin\shuihu-edit-CLI-mingw.exe
```

**三个数据表都不随仓库提供**。第一次用、或换了另一份游戏数据、或清空了 `src\` 后，都要先跑一次生成：

```bat
python tools\gen_data.py <ShuiHu.org 路径> src
```

---

## 五、使用

### 图形界面

双击 `shuihu-edit-GUI.exe`（Win98 与 Win11 通用）。

1. 「浏览...」选存档（会打开上次用过的目录），或把路径粘到左上角输入框后点「打开」
2. 左栏点角色 → 右栏显示 13 项属性，可直接修改（每格右侧标注上限）
3. 装备与物品分两行：第一行 武器/衣服/帽子，第二行 物品1~物品4；每个都是下拉框（200 件道具按名字选，首项「(空)」= 清空）
4. 右下角「金钱」可直接修改（0 .. 4294967295）
5. 「写入存档」→ 自动备份 → 只改变化字节 → 重新解析自校验
6. 「恢复备份」→ 取 `backup_save\` 里最新的一份覆盖回去
7. 「查看技能树」→ 按学习等级列出该职业全部必杀技，隐藏技另列并显示领悟状态
8. 「解锁隐藏技」→ 清零该角色的隐藏技标志块（**不消耗道具**）

底部按钮顺序：写入存档 → 恢复备份 → 解锁隐藏技 → 关于 → 退出

### 命令行

```bat
shuihu-edit-CLI -list   <save>           列出可编辑角色及属性
shuihu-edit-CLI -items  <save> <角色id>   列出该角色 7 个槽（含道具名）
shuihu-edit-CLI -verify <save>           结构自检
shuihu-edit-CLI -fields                  打印字段偏移表

:: 写入：默认干跑，加 -y 才落盘
shuihu-edit-CLI -money  <save> <值>                    [-y]
shuihu-edit-CLI -set    <save> <角色id> <字段> <值>     [-y]
shuihu-edit-CLI -slot   <save> <角色id> <槽0-6> <道具id> [-y]
shuihu-edit-CLI -unlock-hidden <save> <角色id>          [-y]
shuihu-edit-CLI -tree <save> <角色id>          查看技能树
```

字段名：`level hp hpmax mp mpmax atk def hit agi crit exp expnext move`

角色 id：`0 武龍 1 王進 2 公孫勝 3 史進 4 白勝 5 朱武 6 宋江 7 阮小二 8 阮小五 9 阮小七 10 吳用 11 林沖 12 晁蓋 13 陳達 14 楊志 15 楊春 16 魯智深 17 劉唐`

---

## 六、数据格式速查

### 存档

| 项 | 位置 |
|---|---|
| 魔数 | `0x00`：`GAM\x02` |
| 金钱 | `0x1B` 起，u32 小端 |
| 校验和 | **无** |
| 角色锚点 | 「生命当前/上限、气力当前/上限」四连 u16 的起点 |

以锚点 `A` 为基准（`rel`）：

```
 -28  u16  类型 id（0=武龍 …）        = unit+0x24
  -4  u16  等级 1..30                 = unit+0x3C
  +0/+2     生命 当前/上限 ≤510        = unit+0x40/0x42
  +4/+6     气力 当前/上限            = unit+0x44/0x46
  +8..+15   攻击/防御/命中/敏捷（4 × u16，各 ≤255）  = unit+0x48..0x4F
 +16  u16   必杀 ≤255                 = unit+0x50
 +54/+56    经验 当前/升级所需
 +74  u16   移动力                    = unit+0x8A
 +102…      7 × 10B 槽（0 武器 1 衣服 2 帽子 3-6 物品；槽内 +0 = 1 基道具 id）
 +240 起    隐藏技标志块（0=已领悟 1=未领悟），紧邻 `3C 00 03 00`，长度随职业
```

**解析要点**

1. 锚点常落在**奇数偏移**，必须逐字节扫描（按 2 字节步进会全部漏掉）。
2. 过滤假锚点靠三条后验：类型 id ≤ 40、等级 1..30、五项属性 ≤ 255。
3. 记录长度不固定，**不能顺序遍历**。
4. 存档只保存「本局出现过的角色」，未出场者无记录。

### 道具（GOODS.DAB）

`11 字节头 + 200 × 198 字节`。`id` 为 **1 基**（`id-1` 为记录下标）。
记录 `+0` 分类码（实测 0..24）：`0..12` 武器 / `13` 衣服 / `14` 帽子 / `≥15` 物品；
`+6` 名字（`char[20]`，Big5）。

---

## 七、平台兼容性要点

| 约束 | 说明 |
|---|---|
| **发布的 exe 用 TCC 构建** | MinGW 的 CRT 启动代码调用 `GetModuleHandleW`/`VirtualProtect` 等 Win9x 不可用接口，实机会报「执行了非法操作」；TCC 版两个平台通用 |
| 不用 comctl32 | 列表用 `LISTBOX`、按钮用 `BUTTON`，全部 user32 |
| 文件对话框结构大小 | Win9x 的 comdlg32 只接受 **76 字节**的 `OPENFILENAME_SIZE_VERSION_400`，传 `sizeof`（88）会被拒（`CDERR_STRUCTSIZE 0x1`） |
| 控制台输出编码 | Win9x 不支持 UTF-8 代码页：程序内部 UTF-8，输出前转系统 ANSI（简体 = GBK）；GUI 同理 |
| 依赖 DLL | 仅 `kernel32 user32 gdi32 comdlg32 msvcrt` |
| 脚本编码 | `build\*.bat` 为**纯 ASCII**（GBK 中文在非 936 代码页下会被当成 `|`/`^` 拆行）；`tools\*.ps1` 必须存成 **UTF-8 带 BOM**，否则 PowerShell 5.1 按 ANSI 读，中文变乱码导致语法错误 |

---

## 八、验证

### 回归自检

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\selftest.ps1
```

分两组：**A 组通用**（任意存档）+ **B 组样本专属**（哈希匹配才跑，否则跳过）。
带齐样本时：**19 项通过 / 0 组跳过**；只有一份存档时：**9 项通过 / 4 组跳过 / 0 失败**。

> 计数方式：**通过按单个检查项计**（A 组 9 项 + B 组 10 项 = 19）；
> **跳过按"组"计**（B 组共 4 组：字段落点、隐藏技标志块、槽位编辑、逐字节对比）。
> 所以 9 + 4 不等于 19，两组数字含义不同。

其中最关键的一项：

> 用编辑器把武龙五项属性改成 111/122/133/144/155，
> 产物与**游戏已读取并正确显示过**的 `../out/probe_stats.sav` **逐字节相同**。

### 样本对照（均与游戏内截图一致）

| 样本 | 结果 |
|---|---|
| `save9.sav` | 金钱 119,333；武龍 30 级 / 437-437 / 432-432 / 255×4 / 必杀 64 / 经验 86-100 / 移动 7 / 槽 `[5,103,127,177,192]` |
| `save6.sav` | 金钱 51,557；武龍 30 级 / 472-472 / 482-482 / 必杀 84 / 经验 88-100 / 槽 `[4,112,134,150,60,165]` |
| `必杀技\save0→save1` | 隐藏块 `111 → 全解锁`，槽内 150（古武術祕笈）被消耗 |
| `必杀技2\save2→save3` | 武龍 `110 → 全解锁`、史進 `111 → 全解锁` |

### 实机验收（2026-11-10）

Win98 实机完成：打开存档 → 改必杀 84→99 → 写入 → 复查 → 恢复备份 → 回到 84。

---

## 九、已知限制

| 限制 | 说明 |
|---|---|
| 不能新增/删除角色 | 记录变长，插入会破坏后续所有偏移 |
| 未出场角色不可改 | 存档里没有对应记录 |
| 物品无「数量」 | 游戏本身不支持堆叠，同一道具占多个槽 |
| 槽内 `+2` 字段 | 语义未知（已排除"数量"），编辑时保持原值 |
| 隐藏技块定位 | 标志块 = `3C 00` 之前 N 字节（N = 该职业隐藏技条数），标记后两字节可变（`01 00`/`03 C9`/`03 00`）。已在差分样本（5 名角色使用古武術祕笈）上验证：块清零后工具正确显示「全部已领悟」 |
| 同 id 多记录 | 存档里可能出现两条同 type id 的记录（如史進 2 级/1 级各一条），游戏只更新其中一条；`-tree` 会逐条列出，GUI 只显示第一条 |
| 数值上限 | 等级 30 / 属性 255 / 生命气力 510，取自代码夹取值；超限直接拒绝 |

---

## 十、V2（未开始）

改游戏本体数据，风险高于存档编辑器（影响所有存档与新开局）：

- `GOODS.DAB`：道具的攻击/防御/移动/体力等数值
- `HUMAN.HUM`：角色职业的基础值与成长率、默认装备

两条线的格式都已完全解出，字段表见分析报告 §4.18 / §4.20 / §4.29。

## 十一、数值上限的依据

界面上每个字段右侧标了「≤ N」，这些数字来源不同，需要区分：

| 字段 | 上限 | 依据 |
|---|---|---|
| 等级 | 30 | **程序内夹取**（反编译 `FUN_00408540`；与武功学习等级 ≤30 一致） |
| 攻击 / 防御 / 命中 / 敏捷 / 必杀 | 255 | **程序内夹取**（`if (0xFF < x) x = 0xFF`） |
| 生命 / 生命上限 / 气力 / 气力上限 | 510 | **程序内夹取**（`0x1FE`） |
| 移动 | 255 | **字段宽度**：单位对象 `+0x8A` 按单字节写入 |
| 经验 / 升级所需 | 65535 | **字段宽度**（u16），无游戏规则依据 |
| 金钱 | 4294967295 | **字段宽度**（u32） |

**注意**：这些是「工具允许写入的范围」，**不等于游戏内的正常上限**。
写入超出正常范围的值后，游戏读档时是否再夹一次，尚未实测。

「关于」对话框里也列出了这张表。

## 十二、程序图标

Logo 为「水」字朱印（方案 A），详见 [`assets/logo/README.md`](assets/logo/README.md)。

**加载方式：运行时读取**，不做资源内嵌（TCC 无法编译 `.rc`）：

```
exe 所在目录\logo.ico  →  LoadImageA(..., LR_LOADFROMFILE)  →  WM_SETICON
```

- 找不到 `logo.ico` 时**降级为无图标**，不影响任何功能（实测 `GetLastError=2`）
- `logo.ico` 内含 16/24/32（简化版）与 48/64/128/256（精细版）多种尺寸

图标源文件在仓库的 `assets\logo\logo.ico`（`release\` 不入库，需发布时自行打包）。
把它复制到 exe 同目录、命名为 `logo.ico` 即可生效。

## 十三、技能树

「查看技能树」按钮（位于「解锁隐藏技」左边）按学习等级列出选中角色职业的全部必杀技：

```
武龍（类型 id 0）
职业技能表：记录 0，共 12 条    当前等级 30

【普通必杀技】按学习等级排列
  Lv  1   一鳴驚人   ←默认技，面板不显示
  Lv  2   隔山打虎
  Lv  3   百尺竿頭
  Lv  5   八面威風
  Lv  7   千軍萬馬LV1
  ...

【隐藏必杀技】需「古武術祕笈」，该职业共 3 段
  Lv  1   萬夫莫敵LV1    已领悟
  Lv 12   萬夫莫敵LV2    已领悟
  Lv 22   萬夫莫敵LV3    已领悟

存档标志块：全 0（全部已领悟，共 3 格）
```

**数据来源**：`Data\HUMAN.HUM` 的职业技能表（构建期提取，运行时不需要游戏目录）。

**隐藏技的判定**：技能表里「隐藏标志 = 1」的条目。72 个职业里**每个都有 3 条**（LV1/LV2/LV3），
与存档里 3 字节隐藏技标志块**一一对应**——第 k 格 = 该职业第 k 条隐藏技。
这也解释了此前"单独清 `rel 241` 没反应"：那是 LV2，学习等级 12，角色等级不够时不显示。

**注意**：技能树列的是「该职业能学到的全部」，是否在面板显示还取决于角色等级是否达到学习等级。

命令行等价命令：`shuihu-edit-CLI -tree <save> <角色id>`

## 十四、仓库说明

### 不纳入版本库的内容

见 `.gitignore`：

```
bin/  release/                  构建产物与发布包
src/names_data.c
src/items_data.c
src/human_data.c                由游戏数据生成（见「构建前提」）
layout.txt  tools/__pycache__/  运行时/工具残留
```

发布包不要提交进仓库，另行分发。

### 批处理为什么是纯 ASCII

`build\*.bat` 的提示信息用英文，不是偷懒：

cmd 在非 936 代码页下解析 `.bat` 时，**GBK 汉字的第二字节可能正好是 `|` 或 `^`**，
会被当成管道 / 转义符，把一行拆成两条命令。实测从 PowerShell 调用带中文的 `.bat`
会报 `data.c is not recognized as an internal or external command`；加 `chcp 936` 也救不了
（解析发生在读行时，不是执行时）。

所以批处理保持 ASCII，中文说明放在 README 与发布说明里。

### 提交前检查

```bat
git status            :: 确认清单里没有 *_data.c / bin / release
build\build-tcc.bat   :: 在干净克隆上应提示 generated sources are missing 并退出
```

## 十五、运行时设置

程序会在 exe 同目录写一个 `shuihu-edit.ini`（自动生成，不纳入版本库）：

```ini
[ui]
LastDir=C:\ShuiHu\Save\
```

- 「浏览...」对话框以该目录为起始位置
- 用「浏览」选中或手动输入路径打开成功后，都会更新它
- 目录不存在时（U 盘拔掉、改名）自动回退到 exe 所在目录
- 删除该文件即恢复默认行为（从 exe 目录开始）

## 十六、新机器执行清单（人机通用）

这一节写成**步骤 + 预期输出**的形式，人和自动化代理都能照着做；
每一步都给了"成功长什么样"和"失败该做什么"。

### 0. 先跑自检

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\doctor.ps1
```

它检查八项并给出**下一步命令**，不用猜：

```
[OK]   操作系统        Windows NT 10.0...
[OK]   Python 3        py -3 -> Python 3.15.2
[缺失] TinyCC          未找到 —— 下载 tcc-0.9.27-win32-bin.zip，或设 SHUIHU_TCC
[缺失] 数据表 (*_data.c) 缺 names_data.c, items_data.c, human_data.c
        → 生成: py -3 tools\gen_data.py --auto src   （需 SHUIHU_DATA）
        → 或:   py -3 tools\gen_data.py --stub src   （空表，仅能编过）
...
====== 建议的下一步 ======
   安装 TinyCC 并设 SHUIHU_TCC / SHUIHU_WINAPI（见 README 第二节）
   没有游戏就先用空表: python tools\gen_data.py --stub src
   构建: build\build-tcc.bat
```

### 1. 装 Python 3.6+

```bat
python --version
:: 或
py -3 --version
```

**成功**：输出 `Python 3.x`。**失败**：到 python.org 装，勾选 Add to PATH。

> 只有第 2 步用它；构建和运行 exe 都不需要 Python。

### 2. 装 TinyCC

下载并解压两个包：

- 编译器 <https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win32-bin.zip>
- 头文件 <https://download.savannah.gnu.org/releases/tinycc/winapi-full-for-0.9.27.zip>

然后指定路径（解压位置随意）：

```bat
set SHUIHU_TCC=C:\tcc\tcc.exe
set SHUIHU_WINAPI=C:\tcc\include\winapi
```

**成功判据**：`%SHUIHU_TCC%` 能执行、`%SHUIHU_WINAPI%\windows.h` 存在。
（doctor.ps1 会直接检查这两点。）

### 3. 准备游戏数据

**这一步无法自动化**——游戏数据受版权保护，必须由人提供（见第三节「游戏数据放哪里」）。

```bat
set SHUIHU_DATA=C:\ShuiHu
```

**成功判据**：三个文件齐

```bat
dir /b %SHUIHU_DATA%\ShuiHu.org
dir /b %SHUIHU_DATA%\Data\GOODS.DAB
dir /b %SHUIHU_DATA%\Data\HUMAN.HUM
```

**没有游戏**：跳到 3b 用空表。

### 3b.（可选）空表模式

```bat
python tools\gen_data.py --stub src
```

产出只能编译验证，名字全为 `?`。**不是可用版本**。

### 4. 生成数据表

```bat
python tools\gen_data.py --auto src
```

**成功判据——这四个数字必须一致**，否则说明指错了游戏版本：

```
items      : 200
human      : 72 职业 / 302 条技能
char names : 40
skill names: 256
```

> 四个数字对得上只说明**条数**对。再确认**名字不是乱码**：
> 用 `bin\shuihu-edit-CLI.exe -list "任意.sav"` 看一眼角色名——
> 若全是问号或乱码，说明这份数据不是本工具预期的版本/语种。

### 5. 构建

```bat
build\build-tcc.bat
```

脚本会自己 `cd` 到仓库根（`cd /d "%~dp0.."`），在哪个目录调用都行；`bin\` 建在仓库根下。

**成功判据**：`bin\` 下出现两个文件

```
bin\shuihu-edit-GUI.exe    完整版约 40 KB（空表版约 31 KB）
bin\shuihu-edit-CLI.exe    完整版约 37 KB（空表版约 27 KB）
```

> 体积会随数据不同而变化，**不要用大小判断"是不是完整版"**——
> 用 `doctor.ps1` 看「数据表」那一行（它会区分完整数据与空表）。

**失败对照表**：

| 输出 | 含义 | 动作 |
|---|---|---|
| `[ERROR] TinyCC not found.` | 第 2 步没做或路径不对 | 设 `SHUIHU_TCC` |
| `[ERROR] generated sources are missing` | 第 4 步没做 | 回车第 3/4 步 |
| `error: ... undeclared` 等编译错误 | 源码或头文件不匹配 | 检查 `SHUIHU_WINAPI` 是否指向完整 winapi |

### 6. 验证

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\selftest.ps1
```

**成功判据**：`结果: 通过 N 项，失败 0 项`

- 有完整样本时：**19 通过 / 0 跳过**
- 只有一份存档时：**9 通过 / 4 跳过 / 0 失败**（用 `-Save` 指定）

### 7. 冒烟运行

```bat
bin\shuihu-edit-CLI.exe -verify "任意一个 .sav"
```

**成功判据**：输出文件大小、金钱、锚点数量。

### 自动化代理的边界

| 能自动完成 | 不能自动完成 |
|---|---|
| 下载 TCC、解压、设环境变量 | **获取游戏数据**（版权，必须由人提供） |
| 跑 `gen_data`（数据到位后）、构建、跑测试 | **安装 Python**（要跑安装程序，通常需人点确认） |
| 按失败对照表自纠错、用四个数字判断数据对不对 | 获取存档样本（可让人给任意存档代替） |

结论：**只要人把游戏数据放好并告诉路径，其余全流程可以自动化**；
没有游戏数据时，最多只能产出空表版（能编过、不能实用）。

## 十七、文档自检

同一个事实在仓库里往往有**三处**表述：README 的文字、`.gitignore` 的规则、构建脚本的报错文案。
改了一处忘了另一处，就会出现"文档说数据在仓库里、实际却被忽略"这种矛盾——我自己犯过一次。

所以把"必须一致的地方"变成**可执行断言**，而不是靠记性。

### 三处必须一致

| 事实 | 出现在哪里 | 不一致的后果 |
|---|---|---|
| **数据表不入库** | README「构建前提」↔ `.gitignore` ↔ `build\*.bat` 的报错文案 | 新人以为 `*_data.c` 已在仓库，克隆后构建失败 |
| **脚本编码** | README「平台兼容性」↔ 实际文件字节 | `.ps1` 缺 BOM → PowerShell 5.1 按 ANSI 读 → 中文乱码 → 语法错误 |
| **跳过计数口径** | README「验证」↔ `selftest.ps1` 的输出文案 | 读者按"项"去对"组"的数字，算术对不上 |
| **文件清单** | README 目录树 ↔ 仓库实际文件 | 文档提到已删除的文件 |
| **章节编号** | README 自身 | 插节后编号跳号或重复 |
| **许可证范围** | `LICENSE` ↔ README「十八、许可」 | 说了 MIT 却没写"不含游戏数据"，读者误以为游戏也可分发 |

### 一条命令检查

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\doccheck.ps1
```

输出 16 项断言，任一失败即退出码 1：

```
[OK]   数据表：.gitignore 与 README 说法一致
[OK]   数据表：README 明确'不随仓库提供'
[OK]   三个 *_data.c 都在 README 里出现
[OK]   三个 *_data.c 都在构建脚本里被检查
[OK]   所有 .ps1 都是 UTF-8 带 BOM
[OK]   所有 .bat 都是纯 ASCII
[OK]   README 引用的仓库内文件都存在
[OK]   章节编号连续（零…）
[OK]   章节标题无重复
[OK]   跳过计数口径：README 与 selftest 一致
[OK]   无旧名称残留
[OK]   目录树未重复列 selftest.ps1
[OK]   LICENSE 文件存在
[OK]   LICENSE 声明不含游戏数据
[OK]   README 链接到 LICENSE
[OK]   README 引用的报告版本与报告一致

结果: 通过 16 项，失败 0 项
```

### 什么时候跑

- **提交前**（和 `selftest.ps1` 一起）
- 改了文件清单、构建脚本、`.gitignore` 之后
- 大段改 README 之后

### 它**不**检查什么

| 不检查 | 原因 |
|---|---|
| 内容是否**正确**（如偏移表数字） | 需要领域知识，只能人审 |
| 代码块能否执行 | 未真正运行 bat/powershell 片段 |
| 外链是否可达 | 需要联网 |
| 中英文表述是否通顺 | 不属于机械检查 |

所以它是"防低级不一致"的护栏，不是"文档正确性"的证明。
真正的验收仍然是：**构建通过 + 回归 19 项通过 + 实机跑一遍**。

## 十八、许可

本仓库采用 **MIT License**（见 [`LICENSE`](LICENSE)）。

### 覆盖范围

| 内容 | 是否被 MIT 覆盖 | 说明 |
|---|---|---|
| `src\` 源代码 | ✅ | 随你用、改、分发、商用 |
| `build\`、`tools\` 脚本 | ✅ | 同上 |
| `README.md` 等文档 | ✅ | 同上 |
| `assets\logo\` Logo 与生成脚本 | ✅ | 同上 |
| **《水浒传之梁山好汉》游戏本体** | ❌ | 版权归原权利人 |
| **`ShuiHu.org`、`GOODS.DAB`、`HUMAN.HUM` 等游戏数据** | ❌ | 本仓库不包含、不分发 |

也就是说：**代码是自由的，游戏不是。**

### 使用前请知悉

- 本工具会**改写存档文件**。作者不对存档损坏、游戏异常或任何后果作担保，
  请自行备份（工具本身在写入前会自动备份到 `backup_save\`，但这不构成保证）。
- 分发本工具时，请一并保留 `LICENSE`；不要附带游戏数据文件。
- 若要发布构建产物（exe），建议在发布页注明「请自备正版游戏」。

### 想换许可证

如果希望更严格（如 GPL-3.0 要求衍生作品同样开源）或更宽松（如 CC0），
直接替换 `LICENSE` 文件、同步更新本节与文件头即可。
唯一不可变的是"游戏数据不随仓库分发"这一事实——那是版权法，不是许可证能改变的。

### 对账：对话里的结论有没有进报告

分析报告在仓库外（`../ShuiHu-分析报告.md`），容易"只说不写"。`tools\reportcheck.ps1` 做机械提醒：

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\reportcheck.ps1 -Since 3
```

它做两件事：

1. 列出最近 N 个提交的**结论条目**（提交正文的项目符号），供逐条确认
2. 从这些文字里抽关键词（`0x…`、`+132`、`tid=32`、`×27`、反引号内容），
   逐个在报告里查，未命中的列出来

```
------ 最近提交里的结论条目（请逐条确认报告里有没有）------
  - save0 的 68 个锚点按 type id 统计：tid 32 ×27、34 ×18、19 ×6 等，
  - 无属性上限过滤时同一文件得 72 个锚点，多出的 4 个是撞上 hp/hp/mp/mp
  ...
====== 对账提醒 ======
  关键词数: 4
  全部命中：这些关键词在报告里都能找到。
```

**它只能提醒，不能代替判断**：关键词命中不等于结论写全，未命中也可能只是换了措辞。
真正的判定标准是：**每条结论都能回答"它在报告的哪一节"**。