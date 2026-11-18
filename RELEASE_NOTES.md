# ABM AV Player v1.1.3 — Home Theatre, immersive visualizers and playback refinements

## What's new

- Added **Home Theatre**, an optional TMDB-powered movie and television discovery workspace. Search titles, inspect posters/backdrops and recommendations, browse TV seasons and episodes, identify titles across the active playlist and Favourites, correct filename matches with a local-file override, and play the selected local match through the existing player.
- Expanded live music visualization to **ten selectable styles**: four Direct2D scenes (Spectrum Orbit, Fluid Waves, Particle Universe and Neon Spectrum) and six isolated OpenGL scenes (MilkDrop, 3D Spectrum, Particle Universe, Oscilloscope 3D, Fluid Motion and Geometric Tunnel). Fluid Motion (OpenGL) is the default.
- Kept OpenGL rendering in the separate `abm-projectm-renderer.exe` helper and composed its frames into the existing media stage. The selected style remains selected during asynchronous renderer startup; a neutral dark surface is shown until the first frame is ready instead of temporarily displaying Neon Spectrum.
- Smoothed spectrum attack/release and eased visualizer activation to reduce abrupt starts.
- Added embedded audio-track selection to Advanced Audio Centre and refined Subtitle Manager controls, layout, hit targets and user feedback.
- Added three-state repeat mode cycling: **Off → Repeat all → Repeat current**, with a visible active-mode indication.
- Expanded F1 Help and user/developer documentation for Home Theatre, the ten visualizer styles, repeat behaviour and audio/subtitle controls.
- Continued refinements to Settings layout/scrolling, recent-media popup behaviour, playback controls, audio-track synchronization and QA coverage.

## Reliability and compatibility

- OpenGL/ProjectM remains isolated from the main LibVLC playback process. The helper is disposable; renderer failure must not terminate the audio playback path, and Direct2D remains the recovery visualizer.
- The OpenGL renderer is embedded into the existing media stage; no separate visible visualizer window is introduced.
- Visualizer capture uses WASAPI loopback on the Windows default render endpoint. Other applications using that endpoint may influence the visualization; process-specific capture is not implemented.
- Home Theatre metadata and artwork require a configured TMDB Read Access Token and network access. TMDB discovery is optional and does not gate local media playback. Filename identification is a suggestion and should be corrected when the local match is wrong.
- Existing installer identity, installation directory, licensing state and per-user AppData separation must remain unchanged for in-place upgrades.

## Release validation

- This is a **source-prepared v1.1.3 release candidate** based on the latest `main` branch.
- Windows x64/MSVC compilation, runtime testing of all ten visualizer styles, Home Theatre network and local-match flows, audio/subtitle track selection, repeat-mode persistence, playback/casting/torrent regression, clean installation and in-place upgrade have not yet been certified by this source update.
- Complete [UI_QA_CHECKLIST.md](UI_QA_CHECKLIST.md), build the installer on the Windows machine, inspect its version metadata, and complete clean-install and upgrade tests before publishing. Do not treat this source candidate as a certified release until those gates pass.

---

# ABM AV Player v1.1.2 — media continuity, recovery and Queue/Library improvements (previous published release)

## What's new

- Added persistent per-file playback checkpoints with asynchronous, atomic writes and automatic resume after LibVLC enters Playing state.
- Added explicit **Continue from checkpoint** and **Start from beginning** actions so users can choose how playback starts.
- Added a Library view cycle for All tracks, Favourites and Continue Watching, including row-level Continue status.
- Retained missing media paths in the library, favourites, recent history and personal collections instead of silently pruning them.
- Added Missing status and a Relink missing media action that updates active playlist references, favourites, recent history, resume state and saved M3U collections.
- Added **Open file location** to the Queue and Library row context menus, opening Explorer with the selected media file highlighted when it exists.
- Updated the F1 Help Centre guidance for the Queue/Library file-location action.
- Added QA checklist coverage for Queue and Library context-menu access, Explorer selection, missing-file handling and keyboard invocation.
- Retained Queue and Library duration labels with asynchronous metadata resolution and a bounded cache, keeping media parsing off the UI thread.
- Refined windowed/fullscreen playback controls and video/control clearance.

