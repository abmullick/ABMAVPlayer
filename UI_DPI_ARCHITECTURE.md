# ABM AV Player — Per-Monitor DPI Architecture

## Status

**Foundation committed on `main`; full visual scaling migration remains in progress.**

This document distinguishes process/window DPI awareness from the separate work required to make every rendered element and child control scale correctly.

## 1. Awareness policy

- Embed `src/abm_av_player.manifest` as resource ID 1 in `src/player.rc`.
- Declare `PerMonitorV2,PerMonitor` using the Windows 10 DPI-awareness manifest schema, with the legacy `true/pm` declaration as a compatibility fallback.
- Before creating any HWND, `EnableDpiAwareness` dynamically requests `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`, then Per-Monitor V1, then system-aware mode.
- Never change process awareness after the first HWND exists.

The embedded manifest is the preferred declaration because it is active before application code creates windows. The runtime call is a best-effort compatibility fallback and must not override a manifest-established context.

## 2. Shared DPI primitives

`src/ui_dpi.h` provides C/C++-compatible inline helpers:

- `ABM_DpiGetForWindow` — query the effective DPI for an HWND, with a device-context fallback.
- `ABM_DpiGetSystem` — obtain the system DPI before the main window exists.
- `ABM_DpiScale` / `ABM_DpiUnscale` — convert between 96-DPI layout units and physical pixels.
- `ABM_DpiApplySuggestedRect` — apply the rectangle supplied by `WM_DPICHANGED`.

Use these helpers at Win32 HWND boundaries rather than duplicating DPI arithmetic in each module.

## 3. Window transition contract

A top-level window receiving `WM_DPICHANGED` must:

1. Apply the suggested outer rectangle supplied in `lParam`.
2. Recompute its client-area dimensions after the resize.
3. Re-layout child HWNDs and custom-drawn geometry from canonical 96-DPI layout units.
4. Recreate or resize DPI-dependent rendering resources and fonts.
5. Recompute hit-test rectangles and popup/overlay placement.
6. Invalidate the affected surface and restore focus/interaction state.

The main player now stores its layout and hit-test rectangles in canonical 96-DPI logical units. Its Direct2D render target receives the effective monitor DPI, its paint clip/client dimensions are converted to logical units, mouse coordinates are converted before hit-testing, partial invalidation rectangles are converted back to physical pixels, and the LibVLC child HWND is positioned/sized in physical pixels. On monitor changes it applies the suggested rectangle, updates the render-target DPI, recalculates layout and resizes the target. The Settings, Advanced Settings, Torrent, Help, About, Equalizer, licensing dialog, recent popup, fullscreen overlay and Streaming Centre windows now apply the suggested rectangle and invalidate/re-run their existing size/layout paths where available.

**Important:** handling the outer rectangle is not proof that every child control, font, hit target or painted element is correctly scaled. The fixed-coordinate surfaces still require dedicated migration and Windows validation.

## 4. Layout coordinate rules

- Author new layout constants in 96-DPI logical units.
- Convert to physical pixels only when creating or positioning Win32 child HWNDs.
- Convert mouse/touch coordinates to the same logical coordinate space before hit testing.
- Do not mix physical-pixel rectangles with DIPs in the same layout calculation.
- Keep Direct2D render-target DPI, DirectWrite font sizes and HWND child geometry under one explicit policy per surface.
- Avoid repeated scaling: store canonical logical geometry and derive physical geometry from the current window DPI.

The current Direct2D bridge historically fixes the HWND render target at 96 DPI because its drawing and hit-test coordinates were authored in client pixels. Do not simply change that target to the monitor DPI without first migrating that surface's layout and input coordinates to DIPs; doing so would scale drawings twice or misalign them with child HWNDs.

## 5. Migration sequence

1. Main player: **logical-coordinate migration implemented** for layout, Direct2D painting, hit-testing, partial invalidation and LibVLC child HWND geometry. Verify mixed-monitor behaviour and review any remaining direct Win32 calls that consume main-player rectangles.
2. Settings and Advanced Settings: scale all owner-drawn rectangles, input HWNDs, labels, click regions and minimum window geometry from one canonical layout.
3. Torrent Stream: migrate task cards, native child controls, list rows, sliders, and TV/device panels.
4. Streaming Centre: migrate browser chrome, WebView2 controller bounds, device panel and custom hit regions; do not alter provider page content or casting ownership.
5. Help, About, Equalizer, licensing, menus and popups: migrate each surface and verify screen-edge placement.
6. Add automated geometry assertions where possible and complete the 100%, 125%, 150% and 200% mixed-monitor matrix.

## 6. Build and validation

