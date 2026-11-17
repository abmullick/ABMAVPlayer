# ABM AV Player — Final UI QA Checklist

This checklist closes the source-level UI/UX implementation sequence (Steps 1–11) and defines the Windows validation required before calling the redesign release-ready.

**Status convention**
- [x] Source-level implementation or inspection completed.
- [ ] Requires a local Windows build, runtime observation, or manual regression test.
- Do not mark a runtime item complete based only on source inspection.

## 1. Source-level audit snapshot

Audited on `main` during Step 12:

| Area | Source finding |
|---|---|
| Player-owned message dialogs | Calls route through the ABM-branded `ABM_MessageBoxW` wrapper; no direct Win32 `MessageBoxW` invocation was found in the player source. |
| Legacy popup menus | No `CreatePopupMenu` or `TrackPopupMenu` call was found in `src/player.c`. |
| Legacy folder browser | No `SHBrowseForFolderW` call was found in `src/player.c`. |
| File selection | Native common open/save dialogs remain intentional system-owned file workflows. Torrent folder selection uses the modern COM file-dialog interface. |
| Updater dialogs | Updater uses the shared branded dialog entry point; no direct native message-box call was found in `src/updater.cpp`. |
| Streaming Centre | No direct native message-box or legacy popup-menu API calls found; device-panel action buttons expose tab stops and owner-drawn focus feedback. |
| DPI awareness | The player opts into system-aware DPI handling. A per-monitor `WM_DPICHANGED` resize/re-layout path was not found in this audit; mixed-DPI monitor transitions therefore require explicit runtime verification and may remain a limitation. |
| Accessibility coverage | Branded modal keyboard handling and selected Streaming Centre controls are improved, but this is not a complete screen-reader/UI Automation or high-contrast audit. |

This is a targeted source inspection, not a formal static-analysis report or a claim that every native Windows control has been eliminated. Native system file pickers and OS/UAC-owned surfaces are intentional exceptions.

## 2. Build and launch gate

- [ ] Pull the latest `main` and confirm the working tree is clean before building.
- [ ] Build the normal application configuration with the documented Visual Studio Build Tools/MSVC environment.
- [ ] Confirm there are no new compiler errors or linker errors, especially COM dialog symbols and the shared branded dialog linkage.
- [ ] Launch from the build output and from the installed application.
- [ ] Check startup with missing optional WebView2 runtime, unavailable VLC plugins, and no network; errors should remain understandable and recoverable.
- [ ] Confirm no unexpected native message boxes or legacy-looking application popup menus appear during normal use.

## 3. Display, DPI and window geometry

Run at 100%, 125%, 150% and 200% Windows display scaling.

- [ ] Main window: restore, maximize, resize, minimize and restore.
- [ ] Home dashboard: card layout, hover hit targets, labels and icon alignment.
- [ ] Playback surface: video remains correctly sized and is not displaced by UI animation.
- [ ] Playlist drawer and recent-media surface: no clipping, stale selection or off-screen popup.
- [ ] Settings window: controls remain reachable and labels do not overlap.
- [ ] Torrent Stream: compare ABM / AV PLAYER wordmark font roles, sizes, weights and baseline against Main Player.
- [ ] Torrent Stream: verify clipped repaint has no flicker, stale regions or repaint loops after resize and restore.
- [ ] Torrent Stream: inspect magnet input, owner-drawn file list, TV selector, action buttons and all three sliders for shared typography, surface, border, radius and padding.
- [ ] Torrent Stream: verify primary, secondary, destructive and disabled button states plus keyboard focus outlines.
- [ ] Torrent Stream: verify task summary, playback status, download/upload labels and cache text do not overlap headings or controls; test long names and long diagnostics.
- [ ] Torrent Stream: verify slider keyboard arrows, pointer drag, focus outline and displayed values for unlimited, KiB/s, MiB/s and GiB.
- [ ] Torrent Stream: test no-task, metadata-loading, queued/checking, downloading, complete/seeding, paused and error states.
- [ ] Torrent Stream: verify dark title bar, rounded corners and border match the Main Player.
- [ ] Advanced Settings: all ten navigation entries are visible and clickable; the VLC Runtime & Codecs page fits without colliding with the footer at 100%, 125%, 150% and 200% scaling.
- [ ] VLC Runtime & Codecs: loaded LibVLC version/path and plugin directory are accurate; selected plugin-file presence matches the installed runtime; Refresh scan updates the results.
- [ ] Streaming Centre: title bar, device panel, receiver/app lists and WebView2 region remain aligned.
- [ ] Branded dialogs and custom menus: fit within the work area at every screen edge; long text and long menus remain usable.
- [ ] Move windows between monitors with different scaling factors; record any size or coordinate jump. System-aware DPI is not equivalent to per-monitor-v2 support.
- [ ] Test taskbar auto-hide, non-primary monitor placement and display resolution changes.