## Reliability and compatibility

- Playback checkpoint writes remain asynchronous and atomic; media metadata resolution remains outside the UI paint/scroll path.
- Missing-file actions are guarded so users can relink unavailable media before opening its location.
- Existing installer identity, installation directory and per-user AppData separation must remain unchanged for in-place upgrades.

## Release validation

- This was the source-prepared v1.1.2 candidate, subsequently published as the previous stable release.
- Windows x64 compilation, runtime playback/resume/relink testing, Queue/Library scrolling performance measurement, casting regression, clean installation and in-place upgrade have not been performed by this source update.
- Complete the Windows QA checklist, build and inspect the installer, and test clean installation and in-place upgrade before publishing. Do not treat this source candidate as a certified release until those gates pass.

---
## Published release notes

ABM AV Player v1.0.9 — playback UI polish and media library improvements

## What's new

- Added media duration labels to Queue and Library cards, including the Favourites view.
- Added asynchronous duration metadata resolution with a bounded in-memory cache so media parsing is kept off the UI thread and scrolling/painting performs only a cache lookup and non-blocking request enqueue.
- Refined windowed and fullscreen playback controls, including the seek timeline, playhead and volume rail styling.
- Improved windowed video/control clearance so the playback surface leaves room for the seek bar and transport controls.
- Added the Motion System 1.0 architecture audit and documented the staged path toward consistent, responsive UI transitions.

## Reliability and compatibility

- Kept the existing 33 ms operational playback timer separate from the proposed motion scheduler; the motion engine itself is not yet implemented.
- Preserved the existing local playback, receiver discovery/casting, Streaming Centre and Torrent Stream boundaries in the source changes described here.
- Duration metadata is best-effort: files whose duration cannot be resolved display an em dash rather than blocking the interface.

## Release validation

- This is a source-prepared v1.0.9 candidate based on the latest `main` branch.
- Windows x64 compilation, runtime scrolling/performance measurement, playback and casting regression, clean installation and in-place upgrade have not been performed by this source update.
- Complete the Windows QA checklist and verify the generated installer before publishing. Do not treat this source candidate as a certified release until those gates pass.

---

# ABM AV Player v1.0.8 — stability, diagnostics and unified design system

## What's new

- Unified the Main Player, Streaming Centre, Torrent Stream, Settings, Advanced Settings, Help Centre, About, Equalizer and modal surfaces around shared ABM Direct2D/DirectWrite rendering, typography roles, icon-font tokens, semantic surfaces and common geometry.
- Improved typography consistency, wordmark alignment, control surfaces, focus/hover states, clipping, text trimming and fallback rendering across application windows.
- Reworked Settings sizing and scrolling for constrained window sizes and monitor work areas, with custom ABM-rendered scrollbars, keyboard navigation and improved focus visibility.
- Improved Help Centre article navigation, search, typography, paragraph rendering and the Advanced Settings diagnostics guidance.

## Playback, streaming and Torrent reliability

- Added structured playback failure reporting and safer playback cleanup; centralized local playback engine commands.
- Hardened Torrent playback and streaming lifecycles, including stale callback rejection, decoder-error teardown, HTTP request lifetime, stream readiness and cancellable worker shutdown.
- Improved Torrent recovery for classified transient errors and stalled downloads, corrected paused-state handling, and fixed cache quota accounting to include actual torrent payload storage.
- Reclaimed queued worker payloads and cleaned up stale artwork downloads and resource ownership during playback changes and Streaming Centre teardown.
- Preserved separation between the local player, Streaming Centre casting/discovery and Torrent Stream's independent playback/casting paths.

