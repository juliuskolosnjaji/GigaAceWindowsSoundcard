param(
    [string]$DllPath = "$PSScriptRoot\..\build\Release\GigaAceASIO.dll"
)

$ErrorActionPreference = "Stop"

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}

$ResolvedDll = Resolve-Path $DllPath

$Clsid = "{7D874A81-989A-457A-9EE8-7E182DDD8F37}"
$DriverName = "GigaACE ASIO Driver"

Remove-Item -Path "HKLM:\SOFTWARE\ASIO\$DriverName" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\SOFTWARE\Classes\CLSID\$Clsid" -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Unregistered GigaACE ASIO Driver."
