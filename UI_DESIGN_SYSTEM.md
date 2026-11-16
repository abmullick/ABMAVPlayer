# ABM AV Player — Windows UI Design System

**Status:** Foundation and implementation contract  
**Applies to:** Every ABM-owned Windows window, dialog, popup and workspace  
**Baseline:** Shared theme, Direct2D/DirectWrite and motion runtimes

## 1. Product philosophy

ABM AV Player has one visual identity and one interaction philosophy. The Main Player, Library, Queue, Equalizer, Settings, Advanced Settings, Help, About, Streaming Centre chrome and future workspaces must feel like parts of one coherent Windows application.

The system is not a set of unrelated window redesigns. Shared primitives and tokens are the default; local exceptions require a clear functional or platform reason.

## 2. Non-negotiable architecture boundaries

- Reuse `src/ui_theme.h/.c` as the canonical runtime palette and theme-preset source. Do not introduce per-window colour palettes or duplicate persisted theme preferences.
- Reuse `src/ui_d2d.h/.cpp` for ABM-owned custom-rendered surfaces and shared text/rendering conventions.
- Reuse `src/ui_motion.h/.cpp` for time-based, interruptible animation values.
- Reuse `src/ui_motion_win.h/.cpp` for active-only Win32 animation timer scheduling and sampling.
- Keep window procedures responsible for their own state, hit testing, layout, accessibility and painting. Shared infrastructure must not become a feature-specific controller.
- Keep LibVLC playback, the local renderer/casting backend, Streaming Centre receiver discovery and WebView2 lifecycle, Torrent Stream networking, and audio processing outside the presentation framework. UI changes must not alter their ownership or lifecycle.

## 3. Shared visual language

- Dark, cinematic surfaces with restrained depth and clear foreground/background hierarchy.
- Use the selected shared theme preset for accent colour. Do not hardcode a new accent for one window.
- Use shared typography and font sizing from the ABM UI theme definitions.
- Use consistent corner-radius tiers: compact controls, standard cards/panels, and large workspace surfaces.
- Use consistent spacing increments and alignment rules; avoid arbitrary one-off padding.
- Use borders, tonal elevation and restrained shadows to establish hierarchy. Avoid excessive glow, gradients or translucent effects that reduce readability.
- Keep media and playback as the primary content. Chrome and decoration must not compete with the media.

## 4. Shared interaction language

- Buttons, toggles, tabs, list rows, sliders, menus, tooltips, dialogs and floating panels must have consistent normal, hover, pressed, focused, disabled and selected states.
- Keyboard operation is a first-class path. Preserve existing shortcuts and provide visible focus treatment.
- Hit-test rectangles must match the rendered control geometry.
- Avoid hover-only functionality; important actions must remain discoverable by keyboard and pointer.
- Preserve native Windows text input, IME, selection, accessibility and standard window behaviour where native controls are used.
- Respect Windows reduced-motion preferences through the shared motion runtime.
- Animations must be interruptible and must not delay input or make controls temporarily unresponsive.

## 5. Motion standards

Use the common presets documented in `MOTION_SYSTEM.md`:
- QUICK: 120 ms — press/release feedback.
- STANDARD: 180 ms — compact menus and hover feedback.
- CONTENT: 240 ms — content and collection transitions.
- WINDOW: 260 ms — window/dialog transitions.
- PANEL: 280 ms — drawers and side panels.
- EMPHASIS: 350 ms — infrequent, non-critical emphasis.

Use monotonic elapsed time, not frame-count increments. Start a Win32 animation timer only while one or more values are active, target a 16 ms cadence, and stop it immediately when all values complete. Never create a permanent animation timer.

Animation ticks must not perform I/O, network work, media-engine calls, expensive metadata queries or unbounded layout. Invalidate only affected regions where practical. Do not move or resize native child surfaces such as the LibVLC video HWND on every paint-only animation frame.

## 6. Windows compatibility and resilience

- Keep the supported build architecture x64 and retain the documented MSVC C11/C++17 compatibility.
- Use the existing DPI-awareness policy and keep layout, hit testing and rendering coordinates consistent.
- Prefer standard Win32 APIs and gracefully degrade optional Windows 11 chrome effects on older supported Windows versions.
- Handle minimize/restore, resize, DPI changes, theme changes, window destruction and interrupted transitions.
- Every timer must have a unique per-window ID and be stopped during window teardown.
- Shared helpers must remain C-compatible where consumed by C window procedures.
- Do not make core application usability depend on optional DWM, animation or transparency features.

## 7. Implementation and acceptance

For each migrated window:
1. Inventory existing commands, shortcuts, controls and state before changing its presentation.
2. Map existing behaviour to shared ABM tokens and reusable components.
3. Preserve all commands and feature semantics.
4. Build with the documented x64 MSVC toolchain.
5. Test keyboard and mouse paths, reduced motion, rapid open/close, resizing, DPI scaling and teardown.
6. Check idle CPU usage, animation CPU usage and GDI/USER resource counts.
7. Run relevant playback, casting, Streaming Centre, Torrent Stream and WebView2 regression checks.

A window is not considered migrated merely because it looks consistent in a screenshot. Behavioural parity, build validation and regression testing are required.
