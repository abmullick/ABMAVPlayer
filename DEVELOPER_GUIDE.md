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
- Continue Watching checkpoints in `%APPDATA%\\ABMAVPlayer\\resume.tsv`, written asynchronously from an immutable UI-thread snapshot; completed items are removed from the resume list.
- Missing media is retained in the playlist and favourites instead of being pruned. Relink updates the active library, favourites, recent history, resume records and saved M3U collections.

### Continue Watching and media recovery

Resume state is maintained separately from the playlist M3U files in `%APPDATA%\\ABMAVPlayer\\resume.tsv`. Each entry is keyed by the stored media path and contains the last captured position and duration. The UI thread samples the active local LibVLC player and creates an immutable snapshot; a worker thread writes that snapshot to a temporary file and replaces the checkpoint file. The 33 ms playback/UI timer does not perform resume-file I/O. Shutdown waits for the final snapshot write so the last captured state can be flushed.

Checkpoints are captured periodically and around pause, stop, media changes and shutdown. Very early positions are ignored; entries at approximately 95% completion are removed as finished. The row context menu exposes explicit Continue from checkpoint and Start from beginning actions. Start-from-beginning is a one-play override: it bypasses the saved seek and clears the old checkpoint only after the new media successfully starts, so a failed start does not destroy recovery state. Continue Watching membership is cached per playlist row so painting and scrolling do not repeatedly search the resume-entry collection. The Library selector cycles All tracks, Favourites and Continue Watching; the row metadata uses the cached progress state.

Missing-media detection checks stored paths and updates per-row flags. Missing entries are deliberately preserved in the playlist, favourites and recent history; do not reintroduce destructive pruning as part of a cleanup pass. The row context action opens a native file picker. A successful relink replaces matching path references in the active playlist, favourites, recent history, resume records and saved `.m3u` collections under the user's collections directory. It does not move or copy media files and does not attempt recursive folder matching or metadata-based identification. The same row context menu offers Open source folder, implemented through Windows Explorer; it selects an existing file or opens the stored parent directory when the file is missing and that directory remains accessible.

Keep path existence checks out of the paint/scroll hot path. Refresh availability at explicit library/drawer or mutation points. Preserve the ownership boundaries: resume and relink operations must not stop, replace or mutate the Streaming Centre browser player, local receiver discovery state, or Torrent Stream task/transport lifecycle.
- Main Settings and Advanced Settings windows, custom painting and hit testing.
- Independent local and Streaming Centre renderer registries.
- Advanced settings persistence and helpers that attach options to new media.
- Diagnostic report generation and the read-only preview.
- Fullscreen transitions and video child-window placement.

Because much of the interface is custom-painted, adding a setting normally requires both a rendering change and a matching input/hit-test change. Keep hit-test coordinates aligned with the drawn control. Check the fixed footer and native child-window z-order; do not allow a footer or Close button to overlap content.

## Home Theatre

The optional Home Theatre window is implemented in `src/tmdb_home_theatre.cpp` with its C-compatible entry point in `src/tmdb_home_theatre.h`. The main player passes an immutable snapshot of the active playlist plus saved favourites (via `EnsureFavoritesInPlaylist`) and the existing `PlayIndex` callback. The window owns its Direct2D surface and Win32 controls; it does not replace or share state with the Streaming Centre browser player or Torrent Stream lifecycle.

TMDB search, detail, multi-search library identification and season-detail requests run on worker threads. Workers receive copied query/path/title data and return heap-owned result messages; all HWND and Direct2D updates remain on the Home Theatre window thread. The metadata client in `src/tmdb_metadata.cpp` uses the encrypted per-user Read Access Token, WinHTTP and read-only TMDB endpoints. TV details use the existing append-to-response support for credits, images and recommendations; `ABM_TMDB_GetSeasonDetails` retrieves a selected season's episode list.

**Identify library** scans video paths from the active playlist/favourites snapshot. Filename cleanup strips common release-quality tags before a TMDB multi-search request. The scan spaces requests to avoid an aggressive request rate and presents at most 100 identified matches. It is an assisted metadata suggestion, not authoritative file identification. Search and scan failures are isolated from normal playback.

Manual local-file overrides are stored in `%LOCALAPPDATA%\\ABMAVPlayer\\HomeTheatre\\matches.tsv`, keyed by TMDB ID and media type. Poster and backdrop files are cached in the same directory. A selected TV season is loaded asynchronously; episode rows retain season/episode numbers and synopsis, and the local matcher checks common SxxExx / NxNN filename patterns. Similar-title recommendations are parsed from the TMDB detail response and displayed in the discovery list.

