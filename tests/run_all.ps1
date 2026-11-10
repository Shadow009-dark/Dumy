# DUMY regression suite (PowerShell 5.1+, Windows)
#
# Safety rules (ADD-2):
#  - refuses to run if port 8085 is held by a process it did not start
#    (pass -ReuseServer to test an already running server deliberately);
#  - verifies the /health identity (build id + start time + pid) belongs to the
#    server it launched, and that it did not change during the run;
#  - a livestock case (t16) must return at least one PDF source when the corpus
#    is loaded; with chunks=0 it reports "corpus missing" as a WARNING, never a
#    silent pass.
#
# Cases come from tests\expected\cases.json and every tests\cases\*.json file.
# Writes test_results\latest.md and exits non-zero on any failure.

param([switch]$ReuseServer)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = Join-Path $root 'build\Release\dumy.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $root 'build\audit\Release\dumy.exe' }

$port = 8085
$healthUrl = "http://127.0.0.1:$port/health"
$chatUrl = "http://127.0.0.1:$port/chat"
$resultsDir = Join-Path $root 'test_results'
$tmpDir = Join-Path $resultsDir 'payloads'
New-Item -ItemType Directory -Force -Path $resultsDir, $tmpDir | Out-Null

function Get-Health {
    try {
        $r = Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 3
        return ($r.Content | ConvertFrom-Json)
    } catch { return $null }
}

function Get-ListenerPid {
    try {
        $c = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
        if ($c) { return [int](@($c)[0].OwningProcess) }
    } catch {
        $line = (& netstat -ano | Select-String -Pattern ":$port\s" | Select-String 'LISTENING' | Select-Object -First 1)
        if ($line) {
            $parts = ($line.ToString().Trim() -split '\s+')
            return [int]$parts[-1]
        }
    }
    return 0
}

function Get-GarbageRatio([string]$text) {
    $words = @([regex]::Matches($text, '[\p{L}]+') | ForEach-Object { $_.Value })
    if ($words.Count -eq 0) { return 0.0 }
    $bad = 0
    foreach ($w in $words) {
        if ($w -match '^[A-Za-z]+$') { continue }          # Latin terms are fine
        if ($w -match '[bcçdfgğhjklmnprsştvyz]{5,}') { $bad++ }
    }
    return [math]::Round($bad / $words.Count, 3)
}

function Test-TruncatedWord([string]$answer) {
    $t = $answer.TrimEnd()
    if ($t.Length -eq 0) { return $false }
    $last = $t[$t.Length - 1]
    if ('.:!?)]}"»…'.Contains([string]$last)) { return $false }
    $m = [regex]::Match($t, '([\p{L}]+)$')
    if ($m.Success -and $m.Groups[1].Value.Length -le 3) { return $true }
    if ($last -eq '-' -or $last -eq '[' -or $last -eq '(') { return $true }
    return $false
}

$listenerPid = Get-ListenerPid
$server = $null
$h = $null
$identity = ''

