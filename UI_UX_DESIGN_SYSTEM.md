# ABM AV Player — UI/UX Design System

**Status:** Canonical typography and five-level surface tokens established; full runtime visual verification pending  
**Baseline branch:** `main`  
**Baseline commit:** `eb8cd7415cb403c9bb05244ba67ed36efc8eda89`  
**Scope:** Visual consistency and interaction conventions only. This step does not alter playback, casting, torrent, streaming, networking, or updater behaviour.

## 1. Audit summary

The current application already has a recognisable charcoal identity, a split ABM / AV PLAYER wordmark, golden-yellow signature accents, rounded cards, and Segoe UI typography. The Direct2D renderer uses DirectWrite and a 96-DPI coordinate space so its layout coordinates align with Win32 client-area pixels.

The main issue is not a lack of styling. It is that visual decisions are duplicated across windows and rendered through different technologies.

| Surface | Current implementation | Audit observation |
|---|---|---|
| Main player | Win32 + Direct2D/DirectWrite; LibVLC video in a child HWND | Has its own palette and shared text-style indices; video child-window layering constrains overlays |
| Settings | Separate Direct2D context plus native Win32 EDIT controls | Fixed-coordinate layout and separately defined colours; custom-painted controls coexist with native text inputs |
| Torrent Stream | Separate Direct2D context plus native Win32 child controls | Strong branded cards, but its own palette and fixed vertical layout calculations |
| Streaming Centre | Win32/GDI chrome, native controls, WebView2 content | Uses a similar brand treatment but a different renderer, font helper and colour definitions |
| Equalizer / auxiliary dialogs | Win32 window procedures and custom drawing | Needs a focused follow-up inventory during its dedicated redesign step |

### Confirmed inconsistencies to address in later steps

1. **Multiple rendering paths:** Direct2D/DirectWrite is used by the main player, Settings and Torrent; Streaming Centre chrome is GDI-based. This is a source of visual and behaviour differences, not automatically a reason to rewrite the Streaming Centre.
2. **Duplicated palettes:** The main player, Settings, Torrent and Streaming Centre each declare local colour values. Similar navy/teal colours are close but not centrally governed.
3. **Mixed controls:** Custom-painted controls coexist with native EDIT, LISTBOX, BUTTON and other child controls. Their focus, padding, sizing, hover and disabled states can differ.
4. **Typography is not fully centralised:** The Direct2D renderer has body/small/heading/brand formats; Streaming Centre creates cached Segoe UI and MDL2 Assets fonts independently.
5. **Layout conventions differ:** Main player, Settings and Torrent have hand-authored coordinate layouts. The Settings and Torrent surfaces need systematic resizing and DPI review in their own implementation steps.
6. **Interaction states are inconsistent:** Some surfaces have explicit pulse/hover/focus treatments, while other states are implemented by their individual window procedures.

## 2. Shared design principles

- **Video first:** The playback canvas remains the dominant visual surface. Controls must not obscure video unnecessarily.
- **One product, multiple workspaces:** Main Player, Streaming Centre, Torrent, Settings and auxiliary windows share the same brand, typography, spacing and interaction vocabulary.
- **Dark, restrained, legible:** Use the five fixed charcoal surface levels, ABM signature gold, clear text contrast and subtle borders. Avoid excessive gradients, glow and glass effects.
- **ABM gold is the permanent signature accent:** Theme presets may use a secondary hue for decorative elements; orange, warning yellow, success green and destructive red retain their semantic meanings.
- **Clear action hierarchy:** Primary, secondary, quiet and destructive actions must be visually distinguishable.
- **State is explicit:** Hover, pressed, keyboard focus, disabled, loading, success, warning and error states must be designed rather than improvised.
- **Motion is purposeful:** Animation must be short, subtle, interruptible and never compromise playback or input responsiveness.
- **Preserve native behaviour:** Keyboard access, focus order, text selection, accessibility and familiar Windows interactions take precedence over purely decorative treatment.

## 3. Shared design tokens

The shared runtime palette is implemented in `src/ui_theme.h` and `src/ui_theme.c`. App-owned native windows resolve their static colors through runtime tokens; the Streaming Centre welcome page resolves its CSS colors from the same palette. Provider-owned WebView2 pages retain provider styling. Procedurally generated media-art colors remain content-driven rather than interface theme colors.

### Colour palette

| Token | Value | Intended use |
|---|---|---|
| Token | Value | Intended use |
|---|---|---|
| Level 1 — Background | `#151820` | Application and workspace background |
| Level 2 — Navigation / panel | `#1D222D` | Navigation, sidebars and primary panels |
| Level 3 — Card / container | `#272E3A` | Cards, inputs and nested containers |
| Level 4 — Interactive | `#343D4C` | Hover, pressed and active surfaces |
| Level 5 — ABM signature | `#E9B949` | Brand accent, active indicators and focus emphasis |
| Primary text | `#F7FAFF` | Titles and important values |
| Secondary text | `#E2EEFA` | Body copy |
| Muted text | `#91A0B5` | Metadata and supporting labels |
| Disabled text | `#5B6C80` | Disabled controls |
| Theme secondary accent | Preset-derived | Decorative secondary accents only |
| Success | `#4DC499` | Completed/healthy state |
| Warning | `#F2B14F` | Caution and recoverable issues |
| Danger | `#DC4B55` | Destructive actions and errors |
| Border | Shared neutral border token | Subtle card boundaries |
| Focus | Shared focus token | Keyboard focus ring |

**Usage rules:** do not use signature gold for large blocks of text or large low-contrast fills; maintain readable contrast; do not communicate status by colour alone; destructive actions must not use the primary teal treatment.

### Typography

| Role | Target size | Weight | Use |
|---|---:|---|---|
| Caption / metadata | 12 | Medium / semibold | Hints, metadata, section labels |
| Body | 14 | Regular | Standard content |
| Large body | 16 | Regular | Important descriptions and controls |
| Section title | 18 | Semibold | Card and group headings |
| Window title | 24 | Semibold | Main workspace heading |
| Brand display | 30 | Bold | ABM wordmark |

Preferred families: **Segoe UI Variable Text** for UI copy and **Segoe UI Variable Display** for headings. Use platform font fallback gracefully. Avoid using all-caps for long descriptions; reserve it for short section labels.


## 3.1 Canonical runtime typography contract

The Main Player's DirectWrite typography is the authoritative runtime reference. The implementation is centralised in `src/ui_theme.h`, `src/ui_d2d.h` and `src/ui_d2d.cpp`:

- `ABM_D2D_TEXT_BODY`: Segoe UI Variable Text, 16.5 px, regular, 1.38× line spacing.
- `ABM_D2D_TEXT_SMALL`: Segoe UI Variable Text, 12.5 px, medium, 1.30× line spacing; use muted semantic text colours for metadata.
- `ABM_D2D_TEXT_HEADING`: Segoe UI Variable Display, 30 px, semibold, 1.16× line spacing.
- `ABM_D2D_TEXT_BRAND_MAIN`: Segoe UI Variable Display, 32 px, bold, 1.12× line spacing.
- `ABM_D2D_TEXT_BRAND_SUFFIX`: Segoe UI Variable Text, 18 px, medium.
- `ABM_D2D_TEXT_ICON`: Segoe MDL2 Assets, 16 px, regular. The family name is sourced from the shared theme token; avoid per-window icon-font literals.

Both the HWND renderer (`ABM_D2D`) and owner-draw canvas (`ABM_D2D_CANVAS`) now create their formats through the same `CreateCanonicalTextFormats` implementation and use the same semantic role identifiers. This prevents the two DirectWrite paths from silently drifting in family, size or weight. The shared formats now apply explicit, consistent DirectWrite line spacing. Metadata uses medium rather than semibold weight, and important media titles use the larger 30 px heading role. The Main Player and secondary canvases continue to consume the same canonical format factory. Playback transport controls and Streaming Centre now render their icon glyphs through the shared Segoe MDL2 Assets family and shared icon-font token; bespoke brand artwork and decorative line illustrations remain vector artwork by design.

