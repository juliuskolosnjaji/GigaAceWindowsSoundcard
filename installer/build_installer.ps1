$ErrorActionPreference = "Stop"
$root    = Split-Path $PSScriptRoot -Parent
$build   = "$root\build"
$release = "$build\Release"
$qtbin   = (Get-Content "$build\CMakeCache.txt" | Select-String "Qt6Core_DIR:PATH=(.+)/lib" |
            ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1) + "\bin"

Write-Host "==> Building..." -ForegroundColor Cyan
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "==> Running windeployqt from $qtbin..." -ForegroundColor Cyan
& "$qtbin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler "$release\GigaAceVirtualSoundCard.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

Write-Host "==> Compiling installer..." -ForegroundColor Cyan
$iscc = "C:\Program Files (x86)\Inno Setup 6\iscc.exe"
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 not found at $iscc. Install from https://jrsoftware.org/isdl.php" }
& $iscc "$PSScriptRoot\GigaACE.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }

$setup = Get-ChildItem "$PSScriptRoot\GigaACE_Setup_*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host "==> Done: $($setup.FullName)" -ForegroundColor Green
