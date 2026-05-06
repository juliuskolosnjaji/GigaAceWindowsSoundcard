param(
    [string]$InfPath = "$PSScriptRoot\..\dist\driver\ComponentizedAudioSample.inf"
)

$ErrorActionPreference = "Stop"

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}

$ResolvedInf = Resolve-Path $InfPath
pnputil /add-driver $ResolvedInf.Path /install
