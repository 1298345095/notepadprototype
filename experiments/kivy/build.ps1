# Use x64 CPython 3.12 on a native Windows runner.
param(
    [Parameter(Mandatory)][string]$Dll,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
$dllFile = Get-Item -LiteralPath $Dll
$destination = [IO.Directory]::CreateDirectory($OutDir).FullName
$work = [IO.Directory]::CreateDirectory((Join-Path $env:RUNNER_TEMP ("np4-kivy-" + [guid]::NewGuid()))).FullName
function Check-Native($label) {
    if ($LASTEXITCODE -ne 0) { throw "$label exited $LASTEXITCODE" }
}

Copy-Item -LiteralPath $dllFile.FullName -Destination (Join-Path $work 'NP4Probe.dll')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'probe.py') -Destination $work
Push-Location $work
try {
    python -m venv venv
    Check-Native 'venv'
    $python = Join-Path $work 'venv/Scripts/python.exe'
    & $python -m pip install --disable-pip-version-check --only-binary=:all: 'Kivy==2.3.1' 'kivy_deps.sdl2==0.8.0' 'kivy_deps.angle==0.4.0' 'PyInstaller==6.16.0'
    Check-Native 'pip install'
    $env:KIVY_NO_ARGS = '1'
    $env:KIVY_GL_BACKEND = 'angle_sdl2'
    $env:KIVY_WINDOW = 'sdl2'
    & $python probe.py --self-test initial.json
    Check-Native 'Kivy from source'
    Get-Content initial.json

    # Kivy documents shipping SDL and the selected graphics provider alongside
    # its dynamic extension modules. Keep the layout flat inside the one-file bundle.
    $dlls = & $python -c 'import pathlib; from kivy_deps import angle,sdl2; print("\n".join(str(f) for p in [*angle.dep_bins,*sdl2.dep_bins] for f in sorted(pathlib.Path(p).glob("*.dll"))))'
    Check-Native 'locate toolkit libraries'
    if (-not $dlls) { throw 'Could not find SDL2 and ANGLE runtime files' }
    $argsForFreeze = @('--noconfirm','--clean','--noupx','--onefile','--windowed','--name','np4-kivy-probe','--add-binary','NP4Probe.dll;.')
    foreach ($file in $dlls) { $argsForFreeze += @('--add-binary', "${file};.") }
    & $python -m PyInstaller @argsForFreeze probe.py
    Check-Native 'PyInstaller'

    $clean = [IO.Directory]::CreateDirectory((Join-Path $work 'self contained')).FullName
    $exe = Join-Path $clean 'np4-kivy-probe.exe'
    Copy-Item dist/np4-kivy-probe.exe -Destination $exe
    $report = Join-Path $work 'frozen.json'
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $clean
    $start.Environment['PATH'] = "$env:SystemRoot/System32;$env:SystemRoot"
    foreach ($key in @('PYTHONPATH','PYTHONHOME','VIRTUAL_ENV')) { [void]$start.Environment.Remove($key) }
    $start.ArgumentList.Add('--self-test')
    $start.ArgumentList.Add($report)
    $p = [Diagnostics.Process]::Start($start)
    try {
        if (-not $p.WaitForExit(90000)) {
            $p.Kill($true)
            throw 'Kivy one-file self-test timed out'
        }
        if ($p.ExitCode -ne 0) { throw "Kivy one-file self-test exited $($p.ExitCode)" }
    } finally {
        $p.Dispose()
        if (Test-Path "$report.log") { Get-Content "$report.log" -Tail 25 }
    }
    $result = Get-Content -Raw $report | ConvertFrom-Json
    if (-not $result.onefile_extraction -or -not $result.undo -or -not $result.parent_verified) { throw 'Invalid one-file report' }
    if (@(Get-ChildItem $clean -Force).Count -ne 1) { throw 'Unexpected files alongside the finished executable' }
    Copy-Item -LiteralPath $exe -Destination $destination
    $size = (Get-Item $exe).Length
    $hash = (Get-FileHash $exe -Algorithm SHA256).Hash
    Write-Host "PASS Kivy: $(Get-Content -Raw $report)"
    Write-Host "PASS np4-kivy-probe.exe  Bytes=$size  SHA256=$hash"
} finally {
    Pop-Location
}
