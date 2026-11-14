# ABM AV Player v1.0.6 — security hardening and release alignment

- Strengthened native compiler and linker protections with MSVC security checks, Control Flow Guard, ASLR, DEP, high-entropy ASLR and CET compatibility flags.
- Hardened local trial-state validation: persisted records that are present but unreadable, invalid or inconsistent fail closed and require activation rather than silently restarting the trial.
- Changed the embedded activation-key digest comparison to a constant-time byte comparison and added lightweight digest obfuscation. This is a reverse-engineering speed bump, not a cryptographic boundary.
- Kept Authenticode enforcement optional and disabled by default. The normal unsigned development and distribution configuration continues to work; no code-signing certificate, publisher registration or signing workflow is required for this release.
- Aligned the application About version, Streaming Centre About version, installer metadata, packaging guards, release publisher and vcpkg manifest to 1.0.6.
- Existing local playback, Streaming Centre, torrent streaming, caching and casting workflows are retained.

**Release validation:** Build and test the Windows application and installer on the target Windows environment before publishing. Source updates alone do not establish a successful Windows build or runtime test.

# ABM AV Player v1.0.5 (licensing feature)

- Added a 10-day full-feature trial that begins on first launch.
- Added offline perpetual activation using one universal 16-character alphanumeric key, valid across all customer installations.
- Removed device fingerprinting, device-bound activation, individual licence generation and publisher private-key signing utilities.
- Added protected local trial-state persistence and clock-rollback detection.
- Existing playback, Streaming Centre, torrent and casting engines are unchanged.

# ABM AV Player v1.0.4

## What's new

- Added Torrent Stream as a separate peer-to-peer media window for user-supplied magnet links and local `.torrent` files.
- Added torrent metadata and file listing, per-task progress, peer/connection and transfer-rate diagnostics, pause/resume, removal, and selected-file downloading.
- Added progressive local playback through LibVLC using verified torrent pieces and an authenticated loopback HTTP Range source.
- Added persistent selected-file downloads under the configured Windows `Torrent Downloads` folder, separate from the evictable cache.
- Added Torrent Stream TV discovery and independent DLNA casting. Casting serves the same file that libtorrent continues downloading; **Stop TV** stops the receiver/listener without deleting the download.
- Added configurable cache quota, usage reporting, inactive-cache cleanup, and separate download/upload rate limits.

## Improvements

- Enabled DHT/LSD with multiple bootstrap routers and periodic bootstrap retry when DHT is not running.
- Request an immediate tracker announce when a torrent task is added; request a DHT announce when DHT is available.
- Preserved separation between Torrent Stream, the main player's local playback/casting, and Streaming Centre receiver state.
- Updated Torrent Stream and Streaming Centre mastheads to follow the main player's split ABM / AV PLAYER brand treatment.
- Removed the extra custom-painted arrow from the Torrent Stream TV selector, retaining the native Win32 dropdown arrow.

## Fixes

- Corrected libtorrent 2.1.2 DHT API compatibility by using the supported `enable_dht` session setting rather than the unavailable `session::start_dht()` method.
- Guarded `WIN32_LEAN_AND_MEAN` to avoid macro redefinition when supplied by the build configuration.
- Added dedicated DirectWrite font formats for the ABM and AV PLAYER wordmark elements in the Torrent Stream header.

## Validation and limitations

- Windows x64 build completed and the installer was created and installed for maintainer testing.
- Torrent peer discovery depends on the source torrent having reachable peers and on trackers/DHT being reachable from the user's network. A DHT status of ON does not guarantee peers for every torrent.
- DLNA direct play depends on the receiver's supported containers and codecs. This release does not add PC-side transcoding or arbitrary-media Google Cast sender support.
- Netflix and Prime Video desktop-browser title handoff is not implemented. Launching a TV app does not transfer a browser title, DRM session or provider credentials.
- Physical receiver compatibility, firewall behaviour and sustained torrent playback/casting should be validated on the target network.

## Requirements

- Windows 11 x64
- Microsoft Edge WebView2 Evergreen Runtime
- 64-bit LibVLC SDK/runtime and required plugins
- Windows x64 build dependencies, including the manifest-pinned libtorrent 2.1.2 package
