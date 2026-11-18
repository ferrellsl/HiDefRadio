; HiDef Radio for Windows - installer (Inno Setup 6). Built by package.ps1, which passes
; the version, the folder with the files and x64 or x86: ISCC /DAppVersion=1.0.0 /DSourceDir=... /DArch=x64 HiDefRadio.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#if Arch == "x86"
  #define Suffix "-x86"
#else
  #define Suffix ""
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\HiDefRadio-Windows-" + AppVersion + Suffix
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
OutputBaseFilename=HiDefRadio-Windows-{#AppVersion}{#Suffix}-Setup
SetupIconFile=..\res\app.ico
Compression=lzma2/max
SolidCompression=yes
#if Arch == "x64"
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif
; (the 32-bit installer runs on any Windows 10/11 and installs into Program Files (x86) on 64-bit Windows)
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
