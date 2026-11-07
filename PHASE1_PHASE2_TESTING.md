# Phase 1 and Phase 2 validation checklist

These changes have been committed to the feature branch, but must be built and exercised on Windows before they are considered verified.

## Phase 1 — playback and hardware decoding

1. Build x64 from the Visual Studio Native Tools command prompt using the matching x64 LibVLC SDK.
2. Start normally. LibVLC automatic hardware decoding is now the default; the --avcodec-hw=none override has been removed.
3. Test H.264 1080p and 4K files, then HEVC 1080p and 4K files where the GPU/driver supports the codec.
4. During playback, check Task Manager GPU Video Decode activity and CPU utilisation. A missing Video Decode graph does not necessarily mean failure: support depends on codec, GPU, driver and VLC build.
5. If a driver or decoder causes instability, set the process environment variable ABM_VLC_HW=none and relaunch to force software decoding.
6. To explicitly request D3D11VA, set ABM_VLC_HW=d3d11va; remove the variable to return to automatic selection.
7. Test pause/resume, seeking, next/previous, repeat, stop, fullscreen, audio-only files and Cast separately.
8. Test at least one older/unsupported codec to confirm LibVLC falls back or reports a useful playback failure.

## Phase 2 — audio processing

Right-click the video while media is playing:

- ABM graphic EQ: Flat, Bass boost, Vocal clarity and Treble boost. The profile is applied immediately and remembered in %APPDATA%\\ABMAVPlayer\\audio.ini.
- ReplayGain track normalisation: toggles track-mode ReplayGain for media opened after the setting is changed. Existing tracks must be reopened.
- Dynamic-range compressor: toggles VLC's compressor audio filter for media opened after the setting is changed. Existing tracks must be reopened. Compressor support and exact behaviour depend on the VLC build/plugins.

Listen at matched perceived loudness when comparing EQ curves. Begin with conservative settings; bass/treble boosts can reduce headroom and may cause clipping on some material or output devices.

## Known boundaries

- The ABM profiles are fixed-band graphic EQ curves using LibVLC's equalizer API, not a fully parametric EQ with adjustable centre frequency and Q.
- ReplayGain only helps when the media contains usable ReplayGain metadata.
- The compressor is not a guaranteed true-peak brickwall limiter.
- The visualizer continues to capture the Windows default render mix. Other applications can therefore contribute to its display.
- No audio-device selector or exclusive/bit-perfect output path is added in this phase.

## Report back

For each test, record Windows version, GPU model/driver, VLC/LibVLC version, media codec/container, resolution, whether hardware decoding was active, and any playback or audio symptoms. This is needed to distinguish an ABM issue from a codec/driver/plugin limitation.

## Google Cast / Android TV receiver validation

1. Use a Google TV / Chromecast device with its built-in Cast receiver enabled. Confirm the PC and TV are on the same non-guest LAN.
2. Start discovery and verify the TV appears. Repeat after stopping and restarting discovery / the app.
3. Start a local H.264 MP4, switch from local output to the TV, and verify playback resumes near the previous timestamp.
4. Pause locally, switch to the TV, and verify the remote playback remains paused. Resume and seek on the TV.
5. Switch from the TV back to local output and verify position and play/pause state are restored.
6. During remote playback, turn off the TV or disconnect Wi-Fi. Verify the app does not crash and attempts local playback.
7. Try an unsupported container/codec and a file with subtitles. Record whether VLC transcodes, rejects the stream, or falls back locally.
8. Test a non-Google Android TV separately. Record the renderer protocol/service name; Android TV branding alone does not imply Chromecast receiver support.
9. Test discovery with Windows Defender Firewall enabled on a Private network, then with VPN/client isolation disabled if the TV is not discovered.

## Product-hardening validation

- Select a playlist entry whose file has been deleted or moved. The app should show a clear warning and remain usable.
- Play a corrupt or unsupported file. The player should return to a usable stopped/error state rather than repeatedly advancing or crashing.
- Fill the playlist to its 256-item limit, then drag/drop more files. Confirm no out-of-bounds additions occur.
- Switch output repeatedly while playing and while paused. Confirm no duplicate playback, stuck state, or stale seek restoration.
- Disconnect and reconnect the receiver while discovery is active. Confirm the renderer list remains usable.
- Close the app during local playback and during Cast playback. Confirm clean shutdown and no stuck VLC process.

These checks are manual. No Windows build or runtime validation has been performed by this change set yet.
