#define MyAppName "ABM AV Player"
#include "..\build\version.iss"
#define MyAppPublisher "Abhisek Basu Mullick"
#define MyAppExeName "ABMAVPlayer.exe"

[Setup]
AppId={{B4D81E1A-2D7C-4F4E-9D0A-AB10A4B1C026}
AppName={#MyAppName}
AppVersion={#ABM_VERSION}
VersionInfoVersion={#ABM_VERSION}.0
VersionInfoProductVersion={#ABM_VERSION}
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
; Use the same ABM application artwork for the setup executable and installer UI.
SetupIconFile=..\assets\abm_av_player.ico
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
LicenseFile=..\release\ABM AV Player\LICENSE
Uninstallable=yes
ChangesAssociations=yes

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

[Registry]
; Register a single ProgID for the supported media formats. Windows 10/11 still
; requires the user to choose ABM AV Player in Default apps; the installer
; deliberately does not overwrite existing per-extension defaults.
Root: HKLM; Subkey: "Software\Classes\ABMAVPlayer.MediaFile"; ValueType: string; ValueName: ""; ValueData: "ABM AV Player Media File"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\ABMAVPlayer.MediaFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"",0"
Root: HKLM; Subkey: "Software\Classes\ABMAVPlayer.MediaFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKLM; Subkey: "Software\Classes\ABMAVPlayer.MediaFile\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "ABM AV Player"
Root: HKLM; Subkey: "Software\Classes\ABMAVPlayer.MediaFile\Application"; ValueType: string; ValueName: "ApplicationCompany"; ValueData: "{#MyAppPublisher}"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "ABM AV Player"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Play audio and video files with ABM AV Player"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp4"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mkv"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avi"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mov"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wmv"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4v"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webm"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpg"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpeg"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ts"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m2ts"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mts"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flv"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogv"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".3gp"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".vob"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp3"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wav"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flac"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4a"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".aac"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogg"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".opus"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wma"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".aiff"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ape"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".alac"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ac3"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".dts"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".amr"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mid"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\ABMAssets\ABM AV Player\Capabilities\FileAssociations"; ValueType: string; ValueName: ".midi"; ValueData: "ABMAVPlayer.MediaFile"
Root: HKLM; Subkey: "Software\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.avi\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mov\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.wmv\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.m4v\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mpg\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mpeg\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.ts\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.m2ts\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mts\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.flv\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.ogv\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.3gp\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.vob\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.m4a\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.aac\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.opus\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.wma\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.aiff\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.ape\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.alac\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.ac3\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.dts\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.amr\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.mid\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.midi\OpenWithProgids"; ValueType: string; ValueName: "ABMAVPlayer.MediaFile"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "ABM AV Player"; ValueData: "Software\ABMAssets\ABM AV Player\Capabilities"; Flags: uninsdeletevalue

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ABM AV Player mDNS"" dir=in action=allow protocol=UDP localport=5353 remoteip=LocalSubnet profile=private"; Tasks: castfirewall; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""ABM AV Player SSDP"" dir=in action=allow protocol=UDP localport=1900 remoteip=LocalSubnet profile=private"; Tasks: castfirewall; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ABM AV Player"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; Remove the universal activation on explicit uninstall so reinstall requires key entry again.
Type: files; Name: "{userappdata}\ABMAVPlayer\license.key"

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ABM AV Player mDNS"""; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""ABM AV Player SSDP"""; Flags: runhidden waituntilterminated
