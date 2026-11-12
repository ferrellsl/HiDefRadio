; HiDef Radio for Windows - installer (Inno Setup 6). Built by package.ps1, which passes
; the version and the folder with the files: ISCC /DAppVersion=1.0.0 /DSourceDir=... HiDefRadio.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\HiDefRadio-Windows-" + AppVersion
#endif

[Setup]
AppId={{6C1B7E2A-4D0F-4B7E-9C51-5E0D2A7F3B91}
AppName=HiDef Radio
AppVersion={#AppVersion}
AppVerName=HiDef Radio {#AppVersion}
AppPublisher=ferrellsl
AppPublisherURL=https://github.com/ferrellsl/HiDefRadio
AppSupportURL=https://github.com/ferrellsl/HiDefRadio/issues
DefaultDirName={autopf}\HiDef Radio
DefaultGroupName=HiDef Radio
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\HiDefRadio.exe
OutputDir=..\dist
OutputBaseFilename=HiDefRadio-Windows-{#AppVersion}-Setup
SetupIconFile=..\res\app.ico
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog

[Files]
Source: "{#SourceDir}\HiDefRadio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\HiDef Radio"; Filename: "{app}\HiDefRadio.exe"
Name: "{group}\Uninstall HiDef Radio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\HiDef Radio"; Filename: "{app}\HiDefRadio.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\HiDefRadio.exe"; Description: "{cm:LaunchProgram,HiDef Radio}"; Flags: nowait postinstall skipifsilent

; Settings, favorites and blocked pictures (%APPDATA%\HiDefRadio) and saved logos and radar
; (%LOCALAPPDATA%\HiDefRadio) are left in place on uninstall, like most Windows programs.
