# ABM AV Player

**Product Owner:** Abhisek Basu Mullick  
**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.

A native Windows desktop audio/video player written in C, with a custom Direct2D/DirectWrite interface and LibVLC playback engine.

## Download ABM AV Player for Windows

<**[Download the latest Windows installer (ABM-AV-Player-Setup.exe)](https://github.com/abmullick/ABMAVPlayer/releases/latest/download/ABM-AV-Player-Setup.exe)**>

**Current stable release:** [ABM AV Player v1.0.0](https://github.com/abmullick/ABMAVPlayer/releases/tag/v1.0.0) · [All releases](https://github.com/abmullick/ABMAVPlayer/releases)

Download the installer, run it, and follow the setup instructions. The installer is provided through GitHub Releases. See [RELEASES.md](RELEASES.md) for release and update information.

> Public repository visibility does not grant permission to copy, modify, redistribute, sublicense, or commercially exploit this software. See [LICENSE](LICENSE) and [COPYRIGHT.md](COPYRIGHT.md).

## User documentation

- [User Guide](USER_GUIDE.md): Getting started, playback, playlists, audio processing, subtitles, casting, shortcuts and troubleshooting.
- [Google Cast and network receiver guide](CASTING.md): Runtime requirements, receiver discovery and network troubleshooting.
- [Streaming Centre](STREAMING.md): WebView2 setup, account sessions, DRM limitations and troubleshooting.
- [Phase 1 / Phase 2 testing checklist](PHASE1_PHASE2_TESTING.md): Manual validation steps for Windows builds.
- [Product hardening test plan](PRODUCT_HARDENING_TESTS.md): WebView2 recovery, runtime checks, memory soak and installer upgrade validation.

## Features

### Streaming Centre — online video services

ABM AV Player includes a built-in, separate browser window for popular streaming services. Open **Streaming services** from the Command Centre or press **Ctrl+Shift+S**.

- Launch the official websites for **YouTube, Netflix, Prime Video, JioHotstar, Sony LIV and ZEE5** inside Microsoft Edge WebView2.
- Sign in directly with your own provider account. The player does not collect, proxy or store your provider passwords.
- A persistent, per-Windows-user browser profile can retain supported website sign-in sessions between launches.
- Use the Streaming Centre's browser controls to navigate provider websites; use **Open in browser** if a service does not work in the embedded window.
- Streaming compatibility is controlled by each provider. DRM, browser restrictions, device certification, region and runtime support can prevent protected playback; a subscription does not guarantee embedded playback.

This is web-based streaming through the providers' official sites, separate from local LibVLC playback and Google Cast output. See [STREAMING.md](STREAMING.md) for setup, privacy and troubleshooting.

### Media and playback
- Open local audio and video files; drag files from File Explorer into the player.
- Playlist operations: add, play, remove, reorder, clear, load and save M3U playlists.
- Playback controls: play/pause, stop, previous/next, seek and volume.
- Recent-media list, shuffle and repeat-current-track modes.
- Playback speed presets from 0.5x to 2.0x.
- Fullscreen playback and keyboard controls.
- Basic media information (title, artist, album and duration), when available.

### Audio, video and subtitles
- Common formats include MP3, WAV, FLAC, AAC, M4A, OGG, MP4, MKV, AVI, MOV, WMV and WebM; actual support depends on VLC codecs/plugins.
- Audio-track and subtitle-track selection; disable subtitles or load an external subtitle file.
- LibVLC equalizer presets and ABM 10-band graphic EQ profiles: Flat, Bass boost, Vocal clarity and Treble boost.
- Optional ReplayGain track normalisation and dynamic-range compressor for newly opened media.
- Hardware decoding uses LibVLC automatic selection by default. ABM_VLC_HW=none forces software decoding; ABM_VLC_HW=d3d11va requests D3D11VA when supported.

### Streaming services
- Separate WebView2-powered Streaming Centre for YouTube, Netflix, Prime Video, JioHotstar, Sony LIV and ZEE5.
- Users navigate to each provider's official website and sign in with their own accounts; ABM AV Player does not collect or proxy credentials.
- WebView2 browser profile is persisted under %LOCALAPPDATA%\\ABMAVPlayer\\WebView2 so provider sign-in sessions can survive application restarts.
- Protected playback is subject to each provider's embedded-browser and DRM policies. If a provider blocks WebView2 or its DRM requirements are not met, use the provider's official browser/app instead. A subscription alone does not guarantee embedded playback.

### Network playback
- Receiver discovery and renderer selection through LibVLC, including compatible Google Cast / Chromecast devices and other renderer services exposed by the installed VLC build.
- Choose a receiver before playback or switch output while an item is playing.
- Output switching attempts to restore the prior playback position and paused/playing state after the selected renderer starts.
- Casting depends on VLC's renderer-discovery, Chromecast, codec and encoder plugins, receiver compatibility and the local network. See CASTING.md.

## Interface and architecture

- Application and windowing: C/C++ + Win32 API.
- Interface: Direct2D antialiased rendering and DirectWrite typography.
- Media decoding, demuxing, audio/video output and renderer integration: LibVLC.
- Streaming website rendering and provider sign-in: Microsoft Edge WebView2 in a separate top-level window.
- Video is displayed in an embedded Win32 child window.
- Audio output uses LibVLC's Windows audio backend.

LibVLC handles media decoding and device output. ABM AV Player owns the desktop interface and playback controls; it is not a kernel audio/video driver.

## Requirements

- Windows 11 x64.
- Visual Studio / Build Tools with Desktop development with C++ and Windows 10/11 SDK.
- Microsoft Edge WebView2 Evergreen Runtime (required for the Streaming Centre; the app checks for it before browser startup and reports an installation instruction if missing).
- Microsoft.Web.WebView2 NuGet SDK package for compiling the streaming module.
- 64-bit VLC installation and matching 64-bit LibVLC SDK (LibVLC 3.0 or later for renderer discovery).
- For Google Cast, use a full VLC runtime containing its renderer-discovery and Chromecast plugins. The SDK alone is not a complete runtime.

Downloads: [VLC](https://www.videolan.org/vlc/) · [LibVLC SDK](https://www.videolan.org/vlc/libvlc.html)

All components must use the same architecture (x64).

## Updates and releases

ABM AV Player includes an in-app update checker connected to the public GitHub Releases page.

### For users

- Select **Check for updates** from the application menu.
- The player checks the latest published release, compares its version with the installed version, and displays the release notes when an update is available.
- If you accept, the player downloads the official Windows installer and starts the normal Windows installation process. You will be asked to approve installation through the Windows elevation prompt.
- **No GitHub account, GitHub login, access token, or command-line tools are required.** Update checks and downloads are anonymous because this repository is public.
- You can also download the installer manually from [GitHub Releases](https://github.com/abmullick/ABMAVPlayer/releases).

### For maintainers

- A release must be published on GitHub and include an installer asset named exactly `ABM-AV-Player-Setup.exe`; the updater cannot find an installer attached under a different name.
- Before building a release, keep `APP_VERSION` in `src/player.c` and `MyAppVersion` in `installer/ABMAVPlayer.iss` identical to the release version.
- Build and test the application and installer on Windows before publishing.
- The `publish_release.bat` script uses GitHub CLI. **Only the maintainer publishing the release** needs GitHub CLI installed and authenticated with `gh auth login`. This authentication is not used by the application or required by end users.
- See [RELEASES.md](RELEASES.md) for the complete publishing procedure and release requirements.

Updates are installed over the existing ABM AV Player installation. User preferences and other application data are stored separately under AppData.

## Build

1. Install 64-bit VLC.
2. Extract the matching 64-bit LibVLC SDK, for example to C:\\vlc-sdk.
3. Install the WebView2 SDK package from the repository root:
   `nuget install Microsoft.Web.WebView2 -OutputDirectory packages -ExcludeVersion`
4. Open x64 Native Tools Command Prompt for Visual Studio.
5. Set SDK paths if they are not in the default locations:
   `set VLC_SDK=C:\\vlc-sdk`
   `set WEBVIEW2_SDK=%CD%\\packages\\Microsoft.Web.WebView2`
6. Run build.bat from the repository root.

The executable is written to build\\ABMAVPlayer.exe.

## Run

Run run.bat. It defaults to the standard VLC installation directory: C:\\Program Files\\VideoLAN\\VLC.

If VLC is installed elsewhere, set VLC_DIR before running.

## Build the installer

See installer/README.md. The normal flow is:

1. Build the application.
2. Run prepare_installer.bat to stage the executable and VLC runtime.
3. Test the staged application.
4. Run build_installer.bat with Inno Setup installed.

The installer output is dist\\ABM-AV-Player-Setup.exe.

## Data and preferences

User preferences and recent/playlist state are stored under %APPDATA%\\ABMAVPlayer. Audio preferences are stored separately in audio.ini. Uninstalling the application is intended to preserve this user data.

## Known limitations

- Playlist capacity is limited to 256 entries.
- There is no database-backed, automatically indexed media library.
- There is no exclusive-mode / bit-perfect audio path or dedicated audio-device selector.
- The ABM EQ profiles are fixed-band graphic EQ curves, not a parametric EQ with adjustable centre frequency/Q.
- ReplayGain requires usable track metadata. The compressor is a dynamic-range effect, not a guaranteed true-peak brickwall limiter; plugin availability depends on VLC.
- The visualizer captures the Windows default render mix, so audio from other applications may appear. Process-specific loopback capture is not implemented.
- Codec support, hardware decoding, network receiver discovery and transcoding depend on the installed VLC build, plugins, operating system, drivers and receiver.
- Cast output switching and recovery are best-effort. If remote playback fails, the current implementation reports the failure; do not assume automatic local fallback.
- No Windows build/runtime validation is implied by source changes alone. Test on the target Windows/VLC configuration.

## Google Cast / Android TV

A TV must expose a compatible Google Cast / Chromecast receiver service. Android TV branding alone does not guarantee that it does. The PC and TV should be on the same non-guest LAN, and multicast DNS (mDNS) must be allowed. Windows Defender Firewall should allow ABM AV Player / VLC on Private networks. VPNs, guest Wi-Fi, router client isolation and multicast filtering can prevent discovery.

See CASTING.md for the full procedure and troubleshooting notes.
