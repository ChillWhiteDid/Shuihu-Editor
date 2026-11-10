# doccheck.ps1 —— 文档一致性自检
#
# 起因：README 曾说"src\*_data.c 已随仓库提供"，但 .gitignore 明确排除它们；
# 又说"生成的 *_data.c"只有两个，实际三个。这类"文档说的 ≠ 配置做的"矛盾，
# 靠人读很容易漏，所以做成断言。
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\doccheck.ps1
#
# 退出码: 0 = 全部通过；1 = 有失败项

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$readme = Join-Path $root 'README.md'
$gitignore = Join-Path $root '.gitignore'
$buildTcc = Join-Path $root 'build\build-tcc.bat'
$buildW11 = Join-Path $root 'build\build-w11.bat'
$selftest = Join-Path $root 'tools\selftest.ps1'

$pass = 0; $fail = 0
function Ok($name, $cond, $detail) {
    if ($cond) {
        Write-Host ("  [OK]   " + $name) -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host ("  [失败] " + $name + $(if ($detail) { " —— " + $detail } else { "" })) -ForegroundColor Red
        $script:fail++
    }
}

if (-not (Test-Path $readme)) { throw "找不到 README.md" }
$md = [IO.File]::ReadAllText($readme, [Text.Encoding]::UTF8)
$gi = if (Test-Path $gitignore) { [IO.File]::ReadAllText($gitignore, [Text.Encoding]::UTF8) } else { '' }

Write-Host ""
Write-Host "====== 文档一致性自检 ======" -ForegroundColor Cyan
Write-Host ""

# ---- 1. 数据表入库说法 vs .gitignore ----
$genIgnored = ($gi -match 'src[/\\]names_data\.c') -and ($gi -match 'src[/\\]items_data\.c') -and ($gi -match 'src[/\\]human_data\.c')
$claimShipped = $md -match '已随仓库提供|已包含在仓库|随仓库附带|自带数据表'
Ok "数据表：.gitignore 与 README 说法一致" (-not ($genIgnored -and $claimShipped)) `
    ".gitignore 排除 *_data.c，但 README 仍称其随仓库提供"

Ok "数据表：README 明确'不随仓库提供'" ($md -match '不随仓库提供|都不随仓库') `
    "README 缺少'数据表不随仓库提供'的说明"

# ---- 2. 三个 *_data.c 的名字在 README 与构建脚本里一致 ----
$names = @('names_data.c', 'items_data.c', 'human_data.c')
$allInReadme = ($names | Where-Object { $md -notmatch [regex]::Escape($_) }).Count -eq 0
Ok "三个 *_data.c 都在 README 里出现" $allInReadme "README 漏了某个文件名"

$bt = if (Test-Path $buildTcc) { [IO.File]::ReadAllText($buildTcc) } else { '' }
$bw = if (Test-Path $buildW11) { [IO.File]::ReadAllText($buildW11) } else { '' }
$inBuild = ($names | Where-Object { $bt -notmatch [regex]::Escape($_) -or $bw -notmatch [regex]::Escape($_) }).Count -eq 0
Ok "三个 *_data.c 都在构建脚本里被检查" $inBuild "构建脚本漏了某个文件名"

# ---- 3. 脚本编码 ----
$psFiles = Get-ChildItem $root -Recurse -Filter '*.ps1' -File
$noBom = @()
foreach ($f in $psFiles) {
    $b = [IO.File]::ReadAllBytes($f.FullName)
    if ($b.Length -lt 3 -or $b[0] -ne 0xEF -or $b[1] -ne 0xBB -or $b[2] -ne 0xBF) {
        $noBom += $f.Name
    }
}
Ok "所有 .ps1 都是 UTF-8 带 BOM" ($noBom.Count -eq 0) ("缺 BOM: " + ($noBom -join ', '))

$batFiles = Get-ChildItem $root -Recurse -Filter '*.bat' -File
$nonAscii = @()
foreach ($f in $batFiles) {
    $b = [IO.File]::ReadAllBytes($f.FullName)
    if (($b | Where-Object { $_ -gt 0x7F }).Count -gt 0) { $nonAscii += $f.Name }
}
Ok "所有 .bat 都是纯 ASCII" ($nonAscii.Count -eq 0) ("含非 ASCII: " + ($nonAscii -join ', '))

# ---- 4. README 提到的仓库内路径确实存在 ----
$refs = [regex]::Matches($md, '`((?:src|tools|build|assets)[\\/][^`\s]+?)`') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$missing = @()
foreach ($r in $refs) {
    if ($r -match '\*') { continue }                      # 通配符跳过
    if ($r -match '_data\.c$') { continue }               # 生成物，本就不存在
    $p = Join-Path $root ($r -replace '/', '\')
    if (-not (Test-Path $p)) { $missing += $r }
}
Ok "README 引用的仓库内文件都存在" ($missing.Count -eq 0) ("缺: " + ($missing -join ', '))

# ---- 5. 章节编号连续、无重复 ----
$nums = @('零','一','二','三','四','五','六','七','八','九','十','十一','十二','十三','十四','十五','十六','十七','十八')
$heads = [regex]::Matches($md, '(?m)^## ([零一二三四五六七八九十]+)、') | ForEach-Object { $_.Groups[1].Value }
$expected = $nums[0..($heads.Count - 1)]
Ok "章节编号连续（零…）" (($heads -join ',') -eq ($expected -join ',')) ("实际: " + ($heads -join ','))
Ok "章节标题无重复" (($heads | Sort-Object -Unique).Count -eq $heads.Count) "存在重复编号"

# ---- 6. 计数口径与 selftest 实际输出一致 ----
$st = if (Test-Path $selftest) { [IO.File]::ReadAllText($selftest, [Text.Encoding]::UTF8) } else { '' }
$scriptSaysGroup = $st -match '跳过 \{2\} 组'
$readmeSaysGroup = $md -match '跳过按"组"计'
Ok "跳过计数口径：README 与 selftest 一致" ($scriptSaysGroup -eq $readmeSaysGroup) `
    ("selftest 输出组=$scriptSaysGroup, README 说明组=$readmeSaysGroup")

# ---- 7. 文档中的旧名残留 ----
$stale = @('release-w98','shuihu-save.exe','shuihu-edit-tcc','版本说明.txt')
$found = @()
foreach ($s in $stale) { if ($md -match [regex]::Escape($s)) { $found += $s } }
Ok "无旧名称残留" ($found.Count -eq 0) ("发现: " + ($found -join ', '))

# ---- 8. 目录树里不重复列同一文件 ----
$dupTree = ([regex]::Matches($md, 'selftest\.ps1\s+回归自检')).Count
Ok "目录树未重复列 selftest.ps1" ($dupTree -le 1) ("出现 $dupTree 次")

Write-Host ""
Write-Host ("结果: 通过 {0} 项，失败 {1} 项" -f $pass, $fail) `
    -ForegroundColor $(if ($fail -eq 0) { 'Green' } else { 'Red' })
Write-Host ""
exit $(if ($fail -eq 0) { 0 } else { 1 })
