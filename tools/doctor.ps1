# doctor.ps1 —— 环境自检
#
# 目的：把"这台机器能不能构建/能不能跑测试"一次性查清，并给出下一步该敲什么命令。
# 既方便人，也方便自动化代理按结果决定动作。
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\doctor.ps1
#
# 退出码: 0 = 可以构建（数据表或工具链缺失时会给出指引但仍返回 0/1，见末尾）

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent

function Line($name, $ok, $detail) {
    $tag = if ($ok -eq $true) { '[OK]  ' } elseif ($ok -eq $false) { '[缺失]' } else { '[?]   ' }
    Write-Host ("  {0} {1,-22} {2}" -f $tag, $name, $detail)
}

Write-Host ""
Write-Host "====== shuihu-edit 环境自检 ======" -ForegroundColor Cyan
Write-Host ("仓库: " + $root)
Write-Host ""

# ---------------------------------------------------------------- 系统
$os = [Environment]::OSVersion.VersionString
Line "操作系统" $true $os

# ---------------------------------------------------------------- Python
$pyCmd = $null
foreach ($cand in @('python', 'py -3', 'python3')) {
    $parts = $cand -split ' '
    $exe = $parts[0]
    if (Get-Command $exe -ErrorAction SilentlyContinue) {
        try {
            $v = & $exe @($parts[1..($parts.Count-1)] | Where-Object { $_ }) --version 2>&1
            if ($v -match 'Python 3\.(\d+)') {
                $pyCmd = $cand
                Line "Python 3" $true ("$cand -> " + ($v -join ' '))
                break
            }
        } catch { }
    }
}
if (-not $pyCmd) { Line "Python 3" $false "未找到（生成数据表需要；可用 python / py -3 / python3）" }

# ---------------------------------------------------------------- TinyCC
$tcc = $env:SHUIHU_TCC
$winapi = $env:SHUIHU_WINAPI
$tccFrom = 'SHUIHU_TCC'
if (-not $tcc) {
    $def = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'tools\tcc\tcc\tcc.exe'
    if (Test-Path $def) { $tcc = $def; $tccFrom = '默认布局' }
}
if (-not $tcc -and (Get-Command tcc -ErrorAction SilentlyContinue)) {
    $tcc = 'tcc'; $tccFrom = 'PATH'
}
if ($tcc -and (Test-Path $tcc -ErrorAction SilentlyContinue)) {
    Line "TinyCC" $true "$tcc（来自 $tccFrom）"
} elseif ($tcc -eq 'tcc') {
    Line "TinyCC" $true "PATH 上的 tcc"
} else {
    Line "TinyCC" $false "未找到 —— 下载 tcc-0.9.27-win32-bin.zip，或设 SHUIHU_TCC"
}

if (-not $winapi) {
    $defw = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'tools\tcc\winapi-full-for-0.9.27\include\winapi'
    if (Test-Path $defw) { $winapi = $defw }
}
if ($winapi -and (Test-Path (Join-Path $winapi 'windows.h') -ErrorAction SilentlyContinue)) {
    Line "WinAPI 头文件" $true $winapi
} else {
    Line "WinAPI 头文件" $false "未找到 windows.h —— 需要 winapi-full-for-0.9.27，或设 SHUIHU_WINAPI"
}

# ---------------------------------------------------------------- 生成的数据表
$namesC = Join-Path $root 'src\names_data.c'
$itemC  = Join-Path $root 'src\items_data.c'
$humanC = Join-Path $root 'src\human_data.c'
$missing = @()
foreach ($p in @($namesC, $itemC, $humanC)) { if (-not (Test-Path $p)) { $missing += (Split-Path $p -Leaf) } }

if ($missing.Count -gt 0) {
    Line "数据表 (*_data.c)" $false ("缺 " + ($missing -join ', '))
    Write-Host "         → 生成: $pyCmd tools\gen_data.py --auto src   （需 SHUIHU_DATA）" -ForegroundColor Yellow
    Write-Host "         → 或:   $pyCmd tools\gen_data.py --stub src   （空表，仅能编过）" -ForegroundColor Yellow
} else {
    # 判断是真数据还是空表
    $cnt = Select-String -Path $itemC -Pattern 'SH_ITEM_COUNT\s*=\s*(\d+)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Select-Object -First 1
    if ($cnt -eq 0) {
        Line "数据表 (*_data.c)" $null "存在，但是【空表】—— 名字会显示 ?，道具下拉框为空"
    } else {
        Line "数据表 (*_data.c)" $true "完整数据（道具 $cnt 条）"
    }
}

