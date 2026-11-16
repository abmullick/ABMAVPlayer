# ABM AV Player — Motion System 1.0

**Status:** Shared C/C++ motion core and reusable Win32 timer host added; Main Player menu, drawer, transport hover, dashboard-card hover and artwork fade migrated; Windows build and runtime validation pending  
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

The shared runtime (`src/ui_motion.h/.cpp`) provides C-compatible, interruptible scalar animations, monotonic QPC timing, named easing modes and Windows client-area-animation preference handling. The reusable Win32 host (`src/ui_motion_win.h/.cpp`) supplies a standard active-only 16 ms timer schedule/sample/stop contract for any ABM-owned HWND. It does not own layout, painting, hit testing or product state. The existing 33 ms operational timer remains unchanged.

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
- [x] Motion-value engine API and implementation added; build compiles and links `src/ui_motion.cpp`.
- [ ] C/C++ compile and unit tests (source has not yet been built on Windows).
- [x] Main Player command menu and drawer use the shared active-only Win32 motion host.
- [x] Main Player transport-button hover feedback migrated to the shared motion host.
- [ ] Main Player home-card hover and artwork fade migration.
- [ ] Auxiliary-window migration.
- [ ] Windows performance and regression validation.


## Phase 1 implementation note — motion-value runtime

Added to `main`:
- `src/ui_motion.h`: C-compatible animation value and easing API.
- `src/ui_motion.cpp`: monotonic QPC-based sampling, interruption-safe retargeting, cancellation, completion and Windows client-area animation preference handling.
- `build.bat`: compiles and links the new C++ runtime.

This is intentionally an engine-only increment. The existing 33 ms main-window operational timer and current `EaseToward` UI behaviour remain unchanged until the engine passes the Windows compile/test gate. No animation scheduler, window migration or visual behaviour change is claimed yet.

Next implementation gate:
1. Add deterministic tests for endpoints, all easing curves, interruption, cancellation, zero duration, reduced motion and elapsed-time independence.
2. Build with the documented x64 MSVC toolchain and verify the C header from both C11 and C++17 translation units.
3. Integrate a dedicated active-only motion timer into the Main Player, keeping the existing 33 ms operational timer unchanged.
4. Migrate drawer and menu first; keep drawer layout updates bounded and ensure the LibVLC child HWND is not moved during paint-only animation.


## Phase 1 implementation note — Main Player command menu

The Main Player command menu now uses the shared motion-value API:
- Menu open/close transitions use a dedicated 16 ms timer that is started only while the transition is active.
- The existing 33 ms operational timer remains responsible for playback polling and unrelated UI/application work.
- Menu progress is sampled from monotonic elapsed time and uses ease-out when opening and ease-in when closing.
- The menu remains paint-only during animation; the LibVLC video child is not resized on each motion tick.
- All identified mouse, keyboard, fullscreen and drawer-triggered menu dismissal paths start or retarget the transition.
- The dedicated timer is killed when the transition completes and during main-window destruction.

This is a scoped first migration, not a claim that the entire Main Player is now on the motion runtime. Drawer, hover, artwork and auxiliary-window transitions remain on their existing paths. Windows compilation and interactive regression testing are still required before treating this phase as validated.


## Phase 1 implementation note — reusable Windows host and drawer

Added a generic Win32 timer adapter:
- `src/ui_motion_win.h/.cpp` exposes C-compatible schedule, stop and multi-value sample helpers.
- The adapter owns only the Win32 timer lifecycle. It does not know about menus, drawers, media, LibVLC or any ABM feature.
- Any ABM-owned window can use the same adapter with its own motion values and paint/layout policy.
- The C11 ABI smoke test now includes both the motion core and Win32 host headers.
- `build.bat` compiles and links the shared Win32 host.

Main Player integration:
- The command menu and media drawer share one active-only 16 ms timer and two independent motion values.
- Drawer transitions use the common 280 ms panel token and ease-in/ease-out direction.
- The drawer's final video-safe footprint is reserved at transition start. Its panel geometry is updated during motion without moving or resizing the LibVLC child HWND on every frame.
- The video child is repositioned at transition boundaries, not on each drawer animation tick.
- The existing 33 ms operational timer remains unchanged.
- Timer creation failure and Windows reduced-motion preferences snap to a consistent final state.

The adapter is deliberately reusable by Settings, Equalizer, Help, About and Streaming Centre chrome in later migrations. Those windows are not yet migrated. WebView2, receiver discovery, Torrent Stream and all playback/casting engines remain outside this UI layer.

Windows x64 compilation and interactive regression validation remain mandatory; repository commits alone do not establish a successful build.


## Phase 1 implementation note — transport hover feedback

The Main Player transport-button hover transition now uses the same shared motion value and active-only Win32 timer as the menu and drawer. Retargeting occurs on pointer hit-target changes; the 33 ms operational timer no longer advances the transport hover animation. The hover state remains presentation-only and does not alter hit testing, playback commands or keyboard shortcuts.

The reusable Win32 host is intentionally generic: each window supplies its own motion values and timer ID, and retains ownership of its layout, invalidation regions and input state. The Main Player currently schedules menu, drawer and transport-hover values together. Home dashboard card motion, artwork fade and auxiliary-window transitions remain separate follow-up work.


## Phase 1 implementation note — dashboard and artwork

The four idle-dashboard cards now retarget shared motion values only when their pointer target changes. Their interpolation runs on the active-only motion timer rather than the operational 33 ms timer. Artwork reveal now uses the shared EMPHASIS duration and ease-out curve; resetting artwork cancels any stale reveal so a previous track cannot continue fading over the next one. Motion ticks invalidate only the video/dashboard paint region and do not reposition the LibVLC child window.

The Main Player's menu, drawer, transport hover, dashboard-card hover and artwork fade now share one per-window timer host. This completes the currently identified Main Player motion-value migration. It does not mean the Main Player has received its full visual redesign, nor that auxiliary windows have been migrated. Queue/library row transitions and auxiliary workspaces remain separate work items.
