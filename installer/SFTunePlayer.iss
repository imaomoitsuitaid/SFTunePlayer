; Inno Setup script for SF Tune Player (Windows x64)
; Built by GitHub Actions:  iscc /DAppVersion=1.2.3 /DBuildDir=<artefacts\Release> installer\SFTunePlayer.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\SFTunePlayer_artefacts\Release"
#endif

#define AppName "SF Tune Player"

[Setup]
; Keep AppId the same forever: it lets new versions upgrade the old install in place.
AppId={{8F3C2B1E-6A4D-4E7B-9C21-5D0A7E3F9B42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=SFTune
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=..\dist
OutputBaseFilename=SFTunePlayer-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; If a DAW has the plugin loaded, the installer asks to close it (Windows Restart Manager)
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppName}.exe

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "VST3 + Standalone"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application";                          Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone; Flags: unchecked

[InstallDelete]
; remove the previous plug-in bundle completely before copying the new one
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Files]
Source: "{#BuildDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\{#AppName}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\{#AppName}";          Filename: "{app}\{#AppName}.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";    Filename: "{app}\{#AppName}.exe"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone
