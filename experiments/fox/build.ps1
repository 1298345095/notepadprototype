# Run in an x64 VS developer shell on Windows.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$deps = Join-Path $repo 'deps'
[void][IO.Directory]::CreateDirectory($deps)
$archive = Join-Path $deps 'fox-1.6.59.tar.gz'
$expected = '48f33d2dd5371c2d48f6518297f0ef5bbf3fcd37719e99f815dc6fc6e0f928ae'
if (!(Test-Path $archive)) {
    curl.exe --fail --silent --show-error --location --retry 2 'https://distfiles.macports.org/fox/fox-1.6.59.tar.gz' -o $archive
    if ($LASTEXITCODE) { throw 'FOX source download failed' }
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $expected) { throw 'FOX source hash mismatch' }
tar.exe -xf $archive -C $deps
if ($LASTEXITCODE) { throw 'FOX extraction failed' }
$build = Join-Path $repo 'build-fox'
cmake -S $PSScriptRoot -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'FOX configure failed' }
cmake --build $build --target np4-fox --parallel 4
if ($LASTEXITCODE) { throw 'FOX build failed' }
