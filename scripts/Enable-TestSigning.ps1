$ErrorActionPreference = "Stop"

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}

bcdedit /set testsigning on
if ($LASTEXITCODE -ne 0) {
    throw "bcdedit failed. Secure Boot is probably enabled; test-signing cannot be changed while Secure Boot policy protects it."
}
Write-Host "Test-signing enabled. Reboot Windows before installing test drivers."
