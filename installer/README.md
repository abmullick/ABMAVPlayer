# Building the ABM AV Player installer

## Prerequisites
- Windows 11 x64
- A successful x64 build at `build\ABMAVPlayer.exe`
- A matching 64-bit VLC installation containing `libvlc.dll`, `libvlccore.dll` and `plugins\`
- Inno Setup 6

## 1. Prepare the staging folder
From the repository root, run:

```bat
prepare_installer.bat
```

If VLC is installed in a non-standard location:

```bat
set VLC_DIR=D:\Path\To\VLC
prepare_installer.bat
```

This recreates `release\ABM AV Player`, copies the executable, VLC runtime DLLs, plugins, available VLC data folders and ownership/licence notices.

## 2. Test the staged application
Run `release\ABM AV Player\ABMAVPlayer.exe` directly (not through `run.bat`). Test local audio/video, subtitles, playlists and receiver discovery/casting. The app sets VLC plugin/data paths relative to its executable.

## 3. Compile the installer
Open `installer\ABMAVPlayer.iss` in Inno Setup 6 and click **Compile**. The output is:

`dist\ABM-AV-Player-Setup.exe`

The installer targets `C:\Program Files\ABM AV Player`, creates a Start Menu shortcut, offers an optional desktop shortcut, and registers a Windows uninstaller. It does not delete the user's `%APPDATA%\ABMAVPlayer` preferences/library on uninstall.

## Distribution and licence review
The application includes proprietary ABM AV Player materials and third-party VLC/LibVLC components. Review the licence for the exact VLC build and every plugin included in the package before distributing it. VLC's plugin folder can contain components under different licences; copying `COPYING.txt` alone is not proof that all redistribution obligations have been met. This packaging script is a technical staging tool, not a licence audit.
