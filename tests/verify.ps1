# Run with PowerShell 7 on a Windows x64 runner with the MSVC x64 tools.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Exe)

$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) {
    throw 'A 64-bit Windows PowerShell 7 process is required.'
}
$inputFile = Get-Item -LiteralPath $Exe
$systemDir = [Environment]::SystemDirectory
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$dumpbin = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) { throw 'Could not find the MSVC x64 dumpbin.exe.' }

# DUMPBIN prints one name per line for both normal and delay-load dependencies.
# Require an explicit Windows system library or API-set contract. A DLL installed
# by the build toolchain should never qualify by virtue of being on the PATH.
$systemLibraries = @(
    'advapi32.dll', 'bcrypt.dll', 'cabinet.dll', 'cfgmgr32.dll', 'combase.dll',
    'comctl32.dll', 'comdlg32.dll', 'crypt32.dll', 'd2d1.dll', 'd3d11.dll',
    'dbghelp.dll', 'dwmapi.dll', 'dwrite.dll', 'dxgi.dll', 'gdi32.dll',
    'gdiplus.dll', 'hid.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll',
    'kernelbase.dll', 'mpr.dll', 'msimg32.dll', 'msvcrt.dll', 'netapi32.dll',
    'normaliz.dll', 'ntdll.dll', 'ole32.dll', 'oleacc.dll', 'oleaut32.dll',
    'oledlg.dll', 'opengl32.dll', 'powrprof.dll', 'propsys.dll', 'psapi.dll',
    'rpcrt4.dll', 'secur32.dll', 'setupapi.dll', 'shcore.dll', 'shell32.dll',
    'shlwapi.dll', 'ucrtbase.dll', 'uiautomationcore.dll', 'urlmon.dll',
    'user32.dll', 'userenv.dll', 'usp10.dll', 'uxtheme.dll', 'version.dll',
    'windowscodecs.dll', 'winhttp.dll', 'wininet.dll', 'winmm.dll',
    'winspool.drv', 'wintrust.dll', 'ws2_32.dll', 'wtsapi32.dll'
)
$dump = & $dumpbin /nologo /dependents $inputFile.FullName
if ($LASTEXITCODE -ne 0) { throw "DUMPBIN failed for $Exe." }
$imports = @($dump | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^[^:]+\.(dll|drv)$' } | Sort-Object -Unique)
if ($imports.Count -eq 0) { throw 'No PE imports found in DUMPBIN output.' }
foreach ($dll in $imports) {
    if ($dll -match '^(api|ext)-ms-win-[a-z0-9-]+\.dll$') { continue }
    if ($dll -notin $systemLibraries -or -not [IO.File]::Exists((Join-Path $systemDir $dll))) {
        throw "Unexpected non-system PE dependency: $dll"
    }
}
Write-Host "Native PE imports: $($imports -join ', ')"

function Run-EditorTest([string[]]$EditorArguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $isolatedExe
    $start.WorkingDirectory = $work
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['PATH'] = "$systemDir;$env:SystemRoot"
    foreach ($arg in $EditorArguments) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(45000)) {
            $process.Kill($true)
            throw "Timed out waiting for $($EditorArguments[0])."
        }
        Write-Host ($stdout.GetAwaiter().GetResult())
        Write-Host ($stderr.GetAwaiter().GetResult())
        if ($process.ExitCode -ne 0) {
            throw "$($EditorArguments[0]) failed with process exit code $($process.ExitCode)."
        }
    } finally {
        $process.Dispose()
    }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("NP4 native verification " + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($work)
try {
    $isolatedExe = Join-Path $work $inputFile.Name
    Copy-Item -LiteralPath $inputFile.FullName -Destination $isolatedExe
    Run-EditorTest -EditorArguments @('--self-test')
    $utf8 = [Text.UTF8Encoding]::new($false)
    $fixture = Join-Path $work '编辑 测试.json'
    [IO.File]::WriteAllBytes($fixture, $utf8.GetBytes("{`"打开`":`"测试🙂`"}`n"))
    Run-EditorTest -EditorArguments @('--file-self-test', $fixture)
    $expected = $utf8.GetBytes("Native save 中文`n")
    $actual = [IO.File]::ReadAllBytes($fixture)
    if ([Convert]::ToBase64String($actual) -cne [Convert]::ToBase64String($expected)) {
        throw 'Saved bytes differ from the expected UTF-8 contents.'
    }
    $extra = @(Get-ChildItem -LiteralPath $work -Force | Where-Object { $_.FullName -notin @($isolatedExe, $fixture) })
    if ($extra.Count) { throw "Unexpected items created next to the EXE: $($extra.Name -join ', ')" }
    $hash = (Get-FileHash -LiteralPath $isolatedExe -Algorithm SHA256).Hash
    Write-Host "PASS $($inputFile.Name)  Bytes=$($inputFile.Length)  SHA256=$hash"
} finally {
    [IO.Directory]::Delete($work, $true)
}
