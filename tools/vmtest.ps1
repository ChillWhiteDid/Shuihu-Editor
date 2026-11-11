# vmtest.ps1 —— Win98 镜像测试工作流（agent / 人共用）
#
# 作用：不启动 Win98，直接对 .exclude\ShuiHu_Dosbox_win98\windows98.img
#       做 pull/push/sync/probe/backup/check；用户只负责启动 DOSBox 看结果。
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\vmtest.ps1 doctor
#   powershell ... -File tools\vmtest.ps1 status
#   powershell ... -File tools\vmtest.ps1 ls -Path /SH/SAVE
#   powershell ... -File tools\vmtest.ps1 pull -Path /SH/SAVE/save9.sav -Local bin\test\vm\save9.sav
#   powershell ... -File tools\vmtest.ps1 push -Local bin\test\vm\save9.sav -Path /SH/SAVE/save9.sav
#   powershell ... -File tools\vmtest.ps1 sync-release [-WhatIf]
#   powershell ... -File tools\vmtest.ps1 pull-saves
#   powershell ... -File tools\vmtest.ps1 probe -Slot 9 -Field crit -Value 99 [-WhatIf]
#   powershell ... -File tools\vmtest.ps1 backup
#   powershell ... -File tools\vmtest.ps1 restore -Backup <备份.img> -Yes
#   powershell ... -File tools\vmtest.ps1 check
#   powershell ... -File tools\vmtest.ps1 clone
#   powershell ... -File tools\vmtest.ps1 rollback -Journal <journal.bin>
#
# 所有命令都接受 -ImagePath <img>（默认真实镜像）；测试脚本用它指向克隆镜像。
#
# 退出码沿用 imgfs.py：0 成功；3 镜像被占用；4 结构/不支持；5 校验失败；
# 6 容量不足；7 受保护路径（/WINDOWS、/PROGRAM FILES、根系统文件等）。

