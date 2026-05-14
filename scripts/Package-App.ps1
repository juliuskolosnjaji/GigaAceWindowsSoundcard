param(
    [string]$OutputRoot = "$PSScriptRoot\..\dist\app"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Release = Join-Path $Root "build\Release"
$OutputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
$PackageDir = Join-Path $OutputRoot "GigaACEVirtualSoundCard"
$ZipPath = Join-Path $OutputRoot "GigaACEVirtualSoundCard.zip"
$SingleExePath = Join-Path $OutputRoot "GigaACEVirtualSoundCardSetup.exe"
$LegacyIExpressDir = Join-Path $OutputRoot "iexpress"
$LegacySedPath = Join-Path $OutputRoot "GigaACEVirtualSoundCardSetup.sed"

Push-Location $Root
try {
    .\scripts\Build-Release.ps1
    .\scripts\Deploy-Qt.ps1
}
finally {
    Pop-Location
}

if (Test-Path $PackageDir) {
    Remove-Item -Path $PackageDir -Recurse -Force
}
if (Test-Path $LegacyIExpressDir) {
    Remove-Item -Path $LegacyIExpressDir -Recurse -Force
}
if (Test-Path $LegacySedPath) {
    Remove-Item -Path $LegacySedPath -Force
}
New-Item -ItemType Directory -Path $PackageDir | Out-Null

$files = @(
    "GigaAceVirtualSoundCard.exe",
    "GigaAceASIO.dll",
    "GigaAceReplay.exe",
    "GigaAceAnalyze.exe",
    "GigaAceIdentify.exe",
    "GigaAceSetup.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Concurrent.dll",
    "Qt6Network.dll",
    "d3dcompiler_47.dll",
    "opengl32sw.dll"
)

foreach ($file in $files) {
    $src = Join-Path $Release $file
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $PackageDir -Force
    }
}

foreach ($dir in @("platforms", "styles")) {
    $src = Join-Path $Release $dir
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $PackageDir -Recurse -Force
    }
}

@"
GigaACE Virtual Sound Card

1. Run GigaAceSetup.exe as administrator.
2. The setup copies the app to Program Files, registers the ASIO driver, creates shortcuts, and starts the GUI.
3. Start audio in the GUI before selecting the ASIO driver in REAPER.
4. In REAPER choose Audio system: ASIO, Driver: GigaACE ASIO Driver.

For recorded captures:
GigaAceReplay.exe --input capture.pcapng --loop --channels 64

For protocol analysis:
GigaAceAnalyze.exe --input capture.pcapng --tone 440 --slots 128

For controlled stagebox identification tests:
GigaAceIdentify.exe --list
GigaAceIdentify.exe --interface "\Device\NPF_{...}" --send --variant 0 --seconds 10
"@ | Set-Content -Path (Join-Path $PackageDir "README.txt") -Encoding ASCII

if (Test-Path $ZipPath) {
    Remove-Item -Path $ZipPath -Force
}
Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $ZipPath

$cscCandidates = @(
    "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)
$csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($csc) {
    $bootstrapSource = Join-Path $env:TEMP "GigaACEVirtualSoundCardSetup.cs"
    @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows.Forms;

internal static class Program {
    [STAThread]
    private static int Main() {
        string tempRoot = Path.Combine(Path.GetTempPath(), "GigaACEVirtualSoundCardSetup-" + Guid.NewGuid().ToString("N"));
        string zipPath = Path.Combine(tempRoot, "payload.zip");
        try {
            Directory.CreateDirectory(tempRoot);
            using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")) {
                if (input == null) throw new InvalidOperationException("Embedded installer payload not found.");
                using (FileStream output = File.Create(zipPath)) {
                    input.CopyTo(output);
                }
            }
            ZipFile.ExtractToDirectory(zipPath, tempRoot);
            string setup = Path.Combine(tempRoot, "GigaAceSetup.exe");
            if (!File.Exists(setup)) throw new FileNotFoundException("GigaAceSetup.exe not found in payload.", setup);
            ProcessStartInfo psi = new ProcessStartInfo(setup) {
                UseShellExecute = true,
                Verb = "runas",
                WorkingDirectory = tempRoot
            };
            using (Process process = Process.Start(psi)) {
                process.WaitForExit();
                return process.ExitCode;
            }
        } catch (Exception ex) {
            MessageBox.Show(ex.Message, "GigaACE setup failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        } finally {
            try {
                if (Directory.Exists(tempRoot)) Directory.Delete(tempRoot, true);
            } catch {
            }
        }
    }
}
'@ | Set-Content -Path $bootstrapSource -Encoding ASCII

    if (Test-Path $SingleExePath) {
        Remove-Item -Path $SingleExePath -Force
    }

    & $csc /nologo /target:winexe /optimize+ "/out:$SingleExePath" "/resource:$ZipPath,payload.zip" `
        /reference:System.IO.Compression.dll `
        /reference:System.IO.Compression.FileSystem.dll `
        /reference:System.Windows.Forms.dll `
        $bootstrapSource
}

Write-Host "Package directory:"
Write-Host $PackageDir
Write-Host "ZIP package:"
Write-Host $ZipPath
if (Test-Path $SingleExePath) {
    Write-Host "Single EXE installer:"
    Write-Host $SingleExePath
}