New ABM-owned Direct2D surfaces must use these semantic roles; they must not create independent DirectWrite formats or hard-code font family, size or weight. Native GDI/EDIT/RichEdit paths are not considered migrated merely because they use the same font family. Those controls remain migration work until replaced or hosted through a renderer that preserves editing, accessibility and keyboard behaviour.

## 3.2 Canonical shape and control tokens

The runtime tokens in `ui_theme.h` are the source of truth for surface, input, text, accent, border, focus, spacing, control/button/card/panel radius and compact/standard/primary control heights. New controls must consume these tokens rather than declaring local radii, border colours or heights. The existing windows still contain local drawing routines; they must be migrated incrementally to shared control primitives before visual parity can be claimed.

### Spacing

Use a 4-unit base scale: **4, 8, 12, 16, 20, 24, 32**. Align card padding, label/control gaps, section spacing and button groups to this scale. Avoid arbitrary one-off gaps unless a measured layout constraint requires them.

### Shape and sizing

- Control radius: 6
- Button radius: 8
- Card radius: 12
- Large panel radius: 16
- Compact control height: 30
- Standard control height: 36
- Primary action height: 40

These are starting targets, not a mandate to resize every existing control in this step. Touch and high-DPI use may require larger hit targets.

## 4. Component and interaction standards

### Buttons

| Variant | Visual treatment | Use |
|---|---|---|
| Primary | Teal fill, high-contrast label | One main action per panel |
| Secondary | Raised navy surface, subtle border | Supporting actions |
| Quiet | Minimal surface, clear hover/focus | Navigation and low-emphasis actions |
| Destructive | Red treatment | Remove, delete, stop where destructive |
| Disabled | Muted surface and text | Unavailable actions |

Every button needs normal, hover, pressed, keyboard-focus and disabled states. Loading actions need a visible progress/busy state and must prevent duplicate activation where appropriate.

### Inputs and selectors

- Consistent height, inset background, border and padding.
- Visible focus border and keyboard focus.
- Preserve native text editing, selection, clipboard and accessibility behaviour.
- Validation text appears close to the affected field.
- Never rely on placeholder text as the only label.

### Cards and panels

- Use the shared surface and border tokens.
- Keep card headings, supporting text and actions aligned consistently.
- Reserve gradients for branded mastheads or a small number of primary surfaces; avoid a gradient on every card.
- Separate content sections with spacing first and borders second.

### Status and progress

- Pair status colour with a text label or icon.
- Distinguish transfer progress from playback buffer/cache availability.
- Use determinate progress only when a real value exists; otherwise use an indeterminate activity indicator.
- Keep status updates stable and avoid rapid layout shifts.

### Keyboard and accessibility

- All interactive controls must have keyboard focus and predictable tab order.
- Focus must be visible against dark surfaces.
- Hover must not be the only way to reveal an action.
- Use descriptive accessible names for icon-only controls.
- Support Windows text scaling and DPI without clipped labels or inaccessible hit targets.

## 5. Adoption plan

This step adds the shared tokens and records the audit. It deliberately does **not** mass-replace colours or controls in existing windows.

| Later step | Adoption work |
|---|---|
| Step 2 — Main navigation | Apply tokens to application navigation and Command Centre |
| Step 3 — Main player | Align playback controls, drawer, timeline and fullscreen states |
| Step 4 — Streaming Centre | Align GDI chrome and native device panel with the shared visual rules without disturbing WebView2/casting |
| Step 5 — Torrent | Replace local palette values and standardise task/status hierarchy |
| Step 6 — Settings | Standardise inputs, buttons, labels and responsive layout |
| Step 7 — Auxiliary windows | Apply the same system to Equalizer, About, Help and updater |
| Step 8 — Home | Compose the new dashboard from the shared components |
| Step 9 — Motion | Add consistent transitions and feedback |
| Step 10 — Validation | Verify DPI, keyboard, focus, contrast, layout and regression behaviour |

## 5.1 Follow-up audit — global theme propagation and remaining palette references

**Implementation commits on `main`:**
- `7e8f801` — Media Library repaints its parent and native child controls on `WM_THEMECHANGED`; row, input and text treatments now use semantic tokens for the principal surface/text roles.
- `4745148` — a theme preset change in Settings is propagated to every top-level ABM window on the UI thread, rather than refreshing only the main window.
- `89094ce`, `2e8a092`, `87aab3f`, `5348a16` — remaining modal/popup/floating surfaces refresh on theme changes; child controls are explicitly redrawn.
- `3202425`, `394bc4c`, `1df6b24` — Home Theatre, Streaming Centre and Licensing refresh their child controls and cached brushes.

### Theme-change coverage reviewed

| Window / surface | Theme-change response |
|---|---|
| Main Player and fullscreen overlay | Refresh cached main brush and repaint overlay |
| Settings and Advanced Settings | Rebuild cached input/report brushes and redraw child controls |
| Help and About | Repaint; Help also updates cached edit/navigation brushes |
| Equalizer and Video Enhancement | Repaint their Direct2D canvases |
| Command Centre, recent-media popup and ABM modal dialog | Repaint their Direct2D canvases |
| Torrent Stream | Rebuild cached surface/control brushes, refresh DWM chrome and repaint children |
| Streaming Centre and Streaming Help/About | Rebuild cached panel brushes and repaint native children |
| Home Theatre | Rebuild its cached surface brush and repaint children |
| Media Library | Repaint all child controls; owner-draw and native control colours resolve from semantic tokens |
| Licensing | Rebuild its edit brush and repaint children |

Settings broadcasts the theme event to all top-level windows on its UI thread. This is application-level propagation; it does not depend on Windows independently broadcasting a system theme change. Provider-owned WebView2 pages and media artwork remain outside the application palette.

### Remaining colour audit findings

- The canonical literal RGB palette remains centralised in `src/ui_theme.c` and the semantic aliases in `src/ui_theme.h`; the inspected secondary-window source files did not introduce new standalone `RGB(...)` palette definitions.
- Some older drawing code still references named `ABM_THEME_COLOR_*` IDs directly instead of semantic roles. These IDs resolve through the shared theme system, so they are not separate literal palettes, but they make per-surface intent harder to audit.
- The most visible remaining direct palette-ID clusters are the Home Theatre and Media Library masthead/logo treatments, Licensing's branded hero/buttons, and legacy Main Player/Streaming/Torrent owner-draw states. Status colours (success, warning, danger), artwork/content colours and fixed white/black drawing details are intentional exceptions where semantics require them.
- This is a focused source audit of ABM-owned window procedures and render paths, not a claim that every individual colour expression in the 693 KB player source has been migrated. Continue replacing direct IDs with semantic tokens incrementally; do not blindly replace status, artwork, subtitle or content-derived colours.

**Validation limitation:** the repository connector can update and inspect source, but no Windows desktop/runtime is attached here. Theme switching has been verified at source level only; live visual checks, GDI handle checks and a full MSVC build remain outstanding.

## 6. Non-goals and safeguards

- No playback-engine or LibVLC changes.
- No changes to local-player casting, Streaming Centre receiver discovery, Torrent Stream discovery or their independent selection state.
- No changes to torrent download, persistent storage, cache, buffering or playback behaviour.
- No WebView2 navigation, profile, DRM or provider-session changes.
- No broad conversion of GDI/native controls to Direct2D without a dedicated, tested step.
- No visual overhaul of all windows in one commit.

## 7. Step 1 acceptance criteria

- [x] Audit current UI implementation paths and shared rendering primitives.
- [x] Record the existing design inconsistencies and constraints.
- [x] Define shared colour, typography, spacing, radius and control-size tokens.
- [x] Add a C/C++-compatible token header for gradual adoption.
- [x] Keep runtime UI and media behaviour unchanged in this step.
- [ ] Build and launch on Windows to confirm the additive header/documentation commit does not affect the existing build. This requires local Windows validation.

## Step 2 — Main window and Command Centre navigation

**Implementation commit:** `d13e992ace126eeaf8137555d5cd10395aef850a`

The main Command Centre overlay has been refined without changing its command routing or keyboard shortcuts.

