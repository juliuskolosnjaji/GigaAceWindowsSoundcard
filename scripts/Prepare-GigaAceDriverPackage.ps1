param(
    [string]$SourcePackage = "$PSScriptRoot\..\_vendor\Windows-driver-samples\audio\sysvad\x64\Debug\package",
    [string]$OutputPackage = "$PSScriptRoot\..\dist\driver",
    [string]$Inf2Cat = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x86\inf2cat.exe"
)

$ErrorActionPreference = "Stop"

$SourcePackage = (Resolve-Path $SourcePackage).Path
$OutputPackage = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPackage)

if (-not (Test-Path $Inf2Cat)) {
    throw "inf2cat.exe not found. Expected: $Inf2Cat"
}

if (-not (Test-Path $OutputPackage)) {
    New-Item -ItemType Directory -Path $OutputPackage | Out-Null
}

Get-ChildItem -Path $OutputPackage -Force | Remove-Item -Recurse -Force
Copy-Item -Path (Join-Path $SourcePackage "*") -Destination $OutputPackage -Recurse -Force

$audioInf = Join-Path $OutputPackage "ComponentizedAudioSample.inf"
$apoInf = Join-Path $OutputPackage "ComponentizedApoSample.inf"
$extensionInf = Join-Path $OutputPackage "ComponentizedAudioSampleExtension.inf"

$replacements = [ordered]@{
    "TODO-Set-Provider" = "GigaACE Virtual Sound Card"
    "TODO-Set-Manufacturer" = "GigaACE Virtual Sound Card"
    "TODO-Set-Copyright" = "Copyright (c) 2026"
    "Virtual Audio Device \(WDM\) - Tablet Sample" = "GigaACE Virtual Sound Card"
    "Virtual Audio Device \(WDM\) - Tablet Sample Driver" = "GigaACE Virtual Sound Card Driver"
    "Virtual Audio Device \(WDM\) - Midi Device" = "GigaACE Virtual MIDI"
    "Root\\sysvad_ComponentizedAudioSample" = "Root\GigaACEVirtualSoundCard"
    "SYSVAD Wave Speaker" = "GigaACE Monitor Speaker"
    "SYSVAD Topology Speaker" = "GigaACE Monitor Speaker Topology"
    "SYSVAD Wave Speaker Headphone" = "GigaACE Monitor Headphones"
    "SYSVAD Topology Speaker Headphone" = "GigaACE Monitor Headphones Topology"
    "SYSVAD Wave HDMI" = "GigaACE Virtual HDMI"
    "SYSVAD Topology HDMI" = "GigaACE Virtual HDMI Topology"
    "SYSVAD Wave SPDIF" = "GigaACE Virtual SPDIF"
    "SYSVAD Topology SPDIF" = "GigaACE Virtual SPDIF Topology"
    "SYSVAD Wave Microphone Headphone" = "GigaACE Virtual Input 1-2"
    "SYSVAD Topology Microphone Headphone" = "GigaACE Virtual Input 1-2 Topology"
    "SYSVAD Wave Microphone Array - Front" = "GigaACE Virtual Input 1-8"
    "SYSVAD Topology Microphone Array - Front" = "GigaACE Virtual Input 1-8 Topology"
    "SYSVAD Wave Microphone Array - Rear" = "GigaACE Virtual Input 9-16"
    "SYSVAD Topology Microphone Array - Rear" = "GigaACE Virtual Input 9-16 Topology"
    "SYSVAD Wave Microphone Array - Front/Rear" = "GigaACE Virtual Input 1-16"
    "SYSVAD Topology Microphone Array - Front/Rear" = "GigaACE Virtual Input 1-16 Topology"
    "SYSVAD Wave Bluetooth HFP Speaker" = "GigaACE Talkback Speaker"
    "SYSVAD Topology Bluetooth HFP Speaker" = "GigaACE Talkback Speaker Topology"
    "SYSVAD Wave Bluetooth HFP Microphone" = "GigaACE Talkback Microphone"
    "SYSVAD Topology Bluetooth HFP Microphone" = "GigaACE Talkback Microphone Topology"
    "SYSVAD Wave USB Headset Speaker" = "GigaACE Cue Speaker"
    "SYSVAD Topology USB Headset Speaker" = "GigaACE Cue Speaker Topology"
    "SYSVAD Wave USB Headset Microphone" = "GigaACE Cue Microphone"
    "SYSVAD Topology USB Headset Microphone" = "GigaACE Cue Microphone Topology"
    "Internal Microphone Array - Front" = "GigaACE Input Bank A"
    "Internal Microphone Array - Rear" = "GigaACE Input Bank B"
    "Internal Microphone Array - Front/Rear" = "GigaACE Input Banks A+B"
    "External Microphone Headphone" = "GigaACE Input Pair"
}

foreach ($inf in @($audioInf, $apoInf, $extensionInf)) {
    if (-not (Test-Path $inf)) {
        continue
    }

    $content = Get-Content -Raw -Path $inf
    foreach ($pair in $replacements.GetEnumerator()) {
        $content = $content -replace $pair.Key, $pair.Value
    }
    Set-Content -Path $inf -Value $content -Encoding ASCII
}

Remove-Item -Path (Join-Path $OutputPackage "*.cat") -Force -ErrorAction SilentlyContinue

& $Inf2Cat /os:10_x64 /driver:"$OutputPackage\"
if ($LASTEXITCODE -ne 0) {
    throw "inf2cat failed with exit code $LASTEXITCODE"
}

Write-Host "Prepared unsigned GigaACE driver package:"
Write-Host $OutputPackage