The UI uses one image slot and a poster/backdrop toggle, not simultaneous poster and backdrop composition. Recommendations are currently a selectable list rather than a horizontally scrolling artwork shelf. Automatic identification is filename-driven and does not yet perform fuzzy release-group matching, multi-candidate confidence scoring, recursive disk discovery, or automatic metadata writes into media files. Keep those limits explicit. TMDB artwork attribution is covered by the existing About notice.

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
| VLC Runtime & Codecs | Read-only audit of the loaded LibVLC version/module path, plugin DLL inventory by category, in-process module mapping, LibVLC/player initialization and observed current-media playback state. The four evidence levels are deliberately distinct; presence or module mapping is not proof of codec playback support. |

Advanced preferences are persisted in `%APPDATA%\\ABMAVPlayer\\settings.ini`, grouped into INI sections such as `[Video]`, `[Audio]`, `[Subtitles]`, `[Streaming]`, `[Performance]` and `[Controls]`. Audio processing and theme selection also use `audio.ini` and `theme.ini`. Do not create a second source of truth for an existing preference.

### Startup settings vs per-media options

- Hardware-decoding and audio-output-module selections are read at startup and require a restart.
- Aspect ratio is applied to the current LibVLC player and persisted.
- Subtitle appearance, caching and performance options are added to media objects as media are opened; an already-playing media object is not guaranteed to be retroactively reconfigured.
- Subtitle timing also updates the current player when possible.
- Controls preferences are loaded into application state and consumed by their matching interactions.
- Diagnostics are observational. Report generation writes locally and does not upload. VLC Runtime & Codecs inventories plugin DLLs, reports in-process module mappings, confirms LibVLC/player initialization and records whether the current media reached Playing when refreshed. Plugin loading is on demand; the playback indicator is session-level evidence, not a per-codec certification.

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

## Canonical version source and generated artifacts

The root `VERSION` file is the sole editable source for the product's semantic version. Do not define a second current version in C/C++, the installer script, manifests, batch files, PowerShell scripts, About windows or documentation.

Run `tools\prepare_version.ps1` to generate:
- `build\version.h` — C/C++ version macros used by the main application and Streaming Centre.
- `build\version.rc` — Windows executable FileVersion/ProductVersion resource.
- `build\version.iss` — Inno Setup preprocessor definition consumed by `installer\ABMAVPlayer.iss`.
- `vcpkg.json` — generated dependency manifest based on the tracked `vcpkg.json.in` template.