## Diagnostics and documentation

- Added the VLC Runtime & Codecs diagnostics page and included runtime details in diagnostic reports.
- Documented the evidence levels File Found, Plugin Loaded, Runtime Tested and Playback Verified; plugin discovery/loading is not the same as codec certification.
- Expanded the Settings, User, Developer, UI design-system and Windows QA documentation to reflect the implemented behaviour and remaining runtime checks.

## Compatibility and release validation

- This release candidate is prepared from the current `main` branch, after the published `v1.0.7` tag.
- The version is sourced only from the root `VERSION` file. Build-time version headers, resources, Inno Setup metadata and the vcpkg manifest are generated by `tools\\prepare_version.ps1`.
- No claim of Windows build, installer, upgrade or full runtime regression success is made by this source update. Complete the Windows x64 build, clean-install and in-place upgrade checks, and feature regression checklist before publishing.
- Existing installer identity, installation directory and per-user AppData separation must remain unchanged.

---

# ABM AV Player v1.0.7 — Help Centre experience and release alignment

# ABM AV Player — Help Centre experience and release alignment

## Documentation follow-up — Advanced Settings diagnostics

- Expanded the in-app F1 Advanced Settings article to explain the read-only Overview, all ten Advanced Settings pages, Diagnostics report workflow and the separate VLC Runtime & Codecs evidence levels.
- Clarified the distinction between system-wide resource metrics and ABM AV Player process memory, and documented the local report preview and data-folder actions.
- Documented the four VLC evidence states: File Found, Plugin Loaded, Runtime Tested and Playback Verified. Clarified that on-demand plugin loading is normal and playback evidence is not codec certification.
- Expanded the User Guide's Diagnostics report contents, privacy review guidance and Refresh scan workflow.
- Documentation-only change: no settings, playback, diagnostics collection, network or upload behaviour was changed. Windows build and in-app runtime verification remain required.


## What's new

- Refined the F1 Help Centre shell with ABM AV Player styling, stronger typography, clearer navigation and a more spacious search and article layout.
- Reworked help topics to open as focused articles instead of presenting the complete guide as one continuous text block.
- Added rich-text article formatting with a clearer heading/body hierarchy, improved paragraph spacing and application typography.
- Added normalization for real and escaped line endings so help content can render paragraph breaks instead of displaying literal newline escape sequences.
- Improved guide-wide search so a match can take the user to the corresponding topic and highlight the matching text.

## Release and packaging alignment

- Aligned the main application's About/version constant, Streaming Centre About display, Inno Setup application and PE version metadata, vcpkg manifest, installer build guards and release publisher to the canonical version defined in `VERSION`.
- Kept the existing Inno Setup AppId, installation directory, installer filename and AppData separation unchanged to support in-place upgrades and preserve existing user data.
- Retained the existing unsigned distribution workflow; Authenticode enforcement remains optional and disabled by default.

## Compatibility and validation

- No intentional changes to the local playback, Streaming Centre provider integration, torrent engine, receiver discovery, casting, licensing model or user configuration formats are included in this release.
- The release number is read from the repository's canonical `VERSION` file. This is a source-prepared release. A successful Windows x64 build, installer creation, installation/upgrade test and feature regression pass must be completed before publishing the GitHub Release.

---



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


## Settings window — custom scrolling follow-up

- Replaced the Settings window's native Windows horizontal and vertical scrollbars with custom Direct2D-rendered ABM scrollbars using shared theme tokens.
- Added documentation for thumb dragging, track clicks, mouse-wheel scrolling and keyboard navigation, including the retained native text-entry controls.
- Updated the in-app F1 Settings article, Settings reference, User Guide, Developer Guide, UI/UX design-system notes and Windows QA checklist.
- This is a source/documentation update. The custom scrollbar implementation still requires a successful MSVC build and Windows runtime verification at multiple window sizes and display scales.
