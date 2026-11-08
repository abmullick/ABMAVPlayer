# Phase 1 and Phase 2 validation checklist

This document is a manual Windows validation checklist. A source change or successful compilation alone does not verify runtime behaviour.

## Phase 1 — playback and hardware decoding

1. Build x64 from the Visual Studio Native Tools command prompt using the matching x64 LibVLC SDK.
2. Start normally. LibVLC automatic hardware decoding is the default.
3. Test H.264 1080p and 4K files, then HEVC 1080p and 4K where supported by the GPU/driver.
4. Check Task Manager GPU Video Decode activity and CPU use. A missing Video Decode graph does not necessarily mean failure; support depends on codec, GPU, driver and VLC build.
5. If a driver/decoder causes instability, set ABM_VLC_HW=none and relaunch to force software decoding.
6. To request D3D11VA explicitly, set ABM_VLC_HW=d3d11va; remove the variable to return to automatic selection.
7. Test pause/resume, seeking, next/previous, repeat, stop, fullscreen, audio-only files and Cast separately.
8. Test an unsupported/corrupt file and confirm the application remains usable and reports the failure.

## Phase 2 — audio processing

Right-click the video while media is playing and test the available options:

- ABM graphic EQ profiles: Flat, Bass boost, Vocal clarity and Treble boost. Confirm that changes apply and are remembered in %APPDATA%\ABMAVPlayer\audio.ini.
- ReplayGain track normalisation: changes apply to media opened after the setting changes; reopen the current track to test.
- Dynamic-range compressor: changes apply to media opened after the setting changes; reopen the current track to test.
- Audio-track selection, subtitle selection/disable and external subtitle loading.
- Media information dialog when metadata is available.

Listen at matched perceived loudness when comparing EQ curves. Begin conservatively; boosts can reduce headroom and may cause clipping. ReplayGain needs usable track tags. The compressor is not a guaranteed true-peak limiter.

## Google Cast / Android TV receiver validation

1. Use a Google TV / Chromecast device with its Cast receiver enabled. Confirm the PC and TV are on the same non-guest LAN.
2. Start discovery and verify the TV appears. Repeat after stopping/restarting discovery and after restarting the app.
3. Start a local H.264 MP4, switch output from local to the TV and verify playback resumes near the previous timestamp.
4. Pause locally, switch to the TV and verify paused/playing state restoration; resume and seek.
5. Switch from TV back to Local playback and verify position/state restoration.
6. Turn off the TV or disconnect Wi-Fi during remote playback. Verify the app remains usable and reports the failure.
7. Try an unsupported container/codec and a file with subtitles. Record whether VLC transcodes or rejects the stream.
8. Test a non-Google Android TV separately and record the renderer protocol/service name.
9. Test with Windows Defender Firewall enabled on a Private network; if discovery fails, investigate VPN, guest Wi-Fi, client isolation and mDNS.

## Product-hardening validation

- Select a playlist entry whose file has been deleted/moved.
- Play a corrupt or unsupported file.
- Fill the playlist to its 256-item limit, then drag/drop more files.
- Switch output repeatedly while playing and paused.
- Disconnect/reconnect a receiver while discovery is active.
- Close the app during local and Cast playback.
- Test Help window scrolling/resizing and About window portrait loading.
- Verify menu order, keyboard navigation, shortcut labels and menu actions after changes.

## Record test environment

For each test, record Windows version, GPU model/driver, VLC/LibVLC version, media codec/container, resolution, whether hardware decoding was active, receiver model/protocol and any playback/audio symptoms.

These checks are manual. Do not mark a feature verified until it has been exercised on the target Windows/VLC configuration.
