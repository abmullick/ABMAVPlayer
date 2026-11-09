# ABM AV Player — Product Hardening Test Plan

Run these checks on a Windows 11 x64 machine using the exact release build and installer. Record the Windows version, VLC runtime version, WebView2 Runtime version, application version and test result. A source change or successful compile is not a substitute for these tests.

## 1. WebView2 runtime and startup

- [ ] Launch the player with the Evergreen WebView2 Runtime installed. Open Streaming Centre and verify all five provider cards load.
- [ ] Verify the runtime version can be queried and that the browser profile is created under `%LOCALAPPDATA%\ABMAVPlayer\WebView2`.
- [ ] On a disposable Windows test environment, remove/disable the WebView2 Runtime and launch Streaming Centre. Verify the player remains usable and displays a clear instruction to install the official Microsoft Edge WebView2 Evergreen Runtime.
- [ ] Reinstall the runtime and verify Streaming Centre starts normally without reinstalling ABM AV Player.
- [ ] Verify a normal browser navigation failure does not close the main player.

## 2. WebView2 crash recovery

Use a disposable test profile. Do not terminate browser processes on a machine where other WebView2 applications have unsaved work.

- [ ] Open a provider page and sign in with a test account.
- [ ] Use Task Manager or a controlled test harness to terminate the WebView2 renderer/browser process associated with this app.
- [ ] Verify the Streaming Centre detects the process failure, recreates its WebView2 controller and returns to a usable page.
- [ ] Verify an ordinary HTTP(S) page is restored when possible. Confirm internal app pages and script URLs are not replayed.
- [ ] Repeat the crash several times. After the bounded automatic recovery attempts are exhausted, verify a clear message asks the user to close and reopen Streaming Centre rather than entering an endless restart loop.
- [ ] Close Streaming Centre during startup/recovery and verify no delayed callback recreates a window or crashes the main process.
- [ ] Verify fullscreen is exited cleanly if a process failure occurs while fullscreen.

## 3. Memory and performance soak

- [ ] Open and close Streaming Centre at least 20 times.
- [ ] Switch between all five services repeatedly and navigate back/forward/reload.
- [ ] Leave a streaming page open for at least 60 minutes, then repeat navigation and playback controls.
- [ ] Record ABMAVPlayer.exe and WebView2 subprocess memory at start, after 15 minutes, after 60 minutes and after closing Streaming Centre.
- [ ] Confirm memory does not grow continuously across repeated open/close cycles. Allow for normal browser cache and provider-page variation; compare equivalent idle states.
- [ ] Confirm CPU usage returns to a low idle level when the page is idle and no repaint/animation loop is running.
- [ ] Confirm all native GDI/GDI+ resources are released after closing the Streaming Centre.

Suggested evidence: Task Manager screenshots or Process Explorer working-set/private-bytes samples, plus Windows Reliability Monitor/Event Viewer entries if a failure occurs.

## 4. Clean installer test

- [ ] Build the x64 application and run `prepare_installer.bat` from the repository root.
- [ ] Verify staging fails when a required service logo, creator portrait, LibVLC DLL or required Cast plugin is missing.
- [ ] Verify the staged folder contains `ABMAVPlayer.exe`, `libvlc.dll`, `libvlccore.dll`, the VLC plugins folder, all five service logos and `assets\owner_portrait.bmp`.
- [ ] Compile the installer with Inno Setup.
- [ ] Install on a clean Windows test account with the Evergreen Runtime present.
- [ ] Launch from the Start Menu shortcut; verify local playback, Streaming Centre, Help/About artwork and Google Cast discovery.
- [ ] Test with the Evergreen Runtime absent and confirm the main player still opens while Streaming Centre provides the runtime instruction.
- [ ] Verify the firewall task is optional and only applies the intended Private-network discovery rules.

## 5. In-place upgrade and uninstall

- [ ] Install the previous released version and create representative user data: preferences, recent files, playlists/library state and a signed-in WebView2 test session.
- [ ] Close the app normally, then run the new installer over the existing installation.
- [ ] Also test launching the installer while the player is running; confirm the installer closes the app cleanly before replacing files.
- [ ] Verify the install directory is updated and the Start Menu/desktop shortcuts still target the installed executable.
- [ ] Verify preferences, playlists/library state and the WebView2 profile survive the upgrade.
- [ ] Verify the app launches and plays media after the upgrade.
- [ ] Uninstall and verify user data under `%APPDATA%\ABMAVPlayer` and `%LOCALAPPDATA%\ABMAVPlayer\WebView2` is preserved as documented.
- [ ] Reinstall and verify the preserved data is usable.
- [ ] Test upgrade from at least one earlier supported release and document any version-specific migration issue.

## 6. Release gate

Do not publish a release until all applicable checks pass. Attach the test record to the release review and record any skipped tests with a reason. A successful build or installer compile alone does not establish crash recovery, runtime compatibility, memory stability or upgrade safety.