- Widened the panel from 356 to 410 layout units to give action labels and shortcut hints more room.
- Reworked the header into a clear title and supporting navigation descriptor.
- Replaced the previous mixed blue/purple gradients with restrained navy surfaces and teal interaction states.
- Standardised command rows, icon tiles, borders and shortcut-key pills.
- Kept Streaming Centre and Torrent Stream as featured destinations, with teal and warm amber identities respectively.
- Added subtle dividers between existing command groups.
- Preserved all 18 menu entries, their order, click handlers and keyboard shortcuts.
- Preserved existing menu hit rectangles, hover tracking and keyboard selection behaviour; only their panel width and visual presentation changed.

### Step 2 validation status

- [x] Source updated on `main`.
- [x] Existing menu labels, ordering, action indices and shortcut labels retained.
- [x] Existing click dispatch and keyboard navigation code left unchanged.
- [ ] Windows build and runtime visual validation still required.
- [ ] Verify menu fit at minimum window size and at 100%, 125%, 150% and 200% display scaling.

No playback, playlist, local casting, Streaming Centre, torrent, WebView2, settings or updater implementation was changed in this step.


## Step 3 — Main playback surface

**Implementation commit:** `5b1396421873973a760849cc53a06d28480729a2`

The main playback surface has received a focused visual refinement.

- Reworked the seek row into a clear elapsed/total time presentation with a more legible progress track and playhead.
- Refined the transport dock surface, top-edge accent, rim and shadow while retaining its existing footprint and control positions.
- Tightened the Now Playing metadata spacing and hierarchy.
- Removed a duplicated drawer-frame stroke and retained a single restrained inset outline.
- Preserved the existing seek rectangle, transport rectangles, button hit targets, LibVLC calls, playlist actions, drawer state and receiver-selection behaviour.
- No playback engine, media parsing, output discovery, casting or torrent code was changed.

### Step 3 validation status

- [x] Source updated on `main`.
- [x] Seek and transport geometry retained.
- [x] Existing playback and drawer interaction paths retained.
- [ ] Windows build and runtime visual validation still required.
- [ ] Verify short and long track durations, paused/playing states, seek dragging, drawer open/close and fullscreen transitions.
- [ ] Verify at supported window sizes and 100%, 125%, 150% and 200% display scaling.

The fullscreen overlay's existing input forwarding and media control routing were intentionally left unchanged in this focused pass; validate the fullscreen transition and controls as part of the regression checklist.



## Step 5 — Torrent Stream

**Implementation commit:** `948d404910e6003106f191207892d573fa0db011`

A focused visual pass brings the Torrent Stream window closer to the shared ABM design system and makes its live task state easier to scan.

- Included the shared `ui_theme.h` tokens in the native player translation unit and adopted the shared background, border, muted-text, accent and primary-text tokens in the Torrent Stream surface.
- Replaced the static `TORRENT ENGINE` masthead label with a live status chip reflecting the current task state: queued, checking, downloading, complete, seeding, paused or error; when no task is active it reads `ENGINE READY`.
- Used the existing task-information API only to read the current state for painting. No task lifecycle, engine call sequence or state transition was changed.
- Added a concise instruction to the content-library heading so the next action is clear: select a media file to download, play or cast.
- Preserved the existing content-list, playback, bandwidth, cache and TV-control layout and all child-control hit targets.

### Step 5 validation status

- [x] Source updated on `main`.
- [x] Shared theme tokens adopted by the Torrent Stream paint path.
- [x] Status chip reads the existing task state without changing it.
- [x] Torrent task, download, persistent storage, cache, playback, buffering, TV discovery and casting handlers left unchanged.
- [ ] Windows build and runtime visual validation still required.
- [ ] Verify the status chip with no task, queued/checking, downloading, complete/seeding, paused and error states.
- [ ] Verify content-list selection, Play / Stream, pause/resume, Stop, Find TVs, Cast to TV, Stop TV, cache controls and Open Downloads Folder.
- [ ] Verify resize/maximize and 100%, 125%, 150% and 200% display scaling.

## Step 4 — Streaming Centre

**Implementation commit:** `795c67612a37b65cd62a139da7a67e4f69627f00`

A focused native-chrome refinement makes the service chooser read as a distinct section while keeping the WebView2 viewport and TV panel geometry intact.

- Added a clear `YOUR STREAMING SERVICES` section label and a short official-site/sign-in hint above the provider cards.
- Added a subtle divider to separate browser navigation from service selection.
- Rebalanced the six provider cards to a shorter, consistent height; adjusted logo scale, service-label baseline and active underline to fit the new card geometry.
- Kept all six provider URLs, service indices, click rectangles, hover state and active-service selection logic unchanged.
- Kept the 260px native-chrome reservation and WebView2 bounds unchanged.
- Left Find TVs panel controls, receiver discovery, app launch/stop, Cast video, WebView2 profile, navigation, fullscreen and process recovery untouched.
- Retained the existing GDI rendering path for this window; no broad renderer conversion was attempted.

### Step 4 validation status

- [x] Source updated on `main`.
- [x] Provider names, URLs, ordering and click hit rectangles retained.
- [x] WebView2 viewport reservation and receiver-panel implementation retained.
- [ ] Windows build and runtime visual validation still required.
- [ ] Verify all six provider cards at the minimum supported width and a maximized window.
- [ ] Verify hover/active states and provider navigation for every card.
- [ ] Verify Find TVs panel, Cast video and fullscreen after the chrome change.


## Step 6 — Settings UI refinement

**Implementation branch:** `feature/settings-ui-step6`

The Settings window now consumes the shared UI design tokens for its core visual components.

- Replaced the Settings window's independently defined palette with aliases to the shared background, surface, input, text, accent and border tokens.
- Refined section cards with consistent card radius, restrained borders and a small teal section marker.
- Standardised edit-field frames against the shared input and border colours.
- Simplified button treatment to a restrained solid teal primary action and raised-surface secondary action.
- Aligned checkbox sizing, fill, outline and checkmark colours with the shared control tokens.
- Kept native EDIT controls for familiar Windows text selection, keyboard editing and clipboard behaviour.
- Preserved every existing setting, control ID, coordinate/hit target, Save/Close action, validation rule, persistence call, restart warning and update-check action.
- No playback, audio processing, torrent storage/network, local casting, Streaming Centre, WebView2 or updater behaviour was changed.

### Step 6 validation status

- [x] Settings palette now consumes shared design tokens.
- [x] Section cards, edit frames, buttons and checkboxes use the common visual vocabulary.
- [x] Existing setting IDs, values, actions and persistence paths retained.
- [ ] Windows MSVC build and runtime visual validation required.
- [ ] Verify edit focus, keyboard navigation, text selection, dropdown interaction and Save/Close.
- [ ] Verify all sections and controls at 100%, 125%, 150% and 200% display scaling.
- [ ] Verify storage restart warning, invalid-value messages, update check and settings persistence.

This is a focused visual refinement. The current Settings window remains a single-page layout; category navigation and a fully responsive reflow are not introduced in this step.





### Recent media popup refinement

- Replaced the native white `TrackPopupMenu` recent-items list with a custom dark ABM popup, matching the home dashboard palette and typography.
- Added hover highlighting, keyboard navigation (Up/Down, Enter, Esc), filename ellipsis and a recent-item count.
- Recent history is pruned on startup to remove unavailable paths and duplicate entries; valid files retain most-recently-played ordering.
- Both the dashboard card, Command Centre action and Ctrl+J use the same popup. The existing play-from-recent behaviour is preserved.
- [ ] Validate popup placement near screen edges, mouse selection, keyboard selection, and behaviour with 1 and 12 recent files on Windows.
## Step 8 — Home dashboard

**Implementation commit:** `b5e6853aab1070cc8c0b48b569c406890ddc90ac`

The idle home stage now uses an action-led dashboard rather than a decorative-only album-sleeve illustration.

- Added four interactive destinations: **Open media**, **Streaming Centre**, **Torrent Stream**, and **Recent media**.
- Each card has a clear title, supporting description, consistent dark surface, restrained teal treatment, hover feedback and a generous click target.
- Card geometry is derived from the video-stage dimensions and recalculated with the main-window layout, rather than relying on fixed screen coordinates.
- The dashboard cards appear only when no local media is active and the media drawer is closed; the existing now-playing/artwork/visualizer experience remains intact.
- Actions route to the existing file picker, Streaming Centre, Torrent Stream window and recent-media menu. No new playback, torrent, streaming, discovery or casting implementation was introduced.
- Existing transport dock, command menu, media drawer, fullscreen controls and keyboard shortcuts remain unchanged.

