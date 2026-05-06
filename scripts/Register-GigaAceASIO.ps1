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
$ClassesRoot = "HKLM:\SOFTWARE\Classes"
$ClsidKey = Join-Path $ClassesRoot "CLSID\$Clsid"
$InprocKey = Join-Path $ClsidKey "InprocServer32"
$AsioKey = "HKLM:\SOFTWARE\ASIO\$DriverName"

New-Item -Path $ClsidKey -Force | Out-Null
New-Item -Path $InprocKey -Force | Out-Null
New-Item -Path $AsioKey -Force | Out-Null

Set-Item -Path $ClsidKey -Value $DriverName
Set-Item -Path $InprocKey -Value $ResolvedDll.Path
Set-ItemProperty -Path $InprocKey -Name "ThreadingModel" -Value "Both"
Set-ItemProperty -Path $AsioKey -Name "CLSID" -Value $Clsid
Set-ItemProperty -Path $AsioKey -Name "Description" -Value $DriverName

Write-Host "Registered GigaACE ASIO Driver:"
Write-Host $ResolvedDll.Path
