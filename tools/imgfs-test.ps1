# imgfs-test.ps1 —— 在克隆镜像上验证 imgfs.py / vmtest.ps1
#
# 只操作 windows98.test.img，不碰真实 windows98.img。
# 首次运行前先: powershell -File tools\vmtest.ps1 clone
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\imgfs-test.ps1
#
# 覆盖:
#   基线 check / 读取锚点 / 原位写(FAT 不变) / journal 回滚 / 占用时拒绝写
#   黑名单拒绝写(仍可读) / M2 新建与删除 / M2 重分配 / sync-release / clone 配置
# 最后把 /RELEASE/save6.sav 换成 save9 的内容，供人工启动 Win98 验证。

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$imgfs = Join-Path $PSScriptRoot 'imgfs.py'
$vmtest = Join-Path $PSScriptRoot 'vmtest.ps1'
$imgDir = Join-Path $root '.exclude\ShuiHu_Dosbox_win98'
$clone = Join-Path $imgDir 'windows98.test.img'
$realImg = Join-Path $imgDir 'windows98.img'
$out = Join-Path $root 'bin\test\vm'
$knownSave9 = '28C7EAF736C78459FE09830190285125A99AEF9D4262F9A37E27550CBB4F8B36'

if (-not (Test-Path -LiteralPath $imgfs)) { throw "找不到 $imgfs" }
if (-not (Test-Path -LiteralPath $clone)) {
    throw "找不到克隆镜像；先运行: powershell -File tools\vmtest.ps1 clone"
}
if ((Get-Process dosbox* -ErrorAction SilentlyContinue).Count -gt 0) {
    throw "DOSBox 正在运行；测试会独占打开镜像，请先退出 DOSBox（真实镜像或克隆都不行）"
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ---------------------------------------------------------------- Python
$script:PyExe = $null
$script:PyArgs = @()
foreach ($cand in @(@{ Exe = 'python'; Pre = @() }, @{ Exe = 'py'; Pre = @('-3') }, @{ Exe = 'python3'; Pre = @() })) {
    if (-not (Get-Command $cand.Exe -ErrorAction SilentlyContinue)) { continue }
    try {
        $v = & $cand.Exe @($cand.Pre) --version 2>&1
        if (($v -join ' ') -match 'Python 3\.') {
            $script:PyExe = $cand.Exe
            $script:PyArgs = @($cand.Pre)
            break
        }
    } catch { }
}
if (-not $script:PyExe) { throw "找不到 Python 3" }

$script:pass = 0
$script:fail = 0
function Check([string]$name, [bool]$cond, [string]$detail = "") {
    if ($cond) {
        Write-Host ("  [通过] " + $name) -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host ("  [失败] " + $name + $(if ($detail) { " —— " + $detail } else { "" })) -ForegroundColor Red
        $script:fail++
    }
}
function Note([string]$msg) { Write-Host ("  [提示] " + $msg) -ForegroundColor Yellow }

function Invoke-Imgfs {
    param([string[]]$Arguments, [switch]$AllowFail)
    $all = @()
    $all += $script:PyArgs
    $all += $imgfs
    $all += @('--image', $clone)
    $all += $Arguments
    & $script:PyExe @all
    $script:LastCode = $LASTEXITCODE
    if (-not $AllowFail -and $script:LastCode -ne 0) {
        throw ("imgfs 失败，退出码 {0}: {1}" -f $script:LastCode, ($Arguments -join ' '))
    }
}
function Get-ImgfsJson {
    param([string[]]$Arguments)
    $all = @()
    $all += $script:PyArgs
    $all += $imgfs
    $all += @('--image', $clone)
    $all += $Arguments
    $text = & $script:PyExe @all
    if ($LASTEXITCODE -ne 0) { throw ("imgfs --json 失败，退出码 {0}" -f $LASTEXITCODE) }
    return (($text -join "`n") | ConvertFrom-Json)
}
function Invoke-Vmtest {
    param([string[]]$Arguments)
    & powershell -NoProfile -ExecutionPolicy Bypass -File $vmtest @Arguments | Out-Host
    return [int]$LASTEXITCODE
}
function Get-Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }

