# reportcheck.ps1 —— 「对话发现 ↔ 报告」对账提醒
#
# 用途：从最近的 git 提交信息里抽出"事实性关键词"，逐个在分析报告里查是否存在，
#       把没命中的列出来，提醒人工复核是否漏写进报告。
#
# 它只做提醒，不能代替判断：
#   - 关键词命中 ≠ 结论写对了
#   - 关键词未命中 ≠ 一定漏了（可能换了个说法）
#
# 用法:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\reportcheck.ps1
#   powershell ... -File tools\reportcheck.ps1 -Since 5          # 看最近 5 个提交
#   powershell ... -File tools\reportcheck.ps1 -Terms "+172","tid=32"   # 手工给关键词
#
# 退出码: 0 = 没发现疑似遗漏；1 = 有未命中项（需人工确认）

param(
    [int]$Since = 3,
    [string[]]$Terms
)

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$report = Join-Path (Split-Path $root -Parent) 'ShuiHu-分析报告.md'

if (-not (Test-Path $report)) { throw "找不到分析报告: $report" }
$repTxt = [IO.File]::ReadAllText($report, [Text.Encoding]::UTF8)

# ---------------------------------------------------------------- 取关键词
if ($Terms) { $Terms = ($Terms -join ";") -split "[;,]" | Where-Object { $_.Trim() } }
if (-not $Terms -or $Terms.Count -eq 0) {
    Push-Location $root
    $log = git log -n $Since --pretty=format:"%B" 2>&1 | Out-String
    Pop-Location
    $cands = New-Object System.Collections.Generic.List[string]

    # 偏移 / 十六进制 / 数值型
    foreach ($m in [regex]::Matches($log, '0x[0-9A-Fa-f]{3,6}')) { $cands.Add($m.Value) }
    foreach ($m in [regex]::Matches($log, '(?<![\w])\+\d{2,3}(?![\w])')) { $cands.Add($m.Value) }
    # tid=NN
    foreach ($m in [regex]::Matches($log, 'tid\s*=\s*\d+')) { $cands.Add(($m.Value -replace '\s','')) }
    # 反引号/引号里的标识符与文件
    foreach ($m in [regex]::Matches($log, '`([^`\r\n]{3,40})`')) { $cands.Add($m.Groups[1].Value) }
    foreach ($m in [regex]::Matches($log, '3C ?00')) { $cands.Add('3C 00') }
    # 统计:  ×27 之类
    foreach ($m in [regex]::Matches($log, '×\s*\d+')) { $cands.Add(($m.Value -replace '\s','')) }

    $Terms = $cands | Where-Object { $_.Trim().Length -ge 2 } | Sort-Object -Unique
}

# ---------------------------------------------------------------- 查报告

# ---------------------------------------------------------------- 结论清单
Push-Location $root
$logLines = git log -n $Since --pretty=format:"%h %s%n%b" 2>&1 | Out-String
Pop-Location
Write-Host "------ 最近提交里的结论条目（请逐条确认报告里有没有）------" -ForegroundColor Cyan
foreach ($ln in ($logLines -split "`r?`n")) {
    $s = $ln.Trim()
    if ($s -match '^[0-9a-f]{7,} ' -or $s -match '^- ' -or $s -match '^\* ') {
        Write-Host ("  " + $s) -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "====== 对账提醒（关键词是否出现在报告里）======" -ForegroundColor Cyan
Write-Host ("报告: " + $report)
Write-Host ("关键词数: " + $Terms.Count)
Write-Host ""

$miss = @()
foreach ($t in $Terms) {
    $esc = [regex]::Escape($t)
    $hit = [regex]::IsMatch($repTxt, $esc)
    if (-not $hit) { $miss += $t }
}

if ($miss.Count -eq 0) {
    Write-Host "  全部命中：这些关键词在报告里都能找到。" -ForegroundColor Green
    Write-Host "  （注意：命中只说明出现过，不保证结论写全）" -ForegroundColor DarkGray
    Write-Host ""
    exit 0
}

Write-Host ("  以下 " + $miss.Count + " 个关键词在报告里没找到，逐条确认是否需要补写：" ) -ForegroundColor Yellow
foreach ($t in $miss) { Write-Host ("    - " + $t) -ForegroundColor Yellow }
Write-Host ""
Write-Host "  补写后记得：追加报告的版本号与变更记录；README 引用的版本号同步（doccheck 会校验）。" -ForegroundColor DarkGray
Write-Host ""
exit 1
