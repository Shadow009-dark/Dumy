# debug_report.ps1 - one trace section per question in tests\debug_questions.txt.
# Starts its own server with DUMY_DEBUG_TRACE=1, asks every question with
# "debug": true and writes test_results\debug_report.md.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\Release\dumy.exe'
$base = 'http://127.0.0.1:8085'
$outDir = Join-Path $root 'test_results'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outPath = Join-Path $outDir 'debug_report.md'
$env:DUMY_DEBUG_TRACE = '1'

$busy = Get-NetTCPConnection -LocalPort 8085 -State Listen -ErrorAction SilentlyContinue
$srv = $null
if (-not $busy) {
    $srv = Start-Process -FilePath $exe -ArgumentList '--no-browser', '--no-web' `
        -WorkingDirectory $root -PassThru -WindowStyle Hidden
}
$lines = @('# DUMY debug trace report', '', "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')", '')
try {
    $ready = $false
    for ($i = 0; $i -lt 60; $i++) {
        try {
            $h = Invoke-RestMethod "$base/health" -TimeoutSec 2
            if ($h.warmup.main_ready) { $ready = $true; break }
        } catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'server not ready' }

    $n = 0
    foreach ($q in (Get-Content (Join-Path $PSScriptRoot 'debug_questions.txt') -Encoding UTF8)) {
        if ([string]::IsNullOrWhiteSpace($q)) { continue }
        $n++
        $body = @{ message = $q.Trim(); session_id = "dbg-$n"; debug = $true } | ConvertTo-Json -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($body)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            $r = Invoke-RestMethod "$base/chat" -Method Post -Body $bytes `
                -ContentType 'application/json; charset=utf-8' -TimeoutSec 180
        } catch {
            $lines += @('', "## $n. $q", '', "ERROR: $($_.Exception.Message)")
            continue
        }
        $sw.Stop()
        $lines += @('', "## $n. $q", '')
        $lines += "- wall: $([math]::Round($sw.Elapsed.TotalSeconds, 2)) s"
        $lines += "- route: $($r.route) / $($r.qtype)"
        if ($r.trace) {
            $t = $r.trace
            $lines += "- route_why: $($t.route_why)"
            $lines += "- query: $($t.query)"
            $lines += "- rewritten: $($t.rewritten)"
            $lines += "- expansion skipped: $($t.expansion.skipped); queries: $(@($t.expansion.queries) -join ' | ')"
            $lines += "- retrieval: top_k=$($t.retrieval.top_k) max_score=$($t.retrieval.max_score) min_score=$($t.retrieval.min_score) dropped_by_min_score=$($t.retrieval.dropped_by_min_score) chunks=$(@($t.retrieval.chunks).Count)"
            $lines += "- selected chunks: $(@($t.selected).Count)"
            foreach ($c in @($t.selected)) { $lines += "  - $($c.source) p.$($c.page)" }
            $lines += "- timings ms: $($t.timings_ms | ConvertTo-Json -Compress)"
            $lines += "- sampling: $($t.sampling | ConvertTo-Json -Compress)"
            $prompt = [string]$t.prompt
            if ($prompt.Length -gt 1200) { $prompt = $prompt.Substring(0, 1200) + ' ...' }
            $lines += "- prompt (truncated):"
            $lines += '```'
            $lines += $prompt
            $lines += '```'
        } else {
            $lines += '- TRACE MISSING'
        }
        $lines += "- answer: $($r.answer)"
        $srcs = @($r.sources | ForEach-Object { "$($_.file) p.$($_.page)" })
        $lines += "- sources: $($srcs -join '; ')"
    }
}
finally {
    if ($srv -and -not $srv.HasExited) { Stop-Process -Id $srv.Id -Force }
}
Set-Content -Path $outPath -Value ($lines -join "`n") -Encoding UTF8
Write-Host "wrote $outPath ($n questions)"
