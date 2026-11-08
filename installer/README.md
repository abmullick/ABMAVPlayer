# Building the ABM AV Player installer

For end-user operation, see the repository's [USER_GUIDE.md](../USER_GUIDE.md). For receiver discovery and Cast prerequisites, see [CASTING.md](../CASTING.md).

## Prerequisites
- Windows 11 x64
- A successful x64 build at build\ABMAVPlayer.exe
- A matching 64-bit VLC installation containing libvlc.dll, libvlccore.dll and plugins\
- Inno Setup 6 or later

## 1. Prepare the staging folder
From the repository root, run:

    prepare_installer.bat

If VLC is installed in a non-standard location:

    set VLC_DIR=D:\Path\To\VLC
    prepare_installer.bat

This recreates release\ABM AV Player, copies the executable, VLC runtime DLLs, plugins, available VLC data folders and ownership/licence notices. If assets\owner_portrait.bmp exists, the optional founder portrait is copied into the staged assets folder.

## 2. Test the staged application
Run release\ABM AV Player\ABMAVPlayer.exe directly (not through run.bat). Test local audio/video, subtitles, playlists, Help, About/portrait loading and receiver discovery/casting. The app sets VLC plugin/data paths relative to its executable.

## 3. Compile the installer
Run build_installer.bat from the repository root with Inno Setup installed, or open installer\ABMAVPlayer.iss in Inno Setup and compile it.

The output is:

    dist\ABM-AV-Player-Setup.exe

The installer targets C:\Program Files\ABM AV Player, creates a Start Menu shortcut, offers an optional desktop shortcut and registers a Windows uninstaller. It does not delete the user's %APPDATA%\ABMAVPlayer preferences/library on uninstall.

## Distribution and licence review
The application includes proprietary ABM AV Player materials and third-party VLC/LibVLC components. Review the licence for the exact VLC build and every plugin included in the package before distributing it. VLC's plugin folder can contain components under different licences; copying COPYING.txt alone is not proof that all redistribution obligations have been met. This packaging script is a technical staging tool, not a licence audit.


## WebView2 streaming prerequisite

The Streaming Centre uses the Microsoft Edge WebView2 Evergreen Runtime. Current Windows 11 systems commonly have this runtime already installed. If it is missing, install the official Microsoft Edge WebView2 Runtime before launching the Streaming Centre. The runtime is not bundled with ABM AV Player.
