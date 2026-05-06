param(
    [string]$OutDir = "$PSScriptRoot\driver-build"
)

$ErrorActionPreference = "Stop"

$CL = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"
$LINK = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"

$KM_INC = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km"
$SHARED_INC = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$WDF_INC = "C:\Program Files (x86)\Windows Kits\10\Include\wdf\kmdf\1.33"
$CRT_INC = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include"
$UCRT_INC = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"

$KM_LIB = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\km\x64"

$SrcDir = "$PSScriptRoot\driver"
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$CommonArgs = @(
    "/I`"$KM_INC`"",
    "/I`"$SHARED_INC`"",
    "/I`"$WDF_INC`"",
    "/I`"$CRT_INC`"",
    "/I`"$UCRT_INC`"",
    "/I`"$SrcDir`"",
    "/DUNICODE", "/D_UNICODE", "/DKM_DRIVER=1", "/DDBG=1",
    "/D_AMD64_", "/D_M_AMD64", "/D_WIN64",
    "/DNTDDI_VERSION=0x0A00000C", "/D_WIN32_WINNT=0x0A00", "/DWINVER=0x0A00",
    "/W4", "/WX-", "/wd4100", "/wd4201", "/wd4214", "/wd4200", "/wd4603", "/wd4627", "/wd4986", "/wd4996",
    "/c", "/kernel", "/GS-", "/GR-", "/Gy", "/Gz",
    "/Zi",
    "/Fo`"$OutDir\\`"",
    "/Fd`"$OutDir\\vc143.pdb`""
)

$Sources = @(
    "driver.cpp",
    "device.cpp",
    "miniportwavert.cpp",
    "miniportwavertstream.cpp",
    "sharedmemory.cpp",
    "trace.cpp"
)

foreach ($src in $Sources) {
    Write-Host "Compiling $src..."
    $ArgsList = $CommonArgs + @("`"$SrcDir\$src`"")
    $process = Start-Process -FilePath $CL -ArgumentList $ArgsList -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        Write-Host "ERROR: Failed to compile $src" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Linking driver..."
$LinkArgs = @(
    "/driver",
    "/nodefaultlib",
    "/out:`"$OutDir\GigaAceVSC.sys`"",
    "/pdb:`"$OutDir\GigaAceVSC.pdb`"",
    "/libpath:`"$KM_LIB`"",
    "ntoskrnl.lib", "hal.lib", "wmilib.lib", "portcls.lib", "stdunk.lib",
    "`"$OutDir\driver.obj`"",
    "`"$OutDir\device.obj`"",
    "`"$OutDir\miniportwavert.obj`"",
    "`"$OutDir\miniportwavertstream.obj`"",
    "`"$OutDir\sharedmemory.obj`"",
    "`"$OutDir\trace.obj`""
)

$process = Start-Process -FilePath $LINK -ArgumentList $LinkArgs -NoNewWindow -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Write-Host "ERROR: Failed to link driver" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Driver built successfully: $OutDir\GigaAceVSC.sys" -ForegroundColor Green