Write-Host ""
Write-Host "====== imgfs / vmtest 克隆镜像测试 ======" -ForegroundColor Cyan
Write-Host ("镜像: " + $clone)
Write-Host ""

# ================================================================ 基线
Write-Host "-- 基线 --"
$info0 = Get-ImgfsJson @('info', '--json')
$check0 = Get-ImgfsJson @('check', '--json')
$free0 = [int]$check0.free
Check "FAT16 几何" ($info0.fat.clusters -eq 65510 -and $info0.fat.cluster_bytes -eq 32768)
Check "FAT 双份一致" ([bool]$info0.fat_copies_identical)
Check "check 无问题" ($check0.problems.Count -eq 0 -and [int]$check0.lost -eq 0) ("problems={0} lost={1}" -f $check0.problems.Count, $check0.lost)
Check "空闲簇基线有效" ($free0 -gt 0) ("free=$free0")
$fatBase = [string]$info0.fat_sha256

# ================================================================ 读取锚点
Write-Host "-- 读取 --"
$save9 = Join-Path $out 'save9.orig.sav'
Invoke-Imgfs @('get', '/SH/SAVE/save9.sav', $save9)
$h9 = Get-Hash $save9
Check "save9 大小 17617" ((Get-Item -LiteralPath $save9).Length -eq 17617)
if ($h9 -eq $knownSave9) {
    Check "save9 已知锚点 28C7…" $true
} else {
    Note ("save9 哈希 {0} 不是已知锚点；以下用当前值作基线" -f $h9)
}

# ================================================================ 原位写 + FAT 不变
Write-Host "-- 原位写 / journal 回滚 --"
$probe = Join-Path $out 'save9.probe.sav'
$bytes = [IO.File]::ReadAllBytes($save9)
$bytes[0x1B] = $bytes[0x1B] -bxor 0x01
[IO.File]::WriteAllBytes($probe, $bytes)
$hProbe = Get-Hash $probe
Invoke-Imgfs @('put', $probe, '/SH/SAVE/save9.sav')
$info1 = Get-ImgfsJson @('info', '--json')
Check "原位写后 FAT 不变" ([string]$info1.fat_sha256 -eq $fatBase)
$back = Join-Path $out 'save9.back.sav'
Invoke-Imgfs @('get', '/SH/SAVE/save9.sav', $back)
Check "回读哈希 = 探针" ((Get-Hash $back) -eq $hProbe)
$check1 = Get-ImgfsJson @('check', '--json')
Check "写后 check 干净" ($check1.problems.Count -eq 0 -and [int]$check1.lost -eq 0)

$jdir = Join-Path $imgDir '.imgfs\windows98.test'
$journal = Get-ChildItem -LiteralPath $jdir -Filter 'journal-*.bin' -File |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Check "生成 journal" ($null -ne $journal)
if ($journal) {
    Invoke-Imgfs @('rollback', $journal.FullName)
    $rest = Join-Path $out 'save9.restored.sav'
    Invoke-Imgfs @('get', '/SH/SAVE/save9.sav', $rest)
    Check "rollback 恢复原哈希" ((Get-Hash $rest) -eq $h9)
}

# ================================================================ 占用拒绝
Write-Host "-- 占用保护 --"
$fs = [IO.File]::Open($clone, 'Open', 'ReadWrite', 'None')
try {
    Invoke-Imgfs @('put', $save9, '/SH/SAVE/save9.sav') -AllowFail
    Check "被占用时拒绝写(3)" ($script:LastCode -eq 3) ("code=$($script:LastCode)")
} finally {
    $fs.Close()
}
$info2 = Get-ImgfsJson @('info', '--json')
Check "拒绝后镜像未变" ([string]$info2.fat_sha256 -eq $fatBase)

# ================================================================ 黑名单
Write-Host "-- 黑名单（写/删拒绝，读允许）--"
$small = Join-Path $out 'probe.txt'
Set-Content -LiteralPath $small -Value 'imgfs blacklist probe' -Encoding ASCII
$cases = @(
    @{ Path = '/WINDOWS/WIN.INI'; Name = '/WINDOWS/WIN.INI' },
    @{ Path = '/PROGRA~1/X.TXT'; Name = '/PROGRA~1/X.TXT (8.3 别名)' },
    @{ Path = '/IO.SYS'; Name = '/IO.SYS (SYSTEM)' },
    @{ Path = '/SH/../WINDOWS/WIN.INI'; Name = '/SH/../WINDOWS/WIN.INI (归一化)' }
)
foreach ($c in $cases) {
    Invoke-Imgfs @('put', $small, $c.Path) -AllowFail
    Check ("拒绝写 " + $c.Name) ($script:LastCode -eq 7) ("code=$($script:LastCode)")
}
$winini = Join-Path $out 'win.ini'
Invoke-Imgfs @('get', '/WINDOWS/WIN.INI', $winini)
Check "受保护文件仍可读" ((Test-Path -LiteralPath $winini) -and (Get-Item -LiteralPath $winini).Length -eq 8329)
Invoke-Imgfs @('rm', '/WINDOWS/WIN.INI', '--yes') -AllowFail
Check "拒绝删 /WINDOWS/WIN.INI" ($script:LastCode -eq 7) ("code=$($script:LastCode)")
Invoke-Imgfs @('put', $save9, '/SH/SAVE/save9.sav')
Check "非黑名单路径可写" ($script:LastCode -eq 0)

# ================================================================ M2 新建/删除
Write-Host "-- M2 新建 / 删除 --"
Invoke-Imgfs @('put', $save9, '/PROBE.SAV')
Check "新建 /PROBE.SAV" ($script:LastCode -eq 0)
$pback = Join-Path $out 'probe.back.sav'
Invoke-Imgfs @('get', '/PROBE.SAV', $pback)
Check "新建回读哈希" ((Get-Hash $pback) -eq $h9)
$c2 = Get-ImgfsJson @('check', '--json')
Check "新建后 check 干净" ($c2.problems.Count -eq 0 -and [int]$c2.lost -eq 0)
Check "新建后 free = 基线-1" ([int]$c2.free -eq ($free0 - 1)) ("free=$($c2.free)")
Invoke-Imgfs @('rm', '/PROBE.SAV', '--yes')
Check "删除 /PROBE.SAV" ($script:LastCode -eq 0)
Invoke-Imgfs @('get', '/PROBE.SAV', (Join-Path $out 'probe.gone.sav')) -AllowFail
Check "删除后不存在(4)" ($script:LastCode -eq 4) ("code=$($script:LastCode)")
$c3 = Get-ImgfsJson @('check', '--json')
Check "删除后 free 回基线" ([int]$c3.free -eq $free0) ("free=$($c3.free)")

# ================================================================ M2 重分配
Write-Host "-- M2 重分配 --"
$save6 = Join-Path $out 'save6.orig.sav'
Invoke-Imgfs @('get', '/RELEASE/save6.sav', $save6)
$h6 = Get-Hash $save6
$big = Join-Path $out 'probe40k.bin'
$data = New-Object byte[] 40000
for ($i = 0; $i -lt $data.Length; $i++) { $data[$i] = [byte](($i * 37 + 11) -band 0xFF) }
[IO.File]::WriteAllBytes($big, $data)
$hBig = Get-Hash $big
Invoke-Imgfs @('put', $big, '/RELEASE/save6.sav')
Check "40 KB 触发重分配" ($script:LastCode -eq 0)
$bigBack = Join-Path $out 'save6.big.sav'
Invoke-Imgfs @('get', '/RELEASE/save6.sav', $bigBack)
Check "重分配回读哈希" ((Get-Hash $bigBack) -eq $hBig)
$c4 = Get-ImgfsJson @('check', '--json')
Check "重分配后 check 干净" ($c4.problems.Count -eq 0 -and [int]$c4.lost -eq 0)
Check "重分配后 free = 基线-1" ([int]$c4.free -eq ($free0 - 1)) ("free=$($c4.free)")

