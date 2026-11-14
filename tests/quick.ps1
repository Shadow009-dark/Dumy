# quick.ps1 - deterministic, no-model checks for use between edits (~60 s).
# The full suite remains the commit gate.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\Release\dumy.exe'
$base = 'http://127.0.0.1:8085'
$env:DUMY_DEBUG_TRACE = '1'

$busy = Get-NetTCPConnection -LocalPort 8085 -State Listen -ErrorAction SilentlyContinue
if ($busy) { Write-Host 'quick: port 8085 busy - stop the running server first'; exit 3 }

$srv = Start-Process -FilePath $exe -ArgumentList '--no-browser', '--no-web' `
    -WorkingDirectory $root -PassThru -WindowStyle Hidden
$fails = 0
try {
    $ready = $false
    for ($i = 0; $i -lt 60; $i++) {
        try {
            $h = Invoke-RestMethod "$base/health" -TimeoutSec 2
            if ($h.warmup.main_ready) { $ready = $true; break }
        } catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { Write-Host 'quick: server not ready'; exit 1 }

    $variants = Get-Content (Join-Path $PSScriptRoot 'memory\sanitize_variants.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $bad = 0
    foreach ($v in $variants) {
        $u = "$base/debug/sanitize?text=" + [uri]::EscapeDataString([string]$v)
        $o = Invoke-RestMethod $u -TimeoutSec 10
        if ([string]$o.output -match '(?i)qeyd|mənbə|menbe|dəyişiklik|deyisiklik|\.pdf|əsasında verilib') { $bad++ }
    }
    if ($bad -eq 0) { Write-Host 'PASS sanitize_markers 15/15' }
    else { Write-Host "FAIL sanitize_markers bad=$bad"; $fails++ }

    $body = @{ message = 'sualıma cavab ver'; session_id = ('q-' + [guid]::NewGuid().ToString('N')) } |
        ConvertTo-Json -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($body)
    $r = Invoke-RestMethod "$base/chat" -Method Post -Body $bytes `
        -ContentType 'application/json; charset=utf-8' -TimeoutSec 40
    $expected = (Get-Content (Join-Path $PSScriptRoot 'memory\step1_expected.txt') -Raw -Encoding UTF8).Trim()
    if (([string]$r.answer).Trim() -eq $expected) { Write-Host 'PASS step1_meta_first' }
    else { Write-Host ('FAIL step1_meta_first: ' + [string]$r.answer); $fails++ }

    if ($fails -eq 0) { Write-Host 'QUICK PASS' } else { Write-Host 'QUICK FAIL' }
}
finally {
    if ($srv -and -not $srv.HasExited) { Stop-Process -Id $srv.Id -Force }
}
exit $fails
