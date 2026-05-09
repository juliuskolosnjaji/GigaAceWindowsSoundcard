#define AppName      "GigaACE Virtual Sound Card"
#define AppVersion   "0.1.0"
#define AppRevision  GetDateTimeString('yyyymmdd-hhnnss', '', '')
#define AppFullVersion AppVersion + "-dev." + AppRevision
#define AppPublisher "GigaAce"
#define AppURL       ""
#define BuildDir     "..\build\Release"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppFullVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppId={{A3F9C2E1-4B87-4D3A-8E92-1F6D5C4B7A20}
DefaultDirName={autopf}\GigaACE Virtual Sound Card
DefaultGroupName=GigaACE Virtual Sound Card
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename=GigaACE_Setup_{#AppFullVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\GigaAceVirtualSoundCard.exe
SetupIconFile=
WizardStyle=modern
LicenseFile=
CloseApplications=yes
CloseApplicationsFilter=GigaAceVirtualSoundCard.exe,GigaAceReplay.exe
RestartIfNeededByRun=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Main executable
Source: "{#BuildDir}\GigaAceVirtualSoundCard.exe"; DestDir: "{app}"; Flags: ignoreversion

; ASIO driver
Source: "{#BuildDir}\GigaAceASIO.dll"; DestDir: "{app}"; Flags: ignoreversion regserver

; CLI replay tool
Source: "{#BuildDir}\GigaAceReplay.exe"; DestDir: "{app}"; Flags: ignoreversion

; Qt core DLLs
Source: "{#BuildDir}\Qt6Core.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Gui.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Widgets.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Network.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Concurrent.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Svg.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\icuuc.dll";         DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\d3dcompiler_47.dll";DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\opengl32sw.dll";    DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Qt plugins
Source: "{#BuildDir}\platforms\*";          DestDir: "{app}\platforms";          Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\styles\*";             DestDir: "{app}\styles";             Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\imageformats\*";       DestDir: "{app}\imageformats";       Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\iconengines\*";        DestDir: "{app}\iconengines";        Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\generic\*";            DestDir: "{app}\generic";            Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\tls\*";               DestDir: "{app}\tls";                Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\GigaACE Virtual Sound Card"; Filename: "{app}\GigaAceVirtualSoundCard.exe"
Name: "{group}\Uninstall GigaACE";          Filename: "{uninstallexe}"
Name: "{autodesktop}\GigaACE Virtual Sound Card"; Filename: "{app}\GigaAceVirtualSoundCard.exe"; Tasks: desktopicon

[Run]
; Offer to launch after install
Filename: "{app}\GigaAceVirtualSoundCard.exe"; Description: "Launch GigaACE Virtual Sound Card"; Flags: nowait postinstall skipifsilent

[Code]
function VCRedistInstalled: Boolean;
var
  installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed', installed) and (installed = 1);
end;

function NpcapInstalled: Boolean;
var
  dummy: String;
begin
  Result := RegQueryStringValue(HKLM, 'SOFTWARE\Npcap', '', dummy) or
            RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Npcap', '', dummy);
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if not VCRedistInstalled then begin
    if MsgBox(
      'The Visual C++ 2022 x64 Redistributable is not installed.' + #13#10 +
      'The application will not start without it.' + #13#10#13#10 +
      'Download it from microsoft.com/en-us/download (search vc_redist.x64.exe).' + #13#10#13#10 +
      'Continue installing anyway?',
      mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrCode: Integer;
  OldDLL: String;
begin
  if CurStep = ssInstall then begin
    OldDLL := ExpandConstant('{app}\GigaAceASIO.dll');
    if FileExists(OldDLL) then
      Exec('regsvr32.exe', '/u /s "' + OldDLL + '"', '', SW_HIDE, ewWaitUntilTerminated, ErrCode);
  end;

  if CurStep = ssPostInstall then begin
    if not NpcapInstalled then begin
      if MsgBox(
        'Npcap is not installed on this computer.' + #13#10 +
        'Npcap is required for live GigaACE frame capture.' + #13#10#13#10 +
        'Download Npcap from npcap.com and install it with' + #13#10 +
        '"WinPcap API-compatible mode" enabled.' + #13#10#13#10 +
        'Open npcap.com in your browser now?',
        mbConfirmation, MB_YESNO) = IDYES then
        ShellExec('open', 'https://npcap.com/#download', '', '', SW_SHOWNORMAL, ewNoWait, ErrCode);
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpWelcome then begin
    WizardForm.WelcomeLabel2.Caption :=
      'This will install GigaACE Virtual Sound Card ' + '{#AppFullVersion}' + ' on your computer.' + #13#10#13#10 +
      'Requirements:' + #13#10 +
      '  - Npcap (npcap.com) with WinPcap-compatible mode — for live capture' + #13#10 +
      '  - Visual C++ 2022 x64 Redistributable — for the application to run' + #13#10 +
      '  - ASIO-capable DAW (Reaper, etc.) — for recording' + #13#10#13#10 +
      'The GigaACE ASIO driver will be registered automatically.';
  end;
end;
