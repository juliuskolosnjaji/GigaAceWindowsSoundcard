#requires -RunAsAdministrator
<#
.SYNOPSIS
    Uninstalls the GigaACE Virtual Sound Card kernel-mode driver.
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DriverDir = Join-Path $ScriptDir "..\driver"
$InfPath = Join-Path $DriverDir "GigaAceVSC.inf"

Write-Host "=== GigaACE Virtual Sound Card Driver Uninstaller ===" -ForegroundColor Cyan

# 1. Stop the service
Write-Host "`n[1/3] Stopping driver service..." -ForegroundColor Yellow
sc stop GigaAceVSC 2>$null

# 2. Remove the device
Write-Host "`n[2/3] Removing device..." -ForegroundColor Yellow
devcon remove $InfPath "ROOT\GigaAceVSC" 2>$null

# 3. Delete the service
Write-Host "`n[3/3] Deleting service..." -ForegroundColor Yellow
sc delete GigaAceVSC 2>$null

Write-Host "`nDriver uninstalled." -ForegroundColor Green
