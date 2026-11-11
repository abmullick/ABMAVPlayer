# Torrent Stream — Design and Implementation Plan

**Status:** Planned; not implemented in the current Windows release  
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

Use **libtorrent 2.x** as the BitTorrent engine, subject to a pinned and reproducible Windows x64 dependency build.

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
- Implement verified-piece cache reads.
- Add loopback-only HTTP source with Range support and per-session authentication.
- Implement startup buffering, time-critical piece scheduling, seek reprioritization and stall recovery.
- Connect the source to the existing LibVLC player without changing normal file playback.

### Phase 3 — native UI and download manager
- Add Torrent Stream UI, task list, file picker and controls.
- Add rate/peer/buffer indicators and persistent preferences.
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

**Current state:** This document records the approved product direction only. No torrent engine, UI, dependency, or installer integration is present yet.
