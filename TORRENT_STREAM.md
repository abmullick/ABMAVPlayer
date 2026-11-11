# Torrent Stream — Design and Implementation Plan

**Status:** Native source manager and initial local LibVLC handoff are now implemented on `feature/windows-torrent-stream-phase1`. The UI accepts magnet URIs and local `.torrent` files, polls metadata/status, lists torrent files, selects a file, waits for an initial verified buffer on a worker thread and passes the loopback URL to LibVLC. Windows x64 compilation, runtime behaviour and real-peer playback remain unverified; the feature is not in a published Windows release  
**Target:** Windows 11 x64 desktop application  
**Repository:** ABM AV Player  
**Design principle:** Add an optional torrent-backed media source without changing or destabilising existing local playback, Streaming Centre, or TV-casting paths.

## 1. User experience

Add a separate **Torrent Stream** entry point to ABM AV Player. The first release should accept only user-supplied sources:

- Magnet URI.
- Local `.torrent` file.
- An HTTP(S) URL that directly serves a `.torrent` file, if enabled after URL validation.

The workflow:

1. User pastes a magnet URI or opens a torrent file.
2. ABM retrieves metadata and lists files.
3. User selects one media file.
4. ABM checks available peers, file size, and estimated readiness.
5. User chooses **Stream** or **Download**.
6. For Stream, ABM prioritizes the pieces needed near the current playback position and starts LibVLC when a safe startup buffer is available.
7. The user can pause/resume the torrent session, stop it, remove it, and control upload/download limits.
8. When supported by the receiver, the completed/available media is offered to the existing TV-casting path.

No public torrent index, search engine, curated catalogue, or copyrighted-content directory is part of this feature.

## 2. Technology choice

Use **libtorrent 2.1.2** as the BitTorrent engine, pinned through the repository's vcpkg manifest and baseline for a reproducible Windows x64 dependency build.

Official project: https://libtorrent.org/

Do not implement BitTorrent protocol handling from scratch. Keep libtorrent behind a narrow ABM-owned C++ interface so the rest of the application does not depend on libtorrent headers or types.

## 3. Proposed module boundaries

Suggested source layout:

- `src/torrent/torrent_engine.h` — stable ABM-facing API and event structures.
- `src/torrent/torrent_engine.cpp` — libtorrent session, magnet/torrent metadata, file selection, piece priorities and lifecycle.
- `src/torrent/torrent_cache.h/.cpp` — cache path, verified read access and cleanup policy.
- `src/torrent/torrent_stream_source.h/.cpp` — local read-only media source for LibVLC.
- `src/torrent/torrent_ui.h/.cpp` — isolated Torrent Stream window/panel and status updates.

The torrent engine must run off the UI thread. All UI updates must be marshalled to the Win32 UI thread. Callbacks must not retain window handles beyond shutdown, and engine shutdown must join worker activity before releasing state.

## 4. Progressive playback design

Do not expose a partially written file directly to LibVLC and assume that it is ready. The implementation must provide a controlled read path over verified torrent pieces.

Preferred initial design:

1. Use libtorrent's piece/block availability and time-critical piece scheduling for the selected file.
2. Maintain a verified-piece/readiness map for the selected media file.
3. Expose the media through a loopback-only HTTP source or a custom LibVLC access/demux integration.
4. Return data only when the corresponding pieces are verified; otherwise wait asynchronously with cancellation and bounded timeouts.
5. Prioritize a startup window and the next playback range. Reprioritize when the player seeks.
6. Keep a small configurable buffer ahead of playback and a smaller safety window behind it.
7. Detect stalls and surface buffering state rather than blocking the UI thread.

The first implementation should prefer a loopback HTTP source because it is easier to isolate and test than a custom LibVLC plugin. Bind only to loopback, use an unpredictable per-session token, reject non-loopback clients, and close the listener on Stop/exit. Validate HTTP Range requests if LibVLC uses them. Never expose a torrent-backed source to the LAN by default.

If the chosen libtorrent version's time-critical API or file mapping differs, adapt to its pinned API rather than assuming API compatibility across releases.

## 4A. Implemented verified-piece HTTP source (Phase 2 backend)

The native DLL now exports these additional functions in `torrent_engine.h`:

- `ABM_Torrent_SetReadWindow(task, file, offset, length, ahead_bytes)`: clears stale deadlines and prioritizes the requested file range plus a bounded look-ahead window.
- `ABM_Torrent_WaitForRange(task, file, offset, length, timeout_ms)`: schedules an 8 MiB look-ahead and waits off the UI thread until every piece covering the requested range is fully present.
- `ABM_Torrent_StartHttpStream(task, file, url, ...)`: selects the file and starts an authenticated local HTTP server; the returned URL contains a 256-bit random token.
- `ABM_Torrent_StopHttpStream()`: closes the listener, cancels pending verified-range waits and joins client workers.

The HTTP source binds to IPv4 loopback only (`127.0.0.1`), checks that the connecting peer is loopback, accepts only the exact token path, and supports GET, HEAD, and one `Range: bytes=start-end` range (including open-ended and suffix ranges). Responses use `206 Partial Content` and `Content-Range` when a range is requested. The source streams in bounded 256 KiB chunks. A chunk is read from the torrent cache only after libtorrent reports every piece covering that chunk as complete; libtorrent documents `have_piece()` as true only once the piece has been downloaded and written to disk. The engine never exposes a partially written piece as media bytes.

Recommended call order for the upcoming UI/LibVLC integration: (1) add the magnet or torrent and wait for metadata; (2) call `ABM_Torrent_StartHttpStream` to select the file and obtain its tokenized URL; (3) on a worker thread call `ABM_Torrent_WaitForRange` for the first `min(file_size, 8 MiB)` bytes; (4) only after it succeeds, pass the URL to `libvlc_media_new_location`; (5) call `ABM_Torrent_StopHttpStream` when playback stops or is cancelled. `StopHttpStream` also cancels an in-flight range wait. Later HTTP Range requests trigger fresh piece-window scheduling, supporting seek-driven reprioritization. A stalled range is bounded to 30 seconds per chunk; on timeout the connection closes rather than returning unverified bytes.

**Not yet wired:** the Windows player does not yet call these new exports or pass the resulting URL to LibVLC. Magnet/.torrent task creation, file selection UI, startup-buffer progress UI and the LibVLC handoff are still the next integration step. No TV can fetch this loopback URL, and no LAN serving has been added.

## 5. TV casting

Torrent playback and casting are separate concerns.

- For a compatible DLNA renderer, expose only the local media source through the existing Streaming Centre/renderer pathway when the receiver can reach it. A loopback-only URL cannot be fetched by a TV; LAN serving would require a separate explicit, authenticated, firewall-aware mode and must not be enabled by default.
- For Google Cast, the receiver must be able to fetch the media URL and support its container/codec. Use an appropriate Cast sender/receiver integration; do not assume that a local HTTP URL or arbitrary torrent URL is castable.
- Do not change the existing local-player casting or Streaming Centre receiver-selection state.
- If safe and reliable receiver delivery is not available, keep torrent playback local and explain why casting is unavailable.

## 6. Required user controls

- Add magnet / open torrent.
- Metadata and file list, including file size and media type.
- Stream selected file / download selected file.
- Pause/resume torrent activity.
- Stop and remove task.
- Download and upload speed limits.
- Connection/peer count and download/upload rates.
- Playback buffer/readiness indicator.
- Storage location and cache usage.
- Per-task progress and error details.
- Clear warning that BitTorrent may upload pieces to other peers.
- Explicit control over seeding after the selected file is complete.

## 7. Safety, privacy and legal boundaries

- The feature is a general-purpose protocol client for content the user owns, is licensed to access, or is otherwise legally entitled to download.
- Do not bundle torrent search engines, indexes, content recommendations, or preloaded torrent links.
- Do not bypass DRM, subscription checks, paywalls, encryption, or provider access controls.
- Display the torrent's upload/seeding behaviour and provide a clear stop control.
- Respect private-torrent tracker requirements; do not silently alter announce or peer settings.
- Validate magnet URIs and torrent metadata; reject malformed or oversized metadata and unsafe paths.
- Prevent path traversal and writes outside the configured torrent data directory.
- Apply disk-space checks, per-task/global limits, and a configurable cache quota.
- Keep network listeners loopback-only by default. Any future LAN serving must require an explicit user action and clear security warning.

## 8. Build and dependency strategy

Before enabling the feature in the normal build:

1. Pin an exact libtorrent release and its dependency versions.
2. Build or obtain a trusted x64 MSVC-compatible library matching the application's runtime/toolset.
3. Record the source, version, licence, hashes, and reproducible build instructions.
4. Add explicit `LIBTORRENT_ROOT` validation in `build.bat`.
5. Compile the torrent module as a separate translation unit and link the required libtorrent and Boost/system dependencies as appropriate for the pinned build.
6. Stage all required runtime DLLs in the installer and verify clean-machine startup.
7. Keep torrent support optional at runtime: failure to initialize it must not prevent normal ABM playback or Streaming Centre startup.

Do not add a guessed NuGet package or unpinned binary dependency. Do not claim the feature is available until a Windows x64 build and installer have been tested.

## 9. Phased implementation

### Phase 1 — dependency and engine foundation
- Pin libtorrent and establish the x64 MSVC build.
- Add the isolated engine API.
- Parse magnet/torrent metadata and enumerate files.
- Implement task lifecycle, cancellation, status and bandwidth limits.
- Add unit/integration tests for malformed sources and safe paths.

### Phase 2 — progressive local playback
- [x] Implement verified-piece cache reads and reject unverified ranges.
- [x] Add loopback-only HTTP source with Range support and per-session authentication.
- [x] Implement bounded wait-for-range, time-critical piece scheduling and seek-range reprioritization.
- [x] Connect the source to the existing LibVLC player without changing normal file playback (development-branch implementation; runtime validation pending).
- [ ] Exercise stall recovery and cancellation against real peers on Windows.

### Phase 3 — native UI and download manager
- [x] Add an initial Torrent Stream window, magnet/.torrent ingestion, metadata file list, task status, pause/resume/remove and stream/stop controls (runtime validation pending).
- [x] Add task progress, peer count, transfer rates and uploaded-byte status.
- [x] Add separate download/upload rate-limit controls in the Torrent Stream window (KiB/s; 0 = unlimited; runtime validation pending).
- [ ] Add a dedicated buffer/stall indicator and persistent bandwidth preferences.
- Add cache quota, cleanup, and explicit seeding controls.

### Phase 4 — TV casting and release hardening
- Evaluate receiver reachability and codec compatibility.
- Implement only receiver paths that can securely fetch the media.
- Test with Android TV and Google TV devices on real LANs.
- Validate firewall behaviour, shutdown, network loss, disk-full conditions, seek/stall cases, memory use, installer and upgrade/uninstall behaviour.

## 10. Acceptance criteria

The feature is not complete until all of the following pass on Windows 11 x64:

- A user-supplied valid magnet and a local `.torrent` file both load.
- Metadata and selected-file listing are correct.
- A legal test torrent with sufficient peers begins playback before the entire file downloads.
- Playback reads only verified pieces and survives ordinary buffering delays.
- Seeking reprioritizes required pieces and recovers from unavailable ranges.
- Stop cancels playback, network activity and pending reads without hanging the UI.
- Upload/download limits work and seeding state is visible.
- Unsafe file paths and malformed metadata are rejected.
- Loopback HTTP source cannot be accessed from another LAN device.
- Existing local playback, Streaming Centre, TV discovery and local casting regressions are absent.
- Installer works on a clean Windows test machine with all pinned runtime dependencies.

**Current state:** The feature branch now contains a native libtorrent 2.1.2 engine DLL, a C ABI for magnet/.torrent ingestion and task control, vcpkg/CMake build integration, a Windows x64 CI workflow, and installer staging into an isolated `torrent-runtime` folder. The application exposes **Torrent Stream** through its dedicated source manager. The window includes separate download and upload rate-limit inputs (KiB/s, with 0 meaning unlimited); the controls call the engine's existing session rate-limit API. Persistent preferences and Windows runtime validation remain pending. Selecting it loads `torrent-runtime\\abm_torrent_engine.dll` and initializes libtorrent on a worker thread; ordinary player startup does not start torrent networking. The DLL now also exports a verified-piece streaming pipeline: it maps selected-file offsets to torrent pieces, schedules the current read window with piece deadlines, checks `have_piece()` before reading from the on-disk cache, and serves GET/HEAD plus single HTTP byte ranges from a random-token URL bound only to `127.0.0.1`. The read API waits with a bounded timeout and can be cancelled during stream shutdown. File symlink entries and paths resolving outside the configured torrent data directory are rejected. The application UI still does not yet expose magnet/.torrent task creation or call the stream URL/LibVLC handoff APIs; that is the next UI integration step. No LAN listener or TV casting path is created. The Windows x64 build and runtime still require validation on a real Windows machine.