The generated files are build artifacts and must not be edited or committed. `vcpkg.json` and the `build\` directory are ignored by Git. The normal build, standalone torrent dependency build, CI workflow, installer builder and release publisher all regenerate from `VERSION` before consuming version-dependent files.

To change the application version, edit only `VERSION`. Then run `build_installer.bat`. It regenerates the artifacts, rebuilds the application, verifies the built and staged executable versions and compiles the installer. Run `publish_release.bat` only after the Windows acceptance and upgrade tests in [RELEASES.md](RELEASES.md) have passed. The publisher reads the same canonical version to construct the Git tag and validate installer PE metadata.

Historical release notes may mention their own release numbers; those are immutable release history, not active version configuration.

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

## Settings behaviour and engineering contract

The settings UI is not merely descriptive: each preference has a specific scope and activation boundary. When adding or changing a setting, update all four surfaces together: the control and persistence code, the in-app F1 Help Centre, `SETTINGS.md`, and the relevant `USER_GUIDE.md` section.

### Activation classes

- **Startup / restart:** LibVLC instance options such as hardware decoding and audio output module are selected before the LibVLC instance is created. A saved value alone cannot reconfigure that existing instance; restart is required. The `ABM_VLC_HW` environment override has precedence over the persisted hardware preference.
- **New-media options:** caching targets, decoder performance options and subtitle appearance are attached when a media instance is opened. Do not describe these as guaranteed to reconfigure media already playing.
- **Current-player capable:** aspect ratio and subtitle timing have code paths that can affect current playback; document any runtime limitations.
- **Immediate interaction preferences:** Controls & Behaviour toggles are persisted and read by the corresponding interaction handlers.
- **Read-only surfaces:** Overview and Diagnostics report state; they must not be described as settings that alter playback.

### Practical impact guidance for maintainers

Document both the expected benefit and the cost:
- More cache can absorb jitter but adds startup delay and, for live streams, distance from the live edge.
- More decoder threads can help parallel codecs but may increase scheduling/CPU contention; it is not a process-wide CPU cap.
- Frame dropping/skipping can preserve playback responsiveness while sacrificing visual continuity.
- Hardware decoding can reduce CPU work on supported combinations, but GPU/driver/codec compatibility is decisive.
- Torrent cache quota is disk-retention capacity, not bandwidth or peer availability. Cache parent and permanent-download destination are distinct paths; path changes do not migrate existing data.
- Subtitle appearance is a presentation preference; timing is a synchronisation correction. Do not imply that one fixes the other.

Use concrete examples, units and activation requirements. Avoid claims such as "improves performance" without explaining the condition and trade-off. For any new control, add a troubleshooting example and state whether the setting applies immediately, to new media, or after restart.


### F1 Help Centre navigation and search

The F1 Help Centre uses a read-only article edit control plus a topic list, search edit and Find next action. Topic names in `g_helpTopicNames` must match section headings in the help article text; navigation locates the heading and selects/scrolls the article to it. Search scans the displayed article text case-insensitively, selects the next match and wraps at the end. Ctrl+F focuses/selects the query, Enter and Find next advance the search, and F3 repeats it. The search surface is intentionally scoped to in-app Help content; it does not index Markdown files, the filesystem or user media.

The Settings window's scrollbars are custom-painted by the shared Direct2D renderer. Keep their track/thumb colors on existing `ABM_THEME_COLOR_*` tokens declared in `src/ui_theme.h`; do not invent palette identifiers or embed a second palette in the window. The Settings viewport reserves scrollbar space, clamps scroll offsets to the virtual content bounds, moves native child edit controls with the same offset, and keeps mouse-wheel and keyboard navigation paths in sync. Native edit controls intentionally remain native for text-entry semantics.

When extending Help:
- Add or rename the article heading and its matching topic-index entry together.
- Keep topic labels concise and useful for navigation.
- Include practical consequences, examples, limitations and activation timing for settings.
- Verify topic selection, search wraparound, no-match feedback, Ctrl+F, Enter, F3, resize behaviour and Esc close in the Windows build.
- Keep the full documentation in the Markdown guides; the in-app article is a navigable quick reference, not a replacement for the developer guide.


### Codec-aware playback evidence

The VLC Runtime & Codecs diagnostic now maintains evidence for the active local-player media session. `PlaybackSetMedia` resets the evidence; the main player's existing 33 ms UI timer polls `libvlc_media_player_get_state`. Once `Playing` is observed, `VlcCaptureTrackEvidence` retains the current media descriptor, calls `libvlc_media_tracks_get`, copies bounded track metadata into ABM-owned storage, and releases both the returned track array and retained media reference. The snapshot is capped at 32 streams for display while the full LibVLC-returned array count is supplied to `libvlc_media_tracks_release`.

The report and diagnostic export include stream type, track ID, codec description and FourCC, plus the current playlist path where available. The extension-derived container label is intentionally named a hint. `Playback Verified` requires both an observed `Playing` state and a non-empty track snapshot; a LibVLC error overrides that result. This is not decoder-module introspection: LibVLC's public API does not expose the exact internal decoder plugin selected, nor does a parsed track list prove every alternate track was selected or decoded. Keep these evidence boundaries explicit in UI and documentation.


### Active-track and playback-event verification internals

The local player attaches a dedicated event observer for Playing, Paused, Stopped, EndReached, EncounteredError, MediaChanged, ESAdded, ESDeleted and ESSelected. This observer is independent of the Torrent event observer. Its callback performs no UI work and only ORs event bits into an interlocked flag; the main UI timer consumes those flags and queries `libvlc_media_player_get_state`, `libvlc_media_player_get_time`, `libvlc_audio_get_track`, `libvlc_video_get_track` and `libvlc_video_get_spu` on the UI thread. Attach/detach occurs with the main player window lifecycle.

The current media's elementary-stream snapshot is refreshed after stream add/delete/selection events. Every track stores its LibVLC ID, type, codec/FourCC, description and whether its ID matches the selected track returned by the corresponding API. Playback session success is separate: it requires Playing followed by media-time progression, or EndReached after Playing. EncounteredError/player Error takes precedence in the displayed result. The diagnostic export records the same selected IDs, per-track selection flags, event-monitoring status and session result.

Do not infer a decoder module from `i_codec`, FourCC or a matching plugin DLL filename. LibVLC 3.0's public API does not expose the selected internal decoder module. Keep that field explicitly unknown unless a separately validated, version-compatible diagnostic source provides direct evidence. A selected track ID proves LibVLC's current selection state, not that all frames/samples from that track were successfully rendered or heard.


### Live music visualizer rendering

`src/audio_visualizer.cpp` owns the WASAPI shared-mode loopback capture worker and FFT band analysis. The worker publishes 24 smoothed frequency bands; it does not access HWNDs or perform rendering. `src/player.c` consumes the band snapshot on the existing main-window paint path. `g_visualizerMode` selects Spectrum (frequency bars), Fluid Waves (three layered frequency-modulated traces), or Particle Visualizer (deterministic orbiting particles and bass-reactive radial spokes). The playback context menu exposes the three choices and invalidates the existing media stage. Keep capture and paint decoupled: do not add UI calls, allocations or blocking work to the WASAPI worker. Capture is currently the Windows default render mix, not process-isolated audio.