if ($listenerPid -ne 0) {
    if (-not $ReuseServer) {
        Write-Host "ERROR: port $port is already in use by PID $listenerPid," -ForegroundColor Red
        Write-Host "and this script did not start it. Stop that process first, or pass" -ForegroundColor Red
        Write-Host "-ReuseServer to test the already running server (identity check skipped)." -ForegroundColor Red
        exit 3
    }
    Write-Host "Reusing the server already listening on port $port (PID $listenerPid)."
    $h = Get-Health
} else {
    if (-not (Test-Path $exe)) { Write-Error "dumy.exe not found: $exe"; exit 2 }
    Write-Host "Starting DUMY server..."
    $server = Start-Process -FilePath $exe -ArgumentList '--no-browser','--no-web' `
                            -WorkingDirectory $root -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 90; $i++) {
        Start-Sleep -Seconds 2
        $h = Get-Health
        if ($h -and $h.warmup.main_ready) { break }
    }
    if ($h -and $h.warmup.main_ready) {
        if ([int]$h.pid -ne [int]$server.Id) {
            Write-Host "ERROR: /health reports pid $($h.pid) but this script started pid $($server.Id)." -ForegroundColor Red
            Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
            exit 2
        }
        $identity = "$($h.build_id)|$($h.started_at)|$($h.pid)"
        Write-Host "Server identity: pid=$($h.pid) build=$($h.build_id) started=$($h.started_at)"
    }
}

if (-not $h -or -not $h.warmup.main_ready) {
    Write-Error "server did not become ready on $healthUrl"
    if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    exit 2
}

$corpusChunks = 0
try { $corpusChunks = [int]$h.chunks } catch { $corpusChunks = 0 }
$corpusMissing = ($corpusChunks -eq 0)
if ($corpusMissing) {
    Write-Host "WARNING: corpus missing (chunks=0) - PDF-source assertions will be reported as WARN." -ForegroundColor Yellow
}

$cases = @()
$expectedPath = Join-Path $PSScriptRoot 'expected\cases.json'
if (Test-Path $expectedPath) {
    $cases += @((Get-Content $expectedPath -Raw -Encoding UTF8 | ConvertFrom-Json).cases)
}
$casesDir = Join-Path $PSScriptRoot 'cases'
if (Test-Path $casesDir) {
    foreach ($f in (Get-ChildItem $casesDir -Filter *.json | Sort-Object Name)) {
        $cases += @(Get-Content $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json)
    }
}
if ($cases.Count -eq 0) { Write-Error "no test cases found"; exit 2 }

$rows = New-Object System.Collections.Generic.List[object]
$failures = 0
$warnings = 0

foreach ($c in $cases) {
    $payloadPath = Join-Path $tmpDir ($c.id + '.json')
    $payload = @{ session_id = 'suite-' + $c.id; message = $c.question } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($payloadPath, $payload, (New-Object System.Text.UTF8Encoding($false)))

    $respPath = Join-Path $tmpDir ($c.id + '.response.json')
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $httpCode = & curl.exe -s -m 90 -o $respPath -w '%{http_code}' -H 'Content-Type: application/json; charset=utf-8' --data-binary "@$payloadPath" $chatUrl
    $sw.Stop()
    $raw = ''
    if (Test-Path $respPath) { $raw = [IO.File]::ReadAllText($respPath, [Text.Encoding]::UTF8) }
    $seconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)

    $problems = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]

    if ([string]$httpCode -ne '200') {
        $problems.Add("network error: HTTP $httpCode") | Out-Null
    }
    $answer = ''
    $qtype = ''
    $pdfSources = 0
    try {
        $j = $raw | ConvertFrom-Json
        $answer = [string]$j.answer
        $qtype = [string]$j.qtype
        $pdfSources = @($j.sources | Where-Object { $_.type -eq 'pdf' }).Count
    } catch {
        $problems.Add('response is not JSON') | Out-Null
    }

    if ([string]::IsNullOrWhiteSpace($answer)) { $problems.Add('empty answer') | Out-Null }
    foreach ($marker in @('HTTP 400', 'HTTP 500', 'Failed to fetch', 'network error', 'invalid JSON', 'curl:')) {
        if ($answer -match [regex]::Escape($marker)) { $problems.Add("raw error marker: $marker") | Out-Null }
    }
    if ($c.qtype -and $qtype -ne $c.qtype) { $problems.Add("qtype $qtype != $($c.qtype)") | Out-Null }
    if ($c.qtype_in) {
        if (@($c.qtype_in) -notcontains $qtype) {
            $problems.Add("qtype $qtype not in [$(@($c.qtype_in) -join ', ')]") | Out-Null
        }
    }
    foreach ($needle in @($c.expect_contains)) {
        if ($needle -and $answer.IndexOf([string]$needle, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
            $problems.Add("missing: $needle") | Out-Null
        }
    }
    foreach ($needle in @($c.expect_not_contains)) {
        if ($needle -and $answer.IndexOf([string]$needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $problems.Add("unexpected: $needle") | Out-Null
        }
    }
    if ($c.expect_verdict) {
        if (-not [regex]::IsMatch($answer.Trim(), '^(Doğrudur|Qismən doğrudur|Səhvdir|Dəqiq bilmirəm)(\.|\s|$)')) {
            $problems.Add('missing verdict word') | Out-Null
        }
    }
    if ($null -ne $c.max_pdf_sources -and $pdfSources -gt [int]$c.max_pdf_sources) {
        $problems.Add("PDF sources $pdfSources > $($c.max_pdf_sources)") | Out-Null
    }
    if ($null -ne $c.min_pdf_sources -and $pdfSources -lt [int]$c.min_pdf_sources) {
        if ($corpusMissing) {
            $notes.Add('corpus missing: PDF source assertion not evaluated') | Out-Null
        } else {
            $problems.Add("PDF sources $pdfSources < $($c.min_pdf_sources)") | Out-Null
        }
    }
    if ($null -ne $c.max_garbage_ratio) {
        $ratio = Get-GarbageRatio $answer
        if ($ratio -gt [double]$c.max_garbage_ratio) {
            $problems.Add("garbage ratio $ratio > $($c.max_garbage_ratio)") | Out-Null
        }
    }
    if ($c.no_truncated_word -ne $false -and (Test-TruncatedWord $answer)) {
        $problems.Add('answer ends mid-word') | Out-Null
    }
    if ($answer -match '(?i)(\b\p{L}+\b)(\s+\1){4,}') { $problems.Add('repetition loop') | Out-Null }
    $maxSeconds = 30.0
    if ($null -ne $c.max_seconds) { $maxSeconds = [double]$c.max_seconds }
    if ($seconds -gt $maxSeconds) { $problems.Add("slow: ${seconds}s > ${maxSeconds}s") | Out-Null }

    $ok = ($problems.Count -eq 0)
    $status = 'PASS'
    if (-not $ok) { $failures++; $status = 'FAIL' }
    elseif ($notes.Count -gt 0) { $warnings++; $status = 'WARN' }
    $rows.Add([pscustomobject]@{
        Case = $c.id; Status = $status; Seconds = $seconds; Qtype = $qtype
        Pdf = $pdfSources
        Problem = (($problems + $notes) -join '; '); Answer = $answer
    }) | Out-Null
    Write-Host ("{0,-24} {1,-4} {2,6}s  pdf={3}  {4}" -f $c.id, $status, $seconds, $pdfSources, (($problems + $notes) -join '; '))
}

# The server we launched must still be the same process with the same build.
$healthError = ''
if (-not $ReuseServer -and $identity -ne '') {
    $h2 = Get-Health
    if (-not $h2) {
        $healthError = 'server stopped answering /health during the run'
    } elseif ("$($h2.build_id)|$($h2.started_at)|$($h2.pid)" -ne $identity) {
        $healthError = "server identity changed during the run (now pid=$($h2.pid) build=$($h2.build_id))"
    }
    if ($healthError -ne '') { $failures++; Write-Host "FAIL: $healthError" -ForegroundColor Red }
}

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine('# DUMY regression results')
[void]$md.AppendLine('')
[void]$md.AppendLine(('Date: {0}  |  model: {1}  |  chunks: {2}  |  cases: {3}  |  failed: {4}  |  warned: {5}' -f (Get-Date), $h.chat_model, $corpusChunks, $rows.Count, $failures, $warnings))
[void]$md.AppendLine('')
if ($corpusMissing) { [void]$md.AppendLine('> **corpus missing** (chunks=0): PDF-source assertions were reported as WARN, not PASS.') }
if ($healthError -ne '') { [void]$md.AppendLine('> **server identity check failed**: ' + $healthError) }
[void]$md.AppendLine('| Case | Result | Seconds | qtype | PDF | Problem |')
[void]$md.AppendLine('|---|---|---|---|---|---|')
foreach ($r in $rows) {
    [void]$md.AppendLine(('| {0} | {1} | {2} | {3} | {4} | {5} |' -f $r.Case, $r.Status, $r.Seconds, $r.Qtype, $r.Pdf, ($r.Problem -replace '\|', '/')))
}
[void]$md.AppendLine('')
[void]$md.AppendLine('## Answers')
foreach ($r in $rows) {
    [void]$md.AppendLine('')
    [void]$md.AppendLine(('### {0} ({1})' -f $r.Case, $r.Status))
    [void]$md.AppendLine('```')
    [void]$md.AppendLine($r.Answer)
    [void]$md.AppendLine('```')
}
[IO.File]::WriteAllText((Join-Path $resultsDir 'latest.md'), $md.ToString(), (New-Object System.Text.UTF8Encoding($false)))

if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
Write-Host ""
Write-Host "Report: test_results\latest.md  |  $($rows.Count - $failures)/$($rows.Count) passed, $warnings warning(s)"
if ($failures -gt 0) { exit 1 }
exit 0
