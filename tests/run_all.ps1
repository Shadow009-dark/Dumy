# DUMY regression suite (PowerShell 5.1+, Windows)
# Starts the server if it is not already answering /health, posts every case
# with curl.exe + a UTF-8 JSON payload file and asserts content + latency.
# Writes test_results/latest.md and exits non-zero on any failure.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = Join-Path $root 'build\audit\Release\dumy.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $root 'build\Release\dumy.exe' }
$healthUrl = 'http://127.0.0.1:8085/health'
$chatUrl = 'http://127.0.0.1:8085/chat'
$casesPath = Join-Path $PSScriptRoot 'expected\cases.json'
$resultsDir = Join-Path $root 'test_results'
$tmpDir = Join-Path $root 'test_results\payloads'
New-Item -ItemType Directory -Force -Path $resultsDir, $tmpDir | Out-Null

function Get-Health {
    try {
        $r = Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 3
        return ($r.Content | ConvertFrom-Json)
    } catch { return $null }
}

$server = $null
$h = Get-Health
if (-not $h -or -not $h.warmup.main_ready) {
    if (-not (Test-Path $exe)) { Write-Error "dumy.exe not found: $exe"; exit 2 }
    Write-Host "Starting DUMY server..."
    $server = Start-Process -FilePath $exe -ArgumentList '--no-browser' -WorkingDirectory $root -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 90; $i++) {
        Start-Sleep -Seconds 2
        $h = Get-Health
        if ($h -and $h.warmup.main_ready) { break }
    }
}
if (-not $h -or -not $h.warmup.main_ready) {
    Write-Error "server did not become ready on $healthUrl"
    if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    exit 2
}

$cases = (Get-Content $casesPath -Raw -Encoding UTF8 | ConvertFrom-Json).cases
$rows = New-Object System.Collections.Generic.List[object]
$failures = 0

foreach ($c in $cases) {
    $payloadPath = Join-Path $tmpDir ($c.id + '.json')
    $payload = @{ session_id = 'suite-' + $c.id; message = $c.question } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($payloadPath, $payload, (New-Object System.Text.UTF8Encoding($false)))

    $respPath = Join-Path $tmpDir ($c.id + '.response.json')
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $httpCode = & curl.exe -s -m 90 -o $respPath -w '%{http_code}' -H 'Content-Type: application/json; charset=utf-8' --data-binary "@$payloadPath" $chatUrl
    $sw.Stop()
    $raw = [IO.File]::ReadAllText($respPath, [Text.Encoding]::UTF8)
    $seconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)

    $problems = New-Object System.Collections.Generic.List[string]
    if ([string]$httpCode -ne '200') { $problems.Add("HTTP $httpCode") | Out-Null }
    $answer = ''
    $qtype = ''
    try {
        $j = $raw | ConvertFrom-Json
        $answer = [string]$j.answer
        $qtype = [string]$j.qtype
    } catch {
        $problems.Add('response is not JSON') | Out-Null
    }

    if ([string]::IsNullOrWhiteSpace($answer)) { $problems.Add('empty answer') | Out-Null }
    foreach ($marker in @('HTTP 400', 'HTTP 500', 'exceeds', 'invalid JSON', 'curl:')) {
        if ($answer -match [regex]::Escape($marker)) { $problems.Add("raw error marker: $marker") | Out-Null }
    }
    if ($c.qtype -and $qtype -ne $c.qtype) { $problems.Add("qtype $qtype != $($c.qtype)") | Out-Null }
    foreach ($needle in @($c.expect_contains)) {
        if ($needle -and $answer.IndexOf([string]$needle, [StringComparison]::OrdinalIgnoreCase) -lt 0) { $problems.Add("missing: $needle") | Out-Null }
    }
    foreach ($needle in @($c.expect_not_contains)) {
        if ($needle -and $answer.IndexOf([string]$needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $problems.Add("unexpected: $needle") | Out-Null }
    }
    if ($answer -match '(?i)(\b\p{L}+\b)(\s+\1){4,}') { $problems.Add('repetition loop') | Out-Null }
    if ($seconds -gt [double]$c.max_seconds) { $problems.Add("slow: ${seconds}s > $($c.max_seconds)s") | Out-Null }

    $ok = ($problems.Count -eq 0)
    if (-not $ok) { $failures++ }
    $rows.Add([pscustomobject]@{
        Case = $c.id; Pass = $ok; Seconds = $seconds; Qtype = $qtype
        Problem = ($problems -join '; '); Answer = $answer
    }) | Out-Null
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("{0,-22} {1}  {2,6}s  {3}" -f $c.id, $status, $seconds, ($problems -join '; '))
}

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine('# DUMY regression results')
[void]$md.AppendLine('')
[void]$md.AppendLine(('Date: {0}  |  model: {1}  |  cases: {2}  |  failed: {3}' -f (Get-Date), $h.chat_model, $rows.Count, $failures))
[void]$md.AppendLine('')
[void]$md.AppendLine('| Case | Result | Seconds | qtype | Problem |')
[void]$md.AppendLine('|---|---|---|---|---|')
foreach ($r in $rows) {
    $res = if ($r.Pass) { 'PASS' } else { 'FAIL' }
    [void]$md.AppendLine(('| {0} | {1} | {2} | {3} | {4} |' -f $r.Case, $res, $r.Seconds, $r.Qtype, ($r.Problem -replace '\|', '/')))
}
[void]$md.AppendLine('')
[void]$md.AppendLine('## Answers')
foreach ($r in $rows) {
    [void]$md.AppendLine('')
    [void]$md.AppendLine(('### {0} ({1})' -f $r.Case, $(if ($r.Pass) { 'PASS' } else { 'FAIL' })))
    [void]$md.AppendLine('```')
    [void]$md.AppendLine($r.Answer)
    [void]$md.AppendLine('```')
}
[IO.File]::WriteAllText((Join-Path $resultsDir 'latest.md'), $md.ToString(), (New-Object System.Text.UTF8Encoding($false)))

if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
Write-Host ""
Write-Host "Report: test_results\latest.md  |  $($rows.Count - $failures)/$($rows.Count) passed"
if ($failures -gt 0) { exit 1 }
exit 0
