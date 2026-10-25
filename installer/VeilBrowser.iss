#define MyAppName "Veil Browser"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "Cliffamr"
#define MyAppExeName "VeilBrowser.exe"

[Setup]
AppId={{BC312B0C-E5BE-4CA4-9897-ACD06925AD38}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Veil Browser
DefaultGroupName=Veil Browser
DisableProgramGroupPage=yes
OutputDir=..\installer-output
OutputBaseFilename=Veil-Browser_0.2.0_x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE.txt

[Files]
Source: "..\dist\VeilBrowser.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\docs\manual-capture-checklist.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Veil Browser"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Veil Browser"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Veil Browser"; Flags: nowait postinstall skipifsilent
