# Application-wide Background Skin System

## Skin collection

The old eight-effect set has been replaced with a nine-choice collection:

### Animated
1. Classic Music Player — default, preserves the original appearance and performs no background animation.
2. Aurora Flow — slow, layered aurora curtains with gentle lateral movement.
3. Liquid Glass — floating glass-like forms with reflective edge highlights.
4. Cosmic Drift — deep-space colour fields, drifting stars and orbital detail.
5. Prism Wave — layered spectral ribbons with restrained continuous movement.

### Static artwork
6. Glass Facets — angular architectural glass composition.
7. Carbon Weave — dark technical texture with restrained edge accents.
8. Gold Geometry — warm metallic orbital geometry.
9. Minimal Horizon — understated horizon and perspective lines.

The four animated choices share one monotonic animation phase and the existing main-window timer. The five remaining choices (Classic plus four static treatments) do not request continuous animation invalidation.

## Shared implementation

- `src/ui_skin.h` defines the process-wide skin IDs and API.
- `src/ui_skin.cpp` owns selection, animation timing, artwork composition and registered-window invalidation.
- `src/ui_theme.h/.c` remains the independent colour-theme system.
- `src/ui_d2d.h/.cpp` remains the shared Direct2D/DirectWrite rendering layer.
- `build.bat` compiles and links the central renderer.

Both HWND and owner-draw canvas surfaces use the same composition functions. Skin backgrounds are painted after the base fill and before foreground cards, controls and text. The active LibVLC video child and WebView2 provider content are not painted over.

## Persistence and compatibility

The selected skin remains stored in `%APPDATA%\\ABMAVPlayer\\theme.ini` under `[Appearance]`, key `BackgroundSkin`. Values 0–8 are supported. Out-of-range legacy values fall back to Classic.

The existing seven colour-theme presets remain independent. The Settings selector exposes the four animated treatments and four static artwork treatments alongside Classic.

## Validation

The source changes are committed to `main`. A Windows MSVC build and runtime visual/performance pass are still required. In particular, verify the Settings selector, high-DPI rendering, auxiliary-window invalidation, video child placement, WebView2, casting and Torrent Stream before release.
