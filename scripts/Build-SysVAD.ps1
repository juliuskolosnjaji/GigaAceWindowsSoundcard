$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$SamplesRoot = Join-Path $Root "_vendor\Windows-driver-samples"
$Solution = Join-Path $SamplesRoot "audio\sysvad\sysvad.sln"
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

function Invoke-CleanMSBuild {
    param(
        [Parameter(Mandatory=$true)]
        [string[]] $Arguments
    )

    $PathValue = [System.Environment]::GetEnvironmentVariable("Path", "Process")
    if (-not $PathValue) {
        $PathValue = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    }

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $MSBuild
    foreach ($arg in $Arguments) {
        [void]$psi.ArgumentList.Add($arg)
    }
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $SamplesRoot

    foreach ($key in @($psi.Environment.Keys)) {
        if ($key -ieq "PATH") {
            [void]$psi.Environment.Remove($key)
        }
    }
    $psi.Environment["Path"] = $PathValue

    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    return $process.ExitCode
}

if (-not (Test-Path $Solution)) {
    throw "SysVAD solution not found. Expected: $Solution"
}

if (-not (Test-Path $MSBuild)) {
    throw "MSBuild not found. Expected: $MSBuild"
}

Push-Location $SamplesRoot
try {
    $WilHeader = Join-Path $SamplesRoot "wil\include\wil\resource.h"
    if (-not (Test-Path $WilHeader)) {
        git submodule update --init
        if ($LASTEXITCODE -ne 0) {
            throw "git submodule update failed with exit code $LASTEXITCODE"
        }
    }

    $ExitCode = Invoke-CleanMSBuild @(
        "audio\sysvad\sysvad.sln",
        "/p:Configuration=Debug",
        "/p:Platform=x64",
        "/p:SpectreMitigation=false",
        "/p:SkipPackageVerification=true",
        "/p:EnableTestSign=false",
        "/p:SignMode=Off",
        "/m:1",
        "/v:normal"
    )
    if ($ExitCode -ne 0) {
        throw "SysVAD build failed with exit code $ExitCode"
    }
}
finally {
    Pop-Location
}
