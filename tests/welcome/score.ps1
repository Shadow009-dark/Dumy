# Scores the welcome-example candidates against the real pipeline.
# Run with a DUMY server already listening on -Port (default 8085).
param([string]$Port = 8085)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$utf8 = New-Object System.Text.UTF8Encoding($false)
$cand = Get-Content (Join-Path $PSScriptRoot 'candidates.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$out = @()
foreach ($c in $cand.candidates) {
    $payload = @{ message = $c.text; session_id = ('welcome-' + $c.id) } | ConvertTo-Json -Compress
    $p = Join-Path $env:TEMP ('welcome-' + $c.id + '.json')
    [IO.File]::WriteAllText($p, $payload, $utf8)
    $url = 'http://127.0.0.1:' + $Port + '/chat'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $raw = & curl.exe -s -m 90 -H 'Content-Type: application/json; charset=utf-8' --data-binary "@$p" $url
    $sw.Stop()
    $j = $null
    try { $j = $raw | ConvertFrom-Json } catch { $j = $null }
    if (-not $j) { Write-Host ($c.id + ' no-json'); continue }
    $pdf = @($j.sources | Where-Object { $_.type -eq 'pdf' })
    $out += [pscustomobject]@{
        id = $c.id
        area = $c.area
        text = $c.text
        route = [string]$j.route
        seconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)
        answer = [string]$j.answer
        pdf_sources = $pdf.Count
        files = (@($pdf | ForEach-Object { $_.file + ' p.' + $_.page }) -join '; ')
        snippets = (@($pdf | ForEach-Object { $_.snippet }) -join "`n---`n")
    }
    Write-Host ($c.id + ' ' + $j.route + ' ' + [math]::Round($sw.Elapsed.TotalSeconds, 2) + 's pdf=' + $pdf.Count)
}
[IO.File]::WriteAllText((Join-Path $root 'data\welcome_results.json'), ($out | ConvertTo-Json -Depth 6), $utf8)
Write-Host ('WROTE data\welcome_results.json (' + $out.Count + ' rows)')
