param(
    [string]$Target = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Solution = Join-Path $Root "build\GigaAceVirtualSoundCard.sln"
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if (-not (Test-Path $Solution)) {
    Push-Location $Root
    try {
        cmake -S . -B build
        if ($LASTEXITCODE -ne 0) {
            throw "cmake configure failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

if (-not (Test-Path $MSBuild)) {
    throw "MSBuild not found. Expected: $MSBuild"
}

$PathValue = [System.Environment]::GetEnvironmentVariable("Path", "Process")
if (-not $PathValue) {
    $PathValue = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $MSBuild
if ($Target) {
    $Project = Join-Path $Root "build\$Target.vcxproj"
    if (-not (Test-Path $Project)) {
        throw "Target project not found: $Project"
    }
    $psi.Arguments = "`"$Project`" /p:Configuration=Release /m:1"
} else {
    $psi.Arguments = "`"$Solution`" /p:Configuration=Release /m:1"
}
$psi.UseShellExecute = $false
$psi.WorkingDirectory = $Root

foreach ($key in @($psi.Environment.Keys)) {
    if ($key -ieq "PATH") {
        [void]$psi.Environment.Remove($key)
    }
}
$psi.Environment["Path"] = $PathValue

$process = [System.Diagnostics.Process]::Start($psi)
$process.WaitForExit()
if ($process.ExitCode -ne 0) {
    throw "Release build failed with exit code $($process.ExitCode)"
}
