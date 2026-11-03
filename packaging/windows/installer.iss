; Inno Setup script: builds dist\SANDWAVE-<version>-Windows-Setup.exe
; Run from the repository root after building:  iscc /DAppVersion=1.0.5 packaging\windows\installer.iss

#ifndef AppVersion
  #define AppVersion "1.0.5"
#endif

[Setup]
; keep this AppId the same in every release so updates replace the old install
AppId={{9D135BD5-FE39-4159-89AF-9E1989B3E014}
AppName=SANDWAVE
AppVersion={#AppVersion}
AppVerName=SANDWAVE {#AppVersion}
AppPublisher=Uranium Music
AppCopyright=Uranium Music
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Uranium Music
VersionInfoDescription=SANDWAVE installer
; the uninstaller lives here; the plugin itself goes to the standard VST3 folder (see [Files])
DefaultDirName={commonpf64}\Uranium Music\SANDWAVE
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=SANDWAVE-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=SANDWAVE (Uranium Music)
InfoBeforeFile=ReadMe.txt

[Files]
Source: "..\..\build\Sandwave_artefacts\Release\VST3\SANDWAVE.vst3\*"; DestDir: "{commoncf64}\VST3\SANDWAVE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SANDWAVE.vst3"
