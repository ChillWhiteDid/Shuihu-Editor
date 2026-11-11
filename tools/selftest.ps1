# selftest.ps1 —— 回归自检
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\selftest.ps1
#   powershell ... -File tools\selftest.ps1 -Save "D:\my\save9.sav"
#
# 分两组检查:
#   A 组【通用】任意合法存档都能跑
#       干跑不改文件 / 金钱往返 / 越界拦截 / 备份生成 / 长度不变 / 槽位越界
#   B 组【样本专属】依赖已知样本的固定偏移，只有文件哈希匹配时才跑
#       字段落点、隐藏技标志块、与游戏已验证产物逐字节对比
#
# 样本自动查找顺序:
#   1. 命令行参数 -Save / -Save6 / -SkillSave / -ProbeRef
#   2. 环境变量 SHUIHU_DATA 指向的目录（游戏根目录，含 Save\ 与 水浒传之梁山好汉修改器\）
#   3. 仓库默认布局：<repo>\..\ShuiHu\
#
# 找不到样本不报错：相关检查标记为"跳过"，其余照常执行。

param(
    [string]$Save,
    [string]$Save6,
    [string]$SkillSave,
    [string]$ProbeRef,
    [string]$DataDir = $env:SHUIHU_DATA
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $root 'bin\shuihu-edit-CLI.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $root 'bin\shuihu-edit-CLI-mingw.exe' }
if (-not (Test-Path $exe)) { throw "找不到可执行文件，请先运行 build\build-tcc.bat" }

$test = Join-Path $root 'bin\test'
New-Item -ItemType Directory -Force -Path $test | Out-Null
Get-ChildItem $test -Recurse -File -ErrorAction SilentlyContinue | Remove-Item -Force
Remove-Item (Join-Path $test 'backup_save') -Recurse -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- 样本定位
if (-not $DataDir) {
    try {
        $guess = Join-Path (Split-Path $root -Parent) 'ShuiHu'
        if (Test-Path $guess) { $DataDir = $guess }
    } catch { }
}

function Find-Sample($explicit, $relative) {
    try {
        if ($explicit) { return $explicit }
        if ($DataDir) {
            $p = Join-Path $DataDir $relative
            if (Test-Path $p) { return $p }
        }
    } catch {
        # 盘符不存在等情况：当作没找到
    }
    return $null
}

$save9Path = Find-Sample $Save  '水浒传之梁山好汉修改器\save9.sav'
$save6Path = Find-Sample $Save6 'Save\save6.sav'
$skillPath = Find-Sample $SkillSave '水浒传之梁山好汉修改器\必杀技\save0——学习前.sav'
if (-not $ProbeRef) {
    $p = Join-Path (Split-Path $root -Parent) 'ShuiHu-Re\out\probe_stats.sav'
    if (Test-Path $p) { $ProbeRef = $p }
}

$HASH_SAVE9 = 'C74B02AB6E653CCA0EED5982F4F5F665F2EF5CF4C9ECFE735A830E88E56F2C2F'
$HASH_SAVE6 = '587FD6E14FAA2608BE83AA36E3FE02CA1646036FABC7DF1C6489BCB20A2AB65D'
$HASH_SKILL = 'E6D525A433CD4BB5E314690BDFD3D487B31C9DB76CC070C66D9842F9E305A64D'

$pass = 0; $fail = 0; $skip = 0
function Check($name, $cond) {
    if ($cond) { Write-Host ("  [通过] " + $name) -ForegroundColor Green; $script:pass++ }
    else       { Write-Host ("  [失败] " + $name) -ForegroundColor Red;   $script:fail++ }
}
function Skip($name, $why) {
    Write-Host ("  [跳过整组] " + $name + " —— " + $why) -ForegroundColor Yellow
    $script:skip++
}
function HashOf($p) { (Get-FileHash $p -Algorithm SHA256).Hash }

if (-not $save9Path) {
    throw "找不到可测试的存档。请用 -Save <路径> 指定，或设置环境变量 SHUIHU_DATA 指向游戏目录。"
}

Write-Host ("存档样本: " + $save9Path)
if ($save6Path) { Write-Host ("槽位样本: " + $save6Path) }
if ($skillPath) { Write-Host ("隐藏技样本: " + $skillPath) }
if ($ProbeRef)  { Write-Host ("对比基准: " + $ProbeRef) }
Write-Host ""

# ================================================================ A 组：通用
$f = Join-Path $test 't1.sav'
Copy-Item $save9Path $f
$srcLen = (Get-Item $f).Length
$h0 = HashOf $f

cmd /c "`"$exe`" -money `"$f`" 999999" | Out-Null
Check "干跑不改文件"          ((HashOf $f) -eq $h0)
Check "干跑不建备份目录"      (-not (Test-Path (Join-Path $test 'backup_save')))

cmd /c "`"$exe`" -money `"$f`" 999999 -y" | Out-Null
$b = [IO.File]::ReadAllBytes($f)
Check "金钱写入正确"          ([BitConverter]::ToUInt32($b, 0x1B) -eq 999999)
Check "生成 backup_save"      (Test-Path (Join-Path $test 'backup_save'))
Check "备份与原件同大小"      ((Get-ChildItem (Join-Path $test 'backup_save') -File |
                                Select-Object -First 1).Length -eq $b.Length)
Check "文件长度未变"          ($b.Length -eq $srcLen)

$out = cmd /c "`"$exe`" -set `"$f`" 0 crit 300" 2>&1
Check "crit>255 被拒"         ($out -match '超出允许范围')
$out = cmd /c "`"$exe`" -set `"$f`" 0 level 99" 2>&1
Check "level>30 被拒"         ($out -match '超出允许范围')
$out = cmd /c "`"$exe`" -set `"$f`" 0 hpmax 100" 2>&1
Check "破坏 当前≤上限 被拒"   ($out -match '会破坏')

# ================================================================ B 组：样本专属
if ((HashOf $save9Path) -eq $HASH_SAVE9) {
    cmd /c "`"$exe`" -set `"$f`" 0 level 25 -y" | Out-Null
    $b = [IO.File]::ReadAllBytes($f)
    Check "等级写入到 锚点-4" ([BitConverter]::ToUInt16($b, 0x7D3 - 4) -eq 25)
} else {
    Skip "等级写入偏移" "当前存档与已知 save9 不符"
}

if ($skillPath -and (HashOf $skillPath) -eq $HASH_SKILL) {
    $sf = Join-Path $test 'skill.sav'
    Copy-Item $skillPath $sf
    $before = [IO.File]::ReadAllBytes($sf)
    Check "解锁前标志块=111" (($before[0x494+240] -eq 1) -and ($before[0x494+241] -eq 1) -and ($before[0x494+242] -eq 1))
    cmd /c "`"$exe`" -unlock-hidden `"$sf`" 0 -y" | Out-Null
    $after = [IO.File]::ReadAllBytes($sf)
    Check "解锁后标志块=000" (($after[0x494+240] -eq 0) -and ($after[0x494+241] -eq 0) -and ($after[0x494+242] -eq 0))
    Check "解锁不消耗道具"   ($after[0x494+142] -eq 150)
} else {
    Skip "隐藏技标志块" "缺少与已知样本一致的 必杀技\save0"
}

if ($save6Path -and (HashOf $save6Path) -eq $HASH_SAVE6) {
    $sf2 = Join-Path $test 'slot.sav'
    Copy-Item $save6Path $sf2
    $out = cmd /c "`"$exe`" -items `"$sf2`" 0" 2>&1
    Check "items 显示道具名" (($out -join "`n") -match '熟皮甲|雁翎圈金甲|烏金手套')

    $h0 = HashOf $sf2
    cmd /c "`"$exe`" -slot `"$sf2`" 0 3 177" | Out-Null
    Check "slot 干跑不改文件" ((HashOf $sf2) -eq $h0)

    cmd /c "`"$exe`" -slot `"$sf2`" 0 3 177 -y" | Out-Null
    $bb = [IO.File]::ReadAllBytes($sf2)
    Check "slot 写入到 +102+3*10" ($bb[0x6B7 + 102 + 30] -eq 177)

    $out = cmd /c "`"$exe`" -slot `"$sf2`" 0 3 201" 2>&1
    Check "道具 id 越界被拒"  (($out -join "`n") -match '应为 0\.\.200')
    $out = cmd /c "`"$exe`" -slot `"$sf2`" 0 7 5" 2>&1
    Check "槽序号越界被拒"    (($out -join "`n") -match '应为 0\.\.6')
} else {
    Skip "槽位编辑落点" "缺少与已知样本一致的 Save\save6.sav"
}

if ($ProbeRef -and (HashOf $save9Path) -eq $HASH_SAVE9 -and (Test-Path $ProbeRef)) {
    $pf = Join-Path $test 'probe.sav'
    Copy-Item $save9Path $pf
    foreach ($pair in @(@('atk',111),@('def',122),@('hit',133),@('agi',144),@('crit',155))) {
        cmd /c "`"$exe`" -set `"$pf`" 0 $($pair[0]) $($pair[1]) -y" | Out-Null
    }
    Check "与 probe_stats.sav 逐字节相同" ((HashOf $pf) -eq (HashOf $ProbeRef))
} else {
    Skip "与游戏已验证产物对比" "缺少 probe_stats.sav 或样本不符"
}

Write-Host ""
Write-Host ("结果: 通过 {0} 项，失败 {1} 项，跳过 {2} 组" -f $pass, $fail, $skip) `
    -ForegroundColor $(if ($fail -eq 0) { 'Green' } else { 'Red' })
exit $(if ($fail -eq 0) { 0 } else { 1 })
