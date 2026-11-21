# Application-wide Background Skin System

## Purpose

ABM AV Player has one shared background-skin selection and one renderer implementation. Individual windows do not maintain separate skin palettes or selection state.

The existing seven colour-theme presets remain separate. Background skins change the ambient backdrop only; they do not replace the typography system, component styles, window layouts or interactive control behaviour.

## Available skins

1. Classic Music Player — default; preserves the original, non-animated background.
2. Ocean Flow — layered tidal-current curves and drifting surface particles.
3. Aurora — tall, independently moving luminous curtains with fine vertical folds.
4. Golden Waves — metallic orbital arcs and drifting gold glints.
5. Liquid Chrome — reflective slanted ribbons with narrow specular highlights.
6. Neon Pulse — perspective wire grid, animated signal traces and pulse nodes.
7. Deep Ocean — submerged light shafts and rising bubbles/particulate.
8. Holographic — interlaced spectral curves and a shifting vertical interference mesh.

## Shared implementation

- `src/ui_skin.h` exposes the C-compatible skin API.
- `src/ui_skin.cpp` owns the process-wide selection, palette definitions, common animation phase, background drawing and registered-window invalidation list.
- `src/ui_theme.h/.c` remains the independent colour-theme system.
- `src/ui_d2d.h/.cpp` remains the shared Direct2D/DirectWrite rendering layer.
- `build.bat` compiles and links the central renderer.

The two drawing entry points, `ABM_SkinRenderD2D` and `ABM_SkinRenderCanvas`, draw the same ambient composition into the existing HWND renderer or owner-draw canvas. Both must be called after the surface base fill and before foreground cards, controls and text.

## Integrated surfaces

- Main Player workspace and its built-in playlist/library panels; large player panels use the same renderer within their own bounds.
- Settings and Advanced Settings.
- Torrent Stream.
- Advanced Equalizer.
- Video Enhancement Studio.
- Home Theatre.
- Streaming Centre main chrome and its About/Help information surface.
- ABM-owned licensing and modal dialogs, action menu, recent-played popup, About and Help surfaces.

The active LibVLC video child and WebView2 provider content are deliberately not painted over. Native child controls retain their existing editing, keyboard, accessibility and hit-testing behaviour.

## Animation lifecycle

Auxiliary top-level windows register with `ABM_SkinRegisterWindow` when created and unregister when destroyed. The existing Main Player UI timer invalidates registered visible surfaces only while an ambient skin is selected. There are no permanent per-window animation timers. Classic Music Player skips the ambient rendering and auxiliary invalidation path.

The renderer derives its phase from monotonic Windows tick time, so all visible surfaces use the same motion phase even if their paint messages arrive at slightly different times.

## Persistence

The selected skin remains stored in `%APPDATA%\\ABMAVPlayer\\theme.ini`:

- Section: `[Appearance]`
- Key: `BackgroundSkin`
- Values: `0` through `7`

The existing Settings skin cards update the shared process selection immediately and persist the selected value. The seven colour-theme presets continue to use `ColorPreset`.

## Validation status

Source integration and build-script wiring are committed. A Windows MSVC build and runtime regression pass are still required before release sign-off, including checks for window destruction/recreation, multiple auxiliary windows, high-DPI layouts, playback, video child placement, WebView2, casting and Torrent Stream.
