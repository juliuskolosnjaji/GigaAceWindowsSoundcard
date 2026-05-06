param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$QtRoot = Join-Path $Root "qt6\6.10.3\msvc2022_64"
$QtBin = Join-Path $QtRoot "bin"
$QtPlugins = Join-Path $QtRoot "plugins"
$OutDir = Join-Path $Root "build\$Configuration"
$Exe = Join-Path $OutDir "GigaAceVirtualSoundCard.exe"

if (-not (Test-Path $Exe)) {
    throw "Build output not found: $Exe"
}

if (-not (Test-Path $QtBin)) {
    throw "Qt bin directory not found: $QtBin"
}

$dlls = @(
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Concurrent.dll",
    "Qt6Network.dll"
)

foreach ($dll in $dlls) {
    Copy-Item -LiteralPath (Join-Path $QtBin $dll) -Destination $OutDir -Force
}

$platformOut = Join-Path $OutDir "platforms"
New-Item -ItemType Directory -Path $platformOut -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $QtPlugins "platforms\qwindows.dll") -Destination $platformOut -Force

$stylesOut = Join-Path $OutDir "styles"
New-Item -ItemType Directory -Path $stylesOut -Force | Out-Null
$stylePlugin = Join-Path $QtPlugins "styles\qmodernwindowsstyle.dll"
if (Test-Path $stylePlugin) {
    Copy-Item -LiteralPath $stylePlugin -Destination $stylesOut -Force
}

$optionalRuntimeFiles = @(
    "d3dcompiler_47.dll",
    "opengl32sw.dll"
)

foreach ($file in $optionalRuntimeFiles) {
    $path = Join-Path $QtBin $file
    if (Test-Path $path) {
        Copy-Item -LiteralPath $path -Destination $OutDir -Force
    }
}

Write-Host "Qt deployment complete: $OutDir"