## 4. Keyboard and accessibility

- [ ] Operate the main window without a mouse: navigate available controls, open/close the drawer, select media and operate playback.
- [ ] Verify visible focus on every interactive control and a logical Tab/Shift+Tab order.
- [ ] Branded Yes/No dialogs: Tab, Shift+Tab, Left/Right, Enter, Space and Escape; confirm default-No remains safe.
- [ ] OK dialogs: Enter and Escape behave consistently.
- [ ] Custom menus: Up/Down, Enter, Escape and mouse wheel; focus returns to the invoking control after close.
- [ ] Command Centre: verify five grouped surfaces, section labels on taller windows, compact fallback on shorter windows, ABM brand mark, shortcut pills and teal/amber featured destinations; confirm all 19 original commands, hover states, keyboard selection and click targets still work.
- [ ] Streaming Centre: Tab through device actions and lists; operate each action with keyboard only.
- [ ] Verify WebView2 retains browser keyboard behaviour and does not unexpectedly steal focus.
- [ ] Inspect with Windows Narrator: names, roles, values, selected state and action announcements.
- [ ] Inspect Windows contrast themes and text scaling; record custom-drawn surfaces that do not adapt.
- [ ] Confirm destructive actions are clearly labelled and require the expected confirmation.

## 5. Visual quality and interaction

- [ ] Check normal, hover, pressed, selected, disabled and keyboard-focused states for each control family.
- [ ] Verify text truncation is intentional; no clipped labels, overlapping text or unreadable low-contrast metadata.
- [ ] Check empty, loading, error and populated states for Home, Recent, Playlist, Torrent Stream and Streaming Centre.
- [ ] Verify motion is restrained, does not flicker, and settles when playback/fullscreen or other suppression states begin.
- [ ] Confirm hit targets remain aligned during hover animation and after resizing.
- [ ] Verify the shared navy/charcoal, teal accent, typography and focus treatment are consistent across app-owned windows.
- [ ] Confirm provider pages inside WebView2 retain provider-owned styling.

### Help Centre — F1 documentation and search

- [ ] Open Help Centre with F1 and confirm the branded header, search field, topic index, reading pane and footer fit without clipping.
- [ ] Select every topic in the index. Confirm the reading pane shows only the selected topic's article, not the entire concatenated guide.
- [ ] Verify article hierarchy: topic title is visually distinct; paragraphs have visible separation; lists and shortcut references remain understandable.
- [ ] Inspect every article for visible literal escape sequences such as `\\n`, `\\r` or `\\r\\n`; none should appear as documentation text.
- [ ] Confirm long articles wrap at the reading-pane width and scroll vertically without horizontal clipping or hidden final paragraphs.
- [ ] Search for a term in the currently selected topic and in a different topic. Confirm the match opens the correct topic and highlights the actual matching text.
- [ ] Test Enter, Find next, F3, search wraparound, empty query and no-match feedback.
- [ ] Resize the Help Centre and test 100%, 125%, 150% and 200% scaling. Confirm topic labels, article margins, search controls and scrollbars remain usable.
- [ ] Test keyboard-only operation: search focus, topic selection, article scrolling, close via Esc and visible focus feedback.

