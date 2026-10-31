param(
    [Parameter(Mandatory)][string]$FltkExe,
    [Parameter(Mandatory)][string]$WxExe,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$files = @{ FLTK = Get-Item -LiteralPath $FltkExe; wxMSW = Get-Item -LiteralPath $WxExe }
[IO.Directory]::CreateDirectory($OutDir) | Out-Null

function Invoke-Benchmark([IO.FileInfo]$Exe) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe.FullName
    $start.WorkingDirectory = $Exe.DirectoryName
    $start.ArgumentList.Add('--bench')
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['NP4_BENCH_ALL_EVENTS'] = '0'
    $process = [Diagnostics.Process]::Start($start)
    try {
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(90000)) {
            $process.Kill($true)
            throw "$($Exe.Name) --bench timed out"
        }
        $outputText = $outTask.GetAwaiter().GetResult()
        $errorText = $errTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "$($Exe.Name) --bench exited $($process.ExitCode). $errorText" }
        return $outputText
    } finally {
        $process.Dispose()
    }
}

$order = @('FLTK', 'wxMSW', 'wxMSW', 'FLTK', 'FLTK', 'wxMSW')
$results = for ($i = 0; $i -lt $order.Length; ++$i) {
    $name = $order[$i]
    Write-Host "Launch $($i + 1) of $($order.Length): $name"
    $outputText = Invoke-Benchmark $files[$name]
    $lines = $outputText.TrimEnd("`r", "`n") -split '\r?\n'
    if ($lines.Count -ne 5) { throw "$name returned $($lines.Count) samples instead of 5. $outputText" }
    for ($j = 0; $j -lt $lines.Count; ++$j) {
        if ($lines[$j] -cnotmatch '^NP4_CORE,([1-5]),(4194288),([0-9]+\.[0-9]+),([0-9]+\.[0-9]+),([0-9]+\.[0-9]+)$') {
            throw "Unexpected output from $name at trial $($j + 1): $($lines[$j])"
        }
        if ([int]$Matches[1] -ne $j + 1) { throw "Wrong trial sequence from $name" }
        [pscustomobject]@{
            toolkit = $name; launch = $i + 1; trial = $j + 1; bytes = [long]$Matches[2]
            load_ms = [double]::Parse($Matches[3], $invariant)
            colourise_ms = [double]::Parse($Matches[4], $invariant)
            edit_undo_64_ms = [double]::Parse($Matches[5], $invariant)
        }
    }
}
$results | Export-Csv -LiteralPath (Join-Path $OutDir 'samples.csv') -NoTypeInformation -Encoding utf8

$summary = @(
    '## Native Windows x64 editor comparison'
    ''
    'Each host ran three times on this runner, with five timed trials per process after warm-up. Values are p50 of 15 samples per host. Each trial uses 4,194,288 UTF-8 bytes and the Notepad4 insert/delete notification mask.'
    ''
    '| Host | EXE bytes | Load ms | JSON colourise ms | 64 replace-and-undo pairs ms | EXE SHA-256 |'
    '|---|---:|---:|---:|---:|---|'
    foreach ($name in @('FLTK', 'wxMSW')) {
        $rows = @($results | Where-Object toolkit -eq $name)
        $medians = foreach ($property in @('load_ms', 'colourise_ms', 'edit_undo_64_ms')) {
            $sorted = @($rows | ForEach-Object { $_.$property } | Sort-Object)
            $sorted[7].ToString('F3', $invariant)
        }
        $exe = $files[$name]
        $hash = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        '| {0} | {1} | {2} | {3} | {4} | `{5}` |' -f $name, $exe.Length, $medians[0], $medians[1], $medians[2], $hash
    }
    ''
    'The host sends messages to its actual Win32 Scintilla child control, including synchronous parent notifications. The input is already in memory and redraw is suspended. These timings exclude file I/O, application startup, compositor presentation and interactive input latency.'
)
$summary | Write-Output
$summary | Set-Content -LiteralPath (Join-Path $OutDir 'summary.md') -Encoding utf8
if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8 }