**Windows validation checklist**

1. Launch with no media and activate all four dashboard cards.
2. Verify Open media opens the existing file picker; Streaming Centre and Torrent Stream open their existing windows; Recent media shows the existing recent-items menu.
3. Resize the main window through supported sizes and confirm cards remain inside the video stage without colliding with the hero copy or fullscreen control.
4. Add and play media; confirm the dashboard cards disappear and the existing artwork/now-playing view is unchanged.
5. Open the media drawer and command menu; confirm their hit targets and rendering remain unaffected.
6. Check 100%, 125%, 150% and 200% Windows display scaling.

- [x] Add interactive home destinations using existing application actions.
- [x] Keep the idle dashboard separate from active playback presentation.
- [ ] Build with the Windows MSVC toolchain and perform runtime visual/regression checks.
## Step 7 — Auxiliary windows

**Implementation branch:** `feature/auxiliary-windows-step7`

A focused visual alignment pass brings the Equalizer, About and Help Centre closer to the shared ABM design system.

- Replaced the Equalizer's legacy blue/purple palette with shared background, surface, border, text and accent tokens. The active EQ control uses the orange attention accent; the frequency curves and controls remain in their existing positions.
- Updated Equalizer typography to the shared Segoe UI Variable families.
- Aligned About and Help Centre surfaces, header accents, borders and text colours with the shared palette.
- Preserved the Equalizer's 10-band gain calculations, preamp range, five preset curves, audio processing calls, preference persistence, mouse interaction and keyboard close behaviour.
- Preserved About portrait loading, creator/contact/attribution content and Help guide content, navigation, scrolling and shortcuts.
- Inspected the updater: it uses standard Windows message boxes for release notes, confirmation and errors rather than a dedicated custom window. Those prompts and all release/download/SHA-256/UAC flows were left unchanged.
- No playback, streaming, casting, torrent, WebView2 or updater implementation was changed.

### Step 7 validation status

- [x] Auxiliary-window palette and typography changes committed on the feature branch.
- [x] Equalizer DSP, presets, gain ranges and preference-save calls left unchanged.
- [x] About and Help content and interaction paths retained.
- [ ] Windows MSVC build and runtime visual validation required.
- [ ] Verify all five EQ presets, preamp and all ten bands; close/reopen and confirm preferences persist.
- [ ] Verify About portrait/fallback, text fit and Help scrolling/close at 100%, 125%, 150% and 200% display scaling.
- [ ] Inspect updater UI separately before any visual changes; update/download/hash-verification behaviour is intentionally out of scope for this pass.


## Product-wide rule — No legacy-looking native popups

**This is a standing product requirement for all future ABM AV Player UI work.**

> No legacy-looking Windows popup menus or message boxes in the application's normal user experience.

- Application-owned context menus, action menus, recent-media lists, notifications, alerts, confirmations and error surfaces must use ABM-styled custom UI consistent with the shared dark navy / teal design system.
- Do not introduce new uses of Win32 `CreatePopupMenu`, `TrackPopupMenu` or `MessageBox` for normal application interactions.
- Existing native popup menus and message boxes are technical debt and should be replaced incrementally, preserving their existing actions, keyboard behaviour, accessibility, focus handling and safety confirmations.
- Native Windows file and folder pickers may remain as an explicit temporary exception where the familiar OS picker provides meaningful usability or accessibility value. They should not be confused with application-owned popup menus or message boxes.
- System-owned prompts that the application cannot or should not skin (for example, UAC consent) remain native.
- Every custom popup/dialog must handle keyboard navigation, Escape/cancel, focus restoration, screen-edge placement, DPI scaling and long text without clipping.
- A UI step is not complete until its affected popup/dialog paths have been checked for accidental native fallbacks.

**Audit baseline (current source inventory):** `src/player.c` still contains native `MessageBoxW`, context-menu `CreatePopupMenu` / `TrackPopupMenu`, common file dialogs and `SHBrowseForFolderW`; `src/updater.cpp` contains three native `MessageBoxW` calls. The custom recent-media popup is the first replacement, not completion of the audit. Track remaining replacements as explicit work items and do not claim the application is free of native popups until a source-wide audit and Windows runtime validation confirm it.


## Native popup and dialog cleanup — implementation pass

**Implementation status:** Source changes committed to `main`; Windows build and runtime validation are still required.

- Added a reusable ABM-styled modal dialog surface and routed player-owned `MessageBoxW` call sites through it. It supports OK and Yes/No results, warning/error/attention accents, wrapped long text, Escape cancellation and the original default-No behaviour for destructive confirmations.
- Exposed the same dialog entry point to `updater.cpp`; updater release, error and confirmation messages now use the ABM dialog rather than native MessageBox UI.
- Replaced both player-owned `CreatePopupMenu` / `TrackPopupMenu` implementations: playback options and playlist actions now use a custom ABM action menu. Playback actions and LibVLC command IDs are retained; long menus are constrained to the work area and support mouse-wheel and keyboard scrolling/navigation.
- Recent-media empty-state now uses the branded dialog; the recent-media list itself uses the custom ABM popup implemented earlier.
- Replaced the legacy `SHBrowseForFolderW` torrent settings folder browser with the modern Windows `IFileOpenDialog` folder-selection interface.
- Native Windows file open/save pickers (`GetOpenFileNameW` / `GetSaveFileNameW`) remain intentionally native for now; they are system file-selection workflows, not application-owned popup menus or message boxes.
- Source audit of `src/player.c` and `src/updater.cpp`: no direct native `MessageBoxW`, `CreatePopupMenu` or `TrackPopupMenu` call sites remain. Streaming host source was also checked for these legacy APIs.

**Windows validation still required:**

1. Build with the project's Windows MSVC toolchain; verify the COM folder picker symbols/linkage (`uuid.lib`) and C/C++ linkage for `ABM_ShowMessageBox`.
2. Exercise every Yes/No confirmation, including destructive actions with default No; verify Escape cancels and Enter follows the configured default.
3. Test very long error text, popup placement at all screen edges, keyboard and mouse selection, and long playback menus with scrolling.
4. Test torrent cache/download folder selection and cancellation.
5. Run the updater check, update-available confirmation and error paths.
6. Regression-test playlist, audio tracks, subtitles, playback speed, EQ presets, ReplayGain, compressor, shuffle and repeat.
7. Verify 100%, 125%, 150% and 200% DPI scaling and multi-monitor positioning.

Do not mark this cleanup complete until the Windows build and the above runtime checks pass.


## Step 9 — Motion and interaction polish

**Implementation commit:** `df53ad38d4e591a067162e5c23c6967a181d2179`

- Added eased hover transitions to the four idle home-dashboard action cards.
- Cards lift by at most 3 layout pixels, their border and icon tint transition smoothly, and the animation settles when the pointer leaves.
- Motion is driven by the existing main-window 30 Hz UI timer; it reuses the existing integer easing helper and existing paint path.
- Animation is strictly presentation-only. It does not move the LibVLC video child, change playback state, start background work, or add a new timer.
- Hover motion settles when media starts, the drawer/menu opens or fullscreen is entered.

**Windows validation checklist**

1. Hover each of the four home cards; verify smooth lift/tint transition and return-to-rest.
2. Move rapidly between cards and outside the dashboard; verify no flicker, stuck hover state or hit-target mismatch.
3. Click each card during and after the hover transition; verify original actions still fire.
4. Start playback, open the drawer/menu and enter fullscreen; verify dashboard motion stops and playback/video rendering remains unaffected.
5. Observe CPU usage and frame pacing at idle; confirm the existing 30 Hz timer is not causing unnecessary work or visible redraw flicker.
6. Verify 100%, 125%, 150% and 200% display scaling.

- [x] Implement restrained dashboard hover motion using the existing UI timer.
- [ ] Build with Windows MSVC and perform runtime/performance checks.


## Step 10 — Global visual consistency audit

