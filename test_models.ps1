<#
 DUMY model test: modelleri birbasa Ollama-ya qarsi yoxlayir (RAG olmadan).
 Meqsed: hansi modelin hansi sahede guclu/zeif oldugunu, sureti ve GPU/CPU bolgusunu gormek.

 Istifade:
   powershell -ExecutionPolicy Bypass -File .\test_models.ps1
   powershell -ExecutionPolicy Bypass -File .\test_models.ps1 -Models "qwen2.5:7b","gemma3:4b" -NumCtx 4096
#>
param(
    [string[]]$Models = @('qwen3:4b-instruct-2507-q4_K_M', 'qwen3:8b'),
    [int]$NumCtx = 2048,
    [int]$MaxTokens = 400,
    [string]$OllamaUrl = 'http://localhost:11434',
    [string]$OutDir = '.\test_results'
)

$ErrorActionPreference = 'Stop'

# powershell -File ... -Models "a","b" modelleri tek setir kimi ötürür ("a,b"); burada ayırırıq
$Models = @($Models | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

Add-Type -AssemblyName System.Net.Http
$utf8 = New-Object System.Text.UTF8Encoding($false)
$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromSeconds(900)

function Invoke-Ollama {
    param([string]$Path, [hashtable]$Body)
    $json = $Body | ConvertTo-Json -Depth 10 -Compress
    $content = New-Object System.Net.Http.StringContent($json, $utf8, 'application/json')
    $resp = $client.PostAsync("$OllamaUrl$Path", $content).Result
    $bytes = $resp.Content.ReadAsByteArrayAsync().Result
    $text = $utf8.GetString($bytes)
    if (-not $resp.IsSuccessStatusCode) { throw ('HTTP ' + [int]$resp.StatusCode + ': ' + $text) }
    return ($text | ConvertFrom-Json)
}

function Ask-Model {
    param([string]$Model, [string]$System, [string]$Prompt)
    $opts = @{ num_ctx = $NumCtx; temperature = 0; num_predict = $MaxTokens }
    $sys = $System
    if ($Model -match '^qwen3') {
        # Qwen3 ucun tovsiyye olunan non-thinking parametrler (greedy temperature=0 donguye dusur)
        $opts.temperature = 0.7; $opts.top_p = 0.8; $opts.top_k = 20
        if ($Model -notmatch 'instruct') { $sys = $System + ' /no_think' }
    }
    $body = @{
        model      = $Model
        messages   = @(
            @{ role = 'system'; content = $sys },
            @{ role = 'user'; content = $Prompt }
        )
        stream     = $false
        think      = $false
        keep_alive = '10m'
        options    = $opts
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $r = Invoke-Ollama '/api/chat' $body
    }
    catch {
        if ($_.Exception.Message -match 'think') {
            $body.Remove('think')
            $r = Invoke-Ollama '/api/chat' $body
        }
        else { throw }
    }
    $sw.Stop()

    $raw = [string]$r.message.content
    # Qwen3 bezen acilis <think> teqi olmadan yalniz </think> qaytarir: sonuncu </think>-e qeder her seyi atiriq
    if ($raw -match '</think>') {
        $clean = [regex]::Replace($raw, '(?s)^.*</think>', '').Trim()
    }
    else {
        $clean = [regex]::Replace($raw, '(?s)<think>.*?</think>', '').Trim()
    }
    $tps = 0
    if ($r.eval_duration -gt 0) { $tps = [math]::Round($r.eval_count / ($r.eval_duration / 1e9), 1) }
    return [pscustomobject]@{
        Answer   = $clean
        HasThink = ($raw -match '</think>|<think>')
        Cut      = ($r.done_reason -eq 'length')
        WallSec  = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        LoadSec  = [math]::Round($r.load_duration / 1e9, 1)
        TokPerS  = $tps
        Tokens   = [int]$r.eval_count
        ThinkChars = ([string]$r.message.thinking).Length
    }
}

# ---- Tarix / gun (modele real tarix verilir) ----
$inv = [Globalization.CultureInfo]::InvariantCulture
$today = Get-Date
$dateInfo = 'Today is ' + $today.DayOfWeek + ', ' + $today.ToString('yyyy-MM-dd', $inv) + '.'
$azDays = @('Bazar', 'Bazar ertəsi', 'Çərşənbə axşamı', 'Çərşənbə', 'Cümə axşamı', 'Cümə', 'Şənbə')
$day = $azDays[[int]$today.DayOfWeek]
switch ($day) {
    'Bazar'    { $weekdayPattern = 'Bazar(?! ertəsi)' }
    'Çərşənbə' { $weekdayPattern = 'Çərşənbə(?! axşamı)' }
    'Cümə'     { $weekdayPattern = 'Cümə(?! axşamı)' }
    default    { $weekdayPattern = [regex]::Escape($day) }
}

$baseSystem = 'Sənin adın DUMY-dir. Səni Sahdow yaradıb. Həmişə Azərbaycan dilində, qısa və dəqiq cavab ver. Bilmədiyin məlumatı uydurma, Bilmirəm de. ' + $dateInfo

# ---- Testler (Check = avtomatik yoxlama; $null = elle oxumaq lazimdir) ----
$tests = @(
    [pscustomobject]@{ Name = 'Identity';     Prompt = 'Adın nədir və səni kim yaradıb?';
        Check = { $_ -match 'DUMY' -and $_ -match 'Sahdow' } },
    [pscustomobject]@{ Name = 'Weekday';      Prompt = 'Bu gün həftənin hansı günüdür?';
        Check = { $_ -match $weekdayPattern } },
    [pscustomobject]@{ Name = 'Math-simple';  Prompt = '15 vurulsun 12 neçə edir?';
        Check = { $_ -match '180|yüz səksən' } },
    [pscustomobject]@{ Name = 'Capital';      Prompt = 'Azərbaycanın paytaxtı haradır?';
        Check = { $_ -match 'Bakı' } },
    [pscustomobject]@{ Name = 'One-word';     Prompt = 'Yalnız bir sözlə cavab ver: Azərbaycanın ən böyük şəhəri hansıdır?';
        Check = { $_.Trim().TrimEnd('.') -match '^Bakı$' } },
    [pscustomobject]@{ Name = 'AZ-letters';   Prompt = 'Tərkibində ə, ı, ö, ü, ç, ş, ğ hərflərinin hamısı olan məntiqli qısa bir cümlə yaz.';
        Check = $null },
    [pscustomobject]@{ Name = 'Sekerchorek';  Prompt = 'Şəkərçörək necə hazırlanır? Qısa cavab ver.';
        Check = { $_ -notmatch 'çuğundur' -and $_ -match '\bun\b|yumurta' } },
    [pscustomobject]@{ Name = 'Spelling-fix'; Prompt = 'Bu cümləni düzgün Azərbaycan dilinə uyğun düzəlt: men sabah mekteve gedecem';
        Check = { $_ -match 'Mən sabah məktəbə gedəcəyəm' } },
    [pscustomobject]@{ Name = 'Hallucin-Mars'; Prompt = 'Mars-da neçə nəfər yaşayır?';
        Check = { $_ -match 'heç kim|yaşamır|sıfır|insan yoxdur|\b0\b' } },
    [pscustomobject]@{ Name = 'Hallucin-person'; Prompt = 'Sahdow adlı proqramçının doğum tarixi nədir?';
        Check = { $_ -match 'bilmirəm|məlumat yoxdur|məlumatım yoxdur|tapmadım' } },
    [pscustomobject]@{ Name = 'Feed-calc';    Prompt = 'Bir fermada 120 baş mal-qara var, hər birinə gündə 25 kq yem lazımdır. 30 günlük yem ehtiyacını hesabla.';
        Check = { $_ -match '90[\s\.,]?000|doxsan min' } },
    [pscustomobject]@{ Name = 'Logic-age';    Prompt = 'Ali Vəlidən 3 yaş böyükdür. Vəli Aydandan 2 yaş böyükdür. Aydan 10 yaşındadır. Ali neçə yaşındadır?';
        Check = { $_ -match '\b15\b' } },
    [pscustomobject]@{ Name = 'Code-cpp';     Prompt = 'C++17-də vektoru azalan sıra ilə düzən qısa kod yaz.';
        Check = { $_ -match 'sort' -and $_ -match 'greater|rbegin|a\s*>\s*b|b\s*<\s*a' } },
    [pscustomobject]@{ Name = 'Code-sql';     Prompt = 'İki cədvəli (Musteri, Sifaris) JOIN ilə birləşdirən SQL sorğusu yaz.';
        Check = { $_ -match 'JOIN' } },
    [pscustomobject]@{ Name = 'Explain-RAG';  Prompt = 'RAG sistemi ilə adi chatbot arasındakı fərqi 3 cümlə ilə izah et.';
        Check = $null }
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$rows = New-Object System.Collections.ArrayList
$summary = New-Object System.Collections.ArrayList
$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine('# DUMY model test - ' + $stamp + ' (num_ctx=' + $NumCtx + ')')
[void]$md.AppendLine('System prompt: ' + $baseSystem)

function Save-Results {
    $rows | Export-Csv -Path (Join-Path $OutDir ('results_' + $stamp + '.csv')) -NoTypeInformation -Encoding UTF8
    $md.ToString() | Out-File -FilePath (Join-Path $OutDir ('results_' + $stamp + '.md')) -Encoding UTF8
}

function Unload-Model([string]$m) {
    try { [void](Invoke-Ollama '/api/generate' @{ model = $m; keep_alive = 0 }) } catch { }
}

$prev = $null
foreach ($m in $Models) {
    if ($prev) { Unload-Model $prev }
    Write-Host ''
    Write-Host ('=== ' + $m + ' ===') -ForegroundColor Cyan

    # Isinma sorgusu (modeli yuklemek ucun) - netice sayilmir
    try {
        $warm = Ask-Model $m $baseSystem 'salam'
    }
    catch {
        Write-Host ('  ATLANDI: ' + $_.Exception.GetBaseException().Message) -ForegroundColor Red
        Write-Host ('  (model yoxdursa: ollama pull ' + $m + ')') -ForegroundColor DarkGray
        continue
    }
    $prev = $m

    $procLine = ''
    try {
        $procLine = (((& ollama ps) | Where-Object { $_ -match [regex]::Escape($m) }) -join ' ') -replace '\s+', ' '
    }
    catch { }
    Write-Host ('  ollama ps: ' + $procLine) -ForegroundColor DarkGray
    Write-Host ('  yuklenme vaxti: ' + $warm.LoadSec + ' san') -ForegroundColor DarkGray

    [void]$md.AppendLine('')
    [void]$md.AppendLine('## ' + $m)
    [void]$md.AppendLine('ollama ps: ' + $procLine)

    $pass = 0; $auto = 0; $wallSum = 0; $tpsSum = 0; $tokSum = 0; $n = 0
    foreach ($t in $tests) {
        try {
            $r = Ask-Model $m $baseSystem $t.Prompt
        }
        catch {
            Write-Host ('  ' + $t.Name + ': XETA ' + $_.Exception.GetBaseException().Message) -ForegroundColor Red
            continue
        }
        $ans = [string]$r.Answer
        $status = 'MANUAL'
        if ($t.Check) {
            $auto++
            $ok = $false
            try { $ok = [bool]($ans | ForEach-Object $t.Check) } catch { $ok = $false }
            if ($r.Cut) { $ok = $false }
            if ($ok) { $status = 'PASS'; $pass++ } else { $status = 'FAIL' }
        }
        if ($r.HasThink) { $status = $status + '+THINK' }
        if ($r.Cut) { $status = $status + '+CUT' }
        $n++; $wallSum += $r.WallSec; $tpsSum += $r.TokPerS; $tokSum += $r.Tokens

        $color = 'Yellow'
        if ($status -like 'PASS*') { $color = 'Green' } elseif ($status -like 'FAIL*') { $color = 'Red' }
        Write-Host ('  {0,-16} {1,-12} {2,6} san  {3,6} tok/s  {4,5} token  think:{5}' -f $t.Name, $status, $r.WallSec, $r.TokPerS, $r.Tokens, $r.ThinkChars) -ForegroundColor $color

        $oneLine = ($ans -replace '\s+', ' ')
        if ($oneLine.Length -gt 200) { $oneLine = $oneLine.Substring(0, 200) }
        [void]$rows.Add([pscustomobject]@{
            Model = $m; Test = $t.Name; Status = $status; Sec = $r.WallSec; TokPerS = $r.TokPerS; Tokens = $r.Tokens; ThinkChars = $r.ThinkChars; Answer = $oneLine
        })
        [void]$md.AppendLine('')
        [void]$md.AppendLine('### ' + $t.Name + ' - ' + $status + ' - ' + $r.WallSec + ' san')
        [void]$md.AppendLine('Sual: ' + $t.Prompt)
        [void]$md.AppendLine('')
        [void]$md.AppendLine($ans)
        Save-Results
    }

    $avgSec = 0; $avgTps = 0; $avgTok = 0
    if ($n -gt 0) { $avgSec = [math]::Round($wallSum / $n, 1); $avgTps = [math]::Round($tpsSum / $n, 1); $avgTok = [math]::Round($tokSum / $n, 0) }
    [void]$summary.Add([pscustomobject]@{
        Model     = $m
        Dogru     = ('' + $pass + '/' + $auto)
        OrtSaniye = $avgSec
        OrtTokS   = $avgTps
        OrtToken  = $avgTok
        LoadSan   = $warm.LoadSec
        Processor = $procLine
    })
}
if ($prev) { Unload-Model $prev }

$csvPath = Join-Path $OutDir ('results_' + $stamp + '.csv')
$mdPath = Join-Path $OutDir ('results_' + $stamp + '.md')
$rows | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
$md.ToString() | Out-File -FilePath $mdPath -Encoding UTF8

Write-Host ''
Write-Host '=== XULASE ===' -ForegroundColor Cyan
$summary | Format-Table -AutoSize | Out-String -Width 220 | Write-Host
Write-Host ('Tam cavablar: ' + $mdPath)
Write-Host ('CSV:          ' + $csvPath)
Write-Host 'MANUAL olan testlerin (AZ-letters, Explain-RAG) cavabini .md faylinda oxu.'
