# ABM AV Player — Developer Guide

This guide documents the current implementation and maintenance boundaries in the repository. It distinguishes shipped behaviour from capabilities that remain dependent on VLC, Windows, WebView2, network conditions or receiver support.

## Architecture at a glance

ABM AV Player is a native Windows desktop application. The primary application is written in C and uses Win32 for windowing and input, the ABM Direct2D/DirectWrite UI layer for custom-rendered surfaces, and LibVLC for local media playback.

The media paths have separate ownership:

1. **Local playback** — the main LibVLC media player owns ordinary local/network media playback and local renderer selection.
2. **Streaming Centre** — a WebView2-based browser surface for provider websites and browser playback. Its receiver discovery and selection state is intentionally independent of the main local-player renderer state. Do not reuse the local renderer arrays or selected-renderer pointer.
3. **Torrent Stream** — a separate torrent engine DLL owns torrent tasks and download state. Progressive playback uses a verified-piece HTTP source; torrent TV delivery uses its own temporary private-LAN path. Do not stop or replace local playback when operating this path.
4. **Audio facilities** — equalizer, audio processing and visualizer features are additional components with their own runtime/dependency constraints.

These boundaries are product requirements, not incidental implementation details. Changes to one path must not silently alter another path's lifecycle.

## Main source responsibilities

The repository contains the native application under `src/`, Streaming Centre host/browser integration, torrent engine sources and dependency/build support, installer scripts and root-level documentation.

Important responsibilities in `src/player.c` include:

- Main Win32 window procedure and application lifecycle.
- LibVLC initialisation, media creation and playback option application.
- Playlist and recent-media state.
- Main Settings and Advanced Settings windows, custom painting and hit testing.
- Independent local and Streaming Centre renderer registries.
- Advanced settings persistence and helpers that attach options to new media.
- Diagnostic report generation and the read-only preview.
- Fullscreen transitions and video child-window placement.

Because much of the interface is custom-painted, adding a setting normally requires both a rendering change and a matching input/hit-test change. Keep hit-test coordinates aligned with the drawn control. Check the fixed footer and native child-window z-order; do not allow a footer or Close button to overlap content.

## Advanced Settings implementation map

| Page | Current implemented behaviour |
|---|---|
| Overview | Read-only live summary of playback, playlist, engine preferences, caching, performance, subtitles and selected system/process metrics. |
| Video & Decoding | Saves Automatic / software / D3D11VA preference; restart required. |
| Audio Engine | Saves Automatic / WASAPI / DirectSound preference; restart required. |
| Video Output | Aspect ratio Original / 16:9 / 4:3 / 21:9; applies through LibVLC and persists. |
| Subtitles | Delay, relative font size, colour, bold, outline and margin preferences. |
| Streaming & Network | Network/file/live cache targets; passed to newly opened media. Torrent quota and bandwidth are summarised but edited in main Settings. |
| Performance | Decoder thread count, drop-late-frames and skip-frames options; attached to newly opened media. |
| Controls & Behaviour | Exit confirmation, clear-playlist confirmation and double-click fullscreen preferences; wired to main-window behaviour. |
| Diagnostics | Live system/process/playback metrics, local timestamped TXT report, read-only preview and data-folder access. |

Advanced preferences are persisted in `%APPDATA%\\ABMAVPlayer\\settings.ini`, grouped into INI sections such as `[Video]`, `[Audio]`, `[Subtitles]`, `[Streaming]`, `[Performance]` and `[Controls]`. Audio processing and theme selection also use `audio.ini` and `theme.ini`. Do not create a second source of truth for an existing preference.

### Startup settings vs per-media options

- Hardware-decoding and audio-output-module selections are read at startup and require a restart.
- Aspect ratio is applied to the current LibVLC player and persisted.
- Subtitle appearance, caching and performance options are added to media objects as media are opened; an already-playing media object is not guaranteed to be retroactively reconfigured.
- Subtitle timing also updates the current player when possible.
- Controls preferences are loaded into application state and consumed by their matching interactions.
- Diagnostics are observational. Report generation writes locally and does not upload.

See [SETTINGS.md](SETTINGS.md) for the user-facing values and semantics.

## Build environment

The maintainer workflow targets Windows x64 with Visual Studio C/C++ Build Tools, a matching 64-bit LibVLC runtime/SDK, the WebView2 SDK and the pinned torrent dependencies.

From the repository root, in an x64 Native Tools Command Prompt:

```bat
set VLC_SDK=C:\\vlc-sdk
nuget install Microsoft.Web.WebView2 -OutputDirectory packages -ExcludeVersion
set WEBVIEW2_SDK=%CD%\\packages\\Microsoft.Web.WebView2
build.bat
```

Adjust SDK paths for the local machine. The build scripts bootstrap/build the torrent engine and its pinned dependencies. See [VCPKG_SETUP.md](VCPKG_SETUP.md) for dependency-cache preparation and [installer/README.md](installer/README.md) for packaging.

The main executable is produced under `build\\ABMAVPlayer.exe`. Use `run.bat` for the repository runtime launch setup. The installer is staged under `dist\\ABM-AV-Player-Setup.exe` after running the installer preparation and packaging scripts.

## Engineering workflow and guardrails

1. Fetch and inspect current `main` before editing. Keep changes focused and preserve unrelated work.
2. For this repository workflow, commit directly to `main`; do not create feature branches.
3. For UI work, inspect painting, layout and matching input/hit-test logic together. Validate footer clearance, supported window sizes, scaling and child HWND z-order.
4. For LibVLC options, verify whether an option belongs to instance startup, media creation or runtime player state. Do not assume an option can be applied retroactively.
5. Preserve independent player and renderer lifecycles for local playback, Streaming Centre and Torrent Stream.
6. Treat magnet links, URLs, receiver responses and provider pages as untrusted input. Validate inputs and keep temporary listeners scoped to their intended loopback/private-LAN use.
7. Do not add telemetry or automatic diagnostic uploads without an explicit product requirement and clear user consent.
8. Update README, USER_GUIDE, SETTINGS and this guide when a user-visible setting, workflow, dependency or architectural boundary changes.
9. Build with the target Windows toolchain and exercise the affected runtime path. A source commit alone is not evidence of a successful build.

## Verification checklist

- [ ] x64 build completes without new errors.
- [ ] Application launches with the intended LibVLC runtime and plugins.
- [ ] Local file and network playback still work.
- [ ] Settings persist after restart and activate at the documented time.
- [ ] Streaming Centre remains isolated from local-player casting state.
- [ ] Torrent task, progressive playback, permanent download and TV delivery remain independent of local playback.
- [ ] Advanced Settings navigation, hit tests, footer and Close button work at supported window sizes.
- [ ] Diagnostic report generates, appears in the preview and opens from the data folder.
- [ ] Installer stages required runtimes and launches on a clean test machine.

## Documentation map

- [README.md](README.md) — product overview, features, requirements and quick build.
- [USER_GUIDE.md](USER_GUIDE.md) — end-user workflows.
- [SETTINGS.md](SETTINGS.md) — complete settings reference.
- [CASTING.md](CASTING.md) — receiver discovery and casting.
- [STREAMING.md](STREAMING.md) — Streaming Centre browser and TV handoff.
- [TORRENT_STREAM.md](TORRENT_STREAM.md) — torrent design and implementation notes.
- [RELEASES.md](RELEASES.md) — release asset and updater contract.
- [UI_QA_CHECKLIST.md](UI_QA_CHECKLIST.md) — UI validation matrix.
