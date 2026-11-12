#define MyAppName "ABM AV Player"
#define MyAppVersion "1.0.3"
#define MyAppPublisher "Abhisek Basu Mullick"
#define MyAppExeName "ABMAVPlayer.exe"

[Setup]
AppId={{B4D81E1A-2D7C-4F4E-9D0A-AB10A4B1C026}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion=1.0.3.0
VersionInfoProductVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Installer
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) 2026 Abhisek Basu Mullick. All rights reserved.
DefaultDirName={autopf}\ABM AV Player
DefaultGroupName=ABM AV Player
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=ABM-AV-Player-Setup
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\assets\abm_av_player.ico
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
LicenseFile=..\release\ABM AV Player\LICENSE
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "castfirewall"; Description: "Allow Google Cast / network receiver discovery on private networks"; GroupDescription: "Network access:"; Flags: checkedonce

[Files]
Source: "..\release\ABM AV Player\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ABM AV Player"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\ABM AV Player"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ABM AV Player mDNS"" dir=in action=allow protocol=UDP localport=5353 remoteip=LocalSubnet profile=private"; Tasks: castfirewall; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ABM AV Player SSDP"" dir=in action=allow protocol=UDP localport=1900 remoteip=LocalSubnet profile=private"; Tasks: castfirewall; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ABM AV Player"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ABM AV Player mDNS"""; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ABM AV Player SSDP"""; Flags: runhidden waituntilterminated
