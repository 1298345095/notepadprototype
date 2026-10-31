# Run inside the repository root with an x64 MSVC Developer Command Prompt.
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$tcl = Join-Path $root 'deps/tcl'
$tk = Join-Path $root 'deps/tk'
function Invoke-Nmake([string]$where, [string[]]$options) {
    Push-Location "$where/win"
    try {
        & nmake.exe /nologo /f makefile.vc 'OPTS=static,nomsvcrt' 'MACHINE=AMD64' @options
        if ($LASTEXITCODE) { throw "nmake exited $LASTEXITCODE in $where" }
    } finally { Pop-Location }
}
Invoke-Nmake $tcl @('shell')
Invoke-Nmake $tk @("TCLDIR=$tcl",'release')
function One([string]$dir,[string]$pattern) {
    $found = @(Get-ChildItem -LiteralPath "$dir/win" -Recurse -File -Filter $pattern)
    if ($found.Count -ne 1) { throw "Expected one $pattern under $dir/win; found $($found.Count)" }
    return $found[0].FullName
}
$tclLib = One $tcl 'tcl90sx.lib'
$tkLib = One $tk 'tcl9tk90sx.lib'
$tclStub = One $tcl 'tclstub.lib'
$tkStub = One $tk 'tkstub.lib'
$resource = One $tk 'wish.res'
$scripts = One $tk 'wish.zip'
& cmake -S $PSScriptRoot -B build-tk -G Ninja -DCMAKE_BUILD_TYPE=Release "-DTCL_LIB=$tclLib" "-DTK_LIB=$tkLib" "-DTK_STUB=$tkStub" "-DTCL_STUB=$tclStub" "-DTK_WISH_RESOURCE=$resource"
if ($LASTEXITCODE) { throw 'cmake configure failed' }
& cmake --build build-tk --target np4-tk --parallel 4
if ($LASTEXITCODE) { throw 'cmake build failed' }
# This is how the official static wish target embeds Tcl and Tk's script libraries.
$out = [IO.Directory]::CreateDirectory((Join-Path $root 'out/tk')).FullName
$file = [IO.File]::Create((Join-Path $out 'np4-tk.exe'))
try {
    foreach ($part in @((Join-Path $root 'build-tk/np4-tk.exe'), $scripts)) {
        $inputFile = [IO.File]::OpenRead($part)
        try { $inputFile.CopyTo($file) } finally { $inputFile.Dispose() }
    }
} finally { $file.Dispose() }
