# Building the ABM AV Player installer

For end-user operation, see the repository's [USER_GUIDE.md](../USER_GUIDE.md). For receiver discovery and Cast prerequisites, see [CASTING.md](../CASTING.md).

## Prerequisites
- Windows 11 x64
- A successful x64 build at build\ABMAVPlayer.exe
- A matching 64-bit VLC installation containing libvlc.dll, libvlccore.dll and plugins\
- Inno Setup 6 or later

## Licensing integration

The licence gate is part of the compiled native application, not an installer-only check. The installer packages the application executable containing the 10-day trial and signed perpetual-key validation. No private signing key is shipped in the installer. Licence and trial state are retained under the Windows user's AppData/registry and are not removed by a normal application upgrade. See [LICENSING.md](../LICENSING.md) for publisher key handling and validation steps.

## 1. Prepare the staging folder
From the repository root, run:

    prepare_installer.bat

If VLC is installed in a non-standard location:

    set VLC_DIR=D:\Path\To\VLC
    prepare_installer.bat

This recreates release\ABM AV Player and validates/copies the executable, required creator and service artwork, VLC runtime DLLs, required Cast plugins, available VLC data folders and ownership/licence notices. The staging script fails if a required runtime DLL, artwork file or Cast plugin is missing.

## 2. Test the staged application
Run release\ABM AV Player\ABMAVPlayer.exe directly (not through run.bat). Test local audio/video, subtitles, playlists, Help, About/portrait loading and receiver discovery/casting. The app sets VLC plugin/data paths relative to its executable.

## Version source

The only editable application version is stored in the repository-root `VERSION` file. `prepare_installer.bat` and `build_installer.bat` generate `build\version.iss` from that file, and the installer script includes the generated definition. Do not change version numbers directly in the Inno Setup script. If you open the Inno Setup script directly without first running either preparation script, run `tools\prepare_version.ps1` so the generated include exists.

## 3. Build and compile the release installer

For a release build, run `build_installer.bat` from the repository root with Inno Setup installed. This is the recommended release path: it rebuilds the application from source, checks that the main app, Streaming Centre, Inno Setup metadata and vcpkg manifest all identify the same release version, refreshes the staging folder, verifies the built and staged executable versions, and only then compiles the installer.

For a development-only packaging test, you can run `prepare_installer.bat` and compile `installer\ABMAVPlayer.iss` manually. A manually compiled installer does not replace the release build/version preflight.

The output is:

    dist\ABM-AV-Player-Setup.exe

The installer targets C:\Program Files\ABM AV Player, creates a Start Menu shortcut, offers an optional desktop shortcut and registers a Windows uninstaller. It also registers ABM AV Player as a candidate for supported media extensions through Windows RegisteredApplications/Capabilities and a media ProgID. It does not force file-type defaults; users must select ABM AV Player in Windows Settings → Apps → Default apps. Explorer launches are passed to the player through its command-line media-path handler. It is configured to close the running application during an upgrade. It does not delete the user's %APPDATA%\ABMAVPlayer preferences/library or the persistent %LOCALAPPDATA%\ABMAVPlayer\WebView2 browser profile on uninstall. Validate this preservation behaviour on an actual upgrade and uninstall test; do not assume it from a successful installer compile.

## Distribution and licence review
The application includes proprietary ABM AV Player materials and third-party VLC/LibVLC components. Review the licence for the exact VLC build and every plugin included in the package before distributing it. VLC's plugin folder can contain components under different licences; copying COPYING.txt alone is not proof that all redistribution obligations have been met. This packaging script is a technical staging tool, not a licence audit.


## WebView2 streaming prerequisite

The Streaming Centre uses the Microsoft Edge WebView2 Evergreen Runtime. Current Windows 11 systems commonly have this runtime already installed. If it is missing, install the official Microsoft Edge WebView2 Runtime before launching the Streaming Centre. The runtime is not bundled with ABM AV Player.


## Product hardening validation

Follow [PRODUCT_HARDENING_TESTS.md](../PRODUCT_HARDENING_TESTS.md) before distributing a release. Test the WebView2 Runtime missing case, browser-process termination and recovery, repeated streaming navigation, a long-running memory soak, clean installation, in-place upgrade and user-data preservation. Source changes and successful staging do not substitute for those Windows tests.
