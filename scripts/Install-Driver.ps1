#requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs the GigaACE Virtual Sound Card kernel-mode driver.

.DESCRIPTION
    This script installs the GigaACE VSC driver, creates a root-enumerated
    device node, and starts the driver. Requires administrator privileges.

.NOTES
    The driver must be built first:
      cmake -DBUILD_DRIVER=ON -S . -B build
      cmake --build build --config Release

    Test-signing must be enabled for development builds:
      bcdedit /set testsigning on
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DriverDir = Join-Path $ScriptDir "..\driver"
$BuildDir = Join-Path $ScriptDir "..\build\driver\Release"

$InfPath = Join-Path $DriverDir "GigaAceVSC.inf"
$SysPath = Join-Path $BuildDir "GigaAceVSC.sys"

if (-not (Test-Path $InfPath)) {
    Write-Error "INF file not found: $InfPath"
    exit 1
}

if (-not (Test-Path $SysPath)) {
    Write-Error "Driver binary not found: $SysPath`nBuild the driver first."
    exit 1
}

Write-Host "=== GigaACE Virtual Sound Card Driver Installer ===" -ForegroundColor Cyan

# 1. Install the driver store entry
Write-Host "`n[1/4] Adding driver to driver store..." -ForegroundColor Yellow
$Result = pnputil /add-driver $InfPath /install 2>&1
Write-Host $Result

# 2. Create a root-enumerated device node
Write-Host "`n[2/4] Creating root-enumerated device..." -ForegroundColor Yellow
$DevInstResult = devcon install $InfPath "ROOT\GigaAceVSC" 2>&1
Write-Host $DevInstResult

# 3. Start the driver
Write-Host "`n[3/4] Starting driver service..." -ForegroundColor Yellow
Start-Sleep -Seconds 2
$ServiceResult = sc start GigaAceVSC 2>&1
Write-Host $ServiceResult

# 4. Verify
Write-Host "`n[4/4] Verifying installation..." -ForegroundColor Yellow
Start-Sleep -Seconds 1
Get-PnpDevice -Class MEDIA | Where-Object { $_.FriendlyName -match "GigaACE" } | Format-Table Status, Class, FriendlyName, InstanceId

Write-Host "`nInstallation complete. Check Sound Settings for 'GigaACE Virtual Sound Card'." -ForegroundColor Green
Write-Host "Note: If test-signing is not enabled, run: bcdedit /set testsigning on  (then reboot)" -ForegroundColor Yellow
