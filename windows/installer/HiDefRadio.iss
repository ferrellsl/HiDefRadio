; OpenDial for Windows (formerly HiDef Radio) - installer (Inno Setup 6). Built by package.ps1, which passes
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
  #define SourceDir "..\dist\OpenDial-Windows-" + AppVersion + Suffix
#endif

[Setup]
AppId={{6C1B7E2A-4D0F-4B7E-9C51-5E0D2A7F3B91}
AppName=OpenDial
AppVersion={#AppVersion}
AppVerName=OpenDial {#AppVersion}
AppPublisher=ferrellsl
AppPublisherURL=https://github.com/ferrellsl/OpenDial
AppSupportURL=https://github.com/ferrellsl/OpenDial/issues
DefaultDirName={autopf}\OpenDial
DefaultGroupName=OpenDial
; the same AppId as HiDef Radio, so it updates it in place (keeping its folder); the Start menu gets the new name
UsePreviousGroup=no
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\OpenDial.exe
OutputDir=..\dist
OutputBaseFilename=OpenDial-Windows-{#AppVersion}{#Suffix}-Setup
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
Source: "{#SourceDir}\OpenDial.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; what HiDef Radio (before the rename) left: its exe and its shortcuts
Type: files; Name: "{app}\HiDefRadio.exe"
Type: filesandordirs; Name: "{autoprograms}\HiDef Radio"
Type: files; Name: "{autodesktop}\HiDef Radio.lnk"

[Icons]
Name: "{group}\OpenDial"; Filename: "{app}\OpenDial.exe"
Name: "{group}\Uninstall OpenDial"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OpenDial"; Filename: "{app}\OpenDial.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\OpenDial.exe"; Description: "{cm:LaunchProgram,OpenDial}"; Flags: nowait postinstall skipifsilent

; Settings, favorites and blocked pictures (%APPDATA%\HiDefRadio) and saved logos and radar
; (%LOCALAPPDATA%\HiDefRadio) are left in place on uninstall, like most Windows programs.