- Build from the repository root so the resource compiler resolves `src/abm_av_player.manifest` from `src/player.rc`.
- Inspect the built executable to confirm resource ID 1 contains the application manifest.
- Verify process awareness is Per-Monitor V2 on supported Windows versions.
- Move each top-level window between monitors with different scale factors.
- Check window bounds, child HWND positions, Direct2D alignment, font metrics, hit targets, fullscreen transitions, popups and WebView2 bounds.
- Test startup on a system where Per-Monitor V2 is unavailable to confirm the runtime fallback path.

Do not mark the DPI work release-complete until the visual and interaction matrix passes on Windows.


## Advanced Settings logical-DPI migration

The Advanced Settings window now uses the shared per-monitor DPI architecture for its Direct2D surface. Its 1080 × 780 design coordinates remain in 96-DPI logical units; the top-level window dimensions and minimum tracking size are scaled for the owner monitor, and the Direct2D render target receives the active window DPI. Pointer coordinates are converted to logical units before navigation and option hit-testing. The diagnostic preview EDIT child is positioned and sized in physical pixels and its Consolas font is recreated at the destination DPI when the window moves between monitors. WM_SIZE resizes the render target using the physical client dimensions.

This is the first Advanced Settings DPI pass. Other legacy/native child surfaces and the standard Settings window remain separate migration work; do not treat the entire application as fully DPI-migrated until those passes and runtime checks are complete.


## Standard Settings logical-DPI migration

The standard Settings window now follows the same 96-DPI logical-coordinate model. Its 820 × 1020 design size and minimum tracking dimensions are scaled from the owner monitor DPI. The Direct2D target is initialized and updated with the window DPI; native EDIT child controls are created and repositioned in scaled physical pixels, and their Segoe UI font is recreated at the destination DPI. The Settings window converts pointer coordinates to logical units before applying its existing hit targets. Existing settings values, save actions, browse actions, theme preset behaviour and Advanced Settings navigation are unchanged.

This pass covers the six native EDIT controls and the Direct2D-rendered Settings surface. It does not by itself certify every legacy dialog in ABM AV Player as DPI-complete; each remaining surface still requires its own migration and runtime validation.


## Utility windows, Torrent Stream and popup DPI migration

The Torrent Stream manager now uses the active DPI for its Direct2D target, keeps its drawing/layout model in 96-DPI logical units, and scales child HWND placement through a single layout helper. Its child control font is recreated and reapplied on monitor transitions; minimum tracking dimensions and initial window sizing are DPI-aware.

About and Equalizer now scale their GDI drawing coordinate space to the monitor DPI. Their window creation sizes and minimum tracking dimensions are DPI-aware, and Equalizer pointer input is converted to logical units before preset/slider hit-testing and dragging.

The Help Centre scales its top-level dimensions, native search/topic/article controls, topic row height and child-control layout. Its custom-painted chrome uses logical GDI coordinates, and close/title hit testing is converted from physical pixels. Its search, topic navigation, article formatting and keyboard commands remain unchanged.

ABM-owned modal notification dialogs, action menus and Recently Played popups now scale their top-level dimensions and custom GDI drawing to the monitor DPI. Their pointer hit testing uses logical coordinates; modal dialog button geometry is recalculated after a DPI transition. The fullscreen overlay remains a monitor-sized layered surface and is repositioned by the fullscreen owner.

These changes are code-level migrations, not runtime certification. Validate mixed-DPI transitions, control focus, text clipping, popup placement near work-area edges and all interaction paths on real Windows displays before declaring the DPI pass complete.


## Remaining application surfaces: DPI implementation pass

The remaining utility and popup surfaces have been brought into the shared per-monitor DPI model:

- Torrent Stream: logical layout calculations, scaled child-window placement, DPI-aware Direct2D target, monitor-specific control font, owner-drawn list/combo text sizing, row metrics and minimum size.
- About: logical GDI drawing coordinates, DPI-scaled window dimensions and minimum tracking size.
- Equalizer: logical GDI drawing coordinates, DPI-scaled creation/centering/minimum size, and logical-coordinate slider/preset input.
- Help Centre: scaled top-level size, child control creation and resize layout, topic row metrics, monitor-specific control fonts, logical painted chrome, and logical close/title hit testing.
- ABM modal notifications, custom action menus and Recently Played popup: scaled popup dimensions and GDI drawing, logical pointer hit-testing, recalculated dialog buttons, and popup resize on monitor DPI changes.

This is an implementation pass rather than a certification. In particular, validate Torrent's custom slider subclasses and owner-drawn controls, Help's rich-edit formatting and child fonts, Equalizer's GDI back-buffer path, and popup work-area clamping on mixed-DPI monitors. Some native system-owned dialogs and the fullscreen layered overlay continue to use their own Windows/owner-managed sizing paths.
