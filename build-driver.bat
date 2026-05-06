@echo off
setlocal

set CL="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"
set LINK="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"

set KM_INC="C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km"
set SHARED_INC="C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
set WDF_INC="C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\wdf\kmdf\1.33"
set CRT_INC="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include"
set UCRT_INC="C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"

set KM_LIB="C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\km\x64"
set CRT_LIB="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64"
set UCRT_LIB="C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"

set OUT_DIR=%~dp0..\driver-build

if not exist %OUT_DIR% mkdir %OUT_DIR%

echo Compiling driver files...

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0driver.cpp

if errorlevel 1 goto :error

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0device.cpp

if errorlevel 1 goto :error

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0miniportwavert.cpp

if errorlevel 1 goto :error

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0miniportwavertstream.cpp

if errorlevel 1 goto :error

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0sharedmemory.cpp

if errorlevel 1 goto :error

%CL% /c ^
  /I %KM_INC% ^
  /I %SHARED_INC% ^
  /I %WDF_INC% ^
  /I %CRT_INC% ^
  /I %UCRT_INC% ^
  /I %~dp0 ^
  /DUNICODE /D_UNICODE /DKM_DRIVER=1 /DDBG=1 /DNTDDI_VERSION=0x0A00000C /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 ^
  /W4 /WX- /wd4100 /wd4201 /wd4214 /wd4200 /wd4603 /wd4627 /wd4986 /wd4996 ^
  /kernel /GS- /GR- /Gy /Gz ^
  /Zi ^
  /Fo%OUT_DIR%\ ^
  /Fd%OUT_DIR%\vc143.pdb ^
  %~dp0trace.cpp

if errorlevel 1 goto :error

echo Linking driver...

%LINK% ^
  /driver ^
  /out:%OUT_DIR%\GigaAceVSC.sys ^
  /pdb:%OUT_DIR%\GigaAceVSC.pdb ^
  /libpath:%KM_LIB% ^
  /libpath:%CRT_LIB% ^
  /libpath:%UCRT_LIB% ^
  ntoskrnl.lib hal.lib wmilib.lib portcls.lib stdunk.lib ^
  %OUT_DIR%\driver.obj ^
  %OUT_DIR%\device.obj ^
  %OUT_DIR%\miniportwavert.obj ^
  %OUT_DIR%\miniportwavertstream.obj ^
  %OUT_DIR%\sharedmemory.obj ^
  %OUT_DIR%\trace.obj

if errorlevel 1 goto :error

echo.
echo Driver built successfully: %OUT_DIR%\GigaAceVSC.sys
goto :done

:error
echo.
echo ERROR: Driver build failed!
exit /b 1

:done
