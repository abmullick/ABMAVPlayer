# ABM AV Player

**Product Owner:** Abhisek Basu Mullick  
**Copyright:** © 2026 Abhisek Basu Mullick. All rights reserved.

A native Windows desktop audio/video player written in C, with a custom Direct2D/DirectWrite interface and LibVLC playback engine.

## Download ABM AV Player for Windows

[**Download the latest Windows installer (ABM-AV-Player-Setup.exe)**](https://github.com/abmullick/ABMAVPlayer/releases/latest/download/ABM-AV-Player-Setup.exe)

**Next source version (not yet published):** [ABM AV Player v1.0.2 (upcoming)](https://github.com/abmullick/ABMAVPlayer/releases/tag/v1.0.2) · [All releases](https://github.com/abmullick/ABMAVPlayer/releases)

Download the installer, run it, and follow the setup instructions. The installer is provided through GitHub Releases. See [RELEASES.md](RELEASES.md) for release and update information.

> Public repository visibility does not grant permission to copy, modify, redistribute, sublicense, or commercially exploit this software. See [LICENSE](LICENSE) and [COPYRIGHT.md](COPYRIGHT.md).

## User documentation

- [User Guide](USER_GUIDE.md): Getting started, playback, playlists, audio processing, subtitles, casting, shortcuts and troubleshooting.
- [Google Cast and network receiver guide](CASTING.md): Runtime requirements, receiver discovery and network troubleshooting.
- [Streaming Centre](STREAMING.md): WebView2 setup, account sessions, DRM limitations and troubleshooting.
- [Torrent Stream design](TORRENT_STREAM.md): planned magnet/torrent ingestion, progressive playback, caching, bandwidth controls and implementation phases.
- [Phase 1 / Phase 2 testing checklist](PHASE1_PHASE2_TESTING.md): Manual validation steps for Windows builds.
- [Product hardening test plan](PRODUCT_HARDENING_TESTS.md): WebView2 recovery, runtime checks, memory soak and installer upgrade validation.

## Features

### Streaming Centre — browser services and independent TV casting

Open **Streaming services** from the Command Centre or press **Ctrl+Shift+S**. The Streaming Centre is a separate Microsoft Edge WebView2 window with its own navigation, receiver-selection and casting workflow.

- Six provider launch cards: **YouTube, Netflix, Prime Video, JioHotstar, Sony LIV and ZEE5**. Each opens the provider's official website.
- Sign in directly with your provider. ABM AV Player does not collect, proxy or store provider passwords.
- Persistent WebView2 profile at `%LOCALAPPDATA%\\ABMAVPlayer\\WebView2` can retain supported cookies and sign-in sessions for the current Windows user.
- Browser toolbar: Back, Forward, Reload, Home, Fullscreen, Help, About and Open in browser.
- WebView2 process-failure recovery makes a bounded number of attempts and can restore the current safe HTTP(S) page.
- Dedicated **Find TVs** panel, separate from the main player's Playback Output selector. Discovery combines Google Cast, DLNA/UPnP and DIAL receivers; matching receiver names are consolidated.
- **Check TV apps** performs read-only Google Cast availability checks for YouTube (`233637DE`) and an experimental Prime Video receiver ID (`17608BC8`), and probes DIAL application endpoints for Netflix, Prime Video, JioHotstar, SonyLIV and ZEE5. The selector includes clearly labelled manual DIAL launch attempts for Netflix, SonyLIV and ZEE5 when the TV exposes DIAL but does not provide a readable GET status endpoint; these are attempts, not confirmed installed-app results. The Prime Video ID is community-documented, not an Amazon-published third-party sender API. DIAL results are not a complete Android TV installed-app inventory.
- **Launch app** launches the selected YouTube or experimental Prime Video Google Cast receiver when advertised, or sends a DIAL POST launch request for the selected DIAL app. Manual DIAL entries are explicitly labelled as attempts; the TV can reject unsupported application names. Launching an app does not transfer a browser title or provider account session.
- Select a receiver in the panel, then choose **Cast video**. YouTube handoff uses Google Cast V2 or a discovered DIAL YouTube endpoint. Directly addressable non-protected video URLs may be sent to compatible Cast/DLNA renderers.
- Netflix and Prime Video title casting is not supported from the desktop browser. **Launch app** opens the TV app separately; it does not transfer the desktop title or account session. **Refresh app status** queries the selected Cast receiver session or DIAL application state and reports running/not running (or query failure); it does not report movie playback state. **Stop TV** sends a Google Cast session `STOP` for Cast-launched apps or a DIAL application `DELETE` for DIAL-launched apps. The Cast path checks the running session ID and waits for receiver confirmation. DIAL does not expose playback state or pause/resume/seek controls. Use the TV remote or provider's official mobile app when no provider-supported sender session is available. DRM-protected, encrypted, segmented or non-direct browser streams are not extracted or relayed.

Streaming Centre casting uses its own receiver selection and playback path. It does not replace the main player's local media, current playback or local casting selection. See [STREAMING.md](STREAMING.md) for details and limitations.

### Torrent Stream — development branch

The Windows development branch includes the native libtorrent 2.1.2 engine, on-demand loading, a dedicated Torrent Stream window, magnet and local `.torrent` ingestion, metadata/file listing, task progress and peer/rate status, pause/resume/remove controls, and an initial local LibVLC progressive-playback handoff. Playback waits for an initial verified-piece buffer and reads through an authenticated HTTP Range source bound only to `127.0.0.1`. The torrent window is separate from Streaming Centre and does not change existing TV discovery or casting. This work has **not yet been compiled and runtime-tested on Windows**, and is not included in a published release. No torrent search/catalogue is included. See [TORRENT_STREAM.md](TORRENT_STREAM.md) for remaining work and limitations.

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

### Network playback
- Main-player local output selection and discovery through LibVLC. Compatible Google Cast / Chromecast receivers and other renderer services exposed by the installed VLC build may appear.
- Choose a local-player receiver before playback or switch output while an item is playing; returning to Local playback routes audio/video to the PC.
- Output switching attempts to restore the prior playback position and paused/playing state after the new renderer starts. This is best-effort and automatic local fallback is not guaranteed.
- Streaming Centre has a separate Find TVs panel combining Google Cast, DLNA/UPnP and DIAL discovery, plus a Cast video action for supported YouTube app handoff or directly addressable video URLs. Its selection does not change local-player casting.
- Receiver discovery, transcoding, codec support and playback depend on VLC plugins, receiver capabilities, multicast/network configuration and firewall rules. See [CASTING.md](CASTING.md) and [STREAMING.md](STREAMING.md).

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
- CMake 3.21+, vcpkg, and the manifest-pinned libtorrent 2.1.2 dependency for the Torrent Stream engine.
- 64-bit VLC installation and matching 64-bit LibVLC SDK (LibVLC 3.0 or later for renderer discovery).
- For Google Cast, use a full VLC runtime containing its renderer-discovery and Chromecast plugins. The SDK alone is not a complete runtime.

Downloads: [VLC](https://www.videolan.org/vlc/) · [LibVLC SDK](https://www.videolan.org/vlc/libvlc.html)

All components must use the same architecture (x64).

## Updates and releases

ABM AV Player includes an in-app update checker connected to the public GitHub Releases page.

### For users

- Select **Check for updates** from the application menu.
- The player checks the latest published release, compares its version with the installed version, and displays the release notes when an update is available.
- If you accept, the player downloads the official Windows installer and verifies its SHA-256 digest against the digest returned by GitHub Releases before launching it. A missing or mismatched digest blocks installation.
- The installer is started with the normal Windows elevation prompt. No code-signing certificate is required for SHA-256 verification; Windows SmartScreen may still warn about an unsigned installer.
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
6. Set `VCPKG_ROOT` to your vcpkg checkout and run `vcpkg install` from the repository root to install the pinned libtorrent dependency.
7. Run build.bat from the repository root. The build compiles the separate `abm_torrent_engine.dll` and stages its runtime dependencies.

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


**Refresh app status** queries the TV's current receiver application state and reports whether the selected Cast app is running or not running, or reads the selected DIAL app's running/stopped state. It does not report movie playback state (playing, paused or buffering).
