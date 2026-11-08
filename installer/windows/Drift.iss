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
; Dafin uses its own AppIds so it never upgrades or removes an installed Drift.
; Never change either AppId: it is what lets an installer upgrade an existing install
; in place instead of leaving two copies behind. (The app was called Cadra before; these are
; the same AppIds, so installing Dafin replaces a Cadra install.)
#ifdef Arm64
  #define Arch "arm64"
  #define MyArchSpec "arm64"
#else
  #define Arch "x64"
  #define MyArchSpec "x64compatible"
#endif

#ifdef Nightly
  #define MyAppName "Dafin Nightly"
  #define MyAppId "FAEDEE19-55F3-4445-999A-19E5511A7F75"
  #define MyOutputBase "Dafin-Setup-Nightly-" + Arch
#else
  #define MyAppName "Dafin"
  #define MyAppId "873FEB4F-1F9C-446C-B88B-F39A64823BAA"
  #define MyOutputBase "Dafin-Setup-" + Arch
#endif
#define MyAppPublisher "Dafin (based on Drift by CutWire Studios)"
#define MyAppExeName "dafin.exe"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL=https://github.com/laur23212/Dafin/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
; Not the previous install's Start menu folder: that one was named after Cadra.
UsePreviousGroup=no
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

; Shortcuts left by an install from when the app was called Cadra, and the executable from
; before it was renamed dafin.exe ([Files] copies over the folder, it never removes anything).
[InstallDelete]
Type: files; Name: "{app}\drift.exe"
#ifdef Nightly
Type: filesandordirs; Name: "{autoprograms}\Cadra Nightly"
Type: files; Name: "{autodesktop}\Cadra Nightly.lnk"
#else
Type: filesandordirs; Name: "{autoprograms}\Cadra"
Type: files; Name: "{autodesktop}\Cadra.lnk"
#endif

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

#ifndef Nightly
; Deliberately stable-only. A nightly registering the same ProgID would have its uninstaller
; delete the association out from under the stable install (uninsdeletekey), and two channels
; fighting over which opens a .drift file helps nobody.
[Registry]
; The file type registered under the app's earlier name.
Root: HKCR; Subkey: "Cadra.Project"; Flags: deletekey
Root: HKCR; Subkey: ".drift"; ValueType: string; ValueName: ""; ValueData: "Dafin.Project"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "Dafin.Project"; ValueType: string; ValueName: ""; ValueData: "Dafin Project"; Flags: uninsdeletekey
Root: HKCR; Subkey: "Dafin.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCR; Subkey: "Dafin.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
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
