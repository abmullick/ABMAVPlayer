# ABM AV Player — Motion System 1.0

**Status:** Phase 0 architecture audit complete; implementation not yet started  
**Baseline:** `main`  
**Scope:** UI motion and transitions only. Playback, media decoding, casting, Torrent Stream networking and WebView2 behaviour are non-goals.

## 1. Goals

- Establish one shared, C-compatible motion engine for ABM-owned UI.
- Use a restrained, premium motion language across the Main Player and auxiliary workspaces.
- Make transitions interruptible, resize-safe and independent of playback polling.
- Keep idle CPU usage negligible and avoid invalidating the video surface unnecessarily.
- Respect reduced-motion preferences and keyboard/input responsiveness.

## 2. Phase 0 audit — confirmed source facts

### Main Player

- The main window creates a 33 ms Win32 timer in `WM_CREATE` (`SetTimer(hwnd, TIMER_ID, 33, NULL)`).
- That timer is not animation-only. It also polls LibVLC state, codec verification, Torrent playback stalls, audio-output availability, renderer-discovery timeouts, resume state, media completion, fullscreen pointer/control visibility, audio visualizer state and video geometry.
- The same timer advances drawer and menu progress, button hover easing, home-card hover easing, artwork fade and a global visual pulse.
- `EaseToward` uses integer exponential convergence and is described in source as part of the 30 Hz UI animation loop.
- Drawer movement invokes `UpdateLayout` and invalidates the main window while it progresses. Menu movement is paint-only after the initial video-window reposition and final close reposition.
- The main player owns a LibVLC video child HWND. Video child positioning and z-order are sensitive; animated overlay work must not resize or invalidate the video child every frame.

### Direct2D / DirectWrite

- `src/ui_d2d.h` exposes C-compatible renderer APIs and opaque C++ objects.
- `src/ui_d2d.cpp` owns Direct2D HWND and DC render targets and canonical DirectWrite text formats.
- The renderer is a drawing layer, not an animation scheduler. Motion should provide values to window procedures and leave painting/render-target ownership with each existing surface.

### Streaming Centre

- `src/streaming/streaming_host.cpp` has its own 400 ms device refresh timer and a 180 ms device-selection/launch pulse timer.
- Its timers are specific to receiver status and interaction feedback. The WebView2 controller, browser lifecycle and receiver selection must remain independent.
- Motion integration must not alter WebView2 navigation, controller recovery, device discovery, receiver selection or casting.

### Other windows and modules

- Torrent Stream has a 1 s status/discovery timer. It is operational polling, not a candidate for the shared animation scheduler.
- Equalizer, Settings, Advanced Settings, Help, About, modal dialogs and popup windows contain their own Win32 procedures and paint paths. Each requires a focused inventory before motion is added.
- The build is explicitly enumerated in `build.bat`; new C++ motion translation units must be added to both compile and link commands, and the public header must remain C-compatible.

## 3. Architectural decision

Create a small, platform-specific motion runtime that exposes a C ABI and is consumed by the existing C window procedures.

Responsibilities:
- Store animation instances and their start/target values.
- Use monotonic elapsed time (QPC) rather than frame-count increments.
- Provide named duration/easing presets and deterministic interpolation.
- Support cancellation, replacement, completion and reduced-motion snapping.
- Expose whether any animation is active and the next frame's values.
- Run one animation scheduler while motion is active; stop it when the registry becomes idle.

Non-responsibilities:
- No window creation, layout calculation, hit-testing or painting inside the engine.
- No LibVLC, torrent, network, casting, WebView2 or audio operations.
- No ownership of application state; the caller owns values and decides what to invalidate.

Proposed source files:
- `src/ui_motion.h` — C-compatible API and types.
- `src/ui_motion.cpp` — registry, clock, scheduler-facing update and lifecycle.
- `src/ui_motion_easing.cpp` — easing functions if keeping them separate materially improves testability.