**Implementation commit:** `22c0c04c60c1d31bfa811908f3470fee65dce7bf`

### Shared theme adoption

- The Streaming Centre now includes `src/ui_theme.h` and consumes the shared ABM tokens for its main canvas, top brand accent, native title-bar caption, window border and standard text font.
- Removed the Streaming Centre's duplicated near-match values for those shared surfaces. This keeps the Streaming Centre aligned with the main player's navy/charcoal, teal and Segoe UI Variable design language.
- Existing service branding, WebView2 content, TV discovery, app launch, casting and navigation behaviour are intentionally untouched.

### Scope of this pass

- Main player and its home/dashboard, transport, drawer, custom menus/dialogs and torrent/settings surfaces were reviewed against the existing shared theme and prior UI steps.
- Streaming Centre was the clearest remaining top-level window using a separate hard-coded canvas/accent/font palette; this pass brings its shared chrome into alignment.
- Provider webpages rendered inside WebView2 remain provider-owned content and are not recoloured or restyled by ABM.
- This is a source-level consistency pass, not a claim that every control has pixel-identical geometry or that all display scaling has been validated.

### Windows validation checklist

1. Build with the Windows MSVC toolchain; confirm the shared theme header is found by the Streaming Centre compilation unit.
2. Open the main player, Torrent Stream, Settings and Streaming Centre; compare canvas, title-bar, border, accent and typography at the same display scale.
3. Open Streaming Centre About/Help and the TV/device panel; check text hierarchy, button states, list selection and focus visibility.
4. Open each provider in WebView2 and verify its page remains provider-native and unaffected.
5. Verify TV discovery, app checks, launch/stop, local casting and torrent casting are unchanged.
6. Repeat at 100%, 125%, 150% and 200% scaling and on a second monitor if available.

- [x] Align Streaming Centre's shared chrome to the central theme tokens.
- [ ] Complete Windows build, visual comparison and regression validation.


## Step 11 — Accessibility and keyboard usability

**Implementation commits:**
- `d179d150d83a4c9d97ca31292950caf873edbfae` — branded modal keyboard focus
- `c3862f0df8931a17947435ca8998c0113697137d` — Streaming Centre tab stops

### Implemented

- Branded modal dialogs now maintain an explicit focused-button index. Tab and Left/Right move focus between Yes/No actions; Enter and Space activate the focused action; Escape retains its safe cancel/No behaviour.
- The initially focused dialog action matches the configured default button, including default-No confirmations.
- Dialog buttons draw a high-contrast focus outline using the shared `ABM_UI_COLOR_FOCUS` token, distinct from the normal/default button treatment.
- Added `WS_TABSTOP` to all seven owner-drawn action buttons in the Streaming Centre device panel (Refresh devices, Use selected TV, Check TV apps, Launch app, Refresh app status, Stop TV and Close). Their existing owner-drawn focus treatment already uses a high-contrast teal outline.
- Existing custom action menus retain Up/Down/Enter/Escape keyboard navigation; this pass does not replace their interaction model.

### Windows validation checklist

1. Open a Yes/No dialog with default No. Use Tab/Left/Right, verify the visible focus outline, activate each choice with Enter and Space, and verify Escape returns No.
2. Open an OK-only dialog and verify Enter, Space and Escape retain expected behaviour.
3. Open the Streaming Centre device panel and use Tab/Shift+Tab through all seven action buttons and the receiver/app lists; verify disabled controls are skipped and focus is always visible.
4. Operate Refresh, Select TV, Check TV apps, Launch, Refresh app status, Stop TV and Close using keyboard only.
5. Verify keyboard focus is not stolen from WebView2 while browsing provider pages; confirm Escape/F11 and existing browser shortcuts still behave as documented.
6. Check text legibility and focus contrast at 100%, 125%, 150% and 200% display scaling, including high-contrast Windows mode.
7. Regression-test mouse interaction and the default-No safety of destructive confirmations.

**Known remaining work:** this is a targeted accessibility pass, not a full WCAG/Windows accessibility certification. The main player's custom canvas controls and several auxiliary windows still need a complete keyboard/focus and screen-reader audit. Tooltips, UI Automation names/roles for custom-drawn canvas controls, high-contrast theme adaptation and a published shortcut reference remain follow-up items.

- [x] Add visible keyboard focus and full keyboard activation to branded modal buttons.
- [x] Make Streaming Centre action buttons reachable in the tab order.
- [ ] Complete full application keyboard, screen-reader and high-contrast audit.
- [ ] Build and validate with Windows MSVC and runtime keyboard-only testing.


## Step 12 — Final UI QA and release-readiness audit

**Implementation commit:** `7711f505e96580724c22dda58934ac353adb4528` — `UI_QA_CHECKLIST.md`

### Source-level audit completed

- Confirmed player-owned dialog call sites route through the ABM-branded dialog wrapper; no direct Win32 `MessageBoxW` call was found in the player source.
- Confirmed no `CreatePopupMenu`, `TrackPopupMenu` or `SHBrowseForFolderW` calls remain in the audited player source.
- Confirmed updater source has no direct native message-box or legacy popup-menu calls and uses the shared branded dialog entry point.
- Confirmed Streaming Centre source has no direct native message-box or legacy popup-menu calls; its device actions expose tab stops and owner-drawn focus feedback.
- Native common file open/save dialogs remain intentional system-owned file workflows. OS/UAC prompts remain OS-owned.
- Confirmed the application opts into system-aware DPI handling. A per-monitor `WM_DPICHANGED` relayout path was not found in this audit, so mixed-DPI monitor transitions remain an explicit runtime risk to verify.
- Reconfirmed the accessibility scope is targeted rather than complete: screen-reader/UI Automation and high-contrast coverage still need Windows testing.

### QA artifact

Added `UI_QA_CHECKLIST.md`, covering:
- MSVC build and launch gates.
- 100%, 125%, 150% and 200% display scaling, multiple monitors and work-area edge placement.
- Keyboard-only navigation, dialog/menu behaviour, Narrator and high-contrast checks.
- Visual states, empty/loading/error states and motion.
- Local playback/casting, Streaming Centre, WebView2 and torrent cache/download/playback/casting regressions.
- Idle and active performance, long-session stability, installer and updater acceptance.
- Defect recording and release sign-off.

**Validation boundary:** this step is a source-level audit and QA-documentation pass. No Windows build, runtime, DPI, accessibility, performance, installer or playback regression test was executed by this commit. Do not mark the UI redesign fully QA-certified until the checklist is completed on Windows.

- [x] Add a consolidated final UI QA checklist.
- [x] Perform targeted source audit of legacy popup/dialog APIs and DPI/accessibility markers.
- [ ] Complete Windows build and runtime QA matrix.


## Documentation update — user-facing help alignment

The User Guide now documents the branded application menus/dialogs, keyboard interaction for custom menus and confirmations, Streaming Centre device-panel tab navigation, the scope and limitations of current accessibility support, and the idle-only dashboard hover motion. The README now links directly to the consolidated UI QA checklist.

This documentation does not imply that every custom control is screen-reader accessible or that high-contrast and mixed-DPI behaviour has been validated. See [UI_QA_CHECKLIST.md](UI_QA_CHECKLIST.md) for the remaining Windows verification gates.

## Build note — shared theme header

The Streaming Centre source includes the shared theme header relative to its source directory (`../ui_theme.h`). This reflects the repository layout where the header is stored at `src/ui_theme.h` and the translation unit is stored at `src/streaming/streaming_host.cpp`. A source update does not itself establish that the local Windows build has passed; rebuild with the documented MSVC environment after pulling `main`.


## Runtime palette centralization — Step 1

- Added `src/ui_theme.c` as the single runtime palette store. `ABMThemeColor`, `ABMThemeSetColor` and `ABMThemeReset` provide the shared token API.
- Migrated static color literals in the main player, drawer, command centre, playback controls, torrent interface, settings/equalizer surfaces, branded dialogs, licensing window and Streaming Centre native chrome to runtime palette tokens.
- The Streaming Centre welcome page now builds its CSS colors from the shared palette. Third-party streaming websites remain untouched.
- Added the theme module to `build.bat` compilation and linking.
- This step establishes the runtime-token foundation only. User-selectable presets, custom accent selection and preference persistence are subsequent work.
- Procedural cover-art colors and semantic status colors remain distinct; playback, torrent, WebView2 and casting behavior are unchanged.