## 6. Functional regression — do not change these behaviours during UI QA

- [ ] Local audio and video playback, pause/resume, seek, volume and fullscreen.
- [ ] Playlist add/remove/reorder, recent media and file open/save workflows.
- [ ] Audio track, subtitle, playback speed, equalizer, ReplayGain, compressor, shuffle and repeat.
- [ ] Local renderer discovery and local player casting to a TV.
- [ ] Streaming Centre TV discovery, receiver selection, app checks, launch and stop.
- [ ] WebView2 provider navigation, close/reopen and crash recovery.
- [ ] Torrent metadata discovery, download start/pause/stop and destination selection.
- [ ] Torrent persistent cache, buffering, seeking and playback after restart.
- [ ] Torrent video casting: verify the existing shared cache/download pipeline continues while casting; verify downloaded data persists in the configured Windows directory.
- [ ] Torrent download/upload limits and cache quota settings.
- [ ] Updater: check release, verify checksum, download, confirmation, installer launch and error handling.
- [ ] Confirm no UI-only change alters playback engine, renderer selection, torrent session, cache policy or background transfer behaviour.

## 7. Performance and stability

- [ ] Observe idle CPU and memory after startup and after leaving the player idle for several minutes.
- [ ] Observe CPU, memory and frame pacing during local playback, hover animation, drawer interaction and fullscreen.
- [ ] Repeat with high-resolution video and long playlists.
- [ ] Leave Streaming Centre open with WebView2 for an extended session; check memory growth and recovery after close/reopen.
- [ ] Run a long torrent download while playing and while casting; check UI responsiveness and cache consistency.
- [ ] Open and close auxiliary windows repeatedly; check for orphaned windows, handles, threads or stale focus.
- [ ] Test sleep/resume and network disconnect/reconnect where applicable.

## 8. Installer and update acceptance

- [ ] Build a clean installer from the same validated source revision.
- [ ] Install on a clean Windows account or test VM without developer tools.
- [ ] Verify runtime prerequisites, shortcuts, file associations (if enabled), uninstall and upgrade paths.
- [ ] Upgrade an existing installation while preserving user settings and torrent cache.
- [ ] Run updater checksum verification against a known-good package and confirm a deliberately mismatched checksum is rejected.
- [ ] Confirm UAC prompts remain OS-owned and the application does not impersonate system security UI.

## 9. Defect recording and sign-off

For every failure, record:
- Windows edition/build and architecture.
- App commit SHA and build configuration.
- Display scale, monitor arrangement and resolution.
- Exact window/control, steps to reproduce and expected/actual result.
- Screenshot or short recording where useful.
- Whether the issue reproduces after a clean restart.

**Release sign-off**
- [ ] Build succeeds on the target Windows toolchain.
- [ ] No blocker/critical UI defects remain.
- [ ] Keyboard-only and DPI matrix completed.
- [ ] Playback, local casting, Streaming Centre and torrent regression matrix completed.
- [ ] Installer and updater smoke tests completed.
- [ ] Remaining known limitations documented.

**Step 12 source status:** checklist and targeted source audit completed. All unchecked items require local Windows validation. Do not describe the UI redesign as fully QA-certified until those checks pass.


## Torrent Stream design-system migration — source status

Source changes committed on `main`:
- `855a4f973e5be834604a522b6f6ddc919d2e42fe`
- `aea5295854291f031ecf7f291801aac66a9259e4`
- `6c458e890e9e204f8e7d8bb170e7ead59a67a0d1`

The Torrent source now uses the canonical Main Player brand roles, clipped Direct2D repaint, DirectWrite slider/value rendering and DirectWrite application-owned labels. Two label-overlap defects were corrected. Native magnet text editing remains native intentionally to preserve Windows editing and accessibility behaviour.

All screenshot comparison, build, DPI, interaction-state and functional regression items above remain unchecked until run on Windows. Source-level changes alone are not runtime sign-off.


