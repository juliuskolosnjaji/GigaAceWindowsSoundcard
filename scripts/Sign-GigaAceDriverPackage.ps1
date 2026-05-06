param(
    [string]$PackagePath = "$PSScriptRoot\..\dist\driver",
    [string]$CertificateSubject = "CN=GigaACE Virtual Sound Card Test Certificate",
    [string]$SignTool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
)

$ErrorActionPreference = "Stop"

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}

$PackagePath = (Resolve-Path $PackagePath).Path

if (-not (Test-Path $SignTool)) {
    throw "signtool.exe not found. Expected: $SignTool"
}

$cert = Get-ChildItem Cert:\LocalMachine\My |
    Where-Object { $_.Subject -eq $CertificateSubject } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $CertificateSubject `
        -CertStoreLocation Cert:\LocalMachine\My `
        -KeyExportPolicy Exportable `
        -HashAlgorithm SHA256
}

$rootPath = "Cert:\LocalMachine\Root\$($cert.Thumbprint)"
if (-not (Test-Path $rootPath)) {
    Export-Certificate -Cert $cert -FilePath (Join-Path $PackagePath "GigaACE-TestCert.cer") | Out-Null
    Import-Certificate -FilePath (Join-Path $PackagePath "GigaACE-TestCert.cer") -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
}

Get-ChildItem -Path $PackagePath -Include *.sys,*.dll,*.cat -File -Recurse | ForEach-Object {
    & $SignTool sign /fd SHA256 /sha1 $cert.Thumbprint $_.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed for $($_.FullName) with exit code $LASTEXITCODE"
    }
}

Write-Host "Signed driver package:"
Write-Host $PackagePath