## Runtime color presets — Step 2

- Added seven coordinated application palettes: Midnight Teal, Arctic Blue, Royal Violet, Ruby Rose, Sunset Amber, Emerald and Golden Yellow.
- Presets recolor the existing blue/teal design family while preserving neutral surfaces and semantic success, warning, error and orange status colors.
- The selected preset is stored in `%APPDATA%\\ABMAVPlayer\\theme.ini` and restored at startup.
- The Settings window exposes seven clickable swatches with immediate application and a selected-state outline.
- Main player, open application-owned windows and Streaming Centre chrome refresh after a selection. An active third-party streaming website is never navigated or recolored.
- Preset selection is independent of playback, audio routing, torrent cache/download behavior and casting.
- Windows build and visual QA remain required for each preset, including text contrast, DPI scaling and active-window refresh.

## 9. Implementation status and migration gate

The canonical DirectWrite role contract is implemented. This is a foundation milestone, not a claim that every window is visually identical. Help Centre, Streaming Centre, Settings, Advanced Settings, Torrent Stream, About and auxiliary dialogs still require control-by-control migration and runtime screenshot comparison. Provider-owned WebView2 content is explicitly excluded. A window is complete only after its ABM-owned text, icons, buttons, inputs, borders, focus/hover/pressed/disabled states and DPI behaviour have been checked against the Main Player at 100%, 125%, 150% and 200% scaling.


## Step 13 — Torrent Stream design-system migration

**Source implementation commits:**
- `855a4f973e5be834604a522b6f6ddc919d2e42fe` — canonical rendering, typography and layout
- `aea5295854291f031ecf7f291801aac66a9259e4` — control states, semantic tokens and native title-bar chrome
- `6c458e890e9e204f8e7d8bb170e7ead59a67a0d1` — DirectWrite rendering for application-owned labels

### Source-level changes

- Torrent's main-window repaint now uses the Main Player's clipped Direct2D paint pattern (`ABM_D2D_BeginClip` with `PAINTSTRUCT::rcPaint`) and retries after a failed render-target end.
- The Torrent sliders now render their rail, fill, thumb, value capsule, text and keyboard-focus outline through the shared Direct2D/DirectWrite canvas. The prior GDI slider painting and GDI value text have been removed.
- The ABM / AV PLAYER wordmark now uses the Main Player's exact typography roles and brand suffix rectangle: heading role for ABM, body role for AV PLAYER, with the same baseline geometry.
- The native magnet EDIT uses the Main Player body family and size. The native edit implementation is deliberately retained for caret, selection, clipboard, IME and Windows accessibility behaviour.
- Torrent's live task summary, playback status, download/upload labels and cache labels are now painted by the parent Direct2D surface using canonical DirectWrite roles. Their STATIC HWNDs remain as hidden text-state holders so existing status updates and task logic are not rewritten.
- Corrected two source-level layout collisions: the live task summary no longer occupies the content heading row, and playback status no longer overlaps the playback heading/progress rail.
- Owner-drawn Torrent buttons now include an explicit disabled treatment and consume shared semantic text, surface, border, accent and focus tokens. File-list and TV-selector drawing also use the shared semantic palette.
- The Torrent top-level window now uses the shared dark caption, border and rounded-corner treatment. Theme changes now rebuild its cached GDI brushes and reapply DWM caption/border colors; those brushes are released when the window closes.
- Slider keyboard focus is visibly invalidated on focus/enable changes, and the slider explicitly requests arrow-key input. Native edit-control colors use the shared input and text tokens.
- Removed an obsolete Torrent GDI drawing helper. Remaining GDI text branches are defensive fallbacks if the shared canvas cannot begin; the normal path is DirectWrite.

### Explicit boundaries

- No torrent engine, task lifecycle, magnet ingestion, download, cache, buffering, playback, DLNA discovery or casting code was intentionally changed.
- Native EDIT input remains a native Windows text-rendering surface by design; replacing it would require a separately tested editable control that preserves IME, selection, clipboard, keyboard and accessibility semantics.
- Windows uses the application's existing system-aware DPI mode. This step does not claim per-monitor-v2 DPI support or prove that the fixed-coordinate Torrent layout is correct at every scale.
- Some defensive GDI fallback drawing remains in owner-draw/control code for renderer-failure paths. The normal Torrent window path initializes the shared canvas before creating those controls.

### Validation status

- [x] Source migration committed to `main`.
- [x] Main Player brand typography roles and geometry reused.
- [x] Main Player clipped Direct2D repaint pattern adopted.
- [x] Slider value text moved to canonical DirectWrite.
- [x] Application-owned Torrent status and utility labels moved to DirectWrite.
- [x] Content/playback label collisions corrected in source.
- [x] Shared semantic control states and dark title-bar chrome adopted.
- [ ] Build with the local Windows MSVC toolchain.
- [ ] Compare Main Player and Torrent screenshots at 100%, 125%, 150% and 200%.
- [ ] Verify focus, disabled, hover, pressed, selected, empty, loading, error and long-text states.
- [ ] Verify resizing/maximize/restore and minimum-size behaviour.
- [ ] Run the complete Torrent functional regression checklist.

Do not call Torrent Stream fully visually validated until the unchecked Windows build and runtime comparisons pass.


**Additional source commits:** `19384175014e475d8b804c68df605688b84f367c` (theme refresh and resource lifecycle), `ab16915676b330c4366bf793fa89d20060b2bdb7` (semantic input palette and slider keyboard focus), and `86019de4d46b8caa52268fbbd07739fb9da02ef6` (obsolete drawing helper removal).


## Step 14 — Settings, Advanced Settings and Help rendering alignment

**Source implementation commits:**
- `a4a6fccec9fef3837ee8b5b21a0c7e0143cbe841` — canonical DirectWrite roles and semantic input surfaces
- `ffeef69f5c20e4a494c4a3395cd7b8b0ae238963` — shared clipped repaint and theme refresh
- `5c33e906ef20a08db3936cd36fd4211bc4342d8b` — erase-background and Help brush lifecycle hardening

### Source-level changes

- Settings and Advanced Settings now use the same clipped Direct2D invalid-region paint path as the Main Player. The paint handlers retain their existing drawing geometry while limiting background clearing and drawing to the Windows invalidated rectangle.
- Advanced Settings now suppresses redundant background erasure, matching Settings and the Main Player's flicker-resistant paint convention.
- Help Centre now suppresses redundant background erasure while retaining its existing Direct2D/DirectWrite canvas and native RichEdit reading surface.
- Help's cached background, input, article and navigation brushes are rebuilt when the shared theme changes and released when the Help window closes. This fixes a repeated open/close GDI brush accumulation path.
- Advanced Settings rebuilds its diagnostic and VLC-report brushes on theme changes, so open pages follow the active shared palette.
- Existing shared ABM DirectWrite role IDs, theme tokens, DWM caption treatment, page navigation, search, article formatting, settings values, controls and engine handlers are retained.

### Explicit boundaries

- This is a rendering lifecycle and shared-theme alignment pass, not a claim that every control is now a custom DirectWrite control. Native EDIT/RichEdit controls remain where text entry, selection, clipboard, IME, scrolling and familiar Windows accessibility behaviour are important.
- Settings and Advanced Settings retain their existing hand-authored coordinate layouts. This commit does not introduce per-monitor-v2 DPI support, responsive reflow, or a scrollable Settings page.
- No setting semantics, persistence keys, LibVLC options, diagnostics content, help text, search behaviour, playback, torrent, streaming or casting behaviour was intentionally changed.
- The Help article's RichEdit formatting is refreshed when the active theme changes; article content and topic selection logic remain unchanged.

### Validation status