## 10. Settings, Advanced Settings and Help — migration checks

Source-level rendering lifecycle changes are committed. The following are still required on Windows:

- [ ] Settings: inspect every section, input, folder picker, theme swatch, playback-rate selector and action button against Main Player typography, spacing, radius, border and focus treatment.
- [ ] Settings: test long paths, validation errors, Save settings feedback, theme changes while Settings remains open, and opening Advanced Settings.
- [ ] Settings custom scrollbars: drag both thumbs, click before/after each thumb, scroll with wheel and keyboard, resize while scrolled, and cancel a drag by capture loss; verify the thumb tracks the actual content offset and no native scrollbar is visible.
- [ ] Advanced Settings: visit all ten pages; check selected navigation state, option cards, radio-style choices, toggle states, diagnostic controls, VLC plugin inventory and report preview.
- [ ] Advanced Settings: change theme while the window is open; confirm diagnostic and VLC report controls use the new palette.
- [ ] Help: inspect search, topic list, selected topic, RichEdit article typography, paragraph rhythm, scrollbar and footer.
- [ ] Help: test every topic, search match across topics, no-match feedback, Enter, F3, Ctrl+F and Escape.
- [ ] Help: switch theme while open and verify search/list/article surfaces refresh without losing usability.
- [ ] Repeatedly open and close Help; check GDI handle count does not grow from cached brush leakage.
- [ ] Compare all three windows to Main Player at 100%, 125%, 150% and 200%; note the existing system-aware DPI limitation and any clipping on short displays.
- [ ] Confirm all existing settings persistence, audio/video options, torrent storage/network controls, diagnostics and help content remain functionally unchanged.

Source commits:
- `a4a6fccec9fef3837ee8b5b21a0c7e0143cbe841`
- `ffeef69f5c20e4a494c4a3395cd7b8b0ae238963`
- `5c33e906ef20a08db3936cd36fd4211bc4342d8b`

These changes are source-level only; no Windows build or runtime screenshot validation has been performed in this environment.



## Settings / Advanced Settings / Help interaction refinement — source status

Source changes on `main`:
- Settings and Advanced Settings owner-drawn buttons now render pointer-hover and pressed feedback using shared surface, accent and focus tokens.
- Settings toggles now show pointer-hover feedback; native Settings edit fields draw a shared focus outline when focused, and their focus transitions invalidate the parent so the outline stays current.
- Mouse capture is released/reset on mouse-up, capture cancellation and capture changes so a pressed visual cannot remain stuck after an interrupted click.
- Help search and Find controls explicitly participate in dialog tab navigation. The topic list distinguishes hover, selection and keyboard focus.
- Help now switches to a stacked topic-list/article layout below a 760-pixel client width. Panel chrome follows the actual child-window bounds instead of fixed coordinates, so the visual cards track the responsive layout.
- Help retains the native RichEdit reading surface for selection, copy, search highlighting and keyboard reading; its content typography continues to use the shared ABM font family and semantic size tokens.

Remaining source work — do not mark these complete:
- Custom-painted Settings and Advanced Settings cards, toggles, selectors and action surfaces are not yet first-class tab stops with a complete Tab/Shift+Tab focus sequence and Enter/Space activation. Existing arrow-key navigation in Advanced Settings is retained, but is not a substitute for full keyboard traversal.
- Disabled-state treatment must be driven by each control's real availability; the current source pass does not invent disabled states for controls that are always enabled.
- Settings and Advanced Settings still use fixed-coordinate canvases. Their window dimensions and content need a responsive/scrollable layout pass for short work areas and mixed-DPI monitors.
- Main Player visual parity still requires side-by-side Windows screenshots at identical scale factors. Source token reuse is not proof of pixel-level parity.
- No Windows compiler build or runtime screenshot verification is implied by these source commits.

Validation still required: build on the documented MSVC toolchain; exercise 100%, 125%, 150% and 200% scale; resize Help below and above 760 client pixels; test mouse capture interruption; and perform keyboard-only navigation through every interactive control.


