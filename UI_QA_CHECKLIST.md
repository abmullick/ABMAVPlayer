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
| DPI awareness | The application now declares Per-Monitor V2 in its embedded manifest, requests Per-Monitor V2 with Per-Monitor V1/system-aware fallbacks before creating HWNDs, and applies `WM_DPICHANGED` suggested bounds in the main player, Settings, Advanced Settings, Torrent, Help, About, Equalizer, recent popup, fullscreen overlay and Streaming Centre windows. Shared scaling helpers are in `src/ui_dpi.h`. This is the DPI-awareness and window-transition foundation; per-surface layout/control/font scaling still requires staged migration and runtime verification. |
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
- [ ] Settings and Torrent Stream windows: controls remain reachable and labels do not overlap.
- [ ] Streaming Centre: title bar, device panel, receiver/app lists and WebView2 region remain aligned.
- [ ] Branded dialogs and custom menus: fit within the work area at every screen edge; long text and long menus remain usable.
- [ ] Move the main player, Settings, Torrent Stream, Streaming Centre and auxiliary windows between monitors with different scaling factors; record any size or coordinate jump.
- [ ] Verify each window's custom-drawn geometry, native child controls, fonts, hit targets and popup placement are scaled/re-laid out correctly; applying the suggested outer rectangle alone is not sufficient for sign-off.
- [ ] Main player: verify Direct2D content, partial repaint regions, mouse hit targets, fullscreen transport and LibVLC child-video bounds at 100%, 125%, 150% and 200%.
- [ ] Confirm the manifest is embedded in the built EXE and the runtime reports Per-Monitor V2 awareness.
- [ ] Test taskbar auto-hide, non-primary monitor placement and display resolution changes.

## 4. Keyboard and accessibility

- [ ] Operate the main window without a mouse: navigate available controls, open/close the drawer, select media and operate playback.
- [ ] Verify visible focus on every interactive control and a logical Tab/Shift+Tab order.
- [ ] Branded Yes/No dialogs: Tab, Shift+Tab, Left/Right, Enter, Space and Escape; confirm default-No remains safe.
- [ ] OK dialogs: Enter and Escape behave consistently.
- [ ] Custom menus: Up/Down, Enter, Escape and mouse wheel; focus returns to the invoking control after close.
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


- [ ] Advanced Settings at 100%, 125%, 150% and 200%: verify the 1080 × 780 logical design remains proportionate, navigation rows and every option hit target align, and the diagnostic preview edit/font remains aligned and readable.
- [ ] Move Advanced Settings between monitors with different scale factors; verify suggested window bounds, Direct2D rendering, child EDIT placement/font and resize behaviour update without clipping or stale geometry.
- [ ] Verify Advanced Settings minimum tracking size remains usable at each tested DPI and that all existing settings actions still behave identically.


- [ ] Standard Settings at 100%, 125%, 150% and 200%: verify all six EDIT controls align with their Direct2D frames and labels, with readable scaled Segoe UI text.
- [ ] Verify every Settings hit target after scaling: playback-rate dropdown, toggles, browse/open buttons, update button, theme presets, Save settings, Close and Advanced Settings.
- [ ] Move Settings between monitors with different DPI values; verify the dialog bounds, Direct2D rendering, child EDIT positions/sizes and fonts update without drift or clipping.
- [ ] Verify volume, cache paths, download path, quota and bandwidth fields retain their values and save exactly as before the DPI migration.


- [ ] Torrent Stream at 100%, 125%, 150% and 200%: verify Direct2D layout, every child control, sliders, owner-drawn buttons, combo/list item heights and fonts remain aligned and usable.
- [ ] Move Torrent Stream between monitors with different DPI; verify child HWNDs are repositioned, font is recreated, render target updates, minimum size remains sensible and torrent/casting actions are unaffected.
- [ ] About: verify portrait, all text blocks, divider, version line and minimum size at each scale; move between monitors and check no clipping.
- [ ] Equalizer: verify all ten band sliders, preamp, labels, preset hit targets and drag behaviour at each scale; move between monitors while open.
- [ ] Help Centre: verify search field, Find button, topic list, rich-text article, scrollbars, row heights, title-bar drag/close hit targets and Ctrl+F/F3 at each scale and after monitor transitions.
- [ ] Branded modal dialogs: verify Yes/No/OK button positions, keyboard focus, message wrapping and click hit targets at each scale; test long messages and both default-button choices.
- [ ] Custom action menus and Recently Played popup: verify row hit targets, hover, keyboard navigation, scrolling, fonts and work-area edge clamping at each scale.
- [ ] Fullscreen overlay on monitors with different DPI: verify it remains aligned to the fullscreen monitor and playback controls retain correct input routing.
