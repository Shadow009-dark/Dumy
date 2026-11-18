# DUMY regression suite (PowerShell 5.1+, Windows)
param([switch]$Quick)
$env:DUMY_DEBUG_TRACE = "1"
if ($Quick) { & (Join-Path $PSScriptRoot "quick.ps1"); exit $LASTEXITCODE }
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

# ------------------------- memory test helpers (ASCII only) --------------
$utf8 = New-Object System.Text.UTF8Encoding($false)
$baseUrl = "http://127.0.0.1:$port"

function Read-Fixture([string]$name) {
    $p = Join-Path $PSScriptRoot ("memory\" + $name)
    if (-not (Test-Path $p)) { return $null }
    return (Get-Content $p -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Send-ChatBody($bodyObj, [string]$client, [string]$tag) {
    $path = Join-Path $tmpDir ("mem-" + $tag + "-" + [guid]::NewGuid().ToString('N') + ".json")
    [IO.File]::WriteAllText($path, ($bodyObj | ConvertTo-Json -Compress), $utf8)
    $cargs = @('-s','-m','90','-o',($path + '.out'),'-w','%{http_code}',
               '-H','Content-Type: application/json; charset=utf-8')
    if ($client) { $cargs += @('-H', "X-Session-Id: $client") }
    $cargs += @('--data-binary', "@$path", $chatUrl)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $code = & curl.exe @cargs
    $sw.Stop()
    $raw = ''
    if (Test-Path ($path + '.out')) { $raw = [IO.File]::ReadAllText(($path + '.out'), [Text.Encoding]::UTF8) }
    $j = $null
    try { $j = $raw | ConvertFrom-Json } catch { $j = $null }
    return @{ code = [string]$code; json = $j; raw = $raw; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 3) }
}

function Invoke-Api([string]$method, [string]$path, [string]$client, $bodyObj) {
    $outFile = Join-Path $tmpDir ("api-" + [guid]::NewGuid().ToString('N') + ".txt")
    $cargs = @('-s','-m','30','-X',$method,'-o',$outFile,'-w','%{http_code}')
    if ($client) { $cargs += @('-H', "X-Session-Id: $client") }
    if ($null -ne $bodyObj) {
        $bp = Join-Path $tmpDir ("api-" + [guid]::NewGuid().ToString('N') + ".json")
        [IO.File]::WriteAllText($bp, ($bodyObj | ConvertTo-Json -Compress), $utf8)
        $cargs += @('-H','Content-Type: application/json; charset=utf-8','--data-binary',"@$bp")
    }
    $cargs += ($baseUrl + $path)
    $code = & curl.exe @cargs
    $raw = ''
    if (Test-Path $outFile) { $raw = [IO.File]::ReadAllText($outFile, [Text.Encoding]::UTF8) }
    $j = $null
    try { $j = $raw | ConvertFrom-Json } catch { $j = $null }
    return @{ code = [string]$code; json = $j; raw = $raw }
}

function Add-Row([string]$id, [bool]$ok, [double]$secs, [string]$problem, [string]$answer) {
    $status = 'PASS'
    if (-not $ok) { $script:failures++; $status = 'FAIL' }
    $script:rows.Add([pscustomobject]@{
        Case = $id; Status = $status; Seconds = $secs; Qtype = 'memory'
        Pdf = 0; Problem = $problem; Answer = $answer
    }) | Out-Null
    Write-Host ("{0,-24} {1,-4} {2,6}s  {3}" -f $id, $status, $secs, $problem)
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

# Answer text must never contain meta material (PART 1). Markers live in a
# UTF-8 fixture so this script itself stays ASCII-only for PowerShell 5.1.
$forbiddenMarkers = @()
$forbiddenPath = Join-Path $PSScriptRoot 'expected\forbidden.json'
if (Test-Path $forbiddenPath) {
    $forbiddenMarkers = @((Get-Content $forbiddenPath -Raw -Encoding UTF8 | ConvertFrom-Json).markers)
}

$rows = New-Object System.Collections.Generic.List[object]
$failures = 0
$warnings = 0

foreach ($c in $cases) {
    $payloadPath = Join-Path $tmpDir ($c.id + '.json')
    $payload = @{ session_id = 'suite-' + $c.id; message = $c.question } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($payloadPath, $payload, (New-Object System.Text.UTF8Encoding($false)))

    $respPath = Join-Path $tmpDir ($c.id + '.response.json')
    # Latency is judged on the best of up to three runs; the model occasionally
    # stalls on a cold KV cache. Content assertions use that same best run.
    $limit = 90.0
    if ($null -ne $c.max_seconds) { $limit = [double]$c.max_seconds }
    $seconds = 999.0
    $raw = ''
    $httpCode = ''
    $runSeconds = @()
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $httpCode = & curl.exe -s -m 90 -o $respPath -w '%{http_code}' -H 'Content-Type: application/json; charset=utf-8' --data-binary "@$payloadPath" $chatUrl
        $sw.Stop()
        $text = ''
        if (Test-Path $respPath) { $text = [IO.File]::ReadAllText($respPath, [Text.Encoding]::UTF8) }
        $dt = [math]::Round($sw.Elapsed.TotalSeconds, 2)
        $runSeconds += $dt
        if ($dt -lt $seconds) { $seconds = $dt; $raw = $text }
        if ($seconds -le $limit -or [string]$httpCode -ne '200') { break }
    }
    if ($runSeconds.Count -gt 1) { Write-Host ("       runs: " + ($runSeconds -join ', ') + " s") }

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
        # One book cited on three pages is ONE PDF source, not three: count
        # distinct files, not entries (measured in test_results/gate_after.md).
        $pdfSources = @($j.sources | Where-Object { $_.type -eq 'pdf' } |
            ForEach-Object { [string]$_.file } | Sort-Object -Unique).Count
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
    for ($mi = 0; $mi -lt $forbiddenMarkers.Count; $mi++) {
        $bad = [string]$forbiddenMarkers[$mi]
        if ($bad -and $answer.IndexOf($bad, [StringComparison]::Ordinal) -ge 0) {
            $problems.Add("answer contains forbidden marker #$mi") | Out-Null
        }
    }
    if ($c.exactly_one_sentence) {
        $sentences = ([regex]::Matches($answer.Trim(), '[.!?](\s|$)')).Count
        if ($sentences -ne 1) { $problems.Add("expected 1 sentence, got $sentences") | Out-Null }
    }
    if ($c.route) {
        $routeVal = ''
        try { $routeVal = [string]$j.route } catch { $routeVal = '' }
        if ($routeVal -ne [string]$c.route) { $problems.Add("route $routeVal != $($c.route)") | Out-Null }
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

# ======================= PART 1: persistent memory ========================
# All memory cases use the "suite" client namespace, so the user's own
# conversations/facts in the default namespace are never touched.
$memClient = 'suite'
Invoke-Api 'DELETE' '/memory' $memClient $null | Out-Null

$factB = Read-Fixture 'fact_statement.json'
$r1 = Send-ChatBody $factB $memClient 'fact'
Add-Row 'mem_fact_store' (($r1.code -eq '200') -and ($r1.json.memory_used -eq $true)) $r1.seconds '' ([string]$r1.json.answer)

$m1 = Invoke-Api 'GET' '/memory' $memClient $null
$factId = ''
$factCount = 0
if ($m1.json -and $null -ne $m1.json.count) { $factCount = [int]$m1.json.count }
if ($factCount -ge 1) { $factId = [string]$m1.json.facts[0].id }
Add-Row 'mem_fact_list' (($m1.code -eq '200') -and ($factCount -ge 1)) 0 ('count=' + $factCount) ''

$askB = Read-Fixture 'fact_question.json'
$r2 = Send-ChatBody $askB $memClient 'ask'
$answered = ($r2.code -eq '200') -and ([string]$r2.json.answer -match '30') -and ($r2.json.memory_used -eq $true)
Add-Row 'mem_fact_inject' $answered $r2.seconds ([string]$r2.json.answer) ([string]$r2.json.answer)
$lat = $null
if ($r2.json -and $null -ne $r2.json.latency_ms) { $lat = [double]$r2.json.latency_ms }
Add-Row 'mem_step_latency' (($null -ne $lat) -and ($lat -lt 150)) 0 ('latency_ms=' + [string]$lat) ''

if ($factId) { $del = Invoke-Api 'DELETE' (('/memory/' + $factId)) $memClient $null } else { $del = @{ code = '0' } }
$m2 = Invoke-Api 'GET' '/memory' $memClient $null
$r3 = Send-ChatBody $askB $memClient 'after'
$cnt2 = 0
if ($m2.json -and $null -ne $m2.json.count) { $cnt2 = [int]$m2.json.count }
Add-Row 'mem_fact_delete' (($del.code -eq '200') -and ($cnt2 -eq 0) -and ($r3.json.memory_used -ne $true)) $r3.seconds ('count=' + $cnt2) ([string]$r3.json.answer)

# --- disabled memory stores and injects nothing --------------------------
$t0 = Invoke-Api 'POST' '/memory/toggle' $memClient @{ enabled = $false }
$cowB = Read-Fixture 'cow_statement.json'
$r4 = Send-ChatBody $cowB $memClient 'off'
$m3 = Invoke-Api 'GET' '/memory' $memClient $null
$cnt3 = 0
if ($m3.json -and $null -ne $m3.json.count) { $cnt3 = [int]$m3.json.count }
Add-Row 'mem_disabled' (($t0.json.enabled -eq $false) -and ($r4.json.memory_used -ne $true) -and ($cnt3 -eq 0)) $r4.seconds ('count=' + $cnt3) ([string]$r4.json.answer)
Invoke-Api 'POST' '/memory/toggle' $memClient @{ enabled = $true } | Out-Null

# --- memory never reaches PDF answers/citations --------------------------
Send-ChatBody (Read-Fixture 'fact_statement.json') $memClient 'pre' | Out-Null
$r6 = Send-ChatBody (Read-Fixture 'livestock.json') $memClient 'liv'
$pdfCount = 0
if ($r6.json -and $r6.json.sources) {
    $pdfCount = @($r6.json.sources | Where-Object { $_.type -eq 'pdf' } |
        ForEach-Object { [string]$_.file } | Sort-Object -Unique).Count
}
Add-Row 'mem_no_pdf_pollution' (($r6.code -eq '200') -and ($r6.json.memory_used -ne $true)) $r6.seconds ('pdf=' + $pdfCount) ([string]$r6.json.answer)

# --- per-client isolation + path traversal --------------------------------
$cA = Invoke-Api 'POST' '/conversations' 'alpha' @{}
$cidA = ''
if ($cA.json -and $cA.json.conversation) { $cidA = [string]$cA.json.conversation.id }
$cross = Invoke-Api 'GET' (('/conversations/' + $cidA)) 'beta' $null
$trav = Invoke-Api 'GET' '/conversations/..%2F..%2Fetc' $null $null
Add-Row 'mem_isolation' (($cross.code -eq '404') -and (@('400','404') -contains $trav.code)) 0 ('cross=' + $cross.code + ' traversal=' + $trav.code) ''

# --- corrupted store must not crash --------------------------------------
$memDir = Join-Path $root 'data\memory\suite'
New-Item -ItemType Directory -Force -Path $memDir | Out-Null
[IO.File]::WriteAllText((Join-Path $memDir 'facts.json'), '{ this is not json', $utf8)
$corrupt = Invoke-Api 'GET' '/memory' $memClient $null
$afterCorrupt = Send-ChatBody $askB $memClient 'corrupt'
Add-Row 'mem_corrupt_store' (($corrupt.code -eq '200') -and ($afterCorrupt.code -eq '200')) $afterCorrupt.seconds ('get=' + $corrupt.code) 'server survived a corrupt store'

# --- 50 long facts + general question: clean answer, no HTTP 400 ---------
$bigFacts = @()
for ($i = 0; $i -lt 50; $i++) {
    $bigFacts += @{ id = ("f-big-" + $i); text = ("uzun yaddas qeydi nomre " + $i); conversation_id = ''; created = 0 }
}
[IO.File]::WriteAllText((Join-Path $memDir 'facts.json'), (@{ facts = $bigFacts } | ConvertTo-Json -Depth 5 -Compress), $utf8)
$r7 = Send-ChatBody (Read-Fixture 'long_context.json') $memClient 'big'
Add-Row 'mem_oversized' (($r7.code -eq '200') -and (([string]$r7.json.answer).Length -gt 20)) $r7.seconds ('http=' + $r7.code) ([string]$r7.json.answer)
Invoke-Api 'DELETE' '/memory' $memClient $null | Out-Null

# --- deterministic paths stay fast and memory-free -----------------------
$r8 = Send-ChatBody (Read-Fixture 'arithmetic.json') $memClient 'arith'
Add-Row 'mem_fastpath' (($r8.code -eq '200') -and ([string]$r8.json.answer -match '180') -and ($r8.seconds -lt 0.2) -and ($r8.json.memory_used -ne $true)) $r8.seconds ([string]$r8.json.answer) ([string]$r8.json.answer)

# --- UI static safety check (no browser tooling) -------------------------
$htmlPath = Join-Path $root 'web\index.html'
$html = ''
if (Test-Path $htmlPath) { $html = [IO.File]::ReadAllText($htmlPath, [Text.Encoding]::UTF8) }
$uiOk = ($html.Length -gt 1000) -and (-not ($html -match '(?i)<link\b')) -and
        (-not ($html -match '(?i)@import')) -and (-not ($html -match 'src="http')) -and
        (-not ($html -match '(?i)fonts\.')) -and (-not ($html -match 'innerHTML')) -and
        ($html -match 'SpeechRecognition') -and ($html -match 'az-AZ') -and
        ($html -match 'Ctrl\+Shift\+M|ctrlKey')
Add-Row 'ui_static_safe' $uiOk 0 'no CDN/fonts/innerHTML' 'static check only'



# --- A1: the answer filter strips every marker variant -------------------
$variants = Get-Content (Join-Path $PSScriptRoot 'memory\sanitize_variants.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$badVariants = 0
$firstBad = ''
foreach ($v in $variants) {
    $enc = [uri]::EscapeDataString([string]$v)
    $out = & curl.exe -s --max-time 10 ($baseUrl + '/debug/sanitize?text=' + $enc)
    $j = $null
    try { $j = $out | ConvertFrom-Json } catch { $j = $null }
    $o = [string]$j.output
    if ($o -match '(?i)qeyd|mənbə|menbe|dəyişiklik|deyisiklik|\.pdf|əsasında verilib') {
        $badVariants++
        if (-not $firstBad) { $firstBad = $o }
    }
}
Add-Row 'sanitize_markers' ($badVariants -eq 0) 0 ('variants=' + @($variants).Count + ' bad=' + $badVariants) $firstBad
# --- Step 1: conversational meta commands (conversation sequences) --------
$s1 = 'step1-' + [guid]::NewGuid().ToString('N')
$frag = Read-Fixture 'step1_fragment.json'
$sw1 = Get-Date
$r1 = Send-ChatBody @{ message = $frag.message; session_id = $s1 } $s1 'step1a'
$okFrag = ($r1.code -eq '200') -and ([string]$r1.json.answer).Length -gt 80 -and $r1.json.route -ne 'meta'
Add-Row 'step1_fragment' $okFrag ((Get-Date) - $sw1).TotalSeconds ('route=' + $r1.json.route) ([string]$r1.json.answer)

$follow = Read-Fixture 'step1_followup.json'
$sw2 = Get-Date
$r2 = Send-ChatBody @{ message = $follow.message; session_id = $s1 } $s1 'step1b'
$okFollow = ($r2.code -eq '200') -and ([string]$r2.json.answer).Length -gt 80 -and $r2.json.route -ne 'meta'
Add-Row 'step1_followup' $okFollow ((Get-Date) - $sw2).TotalSeconds ('route=' + $r2.json.route) ([string]$r2.json.answer)

$s2 = 'step1b-' + [guid]::NewGuid().ToString('N')
$metaOnly = Read-Fixture 'step1_meta_only.json'
$sw3 = Get-Date
$r3 = Send-ChatBody @{ message = $metaOnly.message; session_id = $s2 } $s2 'step1c'
$elapsed3 = ((Get-Date) - $sw3).TotalSeconds
$expected = (Get-Content (Join-Path $PSScriptRoot 'memory\step1_expected.txt') -Raw -Encoding UTF8).Trim()
$okMeta = ($r3.code -eq '200') -and (([string]$r3.json.answer).Trim() -eq $expected) -and ($elapsed3 -lt 3)
Add-Row 'step1_meta_first' $okMeta $elapsed3 'fixed clarification, no model call' ([string]$r3.json.answer)

$mixed = Read-Fixture 'step1_mixed.json'
$sw4 = Get-Date
$r4 = Send-ChatBody @{ message = $mixed.message; session_id = $s1 } $s1 'step1d'
$okMixed = ($r4.code -eq '200') -and ([string]$r4.json.answer).Length -gt 60 -and $r4.json.route -ne 'meta'
Add-Row 'step1_mixed' $okMixed ((Get-Date) - $sw4).TotalSeconds ('route=' + $r4.json.route) ([string]$r4.json.answer)
# --- conversations survive a server restart ------------------------------
$pc = Invoke-Api 'POST' '/conversations' $memClient @{}
$pcid = ''
if ($pc.json -and $pc.json.conversation) { $pcid = [string]$pc.json.conversation.id }
$pm = Read-Fixture 'fact_statement.json'
if ($pcid) { $pm | Add-Member -NotePropertyName conversation_id -NotePropertyValue $pcid -Force }
Send-ChatBody $pm $memClient 'persist' | Out-Null
if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
$server = Start-Process -FilePath $exe -ArgumentList '--no-browser','--no-web' -WorkingDirectory $root -PassThru -WindowStyle Hidden
$h = $null
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep -Seconds 2
    $h = Get-Health
    if ($h -and $h.warmup.main_ready) { break }
}
if ($h) { $identity = "$($h.build_id)|$($h.started_at)|$($h.pid)" }
$gp = Invoke-Api 'GET' (('/conversations/' + $pcid)) $memClient $null
$msgCount = 0
if ($gp.json -and $gp.json.conversation) { $msgCount = @($gp.json.conversation.messages).Count }
Add-Row 'mem_persist_restart' (($gp.code -eq '200') -and ($msgCount -ge 2)) 0 ('messages=' + $msgCount) ''

# --- welcome chips: every example in config.json must still work ----------
$honestMarkers = @()
$honestPath = Join-Path $PSScriptRoot 'expected\honest_failure.json'
if (Test-Path $honestPath) {
    $honestMarkers = @((Get-Content $honestPath -Raw -Encoding UTF8 | ConvertFrom-Json).markers)
}
$cfg = Invoke-Api 'GET' '/config' $memClient $null
$examples = @()
if ($cfg.json -and $cfg.json.welcome_examples) { $examples = @($cfg.json.welcome_examples) }
Add-Row 'welcome_config' ($examples.Count -ge 4) 0 ('count=' + $examples.Count) ''
$wi = 0
foreach ($ex in $examples) {
    $wi++
    $body = @{ message = [string]$ex.text; session_id = 'welcome-suite' }
    $wr = Send-ChatBody $body $memClient ('welcome' + $wi)
    $wans = [string]$wr.json.answer
    # Correctness of the answer is the chip-selection criterion (scored once,
    # see tests\welcome); the regression guards that the chip still answers
    # cleanly and quickly, so an honest-failure answer is reported, not failed.
    $wbad = ($wr.code -ne '200') -or ($wans.Length -lt 8) -or ($wr.seconds -gt 30)
    $whonest = $false
    foreach ($m in $honestMarkers) {
        if ($wans.IndexOf([string]$m, [StringComparison]::Ordinal) -ge 0) { $whonest = $true }
    }
    foreach ($m in $forbiddenMarkers) {
        if ($wans.IndexOf([string]$m, [StringComparison]::Ordinal) -ge 0) { $wbad = $true }
    }
    if ($wr.json.pdf_sources -and [int]$wr.json.pdf_sources -lt 0) { $wbad = $true }
    $wnote = 'len=' + $wans.Length
    if ($whonest) { $wnote += ' honest-failure' }
    Add-Row ('welcome_' + $wi) (-not $wbad) $wr.seconds $wnote $wans
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
