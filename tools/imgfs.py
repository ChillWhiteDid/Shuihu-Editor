#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""ShuiHu 兼容 shim —— 转发到 FAT16 通用工具。

查找顺序（先命中者为准）：

1. ``REA\tools\fat16img\fat16img.py`` —— 工作区实时核心（开发时改这里）；
2. ``tools\_vendor\fat16img\fat16img.py`` —— 随仓库分发的快照，
   让 ``shuihu-edit`` 单独 clone 出来也能用。

两份不一致时 ``tools\doctor.ps1`` 会报出来；同步命令见 ``tools\_vendor\fat16img\README.md``。

保留旧的调用方式：
  * 未给 ``--image`` 时，默认 ShuiHu 镜像（或 ``SHUIHU_IMG``）；
  * 未给 ``--protect-preset``/``--no-protect`` 时，默认启用 ``win9x`` 保护；
  * 其余参数原样透传。

``tools\vmtest.ps1``、``tools\imgfs-test.ps1``、``doctor.ps1`` 和 README 的旧命令
都继续通过这个 shim 工作。
"""

import os
import runpy
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

CANDIDATES = [
    ("live", os.path.abspath(os.path.join(
        HERE, "..", "..", "..", "tools", "fat16img", "fat16img.py"))),
    ("vendor", os.path.abspath(os.path.join(
        HERE, "_vendor", "fat16img", "fat16img.py"))),
]

DEFAULT_IMAGE = os.path.abspath(os.path.join(
    HERE, "..", ".exclude", "ShuiHu_Dosbox_win98", "windows98.img"))


def _has(flag):
    return any(a == flag or a.startswith(flag + "=") for a in sys.argv[1:])


def _resolve_core():
    for _kind, path in CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


CORE = _resolve_core()
if CORE is None:
    print("[错误] 找不到 FAT16 核心，已尝试：", file=sys.stderr)
    for kind, path in CANDIDATES:
        print("  [%s] %s" % (kind, path), file=sys.stderr)
    print("修复：在 REA 工作区运行，或执行 tools\\_vendor\\fat16img\\README.md 里的同步命令。",
          file=sys.stderr)
    raise SystemExit(4)

argv = list(sys.argv[1:])
if not _has("--image"):
    argv[0:0] = ["--image", os.environ.get("SHUIHU_IMG") or DEFAULT_IMAGE]
if not (_has("--protect-preset") or _has("--no-protect")):
    argv[0:0] = ["--protect-preset", "win9x"]

sys.argv = [CORE] + argv
runpy.run_path(CORE, run_name="__main__")