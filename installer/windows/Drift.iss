#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

#ifndef MyAppSource
  #define MyAppSource "dist\\bin"
#endif

;    /DNightly  builds the nightly channel installer: its own AppId, name and install
;               directory, so it sits beside a stable install instead of upgrading it.
;
;    /DArm64    builds the arm64 installer (default x64): only the allowed and 64-bit-mode
;               architecture and the output name change.
;
; Cadra uses its own AppIds so it never upgrades or removes an installed Drift.
; Never change either AppId: it is what lets an installer upgrade an existing install
; in place instead of leaving two copies behind.
#ifdef Arm64
  #define Arch "arm64"
  #define MyArchSpec "arm64"
#else
  #define Arch "x64"
  #define MyArchSpec "x64compatible"
#endif

#ifdef Nightly
  #define MyAppName "Cadra Nightly"
  #define MyAppId "FAEDEE19-55F3-4445-999A-19E5511A7F75"
  #define MyOutputBase "Cadra-Setup-Nightly-" + Arch
#else
  #define MyAppName "Cadra"
  #define MyAppId "873FEB4F-1F9C-446C-B88B-F39A64823BAA"
  #define MyOutputBase "Cadra-Setup-" + Arch
#endif
#define MyAppPublisher "Cadra (based on Drift by CutWire Studios)"
#define MyAppExeName "drift.exe"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL=https://github.com/laur23212/Cinemora/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
ArchitecturesAllowed={#MyArchSpec}
ArchitecturesInstallIn64BitMode={#MyArchSpec}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
; Path is relative to this script. Without these two, setup runs under the stock
; Inno icon and the Apps & Features entry falls back to a generic one.
SetupIconFile=..\..\resources\windows\drift.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
#ifndef Nightly
ChangesAssociations=yes
#endif
OutputDir=output
OutputBaseFilename={#MyOutputBase}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Recursive: alongside the exe and its Qt runtime this carries the bundled
; effects\, transitions\, effect-templates\ and audio-effects\ package
; directories, which the app resolves relative to the executable.
Source: "{#MyAppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

#ifndef Nightly
; Deliberately stable-only. A nightly registering the same ProgID would have its uninstaller
; delete the association out from under the stable install (uninsdeletekey), and two channels
; fighting over which opens a .drift file helps nobody.
[Registry]
Root: HKCR; Subkey: ".drift"; ValueType: string; ValueName: ""; ValueData: "Cadra.Project"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "Cadra.Project"; ValueType: string; ValueName: ""; ValueData: "Cadra Project"; Flags: uninsdeletekey
Root: HKCR; Subkey: "Cadra.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCR; Subkey: "Cadra.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
#endif

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
; Silent in-app updates pass /DRIFTUPDATE=1. skipifsilent would otherwise leave Drift closed.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: LaunchAfterSilentUpdate

[Code]
function LaunchAfterSilentUpdate: Boolean;
begin
  Result := ExpandConstant('{param:DRIFTUPDATE|}') = '1';
end;