- [x] Settings and Advanced Settings use clipped Direct2D repaint.
- [x] Advanced Settings and Help suppress redundant background erasure.
- [x] Help cached brushes are refreshed on theme change and released on close.
- [x] Advanced Settings diagnostic brushes are refreshed on theme change.
- [ ] Build with the local Windows MSVC toolchain.
- [ ] Compare all three windows against Main Player at 100%, 125%, 150% and 200% scaling.
- [ ] Verify Help topic selection, article scrolling, search, F3/Enter and theme switching.
- [ ] Verify every Settings control, folder picker, Save settings, update check and Advanced Settings navigation.
- [ ] Verify all ten Advanced Settings pages, diagnostic preview, VLC inventory and all option persistence.
- [ ] Verify repeated open/close cycles, GDI handle stability, keyboard focus and long-text clipping.

Do not mark these windows fully migrated or visually validated until the outstanding Windows build and runtime checks pass.

## Torrent typography role enforcement — follow-up

- Replaced numeric DirectWrite style indices in Torrent's main-window labels with the canonical `ABM_D2D_TEXT_*` role constants. This makes the role contract explicit in source and prevents accidental role drift when the shared typography enum is maintained.
- Corrected the Torrent wordmark suffix to use `ABM_D2D_TEXT_BRAND_SUFFIX`, rather than the ordinary body role. The ABM wordmark uses `ABM_D2D_TEXT_BRAND_MAIN`; supporting labels use the shared small/body roles.
- Owner-drawn buttons, file-list rows and TV selector rows continue to use the same shared DirectWrite canvas formats. GDI text paths are retained only as defensive fallback paths if Direct2D canvas drawing cannot start; they are not the normal rendering path.
- Native editable controls remain native for caret, selection, IME, clipboard and accessibility behaviour, with their font sourced from the shared ABM text tokens.
- This is a source-level typography consistency correction. Windows compilation and side-by-side runtime comparison against Main Player are still required before visual parity can be signed off.


## Settings custom scrollbar implementation — follow-up

- Replaced the Settings window's `WS_HSCROLL` / `WS_VSCROLL` native non-client scrollbars with custom-painted Direct2D tracks and thumbs. The window no longer requests native horizontal or vertical scrollbar chrome.
- Both axes derive their thumb length and position from the virtual content size, available viewport and current scroll offset. Thumb dragging and track clicks update the same Settings scroll state used by wheel and keyboard navigation.
- The thumb has a distinct active teal treatment while dragging. Mouse capture is released/reset on mouse-up, cancellation and capture changes.
- Track and thumb colors use existing `ABM_THEME_COLOR_*` definitions from `src/ui_theme.h`; palette identifiers must be checked against that shared header before use. Do not introduce per-window palette globals or duplicate theme values.
- Native edit fields remain native by design; the custom scrollbar treatment does not replace text-entry, selection, IME, clipboard or accessibility behaviour.
- Source commits: `460dfb1` (custom scrollbar implementation), `fc1afb8` (drag lifecycle hardening), `97b5f9d` (corrected undefined palette identifiers).
- Source-level documentation is updated; MSVC build and Windows interaction/visual verification are still required. Do not mark the scrollbar work runtime-validated until tested at multiple window sizes and display scales.


## Step 2 follow-up — Premium Command Centre grouping

**Implementation commits:** `4a3a47bfbffad22f7293b24661282a64e82097f2`, `37de958afa3774ca78d67580c606f376fbdd0c84`, `97c58acf4a4ad330ec5cefe4f9569069c661e499`, `9582a63e672f4a8f567a76e87a1ef98c3c12bdb4`

The Command Centre now uses a responsive five-group hierarchy: Playback & Media, Devices & Casting, Playback Controls, Streaming & Torrent, and App & Support. The panel adds a compact ABM brand mark, shared rounded group surfaces, section labels where vertical space permits, quieter row separators, consistent shortcut pills, and retained teal/amber feature treatments for Streaming and Torrent.

The implementation consumes the existing ABM typography, colour, spacing and radius tokens and continues to use the Main Player's established text and Direct2D stroke helpers. Menu item indices, hit rectangles, command dispatch, keyboard shortcuts, hover tracking and selection routing remain unchanged. On shorter windows, section captions collapse while group surfaces remain, preserving room for the commands. Group cards and captions are clipped to the animated panel boundary so they cannot paint ahead of the menu reveal.

**Windows runtime validation remains outstanding.** Verify that all 19 commands fit and remain clickable at minimum supported window height and 100%, 125%, 150% and 200% scaling. Confirm that the five section captions appear on taller windows and collapse cleanly on shorter windows.


## Media Collection drawer — premium ABM visual pass

**Implementation commits:** `3ada50a3b2ff21d95668ea79555715077f409a8a`, `cc03fe7a7b79ac2ccd17c0575339d4e257fec78a`

The expanded Playback & Output drawer's collection area now has a clearer ABM hierarchy: a dedicated MEDIA COLLECTION heading with a compact layered mark, a restrained track-count capsule, a deliberate clear action, a recessed search surface with a visible focus treatment, more distinct Queue/Library selection states, and quieter segmented filters/group controls. Track cards use deeper navy gradients, a clearer active-track rail, more consistent artwork framing, aligned title/metadata baselines, and restrained AUDIO/VIDEO pills. The empty state is presented as an intentional inset surface rather than bare text.

The layout coordinates and existing hit targets are retained. Queue/library switching, search, media filters, grouping, clear, scrolling, row selection and playback dispatch are unchanged. The collection consumes only existing `ABM_THEME_COLOR_*` tokens from `src/ui_theme.h`; no new local palette or font system is introduced. Compact row artwork remains square and centered within the row bounds.

**Validation status:** source-level palette identifier audit passes against `ui_theme.h`. Windows MSVC build and runtime visual/input verification are still required at normal and compact drawer heights, including 100%, 125%, 150% and 200% scaling. Check that text does not collide with the type pill, the square artwork stays inside the row, and all original collection hit targets remain clickable.


## Personal Media Libraries and Favourites — premium collection extension

The Media Collection drawer now supports a persistent personal-favourites layer and named M3U-based personal libraries without introducing a second theme system or copying media files.

- A compact heart affordance sits at the trailing edge of each media card. Its filled state uses the shared ABM rose accent; the idle state is muted and gains contrast on hover. The title/metadata column and AUDIO/VIDEO pill are narrowed and repositioned to reserve a dedicated heart hit target.
- The Library tab's segmented control switches between all tracks and Favourites. Opening Favourites restores any saved favourite paths missing from the active media list, so favourites remain accessible after the queue has been cleared or replaced. The Queue tab retains its existing Folder/Artist/Album grouping cycle.
- Favourites are stored by original media path in `%APPDATA%\\ABMAVPlayer\\favorites.m3u`, independently from `library.m3u`. Clearing or replacing the playback queue therefore does not clear the favourites list.
- A visible Manage control is present in both Queue and Library header states; it opens the collection/playlist action menu, including Save as personal library, Load saved library and Clear playlist. The row context menu also includes Add/Remove from favourites.
- In Library, the trailing filter control is explicitly labelled Favourites (not Group or All tracks). It uses a rose active treatment when selected; Queue retains the Folder/Artist/Album grouping control. Named collections are M3U files under `%APPDATA%\\ABMAVPlayer\\collections\\`; the Save dialog supplies the collection name and the Load dialog opens that collection into the active playback queue.
- Collections reference the original files; they do not duplicate media. Loading a collection changes the active queue but does not delete or overwrite other saved collection files.
- All added card colours use existing `ABM_THEME_COLOR_*` definitions from `src/ui_theme.h`. No local palette, font or radius system is introduced.

This is a source-level feature pass. The Windows build, font glyph fallback for the heart, card hit-target precision, file-dialog behaviour, persistence across restarts, and collection load/save regression checks remain mandatory before release sign-off.

### Main Player Background Skins

ABM AV Player provides eight application-wide background skins in Settings: Classic Music Player, Ocean Flow, Aurora, Golden Waves, Liquid Chrome, Neon Pulse, Deep Ocean and Holographic. Classic Music Player remains the default and preserves the original appearance. A shared renderer and one process-wide selection apply the same restrained ambient shapes and animated wave language to the Main Player, Settings, Advanced Settings, Torrent Stream, Advanced Equalizer, Video Enhancement Studio, Home Theatre, Streaming Centre, About/Help surfaces and ABM-owned menus and dialogs. The background is always painted before foreground panels, controls and text; it never overlays the active video or WebView2 provider content. Native child controls keep their existing interaction, accessibility and rendering behaviour. The selected skin is stored in `%APPDATA%\\ABMAVPlayer\\theme.ini` under `[Appearance] BackgroundSkin` and remains independent of the seven Color Theme presets.


