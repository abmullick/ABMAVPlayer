# ABM AV Player — UI/UX Design System

**Status:** Step 1 — foundation and audit  
**Baseline branch:** `main`  
**Baseline commit:** `eb8cd7415cb403c9bb05244ba67ed36efc8eda89`  
**Scope:** Visual consistency and interaction conventions only. This step does not alter playback, casting, torrent, streaming, networking, or updater behaviour.

## 1. Audit summary

The current application already has a recognisable dark navy identity, a split ABM / AV PLAYER wordmark, teal accents, rounded cards, and Segoe UI typography. The Direct2D renderer uses DirectWrite and a 96-DPI coordinate space so its layout coordinates align with Win32 client-area pixels.

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
- **Dark, restrained, legible:** Use layered charcoal/navy surfaces, clear text contrast and subtle borders. Avoid excessive gradients, glow and glass effects.
- **Teal is the primary action colour:** Orange is reserved for meaningful attention or selected/active states; red is reserved for destructive or stop/error states.
- **Clear action hierarchy:** Primary, secondary, quiet and destructive actions must be visually distinguishable.
- **State is explicit:** Hover, pressed, keyboard focus, disabled, loading, success, warning and error states must be designed rather than improvised.
- **Motion is purposeful:** Animation must be short, subtle, interruptible and never compromise playback or input responsiveness.
- **Preserve native behaviour:** Keyboard access, focus order, text selection, accessibility and familiar Windows interactions take precedence over purely decorative treatment.

## 3. Shared design tokens

The initial code-level token set is in `src/ui_theme.h`. It is additive and is not yet wired into every window. Later UI steps should consume these tokens rather than introducing new local values.

### Colour palette

| Token | Value | Intended use |
|---|---|---|
| Background | `RGB(6, 9, 16)` | Main window background |
| Surface | `RGB(12, 18, 29)` | Primary cards and panels |
| Raised surface | `RGB(20, 29, 43)` | Nested panels, hover surfaces |
| Blue surface | `RGB(15, 31, 48)` | Secondary workspace panels |
| Input | `RGB(11, 25, 40)` | Editable fields and inset controls |
| Primary text | `RGB(247, 250, 255)` | Titles and important values |
| Secondary text | `RGB(226, 238, 250)` | Body copy |
| Muted text | `RGB(145, 160, 181)` | Metadata and supporting labels |
| Disabled text | `RGB(91, 108, 128)` | Disabled controls |
| Teal accent | `RGB(54, 177, 168)` | Primary actions and selected navigation |
| Bright teal | `RGB(82, 205, 190)` | Active indicators and highlights |
| Orange accent | `RGB(255, 145, 28)` | Attention and selected task completion |
| Success | `RGB(77, 196, 153)` | Completed/healthy state |
| Warning | `RGB(242, 177, 79)` | Caution and recoverable issues |
| Danger | `RGB(220, 75, 85)` | Destructive actions and errors |
| Border | `RGB(39, 51, 70)` | Subtle card boundaries |
| Strong border | `RGB(53, 111, 127)` | Emphasised boundaries |
| Focus | `RGB(99, 225, 205)` | Keyboard focus ring |

**Usage rules:** do not use accent colours for large blocks of text; maintain readable contrast; do not communicate status by colour alone; destructive actions must not use the primary teal treatment.

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