## Settings window — monitor-safe geometry and scrolling

Source implementation committed:
- Settings opens at a practical, shorter default client height instead of requesting a 980-pixel-tall window.
- Initial placement is centered on the owner's nearest monitor and clamped to that monitor's work area, excluding the taskbar and docked app bars.
- The window is resizable and uses custom ABM-painted horizontal/vertical scrollbars when the virtual Settings canvas exceeds the available client area. They use shared theme tokens, not native Windows scrollbar controls. All existing controls—including theme presets, status text, Save and Close—remain reachable on short or narrow displays.
- Direct2D painting uses a client-pixel translation for the scroll offset. Native EDIT controls move with the same offset; pointer hit-testing is translated back into virtual content coordinates.
- Mouse-wheel scrolling works over the window and is forwarded from native edit fields. Page Up/Down and Ctrl+Home/End provide keyboard scrolling.
- Custom scrollbar thumb dragging, track-click positioning, active-thumb feedback and mouse-capture cancellation must be tested; confirm no native scrollbar chrome appears at the right or bottom edge.
- When a native edit field receives focus, the viewport scrolls it into view.
- A `WM_DPICHANGED` handler applies the suggested rectangle when Windows delivers that message, clamps it to the nearest monitor work area and refreshes the render target/scroll ranges. Reopening an existing Settings window also clamps it inside its current monitor work area. This window-level handler does not change the application-wide DPI-awareness mode; mixed-DPI monitor transitions remain subject to the existing system-aware DPI policy and must be runtime-tested.
- Settings values, save logic, theme selection and existing control coordinates remain unchanged; the content is now a scrollable virtual surface rather than a tall fixed-height window.

Windows runtime validation remains mandatory:
- [ ] Build with MSVC and confirm the new Direct2D translation API links from C to C++.
- [ ] Verify at 100%, 125%, 150% and 200% scaling.
- [ ] Verify a short laptop display (including taskbar visible), a narrow portrait/4:3 display, and an ultrawide display.
- [ ] Verify the window can be resized smaller/larger, moved between monitors, and reopened after moving partly off-screen.
- [ ] Verify vertical and horizontal scrollbar thumb, line/page commands, mouse wheel over both canvas and edit fields, Page Up/Down, Ctrl+Home/End, and Tab focus auto-reveal.
- [ ] Verify all settings, theme presets, rate dropdown, Browse/Open cache, Check for updates, Save and Close actions at scroll offsets 0, middle and maximum.
- [ ] Verify no child EDIT field becomes detached from its painted frame while scrolling or resizing.

Do not mark this as runtime-validated until the above checks have been run on Windows.


## Playback & Output — premium Media Collection drawer

- [ ] Verify the MEDIA COLLECTION heading, count capsule and Clear action remain aligned at compact and expanded drawer heights.
- [ ] Verify search focus border, typed query and placeholder do not clip or overlap.
- [ ] Verify Queue and Library tabs retain correct active state and both remain clickable.
- [ ] Verify All, Audio and Video filters and Group cycling retain their existing behavior.
- [ ] Verify track cards show square centered fallback artwork, readable title and metadata, and non-overlapping AUDIO/VIDEO pills.
- [ ] Verify active, selected and hovered track states are distinguishable without departing from the global ABM palette.
- [ ] Verify the empty-state card appears only when no filtered results exist and its message matches empty queue, search miss or filter miss.
- [ ] Verify scrolling, scrollbar dragging, track selection, double-click playback and Clear action still work.
- [ ] Build with MSVC and verify at 100%, 125%, 150% and 200% scaling, plus a short-height window.

Do not mark this work runtime-validated until the Windows build and interaction checks above pass.


## Personal Media Libraries and Favourites