## Step 11 — GPU-rendered floating-surface shadows

**Scope:** presentation-only polish for the existing media drawer and Command Centre.

- Added `ABM_D2D_ShadowRound` to the shared Direct2D renderer. It paints a compact stack of translucent rounded silhouettes using the existing Direct2D render target and cached brush.
- Replaced the two opaque, offset shadow blocks on the media drawer and Command Centre with the shared soft-shadow primitive.
- Shadow opacity follows the existing drawer/menu animation progress, so the floating surfaces gain depth as they enter and lose it as they leave.
- No new timer, thread, HWND, bitmap cache, DirectComposition dependency or external runtime was introduced.
- The existing 30 Hz UI timer and integer easing remain the animation clock. The LibVLC video child geometry, playback state, decoding, casting and torrent operations are unchanged.

**Windows validation checklist**

1. Build with the supported x64 MSVC toolchain.
2. Open and close the media drawer repeatedly; verify the shadow follows the slide and never remains after closing.
3. Open and close the Command Centre; verify its shadow tracks the expanding/collapsing panel.
4. Test rapid open/close reversals, resizing, fullscreen transitions and 100%, 125%, 150% and 200% scaling.
5. Check that the shadow remains subtle on all six themes and does not create visible banding or excessive GPU/CPU use.
6. Confirm playback, video sizing, audio visualisation, casting and torrent streaming remain unchanged.

- [x] Add shared Direct2D floating-surface shadow primitive.
- [x] Apply it to the media drawer and Command Centre.
- [ ] Build with Windows MSVC and complete runtime/performance checks.



## Global five-level ABM surface hierarchy

The application-owned UI has one process-wide semantic surface ladder, declared centrally in `src/ui_theme.h` and backed by the shared runtime palette in `src/ui_theme.c`:

| Role | Token | Default |
|---|---|---|
| Level 1 — Application background | `ABM_UI_COLOR_BACKGROUND` | `#151820` |
| Level 2 — Navigation and panels | `ABM_UI_COLOR_SURFACE_NAVIGATION` | `#1D222D` |
| Level 3 — Cards and containers | `ABM_UI_COLOR_SURFACE_CARD` | `#272E3A` |
| Level 4 — Hover and active surfaces | `ABM_UI_COLOR_SURFACE_INTERACTIVE` | `#343D4C` |
| Level 5 — ABM signature accent | `ABM_UI_COLOR_ACCENT` | `#E9B949` |

Legacy semantic aliases resolve to the corresponding new levels so existing modules can migrate without duplicating palette values. The shared colour resolver also normalizes legacy low-luminance neutral and blue/teal surface swatches into the same four levels, while preserving black video, vivid accents and semantic status colours. The signature gold and all four neutral surface levels are excluded from preset hue transforms. All seven existing selectable presets remain available and can continue to theme secondary decorative colours. Do not add window-local palette globals or hardcoded surface hex values. Every new or revised ABM-owned window must use these global semantic tokens for its surfaces, with the global typography, spacing and radius tokens already defined in the same header.

This is the central token foundation. A repository-wide visual audit, remaining module migration, Windows MSVC build and runtime verification are still required before claiming every existing surface is fully unified.


## 3.2 Five-level surface rollout and compatibility contract

All five surface roles are declared globally in `src/ui_theme.h`, backed by the process-wide palette in `src/ui_theme.c`. Existing aliases map to those roles, and legacy low-luminance neutral/blue-teal palette constants are normalized by `ABMThemeColor` so old call sites converge on the same hierarchy rather than introducing another window-local palette.

The fixed surface ladder and ABM signature gold are intentionally excluded from the seven existing preset hue transforms. Presets continue to recolour secondary decorative colours, while the global surfaces and brand accent remain stable. The Main Player, Home Theatre, Media Library, Licensing dialog and Streaming Centre now consume the shared semantic tokens or the compatibility resolver. Home Theatre's cached colour constants were converted to paint-time semantic macros; Media Library refreshes cached GDI brushes when their resolved colour changes; Home Theatre and Licensing refresh their brushes on `WM_THEMECHANGED`; Streaming Centre refreshes its information-window brushes on the same message.

Optional user-selected background skins in `src/ui_skin.cpp` are an intentional artwork layer, not a competing application palette. They may supply the backdrop where the user explicitly selects a skin, but must not redefine navigation, cards, inputs, hover states, text, borders or other functional controls. Provider-owned WebView2 content also retains its provider styling.

**Migration rule for all future code:** declare no window-local surface or accent colour variables. Use `ABM_UI_COLOR_BACKGROUND`, `ABM_UI_COLOR_SURFACE_NAVIGATION`, `ABM_UI_COLOR_SURFACE_CARD`, `ABM_UI_COLOR_SURFACE_INTERACTIVE` and `ABM_UI_COLOR_ACCENT`. Use `ABM_UI_COLOR_THEME_ACCENT` only for secondary preset decoration. Do not introduce raw RGB/hex literals for functional UI surfaces. Theme-sensitive GDI brushes must be recreated when the resolved token changes or when the window receives its theme-refresh message.

The source migration is not a substitute for Windows validation: the full application still needs an MSVC build, live switching through every preset, and visual inspection of all windows, controls, hover/focus/disabled states, custom skins, high-DPI layouts and fullscreen playback.


### Signature-accent alignment

The Main Player's custom-rendered brand header and idle-state hero now use the same permanent ABM signature gold as Home Theatre: logo halo and mark, AV PLAYER wordmark, header rule, ambient highlight, hero eyebrow/headline and primary home-card emphasis resolve through `ABM_UI_COLOR_ACCENT`, `ABM_UI_COLOR_ACCENT_BRIGHT` and `ABM_UI_COLOR_ACCENT_SOFT`. The remaining teal/blue tones in the player are retained only as secondary ambient artwork or status/decorative colours; they must not recolour the ABM brand mark or primary action identity.


### Streaming Centre button alignment

Streaming Centre toolbar actions now follow the shared ABM button hierarchy: primary Cast video uses the signature gold fill; Find TVs and navigation controls use neutral raised surfaces with gold focus/hover accents; Fullscreen no longer has a separate teal-filled treatment; Stop TV retains its distinct destructive red treatment. The TV/device panel's primary actions continue to use the shared gold semantic accent. Avoid per-window green/teal primary-button fills unless the colour communicates a specific state (for example, a live receiver/status indicator).


### Shared logo gradient

All native ABM window mastheads use the same signature mark treatment: a horizontal gold-to-deep-antique-gold gradient from `ABM_UI_COLOR_ACCENT` to `ABM_THEME_COLOR_66551F`, a consistent rounded-square radius, and a light inset monogram outline. Do not render the logo tile as a flat solid-gold fill in secondary windows. The Main Player, Home Theatre and Streaming Centre should read as the same brand at a glance.


### Cross-window button hierarchy

The Main Player, Torrent Stream, Home Theatre, Streaming Centre and Media Library use one button hierarchy without changing existing control geometry:

- **Primary:** signature-gold filled surface with legible dark-surface contrast (for example Play/Stream, Search, Play local match, Cast video).
- **Important secondary:** dark raised surface with a signature-gold outline or focus ring (for example Add magnet, Identify library, scan actions, Cast to TV, Apply limits/quota).
- **Utility/navigation:** neutral dark raised surface with standard border; gold is reserved for hover/focus and icon emphasis.
- **Destructive:** restrained dark red surface with danger-red outline and pale red icon/text (Remove task, Clear inactive cache, Stop TV).
- **Disabled:** muted surface, text and border; no primary gold fill.

Do not change existing button dimensions, placement, spacing, corner radius or the established dark surface ladder while applying this hierarchy. The gold fill is reserved for the single most important action in the current workflow; it is not a general button colour. Distinct state indicators (such as an active receiver pulse) may retain their status colours.