The initial implementation should avoid a new permanent 60 Hz timer. The main window should use a dedicated motion timer only while animations are active, with a target cadence around 16 ms. The existing 33 ms operational timer remains unchanged. Other windows should opt in only when their own transitions are implemented.

## 4. Motion tokens

| Token | Duration | Intended use |
|---|---:|---|
| QUICK | 120 ms | Press/release feedback |
| STANDARD | 180 ms | Hover and compact menu transitions |
| CONTENT | 240 ms | Queue/library item feedback |
| WINDOW | 260 ms | Floating window/dialog transitions |
| PANEL | 280 ms | Drawers and side panels |
| EMPHASIS | 350 ms | Larger, infrequent transitions |

Easing:
- Ease-out for opening and reveal.
- Ease-in for dismissal.
- Ease-in-out for movement between stable positions.
- Restrained spring only for selected, non-critical emphasis; no elastic overshoot on controls or hit-test geometry.

## 5. Integration order

1. Implement and validate the engine in isolation.
2. Migrate Main Player drawer/menu/hover/home-card/artwork transitions.
3. Migrate queue, library and favorites interaction feedback.
4. Inventory and migrate Equalizer, Settings, Advanced Settings, Help, About and modal surfaces.
5. Assess Streaming Centre chrome separately; keep WebView2 and receiver operations untouched.
6. Add queue reorder and notification transitions only after core lifecycle and interruption behaviour is stable.

## 6. Performance and safety constraints

- Target approximately 60 FPS while a visible animation is active; no continuous animation timer while idle.
- Keep the 33 ms operational timer unchanged.
- Invalidate only the affected UI region where practical.
- Do not move or resize the LibVLC video child during paint-only animations.
- Do not perform I/O, LibVLC calls, network work or expensive layout on animation ticks.
- Cache GDI/Direct2D resources; do not allocate brushes or fonts per frame.
- Handle resize, DPI changes, minimize/restore, window destruction and interrupted/reversed transitions.
- Reduced-motion mode must snap or substantially shorten transitions.
- Do not change main-player casting, Streaming Centre receiver discovery/selection, Torrent Stream, WebView2 or playback semantics.

## 7. Validation gates

Before integrating the engine:
- Compile the C header from C11 and C++17 translation units.
- Unit-test easing endpoints, elapsed-time independence, reversal, cancellation, zero duration and reduced motion.
- Verify no active timer remains after the final animation completes.
- Verify window destruction unregisters animations and kills its timer.
- Verify no callback can target a destroyed HWND.

After Main Player integration, Windows validation must cover:
- Normal playback and audio-only playback.
- Video child HWND placement and resize.
- Drawer/menu open-close reversal and rapid repeated input.
- Fullscreen entry/exit and controls timeout.
- Queue/library/favorites, drag reorder and keyboard interaction.
- 100%, 125%, 150% and 200% display scaling.
- CPU usage at idle and during animation, plus GDI/USER object counts.
- Local casting, Streaming Centre casting, Torrent Stream and WebView2 regression checks.

## 8. Repository workflow note

The current `DEVELOPER_GUIDE.md` says this repository workflow commits directly to `main`. This audit therefore records the architecture on `main` rather than silently introducing a conflicting branch workflow. If the maintainer changes that repository rule, the implementation can be moved to the previously proposed feature branches.

## 9. Current completion status

- [x] Main Player timer and animation responsibilities inspected.
- [x] Direct2D/DirectWrite boundary inspected.
- [x] Streaming Centre timer and WebView2 boundary inspected.
- [x] Torrent operational timer distinguished from UI animation.
- [x] Build integration requirements identified.
- [x] Motion architecture and safeguards documented.
- [ ] Motion engine implementation.
- [ ] C/C++ compile and unit tests.
- [ ] Main Player migration.
- [ ] Auxiliary-window migration.
- [ ] Windows performance and regression validation.
