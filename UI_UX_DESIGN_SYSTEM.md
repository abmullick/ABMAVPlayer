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