- [ ] Verify every media card displays a clearly visible heart control in both Queue and Library tabs.
- [ ] Verify heart hit targets do not overlap the type pill, title/metadata, active-track visualiser or row selection/playback hit target.
- [ ] Add and remove favourites; verify filled/outline states update immediately and remain consistent after closing/reopening the drawer.
- [ ] Restart the application and confirm favourites reload from `favorites.m3u`.
- [ ] Clear the playback queue and confirm favourites remain intact; reopen Favourites and confirm saved paths are restored into the active media list.
- [ ] In Library, switch between All tracks and Favourites; verify count, empty state, search, Audio/Video filters and scrolling remain correct.
- [ ] In Queue, verify Folder/Artist/Album grouping still cycles as before.
- [ ] Right-click a media row and verify Add/Remove from favourites reflects the current state.
- [ ] Save a named personal library; confirm the M3U is created under `%APPDATA%\\ABMAVPlayer\\collections\\` and the active queue is unchanged.
- [ ] Save two differently named collections; verify neither overwrites the other.
- [ ] Load each saved collection and confirm the active queue is replaced with its paths, playback selection is reset safely, and all other saved collections and favourites remain unchanged.
- [ ] Test missing/unavailable paths in a saved collection; confirm the entries remain visible and are flagged Missing rather than pruned.
- [ ] Relink a missing file; confirm Queue, Library, Favourites, Recent and every saved personal collection reference is updated without duplicate entries.
- [ ] Verify a missing favourite remains in Favourites after restarting the application and can be relinked from its row context menu.
- [ ] Remove or rename a source file while the app is open; reopen the drawer and confirm Missing status is refreshed without filesystem work during painting or scrolling.
- [ ] Verify Relink is disabled for healthy media, opens a native file picker for missing media, and handles Cancel and invalid targets safely.
- [ ] Verify Unicode filenames and collection names, paths containing spaces, overwrite confirmation, cancel, and read/write failure handling.
- [ ] Build with MSVC and test at 100%, 125%, 150% and 200% scaling, plus compact drawer height. Confirm heart glyph fallback and no text overlap.
- [ ] Re-run queue drag/drop, click-to-play, double-click playback, context-menu remove/reorder, wheel scrolling, and Clear regression checks.

Do not mark this feature runtime-validated until all Windows build and interaction checks above pass.


## Help Centre documentation regression

- [ ] Open F1 Help and confirm the topic index includes **LIBRARY, CONTINUE WATCHING AND RESUME** and **MISSING MEDIA AND RELINKING**.
- [ ] Select each new topic and verify the article heading/content match the topic label and scroll correctly.
- [ ] Search for `resume.tsv`, `MISSING MEDIA` and `Relink missing media`; verify matching topics open, the match is highlighted and F3/Enter continue searching with wraparound.
- [ ] Confirm the article explains the completion threshold, local checkpoint location, missing-path retention and the fact that relinking updates references rather than moving files.

## Continue Watching and Resume Playback

- [ ] Play a local audio file and a video for more than 10 seconds; verify progress is checkpointed to `%APPDATA%\\ABMAVPlayer\\resume.tsv` without blocking the 33 ms UI timer.
- [ ] Pause playback and close the application; relaunch and verify the same item is marked CONTINUE WATCHING in Library.
- [ ] Play a resumable item and verify LibVLC seeks to the saved position after entering Playing state.
- [ ] Switch between media items and outputs; verify the previous local item position is saved and existing receiver selection / cast failure isolation remains unchanged.
- [ ] Verify the Library filter cycles All tracks → Favourites → Continue Watching → All tracks, with correct counts, search, media filters and empty state.
- [ ] Verify Continue Watching rows display an accurate saved-progress percentage and remaining duration, including long-form media formatting.
- [ ] Verify completed items (at or above 95% duration) are removed from Continue Watching and replay from the beginning.
- [ ] Verify short items, unknown-duration media, live/torrent playback and failed playback do not create invalid resume records.
- [ ] Force-close during playback and verify the last periodic checkpoint is restored; confirm normal exit flushes the final position.
- [ ] Confirm resume writes are atomic (temporary file + replace), bounded to the library capacity and do not run file I/O on the UI timer.

Do not mark Continue Watching or missing-media recovery runtime-validated until all Windows build and interaction checks above pass.