[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Command = "",
    [string]$Path,
    [string]$Local,
    [string]$ImagePath,
    [string]$To,
    [string]$From = "bin",
    [int]$Slot = 9,
    [int]$Char = 0,
    [string]$Field,
    [int]$Value,
    [string]$Journal,
    [string]$Backup,
    [switch]$WhatIf,
    [switch]$IncludeIni,
    [switch]$IncludeDocs,
    [switch]$IncludeLogo,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$imgfs = Join-Path $PSScriptRoot 'imgfs.py'
$defaultImage = Join-Path $root '.exclude\ShuiHu_Dosbox_win98\windows98.img'
if (-not $ImagePath) { $ImagePath = $defaultImage }
$cli = Join-Path $root 'bin\shuihu-edit-CLI.exe'
$vmDir = Join-Path $root 'bin\test\vm'

# ---------------------------------------------------------------- Python
$script:PyExe = $null
$script:PyPrefix = @()
$candidates = @(
    @{ Exe = 'python'; Pre = @() },
    @{ Exe = 'py'; Pre = @('-3') },
    @{ Exe = 'python3'; Pre = @() }
)
foreach ($cand in $candidates) {
    if (-not (Get-Command $cand.Exe -ErrorAction SilentlyContinue)) { continue }
    try {
        $v = & $cand.Exe @($cand.Pre) --version 2>&1
        if (($v -join ' ') -match 'Python 3\.') {
            $script:PyExe = $cand.Exe
            $script:PyPrefix = @($cand.Pre)
            break
        }
    } catch {
    }
}
if (-not $script:PyExe) {
    throw "找不到 Python 3（试过 python / py -3 / python3）"
}

function Invoke-Imgfs {
    param([string[]]$Arguments, [switch]$AllowFail)
    $resolved = (Resolve-Path -LiteralPath $ImagePath -ErrorAction Stop).Path
    $all = @()
    $all += $script:PyPrefix
    $all += $imgfs
    $all += @('--image', $resolved)
    $all += $Arguments
    & $script:PyExe @all
    $script:LastCode = $LASTEXITCODE
    if (-not $AllowFail -and $script:LastCode -ne 0) {
        exit $script:LastCode
    }
}

function Ensure-Dir([string]$p) {
    if ($p -and -not (Test-Path -LiteralPath $p)) {
        New-Item -ItemType Directory -Force -Path $p | Out-Null
    }
}

function Get-Dosbox {
    return @(Get-Process dosbox* -ErrorAction SilentlyContinue)
}

function Show-Usage {
    Write-Host @"
vmtest.ps1 —— Win98 镜像测试工作流

  doctor                          环境自检（Python/镜像/DOSBox/备份）
  status                          镜像几何 + SAVE/RELEASE 清单 + journal
  ls    -Path <镜像路径>           列目录
  pull  -Path <镜像路径> -Local <本地文件>
  push  -Local <本地文件> -Path <镜像路径> [-WhatIf]
  sync-release [-From bin|release] [-WhatIf] [-IncludeIni] [-IncludeDocs] [-IncludeLogo]
  pull-saves [-To <目录>]          拉 save0..9 + manifest.json
  probe -Slot 9 -Field crit -Value 99 [-Char 0] [-WhatIf]
  backup [-Backup <目标>]          整镜像备份 + SHA-256
  restore -Backup <文件> -Yes      用备份覆盖镜像
  check                            全树 FAT 一致性
  clone                            复制测试镜像 + 生成 CD1-test.conf
  rollback -Journal <file>         按 journal 回滚

所有命令可加 -ImagePath <img> 指向测试克隆。
"@
}

# ---------------------------------------------------------------- 固定路径
if (-not (Test-Path -LiteralPath $imgfs)) {
    throw "找不到 $imgfs"
}

Write-Host "镜像: $ImagePath"
if (-not (Test-Path -LiteralPath $ImagePath)) {
    throw "镜像不存在：$ImagePath"
}

switch ($Command.ToLowerInvariant()) {
    "doctor" {
        Write-Host "====== vmtest doctor ======"
        Write-Host ("python    : {0} {1}" -f $script:PyExe, ($script:PyPrefix -join ' '))
        Write-Host ("imgfs     : {0}" -f $imgfs)
        Write-Host ("镜像      : {0} ({1:N0} B)" -f $ImagePath, (Get-Item -LiteralPath $ImagePath).Length)
        $dos = Get-Dosbox
        if ($dos.Count -gt 0) {
            Write-Host ("DOSBox    : 正在运行 PID {0}（若它打开的就是目标镜像，写操作会报占用）" -f (($dos | ForEach-Object { $_.Id }) -join ','))
        } else {
            Write-Host "DOSBox    : 未运行"
        }
        Invoke-Imgfs @('info') -AllowFail
        if ($script:LastCode -ne 0) {
            Write-Host ("[警告] info 失败，退出码 {0}（镜像是否被 DOSBox/其他进程占用？）" -f $script:LastCode)
        }
        $bdir = Join-Path (Split-Path $ImagePath) 'backup'
        if (Test-Path -LiteralPath $bdir) {
            $bk = @(Get-ChildItem -LiteralPath $bdir -Filter '*.img' -File -ErrorAction SilentlyContinue)
            Write-Host ("备份      : {0} 个（{1}）" -f $bk.Count, $bdir)
        } else {
            Write-Host "备份      : 无（首次真实写入前请先 backup）"
        }
        $testImg = Join-Path (Split-Path $ImagePath) 'windows98.test.img'
        Write-Host ("测试克隆  : {0}" -f $(if (Test-Path -LiteralPath $testImg) { $testImg } else { "无（可运行 clone）" }))
        exit 0
    }

    "status" {
        Write-Host "====== vmtest status ======"
        $dos = Get-Dosbox
        if ($dos.Count -gt 0) {
            Write-Host ("DOSBox    : PID {0}" -f (($dos | ForEach-Object { $_.Id }) -join ','))
        } else {
            Write-Host "DOSBox    : 未运行"
        }
        Invoke-Imgfs @('info') -AllowFail
        if ($script:LastCode -ne 0) {
            Write-Host ("[警告] 镜像当前不可读（退出码 {0}）；请退出打开它的进程" -f $script:LastCode)
            exit 0
        }
        Write-Host ""
        Write-Host "--- /SH/SAVE ---"
        Invoke-Imgfs @('ls', '/SH/SAVE') -AllowFail
        Write-Host ""
        Write-Host "--- /RELEASE ---"
        Invoke-Imgfs @('ls', '/RELEASE') -AllowFail
        Write-Host ""
        $jdir = Join-Path (Split-Path $ImagePath) '.imgfs'
        if (Test-Path -LiteralPath $jdir) {
            $js = @(Get-ChildItem -LiteralPath $jdir -Recurse -Filter 'journal-*.bin' -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 5)
            if ($js.Count -gt 0) {
                Write-Host "--- 最近 journal ---"
                $js | ForEach-Object { Write-Host ("  {0}  {1} B" -f $_.FullName, $_.Length) }
            }
        }
        exit 0
    }

    "ls" {
        if (-not $Path) { $Path = '/' }
        Invoke-Imgfs @('ls', $Path, '--long')
        exit 0
    }

    "pull" {
        if (-not $Path -or -not $Local) { throw "pull 需要 -Path <镜像路径> -Local <本地文件>" }
        Invoke-Imgfs @('get', $Path, $Local)
        exit 0
    }

    "push" {
        if (-not $Path -or -not $Local) { throw "push 需要 -Local <本地文件> -Path <镜像路径>" }
        $flags = @('put', $Local, $Path)
        if ($WhatIf) { $flags += '--dry-run' }
        Invoke-Imgfs $flags
        exit 0
    }

    "sync-release" {
        $src = Join-Path $root $From
        $items = @(
            @{ Local = Join-Path $src 'shuihu-edit-GUI.exe'; Image = '/RELEASE/shuihu-edit-GUI.exe'; Required = $true }
        )
        if ($IncludeIni) {
            $items += @{ Local = Join-Path $src 'shuihu-edit.ini'; Image = '/RELEASE/shuihu-edit.ini'; Required = $false }
        }
        if ($IncludeDocs) {
            $items += @{ Local = Join-Path $src '说明.txt'; Image = '/RELEASE/说明.txt'; Required = $false }
        }
        if ($IncludeLogo) {
            $items += @{ Local = Join-Path $src 'logo.ico'; Image = '/RELEASE/logo.ico'; Required = $false }
        }
        $any = $false
        foreach ($it in $items) {
            if (-not (Test-Path -LiteralPath $it.Local)) {
                if ($it.Required) { throw "找不到 $($it.Local)（先运行 build\build-tcc.bat）" }
                Write-Host ("跳过（不存在）: {0}" -f $it.Local)
                continue
            }
            $any = $true
            $localHash = (Get-FileHash -LiteralPath $it.Local -Algorithm SHA256).Hash
            Invoke-Imgfs @('diff', $it.Image, $it.Local) -AllowFail
            $code = $script:LastCode
            if ($code -eq 0) {
                Write-Host ("相同，跳过: {0}  sha256={1}" -f $it.Image, $localHash)
                continue
            }
            if ($code -ne 1 -and $code -ne 2) {
                Write-Host ("[错误] diff 失败，退出码 {0}" -f $code)
                exit $code
            }
            if ($WhatIf) {
                Write-Host ("WhatIf: 将 put {0} -> {1}" -f $it.Local, $it.Image)
                continue
            }
            Invoke-Imgfs @('put', $it.Local, $it.Image)
        }
        if (-not $any) { throw "没有可同步的发布文件" }
        exit 0
    }

    "pull-saves" {
        if ($To) { $out = $To } else { $out = $vmDir }
        Ensure-Dir $out
        $manifestPath = Join-Path $out 'manifest.json'
        $oldSlots = @{}
        if (Test-Path -LiteralPath $manifestPath) {
            try {
                $old = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
                if ($old -and $old.slots) {
                    foreach ($prop in $old.slots.PSObject.Properties) {
                        $oldSlots[$prop.Name] = $prop.Value.sha256
                    }
                }
            } catch { }
        }
        $slots = @{}
        Write-Host ("{0,-12} {1,8}  {2}" -f 'slot', 'size', 'sha256')
        for ($i = 0; $i -le 9; $i++) {
            $name = "save$i.sav"
            $imgPath = "/SH/SAVE/$name"
            $local = Join-Path $out $name
            Invoke-Imgfs @('get', $imgPath, $local) -AllowFail
            if ($script:LastCode -ne 0) {
                Write-Host ("{0,-12} {1}" -f $name, '(missing)')
                continue
            }
            $size = (Get-Item -LiteralPath $local).Length
            $hash = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
            $flag = ''
            if ($oldSlots.ContainsKey($name) -and $oldSlots[$name] -ne $hash) { $flag = ' CHANGED' }
            Write-Host ("{0,-12} {1,8}  {2}{3}" -f $name, $size, $hash, $flag)
            $slots[$name] = @{ size = $size; sha256 = $hash }
        }
        $manifest = @{
            timestamp = (Get-Date).ToString('s')
            image = (Resolve-Path -LiteralPath $ImagePath).Path
            slots = $slots
        }
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        Write-Host ("manifest: {0}" -f $manifestPath)
        exit 0
    }

    "probe" {
        if (-not $Field) {
            throw "probe 需要 -Field（atk/def/hit/agi/crit/level/hp/hpmax/mp/mpmax/move 等，见 CLI 用法）与 -Value"
        }
        if ($Slot -lt 0 -or $Slot -gt 9) { throw "-Slot 应为 0..9" }
        Ensure-Dir $vmDir
        $name = "save$Slot.sav"
        $imgPath = "/SH/SAVE/$name"
        $before = Join-Path $vmDir "$name.before.sav"
        $probe = Join-Path $vmDir "$name.probe.sav"
        Invoke-Imgfs @('get', $imgPath, $before)
        Copy-Item -LiteralPath $before -Destination $probe -Force
        if (-not (Test-Path -LiteralPath $cli)) {
            throw "找不到 $cli；先运行 build\build-tcc.bat"
        }
        $beforeHash = (Get-FileHash -LiteralPath $before -Algorithm SHA256).Hash
        & $cli -set $probe "$Char" $Field "$Value" -y
        if ($LASTEXITCODE -ne 0) {
            throw ("CLI 失败，退出码 {0}（字段 {1} 值 {2}）" -f $LASTEXITCODE, $Field, $Value)
        }
        $probeHash = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
        Write-Host ("原档: {0}  {1} B  sha256={2}" -f $before, (Get-Item -LiteralPath $before).Length, $beforeHash)
        Write-Host ("探针: {0}  {1} B  sha256={2}" -f $probe, (Get-Item -LiteralPath $probe).Length, $probeHash)
        if ($WhatIf) {
            Write-Host "WhatIf: 不写回镜像"
            exit 0
        }
        Invoke-Imgfs @('put', $probe, $imgPath)
        $meta = @{
            timestamp = (Get-Date).ToString('s')
            image = (Resolve-Path -LiteralPath $ImagePath).Path
            slot = $Slot
            char = $Char
            field = $Field
            value = $Value
            before_sha = $beforeHash
            probe_sha = $probeHash
        }
        $meta | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $vmDir 'probe.json') -Encoding UTF8
        Write-Host ""
        Write-Host ("下一步：启动 DOSBox -> 读第 {0} 档 -> 角色 id {1} 的 {2} 应为 {3} -> 退出" -f $Slot, $Char, $Field, $Value)
        Write-Host "退出后运行: powershell -File tools\vmtest.ps1 pull-saves"
        exit 0
    }

    "backup" {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        if ($Backup) { $dest = $Backup } else { $dest = Join-Path (Split-Path $ImagePath) "backup\windows98-$stamp.img" }
        Invoke-Imgfs @('backup', $dest)
        exit 0
    }

    "restore" {
        if (-not $Backup) { throw "restore 需要 -Backup <备份文件>" }
        if (-not (Test-Path -LiteralPath $Backup)) { throw "备份不存在：$Backup" }
        if (-not $Yes) { throw "restore 是覆盖整个镜像的破坏性操作，需要 -Yes" }
        if ((Get-Dosbox).Count -gt 0) { throw "先退出 DOSBox，再 restore" }
        $srcSize = (Get-Item -LiteralPath $Backup).Length
        $dstSize = (Get-Item -LiteralPath $ImagePath).Length
        if ($srcSize -ne $dstSize) { throw "备份大小 $srcSize 与镜像 $dstSize 不一致" }
        Copy-Item -LiteralPath $Backup -Destination $ImagePath -Force
        $h1 = (Get-FileHash -LiteralPath $Backup -Algorithm SHA256).Hash
        $h2 = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash
        if ($h1 -ne $h2) { throw "restore 后哈希不一致" }
        Write-Host ("restore 完成: {0} -> {1}  sha256={2}" -f $Backup, $ImagePath, $h2)
        exit 0
    }

    "check" {
        Invoke-Imgfs @('check')
        exit 0
    }

    "clone" {
        $dir = Split-Path $ImagePath -Parent
        $real = $defaultImage
        if (-not (Test-Path -LiteralPath $real)) { throw "找不到原始镜像：$real" }
        $testImg = Join-Path $dir 'windows98.test.img'
        if (-not (Test-Path -LiteralPath $testImg)) {
            Write-Host ("复制克隆镜像（约 2 GB）: {0}" -f $testImg)
            Copy-Item -LiteralPath $real -Destination $testImg
        } else {
            Write-Host ("克隆已存在: {0}" -f $testImg)
        }
        $confSrc = Join-Path $dir 'CD1.conf'
        $confDst = Join-Path $dir 'CD1-test.conf'
        if (-not (Test-Path -LiteralPath $confSrc)) { throw "找不到 $confSrc" }
        $raw = Get-Content -LiteralPath $confSrc -Raw
        $raw = $raw -replace 'windows98\.img', 'windows98.test.img'
        Set-Content -LiteralPath $confDst -Value $raw -Encoding ASCII
        Write-Host ("测试配置: {0}" -f $confDst)
        Write-Host ("启动测试系统: cd `"{0}`"; .\dosbox.exe -conf CD1-test.conf" -f $dir)
        exit 0
    }

    "rollback" {
        if (-not $Journal) { throw "rollback 需要 -Journal <journal.bin>" }
        Invoke-Imgfs @('rollback', $Journal)
        exit 0
    }

    "info" {
        Invoke-Imgfs @('info')
        exit 0
    }

    default {
        Show-Usage
        exit 0
    }
}