# ---------------------------------------------------------------- 游戏数据
$dataDir = $env:SHUIHU_DATA
if (-not $dataDir) {
    $guess = Join-Path (Split-Path $root -Parent) 'ShuiHu'
    if (Test-Path $guess) { $dataDir = $guess }
}
if ($dataDir -and (Test-Path $dataDir)) {
    $exe = Join-Path $dataDir 'ShuiHu.org'
    $g1  = Join-Path $dataDir 'Data\GOODS.DAB'
    $g2  = Join-Path $dataDir 'Data\HUMAN.HUM'
    $have = @($exe, $g1, $g2) | Where-Object { Test-Path $_ }
    if ($have.Count -eq 3) {
        Line "游戏数据" $true "$dataDir（三个文件齐）"
    } else {
        $lack = @($exe, $g1, $g2) | Where-Object { -not (Test-Path $_) } | ForEach-Object { Split-Path $_ -Leaf }
        Line "游戏数据" $false ("$dataDir 下缺 " + ($lack -join ', '))
    }
} else {
    Line "游戏数据" $false "未找到（SHUIHU_DATA 未设，且默认位置无 ShuiHu）—— 需自备正版游戏"
}

# ---------------------------------------------------------------- 构建产物
$gui = Join-Path $root 'bin\shuihu-edit-GUI.exe'
$cli = Join-Path $root 'bin\shuihu-edit-CLI.exe'
if ((Test-Path $gui) -and (Test-Path $cli)) {
    Line "已构建产物" $true ("GUI " + (Get-Item $gui).Length + " B / CLI " + (Get-Item $cli).Length + " B")
} else {
    Line "已构建产物" $false "bin\ 下还没有 exe —— 运行 build\build-tcc.bat"
}

# ---------------------------------------------------------------- 测试样本
$samples = @()
if ($dataDir) {
    foreach ($rel in @('水浒传之梁山好汉修改器\save9.sav', 'Save\save6.sav', '水浒传之梁山好汉修改器\必杀技\save0——学习前.sav')) {
        $p = Join-Path $dataDir $rel
        if (Test-Path $p) { $samples += $p }
    }
}
if ($samples.Count -ge 3) {
    Line "回归测试样本" $true "$($samples.Count) 个（可跑完整 19 项）"
} elseif ($samples.Count -gt 0) {
    Line "回归测试样本" $null "$($samples.Count) 个（只能跑部分检查）"
} else {
    Line "回归测试样本" $false "无 —— 可用 -Save 指定任意存档跑通用检查"
}

# ---------------------------------------------------------------- Win98 镜像
$img = Join-Path $root '.exclude\ShuiHu_Dosbox_win98\windows98.img'
$imgTool = Join-Path $root 'tools\imgfs.py'
if ((Test-Path $img) -and (Test-Path $imgTool)) {
    $imgGB = (Get-Item $img).Length / 1GB
    Line "Win98 镜像" $true ("{0}（{1:N1} GB；tools\imgfs.py 可用）" -f $img, $imgGB)
} elseif (Test-Path $img) {
    Line "Win98 镜像" $null "$img（缺 tools\imgfs.py）"
} else {
    Line "Win98 镜像" $null "无（可选；有 DOSBox Win98 时才用）"
}

# ---------------------------------------------------------------- 结论
Write-Host ""
Write-Host "====== 建议的下一步 ======" -ForegroundColor Cyan
$steps = @()
if (-not $pyCmd -and $missing.Count -gt 0) { $steps += "1. 安装 Python 3.6+（只有生成数据表要用）" }
if (-not $tcc)   { $steps += "   安装 TinyCC 并设 SHUIHU_TCC / SHUIHU_WINAPI（见 README 第二节）" }
if ($missing.Count -gt 0) {
    if ($dataDir) { $steps += "   生成数据表: python tools\gen_data.py --auto src" }
    else          { $steps += "   没有游戏就先用空表: python tools\gen_data.py --stub src" }
}
if (-not (Test-Path $gui)) { $steps += "   构建: build\build-tcc.bat" }
if ($steps.Count -eq 0) {
    Write-Host "  一切就绪。可直接:" -ForegroundColor Green
    Write-Host "    build\build-tcc.bat" -ForegroundColor Green
    Write-Host "    powershell -NoProfile -ExecutionPolicy Bypass -File tools\selftest.ps1" -ForegroundColor Green
} else {
    $steps | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