Invoke-Imgfs @('rm', '/RELEASE/save6.sav', '--yes')
Invoke-Imgfs @('put', $save6, '/RELEASE/save6.sav')
$save6Back = Join-Path $out 'save6.rest.sav'
Invoke-Imgfs @('get', '/RELEASE/save6.sav', $save6Back)
Check "save6 恢复哈希" ((Get-Hash $save6Back) -eq $h6)
$c5 = Get-ImgfsJson @('check', '--json')
Check "恢复后 free 回基线" ([int]$c5.free -eq $free0) ("free=$($c5.free)")

# ================================================================ sync-release
Write-Host "-- sync-release --"
$code = Invoke-Vmtest @('sync-release', '-ImagePath', $clone)
Check "sync-release 退出 0" ($code -eq 0) ("code=$code")
$gui = Join-Path $out 'gui.exe'
Invoke-Imgfs @('get', '/RELEASE/shuihu-edit-GUI.exe', $gui)
Check "镜像内 GUI 大小 = 本地构建" ((Get-Item -LiteralPath $gui).Length -eq (Get-Item -LiteralPath (Join-Path $root 'bin\shuihu-edit-GUI.exe')).Length)
$localGui = Join-Path $root 'bin\shuihu-edit-GUI.exe'
Check "镜像内 GUI 哈希 = 本地构建" ((Get-Hash $gui) -eq (Get-Hash $localGui))
$c6 = Get-ImgfsJson @('check', '--json')
Check "sync 后 check 干净" ($c6.problems.Count -eq 0 -and [int]$c6.lost -eq 0)

# ================================================================ clone 配置
Write-Host "-- clone 配置 --"
$code = Invoke-Vmtest @('clone')
Check "clone 退出 0" ($code -eq 0) ("code=$code")
$conf = Join-Path $imgDir 'CD1-test.conf'
Check "生成 CD1-test.conf" (Test-Path -LiteralPath $conf)
if (Test-Path -LiteralPath $conf) {
    Check "配置指向测试镜像" ((Get-Content -LiteralPath $conf -Raw) -match 'windows98\.test\.img')
}

# ================================================================ 给人工启动的样本
Write-Host "-- 为 Win98 启动验证准备样本 --"
Invoke-Imgfs @('put', $save9, '/RELEASE/save6.sav')
Check "用 save9 覆盖 /RELEASE/save6.sav" ($script:LastCode -eq 0)
$c7 = Get-ImgfsJson @('check', '--json')
Check "最终 check 干净" ($c7.problems.Count -eq 0 -and [int]$c7.lost -eq 0)

Write-Host ""
Write-Host ("结果: 通过 {0} 项，失败 {1} 项" -f $script:pass, $script:fail) `
    -ForegroundColor $(if ($script:fail -eq 0) { 'Green' } else { 'Red' })
Write-Host ""
Write-Host "人工验证（可选，但建议做一次）:" -ForegroundColor Cyan
Write-Host ("  cd `"{0}`"" -f $imgDir)
Write-Host "  .\dosbox.exe -conf CD1-test.conf"
Write-Host "  在 Win98 里运行 C:\RELEASE\shuihu-edit-GUI.exe，打开 C:\RELEASE\save6.sav"
Write-Host "  预期：武龍 等级 30 / 必杀 64 / 金钱 119333（这是 save9 的内容）"
Write-Host "  确认后正常退出 Win98。"
Write-Host ""
exit $(if ($script:fail -eq 0) { 0 } else { 1 })
